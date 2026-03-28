#include "ObjectsFactory.h"
#include "FFUIDesktop.h"


Vector3 ObjectFactory::screenToWorkspace(Vector2 screenPos, Vector2 screenSize, float zPosition, float workspaceX, float workspaceY)
{

    float x = (screenPos.x / screenSize.x - 0.5f) * workspaceX;
    float y = (0.5f - screenPos.y / screenSize.y) * workspaceY;
    return Vector3(x, y, zPosition);
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
        props.stiffness = 0.001f;
        props.solidForceLimit = 0.006;

    default: return props;

    }
    return props;

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

            float thickness = 10.0f;
      

            float zPos = endZ - roomDepth - ((roomDepth + thickness) * physicalSlot);

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
            
                height = 40;
                width = 350;
                position =  Vector3(0.0f,
                    125.0f - (height * (physicalSlot - maxActive)),
                    135);

                

                thickness = 25;

              // std::cout << "---" << std::endl;
               //std::cout << std::string(windowMeta.windowTitle.begin(), windowMeta.windowTitle.end()) << std::endl;
               //for (auto* window : WindowManager::getInstance().ArchivedWindows) {
               //    _setmode(_fileno(stdout), _O_U16TEXT);

               //    std::cout << window->getMeta().customName << std::endl;
               //}
                

            }

          
            theCreatedUIObjects.emplace_back(std::make_unique<WindowWallObject>(windowMeta, position, thickness,
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


