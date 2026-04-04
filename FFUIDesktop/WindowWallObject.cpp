#include "WindowWallObject.h"
#include "FFUIDesktop.h"
WindowWallObject::WindowWallObject(WindowWallMeta wallMeta, Vector3 position, float thickness, float stiffness,
    float solidForceLimit, float height, float width)
    : FFUIObject({
        position,                                    // globalPosition
        {width, thickness , height},                             // scale 
        Quaternion().setFromEuler(0, 0, 90),                                // orientation
        {solidForceLimit, stiffness, 0.0f, 0.0f, 0.0f},                  // hapticSolidProperties
        std::string(wallMeta.windowTitle.begin(), wallMeta.windowTitle.end()), // customName (wstring to string conversion)
        false,                                                    // snappedToThis
        UIElementType::Window               //uiType
        }),
    windowMeta(wallMeta) // Initialize the UI properties

{
}

Vector3 WindowWallObject::calculateInteractionForce(Location localLoc) {
    Vector3 force(0, 0, 0);
    //printf("title: %s\n", objectMeta.customName);
  //  printf("%f \n", localLoc.position.x);
    Vector3 stylusPosition = localLoc.position;

    float halfX = objectMeta.scale.x * 0.5f;
    float halfY = objectMeta.scale.y * 0.5f;
    float halfZ = objectMeta.scale.z * 0.5f;
    bool withinX = std::abs(stylusPosition.x) < halfX;
    bool withinY = std::abs(stylusPosition.y) < halfY;
    bool withinZ = std::abs(stylusPosition.z) < halfZ;

    float frontWallEdge = halfY;
    float backWallEdge = -halfY;


    //if (!withinX || !withinZ) {
    //    return force;
    //}

    windowMeta.stylusOnThis = false;

    if (isArchived()) {

        if (withinX && withinY && withinZ) {
            //printf("inside \n");
            //std::cout << objectMeta.customName << std::endl;
            //printf("done\n");
            windowMeta.stylusOnThis = true;
           
            const Vector3 rectangleCenter(0, 0, 0);

            Vector3 toCenter = rectangleCenter - stylusPosition;

            float distance = toCenter.length();


    
            float maxForce = 0.001;

            // Vector3 dir = toCenter / distance;


            float minDimension = (std::min)(objectMeta.scale.x, objectMeta.scale.y);


            float stabilityFactor = (std::max)(minDimension, 15.0f);

            // Calculate Effective Stiffness
            // We ensure we don't exceed the stiffness of a 'stabilityFactor' sized button
            float effectiveStiffness = (objectMeta.hapticSolidProperties.stiffness * 13) / stabilityFactor;


            force = toCenter * (0.0005);


            force.x = 0;

            if (force.length() > maxForce) {
                force *= maxForce / force.length();
            }

        }
    
    
    
    }
    else {

        if (stylusPosition.y < roomDepth && stylusPosition.y > frontWallEdge) {
            //  printf("%f\n", stylusPosition.y);

              //We are in area of this tab, so we switch focus if we didn't already.
            if (!isFocused()) {

                WindowManager::bringWindowToFront(this);

                //printf("inside \n");
                //std::cout << objectMeta.customName << std::endl;
                //printf("done\n");
            }
        }
        else if (stylusPosition.y < frontWallEdge && stylusPosition.y > backWallEdge) {

            windowMeta.stylusOnThis = true;

            float penetrationDepth = stylusPosition.y - frontWallEdge;
            float reactionForce = -(penetrationDepth)*objectMeta.hapticSolidProperties.stiffness;
            force.y = reactionForce;

            if (force.y > objectMeta.hapticSolidProperties.solidForceLimit) force.y = objectMeta.hapticSolidProperties.solidForceLimit;
            if (force.y < -objectMeta.hapticSolidProperties.solidForceLimit) force.y = -objectMeta.hapticSolidProperties.solidForceLimit;
        }

    
    
    }

    

    return force;
}



void WindowManager::bringWindowToFront(WindowWallObject* targetWindow) {



    HWND hwnd = targetWindow->getWindowName().windowHandle;


    HWND hCurWnd = GetForegroundWindow();
    DWORD dwMyID = GetCurrentThreadId();
    DWORD dwCurID = GetWindowThreadProcessId(hCurWnd, NULL);

    AttachThreadInput(dwCurID, dwMyID, TRUE);

    if (IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
    }

    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_SHOWWINDOW | SWP_NOSIZE | SWP_NOMOVE);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    SetActiveWindow(hwnd);

    AttachThreadInput(dwCurID, dwMyID, FALSE);

    std::lock_guard<std::mutex> lock(WindowManager::getInstance().windowMutex);
    //Make this the only window that has isFocused set as true:
    for (auto* window : WindowManager::getInstance().ActiveWindows) {
        window->setFocused(false);
    }
    targetWindow->setFocused(true);


}

void WindowManager::moveArchviedToActive(WindowWallObject* mainWindow) {

    if (!mainWindow->isArchived()) return;



}

HWND WindowManager::getHandleOfTheWindowTheStylusIsOn(std::mutex& objectsListMutex) {
    std::scoped_lock doubleLock(objectsListMutex, WindowManager::getInstance().windowMutex);




    //Merge all windows in one list to iterate over all of them
    std::vector<WindowWallObject*> allWindows;
    allWindows.reserve(WindowManager::getInstance().ActiveWindows.size()
        + WindowManager::getInstance().ArchivedWindows.size());
    allWindows.insert(allWindows.end(),
        WindowManager::getInstance().ActiveWindows.begin(),
        WindowManager::getInstance().ActiveWindows.end());

    allWindows.insert(allWindows.end(),
        WindowManager::getInstance().ArchivedWindows.begin(),
        WindowManager::getInstance().ArchivedWindows.end());

    HWND handelOfFoundWindow = NULL;

    for (WindowWallObject* window : allWindows) {
        if (window == nullptr) continue;

        if (window->stylusIsOnThis()) {


            printf("inside \n");
            std::cout << window->getMeta().customName << std::endl;
            printf("done\n");
            handelOfFoundWindow = window->getHandle();
        }


    }

    return handelOfFoundWindow;
}
