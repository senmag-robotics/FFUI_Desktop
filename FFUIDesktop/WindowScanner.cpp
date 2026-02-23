#include "WindowScanner.h"
#include <atlbase.h>

WindowScanner::WindowScanner() {}

WindowScanner::~WindowScanner() {
    shutdown();
}

UIElementType WindowScanner::mapControlType(int controlTypeId) {
    switch (controlTypeId) {
    case UIA_ButtonControlTypeId:    return UIElementType::Button;
    case UIA_WindowControlTypeId:    return UIElementType::Window;
    case UIA_MenuItemControlTypeId:  return UIElementType::MenuItem;
    case UIA_ListItemControlTypeId:  return UIElementType::ListItem;
    //case UIA_TabItemControlTypeId:   return UIElementType::Tab;
    //case UIA_HyperlinkControlTypeId: return UIElementType::Hyperlink;
    case UIA_EditControlTypeId:      return UIElementType::TextField;
    //case UIA_CheckBoxControlTypeId:  return UIElementType::Checkbox;
    //case UIA_RadioButtonControlTypeId: return UIElementType::RadioButton;
    //case UIA_SliderControlTypeId:    return UIElementType::Slider;
    case UIA_PaneControlTypeId: return UIElementType::Window;
    case UIA_ScrollBarControlTypeId: return UIElementType::ScrollBar;
    case UIA_HyperlinkControlTypeId: return UIElementType::Button;
    default:                         return UIElementType::Unknown;
    }
}

bool WindowScanner::initialize() {
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return false;

    hr = CoCreateInstance(__uuidof(CUIAutomation), NULL,
        CLSCTX_INPROC_SERVER,
        __uuidof(IUIAutomation),
        (void**)&pAutomation);
    return SUCCEEDED(hr);
}

void WindowScanner::shutdown() {
    if (pAutomation) {
        pAutomation->Release();
        pAutomation = nullptr;
    }
    CoUninitialize();
}

//Checks if "typeToCheck" exists inside "types"
static bool hasElement(const std::vector<UIElementType>& types, UIElementType typeToCheck) {
    for (UIElementType type : types) {
        if (type == typeToCheck)
            return true;
    }
    return false;
}

//Puts pElement in results after building a ScanneUIElement object, then traverses this pElement 
//if recurse is true;
//Maybe the filtering should be at a later stage such as when creating the objects? Also easier then. 
void WindowScanner::processElement(IUIAutomationElement* pElement,
    std::vector<ScannedUIElement>& results,
    bool recurse,
    const std::vector<UIElementType>& typesToScan) {

    if (!pElement) return;

    ScannedUIElement elem{};

    BSTR name;
    if (SUCCEEDED(pElement->get_CurrentName(&name)) && name) {
        elem.name = name;
        SysFreeString(name);
    }

    BSTR className;
    if (SUCCEEDED(pElement->get_CurrentClassName(&className)) && className) {
        elem.containerWindowName = className;
        SysFreeString(className);
    }

    RECT rect;
    if (SUCCEEDED(pElement->get_CurrentBoundingRectangle(&rect))) {
        elem.boundingRect = rect;
        elem.center.x = (float)(rect.left + rect.right) / 2.0f;
        elem.center.y = (float)(rect.top + rect.bottom) / 2.0f;
        elem.size.x = (float)(rect.right - rect.left);
        elem.size.y = (float)(rect.bottom - rect.top);
    }

    int controlType;
    if (SUCCEEDED(pElement->get_CurrentControlType(&controlType))) {
        elem.controlTypeId = controlType;
        elem.type = mapControlType(controlType);

    }

    BOOL enabled;
    if (SUCCEEDED(pElement->get_CurrentIsEnabled(&enabled))) {
        elem.isEnabled = (enabled == TRUE); //To convert from BOOL (Integer) to bool
    }

    UIA_HWND hwnd;
    if (SUCCEEDED(pElement->get_CurrentNativeWindowHandle(&hwnd))) {
        elem.hwnd = (HWND)hwnd;
    }

    if (typesToScan.empty() || hasElement(typesToScan, elem.type))
        results.push_back(elem);

    /*    filterType == UIElementType::NoFilter || elem.type == filterType)*/

    // Recurse into children if needed, using DEPTH FIRST SEARCH
    if (recurse) {
        IUIAutomationTreeWalker* pWalker = nullptr;
        if (SUCCEEDED(pAutomation->get_ControlViewWalker(&pWalker))) {
            IUIAutomationElement* pChild = nullptr;
            pWalker->GetFirstChildElement(pElement, &pChild);

            while (pChild) {
                processElement(pChild, results, true, typesToScan);

                IUIAutomationElement* pNext = nullptr;
                pWalker->GetNextSiblingElement(pChild, &pNext);
                pChild->Release();
                pChild = pNext;
            }
            pWalker->Release();
        }
    }

}

void WindowScanner::processElement(IUIAutomationElement* pElement,
    std::vector<ScannedUIElement>& results,
    bool recurse) {
    processElement(pElement, results, recurse, {});
}

//We get the root element in the desktop, then we traverse all of its children
std::vector<ScannedUIElement> WindowScanner::scanDesktop(std::vector<UIElementType> typesToScan) {
    std::vector<ScannedUIElement> results;
    if (!pAutomation) return results;

    IUIAutomationElement* pRoot = nullptr;
    if (SUCCEEDED(pAutomation->GetRootElement(&pRoot)) && pRoot) {
        processElement(pRoot, results, true, typesToScan);
        pRoot->Release();
    }
    return results;
}

std::vector<ScannedUIElement> WindowScanner::scanFocusedWindow(std::vector<UIElementType> typesToScan) {

    std::vector<ScannedUIElement> results;
    if (!pAutomation) return results;

    HWND focusedWindowHandle = GetForegroundWindow();
    if (!focusedWindowHandle) return results;

    IUIAutomationElement* windowElement = nullptr;
    HRESULT hr = pAutomation->ElementFromHandle(focusedWindowHandle, &windowElement);
    if (SUCCEEDED(hr) && windowElement) {
    
        processElement(windowElement, results, true, typesToScan);

        windowElement->Release();
    }

    return results;
    
}

std::vector<ScannedUIElement> WindowScanner::scanTaskBar(std::vector<UIElementType> typesToScan) {

    std::vector<ScannedUIElement> results;
    if (!pAutomation) return results;

    // Look for the window with the exact class name "Shell_TrayWnd"
    HWND taskbarHandle = FindWindow(L"Shell_TrayWnd", NULL);

    if (taskbarHandle == NULL) {
        printf("Could not find the taskbar.\n");
        return results;
    }

    
  

    IUIAutomationElement* taskbarElement = nullptr;
    HRESULT hr = pAutomation->ElementFromHandle(taskbarHandle, &taskbarElement);
    BOOL isTaskbarOffScreenBOOL;
    taskbarElement->get_CurrentIsOffscreen(&isTaskbarOffScreenBOOL);
    bool isTaskbarOffScreen = (isTaskbarOffScreenBOOL == TRUE);


    if (SUCCEEDED(hr) && taskbarElement && !isTaskbarOffScreen) {

        processElement(taskbarElement, results, true, typesToScan);

        taskbarElement->Release();
    }

    return results;
}






