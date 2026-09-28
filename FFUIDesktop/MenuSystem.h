#pragma once
#include "mathTypes.h"
#include "GridTileObject.h"
#include <vector>
#include <string>
#include <functional>
#include <memory>

enum class MenuItemType {
    Action,   //selecting fires onSelect() immediately (front/select button press edge)
    Slider,   //press-and-hold to drag (free X, restrained Y/Z); release commits the value
    Back,     //selecting pops one level off the menu stack (closing entirely if it was the only one)
};

//One entry in a menu passed to MenuSystem::createMenu(). Action/Back items only need
//label+type(+onSelect); Slider items additionally need the value accessors below - MenuSystem
//only ever maps a physical X offset onto [sliderMin, sliderMax] and calls setValue()/getValue(),
//it never interprets what the value itself means (rate, volume, or anything added later).
struct MenuItemDef {
    std::wstring label;
    MenuItemType type = MenuItemType::Action;

    std::function<void()> onSelect;   //Action items only

    std::function<float()> getValue;                    //Slider items only
    //Applied EVERY frame while the slider is being dragged (live audible/haptic feedback) -
    //keep this cheap (e.g. an in-memory ISpVoice::SetRate() call), never anything that blocks
    //or touches disk, since it runs on the haptic thread at frame rate.
    std::function<void(float)> setValue;                 //Slider items only
    //Called exactly ONCE, when the drag ends (select button released) - the place to do
    //anything more expensive, like persisting the final value to disk.
    std::function<void(float)> commitValue;               //Slider items only
    std::function<std::wstring(float)> formatValue;       //Slider items only - renders the current value for narration
    float sliderMin = -10.0f;
    float sliderMax = 10.0f;
};

//Generic, reusable haptic menu framework requested to generalize SetupConfirmationPrompt's
//two-detent confirm/cancel choice into an arbitrary N-item menu. createMenu() renders any list
//of MenuItemDef as a grid of GridTileObject tiles spanning the FULL shared front zone (both the
//Program Tray and FFUI Settings halves - normal Settings/Tray content and behaviour is fully
//suspended while any menu is open, per the request, so there's no split to preserve while a
//menu has the floor), with a navigation stack so "back" items return to whichever menu opened
//this one.
//
//A Meyer's singleton, matching WindowManager's own pattern. Entirely haptic-thread-owned -
//every method here is only ever called from FFUIDesktop::updateFrame() (the same thread that
//owns AuxGestureState/SetupConfirmationPrompt), so - like those - no locking is needed.
class MenuSystem {
private:
    MenuSystem() {}
public:
    static MenuSystem& getInstance() {
        static MenuSystem instance;
        return instance;
    }
    MenuSystem(MenuSystem const&) = delete;
    void operator=(MenuSystem const&) = delete;

    //True whenever any menu (top-level or a nested submenu) is open. FFUIDesktop checks this
    //ahead of every other button/force branch while true - "other FFUI behaviours should be
    //suspended while these menus are active" per the request.
    bool isActive() const { return !menuStack.empty(); }

    //True only while a Slider item is actively being dragged (select button held after being
    //pressed while highlighted on it). While true, updateForce() renders the constrained
    //slider force instead of the normal per-tile grid forces, and FFUIDesktop routes the
    //select-button release to endSliderDrag() instead of treating it as a normal press/select.
    bool isDraggingSlider() const { return draggingSlider; }

    //Opens a new menu, pushing it onto the navigation stack (so a Back item on it returns
    //here). Speaks "Menu opened: {title}" - or, when spokenAnnouncement is non-empty, that
    //exact text instead (e.g. StartMenuFlow's "Found N results for {query}") - rebuilds the
    //tile objects for it, and enters the "homing" state described on homingToFirstItem below.
    //spokenAnnouncement exists because this method's own announcement uses
    //SPF_PURGEBEFORESPEAK, which would otherwise cut off anything a caller tried to speak
    //separately just before calling this (the same clipping bug the drop-slot narration fix
    //closed elsewhere - see WindowManager::narrateGrabResult's own speakResult parameter).
    void createMenu(const std::wstring& title, std::vector<MenuItemDef> items, const std::wstring& spokenAnnouncement = L"");

    //Pops one level. If that empties the stack, this is a full close - FFUIDesktop notices
    //isActive() flipping true->false right after and re-triggers the "moved to a new slot"
    //narration per the request - see WindowManager::forceRenarrateOnNextFocus(). Otherwise,
    //speaks "Returned to {title}: {first item's label}" (distinct from createMenu()'s "Menu
    //opened: ..." - this is a return, not a fresh open) and re-enters the "homing" state.
    void back();

    //Immediately closes every open menu/submenu, discarding the whole stack in one step -
    //for FFUIDesktop to call in edge cases (e.g. the device disconnecting mid-menu) rather
    //than the normal one-level-at-a-time Back-item flow above.
    void closeAll();

    //Called every haptic frame while isActive() - computes and returns this frame's force
    //(the tile grid's pull, or the constrained slider force while dragging), and updates which
    //tile is currently highlighted, narrating on change.
    Vector3 updateForce(Location stylusLoc);

    //Select-button press edge, while isActive() and NOT isDraggingSlider(): activates whichever
    //tile is currently highlighted - Action items fire onSelect(), Back pops one level, and a
    //Slider item instead begins a drag (see isDraggingSlider()) rather than "selecting" outright.
    void selectHighlighted(Location stylusLoc);

    //Select-button release edge, while isDraggingSlider(): commits the dragged value (already
    //applied live via setValue() during the drag - see updateForce()) and ends the drag.
    void endSliderDrag();

private:
    struct MenuFrame {
        std::wstring title;
        std::vector<MenuItemDef> items;
    };

    std::vector<MenuFrame> menuStack;
    std::vector<std::unique_ptr<GridTileObject>> tiles;   //one per item in the current (top) frame
    int highlightedIndex = -1;
    int lastNarratedIndex = -2;   //-2 = nothing narrated yet, distinct from -1 ("nothing highlighted")

    //Hysteresis/debounce on top of the raw per-frame box test, so small noise on the position
    //measurement right at a tile's edge can't flip highlightedIndex (and so narration, and
    //selectHighlighted()'s target) back and forth every frame - "the narrator reads out the
    //option multiple times in quick succession... probably due to noise on the position
    //measurements", per the request. pendingHighlightCandidate/Frames track whatever the raw
    //box test has been saying lately; highlightedIndex (above) only actually moves to match
    //once the SAME candidate has held for HIGHLIGHT_DEBOUNCE_FRAMES consecutive frames - a
    //candidate that changes again before that resets the count rather than partially carrying
    //over. Does not affect the per-tile haptic FORCE feel at all (that still reacts every
    //frame, straight off each tile's own live box test) - only which item counts as
    //"highlighted" for narration/selection purposes.
    int pendingHighlightCandidate = -2;   //-2 = not yet tracking anything, distinct from a real candidate of -1
    int pendingHighlightFrames = 0;
    static constexpr int HIGHLIGHT_DEBOUNCE_FRAMES = 3;   //unverified - tune by feel

    bool draggingSlider = false;
    int draggingTileIndex = -1;
    Vector3 sliderDragAnchor{ 0, 0, 0 };        //Y/Z restrained back to this every frame; X read live
    float sliderDragTravelHalfWidth = 1.0f;      //world units from the tile's center to sliderMax/sliderMin
    float lastNarratedSliderValueRounded = 0.0f;
    bool sliderValueNarratedOnce = false;
    float currentDragValue = 0.0f;   //this frame's live-mapped slider value, read back by endSliderDrag() for commitValue()

    //True from the moment any menu frame is (re)built (a fresh createMenu(), or back() landing
    //on a remaining frame) until the stylus physically arrives on item 0 for the first time
    //since then. While true, updateForce() renders ONLY a continual pull toward item 0 (no
    //other tile's detent, no magnetism, nothing else selectable) - "suspend all other options &
    //other haptic effects, and pull the user towards the first option continually until it is
    //highlighted... to always locate the user at 0", per the request. Cleared the instant item 0
    //is reached, at which point normal per-tile behaviour (and selection) resumes as usual.
    bool homingToFirstItem = false;

    void rebuildTilesForTopFrame();
    void announceTopFrame(const std::wstring& overrideText = L"");
    void announceReturnedTo();
    void announceItemHighlight(int index);

    static constexpr float TILE_STIFFNESS = 0.0008f;
    static constexpr float TILE_FORCE_LIMIT = 0.0025f;

    //The Z every tile in the current menu frame sits at - captured once per rebuildTilesForTopFrame()
    //call (same zCenter passed to computeMenuCell for every tile) so updateForce()'s line
    //restraint below has a fixed target to pull back toward, rather than recomputing it from
    //WindowManager every frame.
    float menuZAnchor = 0.0f;

    //Continuous "line" the stylus is held to during the normal (non-slider, non-homing) tile
    //pass below - a spring pulling X back toward the current column's X and Z back toward
    //menuZAnchor, active the WHOLE time a menu is open (not just within a tile's own small
    //box), so it feels like sliding along a physical groove rather than hunting for boxes in
    //open space. This is also what fixes "narration doesn't work at certain Z values" -
    //without anything constraining Z, drifting away from the menu's home Z eventually took
    //every tile out of hit-test range; this keeps that from happening at all. Distinct from,
    //and layered underneath, the smaller-range SLIDER_RESTRAINT springs below, which restrain
    //Y/Z against the anchor captured at the moment a slider drag specifically began.
    static constexpr float LINE_RESTRAINT_STIFFNESS = 0.0012f;
    static constexpr float LINE_RESTRAINT_MAX_FORCE = 0.0045f;

    //The softer "pop" pulling the stylus toward the nearest item's Y along the line - weaker
    //than LINE_RESTRAINT so the line itself always reads as the stronger, more solid
    //constraint versus the individual stopping points along it - "a line... with softer pops
    //between the options", per the request. Independent of whether the stylus is currently
    //inside that item's own (smaller) box, unlike the per-tile GridTileObject detents above,
    //so the space BETWEEN items still feels like part of one continuous line rather than dead
    //space with nothing pulling on it. Halved from this feature's first pass per hardware
    //feedback ("the forces between options are too large right now").
    static constexpr float POP_STIFFNESS = 0.0003f;
    static constexpr float POP_MAX_FORCE = 0.0011f;

    //One-sided soft walls just past the first/last item's Y position - "haptic boundaries
    //around the limits of the menu", per the request - so overshooting past either end is felt
    //as a wall, not open space. Deliberately reuses LINE_RESTRAINT's own stiffness/max force
    //rather than a separate (and, in the first pass, weaker-feeling) pair of constants, so the
    //ends of the list feel exactly "as solid as the x/z constraints" per hardware feedback.
    //MARGIN is how much free travel (world units) is allowed past the end item before the wall
    //actually engages.
    static constexpr float MENU_END_WALL_MARGIN = 10.0f;

    //0 right after a slider drag ends, ramping to 1 (fully restored) over the next several
    //frames - lets the line/pop/wall forces above fade back in smoothly rather than snapping
    //to full strength the instant the drag's own Y/Z restraint lets go, which could otherwise
    //be felt as a jerk if the stylus had wandered far from the line while dragging freely in
    //X - "the Y component will need to be suspended during horizontal slider/drag fields, and
    //re-introduced smoothly after", per the request. Reset to 1.0 (no ramp needed) whenever a
    //menu frame is freshly built, since nothing was just dragging in that case.
    float lineRestoreRamp = 1.0f;
    static constexpr float LINE_RESTORE_RAMP_PER_FRAME = 0.05f; //unverified - tune by feel

    //The "homing" pull toward item 0 while homingToFirstItem is true - deliberately stronger
    //and longer-range than the ordinary per-tile detent (which only engages once already
    //within the tile's own small bounds) and the light magnetism above, since this needs to
    //reliably pull the stylus in from anywhere across the whole menu, not just nudge it once
    //it's already close.
    static constexpr float HOMING_STIFFNESS = 0.0006f;
    static constexpr float HOMING_MAX_FORCE = 0.003f;

    //Rebuilt from scratch per hardware feedback - two previous attempts (a continuous spring,
    //then a spring plus a velocity-damping term) both felt wrong on real hardware ("still very
    //not right", and separately "[damping]... causing issues/instability"). Back to exactly
    //what was actually asked for: "a rail, allowing the user to move along X, but restraining
    //in Y & Z" - a plain proportional spring, no damping term at all this time - with a small
    //dead zone (DEADZONE, reusing the same technique every other detent in this app already
    //uses - see menuApplyDeadzone()) so the restraint doesn't respond to zero-offset position
    //noise at all, rather than trying to damp that noise away after the fact. Unverified
    //starting values - tune by feel.
    static constexpr float SLIDER_RESTRAINT_SPRING_CONSTANT = 0.0015f;
    static constexpr float SLIDER_RESTRAINT_MAX_FORCE = 0.006f;
    static constexpr float SLIDER_RESTRAINT_DEADZONE = 1.0f;

    //How much of the tile's own width becomes the drag's travel half-width (see
    //selectHighlighted()) - bigger means more physical movement is needed to cover the item's
    //full value range, i.e. lower sensitivity. Raised from this feature's original 0.4x to
    //0.8x (half the sensitivity) - "can we reduce the horizontal sensitivity on that slider",
    //per the request. Unverified - tune by feel.
    static constexpr float SLIDER_TRAVEL_WIDTH_FACTOR = 0.8f;

    //A subtle ripple added to the otherwise-free X axis while dragging - "I also want a subtle
    //notch effect along X", per the request. One full ripple cycle per whole-number step of
    //the slider's value (the same granularity the live narration above already rounds to - see
    //updateForce()'s draggingSlider branch), so a felt "tick" lines up with each narrated value
    //change. AMPLITUDE is deliberately small relative to SLIDER_RESTRAINT_MAX_FORCE and every
    //other force in this app - X should still read as essentially free, just textured.
    //Unverified starting value - tune by feel.
    static constexpr float SLIDER_NOTCH_AMPLITUDE = 0.0004f;
    static constexpr float TWO_PI = 6.28318530718f;
};
