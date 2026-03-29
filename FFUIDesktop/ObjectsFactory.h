#pragma once
#include "WindowScanner.h"
#include "ButtonObject.h"
#include <memory>
#include "FFUIDesktop.h"
#include <cmath>
#include "WindowWallObject.h"

//This Creates FFUIObject subclasses automatically from scanned elements




class ObjectFactory {
public:

    // Convert screen coordinates to device workspace
    static Vector3 screenToWorkspace(Vector2 screenPos,
        Vector2 screenSize,
        float zPosition,
        float workspaceX,
        float workspaceY);

    static std::vector<std::unique_ptr<FFUIObject>> createObjectsFromUIElements(
        std::vector<ScannedUIElement>& scannedElements,
        FFUIDesktop_Config config,
        std::vector<WindowWallObject*>& tempActive,    
        std::vector<WindowWallObject*>& tempArchived
    );

    static FFUIObject_Meta createDemoObject();

    static std::unique_ptr<FFUIObject> createGravityWellAtWindowPosition(WindowWallObject* targetWindow);




private:
    static HapticSolidProperties getHapticPropsOfType(UIElementType type);
};