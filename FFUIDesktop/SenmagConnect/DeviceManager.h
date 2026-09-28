#include "SerialComms.h"
#include <vector>
#include <chrono>


#define DEVICE_DISCOVER_TIMEOUT_PERIOD	1000		//milliseconds allowed for a device to reply to the ping

typedef struct {
public:
	bool				newStatus;
	LibreOne_status		deviceStatus;
	LibreOne_settings	deviceSettings;
	int					deviceValid;
	std::chrono::time_point<std::chrono::steady_clock>	discoveryTimeout;
	int					discoveryattempts;
	SerialComms			serialComms;

private:

	uint8_t				lastButtons;

	
}SenmagDevice;


class DeviceManager {
public:
	DeviceManager();
	void update();


	std::vector<SenmagDevice> devices;


	DWORD	numD2XXDevs;
private:

	void regenerateDeviceList();
	void checkDeviceUpdates();

	//Throttles how often update() looks for newly-connected/removed hardware (FT_CreateDeviceInfoList,
	//and regenerateDeviceList()'s FT_Open calls whenever the count changes) - see update()'s own
	//comment for why this used to run on every single call (up to ~500/sec) and what that caused.
	//Telemetry polling for already-open devices in checkDeviceUpdates() is NOT throttled by this -
	//it still runs every call, same as before.
	std::chrono::steady_clock::time_point nextDeviceScanTime{};
	static constexpr std::chrono::seconds DEVICE_SCAN_INTERVAL{ 1 };

};