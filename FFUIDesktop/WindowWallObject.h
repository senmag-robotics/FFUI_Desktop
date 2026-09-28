#pragma once
#include "Windows.h"
#include <UIAutomation.h>
#include "WindowScanner.h"
#include "FFUIDesktop_object.h"
#include "GridTileObject.h"
#include <thread>
#include <mutex>
#include <unordered_map>
#include <string>

#include <atomic>

struct WindowWallMeta {
    HWND windowHandle;
    std::wstring windowTitle;
    bool isArchived = false;
    bool isGrabbed = false;
    bool isFocused = false;
    bool stylusOnThis = false;
};



class WindowWallObject : public FFUIObject {


public:
    WindowWallObject(WindowWallMeta meta, Vector3 position, float thickness, float stiffness, float solidForceLimit, float height, float width);

    const WindowWallMeta& getWindowName() const { return windowMeta; }
    void setGrabbed(bool state) { windowMeta.isGrabbed = state; }
    Vector3 calculateInteractionForce(Location localLoc) override;
   
    WindowWallMeta getWindowMeta() {
        return windowMeta;

    }

    void setArchivedState(bool state) {
        windowMeta.isArchived = state;
    }
    bool isArchived() {
        return windowMeta.isArchived;
    }

    void setFocused(bool state) {
        windowMeta.isFocused = state;
    }
    bool isFocused() {
        return windowMeta.isFocused;
    }

    bool stylusIsOnThis() {
        return windowMeta.stylusOnThis;
    }
    HWND getHandle() {
        return windowMeta.windowHandle;
    }


private:
    WindowWallMeta windowMeta;


};



class WindowManager {
private:
    //Since conctructor is private, no once can create instances of this class(as needed)
    WindowManager() {}

public:

    static WindowManager& getInstance() {
        static WindowManager instance; 
        return instance;
    }
    // The raw Win32 "steal foreground" sequence (AttachThreadInput -> maximize/restore -> two
    // SetWindowPos calls -> SetForegroundWindow -> SetFocus -> SetActiveWindow -> AttachThreadInput
    // detach), working from a plain HWND rather than requiring a live WindowWallObject - needed by
    // cycleActiveSlot()/loadSlot() below (the newly-selected slot's WindowWallObject doesn't exist
    // yet the exact frame the scroll-wheel click happens, only constructed on the NEXT scan cycle,
    // but the OS-level maximise+foreground switch doesn't need to wait for that - it only needs the
    // window's real handle, which windowWallSlotsMap already has immediately) AND by
    // FFUIDesktop::updateFrame()'s own top-level room-crossing logic (see its own comment) - both
    // callers want exactly this raw mechanism with no extra per-object bookkeeping layered on top.
    //
    // A per-object wrapper (bringWindowToFront(WindowWallObject*)) and a debounced trigger
    // (requestFocusSwitch(), with its own FOCUS_SWITCH_HYSTERESIS_FRAMES) used to sit in front of
    // this, invoked from WindowWallObject::calculateInteractionForce()'s wall/pop-through branch.
    // Removed entirely, per the request: "there's no need for this to be attached to an object at
    // all - as top level behaviour this can be handled in the main program loop" - console
    // mirroring added for diagnosing the previous round's report kept showing narration/focus
    // firing from stale per-object state (isFocused() persisting across scan cycles, geometry
    // gated on the wrong local axes given this object's own 90-degree rotation) regardless of how
    // many different signals it was wired to. The debounce concern that motivated
    // FOCUS_SWITCH_HYSTERESIS_FRAMES (AttachThreadInput/SetForegroundWindow firing many times a
    // second on position noise) is now handled by the new top-level logic's own margin/hysteresis
    // band on plain world Z instead - see FFUIDesktop::updateFrame()'s comment for the replacement.
    static void bringWindowToFrontByHandle(HWND hwnd);

    // Top-level, object-independent state for "is the currently active slot's window in control
    // of the OS mouse right now" - i.e. has the stylus crossed, with hysteresis, from the shared
    // front zone (Program Tray/FFUI Settings) into the merged active room, past its wall. Owned
    // and updated entirely by FFUIDesktop::updateFrame() from a plain world-Z comparison against
    // the room's own fixed wall-center Z - no per-object local-frame geometry, no rotation/axis-
    // mapping ambiguity. Read by updateFrame() itself to gate mouseSuspended. Defaults to false
    // (front-zone side) on startup, matching every other piece of in-memory-only session state.
    bool isControllingActiveRoom = false;

    static void moveArchviedToActive(WindowWallObject* targetWindow);

    void swapWindowSlots(HWND grabbedWindow, HWND targetWindow);

    static HWND getHandleOfTheWindowTheStylusIsOn(std::mutex& objectsListMutex);

    static HWND getHandleOfTheWindowTheStylusIsOn(std::mutex& objectsListMutex, const std::vector<std::unique_ptr<FFUIObject>>& objectsList);

    static void removeClosedWindows(const std::vector<HWND>& currentlyOpenWindows);

    // Narration format: "Slot N, {app name}" the first time the stylus lands on a window in
    // a different "layer" (each active slot is its own layer; the whole archived list counts
    // as one shared layer), or just "{app name}" when moving to a different window within the
    // SAME layer (e.g. browsing between archived entries). Dedup/reset is tracked by HWND
    // rather than by name, so callers don't need to worry about string mismatches.
    void narrateWindowFocus(HWND hwnd);
    void resetNarratedWindow(HWND hwnd);

    // Derives a short, speakable app name (e.g. "Firefox") from the process that owns hwnd,
    // by resolving its executable's file name and stripping the ".exe" extension. Falls back
    // to the raw window title if the owning process can't be queried (e.g. an elevated app).
    static std::wstring getShortAppName(HWND hwnd);

    // Looks up hwnd's assigned physical slot (0-based), or -1 if it has none yet.
    int getSlotForWindow(HWND hwnd);

    // Reverse lookup of getSlotForWindow: which window (if any) currently occupies a given
    // physical slot. Used to resolve classifyGrabZone()'s slot index back into the HWND that
    // swapWindowSlots() needs on a successful drop. windowWallSlotsMap persists unchanged
    // through a grab (only the ActiveWindows/ArchivedWindows vectors get cleared for the
    // placeholder swap), so this stays valid the whole time.
    HWND getWindowHandleInSlot(int slot);

    // True if hwnd's slot puts it in the archived "window select" list rather than one of the
    // active slots. A window with no assigned slot yet (-1) is conservatively treated as NOT
    // archived, so callers default to normal (non-suspended) mouse behavior.
    bool isArchivedSlot(HWND hwnd) {
        int slot = getSlotForWindow(hwnd);
        return slot >= numOfActiveWindows;
    }

    // Sentinel layer id shared by every window in the archived list ("Program Tray"),
    // regardless of its individual slot number - narration-wise, the whole tray is one layer.
    static constexpr int ARCHIVED_LAYER_ID = -1;

    // Sentinel layer id for the FFUI Settings tiles (left half of the shared front zone) -
    // distinct from ARCHIVED_LAYER_ID (Program Tray, right half), the -2 "nothing narrated yet"
    // sentinel below, and every real active-slot id (>= 0), so moving between any of these
    // layers is always detected as a layer change by the SAME narrateWindowFocus()/
    // narrateSettingsTileFocus() machinery - see the latter's own comment.
    static constexpr int SETTINGS_LAYER_ID = -3;

    HWND lastNarratedWindowHandle = nullptr;
    int lastNarratedLayer = -2; // -2 = nothing narrated yet (distinct from any real layer id)
    HWND lastGrabbedWindowHandle;

    // Hysteresis on "am I still on the currently-narrated window/tile" - small position noise
    // right at a tile/window boundary can make a single frame's raw box test report false even
    // while genuinely still on (or about to return to) the same one, which previously cleared
    // lastNarratedWindowHandle/lastNarratedSettingsTileIndex immediately and re-narrated on the
    // very next frame it read true again - "the layers seem to have a similar issue to
    // earlier - where it can flip back and forth when near the boundary", per the request, the
    // same class of bug the menu-item highlight debounce fixed a round ago. Only actually
    // clears the remembered identity once resetNarratedWindow()/resetNarratedSettingsTile()
    // have been called that many consecutive times in a row for it - any frame that confirms
    // "still on" (or moves straight to a genuinely different window/tile) resets the count.
    // Separate counters because the two narration channels are independent and can be mid-
    // streak at different times.
    int narratedWindowOffStreak = 0;
    int narratedSettingsTileOffStreak = 0;
    static constexpr int NARRATION_HYSTERESIS_FRAMES = 3; //matches MenuSystem's own HIGHLIGHT_DEBOUNCE_FRAMES

    // Grab/drag feedback (console + speech), replacing the old per-frame "inside/done" debug
    // spam with a handful of clean, event-triggered messages. Both channels fire from the same
    // three calls below so console and narration never drift out of sync with each other.
    // Called once when a grab starts (select button pressed while hovering the window-select
    // list) - announces "{app} grabbed" and resets the option-tracking state below.
    void narrateGrabStart(HWND grabbedWindow);

    // Called every frame while a grab is in progress, with the physical slot (0-based) the
    // stylus currently has a gravity-well hold on, or -1 if it's not currently over one of the
    // active slots (anywhere in the window-select list, or nowhere at all - "cancel", per the
    // "cancel is the selector zone" design). Only speaks/prints when the option actually
    // changes since the last call, so this can be called unconditionally every frame.
    void narrateGrabOption(int hoveredSlot);

    // Called once when a grab ends (select button released) - reports whether it landed on an
    // active slot (drop) or anywhere else (cancel, including nothing at all). The console log
    // line is always printed (diagnostic value, unrelated to what follows), but the SPOKEN
    // "Dropped in slot N"/"Drag cancelled" is only actually spoken when speakResult is true.
    // Callers that already spoke a more specific confirmation for this same event this frame -
    // loadSlot()'s own "Slot N, {app name}" narration, fired right after a successful
    // drop's slot swap - pass false here, since SPF_PURGEBEFORESPEAK would otherwise let this
    // call's own speech immediately cut that one off, leaving "Dropped in slot N" as the only
    // thing actually heard instead of the intended "Slot N, {app name}". "instead of the
    // 'dropped in slot n' narrator prompt, ...the normal 'slot n: <program name>'", per the
    // request. Defaults to true so every other existing call site (the cancel path) is
    // unaffected.
    void narrateGrabResult(bool droppedInActiveSlot, int slotIndex, bool speakResult = true);

    // Stashed by narrateGrabStart so narrateGrabOption/narrateGrabResult can refer to "which
    // program" without re-querying a window that may have closed mid-drag.
    std::wstring lastGrabbedAppName;

    // -1 = "cancel" is the implicit starting state of every grab (it always begins in the
    // window-select list), so narrateGrabStart resets to this rather than a separate sentinel -
    // the first real move into a slot is still always announced.
    int lastNarratedGrabOption = -1;

    // --- Detent-based grab/drag feel -------------------------------------------------------
    // Rather than small discrete "gravity well" boxes (which left a gap - and so a spurious
    // extra "cancel" reading - between the two active slots, and felt inconsistent since each
    // box pulled toward its own different point), dragging now feels like a single continuous
    // spring pulling the stylus back toward the X/Y position it was at when the grab started,
    // with force.z always exactly 0 - so the whole Z axis is a free "rail" to slide along
    // between options, and X/Y just gently self-corrects to a straight line along it.

    // The X/Y point (Z ignored) the detent spring pulls back toward - captured once, from the
    // haptic thread, at the exact instant a grab begins.
    Vector3 grabDetentAnchor{ 0, 0, 0 };

    static constexpr float GRAB_DETENT_SPRING_CONSTANT = 0.0006f; // unverified - tune by feel
    static constexpr float GRAB_DETENT_MAX_FORCE = 0.004f;        // unverified - tune by feel

    // Records stylusPosition as this grab's detent anchor. Called once, at the instant a grab
    // begins.
    void beginGrabDetent(Vector3 stylusPosition) {
        grabDetentAnchor = stylusPosition;
    }

    // A spring back toward grabDetentAnchor in X/Y only, clamped to GRAB_DETENT_MAX_FORCE -
    // force.z is always exactly 0 by construction, never just zeroed-out afterward.
    Vector3 computeGrabDetentForce(Vector3 stylusPosition) {
        Vector3 force(
            (grabDetentAnchor.x - stylusPosition.x) * GRAB_DETENT_SPRING_CONSTANT,
            (grabDetentAnchor.y - stylusPosition.y) * GRAB_DETENT_SPRING_CONSTANT,
            0.0f
        );

        if (force.length() > GRAB_DETENT_MAX_FORCE) {
            force = force.normalized() * GRAB_DETENT_MAX_FORCE;
        }

        return force;
    }

    // One option the stylus can be over while sliding along the Z rail: either a specific
    // active slot (0-based), or -1 for "cancel" (the window-select list).
    struct GrabZoneOption {
        int slot;
        float zCenter;
    };

    // Built once per grab (see buildGrabZoneOptions()) from the REAL, just-scanned positions of
    // the active slots and the archived list, sorted ascending by zCenter - so classifyGrabZone()
    // can partition the whole Z axis into contiguous buckets with no gaps and no double-counted
    // zones, unlike the old small-box hit test.
    std::vector<GrabZoneOption> grabZoneOptions;

    // Sticky "currently classified" index into grabZoneOptions, persisted across frames within a
    // single grab - see classifyGrabZone()'s own comment for why this exists (margin-based
    // hysteresis on top of the plain midpoint partition). -1 means "not yet classified this
    // grab" (the very first call snaps directly, since there's nothing to be sticky about yet).
    // Reset by buildGrabZoneOptions(), the one place a new grab's options are (re)built.
    int grabZoneHysteresisIndex = -1;

    // How far PAST a boundary's plain midpoint the stylus has to move, in the direction of
    // travel, before classifyGrabZone() actually commits to the neighbouring option - see that
    // method's own comment. Matches computeGrabZoneWallForce()'s own WALL_HALF_THICKNESS, since
    // that's the felt width of the resistance the user should already be past by the time the
    // option changes.
    static constexpr float GRAB_ZONE_CROSSING_MARGIN = 8.0f;

    // Called from the scanner thread, at the same moment it reads ActiveWindows/ArchivedWindows
    // to build this grab's gravity-well placeholders (see initiatePeriodicScanner()) - the one
    // place their real Z positions are guaranteed still valid, right before those lists get
    // cleared for the duration of the grab.
    void buildGrabZoneOptions();

    // Classifies a Z position into whichever option's bucket it falls in, using the midpoints
    // between adjacent entries in grabZoneOptions as the underlying partition boundaries, PLUS a
    // margin-based hysteresis (GRAB_ZONE_CROSSING_MARGIN) on top: once classified into an option,
    // the stylus has to move past that option's boundary midpoint by the margin - not just reach
    // the midpoint itself - before this returns a neighbouring option. Without this, the narrated
    // option (narrateGrabOption(), called every frame with this method's result) flipped exactly
    // AT the knife-edge midpoint, while computeGrabZoneWallForce()'s felt resistance is spread
    // across a WALL_HALF_THICKNESS-wide band straddling that same midpoint - so the narrator
    // would announce the new option before the user had actually pushed through (and past) the
    // wall's resistance, i.e. "I can hear the narrator output before I cross the haptic
    // boundary". Using the same width for both margins means the announcement now lands right
    // around when the felt resistance has been pushed through, per the request's own suggested
    // fix ("options should only be selected once the stylus has crossed within an object by a
    // certain margin"). Returns -1 (cancel) if grabZoneOptions hasn't been built yet (e.g. the
    // first frame or two of a grab, before the scanner thread's next tick) - a safe default,
    // since "cancel" performs no swap.
    int classifyGrabZone(float currentZ);

    // A "pop-through" resistance felt as the stylus crosses each boundary between adjacent
    // grabZoneOptions entries (the same boundaries classifyGrabZone() partitions by) - "we
    // still need to re-introduce the layer pop-throughs (between slot1/slot2 etc) when
    // dragging a program from the front", per the request. See the .cpp definition's own
    // comment for why this is needed at all (the real WindowWallObjects, which normally
    // produce this feel, are swapped out for GravityWellObject placeholders for the duration
    // of a grab). Added to (not replacing) computeGrabDetentForce()'s own force at the
    // FFUIDesktop.cpp call site - the detent still provides the main X/Y spring and free Z
    // rail; this only adds the periodic crossing resistance on top.
    Vector3 computeGrabZoneWallForce(float currentZ);

    void increasenNextAvailableSlot() {
        nextAvailableSlot++;
    }

    // --- FFUI Settings / Program Tray shared front zone -------------------------------------
    // The Z-band both sections (and every MenuSystem menu, which spans both) sit in - set once
    // per scan cycle by ObjectFactory::computeFrontZoneBand() (see ObjectsFactory.cpp), so it
    // stays correct even if numOfActiveWindows/roomDepth ever change, without duplicating that
    // arithmetic here.
    float frontZoneZCenter = 0.0f;
    float frontZoneZHalfThickness = 0.0f;

    // The FFUI Settings tiles (left half, X<0, of the shared front zone) - raw, non-owning
    // pointers into whichever GridTileObject instances currently live in layers[0].objects,
    // refreshed every scan cycle at the same time as ActiveWindows/ArchivedWindows (see
    // FFUIDesktop::initiatePeriodicScanner()), exactly mirroring how those two lists are
    // already owned/refreshed.
    std::vector<GridTileObject*> SettingsTiles;

    // Keyed by tile index rather than HWND (no window exists for these) - mirrors
    // lastNarratedWindowHandle's dedup role, just for the Settings side.
    int lastNarratedSettingsTileIndex = -1;

    // Narrates a FFUI Settings tile the same way narrateWindowFocus() narrates a window -
    // "FFUI settings, {label}" the first time the stylus lands on a tile in a different layer
    // (i.e. coming from Program Tray, an active slot, or nothing), or just "{label}" if it's
    // already the settings layer (e.g. moving between the two settings tiles).
    void narrateSettingsTileFocus(int tileIndex, const std::wstring& label);
    void resetNarratedSettingsTile();

    // The single, centralized decision for "what window (if any) should be narrated this tick" -
    // covers both the merged active room and the Program Tray (archived windows), reading each
    // object's own stylusOnThis hit-test result (already computed by its own
    // calculateInteractionForce() call THIS tick) rather than having every object decide to
    // narrate itself independently. See the .cpp definition's own comment for the full
    // rationale - "one central decision-making process that informs both the haptics & narrator",
    // per the request. Must be called exactly once per haptic tick, from FFUIDesktop's normal
    // (non-grab, non-menu) force branch, AFTER processForces() has run every object's
    // updateForces() for this tick.
    void updateWindowNarration();

    // Called every haptic frame from FFUIDesktop's normal (non-grab, non-menu) force branch,
    // alongside updateWindowNarration() above (that one covers windows/Program Tray; this one
    // covers FFUI Settings tiles - see its own .cpp comment for why they're separate functions
    // rather than one merged pass). Resolves whether the stylus is currently within the front
    // zone's Z-band and, if so, narrates whichever FFUI Settings tile (if any) it's on. Crossing
    // between Program Tray/FFUI Settings/any active slot is announced automatically as a side
    // effect of the shared lastNarratedLayer mechanism detecting a layer-id mismatch - no
    // separate "wall crossed" event is needed.
    void updateFrontZoneSectionNarration(Location stylusLoc);

    // A light, constant pull toward the single nearest tile within whichever section (Program
    // Tray or FFUI Settings) the stylus is currently in, active only while nothing in that
    // section is already highlighted (its own per-tile detent takes over once something is) -
    // "there should always be a light force pulling the user towards the nearest item while
    // nothing is highlighted", per the request. Returns zero outside the front zone's Z-band.
    Vector3 computeFrontZoneMagnetism(Location stylusLoc);

    // A vertical "wall" felt at world X=0 within the front zone's Z-band, separating Program
    // Tray (X>=0) from FFUI Settings (X<0) - computed directly against stylusLoc.position.x
    // rather than via a SolidPlane/local-frame object; see this method's own .cpp comment for
    // why. Repels from both sides (unlike SolidPlane's one-sided check), since a divider needs
    // to push back whichever way the stylus already leans, not gate on a single global sign.
    Vector3 computeSectionDividerForce(Location stylusLoc);

    // Called by FFUIDesktop right after a grab or a MenuSystem menu closes (isGrabbingWindow or
    // MenuSystem::isActive() transitioning true->false), so whichever slot/tile the stylus is
    // now resting on gets freshly (re-)announced instead of staying silent because it "was
    // already the last thing narrated" from before the grab/menu started - "the narrator should
    // read out the current slot location" when a menu closes, per the request.
    void forceRenarrateOnNextFocus() {
        lastNarratedWindowHandle = nullptr;
        lastNarratedSettingsTileIndex = -1;
        lastNarratedLayer = -2;
    }


    std::vector<WindowWallObject*> ArchivedWindows;

    std::vector<WindowWallObject*> ActiveWindows;

    // std::recursive_mutex, not a plain std::mutex - getSlotForWindow()/getWindowHandleInSlot()
    // (see their own comments in WindowWallObject.cpp) now take this lock internally so every
    // caller is protected automatically, but several existing callers (updateWindowNarration(),
    // FFUIDesktop::updateFrame()'s own room-crossing logic) already hold this same lock
    // themselves around a call into narrateWindowFocus(), which calls getSlotForWindow()
    // internally - a plain std::mutex would deadlock the very first time that happened, since
    // it isn't safe for one thread to lock it twice. A recursive_mutex costs nothing extra for
    // every OTHER call site here (they still only ever lock it once), and removes the need to
    // audit every current and future call site for "does something further down this call chain
    // already hold this lock" by hand.
    std::recursive_mutex windowMutex;

    // numOfActiveWindows now means two DIFFERENT things it used to conflate: the boundary Pass 1
    // (ObjectsFactory.cpp) uses to decide active-vs-archived, AND how many distinct lanes the
    // grab/drag Z-rail (buildGrabZoneOptions/classifyGrabZone) understands - i.e. the user's own
    // "how many quickslots" setting (see QuickSlotsSettingsManager). It NO LONGER decides how
    // much physical Z-room space is used for browsing - "I'm thinking this will become
    // confusing if we start adding more 3D features - we should focus on displaying one thing
    // well, rather than dividing our workspace arbitrarily", per the request. That physical
    // space is now fixed by ACTIVE_ZONE_ROOM_COUNT below, independent of this value.
    int numOfActiveWindows = 2;

    // The FIXED number of physical Z-rooms worth of space the single merged active-slot room
    // occupies - "the main layout will just be one program slot (filling the space of the
    // current 2 slots)", per the request, literally frozen at today's 2-slot depth regardless of
    // numOfActiveWindows/the user's quickslot-count setting. Used by
    // ObjectFactory::computeFrontZoneBand() (pins the front zone/Program Tray/FFUI Settings at
    // exactly today's Z position) and ObjectFactory::createObjectsFromUIElements() (pins the one
    // rendered active room's own Z position), and the approach-zone threshold in
    // WindowWallObject::calculateInteractionForce().
    static constexpr int ACTIVE_ZONE_ROOM_COUNT = 2;

    // Which one of the numOfActiveWindows logical quickslots is currently shown as the live,
    // full-size room - the others exist only as entries in windowWallSlotsMap below (still
    // fully valid drag/drop targets, just not haptically rendered until selected). Defaults to
    // slot 0 on startup, like every other piece of in-memory-only session state in this app -
    // nothing about slot selection is persisted across restarts. Changed only by
    // cycleActiveSlot() (scroll-wheel click) and clamped by setActiveSlotCount() if the user
    // lowers the quickslot count below it.
    int currentActiveSlotIndex = 0;

    // Lets a scroll-wheel click's focus request survive until the scanner thread's NEXT cycle
    // actually constructs the newly-selected slot's WindowWallObject. bringWindowToFrontByHandle
    // itself is called immediately, synchronously, from cycleActiveSlot() - this is purely so
    // FFUIDesktop::initiatePeriodicScanner()'s focus-reapply block (which normally just carries
    // forward whichever OLD object had isFocused()==true) knows to mark the NEW object focused
    // instead, the moment it exists, rather than briefly reverting to nothing/the old window.
    // Consumed (reset to false) the first time that block reads it - see its own comment.
    bool hasPendingExplicitFocus = false;
    HWND pendingExplicitFocusHandle = NULL;

    // The single, canonical way to make a slot the one live, rendered active room - "a
    // loadSlot(n) function, that checks if the slot is set (if not moves to the next one),
    // maximises the program & reads the narrator prompt", per the request. Replaces the
    // previous activateSlot(int, HWND) split, where callers looked up (or otherwise obtained) an
    // HWND themselves and handed it in alongside the slot index - two pieces of state that could
    // drift out of sync with windowWallSlotsMap's own current answer (e.g. a caller's hwnd
    // becoming stale between when it was looked up and when this ran), which was a real
    // contributor to the reported slot-switching bugs. loadSlot() instead ALWAYS re-derives the
    // HWND itself, fresh, from windowWallSlotsMap at the moment it runs, and NEVER trusts a
    // caller-supplied one - there is now exactly one source of truth for "what's in slot n".
    //
    // If slotIndex itself is currently empty, searches forward (wrapping around at most once)
    // for the next occupied slot instead, so cycling always lands on a real window when at least
    // one quickslot is occupied. If EVERY quickslot is empty, this is a no-op - currentActiveSlotIndex
    // and whatever's currently shown are left untouched rather than narrating "empty" for no
    // reason.
    //
    // Once a slot to load is settled on: triggers the real OS maximise+foreground switch
    // (bringWindowToFrontByHandle), updates currentActiveSlotIndex, stashes
    // hasPendingExplicitFocus/pendingExplicitFocusHandle for the scanner thread's next cycle, and
    // narrates the switch (narrateSlotSwitch) - all synchronous, not waiting on the next scan
    // cycle. Used by cycleActiveSlot() (scroll-wheel click) below, the grab/drag drop handler in
    // FFUIDesktop.cpp's updateFrame(), and StartMenuFlow's post-launch activation - every one of
    // these used to build its own slotIndex/HWND pair and hand it to activateSlot(); now each
    // just names the slot (already updated in windowWallSlotsMap by the time this runs) and lets
    // this resolve the rest.
    //
    // announcementPrefix (new this round - "can the narrator confirm when [a Start-Menu-launched
    // program has] successfully started?") is spoken immediately before narrateSlotSwitch()'s own
    // "Slot N, appName" text, as ONE combined utterance, rather than as a separate Speak() call
    // of its own - a second, separate SpeakFfuiNarration() moments apart would just get cut off
    // by (or itself cut off) narrateSlotSwitch()'s own SPF_PURGEBEFORESPEAK call, the same
    // clipping failure mode the "Dropped in slot N" duplicate-narration bug hit before. Empty by
    // default (every other caller's behavior is unchanged) - see narrateSlotSwitch()'s own
    // comment for exactly how it's combined.
    void loadSlot(int slotIndex, const std::wstring& announcementPrefix = L"");

    // Cycles to the next quickslot (wrapping) when the stylus's scroll-wheel-click button is
    // pressed while browsing the merged active room (not in FFUI Settings/Program Tray, not
    // grabbing, no menu open - see FFUIDesktop::updateFrame()'s own gating) - "clicking the
    // scroll wheel will cycle between program slots... this behaviour should be paused while in
    // the FFUI settings / app drawer", per the request. A thin, semantically-named wrapper around
    // loadSlot() above - loadSlot() itself already handles the empty-slot-skip and wraparound.
    void cycleActiveSlot();

    // Speaks "Slot X, {app name}" (1-based X, matching narrateGrabOption/narrateGrabResult's own
    // "slot N" convention) or "Slot X, empty" if nothing is currently pinned to that slot - "the
    // narrator reading out 'Slot X, <program name>'", per the request. Also updates
    // lastNarratedWindowHandle/lastNarratedLayer the same way narrateWindowFocus() does, so the
    // normal per-frame narration path doesn't immediately re-announce the same thing once the
    // new slot's live WindowWallObject appears on the next scan.
    //
    // announcementPrefix (see loadSlot()'s own comment) is prepended verbatim onto the single
    // spoken string, before "Slot X, ..." - callers own their own trailing space/punctuation.
    void narrateSlotSwitch(int slotIndex, HWND hwnd, const std::wstring& announcementPrefix = L"");

    // Applies a new user-configured quickslot count (see QuickSlotsSettingsManager) - updates
    // numOfActiveWindows and, if currentActiveSlotIndex is now out of range, clamps it to the
    // new highest valid slot (count - 1) rather than resetting to 0, so lowering the count never
    // silently jumps the user to a different, unrelated window when their current one is still
    // in range.
    void setActiveSlotCount(int count);

    // Assigns hwnd to a specific physical slot - used by StartMenuFlow once a freshly-launched
    // program's window has been detected (see StartMenuFlow.h), to land it directly in
    // "whichever slot was used last" rather than wherever ObjectFactory's Pass 1 would otherwise
    // auto-assign a never-before-seen window (the back of the archived tray). Takes windowMutex
    // for the whole operation, matching swapWindowSlots()'s own locking - unlike that method,
    // this doesn't need a two-way swap (the newly-launched window has no prior slot to swap
    // into): if a DIFFERENT hwnd already occupies slot, that other window is simply reassigned
    // to a fresh nextAvailableSlot (bumped to the back of the tray) instead. Safe to call from
    // any thread (see the worker-thread call site in StartMenuFlow.cpp) - the same guarantee
    // swapWindowSlots()/loadSlot() already rely on. Must be called BEFORE
    // ObjectFactory::createObjectsFromUIElements()'s Pass 1 sees hwnd for the first time (i.e.
    // right after detecting the launched window, not waiting on a scan) - otherwise Pass 1 would
    // already have auto-assigned it a different slot by the time this runs.
    void assignWindowToSlot(HWND hwnd, int slot);

    std::unordered_map<HWND, int> windowWallSlotsMap;



    //Works as an ID of the positions, doesn't care what the size of map is
    int nextAvailableSlot = 0;

    
    std::atomic<bool> isUserGrabbingWindow{ false };



    

    // Delete copy constructors to enforce the "Only One" rule
    WindowManager(WindowManager const&) = delete;
    void operator=(WindowManager const&) = delete;
};