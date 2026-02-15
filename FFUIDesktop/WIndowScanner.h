#pragma once
#include <UIAutomation.h>
#include <vector>
#include <string>
#include <memory>
#include "../mathTypes.h"
#include <Windows.h> // Add this include at the top of the file to define RECT

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

    std::vector<ScannedUIElement> scanDesktop();
    std::vector<ScannedUIElement> scanTaskbarIcons();

    //void scanWindow(HWND hwnd);
    //ScannedUIElement getElementAt(int x, int y);




private:
    IUIAutomation* pAutomation = nullptr;

    UIElementType mapControlType(int controlTypeId);


    void processElement(IUIAutomationElement* pElement,
        std::vector<ScannedUIElement>& results,
        bool recurse = true,
        UIElementType filterType = UIElementType::NoFilter
    );
};