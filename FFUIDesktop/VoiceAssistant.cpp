#include "VoiceAssistant.h"
#include "FFUIDesktop.h"   //for pAssistantVoice, the assistant's own dedicated SAPI voice
#include <vector>
#include <iostream>   //for printing recognized speech to the console - see runExchange()

void VoiceAssistant::start() {
	worker = std::jthread([this](std::stop_token stoken) { workerLoop(stoken); });
}

AuxGestureState VoiceAssistant::updateAuxButton(bool auxPressedNow, Location currentLoc) {
	auto now = std::chrono::steady_clock::now();

	switch (state) {
	case AuxGestureState::Idle:
		if (auxPressedNow) {
			state = AuxGestureState::Held_PullMode;
			stateEnteredTime = now;
		}
		break;

	case AuxGestureState::Held_PullMode:
		if (!auxPressedNow) {
			if (now - stateEnteredTime < TAP_MAX) {
				//Quick tap, not a normal hold-and-release - arm the tap+hold window.
				state = AuxGestureState::ArmedAfterTap;
				stateEnteredTime = now;
			}
			else {
				//A normal hold-and-release - today's existing behavior, unchanged. (Shouldn't
				//actually be reachable any more in practice - still held past TAP_MAX commits to
				//DictatingMode below on an earlier frame - but kept as a safe fallback in case a
				//frame is ever missed right at the boundary.)
				state = AuxGestureState::Idle;
			}
		}
		else if (now - stateEnteredTime >= TAP_MAX) {
			//Still held past the tap threshold with no release in between - this was never a
			//tap, it's a genuine plain hold. Commit to dictation now, not the instant AUX was
			//first pressed - see DictatingMode's own comment for why the distinction matters
			//(starting capture any earlier would fire a spurious recording on every tap of a
			//tap-then-hold ListeningMode gesture too). "Hold the Aux button to activate the
			//dictate function", per the request.
			//
			//Dictation only ever needs speech (whisper.cpp) provisioned, NOT the full Claude CLI+
			//login sequence - the two are technically independent (whisper.cpp has no dependency
			//on the Claude CLI at all; they were only ever coupled by run()'s own ordering, not by
			//a real requirement). So this checks speechState, not the full `state` - a plain hold
			//with speech not yet provisioned (or a previous speech-only attempt that failed)
			//offers the two-detent confirm/cancel prompt for JUST that, not commit straight to
			//DictatingMode and leave the user stuck with only the worker's terse "not ready yet"
			//cue/message and no way to actually get set up short of releasing AUX and somehow
			//finding the AI-assistant gesture instead.
			SpeechSetupState speechSetup = assistantSetup.speechState.load();
			if (speechSetup == SpeechSetupState::NotStarted || speechSetup == SpeechSetupState::Failed) {
				state = AuxGestureState::AwaitingSetupConfirmation;
				pendingSetupKind = SetupKind::Speech;
				//Distinct wording from the tap-then-hold AI-assistant gesture's own prompt below
				//(which keeps start()'s default) - this is now genuinely a lighter-weight setup
				//(just a speech-model download, no Claude account/login involved at all), so
				//"enable the microphone and voice detection feature" still matches what a plain
				//AUX hold is actually asking to turn on, from the user's point of view, without
				//overpromising anything about Claude.
				confirmationPrompt.start(currentLoc, L"enable the microphone and voice detection feature");
				//Feeds assistantHasFloor() the same way PendingGestureDecision's own entry into
				//this state does - see that branch's comment. Cleared in
				//commitSetupConfirmation() once this prompt is actually done.
				confirmationPromptActive.store(true);
			}
			else {
				state = AuxGestureState::DictatingMode;
				beginDictating();
			}
		}
		//While held and still under TAP_MAX, this stays Held_PullMode - no force is rendered off
		//it any more (the old "pull force toward nearest object" behavior is deactivated, not
		//deleted - see FFUIDesktop.cpp's force block and calculateForceToClosestObject()'s own
		//comment).
		break;

	case AuxGestureState::DictatingMode:
		if (!auxPressedNow) {
			state = AuxGestureState::Idle;
			endDictating();
		}
		//While held, this stays DictatingMode - FFUIDesktop.cpp renders no force off it (dictation
		//is a pure button+microphone interaction, same as ListeningMode).
		break;

	case AuxGestureState::ArmedAfterTap:
		if (auxPressedNow) {
			state = AuxGestureState::PendingGestureDecision;
			stateEnteredTime = now;
		}
		else if (now - stateEnteredTime >= TAP_TO_HOLD_WINDOW) {
			//Window expired with no second press - that was just a single, standalone tap.
			//"tap to narrate, hold to dictate, tap+hold for the AI assistant", per the request -
			//signal the caller (FFUIDesktop.cpp, via tryConsumeTapNarrateEdge()) to trigger Narrate
			//now, rather than doing nothing as before.
			tapNarrateEdge = true;
			state = AuxGestureState::Idle;
		}
		break;

	case AuxGestureState::PendingGestureDecision:
		if (!auxPressedNow) {
			//Released before committing to a hold - a second tap, not tap+hold. v1 doesn't
			//chain into longer gesture sequences; just reset.
			state = AuxGestureState::Idle;
		}
		else if (now - stateEnteredTime >= TAP_MAX) {
			//Still held past the tap threshold - commit. Where to depends on whether the
			//assistant has ever been set up: if not (or a previous attempt failed), ask first
			//rather than silently installing software, opening a login window, or touching
			//Windows settings - see SetupConfirmationPrompt. Otherwise (Ready, or already
			//confirmed and still mid-setup from an earlier confirmation), this is unchanged
			//from before: enter ListeningMode, and let the worker thread's existing check
			//decide whether to run the real exchange or play a "not ready yet" cue.
			SetupState setup = assistantSetup.state.load();
			if (setup == SetupState::NotStarted || setup == SetupState::Failed) {
				state = AuxGestureState::AwaitingSetupConfirmation;
				pendingSetupKind = SetupKind::FullAssistant;
				confirmationPrompt.start(currentLoc);
				//Feeds assistantHasFloor() so window scanning pauses for this prompt's own
				//narration too, not just AssistantSetup's - see that method's comment. Cleared
				//in commitSetupConfirmation() below, once this prompt is actually done.
				confirmationPromptActive.store(true);
			}
			else {
				state = AuxGestureState::ListeningMode;
				beginListening();
			}
		}
		break;

	case AuxGestureState::ListeningMode:
		if (!auxPressedNow) {
			state = AuxGestureState::Idle;
			endListening();
		}
		break;

	case AuxGestureState::AwaitingSetupConfirmation:
		//Deliberately no AUX-driven transition here at all, in either direction - once this
		//state is entered, AUX is no longer part of the interaction (see the class comment on
		//commitSetupConfirmation()). The stylus is free to move regardless of AUX press/release;
		//FFUIDesktop.cpp drives the two-detent force directly off this state the whole time via
		//getConfirmationForce(), and the front/select button (not AUX) is what commits the
		//choice and leaves this state - see commitSetupConfirmation().
		break;
	}

	return state;
}

void VoiceAssistant::commitSetupConfirmation() {
	if (state != AuxGestureState::AwaitingSetupConfirmation) return;  //defensive - shouldn't be reachable otherwise

	state = AuxGestureState::Idle;
	if (confirmationPrompt.wasConfirmed()) {
		requestSetupStart(pendingSetupKind);
		//confirmationPromptActive is cleared below regardless, but isSetupInProgress() takes
		//over as the reason assistantHasFloor() stays true within a frame or two, once the
		//worker thread picks up setupRequested - no gap in the pause either way.
	}
	else if (pAssistantVoice) {
		//Deliberately not speakAndWait() here (unlike runExchange()/AssistantSetup::speak()) -
		//this runs on the haptic thread, per this method's own doc comment, and blocking it for
		//a multi-second TTS utterance would stall force rendering. Through pAssistantVoice, not
		//pSapiVoice, same as everything else assistant-related - see FFUIDesktop::initDesktop().
		//Mirrors every narrator request to the console, tagged by source - "mirror all the
		//narrator requests to the console please", per the request.
		std::cout << "[Narrate:Assistant] \"Okay, not enabling the assistant.\"" << std::endl;
		pAssistantVoice->Speak(L"Okay, not enabling the assistant.", SPF_ASYNC | SPF_PURGEBEFORESPEAK, NULL);
	}
	confirmationPromptActive.store(false);
}

void VoiceAssistant::speakAndWait(const std::wstring& text) {
	if (pAssistantVoice) {
		// Shared by every runExchange()-driven AI-assistant reply - one mirror line here covers
		// all of them without needing one at each caller.
		std::string textNarrow(text.begin(), text.end());
		std::cout << "[Narrate:Assistant] \"" << textNarrow << "\"" << std::endl;
		pAssistantVoice->Speak(text.c_str(), SPF_ASYNC | SPF_PURGEBEFORESPEAK, NULL);
		pAssistantVoice->WaitUntilDone(INFINITE);
	}
}

void VoiceAssistant::beginListening() {
	captureKind.store(CaptureKind::Ask);
	{
		std::lock_guard<std::mutex> lock(workerMutex);
		captureRequested.store(true);
	}
	workerCv.notify_one();
}

void VoiceAssistant::endListening() {
	{
		std::lock_guard<std::mutex> lock(workerMutex);
		captureRequested.store(false);
	}
	workerCv.notify_one();
}

void VoiceAssistant::beginDictating() {
	//Deliberately does NOT play the listening cue itself - AudioCapture's cues are Beep()-based
	//and documented as worker-thread-only (blocking, must never stall the real-time haptic
	//loop). Just like beginListening()/runExchange(), the cue is played from workerLoop() itself,
	//the moment it actually picks this request up and starts recording - see workerLoop()'s
	//Dictate branch below. This method only signals the worker and returns immediately.
	captureKind.store(CaptureKind::Dictate);
	{
		std::lock_guard<std::mutex> lock(workerMutex);
		captureRequested.store(true);
	}
	workerCv.notify_one();
}

void VoiceAssistant::endDictating() {
	{
		std::lock_guard<std::mutex> lock(workerMutex);
		captureRequested.store(false);
	}
	workerCv.notify_one();
}

void VoiceAssistant::requestSetupStart(SetupKind kind) {
	requestedSetupKind.store(kind);
	{
		std::lock_guard<std::mutex> lock(workerMutex);
		setupRequested.store(true);
	}
	workerCv.notify_one();
}

void VoiceAssistant::workerLoop(std::stop_token stoken) {
	//Own COM apartment for this thread, mirroring WindowScanner::initialize() on the scanner
	//thread - SAPI recognition and the CreateProcess-based Claude call both happen here.
	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

	//Setup is opt-in, not automatic - AssistantSetup::run() is only ever invoked below, in
	//response to setupRequested (set by requestSetupStart(), which the haptic thread's gesture
	//state machine calls only after the user explicitly confirms via the two-detent prompt -
	//see updateAuxButton()'s AwaitingSetupConfirmation handling). Nothing here runs eagerly at
	//thread start any more.
	while (!stoken.stop_requested()) {
		{
			std::unique_lock<std::mutex> lock(workerMutex);
			workerCv.wait(lock, stoken, [this] { return captureRequested.load() || setupRequested.load(); });
		}
		if (stoken.stop_requested()) break;

		if (setupRequested.exchange(false)) {
			//The user just confirmed via the two-detent prompt - run whichever of the two
			//independent setup sequences that prompt was for (see SetupKind's own comment), for
			//the first time, or as a retry if a previous confirmation's attempt ended in Failed.
			//The full run() can block for minutes if it has to wait on a guided login;
			//runSpeechOnly() is normally much quicker (a file-existence check, or a one-time
			//download). Both are stop_token-aware throughout, so app shutdown isn't held hostage
			//by either. A capture request that arrives while this is running, if any, is simply
			//handled on the next loop iteration once this one finishes.
			if (requestedSetupKind.load() == SetupKind::Speech) {
				assistantSetup.runSpeechOnly(stoken);
				whisperTranscriber.setPaths(assistantSetup.resolvedWhisperExePath(), assistantSetup.resolvedWhisperModelPath());
			}
			else {
				assistantSetup.run(stoken);
				cliClient.setCliPath(assistantSetup.resolvedCliPath());
				whisperTranscriber.setPaths(assistantSetup.resolvedWhisperExePath(), assistantSetup.resolvedWhisperModelPath());
			}
			continue;
		}

		if (!captureRequested.load()) continue;  //spurious wake, or already handled by the branch above

		//Dictate only ever needs speech provisioned; Ask needs the full assistant (asking Claude
		//anything still requires the CLI and a signed-in account regardless of whether speech
		//happens to already be ready) - see SetupKind's own comment for why these can now differ.
		bool ready = (captureKind.load() == CaptureKind::Dictate)
			? (assistantSetup.speechState.load() == SpeechSetupState::Ready)
			: (assistantSetup.state.load() == SetupState::Ready);

		if (!ready) {
			//Reachable when a confirmed setup is still in progress (or failed) from an earlier
			//confirmation - the haptic thread only re-offers the confirmation prompt itself
			//when setup has never been requested at all or previously failed outright (see
			//updateAuxButton()), so getting here means the user already said yes once. Don't
			//try to run a pipeline that can't complete; let them know rather than doing nothing.
			//
			//Wrapped in exchangeInProgress (feeding assistantHasFloor()) the same as a real
			//exchange below, so this short message can't get clipped by window-focus narration
			//either - previously this ran with no such protection at all.
			exchangeInProgress.store(true);
			captureRequested.store(false);
			AudioCapture::playNotReadyCue();
			speakAndWait(L"The assistant isn't set up yet.");
			exchangeInProgress.store(false);
			continue;
		}

		exchangeInProgress.store(true);

		//Distinct cue per pipeline - "use similar, but distinct sound cues to the AI speech
		//capture", per the request. Both are brief Beep() wrappers, safe to call from this
		//(worker) thread - see AudioCapture.h's own doc comment on why they must never be called
		//from the haptic thread.
		if (captureKind.load() == CaptureKind::Dictate) {
			AudioCapture::playDictateListeningCue();
		}
		else {
			AudioCapture::playListeningCue();
		}
		audioCapture.start();

		//Poll in short slices rather than one long wait_until: condition_variable_any's
		//stop_token-aware overload only covers plain wait(), not wait_for/wait_until, so a
		//single long wait here couldn't be woken early by app shutdown. Waking every 100ms
		//keeps that shutdown latency bounded instead of blocking for up to
		//MAX_RECORD_DURATION if the app closes mid-recording.
		auto recordingDeadline = std::chrono::steady_clock::now() + AudioCapture::MAX_RECORD_DURATION;
		while (!stoken.stop_requested() && captureRequested.load() &&
			std::chrono::steady_clock::now() < recordingDeadline) {
			std::unique_lock<std::mutex> lock(workerMutex);
			workerCv.wait_for(lock, std::chrono::milliseconds(100),
				[this] { return !captureRequested.load(); });
		}
		//However we got here - the user released AUX, the safety cap fired, or the app is
		//shutting down - stop recording now.
		captureRequested.store(false);

		if (stoken.stop_requested()) {
			//Shutting down mid-recording: close the mic cleanly but skip the rest of the
			//pipeline - ClaudeCliClient::ask() alone can block for up to 30s, which would
			//otherwise make app shutdown hang waiting for this thread to join.
			audioCapture.stop();
			exchangeInProgress.store(false);
			break;
		}

		//CaptureKind decides which pipeline handles the just-captured audio - see that enum's own
		//comment. Read once here, right after the shared record/stop loop above finishes, same
		//spot runExchange() alone used to be called unconditionally.
		if (captureKind.load() == CaptureKind::Dictate) {
			runDictation(stoken);
		}
		else {
			runExchange(stoken);
		}

		exchangeInProgress.store(false);
	}

	CoUninitialize();
}

void VoiceAssistant::runExchange(std::stop_token stoken) {
	std::vector<int16_t> pcm = audioCapture.stop();

	if (pcm.empty()) {
		//Nothing captured (e.g. the mic failed to open) - stay silent rather than speak a
		//confusing error for what the user will otherwise just experience as "nothing happened".
		return;
	}

	AudioCapture::playProcessingCue();

	std::wstring recognizedText = whisperTranscriber.transcribe(pcm, AudioCapture::SAMPLE_RATE_HZ);

	//Printed to the console (FFUIDesktop is a console-subsystem exe, so stdout is always
	//available) so what SAPI actually heard can be checked directly against what was said -
	//the fastest way to tell "recognition misheard me" apart from "Claude answered oddly".
	//Converted to UTF-8 rather than using std::wcout directly - mixing narrow and wide iostream
	//operations on the same underlying stdout handle without an explicit console mode switch is
	//unreliable on Windows, and this project doesn't set one up elsewhere.
	std::cout << "[Voice Assistant] Recognized: "
		<< (recognizedText.empty() ? "(nothing recognized)" : ClaudeCliClient::wideToUtf8(recognizedText))
		<< std::endl;

	if (recognizedText.empty()) {
		speakAndWait(L"Sorry, I didn't catch that.");
		return;
	}

	std::string promptUtf8 = ClaudeCliClient::wideToUtf8(recognizedText);

	//cliClient.ask() blocks synchronously for however long the Claude round trip takes (up to
	//its own 30s timeout) - mostly parked inside a ReadFile() call waiting for the child's
	//stdout, not something this function can easily poll from the inside without restructuring
	//that pipe-draining logic. Instead, a small dedicated ticker thread plays a soft, repeating
	//"still working" cue for as long as the wait lasts, entirely separate from the thread
	//actually blocked in ask() below - so a several-second pause after the processing cue
	//doesn't read as the assistant having silently given up.
	std::atomic<bool> stillWaitingOnReply{ true };
	std::jthread thinkingTicker([&stillWaitingOnReply] {
		const auto tickPeriod = std::chrono::milliseconds(2500);
		while (stillWaitingOnReply.load()) {
			//Slept in short slices (not one long sleep_for(tickPeriod)) so this thread notices
			//stillWaitingOnReply going false promptly once the real reply arrives, rather than
			//up to a full tick period late.
			auto sliceEnd = std::chrono::steady_clock::now() + tickPeriod;
			while (stillWaitingOnReply.load() && std::chrono::steady_clock::now() < sliceEnd) {
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
			if (stillWaitingOnReply.load()) {
				AudioCapture::playThinkingTickCue();
			}
		}
		});

	ClaudeCliResult cliResult = cliClient.ask(promptUtf8, lastSessionId);

	//Stop and join explicitly (rather than just letting thinkingTicker's destructor do it at
	//scope exit) so it's guaranteed to have stopped calling Beep() before anything below - the
	//error/re-login/success narration - runs.
	stillWaitingOnReply.store(false);
	if (thinkingTicker.joinable()) thinkingTicker.join();

	if (!cliResult.success) {
		//A substring check, not a precise error-code match - same approximate-but-acceptable
		//spirit as extractJsonStringField's \u handling (see ClaudeCliClient.h). Could miss a
		//differently-worded error in a future CLI version; worth widening if that happens.
		bool looksLikeExpiredLogin =
			cliResult.rawError.find("Login expired") != std::string::npos ||
			cliResult.rawError.find("please run") != std::string::npos ||
			cliResult.rawError.find("/login") != std::string::npos;

		if (looksLikeExpiredLogin && !stoken.stop_requested()) {
			speakAndWait(L"I need you to sign back in to Claude.");
			//Re-runs just the login step (not the whole setup sequence - the CLI install and
			//speech provisioning already succeeded once). Blocks this worker thread the same
			//way the original guided login did; nothing else needs it while re-authenticating.
			//redoLogin() sets assistantSetup.state to a non-terminal value as its first action,
			//so assistantHasFloor() stays true (via isSetupInProgress()) for its entire
			//duration too - no gap between the line above and this call taking over.
			assistantSetup.redoLogin(stoken);
			cliClient.setCliPath(assistantSetup.resolvedCliPath());
		}
		else {
			speakAndWait(L"Sorry, I couldn't reach the assistant just now.");
		}
		//Deliberately leave lastSessionId untouched on failure - keep whatever last worked
		//rather than resetting the conversation over one hiccup.
		return;
	}

	lastSessionId = cliResult.sessionId;

	//speakAndWait(), not a fire-and-forget Speak() - this is the call that most needed it: it's
	//the longest utterance in the whole feature, so it's the one most likely for the user to
	//still be moving the stylus during, which previously could let a window-focus narration
	//clip it mid-sentence the instant exchangeInProgress flipped false right after Speak() was
	//merely issued (not finished). See assistantHasFloor()'s comment.
	std::wstring replyWide = ClaudeCliClient::utf8ToWide(cliResult.replyText);
	speakAndWait(replyWide);
}

void VoiceAssistant::runDictation(std::stop_token stoken) {
	std::vector<int16_t> pcm = audioCapture.stop();

	if (pcm.empty()) {
		//Nothing captured (e.g. the mic failed to open) - stay silent, same as runExchange()'s
		//own handling of this case.
		return;
	}

	AudioCapture::playDictateProcessingCue();

	std::wstring recognizedText = whisperTranscriber.transcribe(pcm, AudioCapture::SAMPLE_RATE_HZ);

	//Same console-diagnostic spirit as runExchange()'s own recognized-text log.
	std::cout << "[Dictate] Recognized: "
		<< (recognizedText.empty() ? "(nothing recognized)" : ClaudeCliClient::wideToUtf8(recognizedText))
		<< std::endl;

	if (recognizedText.empty()) {
		speakAndWait(L"Sorry, I didn't catch that.");
		return;
	}

	//No Claude call, no spoken reply - deliver the recognized text through the mailbox for
	//whatever haptic-thread flow requested it (or, if nothing did, it's simply consumed and
	//discarded the next time tryConsumePendingDictation() is polled - see that method's own
	//comment). Deliberately does NOT speak the recognized text back - StartMenuFlow (the only
	//consumer today) narrates its own "Searching for..." feedback once it picks this up.
	{
		std::lock_guard<std::mutex> lock(dictationResultMutex);
		pendingDictationResult = recognizedText;
		hasPendingDictationResult = true;
	}
}
