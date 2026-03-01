#pragma once
#include <iostream>
#include <vector>
#include <memory>
#include <string>

#include "HapticVibration.h"

#include "../dataTypes.h"
#include "../mathTypes.h"



typedef enum {
	objectType_hapticPlane,
	objectType_taskbar,
	objectType_window,
	objectType_desktopItem,
	objectType_button,
	objectType_menu,


}FFUIObject_Types;



typedef struct {
	float solidForceLimit;
	float stiffness;
	float damping;
	float frictionStatic;
	float frictionDynamic;
}HapticSolidProperties;

typedef struct {
	Vector3		globalPosition;
	Vector3		scale;
	Quaternion	orientation;
	HapticSolidProperties	hapticSolidProperties;
	std::string		customName;
	
}FFUIObject_Meta;



class FFUIObject {
public:
	FFUIObject(FFUIObject_Meta meta);
	virtual ~FFUIObject() = default;

	Vector3			updateForces(Location cursorLocation);
	
	const FFUIObject_Meta& getMeta() const {
		return objectMeta;
	}

	virtual Vector3	calculateInteractionForce(Location localLoc);
	static Vector3 calculateSnapForceToThis(Location stylusLocation);

	

protected:

	FFUIObject_Meta	objectMeta;



private:
	Location getLocalStylusLocation(Location stylusPosition);	//uses this objects position & orientation to return the relative position&orientation of the stylus
	Vector3	forceLocalToGlobal(Vector3 force);

	//std::vector<FFUIObject> children;
	std::vector<std::unique_ptr<FFUIObject>> children;

	

};