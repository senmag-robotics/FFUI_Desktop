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




