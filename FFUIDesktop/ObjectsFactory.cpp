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
        props.stiffness = 0.0009f;
        props.solidForceLimit = 0.0012;

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


bool dublicatePositionsExist(std::vector<std::unique_ptr<FFUIObject>>& alreadyCreatedObjects, FFUIObject_UIMeta& objectMeta) {
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
    FFUIDesktop_Config config)
{
    float z = 2000;
    std::vector<std::unique_ptr<FFUIObject>> theCreatedUIObjects;
    for (ScannedUIElement elem : scannedElements) {
        HapticSolidProperties hapticPropsOfThisElemType = getHapticPropsOfType(elem.type);
        FFUIObject_UIMeta uiMeta{};
        uiMeta.accessibleName = elem.name;
        uiMeta.uiType = elem.type;
        //uIMeta.scale = Vector3(3, 3, z);

      
        uiMeta.hapticSolidProperties = hapticPropsOfThisElemType;

        switch (elem.type) {
        
        case UIElementType::Window:
        {
            WindowWallMeta windowMeta{};
            windowMeta.windowHandle = elem.hwnd;
            windowMeta.windowTitle = elem.name;
            windowMeta.isArchived = false;
            windowMeta.isGrabbed = false;

            float centerZ = 160 + 30 * WindowManager::getInstance().ActiveWindows.size();

            Vector3 position = screenToWorkspace(
                elem.center,
                config.screenSize,
                centerZ,
                DEVICE_WORKSPACE_X,
                DEVICE_WORKSPACE_Y);

            float thickness = 10.0f;
            float stiffness = hapticPropsOfThisElemType.stiffness;
            float solidForceLimit = hapticPropsOfThisElemType.solidForceLimit;

            float height = elem.size.y / config.screenSize.y * DEVICE_WORKSPACE_Y;
            float width = elem.size.x / config.screenSize.x * DEVICE_WORKSPACE_X;
            //printf("Width: %f\n", width);
            //printf("height: %f\n", height);

           // WindowWallObject windowWall(windowMeta, startZ, thickness, stiffness, height, width);
    
          
            theCreatedUIObjects.emplace_back(std::make_unique<WindowWallObject>(windowMeta, position, thickness,
                stiffness, solidForceLimit,
                height, width));
            WindowWallObject* wallPointer = static_cast<WindowWallObject*>(theCreatedUIObjects.back().get());

            if (WindowManager::getInstance().ActiveWindows.size() >= 1) {
                WindowManager::getInstance().ArchivedWindows.push_back(wallPointer);
                wallPointer->setArchivedState(true);
            }
            else {
                WindowManager::getInstance().ActiveWindows.push_back(wallPointer);
                wallPointer->setArchivedState(false);

            }
            //std::cout << "---" << std::endl;

            //for (auto* window : WindowManager::getInstance().ActiveWindows) {
            //    		//_setmode(_fileno(stdout), _O_U16TEXT);

            //    		std::cout << window->getMeta().customName << std::endl;

            //}

    break;
}

        default:

            uiMeta.globalPosition = screenToWorkspace(
                elem.center,
                config.screenSize,
                0, //zPosition
                DEVICE_WORKSPACE_X,
                DEVICE_WORKSPACE_Y);

            uiMeta.scale = Vector3(
                elem.size.x / config.screenSize.x * DEVICE_WORKSPACE_X,
                elem.size.y / config.screenSize.y * DEVICE_WORKSPACE_Y,
                z);  // Z depth for haptic interaction


            uiMeta.orientation = Quaternion().setFromEuler(1, 0, 0);

            uiMeta.snappedToThis = false;

            if (FFUIDesktop::currentSnapAnchor.isTracking) {

                float dist = (uiMeta.globalPosition - FFUIDesktop::currentSnapAnchor.originalPosition).length();

                if (uiMeta.accessibleName == FFUIDesktop::currentSnapAnchor.objectWindowsName) {
                    //   printf("Snapped to this\n");
                    uiMeta.snappedToThis = true;

                    FFUIDesktop::currentSnapAnchor.originalPosition = uiMeta.globalPosition;
                }
            }




            if (!dublicatePositionsExist(theCreatedUIObjects, uiMeta))
                theCreatedUIObjects.emplace_back(std::make_unique<ButtonObject>(uiMeta));
            //uIMeta.globalPosition = screenToWorkspace();
        
        }
        
     
    }

    return theCreatedUIObjects;

}


