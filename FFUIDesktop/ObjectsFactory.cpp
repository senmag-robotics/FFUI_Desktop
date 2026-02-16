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
        props.stiffness = 0.0002f;
        props.solidForceLimit = 0.004;


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



std::vector<std::unique_ptr<FFUIObject>> ObjectFactory::createObjectsFromUIElements(std::vector<ScannedUIElement> scannedElements)
{
    float z = 2000;
    std::vector<std::unique_ptr<FFUIObject>> theCreatedUIObjects;
    for (ScannedUIElement elem : scannedElements) {
        HapticSolidProperties hapticPropsOfThisElemType = getHapticPropsOfType(elem.type);
        FFUIObject_Meta uIMeta{};
        uIMeta.customName;
        Vector2 elemScreenPosition;
        uIMeta.scale = Vector3(5, 5, z);
        uIMeta.hapticSolidProperties = hapticPropsOfThisElemType;
        uIMeta.globalPosition = Vector3(elem.center.x + 10, elem.center.y + 10, z);

        theCreatedUIObjects.emplace_back(std::make_unique<ButtonObject>(uIMeta));
        //uIMeta.globalPosition = screenToWorkspace();


    }

    return theCreatedUIObjects;

}


