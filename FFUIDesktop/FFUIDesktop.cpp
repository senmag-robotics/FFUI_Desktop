#include "FFUIDesktop.h"
#include "ObjectsFactory.h"
#include "WindowScanner.h"
#include "MenuSystem.h"
#include "VoiceSettingsManager.h"
#include "StartMenuFlow.h"
#include "TutorialFlow.h"
#include "NarrateFlow.h"
#include "AppConfig.h"
#include <thread>
#include <iostream>
#include <chrono>
#include <cmath>
#include <vector>
#include <algorithm>   //std::min - used by initiatePeriodicScanner()'s manual-scan poll wait,
                        //added explicitly rather than assumed transitively available, same
                        //precedent as CursorStateHaptics.cpp's own <oleauto.h>/<algorithm> includes


// Define static member
SnapAnchor FFUIDesktop::currentSnapAnchor;

// Helper function to send input cleanly
void SendMouseInput(DWORD flags, DWORD data = 0) {
	INPUT input = { 0 };
	input.type = INPUT_MOUSE;
	input.mi.dwFlags = flags;
	input.mi.mouseData = data; // Used for wheel scrolling or XBUTTON id
	SendInput(1, &input, sizeof(INPUT));
}

//Sends a plain-text string as synthetic keyboard input, targeting whatever window currently has
//real OS keyboard focus - "treat it as a keyboard, so if a text field is open, the text will
//get entered", per the request. KEYEVENTF_UNICODE bypasses virtual-key mapping entirely: each
//wchar_t is delivered to the focused control as its own WM_CHAR-equivalent code point rather
//than a named key, so this can't accidentally trigger a keyboard shortcut (Ctrl+S, Alt+F4, a
//single letter bound to some app-specific hotkey, etc.) the way sending real virtual-key codes
//could, and it works for any Unicode character the transcription might produce, not just
//whatever a physical keyboard layout could type directly. A character outside the Basic
//Multilingual Plane (rare from speech transcription, but not impossible - an emoji, say) is
//already stored as a UTF-16 surrogate pair inside the std::wstring; sending its two code units
//as two separate KEYEVENTF_UNICODE events, one right after the other, is exactly how a real
//keyboard driver would deliver one too, and Windows' own input stack already reassembles a
//single WM_CHAR surrogate pair from two consecutive events like that - no special-casing needed
//here.
void SendTypedText(const std::wstring& text) {
	if (text.empty()) return;

	std::vector<INPUT> inputs;
	inputs.reserve(text.size() * 2);

	for (wchar_t ch : text) {
		INPUT down = { 0 };
		down.type = INPUT_KEYBOARD;
		down.ki.wVk = 0;
		down.ki.wScan = static_cast<WORD>(ch);
		down.ki.dwFlags = KEYEVENTF_UNICODE;
		inputs.push_back(down);

		INPUT up = down;
		up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
		inputs.push_back(up);
	}

	SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
}

void FFUIDesktop::initDesktop(FFUIDesktop_Config config) {
	//Initialize speaker 


	if (FAILED(CoInitialize(NULL))) {
		// Handle COM init failure if needed
	}
	CoCreateInstance(CLSID_SpVoice, NULL, CLSCTX_ALL, IID_ISpVoice, (void**)&pSapiVoice);

	//The assistant's own, independent voice - see pAssistantVoice's declaration in
	//FFUIDesktop.h for why this exists as a second ISpVoice rather than reusing pSapiVoice.
	CoCreateInstance(CLSID_SpVoice, NULL, CLSCTX_ALL, IID_ISpVoice, (void**)&pAssistantVoice);

	//The Narrate feature's own, independent voice - see pNarrateVoice's declaration in
	//FFUIDesktop.h for why this exists as a third ISpVoice rather than reusing either of the
	//other two.
	CoCreateInstance(CLSID_SpVoice, NULL, CLSCTX_ALL, IID_ISpVoice, (void**)&pNarrateVoice);

	//Fix for first-hardware-test feedback ("can we adjust so the FFUI narrator doesn't wait for
	//the tutorial narrator? - can they both speak over each other at the same time?"): being two
	//separate ISpVoice COM objects is NOT, on its own, enough to guarantee concurrent playback -
	//by default (SPVPRI_NORMAL) SAPI serializes/queues Speak requests that share the same default
	//audio output object, which both of these do unless told otherwise, so one voice's speech was
	//queuing up behind the other's instead of mixing. SPVPRI_OVER is the SAPI priority level
	//documented specifically to mix an utterance over whatever else the audio object is currently
	//rendering, rather than queuing behind it - applied to both voices so either can always speak
	//immediately over the other, matching the "genuinely independent" behavior this codebase's own
	//comments already assumed (see pAssistantVoice's declaration and assistantHasFloor()'s own
	//comment) but which apparently needed this explicit call to actually hold true on real hardware.
	if (pSapiVoice) pSapiVoice->SetPriority(SPVPRI_OVER);
	if (pAssistantVoice) pAssistantVoice->SetPriority(SPVPRI_OVER);
	if (pNarrateVoice) pNarrateVoice->SetPriority(SPVPRI_OVER);

	//Best-effort: if more than one voice is installed on this machine, give the assistant a
	//visibly different one than pSapiVoice's (a freshly-created ISpVoice already uses SAPI's
	//own default voice with no explicit SetVoice() call, so skipping enumeration index 0 and
	//applying index 1 to pAssistantVoice is enough to diverge from it in the common case) - so
	//the two narrators are distinguishable by ear alone, not just by timing/content. Mirrors
	//the SPCAT_RECOGNIZERS token-enumeration pattern already established in
	//SpeechRecognizer::isEnglishRecognizerAvailable(), just against SPCAT_VOICES. If only one
	//voice is installed, or enumeration fails for any reason, both narrators simply keep the
	//same voice - they still stay on two fully independent audio streams either way, so
	//volume-ducking and non-clipping both still work regardless of this succeeding.
	if (pAssistantVoice) {
		ISpObjectTokenCategory* pVoiceCategory = nullptr;
		if (SUCCEEDED(CoCreateInstance(CLSID_SpObjectTokenCategory, NULL, CLSCTX_ALL,
			IID_ISpObjectTokenCategory, (void**)&pVoiceCategory)) && pVoiceCategory) {

			if (SUCCEEDED(pVoiceCategory->SetId(SPCAT_VOICES, FALSE))) {
				IEnumSpObjectTokens* pVoiceEnum = nullptr;
				if (SUCCEEDED(pVoiceCategory->EnumTokens(NULL, NULL, &pVoiceEnum)) && pVoiceEnum) {
					ULONG voiceCount = 0;
					if (SUCCEEDED(pVoiceEnum->GetCount(&voiceCount)) && voiceCount > 1) {
						ISpObjectToken* pSkipped = nullptr;
						ISpObjectToken* pSecondVoice = nullptr;
						if (SUCCEEDED(pVoiceEnum->Next(1, &pSkipped, NULL)) && pSkipped &&
							SUCCEEDED(pVoiceEnum->Next(1, &pSecondVoice, NULL)) && pSecondVoice) {
							pAssistantVoice->SetVoice(pSecondVoice);
						}
						if (pSkipped) pSkipped->Release();
						if (pSecondVoice) pSecondVoice->Release();
					}
					pVoiceEnum->Release();
				}
			}
			pVoiceCategory->Release();
		}
	}

	//Loads any previously-saved FFUI/AI narrator voice choice + speech rate from disk and
	//applies them to pSapiVoice/pAssistantVoice - must run after both exist (just above), and
	//after the best-effort "give the assistant a different voice than FFUI's" block above, so a
	//saved explicit choice always wins over that default. A first-ever run (no saved file yet)
	//leaves both voices exactly as they already were - see its own comment.
	VoiceSettingsManager::getInstance().loadFromDiskAndApply();

	//Loads any previously-saved quickslot-count setting from disk and applies it to
	//WindowManager::numOfActiveWindows (clamping WindowManager::currentActiveSlotIndex if
	//needed) - a first-ever run (no saved file yet) leaves the count at its default (2, matching
	//today's pre-feature behavior). Order relative to the voice-settings load above doesn't
	//matter - the two are unrelated domains.
	QuickSlotsSettingsManager::getInstance().loadFromDiskAndApply();

	//First-ever launch: auto-start the tutorial - "this should automatically start the first time
	//the program is launched", per the request. Checked via the consolidated AppConfig store (see
	//TUTORIAL_SPEC.md's Persistence section) rather than a dedicated file of its own. Placed here
	//(after both voices and the settings above exist, so tutorial narration has a real FFUI rate
	//to borrow - see TutorialFlow::speakStageScript()) and before voiceAssistant.start() below,
	//since Stage 0's own narration doesn't depend on the assistant's worker thread being up yet.
	if (!AppConfig::getInstance().getBool(L"tutorial.hasCompletedTutorialOnce", false)) {
		TutorialFlow::getInstance().begin(true);
	}

	//Started here (after pSapiVoice/pAssistantVoice exist, since the assistant speaks replies
	//through pAssistantVoice) rather than at construction, mirroring scannerThread's own start
	//below.
	voiceAssistant.start();

	desktopConfig = config;

	cusrsorScale.x = desktopConfig.screenSize.x / DEVICE_WORKSPACE_X;
	cusrsorScale.y = desktopConfig.screenSize.y / DEVICE_WORKSPACE_Y;

	FFUIDesktop_Layer newLayer;
	layers.push_back(std::move(newLayer));

	cursorPos = { 0,0 };

	// a lambda to capture 'this' and 'config', and accept the 'st' (stop_token) from jthread
	scannerThread = std::jthread([this, config](std::stop_token st) {
		initiatePeriodicScanner(st, config);
	});


}



//We create an updated list of objects by scanning then we lock the mutex only for the moment of switching 
//the old list with the new list. 
 void FFUIDesktop::initiatePeriodicScanner(std::stop_token stoken, FFUIDesktop_Config config) {
	//Target period for a full desktop scan. UI Automation walks aren't latency-critical
	//the way the haptic loop is, so a plain sleep_until at the bottom of the loop (no
	//busy-wait) is precise enough to keep this off the CPU between scans.
	const std::chrono::duration<double> uiFrameDuration(1.0 / config.targetUIFramerate);

	while (!stoken.stop_requested()) {
		auto frameStart = std::chrono::steady_clock::now();

		//New this round ("the whole program could be paused while the stylus is in the rest
		//position") - this thread's own half of the pause: updateFrame() (the haptic thread)
		//already skips essentially all of its own per-frame work while stylusAtRest is true (see
		//that flag's own comment, FFUIDesktop.h) and sets/clears it based on the device's own
		//raw position; this just mirrors that same flag here rather than doing any rest-detection
		//of its own. Sleeps in short increments - not the loop's own much longer ambient
		//uiFrameDuration wait, and not a tight spin either - purely so this notices stylusAtRest
		//clearing again promptly once real movement resumes.
		if (stylusAtRest.load(std::memory_order_relaxed)) {
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			continue;
		}


		//static auto lastTime = std::chrono::high_resolution_clock::now();
		//static int frameCount = 0;

		//frameCount++;
		//auto currentTime = std::chrono::high_resolution_clock::now();
		//std::chrono::duration<double> elapsed = currentTime - lastTime;

		//if (elapsed.count() >= 1.0) {
		//	double currentHz = frameCount / elapsed.count();

		////	printf("Scanner Thread Frequency: %.2f Hz\n", currentHz);

		//	frameCount = 0;
		//	lastTime = currentTime;
		//}




		//Duck (not pause) FFUI's own narrator while the assistant is likely to be speaking - CLI
		//setup is running, a hold-to-talk exchange is in flight through its spoken reply, or the
		//two-detent confirm/cancel prompt is up (see VoiceAssistant::assistantHasFloor()). This
		//used to skip scanning entirely instead, so FFUI's window-focus narration could never
		//fire at all and clip the assistant's - that's no longer needed now that the assistant
		//speaks through its own separate pAssistantVoice (see FFUIDesktop::initDesktop()): each
		//ISpVoice's SPF_PURGEBEFORESPEAK only purges its own queue, so the two narrators simply
		//can't clip each other any more, and scanning/narration can safely run the whole time -
		//just quieter, so the assistant clearly stays the foreground voice.
		//
		//Was originally meant to fully mute (not just duck) pSapiVoice during Stage 1/Stage 2 of
		//the tutorial on its own - second-round hardware-test feedback: "can we make sure the FFUI
		//narrator is disabled during steps 1 & 2?" Reported as still audible on hardware even
		//after fixing the one confirmed cross-thread race in this block (currentStage() is read
		//from this scanner thread; TutorialFlow::stage is now std::atomic to make that safe - see
		//that class's own comment). Rather than keep chasing SAPI's own volume-application timing
		//relative to whichever Speak() call is already in flight, the real, PRIMARY enforcement of
		//Stage 1/2 muting now lives at the source: every pSapiVoice->Speak() call site goes
		//through SpeakFfuiNarration() (see FFUIDesktop.h), which checks
		//TutorialFlow::shouldMuteFfuiNarrator() and simply never hands a muted utterance to SAPI
		//at all. This volume-ducking block stays only as a harmless second layer, and as the sole
		//mechanism for the still-working, never-reported-broken assistant-floor 50% duck below.
		//Edge-triggered (only calls SetVolume on an actual change), not every iteration, to avoid
		//spamming a COM call ~30x/second for no reason.
		static int ffuiNarratorVolume = 100;
		int targetFfuiNarratorVolume = 100;
		TutorialFlow::Stage tutorialStage = TutorialFlow::getInstance().currentStage();
		if (tutorialStage == TutorialFlow::Stage::Stage1_Stylus || tutorialStage == TutorialFlow::Stage::Stage2_Haptics) {
			targetFfuiNarratorVolume = 0;
		}
		else if (voiceAssistant.assistantHasFloor()) {
			targetFfuiNarratorVolume = 50;
		}
		if (targetFfuiNarratorVolume != ffuiNarratorVolume) {
			if (pSapiVoice) {
				pSapiVoice->SetVolume(targetFfuiNarratorVolume);
			}
			ffuiNarratorVolume = targetFfuiNarratorVolume;
		}

		std::vector<std::unique_ptr<FFUIObject>> newObjects;

		addBoundaryPlanes(newObjects);

		//The 2 static FFUI Settings tiles - recreated every cycle (synthetic, not scanned, so
		//there's nothing to preserve identity-wise), confined to the left half of the shared
		//front zone. Deliberately NOT recreated inside the isUserGrabbingWindow branch below -
		//like every other non-boundary/non-placeholder object, they're absent for the duration
		//of a window drag, same as buttons/list items already are.
		std::vector<GridTileObject*> tempSettingsTiles;
		addFrontZoneSettingsTiles(newObjects, tempSettingsTiles);

		static bool hasGeneratedPlaceholders = false;
		if (WindowManager::getInstance().isUserGrabbingWindow.load()) {
			// Only generate and swap the placeholders ONCE per grab because it will crash if it swaps a second time
			// (since activeWindows list will have dead pointers)
			if (!hasGeneratedPlaceholders) {
				std::vector<std::unique_ptr<FFUIObject>> newObjects;
				addBoundaryPlanes(newObjects);

				std::scoped_lock doubleLock(objectsListMutex, WindowManager::getInstance().windowMutex);

				// Read the ActiveWindows data while it is still alive and safe
				for (WindowWallObject* activeWindow : WindowManager::getInstance().ActiveWindows) {
					std::unique_ptr<FFUIObject> windowPlaceholder = ObjectFactory::createGravityWellAtWindowPosition(activeWindow);
					//printf("z pos: %f\n", windowPlaceholder.get()->getMeta().globalPosition.z);

					newObjects.emplace_back(std::move(windowPlaceholder));
				}

				for (WindowWallObject* archivedWindow : WindowManager::getInstance().ArchivedWindows) {
					std::unique_ptr<FFUIObject> windowPlaceholder = ObjectFactory::createGravityWellAtWindowPosition(archivedWindow);
					//printf("y pos: %f\n", windowPlaceholder.get()->getMeta().globalPosition.y);

					newObjects.emplace_back(std::move(windowPlaceholder));
				}

				//Snapshot the real, just-scanned Z positions of the active slots and the archived
				//list into grabZoneOptions - this frame is the one guaranteed-safe moment to read
				//them (right before ActiveWindows/ArchivedWindows are cleared below), and it's
				//what classifyGrabZone() uses for the rest of this grab to tell which option
				//(Slot 1 / Slot 2 / cancel) the stylus is currently over, with no gaps between
				//zones - see WindowManager::buildGrabZoneOptions()'s own comment.
				WindowManager::getInstance().buildGrabZoneOptions();

				if (!layers.empty()) {

					std::swap(layers[0].objects, newObjects);

					WindowManager::getInstance().ActiveWindows.clear();
					WindowManager::getInstance().ArchivedWindows.clear();
				}

				hasGeneratedPlaceholders = true;
			}

			// Throttle the CPU while the user is dragging the window around
			std::this_thread::sleep_for(std::chrono::milliseconds(16));
			continue;
		}
		else {
			// we are not grabbing, reset the gate so it's ready for the next time grabbing mode is activated
			// (This works across frames because this variable is static)
			hasGeneratedPlaceholders = false;
		}


		//Temporary active and archived windows lists to minimize critical section
		//In the critical section we fill the content of the real ones with the temp ones.
		std::vector<WindowWallObject*> tempActiveWindows;
		std::vector<WindowWallObject*> tempArchivedWindows;

		WindowScanner scanner;
		if (scanner.initialize()) {
			std::vector<UIElementType> typesToScan = { UIElementType::Button,
				UIElementType::ListItem,
				UIElementType::MenuItem,
				};
			std::vector<ScannedUIElement> allscannedElements;


			std::vector<ScannedUIElement> focusedWindowElements = scanner.scanFocusedWindow(typesToScan);
			std::vector<ScannedUIElement> windows = scanner.fetchAllOpenWindows();

			//std::vector<ScannedUIElement> windows = scanner.fetchAllOpenWindows();

			//for (ScannedUIElement elem : focusedWindowElements) {
			//	//upgrading console printing capabilityes
			//	if (elem.type == UIElementType::Window) {
			//		_setmode(_fileno(stdout), _O_U16TEXT);
			//		std::wcout << elem.name << std::endl;

			//	}
			//
			//}

			std::vector<ScannedUIElement> taskbarElements = scanner.scanTaskBar(typesToScan);


			//Combiniing the scanned elements
			allscannedElements
				.reserve(focusedWindowElements.size() + taskbarElements.size() + windows.size());
			allscannedElements.insert(allscannedElements.end(),
				focusedWindowElements.begin(),
				focusedWindowElements.end());

			allscannedElements.insert(allscannedElements.end(),
				taskbarElements.begin(),
				taskbarElements.end());

			allscannedElements.insert(allscannedElements.end(),
					windows.begin(),
					windows.end());


			//We clean up closed windows

			std::vector<HWND> currentOpenHwnds;
			currentOpenHwnds.reserve(windows.size());
			for (const auto& win : windows) {
				currentOpenHwnds.push_back(win.hwnd);
			}

			//See moveNewWindowsToPrimaryMonitor()'s own comment - relocates any window opened on
			//a non-primary monitor since the last scan, so it stays reachable by the haptic
			//device. Deliberately called with this scan's full open-window list before
			//removeClosedWindows() below, though the two don't otherwise interact.
			moveNewWindowsToPrimaryMonitor(currentOpenHwnds);

			WindowManager::getInstance().removeClosedWindows(currentOpenHwnds);


		
			//Now we create the objects of

			std::vector<std::unique_ptr<FFUIObject>> scannedObjects =
				ObjectFactory::createObjectsFromUIElements(allscannedElements, config,
					tempActiveWindows,
					tempArchivedWindows);

			for (auto& obj : scannedObjects) {
				newObjects.emplace_back(std::move(obj));
			}
		}


		//Critical Section minimised: 
		//this locks both mutexes at the same time
		std::scoped_lock doubleLock(objectsListMutex, WindowManager::getInstance().windowMutex);


		if (!layers.empty()) {

			//Find out which window should be focused in the updated list. A scroll-wheel click
			//(WindowManager::cycleActiveSlot()) stashes an explicit request here the moment it
			//happens - preferred over the old "whichever old ActiveWindows member already had
			//isFocused()==true" scan, since the just-selected slot's WindowWallObject doesn't
			//exist in the OLD ActiveWindows at all yet (it's only constructed by THIS scan cycle,
			//just below) - without this, the old scan would find nothing focused and briefly
			//leave the new room unfocused for a cycle. Consumed (reset) here so only the very
			//next scan after a click honors it; every scan after that falls back to the normal
			//"carry the same focus forward" behavior.
			HWND lastFocusedWindowHandle = NULL;
			bool haveFocusTarget = false;
			if (WindowManager::getInstance().hasPendingExplicitFocus) {
				lastFocusedWindowHandle = WindowManager::getInstance().pendingExplicitFocusHandle;
				haveFocusTarget = true;
				WindowManager::getInstance().hasPendingExplicitFocus = false;
			}
			else {
				for (auto* oldWindow : WindowManager::getInstance().ActiveWindows) {
					if (oldWindow->isFocused()) {
						lastFocusedWindowHandle = oldWindow->getHandle();
						haveFocusTarget = true;
						break;
					}
				}
			}

			// Maintain the same focused window in the updated list
			WindowManager::getInstance().ActiveWindows = tempActiveWindows;

			for (auto* window : WindowManager::getInstance().ActiveWindows) {
				if (haveFocusTarget && lastFocusedWindowHandle != NULL && window->getHandle() == lastFocusedWindowHandle) {
					window->setFocused(true);
				}
				else {
					window->setFocused(false);
				}
			}
			WindowManager::getInstance().ArchivedWindows = tempArchivedWindows;
			WindowManager::getInstance().SettingsTiles = tempSettingsTiles;

			std::swap(layers[0].objects, newObjects);


		}

		//Pace this loop to config.targetUIFramerate. If a scan already took longer than the
		//budget (a slow UI Automation walk on a busy window), frameEnd is already in the past
		//and we skip straight to the next iteration rather than trying to catch up with a burst
		//of extra scans.
		auto frameEnd = frameStart + std::chrono::duration_cast<std::chrono::steady_clock::duration>(uiFrameDuration);
		auto now = std::chrono::steady_clock::now();
		if (frameEnd > now) {
			std::this_thread::sleep_until(frameEnd);
		}
	}
}

//Multi-monitor accessibility fix - see the header comment for the "why". Only ever acts on a
//window the very first time this function sees its HWND (tracked in knownWindowHandles); a
//window the user deliberately drags to a second monitor after it opens is left alone on every
//later scan, since it's already in knownWindowHandles by then.
//
//One real edge case worth flagging rather than solving here: on FFUIDesktop's very first scan
//after launch, knownWindowHandles starts completely empty, so every window already open at
//that moment gets treated as "just opened" and is pulled onto the primary monitor once, even
//if the user had deliberately placed it on a second monitor long before FFUI even started. This
//is arguably still correct for this app's purpose (nothing on a non-primary monitor was
//reachable by the haptic device before this ran either), but it's a real behavior change worth
//confirming feels right in practice, not an obviously-safe no-op.
void FFUIDesktop::moveNewWindowsToPrimaryMonitor(const std::vector<HWND>& currentOpenHwnds) {
	POINT originPoint{ 0, 0 };
	HMONITOR primaryMonitor = MonitorFromPoint(originPoint, MONITOR_DEFAULTTOPRIMARY);

	MONITORINFO primaryInfo{};
	primaryInfo.cbSize = sizeof(primaryInfo);
	if (!GetMonitorInfoW(primaryMonitor, &primaryInfo)) {
		return;  //shouldn't happen - the primary monitor always resolves to something
	}

	LONG primaryWidth = primaryInfo.rcWork.right - primaryInfo.rcWork.left;
	LONG primaryHeight = primaryInfo.rcWork.bottom - primaryInfo.rcWork.top;

	std::unordered_set<HWND> updatedKnown;
	updatedKnown.reserve(currentOpenHwnds.size());

	for (HWND hwnd : currentOpenHwnds) {
		if (knownWindowHandles.find(hwnd) != knownWindowHandles.end()) {
			updatedKnown.insert(hwnd);  //already handled on a previous scan - leave it alone
			continue;
		}

		//First time seeing this window. A stale/already-closed handle just isn't added to
		//updatedKnown below, so it's harmlessly reconsidered (and likely dropped, since it
		//won't appear in currentOpenHwnds again) on the next scan rather than causing a crash.
		if (!IsWindow(hwnd)) continue;

		WINDOWPLACEMENT placement{};
		placement.length = sizeof(placement);
		if (!GetWindowPlacement(hwnd, &placement)) continue;

		//A minimized window's rcNormalPosition is still its meaningful restored position, but
		//deliberately not acted on yet - not marked known either, so it gets a real evaluation
		//once it's restored (or is still minimized next scan, in which case this just repeats
		//harmlessly) rather than being silently skipped forever after one glance while hidden.
		if (placement.showCmd == SW_SHOWMINIMIZED) continue;

		HMONITOR windowMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
		if (windowMonitor != primaryMonitor) {
			LONG width = placement.rcNormalPosition.right - placement.rcNormalPosition.left;
			LONG height = placement.rcNormalPosition.bottom - placement.rcNormalPosition.top;

			//Translate the window's position proportionally from its current monitor's work
			//area to the primary monitor's, so (e.g.) a window sitting near the top-left of its
			//monitor ends up near the top-left of the primary monitor too, rather than every
			//relocated window landing in the same spot regardless of where it actually was.
			MONITORINFO oldMonitorInfo{};
			oldMonitorInfo.cbSize = sizeof(oldMonitorInfo);
			RECT oldWorkArea = primaryInfo.rcWork;  //fallback if the old monitor's info can't be read
			if (GetMonitorInfoW(windowMonitor, &oldMonitorInfo)) {
				oldWorkArea = oldMonitorInfo.rcWork;
			}

			LONG oldMonitorWidth = oldWorkArea.right - oldWorkArea.left;
			LONG oldMonitorHeight = oldWorkArea.bottom - oldWorkArea.top;
			float relativeX = oldMonitorWidth > 0
				? (float)(placement.rcNormalPosition.left - oldWorkArea.left) / oldMonitorWidth
				: 0.0f;
			float relativeY = oldMonitorHeight > 0
				? (float)(placement.rcNormalPosition.top - oldWorkArea.top) / oldMonitorHeight
				: 0.0f;

			LONG newLeft = primaryInfo.rcWork.left + (LONG)(relativeX * primaryWidth);
			LONG newTop = primaryInfo.rcWork.top + (LONG)(relativeY * primaryHeight);

			//Clamp size and position so the window ends up fully inside the primary monitor's
			//work area regardless of how the proportional translation above worked out - most
			//importantly this also covers a window larger than the primary monitor's work area.
			if (width > primaryWidth) width = primaryWidth;
			if (height > primaryHeight) height = primaryHeight;
			if (newLeft + width > primaryInfo.rcWork.right) newLeft = primaryInfo.rcWork.right - width;
			if (newLeft < primaryInfo.rcWork.left) newLeft = primaryInfo.rcWork.left;
			if (newTop + height > primaryInfo.rcWork.bottom) newTop = primaryInfo.rcWork.bottom - height;
			if (newTop < primaryInfo.rcWork.top) newTop = primaryInfo.rcWork.top;

			placement.rcNormalPosition.left = newLeft;
			placement.rcNormalPosition.top = newTop;
			placement.rcNormalPosition.right = newLeft + width;
			placement.rcNormalPosition.bottom = newTop + height;

			//SetWindowPlacement, not a raw SetWindowPos - the documented correct way to move a
			//window regardless of whether it's currently maximized or normal (a raw SetWindowPos
			//on a maximized window would resize/distort it, since a maximized window's on-screen
			//rect isn't its "real" size). showCmd is left as whatever GetWindowPlacement already
			//reported, so a maximized window stays maximized - now on the primary monitor, since
			//Windows maximizes onto whichever monitor rcNormalPosition's center falls within.
			SetWindowPlacement(hwnd, &placement);
		}

		updatedKnown.insert(hwnd);
	}

	knownWindowHandles = std::move(updatedKnown);
}

void FFUIDesktop::addBoundaryPlanes(std::vector<std::unique_ptr<FFUIObject>>& targetList) {
	HapticSolidProperties props{};
	props.stiffness = 0.001f;
	props.solidForceLimit = 0.005f;

	FFUIObject_Meta meta{};
	meta.scale = Vector3(2000, 0, 2000);
	meta.hapticSolidProperties = props;
	meta.uiType = UIElementType::ScreenBoundary;

	// Front
	meta.globalPosition = Vector3(0, 0, 130);
	meta.orientation = Quaternion().setFromEuler(0, 0, 90);
	meta.customName = "Workspace Front Boundary";
	targetList.emplace_back(std::make_unique<SolidPlane>(meta));

	//// Back
	//meta.globalPosition = Vector3(0, 0, 240);
	//meta.orientation = Quaternion().setFromEuler(0, 0, -90);
	//meta.customName = "Workspace Front Boundary";
	//targetList.emplace_back(std::make_unique<SolidPlane>(meta));

	// Bottom
	meta.globalPosition = Vector3(0, (-DEVICE_WORKSPACE_Y) / 2 + DEVICE_WORKSPACE_OFFSETY, 0);
	meta.orientation = Quaternion().setFromEuler(1, 0, 0);
	meta.customName = "Workspace Lower Boundary";
	targetList.emplace_back(std::make_unique<SolidPlane>(meta));


	// Right
	meta.globalPosition = Vector3(DEVICE_WORKSPACE_X / 2 + DEVICE_WORKSPACE_OFFSETX, 0, 0);
	meta.orientation = Quaternion().setFromEuler(90, 0, 0);
	meta.customName = "Workspace Right Boundary";
	targetList.emplace_back(std::make_unique<SolidPlane>(meta));


	// Top
	meta.globalPosition = Vector3(0, DEVICE_WORKSPACE_Y / 2 + DEVICE_WORKSPACE_OFFSETY, 0);
	meta.orientation = Quaternion().setFromEuler(180, 0, 0);
	meta.customName = "Workspace Upper Boundary";
	targetList.emplace_back(std::make_unique<SolidPlane>(meta));


	// Left
	meta.globalPosition = Vector3(-DEVICE_WORKSPACE_X / 2 + DEVICE_WORKSPACE_OFFSETX, 0, 0);
	meta.orientation = Quaternion().setFromEuler(270, 0, 0);
	meta.customName = "Workspace Left Boundary";
	targetList.emplace_back(std::make_unique<SolidPlane>(meta));
}

void FFUIDesktop::addFrontZoneSettingsTiles(std::vector<std::unique_ptr<FFUIObject>>& targetList, std::vector<GridTileObject*>& outTiles) {
	// Same shared front-zone Z-band the Program Tray uses - computed once per scan cycle by
	// ObjectFactory::createObjectsFromUIElements() and stashed on WindowManager, so this stays
	// correct even if numOfActiveWindows/roomDepth ever change. If the very first scan hasn't
	// run yet (frontZoneZHalfThickness still its zero-initialized default), fall back to a
	// small placeholder band rather than creating degenerate zero-thickness tiles - they'll
	// snap to the real band on the very next cycle regardless.
	float zCenter = WindowManager::getInstance().frontZoneZCenter;
	float zHalfThickness = WindowManager::getInstance().frontZoneZHalfThickness;
	if (zHalfThickness < 1.0f) zHalfThickness = 20.0f;

	// Order and labels per the request: "<start programs> <Settings: Program quickslots>
	// <Settings: FFUI Narrator> <Settings: AI Narrator> <FFUI Tutorial>". Tile index is what
	// openSettingsTileMenu() dispatches on below - keep the two in sync if this order ever
	// changes again. "FFUI Tutorial" appended at the end (index 4) rather than reordering the
	// existing four, so this addition doesn't shift indices anyone's already used to.
	static const wchar_t* kSettingsTileLabels[5] = { L"Start programs", L"Settings: Program quickslots", L"Settings: FFUI Narrator", L"Settings: AI Narrator", L"FFUI Tutorial" };
	const int settingsTileCount = 5;

	HapticSolidProperties props{};
	props.stiffness = 0.0008f;
	props.solidForceLimit = 0.0025f;

	for (int i = 0; i < settingsTileCount; i++) {
		ObjectFactory::GridCell cell = ObjectFactory::computeGridCell(
			i, settingsTileCount, -DEVICE_WORKSPACE_X / 2.0f, 0.0f, zCenter);

		FFUIObject_Meta meta{};
		meta.hapticSolidProperties = props;
		meta.uiType = UIElementType::GridTile;
		meta.orientation = Quaternion().setFromEuler(1, 0, 0);
		meta.globalPosition = cell.position;
		meta.scale = Vector3(cell.cellWidth * 0.85f, cell.cellHeight * 0.85f, zHalfThickness * 2.0f * 0.85f);
		meta.customName = "FFUI Settings Tile";

		auto tile = std::make_unique<GridTileObject>(meta, kSettingsTileLabels[i], i);
		outTiles.push_back(tile.get());
		targetList.emplace_back(std::move(tile));
	}
}
//calculates the force to the closest object (that is not a boundary) to the cursor 

Vector3 FFUIDesktop::calculateForceToClosestObject(Location deviceLoc, bool button3Clicked) {
	Vector3 snappingForce(0, 0, 0);
	std::lock_guard<std::mutex> lock(objectsListMutex);


	if (layers.empty() || layers[0].objects.empty()) return snappingForce;

	FFUIObject* closestObject = nullptr;

	float minDistance = (std::numeric_limits<float>::max)();
	
	for (const auto& object : layers[0].objects) {

		WindowWallObject* windowPointer = dynamic_cast<WindowWallObject*>(object.get());
		bool isActiveWindow = false; 
		if(windowPointer != nullptr)
		isActiveWindow = object->getMeta().uiType == UIElementType::Window && windowPointer->isArchived() == false;

		bool isScreenBoundary = object->getMeta().uiType == UIElementType::ScreenBoundary;
		//We skip if this object is a boundary or an active window, skip;
		//as we don't want to attract towards them ever.
		if (isScreenBoundary || isActiveWindow)
			continue;



		Vector3 objectPos = object->getMeta().globalPosition;
		float dx = objectPos.x - cursorPos.x;
		float dy = objectPos.y - cursorPos.y;
		float distance2DToThisButton = std::sqrt(dx * dx + dy * dy);

		float distanceToThis = (objectPos - deviceLoc.position).length();
		
		if (distanceToThis < minDistance) {
			minDistance = distanceToThis;
			closestObject = object.get();
		}

		object->setSnapped(false);
	}
	if (closestObject != nullptr) {

		//To enable the feature of snapping inplace of an object when side button is clicked.
		//Move this line inside the else of the next if, and remove "buttonClicked = false" 
		//(Doesn't work currently so don't change anything)

		snappingForce = closestObject->calculateSnappingForceToThis(deviceLoc);


		float halfX = closestObject->getMeta().scale.x * 0.5f;
		float halfY = closestObject->getMeta().scale.y * 0.5f;	
		float halfZ = closestObject->getMeta().scale.z * 0.5f;

		if (std::abs(minDistance) < halfX  && std::abs(minDistance )< halfY) {


			// Attempt to cast the generic object into a WindowWall object
			WindowWallObject* wallPointer = dynamic_cast<WindowWallObject*>(closestObject);

			//if (closestObject->getMeta().uiType == UIElementType::Window ) {

			//	printf("inside \n");
			//	std::cout << closestObject->getMeta().customName << std::endl;
			//	printf("done\n");
			//
			//}

			//Remove this line to enable feature (Doesn't work currently)
			button3Clicked = false;
			if (button3Clicked) {

				stylusSnapped = !stylusSnapped;

				//std::cout << "is: " << stylusSnapped << std::endl;

				closestObject->setSnapped(stylusSnapped);

				currentSnapAnchor.isTracking = stylusSnapped;
				currentSnapAnchor.objectWindowsName = closestObject->getMeta().customName;
				currentSnapAnchor.originalPosition = closestObject->getMeta().globalPosition;

			}

			//std::cout << "Was: " << closestObject->getMeta().snappedToThis << std::endl;
			//std::cout << "Is: " << stylusSnapped << std::endl;
			

		}
		else {
			//std::wcout << "Mystery object: " << closestObject->getUIMeta(). << std::endl;


		}
	}

	return snappingForce;
}



void FFUIDesktop::updateFrame() {



	deviceManager.update();
	for (int x = 0; x < deviceManager.devices.size(); x++) {
		if (deviceManager.devices[x].newStatus) {
			if (deviceManager.devices[x].deviceStatus.position[2] > 100) {	//only process 'active' devices




				//static auto lastTime = std::chrono::high_resolution_clock::now();
				//static int frameCount = 0;

				//frameCount++;
				//auto currentTime = std::chrono::high_resolution_clock::now();
				//std::chrono::duration<double> elapsed = currentTime - lastTime;

				//if (elapsed.count() >= 1.0) {
				//	double currentHz = frameCount / elapsed.count();

				//	//printf("Haptics Thread Frequency: %.2f Hz\n", currentHz);

				//	frameCount = 0;
				//	lastTime = currentTime;
				//}

				//Fetching the device 3d position and orientation
				Location deviceLoc;
				deviceLoc.position.x = deviceManager.devices[x].deviceStatus.position[0];
				deviceLoc.position.y = deviceManager.devices[x].deviceStatus.position[1];
				deviceLoc.position.z = deviceManager.devices[x].deviceStatus.position[2];
				deviceLoc.orientation.w = deviceManager.devices[x].deviceStatus.orientation[0];
				deviceLoc.orientation.i = deviceManager.devices[x].deviceStatus.orientation[1];
				deviceLoc.orientation.j = deviceManager.devices[x].deviceStatus.orientation[2];
				deviceLoc.orientation.k = deviceManager.devices[x].deviceStatus.orientation[3];

				//Rest detection: the firmware itself freezes deviceStatus.position while the
				//stylus is genuinely at rest, so an EXACT (bit-for-bit) compare against last
				//frame's position is enough on its own to notice stillness - no movement-noise
				//threshold needed there, per the request. Any actual change immediately clears
				//stylusAtRest and restarts the timer.
				//
				//BUG FIX, this round ("getting quite some lag when in the program slot... briefly
				//freeze every second or so"): position-frozen alone was NOT enough to safely mean
				//"at rest", and this is what was actually causing that freeze. The device's own
				//detents (a button/list item, and especially the new, deliberately tighter
				//per-line Y detent from last round) genuinely hold the physical tip still - under
				//real, actively-rendered force - for well over a second at a time during totally
				//normal browsing (hovering a button, sitting on a text line while reading), which
				//looks IDENTICAL to genuine rest by position alone. That was getting misclassified
				//as "at rest" mid-use, which zeroed the force and skipped this device's entire
				//per-frame body (including button handling) until the position finally shifted -
				//exactly the periodic freeze reported. Now also requires last frame's actually-
				//rendered force to have been negligible (lastAppliedForceMagnitude, updated right
				//where force is finalized/sent below) - stillness caused by an active pull no
				//longer counts as rest, only stillness with nothing pulling on it at all.
				bool positionChangedThisFrame = !haveLastDevicePosition
					|| deviceLoc.position.x != lastDevicePosition.x
					|| deviceLoc.position.y != lastDevicePosition.y
					|| deviceLoc.position.z != lastDevicePosition.z;

				auto nowSteady = std::chrono::steady_clock::now();
				if (positionChangedThisFrame) {
					lastDevicePosition = deviceLoc.position;
					lastDevicePositionChangeTime = nowSteady;
					haveLastDevicePosition = true;
					stylusAtRest.store(false, std::memory_order_relaxed);
				}
				else if (restPauseFeatureEnabled
					&& nowSteady - lastDevicePositionChangeTime >= restDetectionThreshold
					&& lastAppliedForceMagnitude <= restForceEpsilon) {
					stylusAtRest.store(true, std::memory_order_relaxed);
				}

				//Single shared edge-detect flag for the transition INTO rest, read/written by
				//both branches below - deliberately declared once, here, rather than as two
				//separate function-local statics (one per branch), which would silently be two
				//independent variables that never actually see each other's writes.
				static bool wasAtRestLastFrame = false;

				if (stylusAtRest.load(std::memory_order_relaxed)) {
					//Paused: skip essentially all of this device's per-frame work below
					//(narration polling, UI Automation, menu/button handling, force computation -
					//see initiatePeriodicScanner()'s own comment for the background scanner's own,
					//independent half of this) until real movement resumes, which clears
					//stylusAtRest again immediately (see just above) - no separate "wake" signal
					//needed, the very next changed position IS the wake.
					//
					//The one thing still done here, and only once right on the transition into
					//rest (not every frame while resting) rather than every one of up to 500 times
					//a second: send a single zero-force target, so whatever force happened to be
					//in flight the instant the stylus stopped moving doesn't stay latched/applied
					//indefinitely just because this loop has stopped sending fresh ones.
					if (!wasAtRestLastFrame) {
						LibreOne_targets zeroTargets{};
						zeroTargets.targets[0] = 0;
						zeroTargets.targets[1] = 0;
						zeroTargets.targets[2] = 0;
						deviceManager.devices[x].serialComms.sendTargets(zeroTargets);
					}
					wasAtRestLastFrame = true;
					continue;   //next device (or the for loop simply ends) - skips this device's
					            //entire remaining per-frame body below.
				}
				else {
					wasAtRestLastFrame = false;
				}

				/*
				* bit 7 = aux
				bit 6 = scroll up half
				bit 5 = scroll up full
				bit4 = middle
				bit3 = scroll down full
				bit2 = scroll down half
				bit1 = right
				bit0 = left
				*/
				uint8_t currentInput = deviceManager.devices[x].deviceStatus.toolInputs;

				static uint8_t stylusState_previous = 0xFF;

				//AUX / side button: a plain hold still drives "pull force toward nearest
				//object" exactly as before (Held_PullMode below); a quick tap followed
				//immediately by a second press-and-hold instead starts the voice assistant
				//listening (or, if the assistant has never been set up, the two-detent
				//confirmation prompt - AwaitingSetupConfirmation below). Computed here, before
				//the front button block, so that block can consult the resulting state this
				//same frame - see its comment. voiceAssistant.updateAuxButton() does its own
				//edge/timing tracking internally, so it doesn't need stylusState_previous.
				bool auxPressed = ((currentInput >> 7) & 0x1) == 0;
				AuxGestureState auxGestureState = voiceAssistant.updateAuxButton(auxPressed, deviceLoc);

				//Raw AUX press edge (distinct from the AuxGestureState machine above) - only
				//used by TutorialFlow's Stage-0-only "double-press any button to abandon" check
				//below, which cares about a literal press edge on any of the app's three main
				//buttons, not which gesture state AUX ends up resolving into.
				bool auxPressedEdge = auxPressed && (((stylusState_previous >> 7) & 0x1) == 1);

				bool frontButtonPressedEdge = ((currentInput & 0x1) == 0 && (stylusState_previous & 0x1) == 1);
				bool frontButtonReleasedEdge = ((currentInput & 0x1) == 1 && (stylusState_previous & 0x1) == 0);

				//Duplicated (cheaply) rather than hoisting the existing middleButtonPressedEdge
				//declaration further down in the normal-browsing force branch - that one is
				//scoped to a very different purpose (quickslot cycling, gated to only fire in the
				//final `else` branch) and touching its scope risked more than a second, harmless
				//one-line bit test costs here. TutorialFlow's Stage 3b checkpoint and the
				//double-press-abandon check both need this available up here, well before that
				//branch runs.
				bool middleButtonPressedEdgeForTutorial = (((currentInput >> 4) & 0x1) == 0) && (((stylusState_previous >> 4) & 0x1) == 1);

				//Rear button (bit1, "right" per the button-map comment above) press edge - only
				//used by TutorialFlow's Stage 1 button tour today (Narrate moved off this button
				//onto AUX - see the tryConsumeTapNarrateEdge() block below - so the rear button's
				//only real function now is the OS right-click forwarding further down, gated on
				//!mouseSuspended; that block doesn't need a press-edge computed this early, so this
				//one still exists purely for the tutorial). Duplicated the same way
				//middleButtonPressedEdgeForTutorial is, for the same reason.
				bool rearButtonPressedEdgeForTutorial = (((currentInput >> 1) & 0x1) == 0) && (((stylusState_previous >> 1) & 0x1) == 1);

				//Is the stylus currently over the archived "window select" list? While it is (or
				//while a grab/drag started there is still in progress), normal mouse forwarding
				//and cursor movement are suspended - there's nothing useful under the OS cursor
				//while browsing this haptic-only list - and the select button grabs/drops a
				//program into a slot instead of left-clicking. See isStylusInWindowSelectLayer()'s
				//own comment for how "currently over" is resolved during a grab.
				bool inWindowSelectLayer = isStylusInWindowSelectLayer();
				bool isGrabbingWindow = WindowManager::getInstance().isUserGrabbingWindow.load();

				//Polls VoiceAssistant's dictation mailbox (consuming a pending result while
				//AwaitingDictation) and the background search-results mailbox (opening the
				//matches menu via MenuSystem::createMenu() once results are ready) - see
				//StartMenuFlow.h's own comment. Called once per frame, before menuActive is
				//captured below, so a menu it opens THIS frame is already reflected in this
				//frame's own branching, not just next frame's. voiceAssistant is passed in
				//explicitly (rather than StartMenuFlow reaching for a singleton) since
				//VoiceAssistant is a plain FFUIDesktop member, not a Meyer's singleton like
				//WindowManager/MenuSystem.
				StartMenuFlow::getInstance().update(voiceAssistant);

				bool menuActive = MenuSystem::getInstance().isActive();

				//True only for the AwaitingDictation/Searching phases of the Start Menu flow -
				//once a MenuSystem menu is actually open (ShowingResults), menuActive above
				//already takes over all of this for free, same as any other menu. "other FFUI
				//behaviours should be suspended" applies here too - no menu exists yet during
				//these two phases, so without this the front zone/grab/window-select branches
				//below would otherwise still run normally while the user is mid-dictation.
				bool startMenuAwaiting = StartMenuFlow::getInstance().state() == StartMenuFlow::State::AwaitingDictation
					|| StartMenuFlow::getInstance().state() == StartMenuFlow::State::Searching;

				//"plumb the text to speech elsewhere... treat it as a keyboard, so if a text
				//field is open, the text will get entered", per the request - the SAME AUX-hold
				//dictation gesture used for the Start Menu search (Held_PullMode ->
				//DictatingMode, see VoiceAssistant's own comments) now has a second possible
				//destination for its result. StartMenuFlow::update() just above already has
				//first claim on VoiceAssistant's single-slot dictation mailbox whenever it's
				//actually awaiting one (AwaitingDictation/Searching, i.e. startMenuAwaiting
				//above) - tryConsumePendingDictation() returns false here with nothing to do in
				//that case, so there's no risk of stealing a dictation the Start Menu flow was
				//expecting. Gated on isControllingActiveRoom specifically - "only while mouse
				//control is active", per the request - i.e. only once you've popped through into
				//an actual program, the same moment its window has real OS keyboard focus (see
				//FFUIDesktop::updateFrame()'s room-crossing comment) for these keystrokes to
				//land in. !menuActive is redundant in practice (menus only ever open from the
				//front zone, which sits at a different, non-overlapping Z-band than the room -
				//see inFrontZone/wallCenterZ's own comments - so the two states shouldn't
				//coincide) but costs nothing and avoids relying on that non-overlap holding
				//exactly on every future tuning pass. No separate listening/processing cue is
				//needed here - beginDictating()/runDictation() already play the existing
				//dictate cues (AudioCapture::playDictateListeningCue()/playDictateProcessingCue())
				//for every AUX-hold dictation regardless of which destination ultimately
				//consumes the result, and a failed/empty transcription is already handled (and
				//spoken) entirely inside runDictation() before anything ever reaches the
				//mailbox - so std::wstring dictatedText below is only ever non-empty.
				if (!menuActive && !startMenuAwaiting && WindowManager::getInstance().isControllingActiveRoom) {
					std::wstring dictatedText;
					if (voiceAssistant.tryConsumePendingDictation(dictatedText)) {
						SendTypedText(dictatedText);
					}
				}

				//Whether the stylus is anywhere within the shared front zone's Z-band at all -
				//deliberately NOT tied to any specific tile's own (smaller, shrunk-for-gaps)
				//hit-box the way inWindowSelectLayer/a settings-tile-hit-test are, so mouse
				//forwarding stays reliably suspended across the WHOLE panel, including the
				//small gaps between tiles - "occasional mouse activity while in the front
				//panel" was traced to exactly that gap: the old per-tile check left mouse
				//forwarding briefly re-enabled whenever the stylus was between two tiles rather
				//than precisely on one.
				bool inFrontZone = std::abs(deviceLoc.position.z - WindowManager::getInstance().frontZoneZCenter)
					< WindowManager::getInstance().frontZoneZHalfThickness;

				//Top-level, object-independent wall-crossing state machine - replaces every
				//per-object attempt at deciding "has the stylus popped through into the merged
				//active room" that used to live in
				//WindowWallObject::calculateInteractionForce() (see that function's own comment
				//for the full history of why gating this from inside the object kept firing
				//early or missing narration entirely - isFocused() persisting across scan
				//cycles regardless of stylus position, and the room's local-frame Y-only
				//geometry having no X/Z gating so it could read "on" while genuinely over a
				//different-X tray tile at a similar Z depth). "There's no need for this to be
				//attached to an object at all - as top level behaviour this can be handled in
				//the main program loop", per the request.
				//
				//wallCenterZ mirrors, exactly, the world-Z position
				//ObjectFactory::createObjectsFromUIElements gives the single merged active
				//room's own WindowWallObject (its "zPos", evaluated at zPosSlotIndex ==
				//ACTIVE_ZONE_ROOM_COUNT - 1) - deliberately recomputed from the same fixed
				//constants (endZ, roomDepth, the 5.0f initial-room-margin and 10.0f walls-
				//thickness both already hardcoded at that call site) rather than read back off
				//a live WindowWallObject, since a plain world-Z comparison needs no per-object
				//local-frame geometry at all - it isn't affected by that object's own
				//90-degree Z-axis rotation (see its constructor) the way reading
				//stylusPosition.y out of calculateInteractionForce() was.
				constexpr float wallCenterZ = endZ - 5.0f - roomDepth - ((roomDepth + 10.0f) * (WindowManager::ACTIVE_ZONE_ROOM_COUNT - 1));

				//"if Z > (position of wall centre) + (margin) ... if Z < (position of wall
				//centre) - (margin)", per the request - a plain Schmitt-trigger/hysteresis band
				//around wallCenterZ, so ordinary position noise right at the wall doesn't
				//flicker the state back and forth. This is the same debounce concern
				//FOCUS_SWITCH_HYSTERESIS_FRAMES used to cover from inside the object - handled
				//here instead, on the one signal (plain world Z) that actually tracks which
				//side of the wall the stylus is on reliably.
				constexpr float ROOM_CROSSING_MARGIN = 5.0f;

				//"(while not in a menu)", per the request.
				if (!menuActive) {
					bool wasControllingActiveRoom = WindowManager::getInstance().isControllingActiveRoom;

					if (!wasControllingActiveRoom && deviceLoc.position.z > wallCenterZ + ROOM_CROSSING_MARGIN) {
						//Upward crossing, past the margin, for the first time - maximise/steal
						//OS foreground for the current slot's window, hand it the mouse, and
						//announce it ("Slot N, {app name}"), all three together, from the one
						//signal that reliably tracks which side of the wall the stylus is on.
						HWND hwnd = WindowManager::getInstance().getWindowHandleInSlot(WindowManager::getInstance().currentActiveSlotIndex);
						WindowManager::getInstance().bringWindowToFrontByHandle(hwnd);
						WindowManager::getInstance().isControllingActiveRoom = true;
						WindowManager::getInstance().narrateWindowFocus(hwnd);
					}
					else if (wasControllingActiveRoom && deviceLoc.position.z < wallCenterZ - ROOM_CROSSING_MARGIN) {
						//Downward crossing, past the margin - relinquish the mouse
						//(mouseSuspended, just below, reads this same flag) and fall back to
						//rendering/narrating the Program Tray/FFUI Settings front zone, which
						//already does so entirely independently of this flag (see
						//updateWindowNarration()'s own comment) - nothing further to trigger
						//here beyond clearing the flag itself.
						WindowManager::getInstance().isControllingActiveRoom = false;
					}
				}

				//Fed with this frame's freshly-updated room/button state, right after the
				//room-crossing block above (so isControllingActiveRoom reflects THIS frame, not
				//last frame's) and before any button-dispatch/force computation runs below -
				//see TutorialFlow's own class comment for why this is an overlay (observing
				//alongside normal interaction), not a takeover, and so needs no priority slot in
				//the button-handling chain the way menuActive/startMenuAwaiting do.
				{
					TutorialFlow::FrameInput tutorialInput;
					tutorialInput.deviceLoc = deviceLoc;
					tutorialInput.selectPressedEdge = frontButtonPressedEdge;
					tutorialInput.auxPressedEdge = auxPressedEdge;
					tutorialInput.middleButtonPressedEdge = middleButtonPressedEdgeForTutorial;
					tutorialInput.rearButtonPressedEdge = rearButtonPressedEdgeForTutorial;
					tutorialInput.inFrontZone = inFrontZone;
					tutorialInput.isControllingActiveRoom = WindowManager::getInstance().isControllingActiveRoom;
					TutorialFlow::getInstance().update(tutorialInput, voiceAssistant);
				}

				//"mouse control should always be suspended when Z is below this threshold", per
				//the request - i.e. whenever isControllingActiveRoom is false, which the
				//hysteresis band above guarantees stays false for the whole front-zone side and
				//only flips true once genuinely past the wall (and back to false the instant it
				//re-crosses back out), so this single flag is sufficient on its own without also
				//consulting inFrontZone here (inFrontZone keeps its own, separate role just
				//below, in the front-zone tile-click branch).
				//Hardware-test feedback: "can we make sure to suspend mouse control during the
				//first stages of tutorial?" - without this, a user simply orienting themselves
				//during Stage 0/1 (before the tutorial has explained the room at all) could
				//accidentally cross the wall into the merged active room - room-crossing detection
				//above runs unconditionally, independent of the tutorial - and start driving the
				//real OS mouse cursor before Stage 3 ever teaches that. Reuses
				//shouldSuppressNormalHaptics()'s own Stage 0/Stage 1 scope, since it's the same
				//"nothing real should happen yet" boundary Gareth already defined for haptics.
				bool mouseSuspended = !WindowManager::getInstance().isControllingActiveRoom || isGrabbingWindow || menuActive || startMenuAwaiting
					|| TutorialFlow::getInstance().shouldSuppressNormalHaptics();

				//Narrate (AUX tap, bit7) - Stage 1 only, see NARRATE_SPEC.md: a plain, standalone
				//tap of the AUX button (VoiceAssistant's own ArmedAfterTap window timing out with
				//no follow-up hold - see tryConsumeTapNarrateEdge()'s own comment) reads whatever
				//the stylus is currently on. Moved here from the rear button per the request "tap
				//to narrate, hold to dictate, tap+hold for the AI assistant... keep the back
				//button to right click" - the rear button used to do BOTH Narrate (unconditionally,
				//see the block this replaced) AND forward as an OS right-click (below, gated on
				//!mouseSuspended) at the same time whenever both conditions were true, which this
				//remap now cleanly separates: the rear button is exclusively right-click again,
				//and Narrate lives entirely on AUX.
				//!TutorialFlow::getInstance().isActive() so a stray tap during the tutorial's own
				//AUX button-tour checkpoint (Stage 1) or its later dictation/assistant setup
				//checkpoints (Stage 5) can't fire a real Narrate read before the tutorial has
				//finished. !isGrabbingWindow/!menuActive/!startMenuAwaiting: none of these states
				//has a well-defined single "current object" the way ordinary browsing does (a
				//menu's own MenuSystem narration already announces its highlighted item on its
				//own).
				if (voiceAssistant.tryConsumeTapNarrateEdge() && !TutorialFlow::getInstance().isActive()
					&& !isGrabbingWindow && !menuActive && !startMenuAwaiting) {
					NarrateFlow::getInstance().readCurrent(objectsListMutex, layers[0].objects);
				}

				//A menu just closed (either its last "Back" popped the stack empty, or
				//closeAll() was called) - re-trigger narration for whichever slot/tile the
				//stylus is now resting on, per the request's "when closed, the 'moved to a new
				//slot' behaviour should be triggered". Declared static/per-device-loop like
				//stylusState_previous just above, so this only fires on the actual transition,
				//not every frame menus happen to be closed.
				static bool menuActive_previous = false;
				if (menuActive_previous && !menuActive) {
					//New this round - see suppressNextMenuCloseRenarration's own comment
					//(FFUIDesktop.h): a Start-Menu program selection just spoke its own
					//"Confirmed, launching..." confirmation right before closing this menu, so
					//skip the normal re-narration this one time rather than let it cut that
					//confirmation off. Consumed (cleared) immediately regardless of
					//isControllingActiveRoom below, so a stray leftover true can't suppress some
					//unrelated later menu-close.
					bool suppressThisOne = suppressNextMenuCloseRenarration;
					suppressNextMenuCloseRenarration = false;

					if (!suppressThisOne) {
						WindowManager::getInstance().forceRenarrateOnNextFocus();

						//forceRenarrateOnNextFocus() just cleared lastNarratedWindowHandle, but
						//nothing scans the active room per-tick any more to pick that back up (see
						//updateWindowNarration()'s own comment) - the room's narration only fires on
						//the wall-crossing edge above, which doesn't happen again just because a
						//menu closed while the stylus stayed on the room side the whole time. Explicit
						//re-announcement here keeps this case working the same as it always has.
						if (WindowManager::getInstance().isControllingActiveRoom) {
							HWND hwnd = WindowManager::getInstance().getWindowHandleInSlot(WindowManager::getInstance().currentActiveSlotIndex);
							WindowManager::getInstance().narrateWindowFocus(hwnd);
						}
					}
				}
				menuActive_previous = menuActive;

				if (auxGestureState == AuxGestureState::AwaitingSetupConfirmation) {
					//While choosing whether to enable the assistant, the front/select button
					//commits whichever option is currently selected instead of being forwarded
					//as a normal OS left-click - matches how "select" already works everywhere
					//else in the app, and keeps a stray click from landing on whatever's under
					//the cursor while this haptic-only prompt has the device's attention. AUX
					//itself is no longer tied to this interaction at all once it's started -
					//see VoiceAssistant::commitSetupConfirmation() and the AuxGestureState
					//switch's AwaitingSetupConfirmation case for why.
					if (frontButtonPressedEdge) {
						voiceAssistant.commitSetupConfirmation();
					}
				}
				else if (menuActive) {
					//A FFUI Settings menu (or one of its submenus) is open - "other FFUI
					//behaviours should be suspended while these menus are active", per the
					//request, so this takes priority over the grab/window-select branches below
					//exactly like AwaitingSetupConfirmation does. While a Slider item is being
					//dragged, the select button's release commits the drag instead of acting as
					//a normal "select whatever's highlighted" press.
					if (MenuSystem::getInstance().isDraggingSlider()) {
						if (frontButtonReleasedEdge) {
							MenuSystem::getInstance().endSliderDrag();
						}
					}
					else if (frontButtonPressedEdge) {
						MenuSystem::getInstance().selectHighlighted(deviceLoc);
					}
				}
				else if (startMenuAwaiting) {
					//Mid-dictation or mid-search for the Start Menu flow, with no MenuSystem menu
					//open yet - same priority tier and "suspend everything else" treatment as
					//AwaitingSetupConfirmation/menuActive above. Nothing is currently selectable
					//(the front button is simply absorbed) - the user's only input here is the
					//AUX button itself (handled entirely by VoiceAssistant's own gesture state
					//machine, unaffected by this branch).
				}
				else if (isGrabbingWindow) {
					//Already mid-grab (started below with a select-button press while hovering
					//the window-select list) - the select button now completes the drop wherever
					//it's released, even though that's normally NOT the window-select layer
					//(the whole point is dragging into one of the active slots). Deliberately not
					//re-checked against inWindowSelectLayer here - the grab's own state, not the
					//stylus's current position, is what decides this button's meaning once a grab
					//is already underway.
					if (frontButtonReleasedEdge) {
						//Which option (a specific active slot, or -1 for "cancel") the release
						//lands on is decided purely by Z position now, via the same gap-free
						//partition classifyGrabZone() uses every frame for the live narration -
						//see its own comment. This replaces the old small-box gravity-well hit
						//test, which was the actual source of the phantom extra "cancel" zone
						//between Slot 1 and Slot 2 (a dead gap neither box covered).
						int releasedSlot = WindowManager::getInstance().classifyGrabZone(deviceLoc.position.z);
						bool droppedInActiveSlot = releasedSlot >= 0 && releasedSlot < WindowManager::getInstance().numOfActiveWindows;

						HWND grabbedWindow = WindowManager::getInstance().lastGrabbedWindowHandle;

						//Landing on one of the two active slots is a drop; anything else (still in
						//the window-select list, or nowhere at all) is a cancel - no swap. This is
						//narrower than the old middle-button behavior, which would also swap two
						//archived entries with each other; that's deliberately no longer possible,
						//per the "cancel is the selector zone" design.
						//Tracks whether loadSlot() below already spoke "Slot N, {app name}"
						//for this exact drop - if so, narrateGrabResult()'s own "Dropped in slot
						//N" must NOT also speak, since SPF_PURGEBEFORESPEAK would let it cut the
						//loadSlot() narration off, leaving "Dropped in slot N" as the only
						//thing actually heard - "instead of the 'dropped in slot n' narrator
						//prompt...the normal 'slot n: <program name>'", per the request.
						bool slotActivatedDirectly = false;
						if (droppedInActiveSlot && grabbedWindow != NULL) {
							HWND targetWindow = WindowManager::getInstance().getWindowHandleInSlot(releasedSlot);
							if (targetWindow != NULL) {
								WindowManager::getInstance().swapWindowSlots(grabbedWindow, targetWindow);

								//Dropping a program into a slot should immediately show it, not
								//leave the merged room showing whatever was previously selected -
								//"when a program is dropped into a slot, that slot is
								//automatically activated", per the request. windowWallSlotsMap now
								//has grabbedWindow assigned to releasedSlot per the swap just
								//above, so loadSlot() will re-derive and load exactly that window -
								//no need to pass it explicitly (see loadSlot()'s own comment for
								//why that's deliberate).
								WindowManager::getInstance().loadSlot(releasedSlot);
								slotActivatedDirectly = true;

								//Stage 4's drag-and-drop checkpoint - a no-op unless the tutorial
								//is actually currently waiting on this exact action.
								TutorialFlow::getInstance().notifyProgramDropped();
							}
						}

						//Console log always fires (diagnostic value); the spoken "Dropped in
						//slot N"/"Drag cancelled" only fires when loadSlot() above didn't
						//already narrate this same drop. The pre-existing "dropped on a genuinely
						//empty slot" quirk (targetWindow == NULL, so swapWindowSlots/loadSlot
						//never ran - swapWindowSlots needs an existing occupant) still speaks
						//"Dropped in slot N" here rather than going silent - out of scope to fix.
						WindowManager::getInstance().narrateGrabResult(droppedInActiveSlot, releasedSlot, !slotActivatedDirectly);

						WindowManager::getInstance().isUserGrabbingWindow.store(false);
					}
				}
				else if (inWindowSelectLayer) {
					//Not yet grabbing, but hovering the window-select list - a press here starts
					//a grab (this replaces the old dedicated middle-button grab gesture) instead
					//of a normal left-click.
					if (frontButtonPressedEdge) {
						//Save the window the stylus is on currently in corresponding static variable
						WindowManager::getInstance().lastGrabbedWindowHandle =
							WindowManager::getInstance().getHandleOfTheWindowTheStylusIsOn(objectsListMutex);

						//This will trigger the scanner thread to sleep and create the gravity wells.
						WindowManager::getInstance().isUserGrabbingWindow.store(true);

						WindowManager::getInstance().narrateGrabStart(WindowManager::getInstance().lastGrabbedWindowHandle);

						//Anchor the drag detent to wherever the stylus actually is right now, so
						//the "rail" the user slides along starts exactly where they grabbed from
						//rather than snapping to some fixed point.
						WindowManager::getInstance().beginGrabDetent(deviceLoc.position);
					}
				}
				else if (inFrontZone) {
					//Within the shared front zone's Z-band but not currently over a Program
					//Tray window (that's handled above) - either an FFUI Settings tile, or
					//empty space/a gap between tiles. A LIVE hit test (this exact frame's
					//position, not whatever last frame's per-object pass happened to leave
					//stylusOnThis set to) decides whether a Settings tile is actually under the
					//stylus right now - see liveHitTestSettingsTile()'s own comment for why this
					//one is safe to recompute on demand. Landing anywhere else in the zone
					//simply absorbs the press - no OS click is forwarded, since mouse
					//forwarding is suspended across the whole panel now (see mouseSuspended
					//above), not just precisely on a tile.
					if (frontButtonPressedEdge) {
						GridTileObject* hitSettingsTile = liveHitTestSettingsTile(deviceLoc);
						if (hitSettingsTile != nullptr) {
							FFUIDesktop::openSettingsTileMenu(hitSettingsTile->getTileIndex());
						}
					}
				}
				else {
					//Reached only when none of AwaitingSetupConfirmation/menuActive/
					//isGrabbingWindow/inWindowSelectLayer/inFrontZone apply - i.e. exactly while
					//browsing the single merged active-slot room. The scroll wheel's own
					//clickable center button (bit4, "middle" per the button-map comment above -
					//freed up since the old dedicated middle-button grab gesture moved to the
					//select button) cycles to the next quickslot here - "clicking the scroll
					//wheel will cycle between program slots... this behaviour should be paused
					//while in the FFUI settings / app drawer", per the request. This placement
					//already satisfies that (every other branch above takes priority), and
					//incidentally also pauses it during a grab or an open menu, which is
					//desirable. Checked directly against a press edge, NOT through the
					//room-crossing state machine's own ROOM_CROSSING_MARGIN hysteresis (see its
					//comment near mouseSuspended above) - that debounce exists to filter a noisy
					//CONTINUOUS position signal; a button press edge is already a discrete,
					//deliberate action, so hysteresis would only add unwanted latency here.
					//
					//Both this and the left-click forwarding just below are now gated on
					//!mouseSuspended too - hardware-test feedback: "we also need to make sure
					//button controls aren't mapped to the mouse during the buttons tutorial -
					//right now you can be silently clicking things on programs during this
					//step". mouseSuspended already covers Stage 0/1 via
					//shouldSuppressNormalHaptics() (see its own comment above) and already gates
					//cursor movement and the rear-button/scroll-wheel forwarding just below this
					//block - this branch (the real active-slot room's left-click and
					//quickslot-cycle forwarding) was the one place that got missed, since
					//isControllingActiveRoom being true is what routes execution into this
					//`else` in the first place, and nothing here ever re-checked mouseSuspended
					//before acting. A user standing in the active room's Z-band during Stage 1's
					//button tour (which doesn't move them anywhere - they could easily already be
					//there) had every "press select now" checkpoint also silently left-click
					//whatever program is showing there.
					bool middleButtonPressedEdge = (((currentInput >> 4) & 0x1) == 0) && (((stylusState_previous >> 4) & 0x1) == 1);
					if (!mouseSuspended) {
						if (middleButtonPressedEdge) {
							std::cout << "[Slot] button pressed" << std::endl;
							WindowManager::getInstance().cycleActiveSlot();
						}

						//front button
						//Released
						if (frontButtonReleasedEdge) {
							SendMouseInput(MOUSEEVENTF_LEFTUP);
						}
						//Pressed
						if (frontButtonPressedEdge) {
							SendMouseInput(MOUSEEVENTF_LEFTDOWN);
						}
					}
				}

				//rear button, scroll buttons: suspended along with the rest of normal mouse
				//forwarding while mouseSuspended (see its own comment above) - none of these are
				//useful to send to the OS while browsing the window-select list or mid-grab.
				//The old dedicated middle-button (bit4) grab gesture has been replaced entirely
				//by the select-button handling above, so bit4 is no longer read here.
				if (!mouseSuspended) {
					//rear button
					//Released
					if ((currentInput >> 1 & 0x1) == 1 && (stylusState_previous >> 1 & 0x1) == 0) {
						SendMouseInput(MOUSEEVENTF_RIGHTUP);
					}
					//Pressed
					if ((currentInput >> 1 & 0x1) == 0 && (stylusState_previous >> 1 & 0x1) == 1) {
						SendMouseInput(MOUSEEVENTF_RIGHTDOWN);
					}


					//scroll down half

					if ((currentInput >> 2 & 0x1) == 0 && (stylusState_previous >> 2 & 0x1) == 1) {
						SendMouseInput(MOUSEEVENTF_WHEEL, (DWORD)(-120));
					}

					//scroll down full
					if ((currentInput >> 3 & 0x1) == 0) {
						static int scrollDownRepeatCounter = 0;
						scrollDownRepeatCounter++;

						if (scrollDownRepeatCounter > desktopConfig.targetHapticFramerate / SCROLL_REPEAT_RATE) {
							SendMouseInput(MOUSEEVENTF_WHEEL, (DWORD)(-10));
							scrollDownRepeatCounter = 0;
						}
					}


					//scroll up full
					if ((currentInput >> 5 & 0x1) == 0 && (stylusState_previous >> 5 & 0x1) == 1) {
						//SendMouseInput(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);
					}
					//if ((currentInput >> 5 & 0x1) == 0 && (stylusState_previous >> 5 & 0x1) == 1) {
					if ((currentInput >> 5 & 0x1) == 0) {
						static int scrollUpRepeatCounter = 0;
						scrollUpRepeatCounter++;

						if (scrollUpRepeatCounter > desktopConfig.targetHapticFramerate / SCROLL_REPEAT_RATE) {
							SendMouseInput(MOUSEEVENTF_WHEEL, (DWORD)(10));
							scrollUpRepeatCounter = 0;
						}
					}

					//scroll up half
					if ((currentInput >> 6 & 0x1) == 0 && (stylusState_previous >> 6 & 0x1) == 1) {
						SendMouseInput(MOUSEEVENTF_WHEEL, (DWORD)(120));
					}
				}

				stylusState_previous = currentInput;

				cursorPos.x = desktopConfig.cursorFilter * cursorPos.x + (1.0 - desktopConfig.cursorFilter) * deviceManager.devices[x].deviceStatus.position[0];

				cursorPos.y = desktopConfig.cursorFilter * cursorPos.y + (1.0 - desktopConfig.cursorFilter) * deviceManager.devices[x].deviceStatus.position[1];




				//Cursor movement is likewise suspended while mouseSuspended - the filter above
				//still runs every frame regardless, so cursorPos doesn't need to "catch up" once
				//movement resumes; only the actual OS cursor warp is skipped.
				//if (deviceLoc.position.z > 144) {
				if (!mouseSuspended) {
					moveWindowsCursor(cursorPos);
				}
				//}


				//printf("%f z\n", deviceLoc.position.z);
				//Re-read menuActive fresh here (rather than reusing the snapshot taken at the
				//top of the frame, before button-handling ran) - the button-handling block just
				//above can itself open/close/step a menu this same frame (e.g. pressing a
				//Settings tile opens one, or a Back item empties the stack), and without this
				//re-check the force below would still branch on this frame's now-stale
				//pre-button-handling state for one frame.
				bool menuActiveThisFrame = MenuSystem::getInstance().isActive();

				//Re-read fresh here too, same reason as menuActiveThisFrame above - a Start
				//programs tile press earlier THIS frame (in the inFrontZone button-handling
				//branch) can flip StartMenuFlow Idle->AwaitingDictation before this force block
				//runs, and the top-of-frame startMenuAwaiting snapshot would otherwise still read
				//stale (false) for this one frame.
				bool startMenuAwaitingThisFrame = StartMenuFlow::getInstance().state() == StartMenuFlow::State::AwaitingDictation
					|| StartMenuFlow::getInstance().state() == StartMenuFlow::State::Searching;

				Vector3 force(0, 0, 0);

				if (auxGestureState == AuxGestureState::AwaitingSetupConfirmation) {
					//The user is choosing (via the two-detent haptic prompt) whether to enable
					//the AI assistant - pause all other haptic rendering entirely (not just
					//override it) so the two-detent feel isn't competing with UI/boundary
					//forces. processForces() is skipped outright, not computed and discarded.
					//Checked first, ahead of the assistantHasFloor() branch below (which is also
					//true here, via confirmationPromptActive) - otherwise that branch would zero
					//this out instead of rendering the actual detent force.
					force = voiceAssistant.getConfirmationForce(deviceLoc);
				}
				else if (menuActiveThisFrame) {
					//A FFUI Settings menu (or submenu) is open - render ONLY the menu's own
					//tile-grid/slider force this frame, same "pause all other haptic
					//rendering" spirit as the AwaitingSetupConfirmation branch above
					//(processForces()/boundary forces are skipped outright, not computed and
					//discarded).
					force = MenuSystem::getInstance().updateForce(deviceLoc);
				}
				else if (voiceAssistant.assistantHasFloor()) {
					//No UI-attraction or boundary-wall force while the assistant otherwise has
					//the floor - CLI setup running, or a hold-to-talk exchange in flight through
					//its spoken reply (see assistantHasFloor()'s comment). This branch is purely
					//about not competing for the user's attention with a second, unrelated
					//haptic sensation while they're mid-interaction with the assistant - it is
					//NOT what prevents narration from clipping any more (that's now handled at
					//the voice level: pAssistantVoice and pSapiVoice are two independent SAPI
					//voice instances - see FFUIDesktop::initDesktop() - so one's
					//SPF_PURGEBEFORESPEAK simply can't purge the other's queue in the first
					//place, and initiatePeriodicScanner() ducks pSapiVoice's volume rather than
					//pausing scanning, so FFUI's own narration keeps running quietly throughout).
					//Deliberately zero rather than computed-and-discarded, same spirit as the
					//confirmation-prompt branch above - cursor movement and button/click
					//forwarding, just above this block, are untouched so the user can still move
					//the mouse and click to interact with the visible `claude` console window or
					//the OAuth browser tab.
					force = Vector3(0, 0, 0);
				}
				else if (startMenuAwaitingThisFrame) {
					//Mid-dictation or mid-search for the Start Menu flow - same "pause everything
					//else" treatment as the assistantHasFloor() branch just above (indeed, the
					//actual AUX-hold capture portion of dictation is already covered by that
					//branch too, via exchangeInProgress - this one specifically covers the two
					//phases assistantHasFloor() does NOT: the moment right after selecting "Start
					//programs", before AUX is even pressed, and the search itself running on a
					//background thread once dictation completes).
					force = Vector3(0, 0, 0);
				}
				else if (isGrabbingWindow) {
					//Dragging a grabbed window: a single continuous spring pulling the stylus
					//back toward the X/Y position the grab started at, with force.z always
					//exactly 0 from the detent itself - so the whole Z axis stays a free "rail"
					//to slide along between options, rather than the old per-slot gravity-well
					//boxes (which pulled toward a different point - or nowhere at all -
					//depending on which box you happened to be in, hence feeling inconsistent).
					//UI-object forces are still skipped entirely (same spirit as the
					//AwaitingSetupConfirmation/assistantHasFloor branches above), but the
					//workspace's own boundary walls are layered back in underneath the detent
					//(pop-through resistance at the edges of the reachable space), and
					//computeGrabZoneWallForce() layers in a second, narrower pop-through right
					//at each boundary BETWEEN slots too - "we still need to re-introduce the
					//layer pop-throughs (between slot1/slot2 etc) when dragging a program from
					//the front", per the request. See that method's own comment for why this
					//was missing during a grab specifically.
					force = WindowManager::getInstance().computeGrabDetentForce(deviceLoc.position)
						+ processBoundaryForces(deviceLoc)
						+ WindowManager::getInstance().computeGrabZoneWallForce(deviceLoc.position.z);
				}
				else {
					//"read this state from the cursor and generate unique vibration effects
					//accordingly", per the request - layered on top of everything below the
					//same way SolidPlane's own edge-buzz layers on top of its boundary's normal
					//force (see CursorStateHaptics.h's own comment). Gated on
					//isControllingActiveRoom specifically, not just "reached this else branch" -
					//this branch is also reached in the (normally momentary) gap between the
					//front zone's own Z-band and the room's wall-crossing margin, where the
					//real OS cursor isn't actually being driven by the stylus at all (mouse
					//forwarding is still suspended there - see mouseSuspended's own comment) and
					//polling its shape would just reflect wherever it was last left idle, not
					//anything meaningful about this tick's stylus position.
					//
					//Moved to run FIRST this round (was previously polled near the bottom of this
					//branch) - now that update() also classifies whether the real cursor is over
					//actual text (isCursorOverTextField()), that answer is needed up front, before
					//processForces() below decides whether to suppress the ordinary per-object
					//detent pull.
					Vector3 cursorForce(0, 0, 0);
					bool overRealText = false;
					if (WindowManager::getInstance().isControllingActiveRoom) {
						cursorForce = cursorStateHaptics.update(deviceLoc);
						overRealText = cursorStateHaptics.isCursorOverTextField();
					}

					//Word's UI Automation tree apparently exposes each paragraph as its own
					//scanned list-item-shaped ButtonObject, whose own whole-paragraph
					//pull-to-center detent was fighting CursorStateHaptics' new per-LINE detent
					//(computeTextLineDetentForce(), folded into cursorForce above) one-for-one,
					//making individual lines hard to hover - "playing with a word doc, it feels
					//as though each paragraph has a detent, pulling towards the middle... can we
					//adjust so each line has its own detent", per the request.
					//suppressButtonDetents=true (only while overRealText) drops just that one
					//contribution - see processForces()'s own comment (FFUIDesktop.h) for
					//exactly what is and isn't affected.
					force = processForces(deviceLoc, overRealText); //Interactive forces according to object type

					//The single, centralized narration decision for windows/Program Tray - see
					//WindowManager::updateWindowNarration()'s own comment. Must run AFTER
					//processForces() above (which is what actually updates every object's own
					//stylusOnThis hit-test result for this tick) and exactly once per tick, not
					//once per object.
					WindowManager::getInstance().updateWindowNarration();

					//FFUI Settings / Program Tray front zone: a light pull toward the nearest
					//tile while nothing in that section is currently highlighted, plus a
					//two-sided "wall" felt at the X=0 boundary between them - both layered
					//additively on top of the normal per-object forces above (not a
					//replacement), and both harmlessly return zero outside the front zone's
					//Z-band. Also narrates whichever FFUI Settings tile (if any) the stylus is
					//now on - see updateFrontZoneSectionNarration()'s own comment for why
					//windows/Program Tray use the separate call above instead.
					force += WindowManager::getInstance().computeFrontZoneMagnetism(deviceLoc);
					force += WindowManager::getInstance().computeSectionDividerForce(deviceLoc);
					WindowManager::getInstance().updateFrontZoneSectionNarration(deviceLoc);

					//cursorForce (both the always-on Z-buzz texture and, new this round, the
					//X/Y line-detent pull while overRealText) computed up top - see this
					//branch's own opening comment.
					force += cursorForce;

					//Tutorial guidance/demo force (Stage 3a's pull toward the Program Slot room,
					//Stage 2's demo vibrations) - purely additive, zero at every other stage/
					//moment, see TutorialFlow's own class comment on why this is an overlay
					//rather than a takeover.
					force += TutorialFlow::getInstance().updateForce(deviceLoc);

					//DEACTIVATED, not deleted - "keep but deactivate the code for the 'find
					//nearest UI element', we may remap this later", per the request. This used to
					//push toward the closest object while AUX was held in plain-hold mode; the
					//AUX button's plain hold is now "dictate" instead (see AuxGestureState's own
					//comment - Held_PullMode/DictatingMode in VoiceAssistant.h/.cpp), which is a
					//pure button+microphone interaction with no force of its own, so nothing here
					//replaces this call - the force block just falls through with whatever
					//processForces()/front-zone force was already computed above.
					// if (auxGestureState == AuxGestureState::Held_PullMode) {
					//     force = calculateForceToClosestObject(deviceLoc, false);
					// }
				}

				//Live grab feedback: report which option (a specific active slot, or "cancel" -
				//the window-select list or nowhere) the stylus is currently over, using the same
				//gap-free Z partition as the release handler above (see classifyGrabZone()'s own
				//comment) - no longer dependent on processForces()/gravity wells having run this
				//frame, since it's now decided purely from deviceLoc.position.z. Only actually
				//speaks/prints when the option has changed, so it's safe (and necessary, to
				//catch every transition) to call this every frame while a grab is in progress.
				//Re-checks isUserGrabbingWindow fresh here (rather than reusing the
				//isGrabbingWindow snapshot from the top of the frame) so a release earlier this
				//same frame - which already spoke the final "dropped"/"cancelled" result above -
				//doesn't immediately get talked over by a redundant "option: ..." repeat.
				if (WindowManager::getInstance().isUserGrabbingWindow.load()) {
					int hoveredSlot = WindowManager::getInstance().classifyGrabZone(deviceLoc.position.z);
					WindowManager::getInstance().narrateGrabOption(hoveredSlot);
				}

				//First-hardware-test feedback: "I can feel the FFUI interface during the intro -
				//there should be no haptic effects until they are introduced later." Rather than
				//threading a suppression check through every individual force source above
				//(processForces(), boundary forces, menu forces, cursorStateHaptics, the grab
				//detent...), zero the fully-aggregated total here, in the one place they all
				//funnel through before reaching the device - simplest, and can't miss a source.
				//Only true during Stage 0/Stage 1 (see shouldSuppressNormalHaptics()'s own
				//comment); TutorialFlow's own Stage 2 demo/Stage 3a guidance forces are computed
				//elsewhere in `force` too, but both already return zero outside their own stages,
				//so there's nothing of the tutorial's own to preserve here either.
				if (TutorialFlow::getInstance().shouldSuppressNormalHaptics()) {
					force = Vector3(0, 0, 0);
				}

				//FIX, new this round ("getting quite some lag when in the program slot... briefly
				//freeze every second or so"): stylusAtRest's own rest-detection (see that flag's
				//own comment) needs to know whether a real pull force was actually being rendered
				//just before this tick, not only whether the raw position happened to sit still -
				//see that gate's own comment near the top of this function for the actual bug this
				//fixes. X/Y ONLY, deliberately excluding force.z - Z carries the cursor-state/
				//edge "buzz" vibration textures (CursorStateHaptics, SolidPlane), which are brief
				//oscillating bursts, not something that actually holds the physical tip still the
				//way a detent's X/Y spring does; folding Z in here would make this gate flicker
				//with whatever burst phase a vibration happened to be in, for no reason connected
				//to the actual bug. Stashed here, right where this tick's force is finalized, so
				//next frame's rest-check reads exactly what was last sent to the device.
				lastAppliedForceMagnitude = Vector2(force.x, force.y).length();

				//Send calculated aggregate of forces to device
				LibreOne_targets forceTargets;
				forceTargets.targets[0] = force.x;
				forceTargets.targets[1] = force.y;
				forceTargets.targets[2] = force.z;
				deviceManager.devices[x].serialComms.sendTargets(forceTargets);
			}
		}
	}
}


Vector3 FFUIDesktop::processForces(Location stylusLocation, bool suppressButtonDetents) {
    Vector3 uiForce(0, 0, 0);
    Vector3 boundaryForce(0, 0, 0);


    float globalUiForceLimit = 0.015f;
	std::lock_guard<std::mutex> lock(objectsListMutex);

    for (int x = 0; x < layers.size(); x++) {
        for (int y = 0; y < layers[x].objects.size(); y++) {

            // Calculate individual object force - still called unconditionally even when its
            // contribution below ends up discarded, so every object's own stylusOnThis/hit-test
            // state (read elsewhere, e.g. NarrateFlow's ButtonObject::stylusIsOnThis()) stays
            // live and current every tick, suppressButtonDetents or not.
            Vector3 f = layers[x].objects[y]->updateForces(stylusLocation);

            // Check if this is a boundary plane

            std::string name = layers[x].objects[y]->getMeta().customName;

            if (name.find("Boundary") != std::string::npos) {

                boundaryForce += f;
            }
            else {
                //New this round: while suppressButtonDetents is set, a scanned Button/ListItem/
                //MenuItem element's own pull-to-center detent (a ButtonObject instance - see
                //ObjectsFactory.cpp's default: branch, the only place one is ever constructed) is
                //dropped from the total entirely - see this method's own header comment
                //(FFUIDesktop.h) for why. WindowWallObject/GravityWellObject/GridTileObject
                //forces are untouched.
                bool isSuppressedButtonDetent = suppressButtonDetents
                    && dynamic_cast<ButtonObject*>(layers[x].objects[y].get()) != nullptr;
                if (!isSuppressedButtonDetent) {
                    uiForce += f;
                }
            }
        }
    }


    if (uiForce.length() > globalUiForceLimit) {
        uiForce = uiForce.normalized() * globalUiForceLimit;
    }

    return uiForce + boundaryForce;
}


Vector3 FFUIDesktop::processBoundaryForces(Location stylusLocation) {
    // Same boundary-plane pass as processForces() above, but skipping the UI-object pull
    // entirely - used during a grab/drag, where the detent (computeGrabDetentForce) replaces
    // UI-object forces, but the workspace's own solid walls ("pop-through" resistance at the
    // edges of the reachable space) should still be felt layered underneath it.
    Vector3 boundaryForce(0, 0, 0);
    std::lock_guard<std::mutex> lock(objectsListMutex);

    for (int x = 0; x < layers.size(); x++) {
        for (int y = 0; y < layers[x].objects.size(); y++) {
            std::string name = layers[x].objects[y]->getMeta().customName;
            if (name.find("Boundary") != std::string::npos) {
                boundaryForce += layers[x].objects[y]->updateForces(stylusLocation);
            }
        }
    }

    return boundaryForce;
}


bool FFUIDesktop::isStylusInWindowSelectLayer() {
	HWND hoveredHandle = NULL;

	if (WindowManager::getInstance().isUserGrabbingWindow.load()) {
		//Mid-grab: the real WindowWallObjects have been swapped out for GravityWellObject
		//stand-ins (so the scanner thread doesn't need to re-scan live while dragging), so we
		//have to look at those instead - the same object list processForces() reads from.
		hoveredHandle = WindowManager::getInstance().getHandleOfTheWindowTheStylusIsOn(objectsListMutex, layers[0].objects);
	}
	else {
		//Not grabbing - the real WindowWallObjects are in play, so check them directly.
		hoveredHandle = WindowManager::getInstance().getHandleOfTheWindowTheStylusIsOn(objectsListMutex);
	}

	return hoveredHandle != NULL && WindowManager::getInstance().isArchivedSlot(hoveredHandle);
}

GridTileObject* FFUIDesktop::liveHitTestSettingsTile(Location stylusLoc) {
	// Locked against windowMutex (not objectsListMutex) since that's what actually guards
	// SettingsTiles - it's reassigned wholesale by the scanner thread each cycle, independently
	// of layers[0].objects itself. Held for the whole loop, including the updateForces() calls,
	// so a concurrent scan-cycle swap can't free the very objects being dereferenced here (see
	// this method's own header comment for why calling updateForces() fresh, here, is safe).
	std::lock_guard<std::recursive_mutex> lock(WindowManager::getInstance().windowMutex);
	for (GridTileObject* tile : WindowManager::getInstance().SettingsTiles) {
		if (tile == nullptr) continue;
		tile->updateForces(stylusLoc);
		if (tile->stylusIsOnThis()) return tile;
	}
	return nullptr;
}

void FFUIDesktop::openSettingsTileMenu(int tileIndex) {
	// 5-tile order: 0 = Start programs, 1 = Quick slots settings, 2/3 = FFUI/AI narrator
	// settings, 4 = FFUI Tutorial - see addFrontZoneSettingsTiles()'s kSettingsTileLabels.
	if (tileIndex == 0) {
		StartMenuFlow::getInstance().begin();
		return;
	}
	if (tileIndex == 1) {
		FFUIDesktop::openQuickSlotsMenu();
		return;
	}
	if (tileIndex == 4) {
		// Manual re-launch - "be manually launchable from a new button 'FFUI tutorial' added in
		// the FFUI settings room", per the request. autoStart=false: no double-press-abandon
		// window and no "first time launching" line - see TutorialFlow::begin()'s own comment.
		TutorialFlow::getInstance().begin(false);
		return;
	}

	bool isFfuiVoice = (tileIndex == 2);
	std::wstring title = isFfuiVoice ? L"Settings: FFUI Narrator" : L"Settings: AI Narrator";

	std::vector<MenuItemDef> items;

	MenuItemDef voiceStyleItem;
	voiceStyleItem.label = L"Voice style";
	voiceStyleItem.type = MenuItemType::Action;
	voiceStyleItem.onSelect = [isFfuiVoice]() { FFUIDesktop::openVoiceStyleMenu(isFfuiVoice); };
	items.push_back(voiceStyleItem);

	MenuItemDef voiceSpeedItem;
	voiceSpeedItem.label = L"Voice speed";
	voiceSpeedItem.type = MenuItemType::Action;
	voiceSpeedItem.onSelect = [isFfuiVoice]() { FFUIDesktop::openVoiceSpeedMenu(isFfuiVoice); };
	items.push_back(voiceSpeedItem);

	MenuItemDef backItem;
	backItem.label = L"Back";
	backItem.type = MenuItemType::Back;
	items.push_back(backItem);

	// createMenu() itself speaks "Menu opened: {title}" - with title set to exactly
	// "FFUI narrator settings"/"AI narrator settings" above, this already satisfies the
	// request's "play a narrator prompt 'Menu opened: <X> narrator settings'" with no
	// special-casing needed beyond choosing this title text.
	MenuSystem::getInstance().createMenu(title, items);
}

void FFUIDesktop::openVoiceStyleMenu(bool isFfuiVoice) {
	std::vector<VoiceChoice> voices = VoiceSettingsManager::getInstance().enumerateVoices();

	std::vector<MenuItemDef> items;
	for (const VoiceChoice& voice : voices) {
		MenuItemDef item;
		item.label = voice.displayName;
		item.type = MenuItemType::Action;
		std::wstring tokenId = voice.tokenId;   // captured by value below - voices vector doesn't outlive this function
		item.onSelect = [isFfuiVoice, tokenId]() {
			if (isFfuiVoice) VoiceSettingsManager::getInstance().setFfuiVoice(tokenId);
			else VoiceSettingsManager::getInstance().setAssistantVoice(tokenId);
			// Selecting a voice applies + persists it (setFfuiVoice/setAssistantVoice do both),
			// then returns to the parent ("... narrator settings") menu, mirroring how a plain
			// selection elsewhere in this app closes back to where it came from.
			MenuSystem::getInstance().back();
		};
		items.push_back(item);
	}

	MenuItemDef backItem;
	backItem.label = L"Back";
	backItem.type = MenuItemType::Back;
	items.push_back(backItem);

	MenuSystem::getInstance().createMenu(L"Voice style", items);
}

void FFUIDesktop::openVoiceSpeedMenu(bool isFfuiVoice) {
	std::vector<MenuItemDef> items;

	// The menu's own slider range is a friendlier 1 (slowest) - 12 (fastest) scale - "limit
	// the speed values between 1 - 12", per the request - rather than exposing SAPI's own
	// native -10..10 rate directly. Nothing downstream changes: VoiceSettingsManager,
	// applyRate(), and the persisted .ini all still speak the real SAPI rate; only this menu's
	// slider bounds and the two small conversions below are new.
	static constexpr float SPEED_SCALE_MIN = 1.0f;
	static constexpr float SPEED_SCALE_MAX = 12.0f;
	static constexpr float SAPI_RATE_MIN = -10.0f;
	static constexpr float SAPI_RATE_MAX = 10.0f;
	auto speedToSapiRate = [](float speed) -> int {
		float t = (speed - SPEED_SCALE_MIN) / (SPEED_SCALE_MAX - SPEED_SCALE_MIN);
		float rate = SAPI_RATE_MIN + t * (SAPI_RATE_MAX - SAPI_RATE_MIN);
		return (int)std::lround(rate);
	};
	auto sapiRateToSpeed = [](int rate) -> float {
		float t = ((float)rate - SAPI_RATE_MIN) / (SAPI_RATE_MAX - SAPI_RATE_MIN);
		return SPEED_SCALE_MIN + t * (SPEED_SCALE_MAX - SPEED_SCALE_MIN);
	};

	MenuItemDef sliderItem;
	// "setting" included in the label itself (rather than appended only for the first-highlight
	// hint) so both the highlight narration ("Voice speed setting, drag sideways") and the
	// tile's own repeat-visit narration ("Voice speed setting") match the request's wording.
	sliderItem.label = L"Voice speed setting";
	sliderItem.type = MenuItemType::Slider;
	sliderItem.sliderMin = SPEED_SCALE_MIN;
	sliderItem.sliderMax = SPEED_SCALE_MAX;
	sliderItem.getValue = [isFfuiVoice, sapiRateToSpeed]() -> float {
		int rate = isFfuiVoice ? VoiceSettingsManager::getInstance().getFfuiRate() : VoiceSettingsManager::getInstance().getAssistantRate();
		return sapiRateToSpeed(rate);
	};
	// Applied every frame while dragging (live audible feedback) - cheap, in-memory only, no
	// disk I/O on the haptic thread. See MenuItemDef::setValue's own comment.
	sliderItem.setValue = [isFfuiVoice, speedToSapiRate](float v) {
		VoiceSettingsManager::getInstance().applyRate(isFfuiVoice ? pSapiVoice : pAssistantVoice, speedToSapiRate(v));
	};
	// Persists the final value exactly once, on release.
	sliderItem.commitValue = [isFfuiVoice, speedToSapiRate](float v) {
		int rate = speedToSapiRate(v);
		if (isFfuiVoice) VoiceSettingsManager::getInstance().setFfuiRate(rate);
		else VoiceSettingsManager::getInstance().setAssistantRate(rate);
	};
	// v is already on the 1-12 scale here (MenuSystem always rounds/formats within the item's
	// own sliderMin/sliderMax), so this just displays it directly - no conversion needed.
	sliderItem.formatValue = [](float v) -> std::wstring {
		return std::to_wstring((long long)std::lround(v));
	};
	items.push_back(sliderItem);

	MenuItemDef backItem;
	backItem.label = L"Back";
	backItem.type = MenuItemType::Back;
	items.push_back(backItem);

	MenuSystem::getInstance().createMenu(L"Voice speed", items);
}

void FFUIDesktop::openQuickSlotsMenu() {
	std::vector<MenuItemDef> items;

	MenuItemDef sliderItem;
	sliderItem.label = L"Quick slots setting";
	sliderItem.type = MenuItemType::Slider;
	sliderItem.sliderMin = (float)QuickSlotsSettingsManager::MIN_SLOT_COUNT;
	sliderItem.sliderMax = (float)QuickSlotsSettingsManager::MAX_SLOT_COUNT;
	sliderItem.getValue = []() -> float {
		return (float)QuickSlotsSettingsManager::getInstance().getSlotCount();
	};
	// Deliberately a no-op, unlike the voice-speed slider above (a harmless-to-preview audio
	// property). Changing the quickslot count live, mid-drag, would visibly thrash which
	// slot/window is currently rendered while the user is still sliding - the actual change is
	// deferred to commitValue below, on release. MenuSystem's own live narration during the drag
	// (via formatValue) still lets the user hear the number changing as they move.
	sliderItem.setValue = [](float) {};
	// Persists the final value exactly once, on release - clamps, applies to
	// WindowManager::numOfActiveWindows (and clamps currentActiveSlotIndex if it's now out of
	// range - see WindowManager::setActiveSlotCount()'s own comment), and saves to disk.
	sliderItem.commitValue = [](float v) {
		QuickSlotsSettingsManager::getInstance().setSlotCount((int)std::lround(v));
	};
	sliderItem.formatValue = [](float v) -> std::wstring {
		return std::to_wstring((long long)std::lround(v));
	};
	items.push_back(sliderItem);

	MenuItemDef backItem;
	backItem.label = L"Back";
	backItem.type = MenuItemType::Back;
	items.push_back(backItem);

	MenuSystem::getInstance().createMenu(L"Settings: Program quickslots", items);
}


void FFUIDesktop::moveWindowsCursor(Vector2 targetPos) {
	targetPos.x += DEVICE_WORKSPACE_X / 2 + DEVICE_WORKSPACE_OFFSETX;
	targetPos.y += DEVICE_WORKSPACE_Y / 2 - DEVICE_WORKSPACE_OFFSETY;

	targetPos.x *= cusrsorScale.x;
	targetPos.y *= cusrsorScale.y;
	if (targetPos.x < 0) targetPos.x = 0;
	if (targetPos.x > desktopConfig.screenSize.x) targetPos.x = desktopConfig.screenSize.x;
	if (targetPos.y < 0) targetPos.y = 0;
	if (targetPos.y > desktopConfig.screenSize.y) targetPos.y = desktopConfig.screenSize.y;



	int finalPixelX = static_cast<int>(targetPos.x);
	int finalPixelY = static_cast<int>(desktopConfig.screenSize.y - targetPos.y);

	INPUT input = { 0 };
	input.type = INPUT_MOUSE;
	input.mi.dx = (finalPixelX * 65535) / (desktopConfig.screenSize.x - 1);
	input.mi.dy = (finalPixelY * 65535) / (desktopConfig.screenSize.y - 1);

	input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;

	
	SendInput(1, &input, sizeof(INPUT));

}