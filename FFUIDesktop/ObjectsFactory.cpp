#include "ObjectsFactory.h"
#include "FFUIDesktop.h"

Vector3 ObjectFactory::screenToWorkspace(Vector2 screenPos, Vector2 screenSize, float workspaceX, float workspaceY)
{

    float x = (screenPos.x / screenSize.x - 0.5f) * workspaceX;
    float y = (0.5f - screenPos.y / screenSize.y) * workspaceY;
    return Vector3(x, y, 0);
}
HapticSolidProperties ObjectFactory::getHapticPropsOfType(UIElementType type)
{

    HapticSolidProperties props{};

    switch (type) {
    case UIElementType::Button:
        props.stiffness = 0.0009f;
        props.solidForceLimit = 0.0015;
    /*    props.stiffness = 0.00005f;
        props.solidForceLimit = 0.0002;*/
        break;

    case UIElementType::ListItem:
   /*     props.stiffness = 0.00005f;
        props.solidForceLimit = 0.0002;*/

        props.stiffness = 0.0007f;
        props.solidForceLimit = 0.0015;
        break;

    case UIElementType::MenuItem:
        props.stiffness = 0.0005f;
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



std::vector<std::unique_ptr<FFUIObject>> ObjectFactory::createObjectsFromUIElements(std::vector<ScannedUIElement> scannedElements,
    FFUIDesktop_Config config)
{
    float z = 2000;
    std::vector<std::unique_ptr<FFUIObject>> theCreatedUIObjects;
    for (ScannedUIElement elem : scannedElements) {
        HapticSolidProperties hapticPropsOfThisElemType = getHapticPropsOfType(elem.type);
        FFUIObject_UIMeta uIMeta{};
        uIMeta.accessibleName = elem.name;
        uIMeta.uiType = elem.type;
        Vector2 elemScreenPosition;
        //uIMeta.scale = Vector3(3, 3, z);

      
        uIMeta.hapticSolidProperties = hapticPropsOfThisElemType;


        //Vector2 newPos(elem.center.x - 0.5 * elem.size.x, elem.center.y + elem.size.y);
        uIMeta.globalPosition = screenToWorkspace(
            elem.center ,
            //newPos,
            config.screenSize,
            DEVICE_WORKSPACE_X,
            DEVICE_WORKSPACE_Y);

          uIMeta.scale = Vector3(
            elem.size.x / config.screenSize.x * DEVICE_WORKSPACE_X,
            elem.size.y / config.screenSize.y * DEVICE_WORKSPACE_Y,
           z);  // Z depth for haptic interaction


        uIMeta.orientation = Quaternion().setFromEuler(1, 0, 0);

        //uIMeta.globalPosition = Vector3(elem.center.x, elem.center.y + 5, z);

        theCreatedUIObjects.emplace_back(std::make_unique<ButtonObject>(uIMeta));
        //uIMeta.globalPosition = screenToWorkspace();
     
    }

    return theCreatedUIObjects;

}


