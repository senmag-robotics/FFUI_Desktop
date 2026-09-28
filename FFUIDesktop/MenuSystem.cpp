#include "MenuSystem.h"
#include "FFUIDesktop.h"     //DEVICE_WORKSPACE_X, pSapiVoice
#include "WindowWallObject.h" //WindowManager (frontZoneZCenter/ZHalfThickness)
#include "ObjectsFactory.h"  //ObjectFactory::computeMenuCell
#include <cmath>
#include <limits>
#include <iostream>  //for mirroring narrator requests to the console - see each Speak() call site below

//Removes the dead zone from a signed offset and keeps the result continuous - identical
//pattern to GridTileObject.cpp's own gridTileApplyDeadzone()/ButtonObject.cpp's/
//WindowWallObject.cpp's own applyDeadzone helpers; duplicated rather than shared for the same
//reason those already are (no shared "detent utils" header exists yet). Used by the slider
//drag's Y/Z restraint below - see SLIDER_RESTRAINT_DEADZONE's own comment.
static float menuApplyDeadzone(float offset, float deadzone) {
    if (std::abs(offset) <= deadzone) return 0.0f;
    return offset - std::copysign(deadzone, offset);
}

void MenuSystem::rebuildTilesForTopFrame() {
    tiles.clear();
    highlightedIndex = -1;
    lastNarratedIndex = -2;
    pendingHighlightCandidate = -2;
    pendingHighlightFrames = 0;
    draggingSlider = false;
    draggingTileIndex = -1;
    homingToFirstItem = false;

    if (menuStack.empty()) return;
    const MenuFrame& frame = menuStack.back();

    int count = (int)frame.items.size();
    if (count < 1) return;

    float zCenter = WindowManager::getInstance().frontZoneZCenter;
    float zHalfThickness = WindowManager::getInstance().frontZoneZHalfThickness;
    //Defensive fallback in the unlikely case a menu is opened before the scanner thread has
    //populated these yet (e.g. the very first frame after launch) - keeps tiles from collapsing
    //to zero thickness rather than actually being reachable.
    if (zHalfThickness < 1.0f) zHalfThickness = 20.0f;

    //The line every tile (and updateForce()'s continuous Z restraint) is anchored to for this
    //frame - see menuZAnchor's own comment. A freshly built frame never has a drag in flight,
    //so the line/pop/wall forces should be at full strength immediately, not ramping in.
    menuZAnchor = zCenter;
    lineRestoreRamp = 1.0f;

    for (int i = 0; i < count; i++) {
        //computeMenuCell (not the Settings/Tray grid's computeGridCell) - item 0 always lands
        //at world (0, 0, zCenter), the device's own natural center, so opening ANY menu always
        //gives the user the same known starting reference point. See its own header comment.
        ObjectFactory::GridCell cell = ObjectFactory::computeMenuCell(i, count, zCenter);

        HapticSolidProperties props{};
        props.stiffness = TILE_STIFFNESS;
        props.solidForceLimit = TILE_FORCE_LIMIT;

        FFUIObject_Meta meta{};
        meta.hapticSolidProperties = props;
        meta.uiType = UIElementType::GridTile;
        meta.orientation = Quaternion().setFromEuler(1, 0, 0);
        meta.globalPosition = cell.position;
        //Shrunk slightly from the full cell so adjacent tiles have a small haptic gap between
        //them, same spirit as the Program Tray/FFUI Settings grid's own margins.
        meta.scale = Vector3(cell.cellWidth * 0.85f, cell.cellHeight * 0.85f, zHalfThickness * 2.0f * 0.85f);
        meta.customName = "Menu Tile";

        tiles.push_back(std::make_unique<GridTileObject>(meta, frame.items[i].label, i));
    }

    //Enter "homing" - see homingToFirstItem's own comment. Every fresh/returned-to menu frame
    //starts here, regardless of how it was reached.
    homingToFirstItem = true;
}

void MenuSystem::announceTopFrame(const std::wstring& overrideText) {
    if (menuStack.empty()) return;
    if (pSapiVoice) {
        std::wstring toSpeak = overrideText.empty() ? (L"Menu opened: " + menuStack.back().title) : overrideText;
        // Mirrors every narrator request to the console, tagged by source - "mirror all the
        // narrator requests to the console please", per the request.
        std::string toSpeakNarrow(toSpeak.begin(), toSpeak.end());
        std::cout << "[Narrate:Menu] \"" << toSpeakNarrow << "\"" << std::endl;
        SpeakFfuiNarration(toSpeak.c_str());
    }
}

void MenuSystem::announceReturnedTo() {
    if (menuStack.empty()) return;
    const MenuFrame& frame = menuStack.back();
    if (pSapiVoice) {
        std::wstring toSpeak = L"Returned to " + frame.title + L": ";
        toSpeak += frame.items.empty() ? L"" : frame.items[0].label;
        std::string toSpeakNarrow(toSpeak.begin(), toSpeak.end());
        std::cout << "[Narrate:Menu] \"" << toSpeakNarrow << "\"" << std::endl;
        SpeakFfuiNarration(toSpeak.c_str());
    }
}

void MenuSystem::announceItemHighlight(int index) {
    if (menuStack.empty()) return;
    const MenuFrame& frame = menuStack.back();
    if (index < 0 || index >= (int)frame.items.size()) return;

    const MenuItemDef& item = frame.items[index];
    std::wstring toSpeak = item.label;
    if (item.type == MenuItemType::Slider) {
        toSpeak += L", drag sideways";
    }
    if (pSapiVoice && !toSpeak.empty()) {
        std::string toSpeakNarrow(toSpeak.begin(), toSpeak.end());
        std::cout << "[Narrate:Menu] \"" << toSpeakNarrow << "\"" << std::endl;
        SpeakFfuiNarration(toSpeak.c_str());
    }
}

void MenuSystem::createMenu(const std::wstring& title, std::vector<MenuItemDef> items, const std::wstring& spokenAnnouncement) {
    MenuFrame frame;
    frame.title = title;
    frame.items = std::move(items);
    menuStack.push_back(std::move(frame));

    rebuildTilesForTopFrame();
    announceTopFrame(spokenAnnouncement);
}

void MenuSystem::back() {
    if (menuStack.empty()) return;

    menuStack.pop_back();

    if (menuStack.empty()) {
        tiles.clear();
        highlightedIndex = -1;
        pendingHighlightCandidate = -2;
        pendingHighlightFrames = 0;
        draggingSlider = false;
        draggingTileIndex = -1;
        homingToFirstItem = false;
        //Fully closed - FFUIDesktop notices isActive() flipping true->false right after this
        //call and handles the "moved to a new slot" re-announcement itself.
        return;
    }

    rebuildTilesForTopFrame();
    //A return, not a fresh open - "Returned to {title}: {first item}" rather than
    //"Menu opened: {title}", per the request.
    announceReturnedTo();
}

void MenuSystem::closeAll() {
    menuStack.clear();
    tiles.clear();
    highlightedIndex = -1;
    pendingHighlightCandidate = -2;
    pendingHighlightFrames = 0;
    draggingSlider = false;
    draggingTileIndex = -1;
    homingToFirstItem = false;
}

Vector3 MenuSystem::updateForce(Location stylusLoc) {
    if (menuStack.empty()) return Vector3(0, 0, 0);

    if (draggingSlider) {
        //Rebuilt from scratch - see SLIDER_RESTRAINT_SPRING_CONSTANT's own header comment for
        //why (two previous attempts both felt wrong on real hardware). This is exactly what
        //was asked for: "a rail, allowing the user to move along X, but restraining in Y & Z" -
        //X carries only the subtle notch ripple below, never a restraint; Y/Z get a dead-zoned
        //proportional spring and nothing else, no damping term. This branch's return value is
        //the ENTIRE frame's force while dragging - no per-tile GridTileObject detent, no
        //line/pop/wall force, and (one level up, in FFUIDesktop.cpp) no UI/boundary force
        //either, since a menu being active already means MenuSystem::updateForce()'s result is
        //used as the whole frame's force with nothing else layered on top - "make sure the
        //horizontal slider overrides all other haptic effects while active", per the request.
        Vector3 offset = stylusLoc.position - sliderDragAnchor;

        //Dead zone first (same technique every other detent in this app already uses - see
        //menuApplyDeadzone()'s own comment), THEN a plain proportional spring - no damping.
        float restrainedY = menuApplyDeadzone(offset.y, SLIDER_RESTRAINT_DEADZONE);
        float restrainedZ = menuApplyDeadzone(offset.z, SLIDER_RESTRAINT_DEADZONE);

        Vector3 force(
            0.0f,
            -restrainedY * SLIDER_RESTRAINT_SPRING_CONSTANT,
            -restrainedZ * SLIDER_RESTRAINT_SPRING_CONSTANT
        );
        if (force.length() > SLIDER_RESTRAINT_MAX_FORCE) {
            force = force.normalized() * SLIDER_RESTRAINT_MAX_FORCE;
        }

        //Live value narration while dragging - map the current X offset from the anchor onto
        //[sliderMin, sliderMax], clamp to the travel range, apply it immediately (so the
        //setting takes effect live, not just on release), and narrate only when the rounded
        //value actually changes.
        if (draggingTileIndex >= 0 && draggingTileIndex < (int)menuStack.back().items.size()) {
            MenuItemDef& item = menuStack.back().items[draggingTileIndex];

            float t = (sliderDragTravelHalfWidth > 0.0f)
                ? (stylusLoc.position.x - sliderDragAnchor.x) / sliderDragTravelHalfWidth
                : 0.0f;
            if (t < -1.0f) t = -1.0f;
            if (t > 1.0f) t = 1.0f;
            float value = item.sliderMin + (t + 1.0f) * 0.5f * (item.sliderMax - item.sliderMin);
            currentDragValue = value;

            //Live apply only - cheap, in-memory, safe to call every frame on the haptic thread.
            //Persisting the final value (commitValue) happens once, on release - see
            //endSliderDrag() - never here, to keep anything disk-bound off the per-frame path.
            if (item.setValue) item.setValue(value);

            float rounded = std::round(value);
            if (!sliderValueNarratedOnce || rounded != lastNarratedSliderValueRounded) {
                std::wstring toSpeak = item.formatValue ? item.formatValue(rounded) : std::to_wstring((long long)rounded);
                if (pSapiVoice && !toSpeak.empty()) {
                    std::string toSpeakNarrow(toSpeak.begin(), toSpeak.end());
                    std::cout << "[Narrate:Menu] \"" << toSpeakNarrow << "\"" << std::endl;
                    SpeakFfuiNarration(toSpeak.c_str());
                }
                lastNarratedSliderValueRounded = rounded;
                sliderValueNarratedOnce = true;
            }

            //Subtle notch effect along X - "I also want a subtle notch effect along X", per the
            //request. A gentle ripple with one full cycle per whole-number step of the value
            //(the same granularity narrated above), so a faint physical tick lines up with each
            //narrated value change. Added AFTER the Y/Z clamp above and with its own much
            //smaller, self-limiting amplitude - it doesn't compete with or get folded into the
            //Y/Z restraint's own clamp, and X stays effectively free to slide.
            float valueRange = item.sliderMax - item.sliderMin;
            float worldPerValueStep = (valueRange != 0.0f) ? (sliderDragTravelHalfWidth * 2.0f / valueRange) : 0.0f;
            if (worldPerValueStep > 0.0f) {
                float phase = offset.x / worldPerValueStep;
                force.x = -std::sin(TWO_PI * phase) * SLIDER_NOTCH_AMPLITUDE;
            }
        }

        return force;
    }

    if (homingToFirstItem) {
        //Suspend everything else - only item 0's own tile (if any) is "live" this frame; every
        //other tile is left untouched (never gets updateForces() called), so it can't be
        //accidentally highlighted/selected while still homing in. A continual, longer-range
        //pull toward item 0 is layered on top of its own (normally short-range) detent so the
        //stylus is drawn in reliably from anywhere in the menu, not just once already close.
        if (tiles.empty()) {
            homingToFirstItem = false;
            return Vector3(0, 0, 0);
        }

        Vector3 tileForce = tiles[0]->updateForces(stylusLoc);

        if (tiles[0]->stylusIsOnThis()) {
            //Arrived - end homing and hand off to the normal pass (which will run again next
            //frame); announce item 0 right now since the normal pass's own "highlight changed"
            //narration was bypassed for every frame homing was active. Prime the debounce state
            //to match (see pendingHighlightCandidate's own comment) so the very next frame's
            //hysteresis check isn't starting from a cold, unrelated count.
            homingToFirstItem = false;
            highlightedIndex = 0;
            lastNarratedIndex = 0;
            pendingHighlightCandidate = 0;
            pendingHighlightFrames = HIGHLIGHT_DEBOUNCE_FRAMES;
            announceItemHighlight(0);
            return tileForce;
        }

        //Pull toward item 0 in X, Y, AND Z - previously Z was left at 0 here, which meant the
        //menu's Z constraint only ever started acting once homing had already finished (i.e.
        //once item 0 was reached some other way) - "the Z constraint is only activating after
        //the initial position is reached", per the request. item0Pos.z is always menuZAnchor
        //(both come from the same zCenter in rebuildTilesForTopFrame()), so this pulls back
        //toward the menu's home Z the same way X/Y already pull toward item 0's position.
        Vector3 item0Pos = tiles[0]->getMeta().globalPosition;
        Vector3 toItem0(item0Pos.x - stylusLoc.position.x, item0Pos.y - stylusLoc.position.y, item0Pos.z - stylusLoc.position.z);
        Vector3 homingForce = toItem0 * HOMING_STIFFNESS;
        if (homingForce.length() > HOMING_MAX_FORCE) {
            homingForce = homingForce.normalized() * HOMING_MAX_FORCE;
        }

        return tileForce + homingForce;
    }

    //Normal tile-grid pass: update every tile's force this frame and sum them - only the
    //currently-hit tile will contribute anything nonzero (mirroring GravityWellObject/
    //ButtonObject/GridTileObject's own "zero outside bounds" behaviour) - while tracking which
    //tile (if any) is highlighted, for narration and for selectHighlighted() to act on.
    Vector3 totalForce(0, 0, 0);
    int rawHighlighted = -1;
    for (size_t i = 0; i < tiles.size(); i++) {
        Vector3 f = tiles[i]->updateForces(stylusLoc);
        totalForce += f;
        if (tiles[i]->stylusIsOnThis()) rawHighlighted = (int)i;
    }

    //Hysteresis - see pendingHighlightCandidate's own comment. The raw box-test result above
    //still drives the force feel every frame regardless; only highlightedIndex (narration +
    //selectHighlighted()'s target) waits for the same candidate to hold for a few frames.
    if (rawHighlighted != pendingHighlightCandidate) {
        pendingHighlightCandidate = rawHighlighted;
        pendingHighlightFrames = 1;
    }
    else if (pendingHighlightFrames < HIGHLIGHT_DEBOUNCE_FRAMES) {
        pendingHighlightFrames++;
    }
    if (pendingHighlightFrames >= HIGHLIGHT_DEBOUNCE_FRAMES) {
        highlightedIndex = pendingHighlightCandidate;
    }

    if (highlightedIndex != lastNarratedIndex) {
        announceItemHighlight(highlightedIndex);
        lastNarratedIndex = highlightedIndex;
    }

    //Continuous line + pop + end-wall feel, layered on top of the per-tile box detents above -
    //active THE WHOLE TIME a menu is open (not just while nothing is highlighted), so entering/
    //leaving a tile's own small box is seamless rather than the per-tile force cutting in and
    //out against silence. See each constant's own header comment for what it does.
    if (!tiles.empty()) {
        //Ramp back toward full strength if a slider drag just ended - see lineRestoreRamp's
        //own comment. No-op (stays at 1.0) the rest of the time.
        if (lineRestoreRamp < 1.0f) {
            lineRestoreRamp += LINE_RESTORE_RAMP_PER_FRAME;
            if (lineRestoreRamp > 1.0f) lineRestoreRamp = 1.0f;
        }

        //Which column's X the line is currently sprung back toward - whichever tile is
        //highlighted, or (if none is) whichever tile is nearest by Y. For the common
        //single-column menu this is column 0 (x = 0) for every item; a multi-column submenu
        //(e.g. a long voice list) can still move the line between columns this way.
        int lineTileIndex = (highlightedIndex >= 0) ? highlightedIndex : 0;
        if (highlightedIndex < 0) {
            float bestYDist = (std::numeric_limits<float>::max)();
            for (size_t i = 0; i < tiles.size(); i++) {
                float dy = std::abs(tiles[i]->getMeta().globalPosition.y - stylusLoc.position.y);
                if (dy < bestYDist) { bestYDist = dy; lineTileIndex = (int)i; }
            }
        }
        float lineX = tiles[lineTileIndex]->getMeta().globalPosition.x;

        Vector3 lineForce(
            (lineX - stylusLoc.position.x) * LINE_RESTRAINT_STIFFNESS,
            0.0f,
            (menuZAnchor - stylusLoc.position.z) * LINE_RESTRAINT_STIFFNESS
        );
        if (lineForce.length() > LINE_RESTRAINT_MAX_FORCE) {
            lineForce = lineForce.normalized() * LINE_RESTRAINT_MAX_FORCE;
        }
        totalForce += lineForce * lineRestoreRamp;

        //The "pop" - a softer pull in Y alone toward the nearest item along the line,
        //independent of the box test above.
        GridTileObject* nearestByY = nullptr;
        float bestDist = (std::numeric_limits<float>::max)();
        for (auto& tile : tiles) {
            float dy = std::abs(tile->getMeta().globalPosition.y - stylusLoc.position.y);
            if (dy < bestDist) { bestDist = dy; nearestByY = tile.get(); }
        }
        if (nearestByY != nullptr) {
            float popForceY = (nearestByY->getMeta().globalPosition.y - stylusLoc.position.y) * POP_STIFFNESS;
            if (popForceY > POP_MAX_FORCE) popForceY = POP_MAX_FORCE;
            if (popForceY < -POP_MAX_FORCE) popForceY = -POP_MAX_FORCE;
            totalForce.y += popForceY * lineRestoreRamp;
        }

        //Soft walls just past the first and last item, scanned across every tile (not just
        //tiles.front()/back()) so this stays correct regardless of column layout.
        float topY = tiles.front()->getMeta().globalPosition.y;
        float bottomY = topY;
        for (auto& tile : tiles) {
            float y = tile->getMeta().globalPosition.y;
            if (y > topY) topY = y;
            if (y < bottomY) bottomY = y;
        }

        if (stylusLoc.position.y > topY + MENU_END_WALL_MARGIN) {
            float penetration = stylusLoc.position.y - (topY + MENU_END_WALL_MARGIN);
            float wallForce = -penetration * LINE_RESTRAINT_STIFFNESS;
            if (wallForce < -LINE_RESTRAINT_MAX_FORCE) wallForce = -LINE_RESTRAINT_MAX_FORCE;
            totalForce.y += wallForce * lineRestoreRamp;
        }
        else if (stylusLoc.position.y < bottomY - MENU_END_WALL_MARGIN) {
            float penetration = (bottomY - MENU_END_WALL_MARGIN) - stylusLoc.position.y;
            float wallForce = penetration * LINE_RESTRAINT_STIFFNESS;
            if (wallForce > LINE_RESTRAINT_MAX_FORCE) wallForce = LINE_RESTRAINT_MAX_FORCE;
            totalForce.y += wallForce * lineRestoreRamp;
        }
    }

    return totalForce;
}

void MenuSystem::selectHighlighted(Location stylusLoc) {
    if (menuStack.empty() || highlightedIndex < 0) return;
    if (highlightedIndex >= (int)menuStack.back().items.size()) return;

    MenuItemDef& item = menuStack.back().items[highlightedIndex];

    if (item.type == MenuItemType::Back) {
        back();
        return;
    }

    if (item.type == MenuItemType::Slider) {
        draggingSlider = true;
        draggingTileIndex = highlightedIndex;
        sliderValueNarratedOnce = false;

        int count = (int)menuStack.back().items.size();
        ObjectFactory::GridCell cell = ObjectFactory::computeMenuCell(
            highlightedIndex, count, WindowManager::getInstance().frontZoneZCenter);

        //How much physical X travel maps to the item's full [sliderMin, sliderMax] range -
        //bigger means MORE movement is needed for the same value change, i.e. LOWER
        //sensitivity. Raised from this feature's original 0.4x tile-width factor to 0.8x (half
        //the sensitivity) - "can we reduce the horizontal sensitivity on that slider", per the
        //request.
        sliderDragTravelHalfWidth = cell.cellWidth * SLIDER_TRAVEL_WIDTH_FACTOR;
        if (sliderDragTravelHalfWidth < 1.0f) sliderDragTravelHalfWidth = 1.0f;

        //Y/Z restraint reference is the real press position - but X needs to be a virtual
        //reference point chosen so THIS press position maps back to the item's CURRENT value,
        //not always the range's arithmetic midpoint. Without this, simply grabbing the slider
        //(before moving at all) would snap the live value to (sliderMin+sliderMax)/2 on the
        //very first dragged frame - a real bug, not just an inconvenience, since setValue() is
        //applied live starting immediately.
        sliderDragAnchor = stylusLoc.position;
        float range = item.sliderMax - item.sliderMin;
        float initialValue = item.getValue ? item.getValue() : (item.sliderMin + item.sliderMax) * 0.5f;
        float initialT = (range != 0.0f) ? (2.0f * (initialValue - item.sliderMin) / range - 1.0f) : 0.0f;
        if (initialT < -1.0f) initialT = -1.0f;
        if (initialT > 1.0f) initialT = 1.0f;
        sliderDragAnchor.x = stylusLoc.position.x - initialT * sliderDragTravelHalfWidth;

        currentDragValue = initialValue;
        return;
    }

    //Action item.
    if (item.onSelect) item.onSelect();
}

void MenuSystem::endSliderDrag() {
    if (!menuStack.empty() && draggingTileIndex >= 0 && draggingTileIndex < (int)menuStack.back().items.size()) {
        MenuItemDef& item = menuStack.back().items[draggingTileIndex];
        if (item.commitValue) item.commitValue(currentDragValue);
    }
    draggingSlider = false;
    draggingTileIndex = -1;

    //The line/pop/wall forces (suspended entirely while draggingSlider was true - see
    //updateForce()'s early return in that branch) start back at zero strength and ramp up
    //over the next several frames of the normal pass, rather than snapping straight to full
    //force the instant this drag ends - see lineRestoreRamp's own comment.
    lineRestoreRamp = 0.0f;
}
