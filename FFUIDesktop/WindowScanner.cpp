#include "WindowScanner.h"
#include <atlbase.h>
#include <chrono>
#include <oleauto.h>   //for VariantInit()/VariantClear() - see buildTypeCondition()'s own use

WindowScanner::WindowScanner() {}

WindowScanner::~WindowScanner() {
    shutdown();
}

UIElementType WindowScanner::mapControlType(int controlTypeId) {
    switch (controlTypeId) {
    case UIA_ButtonControlTypeId:    return UIElementType::Button;

    case UIA_CheckBoxControlTypeId:  return UIElementType::Button;
    case UIA_ComboBoxControlTypeId:  return UIElementType::Button;
    case UIA_TreeItemControlTypeId:  return UIElementType::ListItem;
    case UIA_ListItemControlTypeId:  return UIElementType::ListItem;
    case UIA_TabItemControlTypeId:  return UIElementType::ListItem;
    case UIA_MenuItemControlTypeId:  return UIElementType::MenuItem;
    case UIA_EditControlTypeId:      return UIElementType::TextField;

    //case UIA_TabItemControlTypeId:   return UIElementType::Tab;
    //case UIA_HyperlinkControlTypeId: return UIElementType::Hyperlink;
    //case UIA_CheckBoxControlTypeId:  return UIElementType::Checkbox;
    //case UIA_RadioButtonControlTypeId: return UIElementType::RadioButton;
    //case UIA_SliderControlTypeId:    return UIElementType::Slider;
    case UIA_PaneControlTypeId: return UIElementType::Window;
    case UIA_WindowControlTypeId:    return UIElementType::Window;

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
//We check if the element is a broswer element (and hence could have a rectangular box but still hidden)
//If it is we check the clickablePoint property.
bool IsElementClickable(IUIAutomationElement* pElement) {
    // Find out what rendered this element (we check if it's a browser)
    BSTR bstrFrameworkId = nullptr;
    bool isWebFramework = false;

    if (SUCCEEDED(pElement->get_CurrentFrameworkId(&bstrFrameworkId)) && bstrFrameworkId != nullptr) {
        std::wstring fw(bstrFrameworkId);

        // Check for common browser frameworks
        if (fw == L"Chrome" || fw == L"Mozilla" || fw == L"InternetExplorer") {
            isWebFramework = true;
        }
        SysFreeString(bstrFrameworkId);
    }

    if (!isWebFramework) return true;

    bool isClickable = false;

    POINT pt;
    BOOL gotClickable = FALSE;

    HRESULT hr = pElement->GetClickablePoint(&pt, &gotClickable);

    if (SUCCEEDED(hr) && gotClickable == TRUE) {



        return true;
    }

    // Small buttons often don't have a clickable point even though they are clickable
    // (Browsers don't create clikcalbe point for them to save memory)
    RECT rect;
    if (SUCCEEDED(pElement->get_CurrentBoundingRectangle(&rect))) {
        long width = rect.right - rect.left;
        long height = rect.bottom - rect.top;

        if (width > 0 && height > 10 && height < 80) {
            return true; 
        }
    }

    //If it is a webframework and the button does not have the clickable point property, then
    //It is not a clickable
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

    BSTR name = nullptr;
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
    bool typeMatch = typesToScan.empty() || hasElement(typesToScan, elem.type);


  
    if (typeMatch) { 
      
         
     //   if (IsElementClickable(pElement)) {
        results.push_back(elem);
      //  }
    }
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

//See this method's own header comment. Every raw UIA control-type ID mapControlType() actually
//maps to something other than Unknown - kept as one flat list here (rather than, say, a
//std::multimap<UIElementType,int>) since it only needs to be walked once per scan and the whole
//point is avoiding per-element COM overhead, not code elegance for a dozen constant ints.
IUIAutomationCondition* WindowScanner::buildTypeCondition(const std::vector<UIElementType>& typesToScan) {
    if (!pAutomation) return nullptr;

    if (typesToScan.empty()) {
        //"empty means no filter" - see processElement()'s own hasElement() calls for where this
        //convention originates.
        IUIAutomationCondition* pTrue = nullptr;
        pAutomation->CreateTrueCondition(&pTrue);
        return pTrue;
    }

    static const int kAllMappedControlTypeIds[] = {
        UIA_ButtonControlTypeId, UIA_CheckBoxControlTypeId, UIA_ComboBoxControlTypeId, UIA_HyperlinkControlTypeId,
        UIA_TreeItemControlTypeId, UIA_ListItemControlTypeId, UIA_TabItemControlTypeId,
        UIA_MenuItemControlTypeId,
        UIA_EditControlTypeId,
        UIA_PaneControlTypeId, UIA_WindowControlTypeId,
        UIA_ScrollBarControlTypeId,
    };

    IUIAutomationCondition* combined = nullptr;
    for (int controlTypeId : kAllMappedControlTypeIds) {
        if (!hasElement(typesToScan, mapControlType(controlTypeId))) continue;

        VARIANT v;
        VariantInit(&v);
        v.vt = VT_I4;
        v.lVal = controlTypeId;

        IUIAutomationCondition* pCond = nullptr;
        HRESULT hr = pAutomation->CreatePropertyCondition(UIA_ControlTypePropertyId, v, &pCond);
        VariantClear(&v);
        if (FAILED(hr) || !pCond) continue;

        if (!combined) {
            combined = pCond;
        }
        else {
            //IUIAutomation only ORs two conditions at a time - fold the growing list left,
            //releasing each intermediate as it's subsumed into the next.
            IUIAutomationCondition* pOr = nullptr;
            if (SUCCEEDED(pAutomation->CreateOrCondition(combined, pCond, &pOr)) && pOr) {
                combined->Release();
                pCond->Release();
                combined = pOr;
            }
            else {
                pCond->Release();
            }
        }
    }

    if (!combined) {
        //typesToScan didn't match any type this function knows how to express as a raw UIA
        //control type (shouldn't normally happen given the two enums are meant to line up) -
        //fall back to matching everything rather than a condition that silently matches nothing.
        pAutomation->CreateTrueCondition(&combined);
    }

    return combined;
}

//See this method's own header comment - the actual latency fix. One FindAllBuildCache() call
//replaces what used to be a full recursive TreeWalker descent (one navigation COM call per step)
//with six more get_Current*() COM calls at every single element visited, whether or not it ended
//up matching typesToScan.
void WindowScanner::findAllWithCache(IUIAutomationElement* pRoot, TreeScope scope,
    const std::vector<UIElementType>& typesToScan, std::vector<ScannedUIElement>& results) {

    if (!pAutomation || !pRoot) return;

    IUIAutomationCondition* pCondition = buildTypeCondition(typesToScan);
    if (!pCondition) return;

    IUIAutomationCacheRequest* pCacheRequest = nullptr;
    if (FAILED(pAutomation->CreateCacheRequest(&pCacheRequest)) || !pCacheRequest) {
        pCondition->Release();
        return;
    }

    //None, not Full - appendCachedElement() only ever reads get_Cached*() properties off these
    //elements, never get_Current*(), so there's no need to pay for a live provider-side proxy per
    //returned element. This is itself part of the win, on top of batching the property fetches.
    pCacheRequest->put_AutomationElementMode(AutomationElementMode_None);
    pCacheRequest->AddProperty(UIA_NamePropertyId);
    pCacheRequest->AddProperty(UIA_ClassNamePropertyId);
    pCacheRequest->AddProperty(UIA_BoundingRectanglePropertyId);
    pCacheRequest->AddProperty(UIA_ControlTypePropertyId);
    pCacheRequest->AddProperty(UIA_IsEnabledPropertyId);
    pCacheRequest->AddProperty(UIA_NativeWindowHandlePropertyId);

    IUIAutomationElementArray* pFound = nullptr;
    HRESULT hr = pRoot->FindAllBuildCache(scope, pCondition, pCacheRequest, &pFound);

    pCondition->Release();
    pCacheRequest->Release();

    if (FAILED(hr) || !pFound) return;

    int count = 0;
    pFound->get_Length(&count);
    if (count > 0) {
        results.reserve(results.size() + count);
    }

    for (int i = 0; i < count; ++i) {
        IUIAutomationElement* pElement = nullptr;
        if (FAILED(pFound->GetElement(i, &pElement)) || !pElement) continue;
        appendCachedElement(pElement, results);
        pElement->Release();
    }

    pFound->Release();
}

//See this method's own header comment. Mirrors processElement()'s property-reading half exactly,
//property for property, just against the get_Cached*() variants instead of get_Current*() - the
//actual ScannedUIElement shape callers see is unchanged either way.
void WindowScanner::appendCachedElement(IUIAutomationElement* pElement, std::vector<ScannedUIElement>& results) {
    if (!pElement) return;

    ScannedUIElement elem{};

    BSTR name = nullptr;
    if (SUCCEEDED(pElement->get_CachedName(&name)) && name) {
        elem.name = name;
        SysFreeString(name);
    }

    BSTR className = nullptr;
    if (SUCCEEDED(pElement->get_CachedClassName(&className)) && className) {
        elem.containerWindowName = className;
        SysFreeString(className);
    }

    RECT rect;
    if (SUCCEEDED(pElement->get_CachedBoundingRectangle(&rect))) {
        elem.boundingRect = rect;
        elem.center.x = (float)(rect.left + rect.right) / 2.0f;
        elem.center.y = (float)(rect.top + rect.bottom) / 2.0f;
        elem.size.x = (float)(rect.right - rect.left);
        elem.size.y = (float)(rect.bottom - rect.top);
    }

    int controlType = 0;
    if (SUCCEEDED(pElement->get_CachedControlType(&controlType))) {
        elem.controlTypeId = controlType;
        elem.type = mapControlType(controlType);
    }

    BOOL enabled = FALSE;
    if (SUCCEEDED(pElement->get_CachedIsEnabled(&enabled))) {
        elem.isEnabled = (enabled == TRUE);
    }

    UIA_HWND hwnd = 0;
    if (SUCCEEDED(pElement->get_CachedNativeWindowHandle(&hwnd))) {
        elem.hwnd = (HWND)hwnd;
    }

    results.push_back(elem);
}

//We get the root element in the desktop, then we traverse all of its children
std::vector<ScannedUIElement> WindowScanner::scanDesktop(const std::vector<UIElementType>& typesToScan) {
    std::vector<ScannedUIElement> results;
    if (!pAutomation) return results;

    IUIAutomationElement* pRoot = nullptr;
    if (SUCCEEDED(pAutomation->GetRootElement(&pRoot)) && pRoot) {
        processElement(pRoot, results, true, typesToScan);
        pRoot->Release();
    }
    return results;
}

std::vector<ScannedUIElement> WindowScanner::scanFocusedWindow(const std::vector<UIElementType>& typesToScan) {

    std::vector<ScannedUIElement> results;
    if (!pAutomation) return results;

    HWND focusedWindowHandle = GetForegroundWindow();
    if (!focusedWindowHandle) return results;

    IUIAutomationElement* windowElement = nullptr;
    HRESULT hr = pAutomation->ElementFromHandle(focusedWindowHandle, &windowElement);
    if (SUCCEEDED(hr) && windowElement) {

        //TreeScope_Subtree (not just Descendants) to match the old processElement(recurse=true)
        //behavior of checking the root element itself as well as recursing into it - see
        //findAllWithCache()'s own comment for what this call replaces and why.
        findAllWithCache(windowElement, TreeScope_Subtree, typesToScan, results);

        windowElement->Release();
    }

    return results;



}

std::vector<ScannedUIElement> WindowScanner::scanTaskBar(const std::vector<UIElementType>& typesToScan) {

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

        //Same TreeScope_Subtree reasoning as scanFocusedWindow() - see findAllWithCache()'s own
        //comment.
        findAllWithCache(taskbarElement, TreeScope_Subtree, typesToScan, results);

        taskbarElement->Release();
    }

    return results;
}

//This function recieves every window and we return TRUE 
//All the time because that's how  we recieve the next window.
BOOL CALLBACK WindowScanner::EnumWindowsProc(HWND hwnd, LPARAM lParam) {

    //skip if it's a hidden background window
    if (!IsWindowVisible(hwnd)) return TRUE;

    //Skip if it doesn't have a name (a backgorund window as well)
    int length = GetWindowTextLength(hwnd);
    if (length == 0) return TRUE;

 

    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_TOOLWINDOW) return TRUE;

    int cloaked = 0;
    HRESULT hr = DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (SUCCEEDED(hr) && cloaked != 0) {
        return TRUE;

    }



    //We reconstruct the reference to localHandles to add the current one.
    std::vector<HWND>* pHandles = reinterpret_cast<std::vector<HWND>*>(lParam);
    pHandles->push_back(hwnd);

    return TRUE; 
}

std::vector<ScannedUIElement> WindowScanner::fetchAllOpenWindows() {
    std::vector<ScannedUIElement> windows;
    if (!pAutomation) return windows;

    std::vector<HWND> localHandles;

    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&localHandles));

    //One cached property set per open window, fetched in the SAME ElementFromHandleBuildCache
    //call that finds the element - previously this was ElementFromHandle() (1 call) followed by
    //processElement()'s own six separate get_Current*() calls, so this turns 7 round trips per
    //open window into 1. No subtree search needed here (recurse was always false - each window's
    //own root element is the only thing this method has ever wanted), so this skips
    //findAllWithCache()/buildTypeCondition() entirely and just builds the cache request directly.
    IUIAutomationCacheRequest* pCacheRequest = nullptr;
    if (FAILED(pAutomation->CreateCacheRequest(&pCacheRequest)) || !pCacheRequest) return windows;

    pCacheRequest->put_AutomationElementMode(AutomationElementMode_None);
    pCacheRequest->AddProperty(UIA_NamePropertyId);
    pCacheRequest->AddProperty(UIA_ClassNamePropertyId);
    pCacheRequest->AddProperty(UIA_BoundingRectanglePropertyId);
    pCacheRequest->AddProperty(UIA_ControlTypePropertyId);
    pCacheRequest->AddProperty(UIA_IsEnabledPropertyId);
    pCacheRequest->AddProperty(UIA_NativeWindowHandlePropertyId);

    windows.reserve(localHandles.size());
    for (HWND hwnd : localHandles) {
        IUIAutomationElement* pWindowElement = nullptr;
        HRESULT hr = pAutomation->ElementFromHandleBuildCache(hwnd, pCacheRequest, &pWindowElement);

        if (SUCCEEDED(hr) && pWindowElement) {
            appendCachedElement(pWindowElement, windows);
            pWindowElement->Release();
        }
    }

    pCacheRequest->Release();

    return windows;
}
