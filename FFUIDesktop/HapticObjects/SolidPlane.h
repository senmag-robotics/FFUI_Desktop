#pragma once
#include "FFUIDesktop_object.h"
#include "HapticVibration.h"
#include "../mathTypes.h"


class SolidPlane : public FFUIObject
{
public:
	SolidPlane(FFUIObject_Meta meta);
	//Vector3 calculateInteractionForce(Location localPos) override;
	Vector3	calculateInteractionForce(Location localLoc) override;

private:
	std::vector<HapticVibration> vibrationEffects;
	void objectInit();


};

