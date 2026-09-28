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
    ScreenBoundary,
    GravityWell,
    Unknown,
    DesktopIcon,
    TaskbarIcon,
    Window,
    Button,
    MenuItem,
    TextField,
    ScrollBar,
    ListItem,
    NoFilter,

    //A synthetic (not scanned - hand-placed by FFUIDesktop itself) tile used by the FFUI
    //Settings grid and by MenuSystem's generic menu framework (settings entries, voice
    //choices, the voice-speed slider, "back", etc.) - see GridTileObject. Distinct from
    //MenuItem above, which means "a real scanned UI element belonging to some OTHER app the
    //user is running" - these two are never the same object.
    GridTile,
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

    //Builds a UIA search condition equivalent to "would mapControlType() land this element's
    //control type inside typesToScan" - an OR of UIA_ControlTypePropertyId property conditions
    //over every raw control-type ID that maps into one of typesToScan (see mapControlType()'s own
    //switch for that mapping; this is its reverse). Lets FindAllBuildCache() filter server-side,
    //rather than the app fetching and immediately discarding every uninteresting element's
    //properties one at a time, the way the old TreeWalker-based processElement() recursion did.
    //Returns CreateTrueCondition() (match everything) when typesToScan is empty, matching
    //processElement()'s own established "empty means no filter" convention - see hasElement()'s
    //call sites in processElement() for that convention's original definition.
    IUIAutomationCondition* buildTypeCondition(const std::vector<UIElementType>& typesToScan);

    //Replaces the old processElement()-driven recursive walk (one IUIAutomationTreeWalker
    //navigation call PLUS six separate get_Current*() property calls per element, for every
    //single element in the whole subtree) with one FindAllBuildCache() call: a single COM round
    //trip that asks UIA itself to walk pRoot's subtree (scope - TreeScope_Subtree to also include
    //pRoot itself, matching processElement()'s own "check this element too, then recurse"
    //behavior), filtered server-side by buildTypeCondition()'s condition, with every property
    //appendCachedElement() needs already cached on each returned element - so reading those
    //properties back afterward costs zero further COM calls. This is the fix for the
    //hardware-test-reported latency between an on-screen change (a program slot loading, a
    //right-click menu opening) and the haptics picking it up - see FFUIDesktop.cpp's
    //initiatePeriodicScanner() for how this feeds the periodic scan cycle. Appends results
    //directly rather than returning a fresh vector, so callers can combine multiple calls (a
    //window's subtree, the taskbar's subtree) into one results list the same way the old
    //recursive calls did.
    void findAllWithCache(IUIAutomationElement* pRoot, TreeScope scope,
        const std::vector<UIElementType>& typesToScan, std::vector<ScannedUIElement>& results);

    //Reads only get_Cached*() properties (zero additional COM calls - see findAllWithCache()'s
    //own comment for why every property this needs was already requested and cached by whichever
    //BuildCache-flavored call produced pElement) off pElement, and appends the resulting
    //ScannedUIElement to results. The read-only mirror of processElement()'s own property-reading
    //half, kept as its own function since both findAllWithCache() and fetchAllOpenWindows() (a
    //single cached element per open window, no subtree search needed) share it.
    void appendCachedElement(IUIAutomationElement* pElement, std::vector<ScannedUIElement>& results);
};
