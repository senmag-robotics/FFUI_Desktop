#pragma once
#include "Windows.h"
#include <UIAutomation.h>
#include "WindowScanner.h"
#include "FFUIDesktop_object.h"
#include <thread>
#include <mutex>
#include <unordered_map>

#include <atomic>

struct WindowWallMeta {
    HWND windowHandle;
    std::wstring windowTitle;
    bool isArchived = false;
    bool isGrabbed = false;
    bool isFocused = false;
    bool stylusOnThis = false;
  
};



class WindowWallObject : public FFUIObject {


public:
    WindowWallObject(WindowWallMeta meta, Vector3 position, float thickness, float stiffness, float solidForceLimit, float height, float width);

    const WindowWallMeta& getWindowName() const { return windowMeta; }
    void setGrabbed(bool state) { windowMeta.isGrabbed = state; }
    Vector3 calculateInteractionForce(Location localLoc) override;
   
    WindowWallMeta getWindowMeta() {
        return windowMeta;

    }

    void setArchivedState(bool state) {
        windowMeta.isArchived = state;
    }
    bool isArchived() {
        return windowMeta.isArchived;
    }

    void setFocused(bool state) {
        windowMeta.isFocused = state;
    }
    bool isFocused() {
        return windowMeta.isFocused;
    }

    bool stylusIsOnThis() {
        return windowMeta.stylusOnThis;
    }
    HWND getHandle() {
        return windowMeta.windowHandle;
    }


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
    static void bringWindowToFront(WindowWallObject* targetWindow);


    static void moveArchviedToActive(WindowWallObject* targetWindow);

    void swapWindowSlots(HWND grabbedWindow, HWND targetWindow);

    static HWND getHandleOfTheWindowTheStylusIsOn(std::mutex& objectsListMutex);

    static HWND getHandleOfTheWindowTheStylusIsOn(std::mutex& objectsListMutex, const std::vector<std::unique_ptr<FFUIObject>>& objectsList);

    static void removeClosedWindows(const std::vector<HWND>& currentlyOpenWindows);

    HWND lastGrabbedWindowHandle;

    void increasenNextAvailableSlot() {
        nextAvailableSlot++;
    }


    std::vector<WindowWallObject*> ArchivedWindows;

    std::vector<WindowWallObject*> ActiveWindows;

    std::mutex windowMutex;

    int numOfActiveWindows = 2;

    std::unordered_map<HWND, int> windowWallSlotsMap;



    //Works as an ID of the positions, doesn't care what the size of map is
    int nextAvailableSlot = 0;

    
    std::atomic<bool> isUserGrabbingWindow{ false };



    

    // Delete copy constructors to enforce the "Only One" rule
    WindowManager(WindowManager const&) = delete;
    void operator=(WindowManager const&) = delete;
};