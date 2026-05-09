#pragma once
#include "dataTypes.h"
#include "FFUIDesktop_object.h"
#include "mathTypes.h"
#include <windows.h>
#include "SenmagConnect.h"
#include <thread> 
#include <stop_token>
#include "DeviceManager.h"

#include "HapticVibration.h"
#include "SolidPlane.h"
#include "ButtonObject.h"
#include <sapi.h>
//We use these libraries to upgrade what the console can print with wcout
#include <fcntl.h>
#include <io.h>


#define DEVICE_WORKSPACE_X	250
#define DEVICE_WORKSPACE_Y	180

//The range of the z axis values that the device reaches (not counting the front wall)
#define startZ	124.0f
#define endZ	268.0f

# define roomDepth 50


#define DEVICE_WORKSPACE_OFFSETX	0
#define DEVICE_WORKSPACE_OFFSETY	0

#define SCROLL_REPEAT_RATE	10			//scroll ops per second



#include <atomic>

inline ISpVoice* pSapiVoice = nullptr;


inline std::atomic<bool> focusRequested{ false };
inline Vector2 focusPixelTarget{ 0, 0 };
inline std::string lastFocusedObjectName = ""; // To prevent NVDA from stuttering


struct SnapAnchor {
	bool isTracking = false;
	std::string objectWindowsName;
	Vector3 originalPosition = Vector3(0, 0, 0);
};


typedef struct {
	int			targetHapticFramerate;
	int			targetUIFramerate;
	Vector2		screenSize;

	float		cursorFilter;
}FFUIDesktop_Config;


typedef struct {
public:
	std::vector<std::unique_ptr<FFUIObject>> objects;
private:
	

}FFUIDesktop_Layer;


class FFUIDesktop {
public:
	static SnapAnchor currentSnapAnchor;

	void		initDesktop(FFUIDesktop_Config config);
	void		updateFrame();
	void		moveWindowsCursor(Vector2 targetPos);
	Vector3		processForces(Location stylusLocation);

	void initiatePeriodicScanner(std::stop_token stoken, FFUIDesktop_Config config);


	std::jthread scannerThread; 

	std::vector<FFUIDesktop_Layer> layers;
	DeviceManager deviceManager;

	std::mutex objectsListMutex;

	std::vector<std::unique_ptr<FFUIObject>> objects;
	static inline FFUIDesktop_Config	desktopConfig;

private:
	//bool button3Clicked = false;

	void addBoundaryPlanes(std::vector<std::unique_ptr<FFUIObject>>& targetList);

	bool stylusSnapped = false;

	Vector2 cursorPos;
	Vector3 calculateForceToClosestObject(Location deviceLoc,  bool buttonClicked);

	Vector2 cusrsorScale;		//the scale factor between device workspace and digital workspace
};