#pragma once
#include "dataTypes.h"
#include "FFUIDesktop_object.h"
#include "mathTypes.h"
#include <windows.h>
#include "SenmagConnect.h"
#include <thread>
#include <stop_token>
#include <unordered_set>
#include "DeviceManager.h"
#include "GridTileObject.h"

#include "HapticVibration.h"
#include "SolidPlane.h"
#include "CursorStateHaptics.h"
#include "ButtonObject.h"
#include <sapi.h>
#include "VoiceAssistant.h"
#include "QuickSlotsSettingsManager.h"
#include "TutorialFlow.h"
//We use these libraries to upgrade what the console can print with wcout
#include <fcntl.h>
#include <io.h>


#define DEVICE_WORKSPACE_X	250
#define DEVICE_WORKSPACE_Y	180

//The range of the z axis values that the device reaches (not counting the front wall)
#define startZ	124.0f
#define endZ	268.0f

# define roomDepth 50


#define DEVICE_WORKSPACE_OFFSETX	0
#define DEVICE_WORKSPACE_OFFSETY	0

#define SCROLL_REPEAT_RATE	10			//scroll ops per second



#include <atomic>
#include <chrono>   //for stylusAtRest's own rest-detection timer - transitively available already
                    //via DeviceManager.h, added explicitly per this codebase's own established
                    //"don't assume transitive" precedent (see WindowScanner.cpp's own <oleauto.h>).

inline ISpVoice* pSapiVoice = nullptr;

//A second, independent SAPI voice dedicated to the AI assistant's own narration (setup
//progress, the two-detent confirm/cancel prompt, and hold-to-talk replies) - kept entirely
//separate from pSapiVoice (FFUI's own window-focus/UI narration) so the two can play
//concurrently rather than one's SPF_PURGEBEFORESPEAK cutting the other off mid-sentence. See
//FFUIDesktop::initDesktop() for where this is created and (best-effort) given a visibly
//different installed voice than pSapiVoice's, and VoiceAssistant::assistantHasFloor() for how
//pSapiVoice gets ducked to 50% volume (never silenced or paused) while this one is likely to be
//speaking.
inline ISpVoice* pAssistantVoice = nullptr;

//New this round - "when an option is selected, read a confirmation 'Confirmed, Launching
//<program name> now'": StartMenuFlow::showResults()'s per-result onSelect speaks that
//confirmation immediately, then calls MenuSystem::closeAll(). FFUIDesktop::updateFrame() already
//re-triggers the room's own "moved to a new slot" narration (WindowManager::narrateWindowFocus())
//the moment it notices a menu just closed - normally correct (see that block's own comment), but
//here it would land within a frame or two of the confirmation just spoken and, via
//SPF_PURGEBEFORESPEAK, cut it off almost before it starts. Set right after speaking the
//confirmation, consumed (and cleared) by that one block the next time it fires, to skip the
//re-narration exactly once - same "give it a suppress flag" pattern already used for the
//analogous "Dropped in slot N" duplicate-narration bug (WindowWallObject.cpp's own
//narrateGrabResult). Plain bool, not atomic - both the write (StartMenuFlow's onSelect) and the
//read/clear (updateFrame()'s menu-closed block) happen on the haptic thread only; onSelect runs
//synchronously from MenuSystem::selectHighlighted(), itself only ever called from updateFrame().
inline bool suppressNextMenuCloseRenarration = false;

//A third, independent SAPI voice dedicated to the Narrate feature (a standalone AUX-button tap -
//see NARRATE_SPEC.md). Split off from pSapiVoice/pAssistantVoice for exactly the reason
//pAssistantVoice was originally split from pSapiVoice: SPVPRI_OVER (set in initDesktop(), same as
//the other two) means this voice always speaks immediately over whatever the other two are
//currently doing, with zero coordination logic needed against either one - satisfies "Narrate
//always speaks immediately [over the AI assistant]" and "easily separable from the FFUI narrator"
//from the design discussion for free, the same way pAssistantVoice's own SPVPRI_OVER already
//does for the assistant against pSapiVoice. See NarrateFlow.h for what actually speaks through
//this.
inline ISpVoice* pNarrateVoice = nullptr;

//Central chokepoint for every FFUI-narrator utterance - see
//TutorialFlow::shouldMuteFfuiNarrator()'s own comment for why a bare pSapiVoice->Speak() call is
//no longer trusted to respect Stage 1/2 muting on its own (a SetVolume(0)-based approach was
//tried first and reported as still audible on hardware). Every pSapiVoice->Speak(...) call site
//across WindowWallObject.cpp/MenuSystem.cpp/StartMenuFlow.cpp now goes through this instead of
//calling pSapiVoice->Speak() directly - a muted utterance is simply never handed to SAPI, so
//there's no SAPI-internal timing left to get wrong. Every existing call site used the same flags
//(SPF_ASYNC | SPF_PURGEBEFORESPEAK), so this doesn't bother taking a flags parameter; a call site
//that genuinely needs something different can still call pSapiVoice->Speak() directly. Safe to
//call from any thread - shouldMuteFfuiNarrator() reads TutorialFlow's now-atomic `stage`.
inline void SpeakFfuiNarration(const wchar_t* text) {
	if (!pSapiVoice) return;
	if (TutorialFlow::getInstance().shouldMuteFfuiNarrator()) return;
	pSapiVoice->Speak(text, SPF_ASYNC | SPF_PURGEBEFORESPEAK, NULL);
}


inline std::atomic<bool> focusRequested{ false };
inline Vector2 focusPixelTarget{ 0, 0 };
inline std::string lastFocusedObjectName = ""; // To prevent NVDA from stuttering


struct SnapAnchor {
	bool isTracking = false;
	std::string objectWindowsName;
	Vector3 originalPosition = Vector3(0, 0, 0);
};


typedef struct {
	int			targetHapticFramerate;
	int			targetUIFramerate;
	Vector2		screenSize;

	float		cursorFilter;
}FFUIDesktop_Config;


typedef struct {
public:
	std::vector<std::unique_ptr<FFUIObject>> objects;
private:
	

}FFUIDesktop_Layer;


class FFUIDesktop {
public:
	static SnapAnchor currentSnapAnchor;

	void		initDesktop(FFUIDesktop_Config config);
	void		updateFrame();
	void		moveWindowsCursor(Vector2 targetPos);

	//Interactive forces according to object type - the ordinary per-object UI-force pass (every
	//scanned Button/ListItem/MenuItem/Window object, plus the workspace's own boundary walls).
	//suppressButtonDetents, new this round, drops just the ButtonObject-shaped contribution to
	//the total (a scanned Button/ListItem/MenuItem's own pull-to-center detent) while still
	//calling updateForces() on every object as normal, so stylusOnThis/hit-test state stays live
	//either way - used by updateFrame()'s own branch while the real OS cursor is over actual text
	//(see CursorStateHaptics::isCursorOverTextField()'s own comment for why: Word apparently
	//exposes each paragraph as one of these scanned types, so its own whole-paragraph
	//pull-to-center detent was fighting CursorStateHaptics' new per-line one one-for-one -
	//"each paragraph has a detent... can we adjust so each line has its own detent").
	//WindowWallObject's own room-wall force, GravityWellObject, GridTileObject, and every
	//boundary plane are all unaffected either way - only a ButtonObject's own contribution is
	//ever dropped.
	Vector3		processForces(Location stylusLocation, bool suppressButtonDetents = false);

	//Boundary-plane forces only (skips UI-object pull) - used to layer the workspace's own
	//solid-wall "pop-through" resistance underneath the grab/drag detent. See its own comment.
	Vector3		processBoundaryForces(Location stylusLocation);

	void initiatePeriodicScanner(std::stop_token stoken, FFUIDesktop_Config config);


	std::jthread scannerThread;

	//REVERTED - "lets revert your previous changes to how the scanner thread was operating - we
	//did not encounter this issue before, so must be something to do with those additions". This
	//whole round (dropping the scanner's ambient rate and adding this button-triggered manual
	//rescan; the persistent WindowScanner instance and taskbar-scan throttle in
	//initiatePeriodicScanner(), FFUIDesktop.cpp; and the SetThreadPriority tuning added afterward
	//while chasing the resulting stutter) is undone. None of the narrower diagnostics run along
	//the way (disabling the scanner thread entirely, then just its manual-trigger path, then just
	//CursorStateHaptics's own UI Automation calls, then raising/lowering thread priorities)
	//actually fixed the reported "briefly freeze every second or so" except the first, blunt
	//"disable the whole scanner thread" one - meaning the true cause is still unidentified, and
	//everything tried in between only added complexity without resolving it. Back to the simple,
	//previously-working shape: initiatePeriodicScanner() scans at a steady config.targetUIFramerate
	//with no early wake, no per-cycle taskbar throttle, and a fresh WindowScanner constructed each
	//cycle - see that function's own comment for specifics.
	//
	//TEMPORARILY DISABLED for hardware testing - "could you bypass those changes for a moment, so
	//I can test it without that feature active", per the request: Gareth pushed back on the
	//bug-fix comment below, on the grounds that real encoder noise should make an EXACT position
	//freeze during active use excessively unlikely, meaning the reported freeze is probably NOT
	//this feature after all - bypassing it entirely (rather than just re-tuning the fix again) is
	//the fastest way to confirm that on real hardware before chasing this further. Gates the ONE
	//place stylusAtRest is ever set true (updateFrame(), below) - with it permanently false, every
	//downstream consumer (updateFrame()'s own early-out, initiatePeriodicScanner()'s) simply never
	//triggers, so nothing else needs touching to fully restore old behavior. Flip back to true
	//once the real cause is confirmed/fixed, or once this diagnostic rules the feature back in.
	static constexpr bool restPauseFeatureEnabled = false;

	//stylusAtRest - "the whole program could be paused while the stylus is in the rest
	//position", per the request - true once the device's own raw reported position has sat
	//EXACTLY unchanged
	//(bit-for-bit - "the 9D stylus pauses its position updates while in the rest position...
	//check for no position changes exactly, no need for a threshold", per the request; the
	//firmware itself freezes the reported value while genuinely at rest, so an exact compare is
	//both simpler and more reliable than guessing at a movement-noise threshold) for longer than
	//restDetectionThreshold. Set/cleared only from updateFrame() (the haptic thread, where the
	//device position is actually read) but READ from both threads: updateFrame() itself skips
	//essentially all of its own per-frame work while true (see that method's own comment), and
	//initiatePeriodicScanner() independently skips its scan work on the scanner thread the same
	//way - see that method's own comment. Atomic for that cross-thread read, same shape as
	//WindowManager::isUserGrabbingWindow's own cross-thread signal elsewhere in this codebase.
	//Clears the INSTANT the position changes again - see updateFrame()'s own comment for why no
	//separate "wake" signal is needed.
	std::atomic<bool> stylusAtRest{ false };

	//The pair updateFrame() compares this frame's raw device position against, every frame, to
	//drive stylusAtRest above - see that flag's own comment. Plain (non-atomic): only ever
	//touched from the haptic thread, inside updateFrame() itself.
	bool haveLastDevicePosition = false;
	Vector3 lastDevicePosition;
	std::chrono::steady_clock::time_point lastDevicePositionChangeTime;

	//How long the raw position has to sit exactly still before the stylus counts as "at rest" -
	//see stylusAtRest's own comment. 1 second per the request.
	static constexpr std::chrono::seconds restDetectionThreshold{ 1 };

	//BUG FIX, new this round ("getting quite some lag when in the program slot... briefly freeze
	//every second or so"): position sitting exactly still is NOT, on its own, a safe signal for
	//stylusAtRest - a detent (a scanned button/list item, and especially the new, deliberately
	//tighter per-line Y detent) genuinely holds the physical tip motionless under real,
	//actively-rendered force for well over a second during completely normal browsing (hovering a
	//button, sitting on a text line while reading), which looks IDENTICAL to genuine rest by
	//position alone - that was getting misclassified as "at rest" mid-use, silently zeroing the
	//force and skipping this device's entire per-frame body (including button handling) until
	//the position finally shifted, which is exactly the periodic freeze reported. stylusAtRest
	//now additionally requires the X/Y magnitude of the LAST actually-rendered force
	//(lastAppliedForceMagnitude, updated right where force is finalized in updateFrame()) to be
	//at or below this, so stillness caused by an active pull no longer counts as rest - only
	//stillness with nothing pulling on it at all does.
	//
	//Known residual gap: every detent has a small deadzone (a flat, zero-force zone right at its
	//own center - see ButtonObject::calculateInteractionForce()/CursorStateHaptics' own
	//lineDetentDeadzoneFraction* comments) so force can genuinely read ~0 while still resting
	//dead-center on a target, not idle - if brief freezes are still noticeable after this fix,
	//that deadzone-centered case is the next thing to address (most likely by also checking
	//whether the stylus is currently registered as hovering any object at all, not just the
	//force magnitude).
	float lastAppliedForceMagnitude = 0.0f;
	static constexpr float restForceEpsilon = 0.0002f;

	//Hold-to-talk AI voice assistant on the AUX button - see VoiceAssistant.h. Started from
	//initDesktop() once pSapiVoice/pAssistantVoice exist; fed this frame's AUX state every
	//haptic frame from updateFrame().
	VoiceAssistant voiceAssistant;

	//Reads the real OS cursor's current shape (text field, clickable, resize handle,
	//unresponsive) and turns it into a distinct little vibration, on top of whatever normal
	//force is already being rendered - see CursorStateHaptics.h's own comment. One persistent
	//instance (not a per-frame local) so each recognized state's HapticVibration keeps its own
	//timing across frames, the same reason voiceAssistant above is a member rather than a
	//local. Polled from updateFrame() only while WindowManager::isControllingActiveRoom is
	//true - see that call site's own comment.
	CursorStateHaptics cursorStateHaptics;

	std::vector<FFUIDesktop_Layer> layers;
	DeviceManager deviceManager;

	std::mutex objectsListMutex;

	std::vector<std::unique_ptr<FFUIObject>> objects;
	static inline FFUIDesktop_Config	desktopConfig;

private:
	//bool button3Clicked = false;

	void addBoundaryPlanes(std::vector<std::unique_ptr<FFUIObject>>& targetList);

	//Creates the 2 static FFUI Settings tiles ("FFUI narrator settings", "AI narrator
	//settings") confined to the left half (X<0) of the shared front zone, every scan cycle -
	//same "recreated fresh every cycle" spirit as addBoundaryPlanes() above, since these are
	//synthetic (not scanned) and have no persistent identity of their own to preserve across
	//cycles. outTiles collects raw, non-owning pointers to the instances just appended to
	//targetList, for the caller to hand to WindowManager::SettingsTiles inside the same locked
	//section ActiveWindows/ArchivedWindows are refreshed in (see initiatePeriodicScanner()) -
	//mirrors the tempActiveWindows/tempArchivedWindows output-param pattern
	//ObjectFactory::createObjectsFromUIElements() already uses.
	void addFrontZoneSettingsTiles(std::vector<std::unique_ptr<FFUIObject>>& targetList, std::vector<GridTileObject*>& outTiles);

	//Select-button press while hovering an FFUI Settings tile (checked in updateFrame(), in the
	//normal/non-grab/non-menu branch) opens that tile's menu - tileIndex 0 = "FFUI narrator
	//settings" (pSapiVoice), 1 = "AI narrator settings" (pAssistantVoice). Builds and pushes the
	//menu via MenuSystem::createMenu() - see MenuSystem.h.
	static void openSettingsTileMenu(int tileIndex);

	//Builds the "voice style" submenu - one Action item per SAPI voice currently installed
	//(via VoiceSettingsManager::enumerateVoices()), plus Back. Selecting a voice applies +
	//persists it, then returns to the parent menu.
	static void openVoiceStyleMenu(bool isFfuiVoice);

	//Builds the "voice speed" submenu - a single Slider item (SAPI rate, -10..10) plus Back,
	//per the request's "top item is a horizontal slider, bottom is back".
	static void openVoiceSpeedMenu(bool isFfuiVoice);

	//Builds the "Quick slots settings" submenu - a single Slider item (quickslot count,
	//QuickSlotsSettingsManager::MIN_SLOT_COUNT..MAX_SLOT_COUNT) plus Back, mirroring
	//openVoiceSpeedMenu()'s own self-contained-rounding pattern (MenuSystem itself has no
	//built-in integer snapping - see MenuItemDef's own comment). "add a user setting (a
	//horizontal scroll in the FFUI settings) for how many quickslots slots they want", per the
	//request.
	static void openQuickSlotsMenu();

	//Live (not last-frame-cached) hit test against the 2 FFUI Settings tiles - calls
	//updateForces() on each fresh, right now, against the CURRENT stylus position, rather than
	//relying on whatever stylusIsOnThis() last happened to be set to by the main per-frame
	//processForces() pass. Used at the exact moment of a button press, so opening a menu always
	//reflects exactly where the stylus is on THIS frame - "clicking doesn't always trigger the
	//correct options" was traced partly to this kind of one-frame staleness. Safe to call this
	//way for GridTileObject specifically (unlike WindowWallObject) because it has no side
	//effects beyond updating its own stylusOnThis flag - no narration, no bring-to-front, so
	//calling it an extra time per frame changes nothing observable besides the answer itself.
	GridTileObject* liveHitTestSettingsTile(Location stylusLoc);

	bool stylusSnapped = false;

	Vector2 cursorPos;
	Vector3 calculateForceToClosestObject(Location deviceLoc,  bool buttonClicked);

	Vector2 cusrsorScale;		//the scale factor between device workspace and digital workspace

	//True if the stylus is currently over a window in the archived "window select" list (as
	//opposed to one of the active slots, or empty space). Checks live WindowWallObjects
	//normally, or their GravityWellObject stand-ins while a grab/drag is in progress (see
	//WindowManager::isUserGrabbingWindow) - the object type in play differs, but "which window
	//is currently under the stylus" is resolved the same way either case. Used to suspend
	//normal mouse forwarding and repurpose the select button for grab/drop while browsing this
	//list - see updateFrame()'s front-button handling.
	bool isStylusInWindowSelectLayer();

	//Which top-level window handles have already been seen (and, if needed, already relocated)
	//by moveNewWindowsToPrimaryMonitor() below, as of the last scan cycle - so a window that's
	//deliberately moved to a second monitor after it opens isn't fought on every subsequent
	//scan, only handled once, right when it first appears. Pruned to currently-open windows
	//each cycle inside that function, so it can't grow unbounded across a long session.
	std::unordered_set<HWND> knownWindowHandles;

	//Multi-monitor accessibility fix: the haptic device's workspace (DEVICE_WORKSPACE_X/Y,
	//cusrsorScale above) is calibrated against the primary monitor only, so a window that opens
	//on a secondary monitor is effectively unreachable by touch for a user relying on the
	//device. Called once per scan cycle from initiatePeriodicScanner() with that cycle's full
	//list of open top-level window handles; moves any window seen here for the first time (per
	//knownWindowHandles above) off a non-primary monitor and onto the primary one, preserving
	//its relative position and size. See the .cpp for the full reasoning and known edge cases.
	void moveNewWindowsToPrimaryMonitor(const std::vector<HWND>& currentOpenHwnds);
};