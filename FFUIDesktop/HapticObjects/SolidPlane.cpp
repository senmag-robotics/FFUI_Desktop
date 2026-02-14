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
		const float halfX = objectMeta.scale.x * 0.5f;
		const float halfZ = objectMeta.scale.z * 0.5f;

		// adjusted check such that it works when the plane is limited in size.
		if (localLoc.position.x > -halfX && localLoc.position.x < halfX &&
			localLoc.position.z > -halfZ && localLoc.position.z < halfZ) {

			reactionForce = -(localLoc.position.y) * objectMeta.hapticSolidProperties.stiffness;
			force.y = reactionForce;
			if (force.y > objectMeta.hapticSolidProperties.solidForceLimit) force.y = objectMeta.hapticSolidProperties.solidForceLimit;
			if (force.y < -objectMeta.hapticSolidProperties.solidForceLimit) force.y = -objectMeta.hapticSolidProperties.solidForceLimit;
			/*std::cout << "x" << force.x << "\n";
			std::cout << "y" << force.y << "\n";
			std::cout << "z" << force.z << "\n";*/

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