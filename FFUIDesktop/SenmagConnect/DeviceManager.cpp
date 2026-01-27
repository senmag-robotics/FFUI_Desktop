#include "DeviceManager.h"


DeviceManager::DeviceManager() {
	numD2XXDevs = 0;
}

void DeviceManager::update() {

	//run a check for new devices
	DWORD numDevs;
	FT_CreateDeviceInfoList(&numDevs);
	if (numDevs != numD2XXDevs) {
		numD2XXDevs = numDevs;
		regenerateDeviceList();;
	}
	checkDeviceUpdates();
}


void DeviceManager::regenerateDeviceList() {
	devices.clear();
	for (int x = 0; x < numD2XXDevs; x++) {
		SenmagDevice newDev;
		devices.push_back(newDev);

		if (devices[x].serialComms.openPort(x) == 0) {
			devices[x].deviceValid = 0;				//flag device as pending
			devices[x].discoveryTimeout = std::chrono::steady_clock::now();		//flag to wait for device validation
			devices[x].discoveryattempts = 1;
		}
		else {
			devices[x].deviceValid = -1;
			devices[x].serialComms.closePort();
		}
	}
}


void DeviceManager::checkDeviceUpdates() {
	for (int x = 0; x < devices.size(); x++) {
		if (devices[x].deviceValid != -1) {
			int newMessage = 0;
			do {
				newMessage = devices[x].serialComms.checkUpdates();
				if (newMessage == CommsPacketType::packetType_settings) {
					devices[x].deviceSettings = devices[x].serialComms.rPacket.settings;
					devices[x].deviceValid = 1;
				}
				else if (devices[x].deviceValid == 1 && newMessage == CommsPacketType::packetType_status) {
					devices[x].deviceStatus = devices[x].serialComms.rPacket.status;
					devices[x].newStatus = 1;
				}

				if (devices[x].deviceValid == 0) {
					if(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - devices[x].discoveryTimeout).count() >= DEVICE_DISCOVER_TIMEOUT_PERIOD){
						if (devices[x].discoveryattempts < 3) {
							devices[x].serialComms.sendPing();
							devices[x].discoveryattempts++;
							devices[x].discoveryTimeout = std::chrono::steady_clock::now();
						}
						else {
							devices[x].serialComms.closePort();
							devices[x].deviceValid = -1;
						}
					}
				}


			} while (newMessage != -1);
		}
	}

	return;
}