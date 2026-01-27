#include "FFUIDesktop.h"



void FFUIDesktop::initDesktop(FFUIDesktop_Config config) {
	desktopConfig = config;

	cusrsorScale.x = desktopConfig.screenSize.x / DEVICE_WORKSPACE_X;
	cusrsorScale.y = desktopConfig.screenSize.y / DEVICE_WORKSPACE_Y;

	FFUIDesktop_Layer newLayer;
	layers.push_back(std::move(newLayer));

	HapticSolidProperties boundaryHapticProperties;
	boundaryHapticProperties.stiffness = 0.001;

	FFUIObject_Meta boundarySettings;
	boundarySettings.scale = Vector3(2000, 0, 2000);
	boundarySettings.hapticSolidProperties = boundaryHapticProperties;
	boundarySettings.hapticSolidProperties.solidForceLimit = .005;
	sprintf_s(boundarySettings.name, "%s", "Workspace Lower Bounds\0");


	
	boundarySettings.globalPosition = Vector3(0, -DEVICE_WORKSPACE_Y/2, 0);
	boundarySettings.orientation = Quaternion().setFromEuler(1, 0, 0);
	layers[0].objects.emplace_back(std::make_unique<SolidPlane>(boundarySettings));


	boundarySettings.globalPosition = Vector3(DEVICE_WORKSPACE_X/2, 0, 0);
	boundarySettings.orientation = Quaternion().setFromEuler(90, 0, 0);
	layers[0].objects.emplace_back(std::make_unique<SolidPlane>(boundarySettings));


	boundarySettings.globalPosition = Vector3(0, DEVICE_WORKSPACE_Y/2, 0);
	boundarySettings.orientation = Quaternion().setFromEuler(180, 0, 0);
	layers[0].objects.emplace_back(std::make_unique<SolidPlane>(boundarySettings));


	boundarySettings.globalPosition = Vector3(-DEVICE_WORKSPACE_X/2, 0, 0);
	boundarySettings.orientation = Quaternion().setFromEuler(270, 0, 0);
	layers[0].objects.emplace_back(std::make_unique<SolidPlane>(boundarySettings));

	cursorPos.x = 0;
	cursorPos.y = 0;

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
				static uint8_t stylusState_previous = 0xFF;
				//front button
				if ((deviceManager.devices[x].deviceStatus.toolInputs & 0x1) == 1 && (stylusState_previous & 0x1) == 0) {
					mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
				}
				if ((deviceManager.devices[x].deviceStatus.toolInputs & 0x1) == 0 && (stylusState_previous & 0x1) == 1) {
					mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
				}

				//rear button
				if ((deviceManager.devices[x].deviceStatus.toolInputs>>1 & 0x1) == 1 && (stylusState_previous>>1 & 0x1) == 0) {
					mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
				}
				if ((deviceManager.devices[x].deviceStatus.toolInputs>>1 & 0x1) == 0 && (stylusState_previous>>1 & 0x1) == 1) {
					mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);
				}

				
				//scroll down half
				if ((deviceManager.devices[x].deviceStatus.toolInputs >> 2 & 0x1) == 0 && (stylusState_previous >> 2 & 0x1) == 1) {
					//mouse_event(MOUSEEVENTF_WHEEL, 0, 0, -120, 0);
				}
				if ((deviceManager.devices[x].deviceStatus.toolInputs >> 2 & 0x1) == 0 && (stylusState_previous >> 2 & 0x1) == 1) {
					mouse_event(MOUSEEVENTF_WHEEL, 0, 0, -120, 0);
				}

				//scroll down full
				if ((deviceManager.devices[x].deviceStatus.toolInputs >> 3 & 0x1) == 0 && (stylusState_previous >> 3 & 0x1) == 1) {
					//mouse_event(MOUSEEVENTF_WHEEL, 0, 0, -240, 0);
				}
				//if ((deviceManager.devices[x].deviceStatus.toolInputs >> 3 & 0x1) == 0 && (stylusState_previous >> 3 & 0x1) == 1) {
				
				if ((deviceManager.devices[x].deviceStatus.toolInputs >> 3 & 0x1) == 0){
					static int scrollDownRepeatCounter = 0;
					scrollDownRepeatCounter++;
					
					if (scrollDownRepeatCounter >  desktopConfig.targetHapticFramerate / SCROLL_REPEAT_RATE) {
						mouse_event(MOUSEEVENTF_WHEEL, 0, 0, -10, 0);
						scrollDownRepeatCounter = 0;
					}
				}

				//middle click
				if ((deviceManager.devices[x].deviceStatus.toolInputs >> 4 & 0x1) == 0 && (stylusState_previous >> 4 & 0x1) == 1) {
					mouse_event(MOUSEEVENTF_MIDDLEUP, 0, 0, 0, 0);
				}
				if ((deviceManager.devices[x].deviceStatus.toolInputs >> 4 & 0x1) == 0 && (stylusState_previous >> 4 & 0x1) == 1) {
					mouse_event(MOUSEEVENTF_MIDDLEDOWN, 0, 0, 0, 0);
				}

				
				//scroll up full
				if ((deviceManager.devices[x].deviceStatus.toolInputs >> 5 & 0x1) == 0 && (stylusState_previous >> 5 & 0x1) == 1) {
					//mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);
				}
				//if ((deviceManager.devices[x].deviceStatus.toolInputs >> 5 & 0x1) == 0 && (stylusState_previous >> 5 & 0x1) == 1) {
				if ((deviceManager.devices[x].deviceStatus.toolInputs >> 5 & 0x1) == 0){
					static int scrollUpRepeatCounter = 0;
					scrollUpRepeatCounter++;

					if (scrollUpRepeatCounter > desktopConfig.targetHapticFramerate / SCROLL_REPEAT_RATE) {
						mouse_event(MOUSEEVENTF_WHEEL, 0, 0, 10, 0);
						scrollUpRepeatCounter = 0;
					}
				}

				//scroll up half
				if ((deviceManager.devices[x].deviceStatus.toolInputs >> 6 & 0x1) == 0 && (stylusState_previous >> 6 & 0x1) == 1) {
					//mouse_event(MOUSEEVENTF_WHEEL, 0, 0, 120, 0);
				}
				if ((deviceManager.devices[x].deviceStatus.toolInputs >> 6 & 0x1) == 0 && (stylusState_previous >> 6 & 0x1) == 1) {
					mouse_event(MOUSEEVENTF_WHEEL, 0, 0, 120, 0);
				}

				//side button
				if ((deviceManager.devices[x].deviceStatus.toolInputs >> 7 & 0x1) == 0 && (stylusState_previous >> 7 & 0x1) == 1) {
					mouse_event(MOUSEEVENTF_XUP, 0, 0, 0, 0);
				}
				if ((deviceManager.devices[x].deviceStatus.toolInputs >> 7 & 0x1) == 0 && (stylusState_previous >> 7 & 0x1) == 1) {
					mouse_event(MOUSEEVENTF_XDOWN, 0, 0, 0, 0);
				}

				stylusState_previous = deviceManager.devices[x].deviceStatus.toolInputs;

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