#include "DeviceManager.h"
#include <iostream>


DeviceManager::DeviceManager() {
	numD2XXDevs = 0;
}

void DeviceManager::update() {

	//FT_CreateDeviceInfoList (and, whenever the reported count changes, regenerateDeviceList()'s
	//FT_Open calls) used to run unconditionally on EVERY call to update() - i.e. up to ~500
	//times a second, since this is called every haptic frame from FFUIDesktop::updateFrame() on
	//the same real-time thread that also drives cursor movement via SendInput. Neither
	//FT_CreateDeviceInfoList nor FT_Open has any documented timeout of its own, so any transient
	//slowness in Windows' USB/FTDI driver stack (not unusual right after boot, after
	//sleep/resume, or with a flaky hub) stalled this entire thread for however long that call
	//happened to take - the most likely explanation for FFUI's own reported inconsistent,
	//sometimes very long, startup/lockup behavior, since a stalled thread here also freezes the
	//cursor it drives. Throttled to roughly once a second instead: newly-connected hardware
	//doesn't need to be noticed at haptic-frame rate, and this bounds how often the
	//expensive/blocking path can run at all, without changing anything about how it behaves once
	//it does run. Telemetry polling for already-open, already-validated devices
	//(checkDeviceUpdates() below) is untouched and still runs every call - that part only ever
	//touches devices already known to be responding correctly, so there's no equivalent hang
	//risk there to throttle.
	auto now = std::chrono::steady_clock::now();
	if (now >= nextDeviceScanTime) {
		nextDeviceScanTime = now + DEVICE_SCAN_INTERVAL;

		DWORD numDevs = 0;
		FT_CreateDeviceInfoList(&numDevs);
		if (numDevs != numD2XXDevs) {
			numD2XXDevs = numDevs;
			regenerateDeviceList();
		}
	}

	checkDeviceUpdates();
}


void DeviceManager::regenerateDeviceList() {
	devices.clear();
	for (int x = 0; x < numD2XXDevs; x++) {
		//Logged, not yet filtered on: this loop opens, configures (1.5Mbaud etc.), and pings
		//with the Senmag protocol every FTDI-chip device FT_CreateDeviceInfoList finds, whatever
		//it actually is - there's no check anywhere confirming a given device is really the
		//Senmag LibreOne stylus before doing this. The existing deviceValid/ping-timeout state
		//machine in checkDeviceUpdates() already rejects a non-responding device correctly (a
		//wrong device just never replies with packetType_settings and gets marked invalid after
		//3 timeouts), so nothing here is unsafe - it's just slower than it needs to be whenever
		//other FTDI hardware is connected, and silently touches hardware it has no business
		//touching. Logging each candidate's description/serial here is the prerequisite for
		//adding a real product-string filter safely - guessing at that string without first
		//confirming it from a real run risks silently excluding the actual device instead of the
		//unrelated ones, which would be worse than today's slow-but-correct behavior.
		DWORD infoFlags = 0, infoType = 0, infoId = 0, infoLocId = 0;
		char serialNumber[16] = {};
		char description[64] = {};
		FT_HANDLE existingHandle = nullptr;
		if (FT_GetDeviceInfoDetail(x, &infoFlags, &infoType, &infoId, &infoLocId,
				serialNumber, description, &existingHandle) == FT_OK) {
			//Defensively force null-termination regardless of what the driver wrote - ftd2xx.h
			//itself documents that an over-length serial number can come back without one.
			serialNumber[sizeof(serialNumber) - 1] = '\0';
			description[sizeof(description) - 1] = '\0';
			std::cout << "[Device Manager] FTDI device " << x << ": description=\"" << description
				<< "\", serial=\"" << serialNumber << "\"" << std::endl;
		}

		//Value-initialized (matches the {} pattern already used throughout this codebase for
		//STARTUPINFOW/WAVEHDR/etc.), not left default-initialized - a bare "SenmagDevice newDev;"
		//left newStatus/deviceStatus/deviceValid as uninitialized garbage until the first real
		//status packet overwrote them, which is undefined behavior (their initial values, and
		//so this device's very first processed frame in FFUIDesktop::updateFrame(), depended on
		//whatever happened to already be sitting in that memory) rather than a deliberate,
		//predictable "not active yet" starting state.
		SenmagDevice newDev{};
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