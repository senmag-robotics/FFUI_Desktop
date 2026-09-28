#pragma once
#include "WindowScanner.h"
#include "ButtonObject.h"
#include <memory>
#include "FFUIDesktop.h"
#include <cmath>
#include "WindowWallObject.h"

//This Creates FFUIObject subclasses automatically from scanned elements




class ObjectFactory {
public:

    // Convert screen coordinates to device workspace
    static Vector3 screenToWorkspace(Vector2 screenPos,
        Vector2 screenSize,
        float zPosition,
        float workspaceX,
        float workspaceY);

    static Vector2 workspaceToScreen(Vector3 workspacePos, Vector2 screenSize, float workspaceX, float workspaceY);


    static std::vector<std::unique_ptr<FFUIObject>> createObjectsFromUIElements(
        std::vector<ScannedUIElement>& scannedElements,
        FFUIDesktop_Config config,
        std::vector<WindowWallObject*>& tempActive,
        std::vector<WindowWallObject*>& tempArchived
    );

    static FFUIObject_Meta createDemoObject();

    static std::unique_ptr<FFUIObject> createGravityWellAtWindowPosition(WindowWallObject* targetWindow);

    //The shared Z-band the whole front zone (Program Tray on the right, X>=0; FFUI Settings on
    //the left, X<0) sits in - the same depth the archived window-select list has always used,
    //just now given its own name since two different sections (and MenuSystem's own full-width
    //menus) all need to agree on it. See computeFrontZoneBand()'s .cpp comment for the exact
    //derivation (identical arithmetic to what the old single-column archived-list code computed
    //inline per-window).
    struct FrontZoneBand {
        float zCenter;
        float zHalfThickness;
    };
    static FrontZoneBand computeFrontZoneBand();

    //One cell's center position and size within a "rows-then-columns" grid of totalCount items,
    //confined to world X in [xRangeMin, xRangeMax] and centered at world Z = zPos - used for the
    //Program Tray, the FFUI Settings tiles, and every menu MenuSystem renders. Up to maxRows
    //rows (default 6, matching the row height/spacing this app has always used - see
    //computeFrontZoneBand()'s own comment), with as many columns as needed:
    //columns = ceil(totalCount / maxRows). index is 0-based, row-major within each column
    //(index % maxRows = row, index / maxRows = column) - deliberately simple and stable frame
    //to frame as long as (index, totalCount) don't change, with no dependency on scan order.
    struct GridCell {
        Vector3 position;    //world-space center of this cell
        float cellWidth;     //full column width (world units)
        float cellHeight;    //full row height (world units) - always 30, per the fixed 6-row convention
    };
    static GridCell computeGridCell(int index, int totalCount, float xRangeMin, float xRangeMax, float zPos, int maxRows = 6);

    // Layout used specifically by MenuSystem (not the Settings/Tray grids, which keep
    // computeGridCell above) - item 0 of ANY menu always lands at world (0, 0, zPos), the
    // device's own natural horizontal/vertical center, "to aid in localization & navigation"
    // per the request: whichever menu just opened, the user has one fixed, known reference
    // point to start from rather than a position that depends on how many items the menu has.
    // Subsequent items stack downward in the same column (item 1 below item 0, etc., matching
    // e.g. "top item is a slider, bottom is back") until maxRows is reached, at which point
    // extra items spill into a further column to the right - item 0 is still always exactly at
    // 0 either way; only the rare long-list overflow case isn't itself centered.
    static GridCell computeMenuCell(int index, int totalCount, float zPos, int maxRows = 6);




private:
    static HapticSolidProperties getHapticPropsOfType(UIElementType type);
};
