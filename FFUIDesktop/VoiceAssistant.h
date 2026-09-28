#pragma once

#include <chrono>
#include <thread>
#include <stop_token>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <string>

#include "AudioCapture.h"
#include "WhisperTranscriber.h"
#include "ClaudeCliClient.h"
#include "AssistantSetup.h"
#include "SetupConfirmationPrompt.h"
#include "mathTypes.h"

//Gesture states for the AUX (bit 7 / "side") button. A plain press-and-hold with no
//preceding tap starts in Held_PullMode (the ambiguous window - see its own comment below) and,
//once genuinely committed to as a plain hold, moves to DictatingMode - "swap the aux key
//function to a 'dictate'... hold the Aux button to activate the dictate function", per the
//request. A quick tap followed immediately by a second press-and-hold instead enters
//ListeningMode (if the assistant is already set up) or AwaitingSetupConfirmation (if it isn't -
//see updateAuxButton()'s commit-point logic). A quick tap with NO follow-up (ArmedAfterTap timing
//out) resolves to a Narrate request instead - "tap to narrate, hold to dictate, tap+hold for the
//AI assistant... keep the back button to right click", per the request that moved Narrate off the
//rear button (which used to double as both Narrate AND an OS right-click - see FFUIDesktop.cpp's
//own comment on that former overlap) and onto AUX's own already-idle "just a tap" case. See
//VoiceAssistant.cpp's updateAuxButton() for the full transition table, and tryConsumeTapNarrateEdge()
//below for how that resolution is actually delivered to FFUIDesktop.cpp.
enum class AuxGestureState {
	Idle,

	//The ambiguous window every AUX press starts in - held for up to TAP_MAX (200ms) before
	//either being released (possibly the start of a tap-then-hold ListeningMode gesture - see
	//ArmedAfterTap) or, if still held past TAP_MAX, committed to as a genuine plain hold and
	//promoted to DictatingMode below. FFUIDesktop.cpp no longer renders any force off this state
	//itself - the "pull force toward nearest object" behavior it used to drive directly
	//(calculateForceToClosestObject()) is deactivated (kept, not deleted - "we may remap this
	//later", per the request), not moved to DictatingMode or anywhere else. Deliberately NOT
	//renamed despite no longer driving pull-mode force: this is still fundamentally "the
	//ambiguous hold-vs-tap window", not "dictate mode" itself - see DictatingMode below for why
	//dictation only starts once that ambiguity is actually resolved.
	Held_PullMode,

	ArmedAfterTap,
	PendingGestureDecision,
	ListeningMode,

	//A committed plain AUX hold (past TAP_MAX, with no tap beforehand) - captures the microphone
	//via the exact same AudioCapture/WhisperTranscriber pipeline ListeningMode uses, but skips
	//the Claude CLI ask/spoken-reply half entirely; see beginDictating()/runDictation(). Entered
	//only once Held_PullMode has been held past TAP_MAX (NOT the instant AUX is first pressed -
	//every AUX press starts in Held_PullMode, including the first TAP_MAX of what might turn
	//out to be a tap-then-hold ListeningMode gesture, so starting capture any earlier would fire
	//a spurious recording on every single tap of that other gesture, racing the same shared
	//AudioCapture/WhisperTranscriber instance ListeningMode itself needs moments later).
	DictatingMode,

	AwaitingSetupConfirmation   //two-detent confirm/cancel prompt - see SetupConfirmationPrompt
};

//Which of the two capture-and-transcribe pipelines a given AUX gesture is for - both share the
//same physical record/stop machinery in workerLoop() (there's only one AudioCapture/
//WhisperTranscriber pair - only one thing can be recording at a time), diverging only in what
//happens once transcription finishes: Ask goes on to ClaudeCliClient::ask() + a spoken reply
//(runExchange()); Dictate just delivers the recognized text via the mailbox below
//(runDictation()) - no Claude call, no spoken reply beyond a "didn't catch that" retry prompt.
enum class CaptureKind {
	Ask,
	Dictate
};

//Which of AssistantSetup's two independent setup sequences a confirmed two-detent prompt should
//kick off - Speech runs only AssistantSetup::runSpeechOnly() (whisper.cpp alone, no Claude CLI or
//login), FullAssistant runs the existing AssistantSetup::run() (CLI install + login + whisper).
//Dictation's own gesture (Held_PullMode -> DictatingMode) only ever needs Speech; the
//tap-then-hold AI-assistant gesture always needs FullAssistant, since asking Claude anything
//still requires the CLI and a signed-in account regardless of whether speech happens to already
//be provisioned. Set by updateAuxButton() just before starting confirmationPrompt (from whichever
//gesture triggered it), read by commitSetupConfirmation() to route requestSetupStart() correctly
//once the user actually confirms - there's no other way for that method to know which of the two
//gestures asked for the prompt in the first place.
enum class SetupKind {
	Speech,
	FullAssistant
};

//Hold-to-talk AI voice assistant. Owns the AUX-button gesture detector (safe to call every
//haptic frame - pure bookkeeping, no I/O) and a persistent worker thread that does the actual
//blocking work: play a cue, record the microphone, transcribe locally via whisper.cpp (see
//WhisperTranscriber.h - this replaced SAPI's built-in recognizer once real-world testing showed
//its accuracy too weak to rely on), ask Claude via the Claude Code CLI, and speak the reply back
//through pAssistantVoice.
//
//See the project's voice-assistant plan (written before this was implemented) for the full
//design rationale, the risks called out there, and the pre-flight checklist worth running
//through before relying on this in practice (hand-testing the composed `claude` command line,
//auditing %USERPROFILE%\.claude\ for global hooks/MCP servers).
class VoiceAssistant {
public:
	VoiceAssistant() = default;
	~VoiceAssistant() = default;

	//Starts the persistent worker thread. Call once, from FFUIDesktop::initDesktop() after
	//pSapiVoice/pAssistantVoice have been created - mirrors how scannerThread is started there
	//today, and avoids the worker thread being "live" before the TTS voice it eventually speaks
	//through actually exists.
	void start();

	//Feeds this frame's AUX button state and the stylus's current position into the gesture
	//detector and returns the resulting state. Call every haptic frame, from the haptic thread
	//only. Mostly pure bookkeeping (never blocks) - the one exception is that entering or
	//updating AwaitingSetupConfirmation calls pAssistantVoice->Speak() directly on this thread
	//(safe: pAssistantVoice was created on this same thread in initDesktop(), so this is a
	//same-apartment call, not a new cross-thread COM pattern) - see SetupConfirmationPrompt's
	//own comments. That only happens on the occasional frame where the confirmation prompt's
	//selection actually changes, not every frame.
	AuxGestureState updateAuxButton(bool auxPressedNow, Location currentLoc);

	//Call every haptic frame while updateAuxButton() has returned AwaitingSetupConfirmation, in
	//place of the normal processForces()/pull-mode force for that frame entirely (not summed
	//with it) - see FFUIDesktop::updateFrame()'s force block.
	Vector3 getConfirmationForce(Location currentLoc) { return confirmationPrompt.updateForce(currentLoc); }

	//Commits whichever option is currently selected in the two-detent prompt and returns to
	//Idle. Call from the haptic thread only, when the front/select button is pressed while
	//updateAuxButton() has returned AwaitingSetupConfirmation - see FFUIDesktop::updateFrame()'s
	//front-button handling. AUX is deliberately not involved in this decision at all (matches
	//how "select" works everywhere else in the app via the front button, not AUX); a no-op if
	//called while not actually in AwaitingSetupConfirmation.
	void commitSetupConfirmation();

	//Whether the self-configuring setup (CLI install, login, English speech recognition) has
	//finished. Safe to call from the haptic thread - a plain atomic load. Not currently
	//consulted there directly (updateAuxButton() reads assistantSetup.state itself at its
	//commit point) but exposed here as the natural place for any other caller that needs it.
	SetupState setupState() const { return assistantSetup.state.load(); }

	//Speech-only counterpart to setupState() above - whether dictation alone (no Claude CLI/
	//login) is provisioned. See SpeechSetupState's own comment in AssistantSetup.h.
	SpeechSetupState speechSetupState() const { return assistantSetup.speechState.load(); }

	//Whether AssistantSetup::run() or runSpeechOnly() (kicked off by requestSetupStart() after
	//the user confirms the two-detent prompt) or the reactive redoLogin() (triggered by an
	//expired-login error mid-exchange - see runExchange()) is actively running right now, anywhere
	//between having started and reaching its respective Ready or Failed. A plain atomic load,
	//safe to call from any thread - in particular both the haptic thread and the periodic
	//UI-scanner thread, neither of which is the worker thread that actually drives AssistantSetup.
	//Folds in BOTH state and speechState, since the two setup sequences are now fully independent
	//(see SetupKind's own comment) and either one alone should still pause UI/duck the narrator
	//while it's running - e.g. a speech-only whisper download in progress is just as much "the
	//assistant has the user's attention right now" as a full CLI install/login is.
	//
	//FFUIDesktop uses this (folded into assistantHasFloor() below) to pause per-frame haptic UI
	//force and duck FFUI's own narrator while setup has the assistant's attention (see
	//FFUIDesktop.cpp's initiatePeriodicScanner() and updateFrame()), so competing haptic
	//sensations and a loud FFUI narration don't fight for the user's attention while they're
	//mid-interaction with a guided login or similar. Cursor movement and button/click forwarding
	//are deliberately NOT gated by this - the user still needs to be able to move the mouse and
	//click while this is true.
	bool isSetupInProgress() const {
		SetupState s = assistantSetup.state.load();
		bool fullBusy = s != SetupState::NotStarted && s != SetupState::Ready && s != SetupState::Failed;

		SpeechSetupState ss = assistantSetup.speechState.load();
		bool speechBusy = ss != SpeechSetupState::NotStarted && ss != SpeechSetupState::Ready && ss != SpeechSetupState::Failed;

		return fullBusy || speechBusy;
	}

	//True whenever the assistant currently has (or is about to have) pAssistantVoice's
	//attention - the union of isSetupInProgress() above, exchangeInProgress (an ordinary
	//hold-to-talk Q&A, from the listening cue through the spoken reply finishing), and
	//confirmationPromptActive (the two-detent confirm/cancel prompt, from its opening question
	//through its closing "Okay, not enabling..."/the moment setup itself takes over). A plain
	//atomic-load composite, safe from any thread.
	//
	//Two things read this: FFUIDesktop's haptic frame loop zeroes its per-frame UI-attraction
	//force while it's true (see updateFrame()'s force block), and the periodic UI scanner ducks
	//pSapiVoice (FFUI's own narrator, entirely separate from pAssistantVoice - see
	//FFUIDesktop::initDesktop()) to 50% volume rather than 100% while it's true, restoring it
	//once it goes false (see initiatePeriodicScanner()). Scanning and FFUI's own window-focus
	//narration are deliberately NOT paused any more - now that the two narrators are genuinely
	//independent SAPI voice instances, one's SPF_PURGEBEFORESPEAK can't purge the other's queue,
	//so there's no clipping risk left to avoid by pausing; ducking is purely about keeping the
	//assistant clearly in the foreground while it's likely talking.
	bool assistantHasFloor() const {
		return isSetupInProgress() || exchangeInProgress.load() || confirmationPromptActive.load();
	}

	//Polls the dictation mailbox once - true (and fills outText) exactly once per completed
	//dictation, the first call after it lands; false every other call. Call every haptic frame
	//from FFUIDesktop::updateFrame(), mirroring how WindowManager::hasPendingExplicitFocus/
	//pendingExplicitFocusHandle is polled once per scan cycle. Whoever is actually waiting on a
	//result (StartMenuFlow, while AwaitingDictation - see its own header comment) consumes it
	//here; if nothing is currently waiting when this fires, the caller is expected to just log
	//and discard it - dictation has no defined behavior of its own outside of whatever flow
	//requested it. Safe to call from the haptic thread only (matches every other haptic-thread-
	//only method on this class); runDictation() (worker thread) is the sole producer, guarded by
	//the same dictationResultMutex.
	bool tryConsumePendingDictation(std::wstring& outText) {
		std::lock_guard<std::mutex> lock(dictationResultMutex);
		if (!hasPendingDictationResult) return false;
		outText = pendingDictationResult;
		pendingDictationResult.clear();
		hasPendingDictationResult = false;
		return true;
	}

	//True for exactly the one updateAuxButton() call where a plain AUX tap (release within
	//TAP_MAX, with no second press arriving before TAP_TO_HOLD_WINDOW elapses) was just resolved -
	//i.e. the caller should trigger Narrate now. Call once per haptic frame, immediately after
	//updateAuxButton() - same "poll once, consume, clear" shape as tryConsumePendingDictation()
	//above, but no mutex is needed here: this is haptic-thread-only state, set inside
	//updateAuxButton() (also haptic-thread-only) and always polled from that same thread, same as
	//every other member in the "Gesture detection state" block below.
	bool tryConsumeTapNarrateEdge() {
		bool result = tapNarrateEdge;
		tapNarrateEdge = false;
		return result;
	}

private:
	//--- Gesture detection state - touched only from the haptic thread ---
	AuxGestureState state = AuxGestureState::Idle;
	std::chrono::steady_clock::time_point stateEnteredTime{};
	SetupConfirmationPrompt confirmationPrompt;  //haptic-thread-only, same as the rest of this block

	//Set true, for exactly one frame, the instant ArmedAfterTap's TAP_TO_HOLD_WINDOW elapses with
	//no second AUX press - i.e. a plain tap just resolved. Consumed via tryConsumeTapNarrateEdge()
	//above (public, so FFUIDesktop.cpp can poll it right after calling updateAuxButton()).
	bool tapNarrateEdge = false;

	//Which setup sequence the currently-open (or most recently closed) confirmation prompt is
	//for - set by updateAuxButton() the instant it calls confirmationPrompt.start(), read by
	//commitSetupConfirmation() to route requestSetupStart() correctly. Haptic-thread-only, same
	//as the rest of this block - the worker thread only ever sees its value indirectly, already
	//baked into the SetupKind passed to requestSetupStart().
	SetupKind pendingSetupKind = SetupKind::FullAssistant;

	static constexpr std::chrono::milliseconds TAP_MAX{ 200 };

	//How long after releasing a tap (NOT from the original press - ArmedAfterTap's
	//stateEnteredTime is set at the moment of release, see updateAuxButton()'s own Held_PullMode
	//case) a second AUX press must arrive to be read as tap-then-hold (the AI assistant gesture)
	//rather than a plain, standalone tap (Narrate). Shortened from an original 500ms - "could we
	//reduce the window to 250ms, and time it from the release of the aux button?" - trading a
	//little more required precision on the deliberate tap-then-hold gesture for a snappier Narrate
	//response, since Narrate can't speak until this window has fully elapsed with nothing else
	//arriving (see ArmedAfterTap's timeout branch).
	static constexpr std::chrono::milliseconds TAP_TO_HOLD_WINDOW{ 250 };

	void beginListening();   //haptic thread: signals the worker and returns immediately
	void endListening();     //haptic thread: signals the worker and returns immediately

	//Haptic thread: signals the worker to run either AssistantSetup::runSpeechOnly() or the full
	//run(), depending on `kind` - see SetupKind's own comment - and returns immediately.
	void requestSetupStart(SetupKind kind);

	//Haptic thread: signals the worker exactly like beginListening()/endListening() (same
	//captureRequested flag, same physical record/stop machinery - see CaptureKind's own
	//comment), but first sets captureKind to Dictate so workerLoop() routes to runDictation()
	//instead of runExchange() once capture stops. Called from updateAuxButton() the instant
	//Held_PullMode commits to DictatingMode (see that transition's own comment) - deliberately
	//NOT the instant AUX is first pressed. Does NOT play the listening cue itself (AudioCapture's
	//Beep()-based cues are worker-thread-only, never the haptic thread) - workerLoop() plays
	//AudioCapture::playDictateListeningCue() itself, the moment it actually picks up the request,
	//mirroring exactly how runExchange()'s own playListeningCue() call works today.
	void beginDictating();
	void endDictating();     //haptic thread: signals the worker and returns immediately

	void workerLoop(std::stop_token stoken);
	void runExchange(std::stop_token stoken);     //worker thread: stop capture, then recognize -> ask -> speak

	//Worker thread: stop capture, transcribe, then deliver the result via the mailbox below -
	//no Claude CLI call, no spoken reply. Mirrors only the first half of runExchange() (up
	//through whisperTranscriber.transcribe()); an empty transcription still speaks "Sorry, I
	//didn't catch that." through pAssistantVoice (matching runExchange()'s own empty-result
	//handling) rather than delivering anything via the mailbox.
	void runDictation(std::stop_token stoken);

	//Speaks text through pAssistantVoice and blocks the calling thread until it has actually
	//finished playing (via ISpVoice::WaitUntilDone), rather than the fire-and-forget SPF_ASYNC
	//pattern used elsewhere in this codebase. Worker-thread-only - never call this from the
	//haptic thread, which must stay responsive at the haptic framerate and can't afford to
	//block for a multi-second utterance. This is what lets exchangeInProgress (below) stay true
	//for exactly as long as the assistant is actually speaking, not just until Speak() was
	//issued - see runExchange() and workerLoop()'s "not ready yet" branch, and
	//AssistantSetup::speak() for the equivalent there.
	static void speakAndWait(const std::wstring& text);

	//--- Shared with the worker thread ---
	std::mutex workerMutex;
	std::condition_variable_any workerCv;  //condition_variable_any so it can also wait on a stop_token
	std::atomic<bool> captureRequested{ false };

	//Which pipeline the CURRENT (or most recently requested) capture is for - set by
	//beginListening()/beginDictating() just before captureRequested.store(true), read once by
	//workerLoop() right after the shared record/stop loop finishes, to decide between
	//runExchange() and runDictation(). See CaptureKind's own comment.
	std::atomic<CaptureKind> captureKind{ CaptureKind::Ask };

	//Single-slot mailbox for a completed dictation's recognized text - filled by runDictation()
	//(worker thread), drained by tryConsumePendingDictation() (haptic thread, public above).
	//Mirrors WindowManager::hasPendingExplicitFocus/pendingExplicitFocusHandle's own producer/
	//consumer pattern. A result that arrives with nothing currently polling for it just sits
	//here until the next tryConsumePendingDictation() call consumes (and, per that method's own
	//comment, discards) it - at most one dictation's worth of staleness, never unbounded growth.
	std::mutex dictationResultMutex;
	bool hasPendingDictationResult = false;
	std::wstring pendingDictationResult;

	//Set by requestSetupStart() when the user confirms the two-detent prompt; consumed by
	//workerLoop(), which runs AssistantSetup::run() or runSpeechOnly() in response (depending on
	//requestedSetupKind just below) rather than eagerly at thread start (see the plan addendum -
	//setup is opt-in now, never automatic).
	std::atomic<bool> setupRequested{ false };

	//Set by requestSetupStart() alongside setupRequested, just before notifying the worker - see
	//SetupKind's own comment for what the two values mean and why they're independent now.
	std::atomic<SetupKind> requestedSetupKind{ SetupKind::FullAssistant };

	//Refuses a new gesture while a previous exchange's pipeline is still in flight, rather
	//than queuing or interrupting it. Currently advisory - updateAuxButton() doesn't consult
	//it, since completing a tap-then-hold gesture takes long enough in practice that two
	//overlapping exchanges are unlikely - flagged here as the natural place to add a hard
	//guard if that assumption turns out to be wrong in testing. Also now part of
	//assistantHasFloor() above - true from just before the listening cue plays until the
	//spoken reply (or a short error message) has actually finished, via speakAndWait().
	std::atomic<bool> exchangeInProgress{ false };

	//True for the lifetime of the two-detent confirm/cancel prompt - set in updateAuxButton()
	//when entering AwaitingSetupConfirmation, cleared at the end of commitSetupConfirmation().
	//Exists purely to feed assistantHasFloor() above; SetupConfirmationPrompt itself doesn't
	//need it; it already governs the prompt's own force/narration directly.
	std::atomic<bool> confirmationPromptActive{ false };

	//--- Worker-thread-only state --- these, plus lastSessionId, are declared before `worker`
	//specifically so they're constructed before the thread starts and destroyed only after
	//it has been joined (a jthread's members destruct/join in reverse declaration order).
	AudioCapture       audioCapture;
	WhisperTranscriber whisperTranscriber;  //see WhisperTranscriber.h for why this replaced SAPI-based recognition
	ClaudeCliClient    cliClient;
	AssistantSetup   assistantSetup;  //run() only in response to setupRequested - see workerLoop()
	std::string      lastSessionId;   //empty until the first successful exchange; kept for --resume

	//Must be the LAST member declared: it must start after everything above it exists, and
	//must finish joining (which its destructor does automatically) before any of the above
	//is destroyed.
	std::jthread worker;
};
