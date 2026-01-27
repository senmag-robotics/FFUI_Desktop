#pragma once

#include "../mathTypes.h"
#include <chrono>
#include <iostream>

typedef enum {
	vibrationProfile_sine,
	//vibrationProfile_square,
	//vibrationProfile_trapezoid,
	//vibrationProfile_custom,	
}VibrationProfile;


typedef enum {
	vibrationType_oneOff,
	vibrationType_Continuous,
	vibrationType_Periodic,		//

}VibrationType;


typedef enum {
	modulationType_constant,
	modulationType_linearIncrease,
	modulationType_linearDecrease,
	modulationType_sine,
	//modulationType_triangle,
}ModulationType;

typedef struct {
	ModulationType type;
	float gainMaximum;		//the maximum value of the modulation
	float gainMinimum;		//the minimum value of the modulation
	float duration;			//complete cycles
	float repeatPeriod;		//in periodic mode, repeat the effect every x seconds


}VibrationModulation;


typedef struct {
	VibrationType		type;
	float				frequency;		//the frequecy of the vibration force
	float				offset;			//the DC offset of the vibration force

	VibrationProfile	profile;
	VibrationModulation modulation;

	
}VibrationSettings;

class HapticVibration {
public:
	HapticVibration(VibrationSettings settings);
	float				processVibration();
	void				sleepVibration();

	//settings:




	bool complete;

	//program memory:
private:
	VibrationSettings vibSettings;
	float vibrationProgress;
	float modulationProgress;
	float currentAmplitude;
	std::chrono::time_point<std::chrono::steady_clock>	lastUpdate;
	std::chrono::time_point<std::chrono::steady_clock>	startTime;
};