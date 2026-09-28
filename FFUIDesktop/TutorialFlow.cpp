#include "TutorialFlow.h"
#include "FFUIDesktop.h"          //for pAssistantVoice, and the endZ/roomDepth macros the Stage 3a guidance target mirrors
#include "WindowWallObject.h"     //for WindowManager (room state, ArchivedWindows, ACTIVE_ZONE_ROOM_COUNT)
#include "VoiceAssistant.h"       //for SetupState/SpeechSetupState, read during Stage 5
#include "VoiceSettingsManager.h" //for getFfuiRate() - tutorial narration's own speed
#include "AppConfig.h"            //persists hasCompletedTutorialOnce
#include <windows.h>
#include <sapi.h>
#include <iostream>
#include <mutex>

//There is no shared "which room am I in" enum anywhere in this codebase (confirmed while
//building this feature) - FFUIDesktop::updateFrame() derives it inline, every frame, from three
//independent signals: WindowManager::isControllingActiveRoom (the merged Program Slot room),
//`inFrontZone` (the shared front-zone Z-band), and world-X sign within that zone (X>=0 = Program
//Tray/Selector, X<0 = FFUI Settings - see WindowWallObject.cpp's own narration/magnetism code for
//the same split). classifyRoom() below just re-derives the same three-way split from the copies
//FrameInput already carries, rather than teaching WindowManager a new enum only this class needs.
//Hardware-test feedback: "some of the events that we get the user to trigger (e.g. moving into
//the front zone) trigger FFUI narrator outputs - for these specific events, could we make the
//tutorial narrator wait for the FFUI narrator to finish before starting?" Crossing into the
//active room (WindowManager::narrateWindowFocus, "Slot N, {app}"), clicking the scroll wheel to
//cycle slots (loadSlot()'s own narration), and crossing into the front zone
//(updateFrontZoneSectionNarration()) all speak through pSapiVoice independently of the tutorial,
//often on the exact same frame a tutorial checkpoint is satisfied. Now that pSapiVoice and
//pAssistantVoice can genuinely speak concurrently (SPVPRI_OVER - see FFUIDesktop::initDesktop()),
//that simultaneity became audible overlap instead of silently serialized speech - for these
//moments specifically, FFUI's own narrator should be heard first. Rather than threading a
//"should I wait this time" flag through every speakStageScript() call site, this polls
//pSapiVoice's own idle state once at the START of every tutorial utterance - a near-instant
//no-op whenever pSapiVoice isn't currently speaking, which is most of the time. Bounded at 2
//seconds so an unexpected SAPI status can never hang tutorial narration indefinitely - the same
//"generous but bounded, not guaranteed" tolerance this codebase already extends to
//AssistantSetup/WhisperTranscriber's own waits.
static void waitForFfuiNarratorIdle() {
	if (!pSapiVoice) return;

	//Second-round hardware-test feedback: "the tutorial narrators seem to trigger before the FFUI
	//ones in many cases, so don't end up waiting for them - could we mediate by adding a short
	//delay?" The polling loop below was checking pSapiVoice's status immediately, but a just-issued
	//SPF_ASYNC Speak() call on pSapiVoice (fired the same frame a tutorial checkpoint succeeds -
	//e.g. the room-focus/slot-cycle narration triggered by the very button press that satisfies the
	//checkpoint) doesn't necessarily register as SPRS_IS_SPEAKING in SAPI's own status the instant
	//Speak() returns - there's a brief window where GetStatus() still reports idle even though
	//audio is about to start. Polling that fast can catch that idle gap and skip the wait entirely.
	//A short fixed delay up front gives SAPI time to actually transition into IS_SPEAKING before
	//the loop starts checking.
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (std::chrono::steady_clock::now() < deadline) {
		SPVOICESTATUS status{};
		if (FAILED(pSapiVoice->GetStatus(&status, NULL)) || status.dwRunningState != SPRS_IS_SPEAKING) {
			return;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
}

TutorialFlow::Room TutorialFlow::classifyRoom(const FrameInput& input) {
	if (input.isControllingActiveRoom) return Room::ProgramSlot;
	if (input.inFrontZone) {
		return (input.deviceLoc.position.x >= 0.0f) ? Room::ProgramSelector : Room::FfuiSettings;
	}
	return Room::Unknown;
}

TutorialFlow::TutorialFlow() :
	//Boundary demo (Stage 2): matches SolidPlane's own real edge-buzz settings exactly
	//(SolidPlane.cpp's constructor) - "trigger a unique stimulus to represent the screen edges",
	//reused here verbatim so the demo genuinely feels like the real thing rather than an
	//approximation of it.
	demoBoundaryVibration(VibrationSettings{
		vibrationType_Periodic, 100.0f, 1.0f, vibrationProfile_sine,
		VibrationModulation{ modulationType_sine, 0.0015f, 0.0f, 0.1f, 0.2f }
		}),
	//TextField/Clickable/Unresponsive demos: match CursorStateHaptics' own current (post-halving
	//- "make them a bit more subtle - maybe half the magnitude") tuned settings exactly, for the
	//same "genuinely the real thing" reason as the boundary demo above. Duplicated here rather
	//than exposed from CursorStateHaptics itself, to avoid touching that already-tuned, already-
	//tested class just to share four literals - see this file's own header comment.
	demoTextFieldVibration(VibrationSettings{
		vibrationType_Periodic, 60.0f, 0.0f, vibrationProfile_sine,
		VibrationModulation{ modulationType_sine, 0.0005f, 0.0f, 0.08f, 0.10f }
		}),
	demoClickableVibration(VibrationSettings{
		vibrationType_Periodic, 40.0f, 0.0f, vibrationProfile_sine,
		VibrationModulation{ modulationType_sine, 0.0009f, 0.0f, 0.05f, 0.20f }
		}),
	demoUnresponsiveVibration(VibrationSettings{
		vibrationType_Periodic, 8.0f, 0.0f, vibrationProfile_sine,
		VibrationModulation{ modulationType_sine, 0.0010f, 0.0f, 0.25f, 0.6f }
		})
{
}

std::vector<std::wstring> TutorialFlow::openingLinesForStage0() const {
	std::vector<std::wstring> lines;
	if (isAutoStart) {
		lines.push_back(L"This is your first time launching FFUI. I am auto-starting the tutorial mode - double-press any button to skip.");
	}
	lines.push_back(L"Welcome to FFUI - this project explores the Windows operating system as a physical 3D space you can explore with force feedback.");
	lines.push_back(L"You can turn off the stylus at any time by pushing it all the way back onto the desk.");
	return lines;
}

std::vector<std::wstring> TutorialFlow::openingLinesForStage1() const {
	//"The Stylus" - just the intro line, plus (added per later hardware-test feedback - "can we
	//add an overview of buttons and locations at the beginning") one orientation line naming
	//every button and where it sits, before updateStage1() below walks through them one at a time
	//with its own press-to-continue checkpoints. Originally this named every button in one long,
	//uninterrupted narration block with no checkpoint at all; per an earlier round's feedback ("as
	//we introduce each button, can we make the user press it in order to move onto the next
	//one?"), that became the current per-button checkpoint design - this overview line doesn't
	//reintroduce that problem, since it's purely locational (no function is explained here; that's
	//still each checkpoint's own job) and isn't gated on any press of its own.
	return {
		L"Let's go over the stylus's buttons.",
		L"The stylus has a number of buttons. On top, there are two, one in front of the other - these are the select button, at the front, and a right-click button, at the rear. On the side, there's an AUX button, and a scroll wheel."
	};
}

void TutorialFlow::speakStageScript(std::vector<std::wstring> lines) {
	//Never called while a previous script is still speaking - every call site below checks
	//narrationInProgress first. Defensive only.
	if (narrationInProgress.load()) return;

	narrationInProgress.store(true);
	narrationThread = std::jthread([this, lines](std::stop_token stoken) {
		//Own COM apartment, mirroring StartMenuFlow's searchThread/launchThread and
		//AssistantSetup/VoiceAssistant's worker thread - every one of these spawns its own
		//CoInitializeEx/CoUninitialize pair since ISpVoice calls are COM calls and this can't
		//share the haptic thread's apartment.
		CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
		waitForFfuiNarratorIdle();

		if (pAssistantVoice) {
			//"all tutorial narration uses pAssistantVoice... but the FFUI narrator's speed
			//setting", per TUTORIAL_SPEC.md's Global Rules - neither existing pattern in this
			//codebase produces that combination (MenuSystem always speaks through pSapiVoice,
			//already tied to ffuiRate; pAssistantVoice is otherwise always driven by its own
			//assistantRate), so this temporarily overrides pAssistantVoice's rate for exactly the
			//duration of this script, restoring its own real rate afterward so Stage 5's real,
			//live AI-assistant interactions (triggered by the user's own actual AUX gestures, not
			//by this class) still sound like the assistant normally does. Reading
			//VoiceSettingsManager's rate from this background thread without a lock is a small,
			//accepted race - the same tolerance this codebase already extends to reading
			//pSapiVoice/pAssistantVoice pointers cross-thread elsewhere - since a voice-speed
			//change happens rarely and via a deliberate menu action, not concurrently with
			//tutorial narration in practice.
			long savedRate = 0;
			pAssistantVoice->GetRate(&savedRate);
			pAssistantVoice->SetRate(VoiceSettingsManager::getInstance().getFfuiRate());

			for (const std::wstring& line : lines) {
				if (stoken.stop_requested()) break;
				std::string narrow(line.begin(), line.end());
				std::cout << "[Narrate:Tutorial] \"" << narrow << "\"" << std::endl;
				pAssistantVoice->Speak(line.c_str(), SPF_ASYNC | SPF_PURGEBEFORESPEAK, NULL);
				pAssistantVoice->WaitUntilDone(INFINITE);
			}

			pAssistantVoice->SetRate(savedRate);
		}

		CoUninitialize();
		//Last statement before returning - see this class's own header comment on why checking
		//this before ever reassigning narrationThread again is safe (the jthread's own
		//move-assignment join() will return near-instantly, since the thread function has
		//already finished by the time anything reads this flag as false).
		narrationInProgress.store(false);
	});
}

void TutorialFlow::runStage2DemoSequence() {
	if (narrationInProgress.load()) return;

	narrationInProgress.store(true);
	narrationThread = std::jthread([this](std::stop_token stoken) {
		CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
		waitForFfuiNarratorIdle();

		long savedRate = 0;
		if (pAssistantVoice) {
			pAssistantVoice->GetRate(&savedRate);
			pAssistantVoice->SetRate(VoiceSettingsManager::getInstance().getFfuiRate());
		}

		auto speak = [&](const std::wstring& line) {
			if (stoken.stop_requested() || !pAssistantVoice) return;
			std::string narrow(line.begin(), line.end());
			std::cout << "[Narrate:Tutorial] \"" << narrow << "\"" << std::endl;
			pAssistantVoice->Speak(line.c_str(), SPF_ASYNC | SPF_PURGEBEFORESPEAK, NULL);
			pAssistantVoice->WaitUntilDone(INFINITE);
			};

		//Speaks the line describing a demo vibration, then holds that vibration "on" for long
		//enough to be clearly felt (1.5s comfortably covers at least two full repeat cycles of
		//even the slowest of the four - Unresponsive's 0.6s repeatPeriod) before moving on -
		//updateForce() reads activeDemoVibration every haptic frame to decide which
		//HapticVibration to actually drive with processVibration()/sleepVibration(), the same
		//pattern CursorStateHaptics::update() already uses for the real, live versions of these.
		auto demo = [&](DemoVibrationKind kind, const std::wstring& line) {
			speak(line);
			if (stoken.stop_requested()) return;
			activeDemoVibration.store(kind);
			std::this_thread::sleep_for(std::chrono::milliseconds(1500));
			activeDemoVibration.store(DemoVibrationKind::None);
			};

		demo(DemoVibrationKind::Boundary, L"This means you're at a hard boundary, such as the screen edge.");
		demo(DemoVibrationKind::TextField, L"This means you're hovering over a text field.");
		demo(DemoVibrationKind::Clickable, L"This means you're hovering over clickable text - for example, a hyperlink.");
		demo(DemoVibrationKind::Unresponsive, L"This means the program is busy or unresponsive.");
		speak(L"Some surfaces are solid; others will pop through if you push.");

		if (pAssistantVoice) pAssistantVoice->SetRate(savedRate);
		CoUninitialize();
		narrationInProgress.store(false);
	});
}

bool TutorialFlow::waitForCondition(bool conditionMet, const std::wstring& reminderLine, bool suppressReminder) {
	if (conditionMet) {
		reminderCount = 0;
		return true;
	}
	if (reminderCount >= TUTORIAL_MAX_REMINDERS_BEFORE_AUTO_ADVANCE) {
		//"what if they never do it" - not explicitly answered in TUTORIAL_SPEC.md's original
		//open question, but leaving the tutorial stuck forever (e.g. if the user declines a
		//Stage 5 setup prompt entirely, or the Program Tray genuinely has nothing to drag)
		//is clearly worse than moving on - see TUTORIAL_STAGE_MAX_REMINDERS... comment in the header.
		reminderCount = 0;
		return true;
	}

	//Bug fix (first hardware test: "the repeat period timer to only start after the previous
	//prompt finishes - right now, it seems to always repeat prompts immediately"): the old code
	//stamped lastReminderTime at the MOMENT a reminder started speaking, then measured the next
	//interval from there - so a reminder line that itself took several seconds to speak already
	//ate into (or exceeded) TUTORIAL_REPEAT_REMINDER_SECONDS before it even finished, making the
	//very next check fire again almost immediately. Fix: detect the narrationInProgress
	//true->false edge (the previous reminder, or this checkpoint's own preceding transition line,
	//just actually finished) and restart the countdown from THAT moment instead.
	bool narrating = narrationInProgress.load();
	if (lastKnownNarrating && !narrating) {
		lastReminderTime = std::chrono::steady_clock::now();
	}
	lastKnownNarrating = narrating;

	auto now = std::chrono::steady_clock::now();

	//Third-round-of-testing bug fix: some checkpoints wait on a long-running, multi-step process
	//(AssistantSetup) that narrates its OWN progress through the same pAssistantVoice this class
	//itself speaks through. Once the user has clearly acted and that process is under way, this
	//checkpoint has nothing further to add - so hold the reminder countdown right here instead of
	//letting it keep expiring and firing every TUTORIAL_REPEAT_REMINDER_SECONDS regardless (which
	//would purge/cut off whatever the setup flow's own narration was mid-sentence, over and over -
	//see waitForCondition()'s own header comment for the exact feedback this fixes). Stamping
	//lastReminderTime here too means a reminder doesn't fire the instant suppression lifts (e.g.
	//right as the process fails and control returns to this checkpoint) - the full interval still
	//has to elapse first, same as any other reminder.
	if (suppressReminder) {
		lastReminderTime = now;
		return false;
	}

	//Skip firing a reminder while a previous one (or this stage's own opening/transition line) is
	//still speaking, rather than queuing up - SPF_PURGEBEFORESPEAK would just cut it off anyway,
	//and this keeps narrationThread reassignment safe (see TutorialFlow.h's own comment).
	if (!narrating && (now - lastReminderTime) >= std::chrono::seconds(TUTORIAL_REPEAT_REMINDER_SECONDS)) {
		lastReminderTime = now;
		reminderCount++;
		speakStageScript({ reminderLine });
	}
	return false;
}

void TutorialFlow::enterStage(Stage newStage) {
	stage = newStage;
	subStep = 0;
	checkpointAnnounced = false;
	reminderCount = 0;
	programDroppedFlag = false;
	stage2DemoStarted = false;
	stage4TrayWasEmpty = false;
	stage4EmptyCheckStartTime = std::chrono::steady_clock::time_point{};
	phase = Phase::Narrating;
	lastReminderTime = std::chrono::steady_clock::now();
	//The switch below is about to (re)start narration via speakStageScript() - seed this true so
	//waitForCondition()'s true->false edge detection (see its own comment) correctly treats that
	//narration as "in flight" from the start, rather than mistaking the not-yet-begun state for an
	//edge and restarting the reminder countdown too early.
	lastKnownNarrating = true;

	switch (newStage) {
	case Stage::Stage0_Intro: speakStageScript(openingLinesForStage0()); break;
	case Stage::Stage1_Stylus: speakStageScript(openingLinesForStage1()); break;
	case Stage::Stage2_Haptics:
		speakStageScript({ L"The stylus uses force feedback to present UI elements as solid surfaces, and to create vibration effects. Different vibrations tell you what you're hovering over." });
		break;
	case Stage::Stage3_ProgramSlot:
		speakStageScript({ L"I am guiding you towards the main room, called the Program Slot." });
		break;
	case Stage::Stage4_ProgramSelector:
		speakStageScript({ L"This is the Program Tray. Here, you can assign your open programs to quickslots to use in the main Program Slot room. Every program running on your machine is represented as a tile. When you move to a new tile, FFUI reads out the program name." });
		break;
	case Stage::Stage5_Settings:
		//Rewritten after hardware-test feedback ("some of the language is a bit confusing and
		//vulnerable to extra confusion in case the user starts clicking on menu items"). The old
		//single line had two concrete problems - it called the first tile "the Start Menu" when
		//the real tile (see FFUIDesktop.cpp's kSettingsTileLabels) is labelled "Start programs",
		//and it never mentioned the room now holds 5 tiles total (quickslots, FFUI narrator, AI
		//narrator, Start programs, and FFUI Tutorial), several of which didn't exist yet when this
		//line was first written - and one structural problem: this stage's own checkpoints
		//(updateStage5() below) never ask the user to touch any tile at all, they ask for an
		//AUX-button hold instead, so a room that visually looks like "here are settings to click"
		//invites exactly the kind of exploratory clicking that could leave someone stuck in an
		//unfamiliar submenu mid-checkpoint. Fixed by explaining hover-vs-select up front, naming
		//the safety net (every menu has a Back item), and explicitly saying none of the tiles are
		//needed for what comes next.
		speakStageScript({
			L"This is the FFUI Settings room. Moving between tiles just reads out their name - nothing happens until you press select, and every menu you open has a Back option to bring you right back here.",
			L"There are tiles here for adjusting your quickslots, the FFUI narrator's voice, the AI assistant's voice, launching programs by voice, and replaying this tutorial - feel free to explore them later. For now, you don't need to touch any of them: dictation and the AI assistant are both turned on using the AUX button instead, which I'll walk you through next."
			});
		break;
	case Stage::Stage6_End:
		speakStageScript({ L"Thanks for completing the FFUI tutorial! You can re-launch it at any time by selecting the FFUI Tutorial tile in the FFUI Settings room." });
		break;
	default: break;
	}
}

void TutorialFlow::goToSubStep(int newSubStep) {
	subStep = newSubStep;
	checkpointAnnounced = false;
}

void TutorialFlow::advanceToNextStage() {
	switch (stage) {
	case Stage::Stage0_Intro: enterStage(Stage::Stage1_Stylus); break;
	case Stage::Stage1_Stylus: enterStage(Stage::Stage2_Haptics); break;
	case Stage::Stage2_Haptics: enterStage(Stage::Stage3_ProgramSlot); break;
	case Stage::Stage3_ProgramSlot: enterStage(Stage::Stage4_ProgramSelector); break;
	case Stage::Stage4_ProgramSelector: enterStage(Stage::Stage5_Settings); break;
	case Stage::Stage5_Settings: enterStage(Stage::Stage6_End); break;
	case Stage::Stage6_End:
		stage = Stage::Inactive;
		//"Persistence of 'first launch ever'" - written into the consolidated ffui-config.ini via
		//AppConfig, per the request, rather than a separate file of its own.
		AppConfig::getInstance().setBool(L"tutorial.hasCompletedTutorialOnce", true);
		break;
	default: break;
	}
}

void TutorialFlow::begin(bool autoStart) {
	isAutoStart = autoStart;
	autoStartAbandonWindowActive = autoStart;
	waitingOnFirstPress = true;
	enterStage(Stage::Stage0_Intro);
}

void TutorialFlow::abandon() {
	//Cuts the current narration off immediately rather than letting it finish - request_stop()
	//alone (not a full join here) is enough: narrationThread's own move-assignment/destructor
	//will join it the next time this class actually needs to (the next begin(), or process exit),
	//and the background lambda's for-loop already checks stoken.stop_requested() between lines.
	if (narrationThread.joinable()) narrationThread.request_stop();

	stage = Stage::Inactive;
	//Double-pressing to abandon is an intentional "I've seen this, skip it" gesture, not an
	//accident - treated the same as actually finishing, so it doesn't keep re-offering itself on
	//every subsequent launch (see advanceToNextStage()'s own Stage6 comment for why this key
	//exists at all).
	AppConfig::getInstance().setBool(L"tutorial.hasCompletedTutorialOnce", true);

	std::cout << "[Tutorial] Abandoned via double-press." << std::endl;
}

void TutorialFlow::checkDoublePressAbandon(const FrameInput& input) {
	//"double tap to skip the tutorial should only be an active option at the very beginning -
	//after that, it should no longer apply" - Gareth's own answer. autoStartAbandonWindowActive
	//is set only by begin(autoStart=true) and never re-armed, so once Stage 0 ends (see
	//advanceToNextStage()) this whole check is permanently inert for the rest of this tutorial
	//run, regardless of what stage is later re-entered by any future begin() call.
	if (stage != Stage::Stage0_Intro || !autoStartAbandonWindowActive) return;

	bool anyPress = input.selectPressedEdge || input.auxPressedEdge || input.middleButtonPressedEdge;
	if (!anyPress) return;

	auto now = std::chrono::steady_clock::now();
	if (!waitingOnFirstPress && (now - lastAnyButtonPressTime) <= std::chrono::milliseconds(TUTORIAL_DOUBLE_PRESS_WINDOW_MS)) {
		abandon();
		return;
	}
	waitingOnFirstPress = false;
	lastAnyButtonPressTime = now;
}

void TutorialFlow::notifyProgramDropped() {
	if (stage == Stage::Stage4_ProgramSelector) {
		programDroppedFlag = true;
	}
}

void TutorialFlow::updateStage1(const FrameInput& input) {
	//Per hardware-test feedback ("as we introduce each button, can we make the user press it in
	//order to move onto the next one?") - each button gets its own instruction, spoken exactly
	//once immediately on entering that subStep (via checkpointAnnounced, not left to
	//waitForCondition()'s own repeat-interval timing - see that member's own comment), then waits
	//for a press of that specific button before moving to the next one. The rear button's real
	//function (an OS right-click) is only meaningful once actually browsing a program, which
	//hasn't been taught yet at this point in the tour - so, same as the "Future functionality,
	//deferred" section of TUTORIAL_SPEC.md notes, its own step here is just "press it to finish
	//the tour", confirming the button exists and responds without yet explaining right-clicking
	//itself.
	switch (subStep) {
	case 0: { //Select
		if (!checkpointAnnounced && !narrationInProgress.load()) {
			//Guarded on narrationInProgress too, not just checkpointAnnounced - if the PREVIOUS
			//checkpoint's own line is still finishing (e.g. the user pressed the previous button
			//while its instruction was still mid-sentence, so this subStep began before that audio
			//actually finished), speakStageScript() would silently drop this call entirely (see its
			//own early-return). Waiting one more frame here costs nothing and guarantees this
			//instruction is never lost - still "as soon as possible", never on a fixed timer.
			checkpointAnnounced = true;
			speakStageScript({ L"The front button, under your fingertip, is Select - press it to interact with whatever you're touching: choosing a menu item, or grabbing and dropping a program. Go ahead and press it now." });
		}
		if (waitForCondition(input.selectPressedEdge, L"Press the front button, Select, to continue.")) {
			goToSubStep(1);
		}
		break;
	}
	case 1: { //Aux
		if (!checkpointAnnounced && !narrationInProgress.load()) {
			//Guarded on narrationInProgress too, not just checkpointAnnounced - if the PREVIOUS
			//checkpoint's own line is still finishing (e.g. the user pressed the previous button
			//while its instruction was still mid-sentence, so this subStep began before that audio
			//actually finished), speakStageScript() would silently drop this call entirely (see its
			//own early-return). Waiting one more frame here costs nothing and guarantees this
			//instruction is never lost - still "as soon as possible", never on a fixed timer.
			checkpointAnnounced = true;
			speakStageScript({ L"The side button is Aux. Tap it once, on its own, to hear what the stylus is currently on - that's called Narrate. Hold it to dictate speech into a text field. Tap it once, then hold, to talk to the built-in Claude AI assistant instead. Go ahead and press it now." });
		}
		if (waitForCondition(input.auxPressedEdge, L"Press the side button, Aux, to continue.")) {
			goToSubStep(2);
		}
		break;
	}
	case 2: { //Scroll wheel click
		if (!checkpointAnnounced && !narrationInProgress.load()) {
			//Guarded on narrationInProgress too, not just checkpointAnnounced - if the PREVIOUS
			//checkpoint's own line is still finishing (e.g. the user pressed the previous button
			//while its instruction was still mid-sentence, so this subStep began before that audio
			//actually finished), speakStageScript() would silently drop this call entirely (see its
			//own early-return). Waiting one more frame here costs nothing and guarantees this
			//instruction is never lost - still "as soon as possible", never on a fixed timer.
			checkpointAnnounced = true;
			//"don't mention the slot switching on the middle button during the button intro - just
			//call it the middle button here - we didn't introduce the concept of slots yet" -
			//second-round hardware-test feedback. Quick slots get their own proper introduction in
			//Stage 3 (see updateStage3()'s case 1, which uses the real term), so this is
			//deliberately just "the middle button" here, with no mention of what it does yet.
			speakStageScript({ L"The scroll wheel under your thumb also works as a middle button. Go ahead and press it now." });
		}
		if (waitForCondition(input.middleButtonPressedEdge, L"Press the middle button to continue.")) {
			goToSubStep(3);
		}
		break;
	}
	case 3: { //Rear/Right-click
		if (!checkpointAnnounced && !narrationInProgress.load()) {
			//Guarded on narrationInProgress too, not just checkpointAnnounced - if the PREVIOUS
			//checkpoint's own line is still finishing (e.g. the user pressed the previous button
			//while its instruction was still mid-sentence, so this subStep began before that audio
			//actually finished), speakStageScript() would silently drop this call entirely (see its
			//own early-return). Waiting one more frame here costs nothing and guarantees this
			//instruction is never lost - still "as soon as possible", never on a fixed timer.
			checkpointAnnounced = true;
			speakStageScript({ L"The rear button sends a right-click, the same as right-clicking with a mouse, once you're browsing a program. It won't do anything right now, but go ahead and press it to finish this tour." });
		}
		if (waitForCondition(input.rearButtonPressedEdge, L"Press the rear button to finish the tour.")) {
			advanceToNextStage();
		}
		break;
	}
	default: advanceToNextStage(); break;
	}
}

void TutorialFlow::updateStage2(const FrameInput&) {
	if (!stage2DemoStarted) {
		stage2DemoStarted = true;
		runStage2DemoSequence();
		return;
	}
	if (!narrationInProgress.load()) {
		advanceToNextStage();
	}
}

void TutorialFlow::updateStage3(const FrameInput& input) {
	switch (subStep) {
	case 0: {
		//3a - guide into the Program Slot room. The guidance FORCE itself is rendered from
		//updateForce() (see its own comment) - this just watches for arrival and narrates.
		//Deliberately NOT using the checkpointAnnounced immediate-announce pattern here - this
		//reminder text is verbatim the stage's own opening narration (already just spoken in
		//enterStage()), so an immediate repeat the moment this checkpoint starts watching would
		//just be redundant; the normal interval-gated reminder is the right cadence for it.
		if (waitForCondition(input.isControllingActiveRoom, L"I am guiding you towards the main room, called the Program Slot.")) {
			goToSubStep(1);
			speakStageScript({ L"This is where you can work with your active program. While in here, you are controlling the mouse cursor on the active program, and getting haptic feedback from it." });
		}
		break;
	}
	case 1: {
		//3b - cycle quickslots via the real scroll-click. loadSlot()'s own real "Slot N, {app
		//name}" narration already covers the actual press's confirmation - this only reminds
		//until it happens once.
		if (!checkpointAnnounced && !narrationInProgress.load()) {
			checkpointAnnounced = true;
			speakStageScript({ L"You have a number of quick slots in which you can store different programs. To swap between them, use the scroll click - try it now." });
		}
		if (waitForCondition(input.middleButtonPressedEdge, L"Use the scroll click to swap quick slots.")) {
			goToSubStep(2);
			speakStageScript({ L"If you push forward, you'll feel a wall. Behind it are the Program Tray, on the right, and FFUI Settings, on the left. To reach them, gently push through this wall - you'll feel a pop." });
		}
		break;
	}
	case 2: {
		//3c - cross into the shared front zone, either side. Already announced immediately at
		//case 1's exit above (same text reused as the repeat reminder here), so no separate
		//checkpointAnnounced call needed.
		if (waitForCondition(input.inFrontZone, L"If you push forward, you'll feel a wall. Behind it are the Program Tray, on the right, and FFUI Settings, on the left. To reach them, gently push through this wall - you'll feel a pop.")) {
			goToSubStep(3);
		}
		break;
	}
	case 3: {
		//3d - route to the Program Tray specifically, if the user landed in FFUI Settings
		//instead. Previously had NO announcement at all on entering this case - the routing line
		//only ever got spoken via waitForCondition()'s reminder timer, several seconds after
		//arriving in the front zone with nothing said yet. Now announced immediately instead.
		Room room = classifyRoom(input);
		if (room == Room::ProgramSelector) {
			advanceToNextStage();
			break;
		}
		if (!checkpointAnnounced && !narrationInProgress.load()) {
			checkpointAnnounced = true;
			speakStageScript({ L"Move to the Program Tray, on the right-hand side - you'll feel another pop-through wall in the middle of the two rooms." });
		}
		if (waitForCondition(false, L"Move to the Program Tray, on the right-hand side.")) {
			//waitForCondition(false, ...) only returns true via the reminder-cap safety net here -
			//move on regardless of which side the user ended up on, rather than waiting forever.
			advanceToNextStage();
		}
		break;
	}
	default: advanceToNextStage(); break;
	}
}

void TutorialFlow::updateStage4(const FrameInput& input) {
	switch (subStep) {
	case 0: {
		//4b - assign a program, OR the empty-tray fallback. "empty program tray is a true
		//challenge - could we fill it with 3 'dummy' programs..." - Gareth's answer confirmed
		//this is the right idea, but synthesizing fake Program Tray tiles safely needs a deeper
		//change to ObjectsFactory.cpp's per-scan-cycle window synthesis than this first pass
		//implements (see TUTORIAL_SPEC.md's own implementation note) - v1 narrates an honest
		//explanation and moves on instead of waiting on a drop that could never happen.
		//Hardware-test feedback: "I still got a false trigger on the program list empty check -
		//after I start dragging a program." The 1-second debounce below was built for a
		//TRANSIENT scan-cycle blip, but a real drag isn't transient: ArchivedWindows is cleared
		//for the entire duration of any grab (see initiatePeriodicScanner()'s own comment on why),
		//and a deliberate, careful drag routinely takes well over a second - long enough to cross
		//the debounce threshold for real. So the very act of doing what this checkpoint asks
		//(grab a tray tile and drag it) was itself what triggered the "tray is empty, give up"
		//fallback. Fixed by treating an active grab as unambiguous proof the tray had something in
		//it - there's no need to even look at ArchivedWindows while isUserGrabbingWindow is true.
		bool grabInProgress = WindowManager::getInstance().isUserGrabbingWindow.load();
		bool trayEmptyNow = false;
		if (!grabInProgress) {
			std::lock_guard<std::recursive_mutex> lock(WindowManager::getInstance().windowMutex);
			trayEmptyNow = WindowManager::getInstance().ArchivedWindows.empty();
		}
		if (grabInProgress || !trayEmptyNow) {
			//Not empty (or a grab is actively in progress, which implies the same thing) this
			//frame - clear any in-progress debounce window (see TUTORIAL_TRAY_EMPTY_DEBOUNCE_MS's
			//own comment) so a stray earlier empty reading can't combine with a later, unrelated
			//one to falsely cross the debounce threshold.
			stage4EmptyCheckStartTime = std::chrono::steady_clock::time_point{};
		}
		else {
			//Hardware-test feedback: "the 'program tray empty' check false triggered for me."
			//ArchivedWindows can read transiently empty for reasons that have nothing to do with
			//the tray actually being empty - it's deliberately cleared to build drag placeholders
			//during ANY window grab elsewhere in the app, and only repopulates on the scanner's
			//next full cycle after release (see initiatePeriodicScanner()'s own comment). Don't
			//trust a single frame's reading - only commit to "truly empty" once it's stayed empty
			//continuously for TUTORIAL_TRAY_EMPTY_DEBOUNCE_MS.
			if (stage4EmptyCheckStartTime == std::chrono::steady_clock::time_point{}) {
				stage4EmptyCheckStartTime = std::chrono::steady_clock::now();
			}
			bool debounced = (std::chrono::steady_clock::now() - stage4EmptyCheckStartTime)
				>= std::chrono::milliseconds(TUTORIAL_TRAY_EMPTY_DEBOUNCE_MS);
			if (debounced) {
				stage4TrayWasEmpty = true;
				speakStageScript({ L"Your program tray is currently empty, so there's nothing to try this step with right now. Once you have another program open, you can practice this from the FFUI Tutorial tile in FFUI Settings. For now, let's move on." });
				goToSubStep(2);
				break;
			}
		}
		if (!checkpointAnnounced && !narrationInProgress.load()) {
			checkpointAnnounced = true;
			speakStageScript({ L"To assign a program to a quickslot, hover over its tile, hold the select button, and pull it forward. You'll feel it pop between the quickslots. Let go of the button to drop it in one." });
		}
		if (waitForCondition(programDroppedFlag, L"Hold the select button on a tile and pull it forward to assign it.")) {
			programDroppedFlag = false;
			speakStageScript({ L"After assigning a program, FFUI automatically loads that quickslot in the main room." });
			goToSubStep(2);
		}
		break;
	}
	case 2: {
		//Exit routing - simplified for v1 to one generic instruction regardless of which of the
		//two front-zone rooms the user is currently in (TUTORIAL_SPEC.md's original draft
		//branches the wording by origin room; collapsed here to reduce risk in this first pass).
		//Announced immediately (guarded on narrationInProgress so it never collides with whichever
		//exit line case 0 just spoke above) rather than left to waitForCondition()'s own reminder
		//timer, which previously left this step silent for several seconds after arriving here.
		if (!checkpointAnnounced && !narrationInProgress.load()) {
			checkpointAnnounced = true;
			speakStageScript({ L"Now let's find FFUI Settings. Push toward the front zone, and to the left." });
		}
		if (waitForCondition(classifyRoom(input) == Room::FfuiSettings, L"Push toward the front zone, and to the left, to find FFUI Settings.")) {
			advanceToNextStage();
		}
		break;
	}
	default: advanceToNextStage(); break;
	}
}

void TutorialFlow::updateStage5(const FrameInput&, VoiceAssistant& voiceAssistant) {
	switch (subStep) {
	case 0: {
		//5b/5c - dictation. Skips the prompt narration entirely if already set up, per spec
		//("If already set up: skip straight to 5c"). The actual opt-in itself happens through the
		//user's own real AUX-hold gesture and its existing confirmation prompt - this narrates
		//and watches VoiceAssistant's own speech-setup state, it never triggers setup itself.
		if (voiceAssistant.speechSetupState() == SpeechSetupState::Ready) {
			goToSubStep(1);
			speakStageScript({ L"You can now dictate into any text field. Hold the AUX button - you'll hear two beeps, meaning the microphone is listening. Release AUX when you're done; you'll hear two more beeps confirming it's finished listening." });
			break;
		}
		//Announced immediately (guarded on narrationInProgress, same reasoning as every other
		//checkpoint above) rather than left entirely to waitForCondition()'s own reminder timer -
		//this is genuinely new information the stage's own opening narration didn't cover.
		//Reworded per later hardware-test feedback ("make it clear that the user will have the
		//choice to set up dictation / AI assist or not after the hold aux - right now it could
		//feel like it's pulling you into setting it up without an exit path"). Holding AUX always
		//opens SetupConfirmationPrompt's two-detent confirm/cancel prompt first (Cancel is even
		//its safe default - see that class's own comment), so the exit path already existed in
		//practice; this line just says so up front instead of reading like a commitment.
		if (!checkpointAnnounced && !narrationInProgress.load()) {
			checkpointAnnounced = true;
			speakStageScript({ L"You can dictate text into any text field. This needs a one-time setup, which just downloads a small speech-recognition file - no account needed. Hold the AUX button now, and I'll bring up the choice - you can go ahead with it, or decide not to, right there." });
		}
		//Suppressed for as long as the assistant/setup machinery has the floor - NOT just once
		//speechSetupState() itself has left NotStarted (that was the previous, still-broken fix) -
		//holding AUX first opens SetupConfirmationPrompt's two-detent confirm/cancel prompt, which
		//narrates through pAssistantVoice too and keeps speechSetupState() sitting at NotStarted
		//the whole time it's up, since nothing has actually been confirmed to start yet - so the
		//reminder kept firing and purging that confirmation dialogue exactly as before, just for a
		//window slightly earlier than the one that fix addressed. assistantHasFloor() is the
		//correct, already-existing signal for this: it folds in confirmationPromptActive
		//(the confirm/cancel prompt itself, from its opening question to the moment it hands off
		//to setup or narrates "Okay, not enabling...") together with isSetupInProgress() (the
		//actual download/install afterward) - so this now covers watching for the AUX hold itself,
		//through the confirmation prompt closing, through setup finishing, as one continuous
		//window with no gap for a reminder to sneak into.
		bool dictationSetupUnderway = voiceAssistant.assistantHasFloor();
		bool done = waitForCondition(
			voiceAssistant.speechSetupState() == SpeechSetupState::Ready || voiceAssistant.speechSetupState() == SpeechSetupState::Failed,
			L"Hold the AUX button now to bring up the dictation setup choice, whenever you're ready.",
			dictationSetupUnderway);
		if (done) {
			if (voiceAssistant.speechSetupState() == SpeechSetupState::Ready) {
				speakStageScript({ L"You can now dictate into any text field. Hold the AUX button - you'll hear two beeps, meaning the microphone is listening. Release AUX when you're done; you'll hear two more beeps confirming it's finished listening." });
			}
			goToSubStep(1);
		}
		break;
	}
	case 1: {
		//5d/5e - the AI assistant, same pattern as above against the full setup state.
		if (voiceAssistant.setupState() == SetupState::Ready) {
			goToSubStep(2);
			speakStageScript({ L"Tap the AUX button once, then hold it - you'll hear a different beep, indicating Claude is listening. Release AUX when you're done; you'll hear another beep confirming Claude has stopped listening." });
			break;
		}
		//Reworded for the same reason as the dictation checkpoint above ("make it clear...right
		//now it could feel like it's pulling you into setting it up without an exit path").
		if (!checkpointAnnounced && !narrationInProgress.load()) {
			checkpointAnnounced = true;
			speakStageScript({ L"There's also a Claude AI assistant built in, which can answer questions for you. This needs a one-time setup too. Tap the AUX button once, then hold it, and I'll bring up the choice - you can go ahead with it, or decide not to, right there." });
		}
		//Same fix as the dictation checkpoint above, and the same assistantHasFloor() signal -
		//this gesture opens the very same two-detent confirmation prompt (just with a different
		//SetupKind/description - see SetupKind's own comment), so it needs the same "watch from
		//the AUX hold itself through the confirmation prompt closing through setup finishing"
		//coverage, not just setupState() leaving NotStarted.
		bool assistantSetupUnderway = voiceAssistant.assistantHasFloor();
		bool done = waitForCondition(
			voiceAssistant.setupState() == SetupState::Ready || voiceAssistant.setupState() == SetupState::Failed,
			L"Tap then hold the AUX button now to bring up the AI assistant setup choice, whenever you're ready.",
			assistantSetupUnderway);
		if (done) {
			if (voiceAssistant.setupState() == SetupState::Ready) {
				speakStageScript({ L"Tap the AUX button once, then hold it - you'll hear a different beep, indicating Claude is listening. Release AUX when you're done; you'll hear another beep confirming Claude has stopped listening." });
			}
			goToSubStep(2);
		}
		break;
	}
	case 2: {
		if (!narrationInProgress.load()) advanceToNextStage();
		break;
	}
	default: advanceToNextStage(); break;
	}
}

void TutorialFlow::update(const FrameInput& input, VoiceAssistant& voiceAssistant) {
	if (stage == Stage::Inactive) return;

	checkDoublePressAbandon(input);
	if (stage == Stage::Inactive) return;   //just abandoned this same frame

	if (phase == Phase::Narrating) {
		if (narrationInProgress.load()) return;
		phase = Phase::SkipWindowOpen;
		phaseEnteredTime = std::chrono::steady_clock::now();
		lastReminderTime = phaseEnteredTime;
		lastKnownNarrating = false;   //the opening narration this stage just spoke has ended
		//Deliberately falls through to the SkipWindowOpen handling below THIS SAME FRAME, rather
		//than returning - see that block's own comment for why.
	}

	if (phase == Phase::SkipWindowOpen) {
		if (input.selectPressedEdge) {
			advanceToNextStage();
			return;
		}
		bool skipWindowElapsed = (std::chrono::steady_clock::now() - phaseEnteredTime) >= std::chrono::seconds(TUTORIAL_STAGE_SKIP_WINDOW_SECONDS);
		if (skipWindowElapsed) {
			phase = Phase::AwaitingAction;
		}
		//Hardware-test feedback: "the guidance force lets go the moment the user reaches the
		//target, but the narrator confirming it was delayed" - and, more generally, "the delay
		//between tutorial narrator outputs is a bit too long." Both traced back to the same root
		//cause: a stage's own checkpoint-watching (updateStageN() below) used to be gated behind
		//BOTH the opening narration finishing AND the full skip window elapsing, even though the
		//skip window's only real job is to keep a select-press meaning "skip this whole stage"
		//live for a few seconds - it was never meant to also hold back checkpoint detection that
		//has nothing to do with select presses (room arrivals, other button presses, setup state).
		//Stage 0 and Stage 6 are the one exception: pure timed narration with no real checkpoint of
		//their own, where the skip window's pause IS the entire point of this phase, so they still
		//wait for skipWindowElapsed before advancing.
		if (stage == Stage::Stage0_Intro || stage == Stage::Stage6_End) {
			if (!skipWindowElapsed) return;
		}
		//Falls through to the checkpoint dispatch below - either the skip window already elapsed,
		//or this stage has a real checkpoint that shouldn't sit idle waiting for it.
	}

	switch (stage) {
	case Stage::Stage0_Intro: advanceToNextStage(); break;   //pure narration, no checkpoint
	case Stage::Stage1_Stylus: updateStage1(input); break;
	case Stage::Stage2_Haptics: updateStage2(input); break;
	case Stage::Stage3_ProgramSlot: updateStage3(input); break;
	case Stage::Stage4_ProgramSelector: updateStage4(input); break;
	case Stage::Stage5_Settings: updateStage5(input, voiceAssistant); break;
	case Stage::Stage6_End: advanceToNextStage(); break;
	default: break;
	}
}

Vector3 TutorialFlow::updateForce(Location currentLoc) {
	if (stage == Stage::Stage2_Haptics) {
		DemoVibrationKind kind = activeDemoVibration.load();
		Vector3 force(0, 0, 0);

		if (kind == DemoVibrationKind::Boundary) force.y += demoBoundaryVibration.processVibration();
		else demoBoundaryVibration.sleepVibration();

		if (kind == DemoVibrationKind::TextField) force.y += demoTextFieldVibration.processVibration();
		else demoTextFieldVibration.sleepVibration();

		if (kind == DemoVibrationKind::Clickable) force.y += demoClickableVibration.processVibration();
		else demoClickableVibration.sleepVibration();

		if (kind == DemoVibrationKind::Unresponsive) force.y += demoUnresponsiveVibration.processVibration();
		else demoUnresponsiveVibration.sleepVibration();

		return force;
	}

	//Loosened from also requiring phase==AwaitingAction after first hardware test ("the force
	//pulling towards the centre of the workspace slot didn't work - I couldn't feel anything") -
	//the old gate meant this pull only started once Stage 3's OWN opening narration had fully
	//finished AND its 5-second skip window had also elapsed (roughly 8-9 seconds after entering
	//the stage), during which the guidance force was silently zero the whole time. subStep==0
	//alone is enough - it stays 0 through Narrating/SkipWindowOpen too (only advances once the
	//room is actually reached, in updateStage3()), so there's no reason to also gate on phase; the
	//pull can safely start guiding the moment Stage 3 begins.
	if (stage == Stage::Stage3_ProgramSlot && subStep == 0) {
		//3a's guidance pull toward the merged Program Slot room - a plain capped proportional
		//spring, same style as WindowManager::computeGrabDetentForce(). wallCenterZ mirrors
		//FFUIDesktop.cpp's own local constexpr of the same name exactly (see that file's own
		//comment for why it's a duplicated plain formula rather than read back off a live
		//object) - duplicated here rather than shared, since it's a compile-time constant
		//derived from fixed macros/constants already visible in both translation units.
		constexpr float wallCenterZ = endZ - 5.0f - roomDepth - ((roomDepth + 10.0f) * (WindowManager::ACTIVE_ZONE_ROOM_COUNT - 1));
		constexpr float GUIDANCE_TARGET_Z_OFFSET = 20.0f;   //comfortably past the crossing margin, inside the room
		//Raised from an initial 0.0005f/0.003f (unverified - tune by feel) after "I couldn't feel
		//anything" - that cap sat at the low end of this codebase's own real force range (compare
		//ObjectsFactory.cpp's button/tile solidForceLimit values, up to 0.006f), so on top of the
		//gating bug above, it may also simply have been too subtle. Raised to sit at the strong
		//end of that same range instead, since this force's whole job is to be noticed - still
		//unverified/tune-by-feel, same as every other haptic constant in this codebase. Eased back
		//down by 0.75x after later hardware-test feedback ("make the assistive drag to the program
		//slot a little gentler (maybe 0.75x?)") - noticed, but not quite this strong.
		constexpr float GUIDANCE_SPRING_CONSTANT = 0.0015f * 0.75f;
		constexpr float GUIDANCE_MAX_FORCE = 0.006f * 0.75f;

		Vector3 target(0.0f, 0.0f, wallCenterZ + GUIDANCE_TARGET_Z_OFFSET);
		Vector3 force(
			(target.x - currentLoc.position.x) * GUIDANCE_SPRING_CONSTANT,
			(target.y - currentLoc.position.y) * GUIDANCE_SPRING_CONSTANT,
			(target.z - currentLoc.position.z) * GUIDANCE_SPRING_CONSTANT
		);
		if (force.length() > GUIDANCE_MAX_FORCE) {
			force = force.normalized() * GUIDANCE_MAX_FORCE;
		}
		return force;
	}

	return Vector3(0, 0, 0);
}
