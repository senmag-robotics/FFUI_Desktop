#pragma once
#include "Windows.h"
#include <UIAutomation.h>
#include "WIndowScanner.h"


struct WindowWallMeta {
    HWND windowHandle;
    std::wstring windowTitle;
    bool isArchived;
    float currentZ;       
    float thickness;      // How far to push such that it breaks
    float stiffness;      // How hard the wall pushes back
    bool isGrabbed;       // State flag for rearranging z position
};

std::vector<WindowWallMeta> openWindows; //