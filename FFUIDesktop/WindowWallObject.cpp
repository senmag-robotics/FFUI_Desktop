#include "WindowWallObject.h"

WindowWallObject::WindowWallObject(WindowWallMeta wallMeta, float startZ, float thickness, float stiffness,float height, float width)
    : FFUIObject({
        {0.0f, 0.0f, startZ},                                    // globalPosition
        {width, height, thickness},                             // scale 
        {0.0f, 0.0f, 0.0f, 1.0f},                                // orientation
        {1000.0f, stiffness, 0.0f, 0.0f, 0.0f},                  // hapticSolidProperties
        std::string(windowMeta.windowTitle.begin(), windowMeta.windowTitle.end()), // customName (wstring to string conversion)
        false                                                    // snappedToThis
        }),
    windowMeta(wallMeta) // Initialize the UI properties

{
}