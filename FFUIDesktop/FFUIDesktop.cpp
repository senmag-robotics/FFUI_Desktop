#include "FFUIDesktop.h"
#include "ObjectsFactory.h"
#include "WindowScanner.h"
#include <thread>
#include <iostream>


// Define static member
SnapAnchor FFUIDesktop::currentSnapAnchor;

// Helper function to send input cleanly
void SendMouseInput(DWORD flags, DWORD data = 0) {
	INPUT input = { 0 };
	input.type = INPUT_MOUSE;
	input.mi.dwFlags = flags;
	input.mi.mouseData = data; // Used for wheel scrolling or XBUTTON id
	SendInput(1, &input, sizeof(INPUT));
}

void FFUIDesktop::initDesktop(FFUIDesktop_Config config) {
	desktopConfig = config;

	cusrsorScale.x = desktopConfig.screenSize.x / DEVICE_WORKSPACE_X;
	cusrsorScale.y = desktopConfig.screenSize.y / DEVICE_WORKSPACE_Y;

	FFUIDesktop_Layer newLayer;
	layers.push_back(std::move(newLayer));

	cursorPos = { 0,0 };

	// a lambda to capture 'this' and 'config', and accept the 'st' (stop_token) from jthread
	scannerThread = std::jthread([this, config](std::stop_token st) {
		initiatePeriodicScanner(st, config);
	});


}



//We create an updated list of objects by scanning then we lock the mutex only for the moment of switching 
//the old list with the new list. 
 void FFUIDesktop::initiatePeriodicScanner(std::stop_token stoken, FFUIDesktop_Config config) {
	while (!stoken.stop_requested()) {






		std::vector<std::unique_ptr<FFUIObject>> newObjects;

		addBoundaryPlanes(newObjects);

		static bool hasGeneratedPlaceholders = false;
		if (WindowManager::getInstance().isUserGrabbingWindow.load()) {
			// Only generate and swap the placeholders ONCE per grab because it will crash if it swaps a second time
			// (since activeWindows list will have dead pointers)
			if (!hasGeneratedPlaceholders) {
				std::vector<std::unique_ptr<FFUIObject>> newObjects;
				addBoundaryPlanes(newObjects);

				std::scoped_lock doubleLock(objectsListMutex, WindowManager::getInstance().windowMutex);

				// Read the ActiveWindows data while it is still alive and safe
				for (WindowWallObject* activeWindow : WindowManager::getInstance().ActiveWindows) {
					std::unique_ptr<FFUIObject> windowPlaceholder = ObjectFactory::createGravityWellAtWindowPosition(activeWindow);
					//printf("z pos: %f\n", windowPlaceholder.get()->getMeta().globalPosition.z);

					newObjects.emplace_back(std::move(windowPlaceholder));
				}

				for (WindowWallObject* activeWindow : WindowManager::getInstance().ArchivedWindows) {
					std::unique_ptr<FFUIObject> windowPlaceholder = ObjectFactory::createGravityWellAtWindowPosition(activeWindow);
					//printf("y pos: %f\n", windowPlaceholder.get()->getMeta().globalPosition.y);

					newObjects.emplace_back(std::move(windowPlaceholder));
				}

				if (!layers.empty()) {

					std::swap(layers[0].objects, newObjects);

					WindowManager::getInstance().ActiveWindows.clear();
					WindowManager::getInstance().ArchivedWindows.clear();
				}

				hasGeneratedPlaceholders = true;
			}

			// Throttle the CPU while the user is dragging the window around
			std::this_thread::sleep_for(std::chrono::milliseconds(16));
			continue;
		}
		else {
			// we are not grabbing, reset the gate so it's ready for the next time grabbing mode is activated
			// (This works across frames because this variable is static)
			hasGeneratedPlaceholders = false;
		}


		//Temporary active and archived windows lists to minimize critical section
		//In the critical section we fill the content of the real ones with the temp ones.
		std::vector<WindowWallObject*> tempActiveWindows;
		std::vector<WindowWallObject*> tempArchivedWindows;

		WindowScanner scanner;




		if (scanner.initialize()) {
			std::vector<UIElementType> typesToScan = { UIElementType::Button,
				UIElementType::ListItem,
				UIElementType::MenuItem,
				};
			std::vector<ScannedUIElement> allscannedElements;

			
			std::vector<ScannedUIElement> focusedWindowElements = scanner.scanFocusedWindow(typesToScan);
			std::vector<ScannedUIElement> windows = scanner.fetchAllOpenWindows();

			//std::vector<ScannedUIElement> windows = scanner.fetchAllOpenWindows();
			
			//for (ScannedUIElement elem : focusedWindowElements) {
			//	//upgrading console printing capabilityes
			//	if (elem.type == UIElementType::Window) {
			//		_setmode(_fileno(stdout), _O_U16TEXT);
			//		std::wcout << elem.name << std::endl;

			//	}
			//	
			//}

			std::vector<ScannedUIElement> taskbarElements = scanner.scanTaskBar(typesToScan);


			//Combiniing the scanned elements
			allscannedElements
				.reserve(focusedWindowElements.size() + taskbarElements.size() + windows.size());
			allscannedElements.insert(allscannedElements.end(),
				focusedWindowElements.begin(),
				focusedWindowElements.end());

			allscannedElements.insert(allscannedElements.end(),
				taskbarElements.begin(),
				taskbarElements.end());

			allscannedElements.insert(allscannedElements.end(),
					windows.begin(),
					windows.end());


			//We clean up closed windows

			std::vector<HWND> currentOpenHwnds;
			currentOpenHwnds.reserve(windows.size());
			for (const auto& win : windows) {
				currentOpenHwnds.push_back(win.hwnd);
			}


			WindowManager::getInstance().removeClosedWindows(currentOpenHwnds);


		
			//Now we create the objects of 

			std::vector<std::unique_ptr<FFUIObject>> scannedObjects =
				ObjectFactory::createObjectsFromUIElements(allscannedElements, config,
					tempActiveWindows,
					tempArchivedWindows);

			for (auto& obj : scannedObjects) {
				newObjects.emplace_back(std::move(obj));
			}
		}


		//Critical Section minimised: 
		//this locks both mutexes at the same time
		std::scoped_lock doubleLock(objectsListMutex, WindowManager::getInstance().windowMutex);


		if (!layers.empty()) {

			//Find out which window had focus in last scan
			
			HWND lastFocusedWindowHandle = NULL;
			for (auto* oldWindow : WindowManager::getInstance().ActiveWindows) {
				if (oldWindow->isFocused()) {
					lastFocusedWindowHandle = oldWindow->getHandle();
					break;
				}
			}

			// Maintain the same focused window in the updated list
			WindowManager::getInstance().ActiveWindows = tempActiveWindows;

			for (auto* window : WindowManager::getInstance().ActiveWindows) {
				if (lastFocusedWindowHandle != NULL && window->getHandle() == lastFocusedWindowHandle) {
					window->setFocused(true);
				}
				else {
					window->setFocused(false);
				}
			}
			WindowManager::getInstance().ArchivedWindows = tempArchivedWindows;

			std::swap(layers[0].objects, newObjects);


		}
		
	
	}
}

void FFUIDesktop::addBoundaryPlanes(std::vector<std::unique_ptr<FFUIObject>>& targetList) {
	HapticSolidProperties props{};
	props.stiffness = 0.001f;
	props.solidForceLimit = 0.005f;

	FFUIObject_Meta meta{};
	meta.scale = Vector3(2000, 0, 2000);
	meta.hapticSolidProperties = props;
	meta.uiType = UIElementType::ScreenBoundary;

	// Front
	meta.globalPosition = Vector3(0, 0, 130);
	meta.orientation = Quaternion().setFromEuler(0, 0, 90);
	meta.customName = "Workspace Front Boundary";
	targetList.emplace_back(std::make_unique<SolidPlane>(meta));

	//// Back
	//meta.globalPosition = Vector3(0, 0, 240);
	//meta.orientation = Quaternion().setFromEuler(0, 0, -90);
	//meta.customName = "Workspace Front Boundary";
	//targetList.emplace_back(std::make_unique<SolidPlane>(meta));

	// Bottom
	meta.globalPosition = Vector3(0, (-DEVICE_WORKSPACE_Y) / 2 + DEVICE_WORKSPACE_OFFSETY, 0);
	meta.orientation = Quaternion().setFromEuler(1, 0, 0);
	meta.customName = "Workspace Lower Boundary";
	targetList.emplace_back(std::make_unique<SolidPlane>(meta));


	// Right
	meta.globalPosition = Vector3(DEVICE_WORKSPACE_X / 2 + DEVICE_WORKSPACE_OFFSETX, 0, 0);
	meta.orientation = Quaternion().setFromEuler(90, 0, 0);
	meta.customName = "Workspace Right Boundary";
	targetList.emplace_back(std::make_unique<SolidPlane>(meta));


	// Top
	meta.globalPosition = Vector3(0, DEVICE_WORKSPACE_Y / 2 + DEVICE_WORKSPACE_OFFSETY, 0);
	meta.orientation = Quaternion().setFromEuler(180, 0, 0);
	meta.customName = "Workspace Upper Boundary";
	targetList.emplace_back(std::make_unique<SolidPlane>(meta));


	// Left
	meta.globalPosition = Vector3(-DEVICE_WORKSPACE_X / 2 + DEVICE_WORKSPACE_OFFSETX, 0, 0);
	meta.orientation = Quaternion().setFromEuler(270, 0, 0);
	meta.customName = "Workspace Left Boundary";
	targetList.emplace_back(std::make_unique<SolidPlane>(meta));
}
//calculates the force to the closest object (that is not a boundary) to the cursor 

Vector3 FFUIDesktop::calculateForceToClosestObject(Location deviceLoc, bool button3Clicked) {
	Vector3 snappingForce(0, 0, 0);
	std::lock_guard<std::mutex> lock(objectsListMutex);


	if (layers.empty() || layers[0].objects.empty()) return snappingForce;

	FFUIObject* closestObject = nullptr;

	float minDistance = (std::numeric_limits<float>::max)();
	
	for (const auto& object : layers[0].objects) {

		WindowWallObject* windowPointer = dynamic_cast<WindowWallObject*>(object.get());
		bool isActiveWindow = false; 
		if(windowPointer != nullptr)
		isActiveWindow = object->getMeta().uiType == UIElementType::Window && windowPointer->isArchived() == false;

		bool isScreenBoundary = object->getMeta().uiType == UIElementType::ScreenBoundary;
		//We skip if this object is a boundary or an active window, skip;
		//as we don't want to attract towards them ever.
		if (isScreenBoundary || isActiveWindow)
			continue;



		Vector3 objectPos = object->getMeta().globalPosition;
		float dx = objectPos.x - cursorPos.x;
		float dy = objectPos.y - cursorPos.y;
		float distance2DToThisButton = std::sqrt(dx * dx + dy * dy);

		float distanceToThis = (objectPos - deviceLoc.position).length();
		
		if (distanceToThis < minDistance) {
			minDistance = distanceToThis;
			closestObject = object.get();
		}

		object->setSnapped(false);
	}
	if (closestObject != nullptr) {

		//To enable the feature of snapping inplace of an object when side button is clicked.
		//Move this line inside the else of the next if, and remove "buttonClicked = false" 
		//(Doesn't work currently so don't change anything)

		snappingForce = closestObject->calculateSnappingForceToThis(deviceLoc);


		float halfX = closestObject->getMeta().scale.x * 0.5f;
		float halfY = closestObject->getMeta().scale.y * 0.5f;	
		float halfZ = closestObject->getMeta().scale.z * 0.5f;

		if (std::abs(minDistance) < halfX  && std::abs(minDistance )< halfY) {


			// Attempt to cast the generic object into a WindowWall object
			WindowWallObject* wallPointer = dynamic_cast<WindowWallObject*>(closestObject);

			//if (closestObject->getMeta().uiType == UIElementType::Window ) {

			//	printf("inside \n");
			//	std::cout << closestObject->getMeta().customName << std::endl;
			//	printf("done\n");
			//
			//}

			//Remove this line to enable feature (Doesn't work currently)
			button3Clicked = false;
			if (button3Clicked) {

				stylusSnapped = !stylusSnapped;

				//std::cout << "is: " << stylusSnapped << std::endl;

				closestObject->setSnapped(stylusSnapped);

				currentSnapAnchor.isTracking = stylusSnapped;
				currentSnapAnchor.objectWindowsName = closestObject->getMeta().customName;
				currentSnapAnchor.originalPosition = closestObject->getMeta().globalPosition;

			}

			//std::cout << "Was: " << closestObject->getMeta().snappedToThis << std::endl;
			//std::cout << "Is: " << stylusSnapped << std::endl;
			

		}
		else {
			//std::wcout << "Mystery object: " << closestObject->getUIMeta(). << std::endl;


		}
	}

	return snappingForce;
}



void FFUIDesktop::updateFrame() {



	deviceManager.update();
	for (int x = 0; x < deviceManager.devices.size(); x++) {
		if (deviceManager.devices[x].newStatus) {
			if (deviceManager.devices[x].deviceStatus.position[2] > 100) {	//only process 'active' devices

				//Fetching the device 3d position and orientation
				Location deviceLoc;
				deviceLoc.position.x = deviceManager.devices[x].deviceStatus.position[0];
				deviceLoc.position.y = deviceManager.devices[x].deviceStatus.position[1];
				deviceLoc.position.z = deviceManager.devices[x].deviceStatus.position[2];
				deviceLoc.orientation.w = deviceManager.devices[x].deviceStatus.orientation[0];
				deviceLoc.orientation.i = deviceManager.devices[x].deviceStatus.orientation[1];
				deviceLoc.orientation.j = deviceManager.devices[x].deviceStatus.orientation[2];
				deviceLoc.orientation.k = deviceManager.devices[x].deviceStatus.orientation[3];


				/*
				* bit 7 = aux
				bit 6 = scroll up half
				bit 5 = scroll up full
				bit4 = middle
				bit3 = scroll down full
				bit2 = scroll down half
				bit1 = right
				bit0 = left
				*/
				uint8_t currentInput = deviceManager.devices[x].deviceStatus.toolInputs;

				static uint8_t stylusState_previous = 0xFF;
				//front button
				//Released
				if ((currentInput & 0x1) == 1 && (stylusState_previous & 0x1) == 0) {
					SendMouseInput(MOUSEEVENTF_LEFTUP);
				}
				//Pressed
				if ((currentInput & 0x1) == 0 && (stylusState_previous & 0x1) == 1) {
					SendMouseInput(MOUSEEVENTF_LEFTDOWN);
				}

				//rear button
				//Released
				if ((currentInput >> 1 & 0x1) == 1 && (stylusState_previous >> 1 & 0x1) == 0) {
					SendMouseInput(MOUSEEVENTF_RIGHTUP);
				}
				//Pressed
				if ((currentInput >> 1 & 0x1) == 0 && (stylusState_previous >> 1 & 0x1) == 1) {
					SendMouseInput(MOUSEEVENTF_RIGHTDOWN);
				}


				//scroll down half

				if ((currentInput >> 2 & 0x1) == 0 && (stylusState_previous >> 2 & 0x1) == 1) {
					SendMouseInput(MOUSEEVENTF_WHEEL, (DWORD)(-120));
				}

				//scroll down full
				if ((currentInput >> 3 & 0x1) == 0) {
					static int scrollDownRepeatCounter = 0;
					scrollDownRepeatCounter++;

					if (scrollDownRepeatCounter > desktopConfig.targetHapticFramerate / SCROLL_REPEAT_RATE) {
						SendMouseInput(MOUSEEVENTF_WHEEL, (DWORD)(-10));
						scrollDownRepeatCounter = 0;
					}
				}


				//middle click (disabled normal behavious and instead used for grabbing mode
				//Released
				if ((currentInput >> 4 & 0x1) == 1 && (stylusState_previous >> 4 & 0x1) == 0) {
					//SendMouseInput(MOUSEEVENTF_MIDDLEUP);

					HWND theWindowTheStylusReleasedOn =
						WindowManager::getInstance().getHandleOfTheWindowTheStylusIsOn(objectsListMutex, layers[0].objects);

					HWND grabbedWindow = WindowManager::getInstance().lastGrabbedWindowHandle;
					if (theWindowTheStylusReleasedOn != NULL && grabbedWindow != NULL) {
						// Trigger your swap logic here using the map!
						WindowManager::getInstance().swapWindowSlots(grabbedWindow, theWindowTheStylusReleasedOn);
					}

					WindowManager::getInstance().isUserGrabbingWindow.store(false);

			


				}


				//Pressed
				if ((currentInput >> 4 & 0x1) == 0 && (stylusState_previous >> 4 & 0x1) == 1) {
					//SendMouseInput(MOUSEEVENTF_MIDDLEDOWN);

					//Save the window the stylus is on currently in corresponding static variable
					WindowManager::getInstance().lastGrabbedWindowHandle =
						WindowManager::getInstance().getHandleOfTheWindowTheStylusIsOn(objectsListMutex);

					//This will trigger the scanner thread to sleep and create the gravity wells. 
					WindowManager::getInstance().isUserGrabbingWindow.store(true);


				}


				//scroll up full
				if ((currentInput >> 5 & 0x1) == 0 && (stylusState_previous >> 5 & 0x1) == 1) {
					//SendMouseInput(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);
				}
				//if ((currentInput >> 5 & 0x1) == 0 && (stylusState_previous >> 5 & 0x1) == 1) {
				if ((currentInput >> 5 & 0x1) == 0) {
					static int scrollUpRepeatCounter = 0;
					scrollUpRepeatCounter++;

					if (scrollUpRepeatCounter > desktopConfig.targetHapticFramerate / SCROLL_REPEAT_RATE) {
						SendMouseInput(MOUSEEVENTF_WHEEL, (DWORD)(10));
						scrollUpRepeatCounter = 0;
					}
				}

				//scroll up half
				if ((currentInput >> 6 & 0x1) == 0 && (stylusState_previous >> 6 & 0x1) == 1) {
					SendMouseInput(MOUSEEVENTF_WHEEL, (DWORD)(120));
				}

				//side button
				//Released
				if ((currentInput >> 7 & 0x1) == 1 && (stylusState_previous >> 7 & 0x1) == 0) {

					//SendMouseInput(MOUSEEVENTF_XUP, XBUTTON1);
				}
				bool button3Clicked = false;
				//side button
				//Initial press
				if ((currentInput >> 7 & 0x1) == 0 && (stylusState_previous >> 7 & 0x1) == 1) {
				//	std::cout << "flag1: " << stylusSnapped << std::endl;

					button3Clicked = true;
					//SendMouseInput(MOUSEEVENTF_XUP, XBUTTON1);
				}
			

				stylusState_previous = currentInput;

				cursorPos.x = desktopConfig.cursorFilter * cursorPos.x + (1.0 - desktopConfig.cursorFilter) * deviceManager.devices[x].deviceStatus.position[0];
				cursorPos.y = desktopConfig.cursorFilter * cursorPos.y + (1.0 - desktopConfig.cursorFilter) * deviceManager.devices[x].deviceStatus.position[1];
				moveWindowsCursor(cursorPos);



				//printf("%f z\n", deviceLoc.position.z);
				Vector3 force = processForces(deviceLoc); //Interactive forces according to object type



				//While side button is held down push towards closest object
				if ((currentInput >> 7 & 0x1) == 0) {

			
					force = calculateForceToClosestObject(deviceLoc, button3Clicked);
				}
				

				//Send calculated aggregate of forces to device
				LibreOne_targets forceTargets;
				forceTargets.targets[0] = force.x;
				forceTargets.targets[1] = force.y;
				forceTargets.targets[2] = force.z;
				deviceManager.devices[x].serialComms.sendTargets(forceTargets);
			}
		}
	}
}


Vector3 FFUIDesktop::processForces(Location stylusLocation) {
    Vector3 uiForce(0, 0, 0);
    Vector3 boundaryForce(0, 0, 0);
    

    float globalUiForceLimit = 0.015f;
	std::lock_guard<std::mutex> lock(objectsListMutex);

    for (int x = 0; x < layers.size(); x++) {
        for (int y = 0; y < layers[x].objects.size(); y++) {

            // Calculate individual object force
            Vector3 f = layers[x].objects[y]->updateForces(stylusLocation);

            // Check if this is a boundary plane
       
            std::string name = layers[x].objects[y]->getMeta().customName;
            
            if (name.find("Boundary") != std::string::npos) {
     
                boundaryForce += f;
            } 
            else {
                uiForce += f;
            }
        }
    }


    if (uiForce.length() > globalUiForceLimit) {
        uiForce = uiForce.normalized() * globalUiForceLimit;
    }

    return uiForce + boundaryForce;
}


void FFUIDesktop::moveWindowsCursor(Vector2 targetPos) {
	targetPos.x += DEVICE_WORKSPACE_X / 2 + DEVICE_WORKSPACE_OFFSETX;
	targetPos.y += DEVICE_WORKSPACE_Y / 2 - DEVICE_WORKSPACE_OFFSETY;

	targetPos.x *= cusrsorScale.x;
	targetPos.y *= cusrsorScale.y;
	if (targetPos.x < 0) targetPos.x = 0;
	if (targetPos.x > desktopConfig.screenSize.x) targetPos.x = desktopConfig.screenSize.x;
	if (targetPos.y < 0) targetPos.y = 0;
	if (targetPos.y > desktopConfig.screenSize.y) targetPos.y = desktopConfig.screenSize.y;

	int finalPixelX = static_cast<int>(targetPos.x);
	int finalPixelY = static_cast<int>(desktopConfig.screenSize.y - targetPos.y);

	INPUT input = { 0 };
	input.type = INPUT_MOUSE;
	input.mi.dx = (finalPixelX * 65535) / (desktopConfig.screenSize.x - 1);
	input.mi.dy = (finalPixelY * 65535) / (desktopConfig.screenSize.y - 1);

	input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
	SendInput(1, &input, sizeof(INPUT));

}