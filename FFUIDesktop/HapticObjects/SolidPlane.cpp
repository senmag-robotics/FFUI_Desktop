#include "SolidPlane.h"
#include <iostream>

SolidPlane::SolidPlane(FFUIObject_Meta meta) : FFUIObject(meta) {
	objectInit();

	VibrationSettings vibrationSettings;
	vibrationSettings.type = vibrationType_Periodic;
	vibrationSettings.frequency = 100;
	vibrationSettings.offset = 1;

	vibrationSettings.profile = vibrationProfile_sine;

	vibrationSettings.modulation.type = modulationType_sine;
	vibrationSettings.modulation.duration = 0.1;
	vibrationSettings.modulation.repeatPeriod = .2;
	vibrationSettings.modulation.gainMaximum = .0015;
	vibrationSettings.modulation.gainMinimum = 0;

	HapticVibration newVibration(vibrationSettings);
	vibrationEffects.push_back(newVibration);

}


void SolidPlane::objectInit() {

}


Vector3 SolidPlane::calculateInteractionForce(Location localLoc) {
	Vector3 force = Vector3(0, 0, 0);
	if (localLoc.position.y < 0) {
		float reactionForce = 0;
		if (
		(localLoc.position.x > objectMeta.globalPosition.x - objectMeta.scale.x / 2 && localLoc.position.x < objectMeta.globalPosition.x + objectMeta.scale.x / 2)
		&& (localLoc.position.z > objectMeta.globalPosition.z - objectMeta.scale.z / 2 && localLoc.position.z < objectMeta.globalPosition.z + objectMeta.scale.z / 2)){

			reactionForce = -(localLoc.position.y) * objectMeta.hapticSolidProperties.stiffness;
			force.y = reactionForce;
			if (force.y > objectMeta.hapticSolidProperties.solidForceLimit) force.y = objectMeta.hapticSolidProperties.solidForceLimit;
			if (force.y < -objectMeta.hapticSolidProperties.solidForceLimit) force.y = -objectMeta.hapticSolidProperties.solidForceLimit;
			//std::cout << localLoc.position.y << "\n";
		}
		
		if (abs(reactionForce) > objectMeta.hapticSolidProperties.solidForceLimit *1.5) {
			for (int x = 0; x < vibrationEffects.size(); x++) force.y += vibrationEffects[x].processVibration();
		}
	}
	else {
		for (int x = 0; x < vibrationEffects.size(); x++) vibrationEffects[x].sleepVibration();
	}
	

	return force;
}