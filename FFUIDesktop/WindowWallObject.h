#pragma once
#include "Windows.h"
#include <UIAutomation.h>
#include "WindowScanner.h"
#include "FFUIDesktop_object.h"
#include <thread>
#include <mutex>

struct WindowWallMeta {
    HWND windowHandle;
    std::wstring windowTitle;
    bool isArchived = false;
    bool isGrabbed = false;
};



class WindowWallObject : public FFUIObject {


public:
    WindowWallObject(WindowWallMeta meta, float startZ, float thickness, float stiffness, float height, float width);

    const WindowWallMeta& getWallMeta() const { return windowMeta; }
    void setGrabbed(bool state) { windowMeta.isGrabbed = state; }

   


private:
    WindowWallMeta windowMeta;


};

class WindowManager {
private:
    //Since conctructor is private, no once can create instances of this class(as needed)
    WindowManager() {}

public:

    static WindowManager& getInstance() {
        static WindowManager instance; 
        return instance;
    }

    std::vector<WindowWallObject> ActiveWindows;
    std::mutex windowMutex;

    // Delete copy constructors to enforce the "Only One" rule
    WindowManager(WindowManager const&) = delete;
    void operator=(WindowManager const&) = delete;
};