#pragma once
#include <atomic>
#include <chrono>
#include <thread>
#include <string>
#include <vector>
#include "mathTypes.h"
//Bare filename (no "HapticObjects/" prefix), matching FFUIDesktop.h's own include of the same
//header - the project's Additional Include Directories already covers the HapticObjects folder.
#include "HapticVibration.h"

class VoiceAssistant;

//Tuning constants for the tutorial's OWN pacing - "lets keep this as a #define so we can adjust?
//- maybe start with 5 seconds?", per the request. A different kind of knob than DetentTuning.h
//(tile-detent feel) or HapticVibration's own settings (per-vibration shape) - these are narration
//timeouts, kept here rather than folded into either of those.
#define TUTORIAL_STAGE_SKIP_WINDOW_SECONDS 5
//Lowered from an initial 5 after hardware-test feedback ("the delay between tutorial narrator
//outputs is a bit too long - feels like a lot of deadspace") - this is the interval between
//REPEAT reminders once a checkpoint's own instruction has already been spoken once (see
//waitForCondition()'s own comment - the genuinely new-information delay this feedback was mostly
//about was a separate bug, fixed alongside this).
#define TUTORIAL_REPEAT_REMINDER_SECONDS 3
//How long a second button press has to land after the first to count as the Stage-0-only
//"double-press any button to abandon the tutorial" gesture - not itself one of the two timeouts
//Gareth's answer was about (that answer covered the per-stage skip window and the
//repeat-until-done reminder interval), so left as a separate, smaller constant rather than
//overloading either of the two above. Widened from an initial 500ms after first hardware test
//("didn't work - maybe I was too slow") - half a second for two deliberate presses is a tight
//ask; 1500ms gives real margin while still reading as "double-press" rather than two unrelated
//presses.
#define TUTORIAL_DOUBLE_PRESS_WINDOW_MS 1500
//A safety cap Gareth's spec didn't explicitly ask for, added here defensively: if a
//repeat-until-done checkpoint (Stage 3/4/5) hasn't been satisfied after this many reminders, the
//tutorial moves on anyway rather than waiting forever - e.g. if the user declines the Stage 5
//setup prompts entirely, nothing would otherwise ever satisfy those checkpoints. Flagged in
//TUTORIAL_SPEC.md as an added-beyond-spec behavior, not a silent shortcut.
#define TUTORIAL_MAX_REMINDERS_BEFORE_AUTO_ADVANCE 6
//How long WindowManager::ArchivedWindows must be observed CONTINUOUSLY empty before Stage 4
//trusts it and switches to the empty-tray fallback narration - added after hardware-test feedback
//("the 'program tray empty' check false triggered for me"). A single frame's snapshot isn't
//trustworthy on its own: ArchivedWindows is deliberately cleared to build drag placeholders during
//ANY window grab elsewhere in the app, and only repopulates on the next full scanner cycle after
//release (see initiatePeriodicScanner()'s own comment) - a beat of sustained emptiness rules that
//kind of transient false-empty out.
#define TUTORIAL_TRAY_EMPTY_DEBOUNCE_MS 1000

//Drives the first-launch / manually-launchable Tutorial mode - see TUTORIAL_SPEC.md for the full
//design (stage-by-stage narration scripts, global rules, and the "Open questions" Gareth answered
//that shaped several decisions below, most visibly the two independently-time-boxed skip
//mechanisms and the Stage 4 empty-tray fallback).
//
//A Meyer's singleton, matching WindowManager/MenuSystem/StartMenuFlow's own pattern. Entirely
//haptic-thread-owned for its own state (begin()/update()/updateForce()/notifyProgramDropped() -
//only ever called from FFUIDesktop::updateFrame(), same as StartMenuFlow, so none of that needs
//locking) - the actual blocking narration work (sequential Speak()+WaitUntilDone() calls through
//pAssistantVoice) runs on a short-lived background jthread this class spawns itself, mirroring
//StartMenuFlow's own searchThread/launchThread pattern, since a multi-second blocking TTS call
//can never run on the haptic thread (see speakStageScript()'s own comment in the .cpp).
//
//Two exceptions to that single-thread ownership: `stage` and `subStep` are also read (never
//written) from other threads - originally just FFUIDesktop's scanner thread, now also whichever
//thread is about to speak an FFUI-narrator line (see SpeakFfuiNarration() in FFUIDesktop.h and
//shouldMuteFfuiNarrator() below) - to decide whether the FFUI narrator should currently be muted.
//Those two fields are std::atomic for exactly that reason - everything else below stays
//plain/unsynchronized because nothing else is read off the haptic thread.
//
//Deliberately an OVERLAY, not a takeover: unlike MenuSystem/StartMenuFlow/
//AwaitingSetupConfirmation (each a fully exclusive interaction mode that suspends everything
//else), the tutorial narrates and watches alongside completely normal interaction the whole time
//- Stage 3/4/5 specifically expect the user to use the real wall-crossing, quickslot-cycling,
//drag-and-drop, and AUX-hold mechanics, not a tutorial-only stand-in for them. That's why this
//class has no button-absorbing branch in FFUIDesktop::updateFrame()'s dispatch chain the way
//StartMenuFlow does - it only adds a small guidance FORCE on top of normal forces (Stage 3a
//only), and observes button-press edges/room state passed in by FFUIDesktop rather than gating
//anything.
class TutorialFlow {
public:
	static TutorialFlow& getInstance() {
		static TutorialFlow instance;
		return instance;
	}
	TutorialFlow(TutorialFlow const&) = delete;
	void operator=(TutorialFlow const&) = delete;

	enum class Stage {
		Inactive,
		Stage0_Intro,
		Stage1_Stylus,
		Stage2_Haptics,
		Stage3_ProgramSlot,
		Stage4_ProgramSelector,
		Stage5_Settings,
		Stage6_End
	};

	bool isActive() const { return stage != Stage::Inactive; }
	Stage currentStage() const { return stage; }

	//True while ALL normal FFUI haptic output (walls, tile detents, boundary buzz, everything
	//processForces()/boundary forces/cursorStateHaptics would otherwise render) should be
	//suppressed entirely - "I can feel the FFUI interface during the intro - there should be no
	//haptic effects until they are introduced later", from first hardware testing. True only
	//during Stage 0 (Intro) and Stage 1 (Stylus tour), before Stage 2 narrates and demonstrates
	//force feedback for the first time - false from Stage 2 onward, where real walls/tiles are
	//expected to be felt exactly as normal. FFUIDesktop::updateFrame() checks this once, right
	//before the final aggregate force is sent to the device, rather than threading a check through
	//every individual force source.
	bool shouldSuppressNormalHaptics() const {
		return stage == Stage::Stage0_Intro || stage == Stage::Stage1_Stylus;
	}

	//True while the FFUI narrator (pSapiVoice) should be fully silent. Originally just Stage 1 +
	//Stage 2 ("can we make sure the FFUI narrator is disabled during steps 1 & 2?", second-round
	//hardware-test feedback) - first enforced ONLY by ducking pSapiVoice's own volume to 0 from
	//initiatePeriodicScanner()'s scanner thread (reported still audible), then by gating every
	//pSapiVoice->Speak() call site directly via the SpeakFfuiNarration() wrapper in FFUIDesktop.h
	//(see that wrapper's own comment) - still reported audible ("still getting FFUI narrator
	//prompts during the tutorial first stages"), which turned out to be because Stage 0 itself was
	//never covered by this condition at all - only Stage 1/2 were, so anything narrated during the
	//Intro stage was always going to be heard regardless of how solid the Stage-1/2 gating was.
	//Fixed by extending coverage to Stage 0 and, per the precise stop-point given in that same
	//follow-up ("these need to be completely blocked until we start introducing the middle button
	//to change quickslots"), narrowing WHEN unmuting happens within Stage 1 itself: silent through
	//subStep 0 (Select) and 1 (Aux), audible again from subStep 2 onward - the moment
	//updateStage1() actually introduces the middle button/scroll-wheel checkpoint (see its own
	//case 2). Stage 2 stays fully muted throughout, unchanged from the original "steps 1 & 2"
	//request - only Stage 1's own internal cutoff point changed.
	//
	//Reads `subStep` as well as `stage` now - like `stage`, that's std::atomic for the same
	//reason (see its own comment): this method is called from SpeakFfuiNarration(), which runs on
	//whichever thread happens to be narrating, not just the haptic thread.
	bool shouldMuteFfuiNarrator() const {
		if (stage == Stage::Stage0_Intro) return true;
		if (stage == Stage::Stage1_Stylus) return subStep < 2;
		if (stage == Stage::Stage2_Haptics) return true;
		return false;
	}

	//Starts (or restarts - "I think it's ok to just restart from 0", Gareth's own answer on
	//mid-tutorial interruption, which this method also satisfies for a manual re-launch since
	//there's no partial-progress state to resume from anyway) the tutorial from Stage 0.
	//autoStart=true plays the extra "this is your first time..." line and enables the
	//double-press-abandon window - both confined to Stage 0 only, per TUTORIAL_SPEC.md's Global
	//Rules ("only offered at the very start"). Called from FFUIDesktop::initDesktop()
	//(auto-start, first-ever launch) or the new "FFUI Tutorial" settings tile (manual re-launch,
	//autoStart=false).
	void begin(bool autoStart);

	//Everything the tutorial's own checkpoints need from this frame - FFUIDesktop already
	//computes all of these for its own logic, so this just bundles copies rather than duplicating
	//any of that computation.
	struct FrameInput {
		Location deviceLoc;
		bool selectPressedEdge = false;       //front/select button press edge - per-stage skip trigger, and counts toward double-press-abandon
		bool auxPressedEdge = false;          //AUX button press edge - counts toward double-press-abandon only (the real AUX gesture machine is untouched - see class comment)
		bool middleButtonPressedEdge = false; //scroll-click press edge - Stage 3b's checkpoint, and counts toward double-press-abandon
		bool rearButtonPressedEdge = false;   //rear button press edge - Stage 1's own button-tour checkpoint only (its real function, an OS right-click, is handled entirely in FFUIDesktop.cpp's own mouse-forwarding block, not through this struct - see openingLinesForStage1())
		bool inFrontZone = false;
		bool isControllingActiveRoom = false;
	};

	//Called once every haptic frame from FFUIDesktop::updateFrame(), regardless of tutorial state
	//(a no-op while Inactive) - mirrors StartMenuFlow::update()'s own calling convention.
	//voiceAssistant is passed in the same way StartMenuFlow takes it (a plain FFUIDesktop member,
	//not a Meyer's singleton) - Stage 5 reads its speech/assistant setup state directly.
	void update(const FrameInput& input, VoiceAssistant& voiceAssistant);

	//Force to add THIS FRAME while the tutorial is active - purely additive (see class comment on
	//why this is an overlay, not a takeover). A guidance pull toward the Program Slot room during
	//Stage 3a, or the currently-active demo vibration during Stage 2's haptics-vocabulary
	//sequence; zero at every other stage/moment. Not const - like CursorStateHaptics::update(),
	//advancing a HapticVibration's internal timing (processVibration()/sleepVibration()) mutates
	//state every call.
	Vector3 updateForce(Location currentLoc);

	//Called by FFUIDesktop's existing drop-handler, right after a genuine, successful drop (see
	//that call site's own comment for exactly where) - Stage 4's drag-and-drop checkpoint. A
	//no-op unless the tutorial is currently actually waiting on this exact action.
	void notifyProgramDropped();

private:
	TutorialFlow();

	//Was a plain (non-atomic) Stage member for this class's entire lifetime so far, on the theory
	//that every reader/writer lived on the haptic thread (see class comment). That stopped being
	//true the moment FFUIDesktop::initiatePeriodicScanner() (the SCANNER thread) started reading
	//currentStage() to decide whether to mute the FFUI narrator during Stage 1/2 - a genuine,
	//unsynchronized cross-thread read of a plain enum, added without updating this member to
	//match. Prime suspect for "the muting hasn't worked - i'm still hearing FFUI narrator prompts
	//in this first stage" (third-round hardware-test feedback): nothing guarantees the scanner
	//thread ever observes a stage change written on the haptic thread, so it can keep computing
	//targetFfuiNarratorVolume from a stale (often still Stage::Inactive/Stage0_Intro) read
	//indefinitely. Made atomic to close that gap, matching how every other piece of state this
	//codebase shares across threads is already handled (VoiceAssistant's
	//captureRequested/exchangeInProgress/confirmationPromptActive, this same class's own
	//narrationInProgress/activeDemoVibration) - not a defensive-only change, but a direct fix for
	//an identified, real data race. Every existing read/write site (`stage = newStage;`,
	//`stage == Stage::X`, `switch (stage)`) keeps compiling unchanged, since std::atomic<Stage>
	//still implicitly converts to/from Stage.
	std::atomic<Stage> stage{ Stage::Inactive };
	bool isAutoStart = false;
	bool autoStartAbandonWindowActive = false;   //Stage 0 only - see begin()'s own comment

	//Sub-phase within whichever stage is current - haptic-thread-only, same as `stage` itself.
	enum class Phase {
		Narrating,       //background thread is mid-speech for this stage's OPENING script (see enterStage())
		SkipWindowOpen,  //opening narration finished; select-press-to-skip is live for TUTORIAL_STAGE_SKIP_WINDOW_SECONDS
		AwaitingAction,  //skip window closed; this stage's own updateStageN() now drives its remaining sub-steps, each with no further skip option - "not all the time", per the request
	};
	Phase phase = Phase::Narrating;
	std::chrono::steady_clock::time_point phaseEnteredTime{};

	//Which sub-step within the CURRENT stage's own AwaitingAction handling is active - meaning is
	//entirely local to whichever updateStageN() is reading it; reset to 0 on every enterStage().
	//Atomic for the same reason `stage` is (see its own comment) - shouldMuteFfuiNarrator() below
	//now reads this too, from whichever thread is about to narrate, not just the haptic thread
	//that writes it (via goToSubStep()/enterStage()).
	std::atomic<int> subStep{ 0 };
	//True once the CURRENT subStep's own instruction line has been spoken at least once - lets
	//each updateStageN() case speak its instruction immediately, exactly once, the first frame
	//it's reached, rather than leaving that first utterance to waitForCondition()'s own
	//interval-gated reminder timing (which is only meant for REPEATING an already-heard
	//instruction, not for the first, genuinely-new one - see waitForCondition()'s own comment for
	//the "deadspace" bug this distinction fixes). Reset by goToSubStep() below and by
	//enterStage(). Deliberately left false and unused by Stage 3's own case 0, which intentionally
	//reuses its stage's opening narration verbatim rather than adding a second, immediate copy of
	//the same line.
	bool checkpointAnnounced = false;
	//Advances to a new subStep AND resets checkpointAnnounced together, so the two can never drift
	//out of sync - every subStep transition in every updateStageN() below goes through this rather
	//than assigning subStep directly.
	void goToSubStep(int newSubStep);

	//--- Generic repeat-until-done helper state, shared by every stage's own wait loops ---
	std::chrono::steady_clock::time_point lastReminderTime{};
	int reminderCount = 0;
	//Tracks narrationInProgress as observed on the PREVIOUS call to waitForCondition(), so it can
	//detect the true->false edge (a reminder, or the transition line that preceded this
	//checkpoint, just finished speaking) and restart the repeat-period countdown from THAT moment
	//- see waitForCondition()'s own comment in the .cpp for the bug this fixes ("repeat period
	//timer to only start after the previous prompt finishes"). Reset true in enterStage(), since a
	//new stage's own opening narration is already in flight (or about to be) by the time this
	//would first be read.
	bool lastKnownNarrating = true;
	//Returns true once `conditionMet` is true OR the reminder cap has been hit (either way, the
	//caller should move on) - otherwise speaks reminderLine if TUTORIAL_REPEAT_REMINDER_SECONDS
	//have passed since the PREVIOUS reminder actually finished speaking (skipped entirely for a
	//tick where narrationInProgress is still true, so reminders never stack) and returns false.
	//
	//suppressReminder holds the reminder countdown (without treating the checkpoint as done, and
	//without counting toward TUTORIAL_MAX_REMINDERS_BEFORE_AUTO_ADVANCE) while some OTHER process
	//this checkpoint is waiting on is actively narrating its own progress through the same
	//pAssistantVoice - added after hardware-test feedback ("the 'hold the aux button now to set
	//up dictation' prompt repeated after I had already pressed the button, then overrode the
	//narrator prompt from the menu"). Both this class's own reminders AND AssistantSetup's
	//real-time setup narration (see AssistantSetup.cpp) speak through pAssistantVoice with
	//SPF_PURGEBEFORESPEAK, so a reminder that kept firing every TUTORIAL_REPEAT_REMINDER_SECONDS
	//throughout a multi-step setup that was already under way would repeatedly cut that setup's
	//own narration off mid-sentence - see updateStage5()'s own call sites for the concrete case.
	//Defaulted so every other existing call site (nothing else in this class waits on a
	//long-running externally-narrated process) is unaffected.
	bool waitForCondition(bool conditionMet, const std::wstring& reminderLine, bool suppressReminder = false);

	//Set true by notifyProgramDropped() while Stage 4 is waiting on it; consumed (and cleared) by
	//updateStage4(). A plain bool, not atomic - both the setter (FFUIDesktop's drop-handler) and
	//reader (update(), called every frame) run on the haptic thread only, never concurrently.
	bool programDroppedFlag = false;

	//--- Double-press-to-abandon (Stage 0 only) ---
	std::chrono::steady_clock::time_point lastAnyButtonPressTime{};
	bool waitingOnFirstPress = true;
	void checkDoublePressAbandon(const FrameInput& input);
	void abandon();

	//--- Background narration (see the .cpp's speakStageScript() for why this can't run on the haptic thread) ---
	std::atomic<bool> narrationInProgress{ false };
	std::jthread narrationThread;
	void speakStageScript(std::vector<std::wstring> lines);

	//--- Stage 2's demo-vibration sequence ---
	enum class DemoVibrationKind { None, Boundary, TextField, Clickable, Unresponsive };
	std::atomic<DemoVibrationKind> activeDemoVibration{ DemoVibrationKind::None };
	HapticVibration demoBoundaryVibration;
	HapticVibration demoTextFieldVibration;
	HapticVibration demoClickableVibration;
	HapticVibration demoUnresponsiveVibration;
	void runStage2DemoSequence();

	void enterStage(Stage newStage);
	void advanceToNextStage();

	//Each returns the OPENING narration (spoken via the generic Narrating/SkipWindowOpen pipeline
	//before updateStageN() ever runs) for its stage - see class comment for why only the opening
	//narration gets a skip window, not every sub-step.
	std::vector<std::wstring> openingLinesForStage0() const;
	std::vector<std::wstring> openingLinesForStage1() const;

	//Per-stage checkpoint handlers - called from update() once each stage's own opening narration
	//has finished (see update()'s own comment on why that no longer also waits out the skip
	//window first).
	void updateStage1(const FrameInput& input);
	void updateStage2(const FrameInput& input);
	void updateStage3(const FrameInput& input);
	void updateStage4(const FrameInput& input);
	void updateStage5(const FrameInput& input, VoiceAssistant& voiceAssistant);

	bool stage2DemoStarted = false;
	bool stage4TrayWasEmpty = false;
	//First moment ArchivedWindows was observed empty during Stage 4's case 0, or the epoch value
	//(default-constructed time_point) while it's currently non-empty - see
	//TUTORIAL_TRAY_EMPTY_DEBOUNCE_MS's own comment. Reset in enterStage().
	std::chrono::steady_clock::time_point stage4EmptyCheckStartTime{};

	//Derived "which of the three front-zone/room areas is the stylus in right now" - synthesized
	//here from the same booleans FFUIDesktop::updateFrame() already computes inline (no shared
	//room enum exists anywhere else in this codebase today - see this class's own .cpp comment).
	enum class Room { Unknown, ProgramSlot, ProgramSelector, FfuiSettings };
	static Room classifyRoom(const FrameInput& input);
};
