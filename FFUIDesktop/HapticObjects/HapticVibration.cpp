#include "HapticVibration.h"

HapticVibration::HapticVibration(VibrationSettings settings) {
	vibSettings = settings;
	vibrationProgress = 0;
	complete = false;
}
float HapticVibration::processVibration() {
	
	

	float force = 0;


	if (complete) {
		if (vibSettings.type == vibrationType_Periodic) {
			//if this is a periodic effect, 
			if (((std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime).count()) / 1000.0) > vibSettings.modulation.repeatPeriod) {
				complete = false;
				startTime = std::chrono::steady_clock::now();
			}
		}
		return force;

	}

	modulationProgress = ((std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime).count()) / 1000.0) / vibSettings.modulation.duration;
	if (modulationProgress >= 1) {
		if (vibSettings.type == vibrationType_Continuous) {
			modulationProgress -= 1;
		}
		else {
			complete = true;
			return force;
		}
		
	}

	if (vibrationProgress == 0 && modulationProgress == 0) startTime = std::chrono::steady_clock::now();


	float timestep = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - lastUpdate).count();
	lastUpdate = std::chrono::steady_clock::now();

	vibrationProgress += (timestep / 1000000.0) * vibSettings.frequency;
	if (vibrationProgress > 1) {
		vibrationProgress -= (int)vibrationProgress;
	}
	//while (vibrationProgress > 1) {
	//	vibrationProgress -= 1;
	//}



	float modulationFactor = 0;
	if (vibSettings.modulation.type == modulationType_constant) modulationFactor = vibSettings.modulation.gainMaximum;
	if (vibSettings.modulation.type == modulationType_linearIncrease) modulationFactor = vibSettings.modulation.gainMinimum + modulationProgress * (vibSettings.modulation.gainMaximum - vibSettings.modulation.gainMinimum);
	if (vibSettings.modulation.type == modulationType_linearDecrease) modulationFactor = vibSettings.modulation.gainMaximum - modulationProgress * (vibSettings.modulation.gainMaximum - vibSettings.modulation.gainMinimum);
	if (vibSettings.modulation.type == modulationType_sine) modulationFactor = vibSettings.modulation.gainMinimum + ((sinf(modulationProgress * 2 * M_PI) + 1) / 2.0) * (vibSettings.modulation.gainMaximum - vibSettings.modulation.gainMinimum);


	//std::cout << timestep << "\t" << modulationFactor << "\t" << vibrationProgress << "\n";

	
	if (vibSettings.profile == vibrationProfile_sine) force = sinf(vibrationProgress * 2 * M_PI) + vibSettings.offset;



	return force * modulationFactor;

}

void HapticVibration::sleepVibration() {
	lastUpdate = std::chrono::steady_clock::now();
	vibrationProgress = 0;
	modulationProgress = 0;
	complete = 0;
}