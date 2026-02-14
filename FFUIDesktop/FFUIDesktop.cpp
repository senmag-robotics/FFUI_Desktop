#include "FFUIDesktop.h"

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

	addBoundaryPlanes();
	addDemoObjects(); // optional starter objects
}


void FFUIDesktop::addBoundaryPlanes() {
	HapticSolidProperties props{};
	props.stiffness = 0.001f;
	props.solidForceLimit = 0.005f;

	FFUIObject_Meta meta{};
	meta.scale = Vector3(2000, 0, 2000);
	meta.hapticSolidProperties = props;

	// Bottom
	meta.globalPosition = Vector3(0, -DEVICE_WORKSPACE_Y / 2, 0);
	meta.orientation = Quaternion().setFromEuler(1, 0, 0);
	sprintf_s(meta.name, "%s", "Workspace Lower Bounds");
	layers[0].objects.emplace_back(std::make_unique<SolidPlane>(meta));


	// Right
	meta.globalPosition = Vector3(DEVICE_WORKSPACE_X / 2, 0, 0);
	meta.orientation = Quaternion().setFromEuler(90, 0, 0);
	sprintf_s(meta.name, "%s", "Workspace Right Bounds");
	layers[0].objects.emplace_back(std::make_unique<SolidPlane>(meta));


	// Top
	meta.globalPosition = Vector3(0, DEVICE_WORKSPACE_Y / 2, 0);
	meta.orientation = Quaternion().setFromEuler(180, 0, 0);
	sprintf_s(meta.name, "%s", "Workspace Upper Bounds");
	layers[0].objects.emplace_back(std::make_unique<SolidPlane>(meta));


	// Left
	meta.globalPosition = Vector3(-DEVICE_WORKSPACE_X / 2, 0, 0);
	meta.orientation = Quaternion().setFromEuler(270, 0, 0);
	sprintf_s(meta.name, "%s", "Workspace Left Bounds");
	layers[0].objects.emplace_back(std::make_unique<SolidPlane>(meta));
}

void FFUIDesktop::addDemoObjects() {
	// Example: center button with no rotation
	FFUIObject_Meta buttonMeta{};
	buttonMeta.globalPosition = Vector3(0, 0, 50);
	buttonMeta.scale = Vector3(20, 20, 20);
	buttonMeta.orientation = Quaternion().setFromEuler(1, 0, 0);
	buttonMeta.hapticSolidProperties.stiffness = 0.0002f;
	//buttonMeta.hapticSolidProperties.solidForceLimit = 0.01f;
	sprintf_s(buttonMeta.name, "%s", "Center Button");
	layers[0].objects.emplace_back(std::make_unique<ButtonObject>(buttonMeta));
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
				Vector3 force = processForces(deviceLoc);

				

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