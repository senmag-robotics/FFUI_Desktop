#pragma once
#include <UIAutomation.h>
#include <vector>
#include <string>
#include <memory>
#include "../mathTypes.h"
#include <Windows.h> 
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib") // Tells Visual Studio to link the DWM library
#pragma comment(lib, "oleaut32.lib")

// Types of UI elements we can detect
enum class UIElementType {
    Unknown,
    DesktopIcon,
    TaskbarIcon,
    Window,
    Button,
    MenuItem,
    TextField,
    ScrollBar,
    ListItem,
    NoFilter
};

struct ScannedUIElement {
    UIElementType type;
    std::wstring name;
    std::wstring containerWindowName;
    RECT boundingRect;
    Vector2 center;
    Vector2 size;           // x = Width, y = height

    int controlTypeId;  // UIA_ControlTypeId
    HWND hwnd;           // Window handle (if applicable)
    bool isEnabled;
    bool isFocusable;


};

class WindowScanner {
public:
    WindowScanner();
    ~WindowScanner();
    
    bool initialize();
    void shutdown();

    std::vector<ScannedUIElement> scanDesktop(const std::vector<UIElementType>& typesToScan);

    std::vector<ScannedUIElement> scanFocusedWindow(const std::vector<UIElementType>& typesToScan);

    std::vector<ScannedUIElement> scanTaskBar(const std::vector<UIElementType>& typesToScan);

    std::vector<ScannedUIElement> fetchAllOpenWindows();

  

    std::vector<HWND> foundWindowHandles;


private:
    IUIAutomation* pAutomation = nullptr;
    UIElementType mapControlType(int controlTypeId);

    //An old style helper function to filter fetched windows from the old API function: EnumWindows()
    static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam);


    void processElement(IUIAutomationElement* pElement,
        std::vector<ScannedUIElement>& results,
        bool recurse = true);

    void processElement(IUIAutomationElement* pElement,
        std::vector<ScannedUIElement>& results,
        bool recurse,
        const std::vector<UIElementType>& typesToScan);
};

