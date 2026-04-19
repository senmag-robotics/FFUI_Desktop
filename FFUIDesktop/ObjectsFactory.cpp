#include "ObjectsFactory.h"
#include "FFUIDesktop.h"
#include "GravityWellObject.h"

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
        slotPosition = Vector3(0, windowYPosition, windowZPosition);
        gravityWellMeta.scale = Vector3(scale.x, scale.z, scale.y) * 0.7;




    }
    else {
        slotPosition = Vector3(0, 0, windowZPosition + 10);
        gravityWellMeta.scale = Vector3(roomDepth * 1.5, roomDepth, 10);

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
std::vector<std::unique_ptr<FFUIObject>> ObjectFactory::createObjectsFromUIElements(std::vector<ScannedUIElement>& scannedElements,
    FFUIDesktop_Config config,
    std::vector<WindowWallObject*>& tempActiveWindows,    
    std::vector<WindowWallObject*>& tempArchivedWindows)
{
    std::vector<std::unique_ptr<FFUIObject>> theCreatedUIObjects;


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
            
            int maxActive = WindowManager::getInstance().numOfActiveWindows;


   

            int physicalSlot;

            // Has this window been assigned a wall slot?
            auto& slotsMap = WindowManager::getInstance().windowWallSlotsMap;
            if (slotsMap.find(elem.hwnd) != slotsMap.end()) {
                physicalSlot = slotsMap[elem.hwnd];
            }
            else {
                physicalSlot = WindowManager::getInstance().nextAvailableSlot;
                slotsMap[elem.hwnd] = physicalSlot;
                WindowManager::getInstance().increasenNextAvailableSlot();
            }

            float WallsThickness = 10.0f;
            float inititalRoomMargin = 5;

            float zPos = endZ - inititalRoomMargin - roomDepth - ((roomDepth + WallsThickness) * physicalSlot);

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
                float initialSlotMargin = 5;
                float numberOfArchivedWindowsPossible = 9.0;

                //Put the slots list in the furthest point from the user, behind the last active window
                float archivedWindowsZPosition = endZ - inititalRoomMargin - maxActive * roomDepth - WallsThickness;
                //Z thickness of this list is the margin left between the last active window and the least possible z axis value.
                float slotsThickness = archivedWindowsZPosition - startZ;
                if (slotsThickness < 5) {
                    printf("No space available for an archived list; reduce the number of active windows or their room depth");

                }
                //Shift the position further such that the list is exactly in the middle of the gap.
                // (1/2 thickness is added on both sides later to fill the whole gap available)
                archivedWindowsZPosition -= slotsThickness / 2.0;
            

                height = (DEVICE_WORKSPACE_Y - initialSlotMargin) / numberOfArchivedWindowsPossible; 
                width = DEVICE_WORKSPACE_X;
                position =  Vector3(0.0f,
                    DEVICE_WORKSPACE_Y/2.0 - initialSlotMargin - (height * (physicalSlot - maxActive)),
                    archivedWindowsZPosition );

                WallsThickness = slotsThickness;


              // std::cout << "---" << std::endl;
               //std::cout << std::string(windowMeta.windowTitle.begin(), windowMeta.windowTitle.end()) << std::endl;
               //for (auto* window : WindowManager::getInstance().ArchivedWindows) {
               //    _setmode(_fileno(stdout), _O_U16TEXT);

               //    std::cout << window->getMeta().customName << std::endl;
               //}
                

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
            //std::cout << "---" << std::endl;

            //for (auto* window : WindowManager::getInstance().ActiveWindows) {
            //    		//_setmode(_fileno(stdout), _O_U16TEXT);

            //    		std::cout << window->getMeta().customName << std::endl;

            //}

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
                78);  // Z depth for haptic interaction


            objectMeta.orientation = Quaternion().setFromEuler(1, 0, 0);

            objectMeta.snappedToThis = false;

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


