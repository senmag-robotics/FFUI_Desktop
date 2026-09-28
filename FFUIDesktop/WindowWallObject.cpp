#include "WindowWallObject.h"
#include "FFUIDesktop.h"
#include "GravityWellObject.h"
#include "DetentTuning.h"
#include <algorithm>
#include <limits>
#include <cmath>

//Removes the dead zone from a signed offset and keeps the result continuous - identical
//pattern to GridTileObject.cpp's own gridTileApplyDeadzone()/ButtonObject.cpp's
//applyDeadzone(); duplicated rather than shared for the same reason those two already are
//(no shared "detent utils" header exists yet). Used by the archived-tile (Program Tray)
//detent in calculateInteractionForce() below, which now deliberately mirrors GridTileObject's
//own detent formula - see that call site's comment.
static float windowWallApplyDeadzone(float offset, float deadzone) {
    if (std::abs(offset) <= deadzone) return 0.0f;
    return offset - std::copysign(deadzone, offset);
}

WindowWallObject::WindowWallObject(WindowWallMeta wallMeta, Vector3 position, float thickness, float stiffness,
    float solidForceLimit, float height, float width)
    : FFUIObject({
        position,                                    // globalPosition
        {width, thickness , height},                             // scale 
        Quaternion().setFromEuler(0, 0, 90),                                // orientation
        {solidForceLimit, stiffness, 0.0f, 0.0f, 0.0f},                  // hapticSolidProperties
        std::string(wallMeta.windowTitle.begin(), wallMeta.windowTitle.end()), // customName (wstring to string conversion)
        false,                                                    // snappedToThis
        UIElementType::Window               //uiType
        }),
    windowMeta(wallMeta) // Initialize the UI properties

{
}


int WindowManager::getSlotForWindow(HWND hwnd) {
    // Locks windowMutex itself now, rather than trusting every caller to already hold it - this
    // used to be a genuine unlocked read racing against the scanner thread's own LOCKED writes to
    // windowWallSlotsMap (Pass 1's rebuild in ObjectFactory::createObjectsFromUIElements,
    // swapWindowSlots(), assignWindowToSlot()) every time this ran on the haptic thread outside
    // of an already-locked caller (e.g. via narrateWindowFocus(), called from the new top-level
    // room-crossing logic in FFUIDesktop::updateFrame() with no lock of its own) - a real, if
    // rare, concurrent read/write on the same std::unordered_map, undefined behavior that can
    // corrupt the map's internal structure and crash somewhere completely unrelated later (seen
    // as a std::bad_array_new_length thrown out of an unrelated wstring concatenation, right
    // after a drop-into-slot exercised exactly this path). windowMutex is a recursive_mutex (see
    // its own comment) specifically so this doesn't deadlock the callers that already hold it.
    std::lock_guard<std::recursive_mutex> lock(windowMutex);
    auto it = windowWallSlotsMap.find(hwnd);
    return (it != windowWallSlotsMap.end()) ? it->second : -1;
}

std::wstring WindowManager::getShortAppName(HWND hwnd) {
    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);

    if (processId != 0) {
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
        if (hProcess) {
            wchar_t pathBuffer[MAX_PATH] = {};
            DWORD pathSize = MAX_PATH;
            bool gotPath = QueryFullProcessImageNameW(hProcess, 0, pathBuffer, &pathSize) != 0;
            CloseHandle(hProcess);

            if (gotPath) {
                std::wstring fullPath(pathBuffer, pathSize);

                // Strip the directory, keeping just the executable's file name.
                size_t lastSlash = fullPath.find_last_of(L"\\/");
                std::wstring fileName = (lastSlash == std::wstring::npos) ? fullPath : fullPath.substr(lastSlash + 1);

                // Strip the ".exe" extension, if present, so we speak "Firefox" not "firefox.exe".
                size_t lastDot = fileName.find_last_of(L'.');
                if (lastDot != std::wstring::npos) {
                    fileName = fileName.substr(0, lastDot);
                }

                if (!fileName.empty()) return fileName;
            }
        }
    }

    // Couldn't resolve the owning process (e.g. an elevated app we don't have permission to
    // query) - fall back to the raw window title rather than saying nothing.
    wchar_t titleBuffer[256] = {};
    GetWindowTextW(hwnd, titleBuffer, 256);
    return std::wstring(titleBuffer);
}

void WindowManager::narrateWindowFocus(HWND hwnd) {
    // Already the last thing we narrated - nothing has changed, stay silent. Also clears any
    // pending "off" streak resetNarratedWindow() may have started for this same window (e.g.
    // one noisy frame reported it as off, then the very next frame confirmed it's still on) -
    // see narratedWindowOffStreak's own comment.
    if (hwnd == lastNarratedWindowHandle) {
        narratedWindowOffStreak = 0;
        return;
    }

    int physicalSlot = getSlotForWindow(hwnd);
    if (physicalSlot < 0) {
        // Shouldn't normally happen - anything rendered as a window wall/gravity-well object
        // should already have a slot assigned by ObjectFactory. Bail out rather than announce
        // something wrong.
        return;
    }

    int layerId = (physicalSlot < numOfActiveWindows) ? physicalSlot : ARCHIVED_LAYER_ID;
    std::wstring appName = getShortAppName(hwnd);

    // New layer (moved between active slots, entered/left the Program Tray, or came from FFUI
    // Settings): lead with the slot number (or "Program Tray" for the tray, superseding the
    // earlier "continue the slot numbering" behavior for archived windows, per this feature's
    // "the narrator should read Program Tray: <highlighted program name>" requirement). Same
    // layer (e.g. browsing within the tray): just the program name, e.g. "Firefox".
    std::wstring toSpeak;
    if (layerId != lastNarratedLayer) {
        toSpeak = (layerId == ARCHIVED_LAYER_ID)
            ? (L"Program Tray, " + appName)
            : (L"Slot " + std::to_wstring(physicalSlot + 1) + L", " + appName);
    }
    else {
        toSpeak = appName;
    }

    if (pSapiVoice && !toSpeak.empty()) {
        // Mirrors every narrator request to the console, tagged by source, so what's actually
        // spoken (and when) can be cross-checked against what's heard without guessing which of
        // the many Speak() call sites in this codebase produced a given utterance - "mirror all
        // the narrator requests to the console please", per the request.
        std::string toSpeakNarrow(toSpeak.begin(), toSpeak.end());
        std::cout << "[Narrate:Window] \"" << toSpeakNarrow << "\"" << std::endl;
        SpeakFfuiNarration(toSpeak.c_str());
    }

    lastNarratedLayer = layerId;
    lastNarratedWindowHandle = hwnd;
    narratedWindowOffStreak = 0;
    // Entering a window/tray layer always supersedes whichever FFUI Settings tile was last
    // narrated, so returning to Settings later is treated as a fresh layer entry rather than
    // being silently deduped against a tile index that's no longer the "current" thing.
    lastNarratedSettingsTileIndex = -1;
}

void WindowManager::resetNarratedWindow(HWND hwnd) {
    // If the stylus pulls out of the window we last narrated, clear the memory so re-entering
    // it later speaks again - but only once this has been reported NARRATION_HYSTERESIS_FRAMES
    // times in a row, not on the very first "off" frame. A single noisy frame right at a
    // boundary no longer clears (and so doesn't cause the next "on" frame to re-narrate) - see
    // narratedWindowOffStreak's own comment. Deliberately leave lastNarratedLayer untouched
    // either way: re-entering this same window without anything else having been narrated in
    // between is still "the same layer", so re-entry should be announced with just the program
    // name, not "Slot N" again.
    if (lastNarratedWindowHandle == hwnd) {
        narratedWindowOffStreak++;
        if (narratedWindowOffStreak >= NARRATION_HYSTERESIS_FRAMES) {
            lastNarratedWindowHandle = nullptr;
            narratedWindowOffStreak = 0;
        }
    }
}

void WindowManager::narrateGrabStart(HWND grabbedWindow) {
    if (grabbedWindow == NULL) return;

    lastGrabbedAppName = getShortAppName(grabbedWindow);
    std::string appNameNarrow(lastGrabbedAppName.begin(), lastGrabbedAppName.end());

    std::cout << "[Grab] " << appNameNarrow << " grabbed" << std::endl;

    if (pSapiVoice && !lastGrabbedAppName.empty()) {
        std::wstring toSpeak = lastGrabbedAppName + L" grabbed";
        std::cout << "[Narrate:Grab] \"" << appNameNarrow << " grabbed\"" << std::endl;
        SpeakFfuiNarration(toSpeak.c_str());
    }

    // A grab always starts in the window-select list, so "cancel" is the implicit starting
    // option - reset here so the first real move into a slot still gets announced.
    lastNarratedGrabOption = -1;
}

void WindowManager::narrateGrabOption(int hoveredSlot) {
    int option = (hoveredSlot >= 0 && hoveredSlot < numOfActiveWindows) ? hoveredSlot : -1;
    if (option == lastNarratedGrabOption) return; // no change - stay silent

    std::string appNameNarrow(lastGrabbedAppName.begin(), lastGrabbedAppName.end());

    if (option == -1) {
        std::cout << "[Grab] " << appNameNarrow << " dragged to selector (cancel)" << std::endl;
        if (pSapiVoice) {
            std::cout << "[Narrate:Grab] \"Option: cancel\"" << std::endl;
            SpeakFfuiNarration(L"Option: cancel");
        }
    }
    else {
        std::cout << "[Grab] " << appNameNarrow << " dragged to slot " << (option + 1) << std::endl;
        if (pSapiVoice) {
            std::wstring toSpeak = L"Option: slot " + std::to_wstring(option + 1);
            std::string toSpeakNarrow(toSpeak.begin(), toSpeak.end());
            std::cout << "[Narrate:Grab] \"" << toSpeakNarrow << "\"" << std::endl;
            SpeakFfuiNarration(toSpeak.c_str());
        }
    }

    lastNarratedGrabOption = option;
}

void WindowManager::narrateGrabResult(bool droppedInActiveSlot, int slotIndex, bool speakResult) {
    std::string appNameNarrow(lastGrabbedAppName.begin(), lastGrabbedAppName.end());

    if (droppedInActiveSlot) {
        std::cout << "[Grab] " << appNameNarrow << " dropped in slot " << (slotIndex + 1) << std::endl;
        if (pSapiVoice && speakResult) {
            std::wstring toSpeak = L"Dropped in slot " + std::to_wstring(slotIndex + 1);
            std::string toSpeakNarrow(toSpeak.begin(), toSpeak.end());
            std::cout << "[Narrate:Grab] \"" << toSpeakNarrow << "\"" << std::endl;
            SpeakFfuiNarration(toSpeak.c_str());
        }
    }
    else {
        std::cout << "[Grab] " << appNameNarrow << " drag cancelled" << std::endl;
        if (pSapiVoice && speakResult) {
            std::cout << "[Narrate:Grab] \"Drag cancelled\"" << std::endl;
            SpeakFfuiNarration(L"Drag cancelled");
        }
    }
}

void WindowManager::buildGrabZoneOptions() {
    grabZoneOptions.clear();

    // A fresh grab (the only time this is called) has no "currently classified" option yet -
    // classifyGrabZone()'s first call this grab should snap directly to wherever the stylus
    // actually is, not apply hysteresis against a stale index left over from a previous grab.
    grabZoneHysteresisIndex = -1;

    // The whole archived list shares a single Z depth (only Y differs between entries - see
    // ObjectFactory::createObjectsFromUIElements), so any one archived window's Z stands in for
    // "the window-select list" as a whole, i.e. "cancel". Every Program Tray tile is still
    // rendered live every scan cycle (only the ACTIVE side changed with the single-merged-room
    // feature), so this can keep reading a real object's real Z unchanged.
    if (!ArchivedWindows.empty()) {
        grabZoneOptions.push_back({ -1, ArchivedWindows.front()->getMeta().globalPosition.z });
    }

    // ACTIVE side: only ONE active-slot WindowWallObject is ever live at a time now (the
    // "current" one - see currentActiveSlotIndex), so a live object's real Z can no longer stand
    // in for "where slot N is" the way it used to. Synthesize each OCCUPIED active slot's Z
    // directly from windowWallSlotsMap instead - iterating that map (rather than ActiveWindows)
    // preserves the exact existing behavior that only slots with an actual window get a drop
    // lane. Evenly spread across the SAME fixed physical span the merged room itself occupies
    // (ACTIVE_ZONE_ROOM_COUNT worth of rooms, matching computeFrontZoneBand()'s own math with
    // ACTIVE_ZONE_ROOM_COUNT in place of numOfActiveWindows) - deliberately NOT the legacy
    // per-slot pitch formula, which was only ever tuned to fit exactly ACTIVE_ZONE_ROOM_COUNT
    // slots; reusing it directly for a higher quickslot count would push lanes past startZ,
    // outside the device's real reachable Z range.
    float activeZoneFront = endZ - 5.0f;
    float activeZoneBack = endZ - 5.0f - (ACTIVE_ZONE_ROOM_COUNT * roomDepth) - 10.0f;
    float activeZoneSpan = activeZoneFront - activeZoneBack;

    for (const auto& pair : windowWallSlotsMap) {
        int slot = pair.second;
        if (slot < 0 || slot >= numOfActiveWindows) continue; // archived - handled by the "cancel" entry above

        // Slot 0 ("Slot 1", narrated 1-based) sits at activeZoneBack - the end of the span
        // nearest the archived/"cancel" entry above (and, not coincidentally, exactly where the
        // single merged room's own real wall always physically sits - see
        // ObjectFactory::createObjectsFromUIElements's zPosSlotIndex) - so it's the FIRST slot
        // reached while dragging in from the Program Tray, not the last. "reverse the order of
        // the slots when dragging a program (so we reach slot 1 first)", per the request; this
        // was previously activeZoneFront - t * activeZoneSpan, which put slot 0 furthest away
        // and the highest-numbered slot first.
        float t = (numOfActiveWindows > 1) ? (float)slot / (float)(numOfActiveWindows - 1) : 0.5f;
        float z = activeZoneBack + t * activeZoneSpan;
        grabZoneOptions.push_back({ slot, z });
    }

    std::sort(grabZoneOptions.begin(), grabZoneOptions.end(),
        [](const GrabZoneOption& a, const GrabZoneOption& b) { return a.zCenter < b.zCenter; });
}

int WindowManager::classifyGrabZone(float currentZ) {
    if (grabZoneOptions.empty()) {
        grabZoneHysteresisIndex = -1;
        return -1;
    }

    // Plain nearest-midpoint bucket, recomputed fresh every call - this is what
    // computeGrabZoneWallForce()'s own boundary math is centered on too.
    size_t rawIndex = 0;
    while (rawIndex + 1 < grabZoneOptions.size()) {
        float midpoint = (grabZoneOptions[rawIndex].zCenter + grabZoneOptions[rawIndex + 1].zCenter) / 2.0f;
        if (currentZ <= midpoint) break;
        rawIndex++;
    }

    if (grabZoneHysteresisIndex < 0 || grabZoneHysteresisIndex >= (int)grabZoneOptions.size()) {
        // First call this grab (see buildGrabZoneOptions()'s reset) - nothing to be sticky
        // against yet, so snap directly to wherever the stylus already is.
        grabZoneHysteresisIndex = (int)rawIndex;
        return grabZoneOptions[grabZoneHysteresisIndex].slot;
    }

    // Sticky: only actually move away from the currently-classified option once the stylus is
    // GRAB_ZONE_CROSSING_MARGIN past THAT option's own boundary midpoint on the new side - not
    // right at the knife-edge midpoint itself. See this method's header comment for why (keeps
    // the narrated option change aligned with computeGrabZoneWallForce()'s felt resistance,
    // rather than announcing early). Only steps one bucket per call - at haptic framerate the
    // stylus can't outrun that within a single grab.
    if (rawIndex > (size_t)grabZoneHysteresisIndex) {
        float boundary = (grabZoneOptions[grabZoneHysteresisIndex].zCenter + grabZoneOptions[grabZoneHysteresisIndex + 1].zCenter) / 2.0f;
        if (currentZ > boundary + GRAB_ZONE_CROSSING_MARGIN) {
            grabZoneHysteresisIndex++;
        }
    }
    else if (rawIndex < (size_t)grabZoneHysteresisIndex) {
        float boundary = (grabZoneOptions[grabZoneHysteresisIndex - 1].zCenter + grabZoneOptions[grabZoneHysteresisIndex].zCenter) / 2.0f;
        if (currentZ < boundary - GRAB_ZONE_CROSSING_MARGIN) {
            grabZoneHysteresisIndex--;
        }
    }

    return grabZoneOptions[grabZoneHysteresisIndex].slot;
}

Vector3 WindowManager::computeGrabZoneWallForce(float currentZ) {
    // A "pop-through" resistance felt as the stylus crosses between adjacent grab-zone options
    // (Slot 1 / Slot 2 / the archived list) while actively dragging. During ordinary
    // (non-grab) browsing, this same kind of boundary "click" comes for free from each active
    // WindowWallObject's own front/back wall detection - but for the duration of a grab, the
    // real WindowWallObjects are swapped out for plain GravityWellObject placeholders (see
    // initiatePeriodicScanner()), and FFUIDesktop.cpp's isGrabbingWindow force branch bypasses
    // processForces() entirely (using computeGrabDetentForce() + processBoundaryForces()
    // instead) - so nothing was producing that between-slot pop feel while a drag was actually
    // in progress. "We still need to re-introduce the layer pop-throughs (between slot1/slot2
    // etc) when dragging a program from the front", per the request.
    //
    // Reuses grabZoneOptions - the same real, just-scanned Z positions, in the same sorted
    // order, that classifyGrabZone() already partitions by midpoint for the live "which option"
    // narration - so the wall sits exactly where the narration already treats as a crossing,
    // rather than a separately-tuned position that could drift out of sync with it. Same
    // two-sided-spring shape (and the same WALL_STIFFNESS/WALL_FORCE_LIMIT values) as
    // computeSectionDividerForce()'s own pop-through wall between Program Tray and FFUI
    // Settings - see that method's own comment for why a plain two-sided spring, computed
    // directly in world space, was chosen over a SolidPlane/local-frame object.
    //
    // This is a deliberate, requested exception to computeGrabDetentForce()'s own "force.z
    // always exactly 0, so the whole Z axis is a free rail" design - the rail stays free
    // between boundaries, this only adds a felt "click" right at each crossing, the same way
    // the horizontal slider's own notch effect adds periodic texture to an otherwise-free axis.
    Vector3 force(0, 0, 0);

    if (grabZoneOptions.size() < 2) return force;

    static constexpr float WALL_HALF_THICKNESS = 8.0f; //world units either side of each boundary the wall is felt across - unverified, tune by feel
    static constexpr float WALL_STIFFNESS = 0.001f;     //matches computeSectionDividerForce()'s own wall
    static constexpr float WALL_FORCE_LIMIT = 0.005f;   //matches computeSectionDividerForce()'s own wall

    for (size_t i = 0; i + 1 < grabZoneOptions.size(); i++) {
        float boundary = (grabZoneOptions[i].zCenter + grabZoneOptions[i + 1].zCenter) / 2.0f;
        float offset = currentZ - boundary;
        float absOffset = std::abs(offset);
        if (absOffset < WALL_HALF_THICKNESS) {
            // A "hump" you have to push through, not a well that sucks you toward the boundary:
            // reaction is STRONGEST right at the boundary itself (absOffset == 0) and fades to
            // zero at the band's own edges, and always points AWAY from the boundary in
            // whichever direction currentZ already leans (same sign as offset) - this was
            // previously `-offset * WALL_STIFFNESS`, which is a restoring spring back TOWARD the
            // boundary (strongest at the edges, zero at the center) - the exact opposite feel,
            // reported as "presenting as detents (sucking towards the border), but should be
            // presenting as pop-through walls (push away from the border)", per the request.
            float magnitude = WALL_STIFFNESS * (WALL_HALF_THICKNESS - absOffset);
            float reaction = std::copysign(magnitude, offset);
            if (reaction > WALL_FORCE_LIMIT) reaction = WALL_FORCE_LIMIT;
            if (reaction < -WALL_FORCE_LIMIT) reaction = -WALL_FORCE_LIMIT;
            force.z += reaction;
        }
    }

    return force;
}

HWND WindowManager::getWindowHandleInSlot(int slot) {
    // Same reasoning as getSlotForWindow()'s own comment - this iterates windowWallSlotsMap
    // directly, so it needs the exact same protection against the scanner thread's locked
    // writes, and for the same reason (several existing unlocked callers - loadSlot()'s own top
    // section, the drop-handler in FFUIDesktop::updateFrame(), and now the room-crossing logic
    // there too).
    std::lock_guard<std::recursive_mutex> lock(windowMutex);
    for (const auto& pair : windowWallSlotsMap) {
        if (pair.second == slot) return pair.first;
    }
    return NULL;
}



Vector3 WindowWallObject::calculateInteractionForce(Location localLoc) {
    Vector3 force(0, 0, 0);
    //printf("title: %s\n", objectMeta.customName);
  //  printf("%f \n", localLoc.position.x);
    Vector3 stylusPosition = localLoc.position;

    float halfX = objectMeta.scale.x * 0.5f;
    float halfY = objectMeta.scale.y * 0.5f;
    float halfZ = objectMeta.scale.z * 0.5f;
    bool withinX = std::abs(stylusPosition.x) < halfX;
    bool withinY = std::abs(stylusPosition.y) < halfY;
    bool withinZ = std::abs(stylusPosition.z) < halfZ;

    float frontWallEdge = halfY;
    float backWallEdge = -halfY;


    //if (!withinX || !withinZ) {
    //    return force;
    //}

    windowMeta.stylusOnThis = false;

    if (isArchived()) {

        if (withinX && withinY && withinZ){
            //printf("inside \n");
            //std::cout << objectMeta.customName << std::endl;
            //printf("done\n");
            windowMeta.stylusOnThis = true;

            // Uses the EXACT same dead-zone-plus-stiffness detent formula AND the same
            // DetentTuning.h constants (base stiffness, force limit, dead zone fraction,
            // stiffness gain, stability floor) as GridTileObject's own tiles (FFUI Settings and
            // every MenuSystem menu item) - "I want the edges / detents to feel consistent with
            // the other menus we've been working on (use the same settings)", per the request.
            //
            // Originally (this same round) this reused objectMeta.hapticSolidProperties'
            // stiffness/solidForceLimit instead - but those come from
            // ObjectFactory::getHapticPropsOfType(UIElementType::Window) (0.0015f / 0.006f),
            // tuned for a completely different interaction (the active-slot front wall you
            // physically push through), nearly double FFUI Settings/MenuSystem's own 0.0008f /
            // 0.0025f. That mismatch - not the dead-zone/gain/floor shape, which already
            // matched - is what made Program Tray feel noticeably harder than the menu tiles
            // it was meant to match: "the program tiles are a bit better there, but the
            // detents are a bit too hard now - is there a global setting somewhere I can
            // change them?", per the request. DetentTuning.h is that single place now - see
            // its own header comment.
            //
            // The pull is on world X (local.x, unchanged) and world Y (local.z here, per this
            // object's own 90-degree rotation - see the constructor's own comment on the axis
            // mapping); local.y (world Z, the shelf's own fixed depth) stays at zero force,
            // same as before - it's only ever a bounding gate (withinY above), never a
            // restoring axis, matching how GridTileObject also leaves its own third axis at
            // zero.
            float maxForce = DetentTuning::FORCE_LIMIT;
            float minDimension = (std::min)(objectMeta.scale.x, objectMeta.scale.z);
            float stabilityFactor = (std::max)(minDimension, DetentTuning::STABILITY_FLOOR);
            float effectiveStiffness = (DetentTuning::BASE_STIFFNESS * DetentTuning::STIFFNESS_GAIN) / stabilityFactor;

            float deadzoneX = halfX * DetentTuning::DEADZONE_FRACTION;
            float deadzoneRow = halfZ * DetentTuning::DEADZONE_FRACTION;

            float pullX = windowWallApplyDeadzone(stylusPosition.x, deadzoneX);
            float pullRow = windowWallApplyDeadzone(stylusPosition.z, deadzoneRow);

            force = Vector3(-pullX * effectiveStiffness, 0.0f, -pullRow * effectiveStiffness);

            if (force.length() > maxForce) {
                force *= maxForce / force.length();
            }

            // Narration itself no longer happens here - this per-object, per-tick pass is now
            // ONLY responsible for the geometry/hit-test result (windowMeta.stylusOnThis, just
            // above) and this object's own force. WindowManager::updateWindowNarration(), called
            // exactly once per haptic tick (see its own comment for why), is the single place
            // that decides what - if anything - actually gets narrated this tick, reading
            // stylusOnThis back off ActiveWindows/ArchivedWindows after every object's own pass
            // has finished. See that method's comment for the full rationale.
        }
        else {
            // No explicit resetNarratedWindow() call needed here either, for the same reason -
            // updateWindowNarration() handles the "nothing is currently on any window" case
            // centrally, once per tick, rather than each non-hit object separately asking to be
            // forgotten.
        }
    }
    else {

        // Approach-zone threshold widened from a single roomDepth to ACTIVE_ZONE_ROOM_COUNT worth
        // of roomDepth - the merged active room is now ACTIVE_ZONE_ROOM_COUNT rooms deep (see
        // WindowManager::ACTIVE_ZONE_ROOM_COUNT's own comment), so this zone needs to cover that
        // whole depth to keep triggering auto-focus-on-approach anywhere within the room, not
        // just its old single-slot-sized inner portion. No force or side effect of any kind is
        // rendered in this zone any more - see the wall branch's own comment for where the OS
        // focus-switch and narration decision actually live now (FFUIDesktop::updateFrame(), not
        // any per-object code at all).
        if (stylusPosition.y < (WindowManager::ACTIVE_ZONE_ROOM_COUNT * roomDepth) && stylusPosition.y > frontWallEdge) {
            //  printf("%f\n", stylusPosition.y);
        }
        else if (stylusPosition.y < frontWallEdge && stylusPosition.y > backWallEdge) {

            float penetrationDepth = 0;

            if (stylusPosition.y < 0) {
                // Past the wall's own center-plane (y=0) - this is the genuine "pop-through"
                // point: penetrationDepth is measured from backWallEdge from here on (instead of
                // frontWallEdge, just below), which flips reactionForce's sign so the wall stops
                // resisting entry and instead assists the rest of the way through, the same way a
                // snap-through detent would. windowMeta.stylusOnThis is set true here, same as
                // it always has been - computeFrontZoneMagnetism()/computeSectionDividerForce()
                // both still rely on this exact Y-only reach to know when to defer their own
                // force entirely to this wall (see their own comments) - this flag's meaning for
                // FORCE purposes is unchanged.
                //
                // What no longer lives here: any OS focus-switch or narration decision. Several
                // rounds of trying to gate those correctly from THIS object - a Y-only margin, an
                // X/Z-gated companion flag, isFocused()/hasPendingExplicitFocus bookkeeping -
                // kept failing because they were all reading state that either doesn't reset
                // cleanly across scan cycles (isFocused() persists via FFUIDesktop.cpp's own
                // "carry the same focus forward" logic regardless of stylus position) or depends
                // on this object's own local axes, which a 90-degree rotation (see this class's
                // constructor) makes non-obvious to reason about from here. "There's no need for
                // this to be attached to an object at all - as top level behaviour this can be
                // handled in the main program loop", per the request - see
                // FFUIDesktop::updateFrame()'s own comment for the replacement: a plain world-Z
                // hysteresis band around this room's fixed wall-center position, owned by a single
                // WindowManager::isControllingActiveRoom flag, with no per-object involvement.
                windowMeta.stylusOnThis = true;
                penetrationDepth = stylusPosition.y - backWallEdge;
            }
            else {
                penetrationDepth = stylusPosition.y - frontWallEdge;
            }
            float reactionForce = -(penetrationDepth)*objectMeta.hapticSolidProperties.stiffness;
            force.y = reactionForce;

            if (force.y > objectMeta.hapticSolidProperties.solidForceLimit) force.y = objectMeta.hapticSolidProperties.solidForceLimit;
            if (force.y < -objectMeta.hapticSolidProperties.solidForceLimit) force.y = -objectMeta.hapticSolidProperties.solidForceLimit;
        }
        else {
            // No explicit resetNarratedWindow() call needed here either - see the archived
            // branch's matching comment above; narration for this room is decided entirely by
            // FFUIDesktop::updateFrame()'s top-level logic now, not per-object.
        }
    }

    

    return force;
}




void WindowManager::bringWindowToFrontByHandle(HWND hwnd) {
    if (hwnd == NULL) return;

    HWND hCurWnd = GetForegroundWindow();
    DWORD dwMyID = GetCurrentThreadId();
    DWORD dwCurID = GetWindowThreadProcessId(hCurWnd, NULL);

    AttachThreadInput(dwCurID, dwMyID, TRUE);

    //Swapping to an application maximises it, so its UI elements always occupy the whole
    //screen. That keeps the mapping from screen space to the haptic workspace consistent
    //between applications - a button is in the same place in the virtual environment
    //regardless of what size the user last left the window at.
    //Windows with no maximise box or no resizable frame (dialogs, fixed size tool windows)
    //are left alone, since forcing those to maximise misplaces or mis-sizes them.
    LONG_PTR windowStyle = GetWindowLongPtr(hwnd, GWL_STYLE);
    bool canMaximise = (windowStyle & WS_MAXIMIZEBOX) && (windowStyle & WS_THICKFRAME);

    if (canMaximise) {
        //SW_MAXIMIZE also un-minimises, so this covers the iconic case too.
        if (!IsZoomed(hwnd)) ShowWindow(hwnd, SW_MAXIMIZE);
    }
    else if (IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
    }

    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_SHOWWINDOW | SWP_NOSIZE | SWP_NOMOVE);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    SetActiveWindow(hwnd);

    AttachThreadInput(dwCurID, dwMyID, FALSE);
}

void WindowManager::loadSlot(int slotIndex, const std::wstring& announcementPrefix) {
    if (numOfActiveWindows <= 0) return;

    // Normalize slotIndex into [0, numOfActiveWindows) first - handles both an already-in-range
    // index (the common case) and cycleActiveSlot()'s currentActiveSlotIndex+1, which can equal
    // numOfActiveWindows exactly. The extra add-then-mod step also guards against a negative
    // slotIndex (defensive - nothing currently passes one).
    int candidate = ((slotIndex % numOfActiveWindows) + numOfActiveWindows) % numOfActiveWindows;

    // Always re-derived fresh from windowWallSlotsMap right here, never trusted from a caller -
    // see this method's own header comment for why. If candidate itself is empty, search forward
    // (wrapping around at most once, via the attempts bound below) for the next occupied slot -
    // "checks if the slot is set, if not moves to the next one", per the request.
    HWND hwnd = getWindowHandleInSlot(candidate);
    int attempts = 0;
    while (hwnd == NULL && attempts < numOfActiveWindows) {
        candidate = (candidate + 1) % numOfActiveWindows;
        hwnd = getWindowHandleInSlot(candidate);
        attempts++;
    }

    // Every quickslot is empty - nothing to load. Leave currentActiveSlotIndex and whatever's
    // currently shown untouched rather than narrating "empty" for no reason.
    if (hwnd == NULL) return;

    // OS-level maximise+foreground switch happens immediately, synchronously - it only needs the
    // raw handle (already in windowWallSlotsMap), not a live WindowWallObject for the new slot,
    // which won't exist until the scanner thread's next cycle. See bringWindowToFrontByHandle's
    // own comment.
    bringWindowToFrontByHandle(hwnd);

    {
        std::lock_guard<std::recursive_mutex> lock(windowMutex);
        currentActiveSlotIndex = candidate;
        // The slot that was current a moment ago is about to stop being rendered at all (see
        // ObjectFactory::createObjectsFromUIElements's skip-if-not-current branch) - clear its
        // isFocused() now rather than waiting for that to happen implicitly next scan.
        for (auto* window : ActiveWindows) {
            window->setFocused(false);
        }
        // Consumed once by initiatePeriodicScanner()'s focus-reapply block, the moment the new
        // slot's WindowWallObject actually gets constructed - see hasPendingExplicitFocus's own
        // comment.
        hasPendingExplicitFocus = true;
        pendingExplicitFocusHandle = hwnd;
    }

    // Single debug line per the request ("just the events 'button pressed', 'loaded slot n'") -
    // everything loadSlot() actually settled on, in one place, after it's all decided.
    {
        std::wstring appNameDbg = getShortAppName(hwnd);
        std::string appNameNarrowDbg(appNameDbg.begin(), appNameDbg.end());
        std::cout << "[Slot] loaded slot " << (candidate + 1) << ": " << appNameNarrowDbg
            << " (hwnd " << hwnd << ")" << std::endl;
    }

    narrateSlotSwitch(candidate, hwnd, announcementPrefix);
}

void WindowManager::cycleActiveSlot() {
    if (numOfActiveWindows <= 0) return;

    // loadSlot() itself handles normalizing this into range, skipping past any empty slot it
    // lands on, and narrating whatever it actually settles on.
    loadSlot(currentActiveSlotIndex + 1);
}

void WindowManager::narrateSlotSwitch(int slotIndex, HWND hwnd, const std::wstring& announcementPrefix) {
    std::wstring toSpeak = announcementPrefix + L"Slot " + std::to_wstring(slotIndex + 1) + L", "
        + (hwnd != NULL ? getShortAppName(hwnd) : L"empty");

    if (pSapiVoice) {
        std::string toSpeakNarrow(toSpeak.begin(), toSpeak.end());
        std::cout << "[Narrate:SlotSwitch] \"" << toSpeakNarrow << "\"" << std::endl;
        SpeakFfuiNarration(toSpeak.c_str());
    }

    // Mirrors narrateWindowFocus()'s own bookkeeping (lastNarratedWindowHandle/lastNarratedLayer)
    // so the normal per-frame narration path - which will run once the new slot's live
    // WindowWallObject appears on the next scan and the stylus is already sitting in its solid-
    // wall band - doesn't immediately re-announce the exact same thing a second time.
    lastNarratedWindowHandle = hwnd;
    lastNarratedLayer = slotIndex;
    lastNarratedSettingsTileIndex = -1;
}

void WindowManager::setActiveSlotCount(int count) {
    std::lock_guard<std::recursive_mutex> lock(windowMutex);
    numOfActiveWindows = count;
    // Clamp to the new highest valid slot rather than resetting to 0 - lowering the count should
    // never silently jump the user to a different, unrelated window while their current one is
    // still in range.
    if (currentActiveSlotIndex >= numOfActiveWindows) {
        currentActiveSlotIndex = numOfActiveWindows - 1;
    }
    if (currentActiveSlotIndex < 0) {
        currentActiveSlotIndex = 0;
    }
}

void WindowManager::assignWindowToSlot(HWND hwnd, int slot) {
    if (hwnd == NULL) return;

    std::lock_guard<std::recursive_mutex> lock(windowMutex);

    // If some OTHER window currently occupies slot, bump IT to the back of the archived tray
    // (a fresh slot number) rather than attempting a two-way swap - hwnd has no prior slot of
    // its own to swap into, since it's either brand new to windowWallSlotsMap or (defensively)
    // already had some other slot that's about to be overwritten below either way.
    for (auto& pair : windowWallSlotsMap) {
        if (pair.first != hwnd && pair.second == slot) {
            pair.second = nextAvailableSlot;
            nextAvailableSlot++;
            break;   // slots are unique by construction - at most one prior occupant
        }
    }

    windowWallSlotsMap[hwnd] = slot;
}

void WindowManager::moveArchviedToActive(WindowWallObject* mainWindow) {

    if (!mainWindow->isArchived()) return;



}

HWND WindowManager::getHandleOfTheWindowTheStylusIsOn(std::mutex& objectsListMutex) {

    std::scoped_lock doubleLock(objectsListMutex, WindowManager::getInstance().windowMutex);


    //Merge all windows in one list to iterate over all of them
    std::vector<WindowWallObject*> allWindows;
    allWindows.reserve(WindowManager::getInstance().ActiveWindows.size()
        + WindowManager::getInstance().ArchivedWindows.size());
    allWindows.insert(allWindows.end(),
        WindowManager::getInstance().ActiveWindows.begin(),
        WindowManager::getInstance().ActiveWindows.end());

    allWindows.insert(allWindows.end(),
        WindowManager::getInstance().ArchivedWindows.begin(),
        WindowManager::getInstance().ArchivedWindows.end());

    // Both lists are legitimately empty while a grab is in progress (they're cleared in favor
    // of GravityWellObject stand-ins - see the other overload below), so that's not worth
    // logging on every frame.

    HWND handelOfFoundWindow = NULL;

    for (WindowWallObject* window : allWindows) {
        if (window == nullptr) continue;

        if (window->stylusIsOnThis()) {
            handelOfFoundWindow = window->getHandle();
        }
    }

    return handelOfFoundWindow;
}


HWND WindowManager::getHandleOfTheWindowTheStylusIsOn(std::mutex& objectsListMutex, const std::vector<std::unique_ptr<FFUIObject>>& objectsList){


    std::scoped_lock lock(objectsListMutex);

    HWND handelOfFoundWindow = NULL;

    for (const auto& objPtr : objectsList) {
        //We only care about the gravity wells 
        if (objPtr->getMeta().uiType != UIElementType::GravityWell) continue;

        GravityWellObject* gravityWell = dynamic_cast<GravityWellObject*>(objPtr.get());

        if (gravityWell != nullptr && gravityWell->correspondingWindowMeta.stylusOnThis) {
            handelOfFoundWindow = gravityWell->correspondingWindowMeta.windowHandle;
        }
    }

    return handelOfFoundWindow;
}


void WindowManager::swapWindowSlots(HWND grabbedWindow, HWND targetWindow) {
    std::lock_guard<std::recursive_mutex> lock(windowMutex);
    
    //Ensure both windows exist in the map to prevent creating junk keys
    if (windowWallSlotsMap.find(grabbedWindow) != windowWallSlotsMap.end() &&
        windowWallSlotsMap.find(targetWindow) != windowWallSlotsMap.end()) {

        //Swap the integer slots
        int tempSlot = windowWallSlotsMap[grabbedWindow];
        windowWallSlotsMap[grabbedWindow] = windowWallSlotsMap[targetWindow];   
        windowWallSlotsMap[targetWindow] = tempSlot;

        //The next time the window scanner runs it will read these 
        //updated slots and generate the positions in their new locations
        //based on the new slots.
    }
}

void WindowManager::narrateSettingsTileFocus(int tileIndex, const std::wstring& label) {
    // Already the last thing narrated - nothing has changed, stay silent. Also clears any
    // pending "off" streak resetNarratedSettingsTile() may have started for this same tile -
    // see narratedSettingsTileOffStreak's own comment.
    if (tileIndex == lastNarratedSettingsTileIndex) {
        narratedSettingsTileOffStreak = 0;
        return;
    }

    std::wstring toSpeak = (lastNarratedLayer != SETTINGS_LAYER_ID)
        ? (L"FFUI settings, " + label)
        : label;

    if (pSapiVoice && !toSpeak.empty()) {
        std::string toSpeakNarrow(toSpeak.begin(), toSpeak.end());
        std::cout << "[Narrate:SettingsTile] \"" << toSpeakNarrow << "\"" << std::endl;
        SpeakFfuiNarration(toSpeak.c_str());
    }

    lastNarratedLayer = SETTINGS_LAYER_ID;
    lastNarratedSettingsTileIndex = tileIndex;
    narratedSettingsTileOffStreak = 0;
    // Entering Settings always supersedes whichever window was last narrated (mirrors
    // narrateWindowFocus()'s own reset of lastNarratedSettingsTileIndex, the other direction).
    lastNarratedWindowHandle = nullptr;
}

void WindowManager::resetNarratedSettingsTile() {
    // Same hysteresis as resetNarratedWindow() - see narratedSettingsTileOffStreak's own
    // comment - "the layers seem to have a similar issue to earlier - where it can flip back
    // and forth when near the boundary", per the request.
    if (lastNarratedSettingsTileIndex < 0) return;  // already "nothing" - no streak to track
    narratedSettingsTileOffStreak++;
    if (narratedSettingsTileOffStreak >= NARRATION_HYSTERESIS_FRAMES) {
        lastNarratedSettingsTileIndex = -1;
        narratedSettingsTileOffStreak = 0;
    }
}

void WindowManager::updateWindowNarration() {
    // Program Tray narration only, now - the merged active room's "Slot N, {app name}"
    // announcement is no longer decided per-tick from here at all. Every previous attempt at
    // gating that decision from *some* per-object or per-tick signal - isFocused() plus a Y-only
    // margin, isFocused() plus an additionally X/Z-gated hasPoppedThrough() flag, a coupling to
    // FFUIDesktop.cpp's own inFrontZone - kept either firing early (isFocused() persists across
    // scan cycles regardless of stylus position; the room's Y-only geometry has no X/Z gating and
    // can read "on" while genuinely over a different-X tray tile at a similar Z depth) or broke
    // ordinary narration outright when tightened. "There's no need for this to be attached to an
    // object at all - as top level behaviour this can be handled in the main program loop", per
    // the request - the room's narration (and the OS focus-switch that goes with it) now happens
    // exactly once, on the upward crossing, directly in FFUIDesktop::updateFrame()'s own top-level
    // world-Z hysteresis logic (see its own comment) - not scanned for here every tick at all.
    std::lock_guard<std::recursive_mutex> lock(windowMutex);

    // Program Tray (archived) items have no isFocused()/"current slot" concept of their own -
    // any tile the stylus is physically touching is narrated, same as always. This was never part
    // of the reported bug and is unaffected by the room-narration change above.
    for (WindowWallObject* window : ArchivedWindows) {
        if (window != nullptr && window->stylusIsOnThis()) {
            narrateWindowFocus(window->getHandle());
            return;
        }
    }

    // Nothing is currently on any Program Tray tile this tick. resetNarratedWindow() only
    // advances its hysteresis streak when its argument already matches lastNarratedWindowHandle,
    // so passing lastNarratedWindowHandle itself here is a safe, deliberate no-op when nothing was
    // narrated to begin with, and otherwise correctly starts/continues the "was on, now off"
    // countdown.
    resetNarratedWindow(lastNarratedWindowHandle);
}

void WindowManager::updateFrontZoneSectionNarration(Location stylusLoc) {
    bool inBand = std::abs(stylusLoc.position.z - frontZoneZCenter) < frontZoneZHalfThickness;
    if (!inBand) return;  // outside the front zone entirely - FFUI Settings tiles are the only thing this function narrates; windows/Program Tray are handled centrally by updateWindowNarration() above

    if (stylusLoc.position.x < 0.0f) {
        // FFUI Settings section (left half). Locked, unlike classifyGrabZone()'s own per-frame
        // read elsewhere in this class - SettingsTiles is reassigned wholesale by the scanner
        // thread each cycle (see FFUIDesktop::initiatePeriodicScanner()), so iterating it here
        // without windowMutex would be a real (if rare) use-after-free window, not just a stale
        // read.
        std::lock_guard<std::recursive_mutex> lock(windowMutex);
        GridTileObject* highlighted = nullptr;
        for (GridTileObject* tile : SettingsTiles) {
            if (tile != nullptr && tile->stylusIsOnThis()) {
                highlighted = tile;
                break;
            }
        }
        if (highlighted != nullptr) {
            narrateSettingsTileFocus(highlighted->getTileIndex(), highlighted->getLabel());
        }
        else {
            resetNarratedSettingsTile();
        }
    }
    // Program Tray side (right half, x >= 0) needs no explicit call here - it's covered by
    // updateWindowNarration() above, alongside the merged active room.
}

Vector3 WindowManager::computeFrontZoneMagnetism(Location stylusLoc) {
    Vector3 force(0, 0, 0);

    bool inBand = std::abs(stylusLoc.position.z - frontZoneZCenter) < frontZoneZHalfThickness;
    if (!inBand) return force;

    static constexpr float MAGNETISM_STIFFNESS = 0.0003f;
    static constexpr float MAGNETISM_MAX_FORCE = 0.0012f;

    bool inSettingsSection = stylusLoc.position.x < 0.0f;

    // Is anything in THIS section already highlighted? If so, its own per-tile detent is
    // already doing the work - this light magnetism only kicks in when nothing is. Locked for
    // the same reason as updateFrontZoneSectionNarration() above - both SettingsTiles and
    // ArchivedWindows are reassigned wholesale by the scanner thread each cycle.
    std::lock_guard<std::recursive_mutex> lock(windowMutex);

    // The merged active room's own wall sits (deliberately) right at the boundary nearest the
    // front zone - see ObjectFactory::createObjectsFromUIElements's zPosSlotIndex comment - and
    // WindowWallObject::calculateInteractionForce's wall/approach-zone branch has no X/Z gating
    // of its own (it only ever checks local Y, i.e. world Z depth), so a thin sliver of ITS OWN
    // force+narration range spills into the front zone's own Z-band regardless of which section
    // (Settings/Tray) the stylus is nominally over. Defer to it entirely there, same as the
    // stylusIsOnThis() guards against SettingsTiles/ArchivedWindows just below - without this,
    // the room's wall push and this magnetism fought each other right at that shared boundary
    // ("pulling toward two different things... persists until crossing the zone boundary", per
    // the request).
    for (WindowWallObject* activeRoom : ActiveWindows) {
        if (activeRoom != nullptr && activeRoom->stylusIsOnThis()) return force;
    }

    bool somethingHighlighted = false;
    float nearestX = 0.0f, nearestY = 0.0f;
    bool haveCandidate = false;
    float bestDist = (std::numeric_limits<float>::max)();

    if (inSettingsSection) {
        for (GridTileObject* tile : SettingsTiles) {
            if (tile == nullptr) continue;
            if (tile->stylusIsOnThis()) { somethingHighlighted = true; break; }

            float dx = tile->getMeta().globalPosition.x - stylusLoc.position.x;
            float dy = tile->getMeta().globalPosition.y - stylusLoc.position.y;
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist < bestDist) {
                bestDist = dist;
                nearestX = tile->getMeta().globalPosition.x;
                nearestY = tile->getMeta().globalPosition.y;
                haveCandidate = true;
            }
        }
    }
    else {
        for (WindowWallObject* window : ArchivedWindows) {
            if (window == nullptr) continue;
            if (window->stylusIsOnThis()) { somethingHighlighted = true; break; }

            float dx = window->getMeta().globalPosition.x - stylusLoc.position.x;
            float dy = window->getMeta().globalPosition.y - stylusLoc.position.y;
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist < bestDist) {
                bestDist = dist;
                nearestX = window->getMeta().globalPosition.x;
                nearestY = window->getMeta().globalPosition.y;
                haveCandidate = true;
            }
        }
    }

    if (somethingHighlighted || !haveCandidate) return force;

    Vector3 toNearest(nearestX - stylusLoc.position.x, nearestY - stylusLoc.position.y, 0.0f);
    force = toNearest * MAGNETISM_STIFFNESS;
    if (force.length() > MAGNETISM_MAX_FORCE) {
        force = force.normalized() * MAGNETISM_MAX_FORCE;
    }
    return force;
}

Vector3 WindowManager::computeSectionDividerForce(Location stylusLoc) {
    // A vertical "pop-through" wall between Program Tray and FFUI Settings, computed directly
    // in world space from stylusLoc.position.x rather than via a SolidPlane/local-frame object.
    // SolidPlane's reaction force is expressed in the object's LOCAL frame and re-mapped to
    // world axes by its own orientation quaternion (see FFUIObject::forceLocalToGlobal) -
    // getting a brand-new interior wall's orientation right, for an object that (unlike every
    // existing SolidPlane instance) needs to repel from BOTH sides rather than gate on a single
    // global sign, felt like unnecessary risk to take on without being able to compile/test it.
    // The one-sided-spring-plus-buzz feel SolidPlane itself has is deliberately simplified away
    // here to a plain two-sided spring - flagged as a real, if minor, simplification worth
    // confirming feels right on real hardware.
    Vector3 force(0, 0, 0);

    bool inBand = std::abs(stylusLoc.position.z - frontZoneZCenter) < frontZoneZHalfThickness;
    if (!inBand) return force;

    static constexpr float WALL_HALF_THICKNESS = 8.0f;  // world units either side of X=0 the wall is felt across - unverified, tune by feel
    static constexpr float WALL_STIFFNESS = 0.001f;      // matches SolidPlane's own default stiffness
    static constexpr float WALL_FORCE_LIMIT = 0.005f;    // matches SolidPlane's own default solidForceLimit

    float x = stylusLoc.position.x;
    if (std::abs(x) >= WALL_HALF_THICKNESS) return force;

    // If the stylus is currently sitting on a real tile (an FFUI Settings tile, or a Program
    // Tray/archived window), that tile's own pull-to-center detent force already owns this
    // position - defer to it entirely rather than layering this wall's own X=0-seeking pull on
    // top of it. Program Tray's column 0 is laid out with its LEFT edge exactly at X=0 (see
    // computeGridCell()'s xRangeMin=0.0f in ObjectsFactory.cpp), so this wall's own
    // +-WALL_HALF_THICKNESS band around X=0 reaches straight into the middle of column 0's own
    // tiles - without this guard, a tile's detent (pulling toward ITS OWN center, away from
    // X=0) and this wall (pulling toward X=0) fought each other right there, felt as "a vertical
    // pop-through wall part way through the first column... doesn't appear on any other
    // columns" (every other column starts well past WALL_HALF_THICKNESS units from X=0, so this
    // band never reaches them at all) - per the request. This is the same kind of force-fight
    // already diagnosed once this feature (see the tile-overlap comment in
    // ObjectFactory::createObjectsFromUIElements's Window case) and guarded the same way
    // computeFrontZoneMagnetism() already guards its own "nothing highlighted yet" magnetism
    // against exactly this kind of double-force fight - "somethingHighlighted" there,
    // stylusIsOnThis() here.
    {
        std::lock_guard<std::recursive_mutex> lock(windowMutex);
        for (GridTileObject* tile : SettingsTiles) {
            if (tile != nullptr && tile->stylusIsOnThis()) return force;
        }
        for (WindowWallObject* window : ArchivedWindows) {
            if (window != nullptr && window->stylusIsOnThis()) return force;
        }
        // Same extension as computeFrontZoneMagnetism()'s own matching guard, and for the same
        // reason - the merged active room's own wall sits right at this boundary and its
        // force/narration branch has no X/Z gating, so without this, this divider wall and the
        // room's own wall push fought each other in the thin Z sliver where both ranges overlap.
        for (WindowWallObject* activeRoom : ActiveWindows) {
            if (activeRoom != nullptr && activeRoom->stylusIsOnThis()) return force;
        }
    }

    // A "hump" you have to push through, not a well that sucks you toward X=0: magnitude is
    // STRONGEST right at X=0 and fades to zero at the band's own edges, and always points AWAY
    // from X=0 in whichever direction x already leans (same sign as x) - this was previously
    // `-x * WALL_STIFFNESS`, a restoring spring back TOWARD X=0 (strongest at the edges, zero at
    // the center) - the exact opposite feel, same bug as computeGrabZoneWallForce()'s own
    // (see that method's own comment) - "may be the same issue as the vertical boundary between
    // FFUI settings & program selector", per the request.
    float absX = std::abs(x);
    float magnitude = WALL_STIFFNESS * (WALL_HALF_THICKNESS - absX);
    float reaction = std::copysign(magnitude, x);
    if (reaction > WALL_FORCE_LIMIT) reaction = WALL_FORCE_LIMIT;
    if (reaction < -WALL_FORCE_LIMIT) reaction = -WALL_FORCE_LIMIT;
    force.x = reaction;

    return force;
}

void WindowManager::removeClosedWindows(const std::vector<HWND>& currentlyOpenWindows) {
    std::lock_guard<std::recursive_mutex> lock(WindowManager::getInstance().windowMutex);

    WindowManager& windowManager = WindowManager::getInstance();


    bool slotsChanged = false;

    // Which HWND (if any) currentActiveSlotIndex currently points to, captured BEFORE any
    // removal/renumbering below - so if slots do get collapsed and renumbered, currentActiveSlotIndex
    // can be corrected afterward to keep following the SAME window, rather than being left as a
    // bare slot NUMBER that may now resolve to a completely different, unrelated window that
    // happened to get renumbered into that same position. This was a real bug: closing ANY
    // window - even one the user never touched, active or archived, anywhere with a lower slot
    // number than the current one - collapses every surviving window's slot number to fill the
    // gap, but currentActiveSlotIndex previously never moved to compensate, so the merged room
    // could silently start showing (and narrating) a completely different program than the one
    // actually last selected. Matches the reported "slot switching" symptoms - a middle-button
    // press that "doesn't always change" anything real, and an unexplained "Slot 3" appearing
    // just from browsing into the front zone and back out, with no cycling in between.
    HWND previouslyActiveHandle = NULL;
    for (const auto& pair : windowManager.windowWallSlotsMap) {
        if (pair.second == windowManager.currentActiveSlotIndex) {
            previouslyActiveHandle = pair.first;
            break;
        }
    }
    // Remove any HWND from the map that is no longer open in Windows
    for (auto it = windowManager.windowWallSlotsMap.begin(); it != windowManager.windowWallSlotsMap.end(); ) {
        HWND mappedHwnd = it->first;

        // Check if mappedHwnd exists in the currentlyOpenWindows list
        auto found = std::find(currentlyOpenWindows.begin(), currentlyOpenWindows.end(), mappedHwnd);

        if (found == currentlyOpenWindows.end()) {
            //The window was closed. Erase it from the map.
            if (mappedHwnd == previouslyActiveHandle) {
                // The active window itself closed - nothing to re-anchor to below. Leaving
                // currentActiveSlotIndex untouched is the right call here: after the collapse
                // below, whichever survivor (if any) shifts down into this same slot number
                // becomes the new "current" window, which reads as a reasonable "moved on to
                // what's next" default - the same way closing a browser tab lands you on
                // whichever tab took its place.
                previouslyActiveHandle = NULL;
            }
            it = windowManager.windowWallSlotsMap.erase(it);
            slotsChanged = true;
        }
        else {
            ++it;
        }
    }

    //If a window was removed, we need to collapse the empty physical slots
    if (slotsChanged) {
        // Extract the surviving windows and their current slots
        std::vector<std::pair<HWND, int>> survivingWindows(windowManager.windowWallSlotsMap.begin(), windowManager.windowWallSlotsMap.end());

        // Sort them by their old slot order so they don't swap places with each other
        std::sort(survivingWindows.begin(), survivingWindows.end(),
            [](const std::pair<HWND, int>& a, const std::pair<HWND, int>& b) {
                return a.second < b.second;
            }
        );

        // Reassigning slots
        windowManager.windowWallSlotsMap.clear();
        int newSlotIndex = 0;
        for (const auto& pair : survivingWindows) {
            windowManager.windowWallSlotsMap[pair.first] = newSlotIndex;
            if (pair.first == previouslyActiveHandle) {
                // Follow the same window to wherever it landed - see previouslyActiveHandle's
                // own comment above for why this is necessary.
                windowManager.currentActiveSlotIndex = newSlotIndex;
            }
            newSlotIndex++;
        }

        // Reset the next available slot for the next time a completely new window opens
        windowManager.nextAvailableSlot = newSlotIndex;
    }
}
