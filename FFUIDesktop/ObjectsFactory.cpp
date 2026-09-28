#include "ObjectsFactory.h"
#include "FFUIDesktop.h"
#include "GravityWellObject.h"
#include <unordered_map>

Vector3 ObjectFactory::screenToWorkspace(Vector2 screenPos, Vector2 screenSize, float zPosition, float workspaceX, float workspaceY)
{

    float x = (screenPos.x / screenSize.x - 0.5f) * workspaceX;
    float y = (0.5f - screenPos.y / screenSize.y) * workspaceY;
    return Vector3(x, y, zPosition);
}

Vector2 ObjectFactory::workspaceToScreen(Vector3 workspacePos, Vector2 screenSize, float workspaceX, float workspaceY)
{
    float screenX = ((workspacePos.x / workspaceX) + 0.5f) * screenSize.x;
    float screenY = (0.5f - (workspacePos.y / workspaceY)) * screenSize.y;

    return Vector2(screenX, screenY);
}

HapticSolidProperties ObjectFactory::getHapticPropsOfType(UIElementType type)
{

    HapticSolidProperties props{};

    switch (type) {
    case UIElementType::Button:
        props.stiffness = 0.001f;
        props.solidForceLimit = 0.0015;
    /*    props.stiffness = 0.00005f;
        props.solidForceLimit = 0.0002;*/
        break;

    case UIElementType::ListItem:
   /*     props.stiffness = 0.00005f;
        props.solidForceLimit = 0.0002;*/

        props.stiffness = 0.0009f;
        props.solidForceLimit = 0.0015;
        break;

    case UIElementType::MenuItem:
        props.stiffness = 0.0007f;
        props.solidForceLimit = 0.0012;

        break;

    case UIElementType::Window:
        props.stiffness = 0.0015f;
        props.solidForceLimit = 0.006;

        break;
    case UIElementType::GravityWell:
        props.stiffness = 0.0008f;
        props.solidForceLimit = 0.005;

    default: return props;

    }
    return props;

}


std::unique_ptr<FFUIObject> ObjectFactory::createGravityWellAtWindowPosition(WindowWallObject* targetWindow) {

    if (!targetWindow) {
        printf("Windows have been removed from memory at the wrong time");
        return nullptr;
    }

    HapticSolidProperties props = getHapticPropsOfType(UIElementType::GravityWell);
    //props.solidForceLimit = 0.0002f;

    //get the window position

    FFUIObject_Meta gravityWellMeta{};
    gravityWellMeta.hapticSolidProperties = props;
    gravityWellMeta.uiType = UIElementType::GravityWell;



    //We set the x and y of the well to be the center of the workspace, and the z position to be target window's.
    float windowZPosition = targetWindow->getMeta().globalPosition.z;
    float windowYPosition = targetWindow->getMeta().globalPosition.y;
    Vector3 scale = targetWindow->getMeta().scale;

    Vector3 slotPosition(0, 0, 0);
    if (targetWindow->isArchived()) {
        //Grab placeholders for archived (Program Tray) windows now need to sit at the same X
        //column their real WindowWallObject occupies, not always X=0 - the tray can be several
        //columns wide now (see the Window case below). Everything else about this placeholder
        //is unchanged from before this feature.
        float windowXPosition = targetWindow->getMeta().globalPosition.x;
        slotPosition = Vector3(windowXPosition, windowYPosition, windowZPosition);
        gravityWellMeta.scale = Vector3(scale.x, scale.z, scale.y) * 0.8;




    }
    else {
        slotPosition = Vector3(0, 0, windowZPosition + 15);
        gravityWellMeta.scale = Vector3(roomDepth * 2, roomDepth * 1.5, 20);

    }

    gravityWellMeta.globalPosition = slotPosition;
    gravityWellMeta.orientation = Quaternion().setFromEuler(1, 0, 0);
    gravityWellMeta.customName = targetWindow->getMeta().customName + " " + "place holder";

    auto builtGravityWell = std::make_unique<GravityWellObject>(gravityWellMeta);

    builtGravityWell->correspondingWindowMeta = targetWindow->getWindowMeta();
    return builtGravityWell;
}

FFUIObject_Meta ObjectFactory::createDemoObject() {

    HapticSolidProperties props{};
    props.stiffness = 0.0002f;
    props.solidForceLimit = 0.004;
    //props.solidForceLimit = 0.0002f;

    // Example: center button with no rotation
    FFUIObject_Meta buttonMeta{};
    buttonMeta.hapticSolidProperties = props;


    buttonMeta.globalPosition = Vector3(0, 0, 50);
    buttonMeta.scale = Vector3(5, 5, 2000);
    buttonMeta.orientation = Quaternion().setFromEuler(1, 0, 0);
    buttonMeta.customName = "3D Center Button";

    return buttonMeta;
}


bool dublicatePositionsExist(std::vector<std::unique_ptr<FFUIObject>>& alreadyCreatedObjects, FFUIObject_Meta& objectMeta) {
    for (const auto& object : alreadyCreatedObjects) {
        float diognalOfWorkspace = std::sqrt(DEVICE_WORKSPACE_X * DEVICE_WORKSPACE_X
            + DEVICE_WORKSPACE_Y * DEVICE_WORKSPACE_Y);
        float overlapingThreshold = diognalOfWorkspace * 0.00768;
        if ((objectMeta.globalPosition - object->getMeta().globalPosition).length() < overlapingThreshold)
            return true;
    }
    return false;

}

ObjectFactory::FrontZoneBand ObjectFactory::computeFrontZoneBand() {
    // Identical arithmetic to what the old single-column archived-list code used to compute
    // inline, per-window, inside the Window case below - hoisted out here so it can be computed
    // exactly once per scan (independent of whether any window is actually archived right now,
    // so FFUI Settings' tiles and the section divider wall have a stable Z-band to sit in even
    // when the tray is empty), and shared by the Program Tray, FFUI Settings, and MenuSystem's
    // own full-width menus alike.
    FrontZoneBand band{};

    // FIXED at ACTIVE_ZONE_ROOM_COUNT, not the user's own numOfActiveWindows quickslot-count
    // setting - the front zone's physical Z position must stay exactly where it's always been
    // ("filling the space of the current 2 slots", per the request) no matter how many
    // quickslots the user configures. See ACTIVE_ZONE_ROOM_COUNT's own comment.
    int maxActive = WindowManager::ACTIVE_ZONE_ROOM_COUNT;
    float inititalRoomMargin = 5.0f;
    float wallsThickness = 10.0f;

    float archivedWindowsZPosition = endZ - inititalRoomMargin - maxActive * roomDepth - wallsThickness;
    float slotsThickness = archivedWindowsZPosition - startZ;
    if (slotsThickness < 5) {
        printf("No space available for the front zone (settings/tray); reduce the number of active windows or their room depth\n");
    }
    // Shift the position further such that the band is exactly in the middle of the gap.
    archivedWindowsZPosition -= slotsThickness / 2.0f;

    band.zCenter = archivedWindowsZPosition;
    band.zHalfThickness = slotsThickness / 2.0f;
    return band;
}

ObjectFactory::GridCell ObjectFactory::computeGridCell(int index, int totalCount, float xRangeMin, float xRangeMax, float zPos, int maxRows) {
    GridCell cell{};

    if (totalCount < 1) totalCount = 1;
    if (maxRows < 1) maxRows = 1;
    if (index < 0) index = 0;
    if (index >= totalCount) index = totalCount - 1;

    int totalCols = (totalCount + maxRows - 1) / maxRows;  // ceil(totalCount / maxRows)
    if (totalCols < 1) totalCols = 1;

    int row = index % maxRows;
    int col = index / maxRows;

    const float initialSlotMargin = 10.0f;
    const float rowHeight = 30.0f;   // matches the fixed row spacing this app has always used (DEVICE_WORKSPACE_Y / 6)

    float xSpan = xRangeMax - xRangeMin;
    float colWidth = (totalCols > 0) ? (xSpan / totalCols) : xSpan;

    cell.cellWidth = colWidth;
    cell.cellHeight = rowHeight;
    cell.position = Vector3(
        xRangeMin + colWidth * (col + 0.5f),
        DEVICE_WORKSPACE_Y / 2.0f - initialSlotMargin - rowHeight * row,
        zPos
    );

    return cell;
}

ObjectFactory::GridCell ObjectFactory::computeMenuCell(int index, int totalCount, float zPos, int maxRows) {
    GridCell cell{};

    if (maxRows < 1) maxRows = 1;
    if (index < 0) index = 0;

    const float rowHeight = 30.0f;   // matches every other grid's row spacing in this app
    const float colPitch = 70.0f;    // only matters once a menu has more than maxRows items

    int row = index % maxRows;
    int col = index / maxRows;

    cell.cellWidth = colPitch;
    cell.cellHeight = rowHeight;
    // col 0 sits at X=0 and row 0 at Y=0 by construction, so index 0 is always exactly
    // (0, 0, zPos) - see this method's own header comment for why.
    cell.position = Vector3(colPitch * col, -rowHeight * row, zPos);

    return cell;
}

std::vector<std::unique_ptr<FFUIObject>> ObjectFactory::createObjectsFromUIElements(std::vector<ScannedUIElement>& scannedElements,
    FFUIDesktop_Config config,
    std::vector<WindowWallObject*>& tempActiveWindows,
    std::vector<WindowWallObject*>& tempArchivedWindows)
{
    std::vector<std::unique_ptr<FFUIObject>> theCreatedUIObjects;

    int maxActive = WindowManager::getInstance().numOfActiveWindows;

    // --- Pass 1: resolve/assign every Window element's physical slot up front. The Program
    // Tray's grid layout below needs to know the TOTAL number of archived windows before it can
    // place any single one of them in a column, so slot assignment (previously done inline,
    // one window at a time, inside the main loop below) has to happen for every window first -
    // otherwise a window scanned early in this cycle could get placed using an undercounted
    // total (windows scanned later that turn out to be brand new, bumping the count further,
    // hadn't been assigned yet). This is the same assignment logic the old single-column code
    // used to do inline; only the timing (all up front, not lazily) changed.
    // Locked for the whole read/write pass below (through archivedCount's own read of
    // nextAvailableSlot just after) - windowWallSlotsMap/nextAvailableSlot are also written by
    // swapWindowSlots() and assignWindowToSlot() (StartMenuFlow.cpp, off the haptic thread) under
    // this same windowMutex; this pass used to run entirely unlocked, a genuine (if previously
    // rare) unordered_map data race - now closed. Neither slotsMap.find()/operator[] nor
    // increasenNextAvailableSlot() below take windowMutex themselves, so this single lock_guard
    // for the whole block is safe (no recursive-lock risk).
    std::unordered_map<HWND, int> resolvedSlots;
    int archivedCount = 0;
    {
        std::lock_guard<std::recursive_mutex> lock(WindowManager::getInstance().windowMutex);
        auto& slotsMap = WindowManager::getInstance().windowWallSlotsMap;
        for (const ScannedUIElement& elem : scannedElements) {
            if (elem.type != UIElementType::Window) continue;
            if (resolvedSlots.find(elem.hwnd) != resolvedSlots.end()) continue;  // duplicate scan entry - ignore

            int physicalSlot;
            if (slotsMap.find(elem.hwnd) != slotsMap.end()) {
                physicalSlot = slotsMap[elem.hwnd];
            }
            else {
                physicalSlot = WindowManager::getInstance().nextAvailableSlot;
                slotsMap[elem.hwnd] = physicalSlot;
                WindowManager::getInstance().increasenNextAvailableSlot();
            }
            resolvedSlots[elem.hwnd] = physicalSlot;
        }
        archivedCount = (std::max)(0, WindowManager::getInstance().nextAvailableSlot - maxActive);
    }

    // Shared Z-band for the whole front zone (Program Tray on the right, FFUI Settings on the
    // left, and every MenuSystem menu spanning both) - stashed on WindowManager so
    // FFUIDesktop's settings-tile creation, the section divider wall, and MenuSystem's own
    // layout can all agree on exactly the same depth without recomputing it themselves.
    FrontZoneBand frontZone = computeFrontZoneBand();
    WindowManager::getInstance().frontZoneZCenter = frontZone.zCenter;
    WindowManager::getInstance().frontZoneZHalfThickness = frontZone.zHalfThickness;

    for (ScannedUIElement elem : scannedElements) {
        HapticSolidProperties hapticPropsOfThisElemType = getHapticPropsOfType(elem.type);
        FFUIObject_Meta objectMeta{};
        objectMeta.customName = std::string(elem.name.begin(), elem.name.end());
        objectMeta.uiType = elem.type;
        //uIMeta.scale = Vector3(3, 3, z);



        objectMeta.hapticSolidProperties = hapticPropsOfThisElemType;

        switch (elem.type) {

        case UIElementType::Window:
        {
            WindowWallMeta windowMeta{};
            windowMeta.windowHandle = elem.hwnd;
            windowMeta.windowTitle = elem.name;
            windowMeta.isArchived = false;
            windowMeta.isGrabbed = false;

            // Already resolved in the pass above - no assignment happens here any more.
            int physicalSlot = resolvedSlots.count(elem.hwnd) ? resolvedSlots[elem.hwnd] : 0;

            // Active (physicalSlot < maxActive, the user's quickslot count), but not the ONE
            // currently-selected slot - no live WindowWallObject is created for it at all this
            // cycle. It stays fully tracked in windowWallSlotsMap (drag/drop, narration-by-name,
            // and buildGrabZoneOptions() all still work against it via that map, independent of
            // whether a live object exists), just not haptically rendered until the user scrolls
            // to it - "the main layout will just be one program slot", per the request.
            if (physicalSlot < maxActive && physicalSlot != WindowManager::getInstance().currentActiveSlotIndex) {
                break;
            }

            float WallsThickness = 10.0f;
            float inititalRoomMargin = 5;

            // The ONE rendered active room's Z position is FIXED - evaluated at
            // ACTIVE_ZONE_ROOM_COUNT - 1 (the position the old, further-back slot used to
            // occupy) rather than this window's own physicalSlot, so the merged room's single
            // wall always sits in exactly the same place - right at the boundary nearest the
            // front zone - regardless of the quickslot count or which slot happens to be
            // selected. (For an archived window, physicalSlot itself is never used in this
            // formula anyway - see the override a few lines down - so only the active case
            // actually depends on this.)
            int zPosSlotIndex = (physicalSlot < maxActive) ? (WindowManager::ACTIVE_ZONE_ROOM_COUNT - 1) : physicalSlot;
            float zPos = endZ - inititalRoomMargin - roomDepth - ((roomDepth + WallsThickness) * zPosSlotIndex);

            Vector3 position = screenToWorkspace(
                elem.center,
                config.screenSize,
                zPos,
                DEVICE_WORKSPACE_X,
                DEVICE_WORKSPACE_Y);


            float stiffness = hapticPropsOfThisElemType.stiffness;
            float solidForceLimit = hapticPropsOfThisElemType.solidForceLimit;

            float height = elem.size.y / config.screenSize.y * DEVICE_WORKSPACE_Y;
            float width = elem.size.x / config.screenSize.x * DEVICE_WORKSPACE_X;

            if (physicalSlot >= maxActive) {
                int archivedIndex = physicalSlot - maxActive;

                // Program Tray occupies the RIGHT half of the shared front zone (world X in
                // [0, DEVICE_WORKSPACE_X/2]) - FFUI Settings (added separately, see
                // FFUIDesktop::addFrontZoneSettingsTiles()) occupies the left half. Up to 6
                // rows (unchanged from before), with as many columns as needed -
                // columns = ceil(archivedCount / 6) - so larger numbers of open programs no
                // longer just run out of vertical space, per the request.
                GridCell cell = computeGridCell(archivedIndex, archivedCount, 0.0f, DEVICE_WORKSPACE_X / 2.0f, frontZone.zCenter);

                // Tiles are sized to their EXACT grid pitch - touching neighbours edge-to-edge,
                // no overlap. A previous attempt deliberately over-sized each tile by 15% to
                // eliminate a reported dead zone right at the shared boundary between two
                // tiles - but since adjacent tiles' hit-boxes now overlapped, a position in
                // that sliver satisfied BOTH tiles' box test at once, and each tile's own
                // pull-to-its-own-center detent force fought the other's, right at the
                // boundary - which is exactly what surfaced as "a vertical pop-through barrier
                // part way through the first column, in the middle of the tiles", per the
                // request. The dead-zone concern that overlap was meant to fix is instead
                // covered by the narration hysteresis added this round (see
                // WindowManager::resetNarratedWindow()) - a single frame landing exactly on the
                // mathematical boundary now just gets silently absorbed rather than either
                // fighting forces or flickering narration.
                height = cell.cellHeight;
                width = cell.cellWidth;
                position = Vector3(cell.position.x, cell.position.y, frontZone.zCenter);
                WallsThickness = frontZone.zHalfThickness * 2.0f;
            }


            theCreatedUIObjects.emplace_back(std::make_unique<WindowWallObject>(windowMeta, position, WallsThickness,
                stiffness, solidForceLimit,
                height, width));

            WindowWallObject* wallPointer = static_cast<WindowWallObject*>(theCreatedUIObjects.back().get());

            //If active windows list is full, store the rest in archived list
            if (physicalSlot >= maxActive) {
                wallPointer->setArchivedState(true);
                tempArchivedWindows.push_back(wallPointer);

            }
            else{
                wallPointer->setArchivedState(false);
                tempActiveWindows.push_back(wallPointer);

            }

    break;
}

        default:

            //The z position and depth has been calculated such that the button objects
            //Are placed in the region of the whole space except the 20 units at the very front,
            //As that space is for the archived windows list.
            objectMeta.globalPosition = screenToWorkspace(
                elem.center,
                config.screenSize,
                215, //zPosition
                DEVICE_WORKSPACE_X,
                DEVICE_WORKSPACE_Y);

            objectMeta.scale = Vector3(
                elem.size.x / config.screenSize.x * DEVICE_WORKSPACE_X,
                elem.size.y / config.screenSize.y * DEVICE_WORKSPACE_Y,
                110);


            objectMeta.orientation = Quaternion().setFromEuler(1, 0, 0);

            objectMeta.snappedToThis = false;

            //Carried through for the Narrate feature (NARRATE_SPEC.md) - see FFUIObject_Meta's
            //own comment on isDisabled for why this is inverted (elem.isEnabled -> isDisabled).
            objectMeta.isDisabled = !elem.isEnabled;

            if (FFUIDesktop::currentSnapAnchor.isTracking) {

                float dist = (objectMeta.globalPosition - FFUIDesktop::currentSnapAnchor.originalPosition).length();

                if (objectMeta.customName == FFUIDesktop::currentSnapAnchor.objectWindowsName) {
                    //   printf("Snapped to this\n");
                    objectMeta.snappedToThis = true;

                    FFUIDesktop::currentSnapAnchor.originalPosition = objectMeta.globalPosition;
                }
            }




            if (!dublicatePositionsExist(theCreatedUIObjects, objectMeta))
                theCreatedUIObjects.emplace_back(std::make_unique<ButtonObject>(objectMeta));
            //uIMeta.globalPosition = screenToWorkspace();

        }


    }

    return theCreatedUIObjects;

}


