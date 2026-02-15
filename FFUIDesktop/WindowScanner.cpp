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
    case UIA_ScrollBarControlTypeId: return UIElementType::ScrollBar;
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

//Puts pElement in results after building a ScanneUIElement object, then traverses this pElement 
//if recurse is true;
void WindowScanner::processElement(IUIAutomationElement* pElement,
    std::vector<ScannedUIElement>& results,
    bool recurse,
    UIElementType filterType) {
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

    if (filterType == UIElementType::NoFilter || elem.type == filterType)
        results.push_back(elem);

    // Recurse into children if needed
    if (recurse) {
        IUIAutomationTreeWalker* pWalker = nullptr;
        if (SUCCEEDED(pAutomation->get_ControlViewWalker(&pWalker))) {
            IUIAutomationElement* pChild = nullptr;
            pWalker->GetFirstChildElement(pElement, &pChild);

            while (pChild) {
                processElement(pChild, results, true, UIElementType::Button);

                IUIAutomationElement* pNext = nullptr;
                pWalker->GetNextSiblingElement(pChild, &pNext);
                pChild->Release();
                pChild = pNext;
            }
            pWalker->Release();
        }
    }

}

//We get the root element in the desktop, then we traverse all of its children
std::vector<ScannedUIElement> WindowScanner::scanDesktop() {
    std::vector<ScannedUIElement> results;
    if (!pAutomation) return results;

    IUIAutomationElement* pRoot = nullptr;
    if (SUCCEEDED(pAutomation->GetRootElement(&pRoot)) && pRoot) {
        processElement(pRoot, results, true, UIElementType::NoFilter);
        pRoot->Release();
    }
    return results;
}



