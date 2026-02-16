#pragma once
#include "dataTypes.h"
#include "FFUIDesktop_object.h"
#include "mathTypes.h"
#include <windows.h>
#include "SenmagConnect.h"

#include "DeviceManager.h"

#include "HapticVibration.h"
#include "SolidPlane.h"
#include "ButtonObject.h"
#define DEVICE_WORKSPACE_X	200
#define DEVICE_WORKSPACE_Y	100


#define DEVICE_WORKSPACE_OFFSETX	0
#define DEVICE_WORKSPACE_OFFSETY	0

#define SCROLL_REPEAT_RATE	10			//scroll ops per second


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

	void		initDesktop(FFUIDesktop_Config config);
	void		updateFrame();
	void		moveWindowsCursor(Vector2 targetPos);
	Vector3		processForces(Location stylusLocation);


	std::vector<FFUIDesktop_Layer> layers;
	DeviceManager deviceManager;

	std::vector<std::unique_ptr<FFUIObject>> objects;

private:
	void addBoundaryPlanes();
	FFUIDesktop_Config	desktopConfig;

	Vector2 cursorPos;


	Vector2 cusrsorScale;		//the scale factor between device workspace and digital workspace
};