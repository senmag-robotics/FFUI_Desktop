#include "FFUIDesktop.h"
#include "ObjectsFactory.h"
#include "WindowScanner.h"
#include <thread>

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

//We create an updated list of objects by scanning then we lock the mutex only for hte moment of switching 
//the old list with the new list. 
 void FFUIDesktop::initiatePeriodicScanner(std::stop_token stoken, FFUIDesktop_Config config) {
	while (!stoken.stop_requested()) {

		std::vector<std::unique_ptr<FFUIObject>> newObjects;
		
		addBoundaryPlanes(newObjects);

		WindowScanner scanner;
		if (scanner.initialize()) {
			std::vector<UIElementType> typesToScan = { UIElementType::Button, UIElementType::ListItem, UIElementType::MenuItem };
			std::vector<ScannedUIElement> allscannedElements;

			
			std::vector<ScannedUIElement> focusedWindowElements = scanner.scanFocusedWindow(typesToScan);
			std::vector<ScannedUIElement> taskbarElements = scanner.scanTaskBar(typesToScan);


			//Combiniing the scanned elements
			allscannedElements
				.reserve(focusedWindowElements.size() + taskbarElements.size());
			allscannedElements.insert(allscannedElements.end(),
				focusedWindowElements.begin(),
				focusedWindowElements.end());

			allscannedElements.insert(allscannedElements.end(),
				taskbarElements.begin(),
				taskbarElements.end());




			std::vector<std::unique_ptr<FFUIObject>> scannedObjects =
				ObjectFactory::createObjectsFromUIElements(allscannedElements, config);

			for (auto& obj : scannedObjects) {
				newObjects.emplace_back(std::move(obj));
			}
		}

	
		
		std::lock_guard<std::mutex> lock(objectsListMutex);
		if (!layers.empty()) {
			std::swap(layers[0].objects, newObjects);
		}
		
	
		// Prevent CPU hogging
		//std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
}

// Update this function to take the vector as an argument
void FFUIDesktop::addBoundaryPlanes(std::vector<std::unique_ptr<FFUIObject>>& targetList) {
	HapticSolidProperties props{};
	props.stiffness = 0.001f;
	props.solidForceLimit = 0.005f;

	FFUIObject_Meta meta{};
	meta.scale = Vector3(2000, 0, 2000);
	meta.hapticSolidProperties = props;

	// Bottom
	meta.globalPosition = Vector3(0, -DEVICE_WORKSPACE_Y / 2, 0);
	meta.orientation = Quaternion().setFromEuler(1, 0, 0);
	meta.customName = "Workspace Lower Boundary";
	targetList.emplace_back(std::make_unique<SolidPlane>(meta));


	// Right
	meta.globalPosition = Vector3(DEVICE_WORKSPACE_X / 2, 0, 0);
	meta.orientation = Quaternion().setFromEuler(90, 0, 0);
	meta.customName = "Workspace Right Boundary";
	targetList.emplace_back(std::make_unique<SolidPlane>(meta));


	// Top
	meta.globalPosition = Vector3(0, DEVICE_WORKSPACE_Y / 2, 0);
	meta.orientation = Quaternion().setFromEuler(180, 0, 0);
	meta.customName = "Workspace Upper Boundary";
	targetList.emplace_back(std::make_unique<SolidPlane>(meta));


	// Left
	meta.globalPosition = Vector3(-DEVICE_WORKSPACE_X / 2, 0, 0);
	meta.orientation = Quaternion().setFromEuler(270, 0, 0);
	meta.customName = "Workspace Left Boundary";
	targetList.emplace_back(std::make_unique<SolidPlane>(meta));
}
//Returns a pointer to the closest object (that is not a boundary) to the cursor 

FFUIObject* FFUIDesktop::findCloestObjectToCursor() {
	if (layers.empty() || layers[0].objects.empty()) return nullptr;

	FFUIObject* closestObject = layers[0].objects[0].get();
	float minDistance = (layers[0].objects[0]->getMeta().globalPosition - 
	                     Vector3(cursorPos.x, cursorPos.y, 0)).length();
	
	for (const auto& object : layers[0].objects) {
		//We skip if this object is a boundary (has "Boundary" in its name)
		if (std::string(object->getMeta().customName).find("Boundary") != std::string::npos)
			continue;

		Vector3 objectPos = object->getMeta().globalPosition;
		Vector3 cursorPos3D(cursorPos.x, cursorPos.y, 0);
		float distanceToThisObject = (objectPos - cursorPos3D).length();
		
		if (distanceToThisObject < minDistance) {
			minDistance = distanceToThisObject;
			closestObject = object.get();
		}
	}

	return closestObject;
}



void FFUIDesktop::updateFrame() {
	/*Vector3 force = {0,0,0};
	for (FFUIDesktop_Layer layer : layers)
	{
		for (FFUIObject object : layer.objects)
		{
			force += object.update(cursorPos);
		}
	}*/

	deviceManager.update();
	for (int x = 0; x < deviceManager.devices.size(); x++) {
		if (deviceManager.devices[x].newStatus) {
			if (deviceManager.devices[x].deviceStatus.position[2] > 100) {	//only process 'active' devices

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

				//middle click
				//Released
				if ((currentInput >> 4 & 0x1) == 1 && (stylusState_previous >> 4 & 0x1) == 0) {
					SendMouseInput(MOUSEEVENTF_MIDDLEUP);
				}
				//Pressed
				if ((currentInput >> 4 & 0x1) == 0 && (stylusState_previous >> 4 & 0x1) == 1) {
					SendMouseInput(MOUSEEVENTF_MIDDLEDOWN);
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
			

					SendMouseInput(MOUSEEVENTF_XUP, XBUTTON1);
				}
				//Pressed
				if ((currentInput >> 7 & 0x1) == 0 && (stylusState_previous >> 7 & 0x1) == 1) {
		
					SendMouseInput(MOUSEEVENTF_XDOWN, XBUTTON1);

				}

				stylusState_previous = currentInput;

				cursorPos.x = desktopConfig.cursorFilter * cursorPos.x + (1.0 - desktopConfig.cursorFilter) * deviceManager.devices[x].deviceStatus.position[0];
				cursorPos.y = desktopConfig.cursorFilter * cursorPos.y + (1.0 - desktopConfig.cursorFilter) * deviceManager.devices[x].deviceStatus.position[1];
				moveWindowsCursor(cursorPos);

				Location deviceLoc;
				deviceLoc.position.x = deviceManager.devices[x].deviceStatus.position[0];
				deviceLoc.position.y = deviceManager.devices[x].deviceStatus.position[1];
				deviceLoc.position.z = deviceManager.devices[x].deviceStatus.position[2];
				deviceLoc.orientation.w = deviceManager.devices[x].deviceStatus.orientation[0];
				deviceLoc.orientation.i = deviceManager.devices[x].deviceStatus.orientation[1];
				deviceLoc.orientation.j = deviceManager.devices[x].deviceStatus.orientation[2];
				deviceLoc.orientation.k = deviceManager.devices[x].deviceStatus.orientation[3];
				Vector3 force = processForces(deviceLoc); //Interactive forces according to object type

				//Sudden forces:
				if ((currentInput >> 7 & 0x1) == 0) {
					auto* closestObject = findCloestObjectToCursor();
					if (closestObject != nullptr)
				    force += closestObject->calculateSnapForceToThis(deviceLoc);
				}
				

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

	Vector3 interactionForce = Vector3(0, 0, 0);
	for (int x = 0; x < layers.size(); x++) {
		std::lock_guard<std::mutex> lock(objectsListMutex);

		for (int y = 0; y < layers[x].objects.size(); y++) {
			interactionForce += layers[x].objects[y]->updateForces(stylusLocation);
		}
	}
	return interactionForce;
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

	SetCursorPos(targetPos.x, desktopConfig.screenSize.y - targetPos.y);
}