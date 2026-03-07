#pragma once
#include <UIAutomation.h>
#include <vector>
#include <string>
#include <memory>
#include "../mathTypes.h"
#include <Windows.h> 

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

    std::vector<ScannedUIElement> scanDesktop(std::vector<UIElementType> typesToScan);

    std::vector<ScannedUIElement> scanFocusedWindow(const std::vector<UIElementType>& typesToScan);

    std::vector<ScannedUIElement> scanTaskBar(std::vector<UIElementType> typesToScan);

    //std::vector<ScannedUIElement> scanTaskbarIcons();

    //void scanWindow(HWND hwnd);
    //ScannedUIElement getElementAt(int x, int y);




private:
    IUIAutomation* pAutomation = nullptr;

    UIElementType mapControlType(int controlTypeId);


    void processElement(IUIAutomationElement* pElement,
        std::vector<ScannedUIElement>& results,
        bool recurse = true);

    void processElement(IUIAutomationElement* pElement,
        std::vector<ScannedUIElement>& results,
        bool recurse,
        const std::vector<UIElementType>& typesToScan);
};