#include "main.h"
#include <windows.h>


void main(void) {
	DeviceManager	deviceManager;
	FFUIDesktop		ffuiDesktop;


	FFUIDesktop_Config desktopConfig;
	desktopConfig.targetHapticFramerate = 100;
	desktopConfig.targetUIFramerate = 10;
	desktopConfig.screenSize.x = GetSystemMetrics(SM_CXSCREEN);
	desktopConfig.screenSize.y = GetSystemMetrics(SM_CYSCREEN);
	desktopConfig.cursorFilter = 0.99;

	ffuiDesktop.initDesktop(desktopConfig);




	float cursorGain = 20;






	while (1) {
		ffuiDesktop.updateFrame();

		//Sleep(10);
	}

}