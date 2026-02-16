#pragma once
#include "WindowScanner.h"
#include "ButtonObject.h"
#include <memory>

//This Creates FFUIObject subclasses automatically from scanned elements


// Extended metadata for UI-linked objects
//struct FFUIObject_UIMeta : public FFUIObject_Meta {
//    UIElementType       uiType;
//    std::wstring        accessibleName;  // For TTS
//    HWND                linkedHwnd;
//    RECT                screenRect;
//};

class ObjectFactory {
public:

    // Convert screen coordinates to device workspace
    static Vector3 screenToWorkspace(Vector2 screenPos,
        Vector2 screenSize,
        float workspaceX,
        float workspaceY);

    static std::vector<std::unique_ptr<FFUIObject>> createObjectsFromUIElements
    (std::vector<ScannedUIElement> scannedElements);

    static FFUIObject_Meta createDemoObject();



private:
    static HapticSolidProperties getHapticPropsOfType(UIElementType type);
};