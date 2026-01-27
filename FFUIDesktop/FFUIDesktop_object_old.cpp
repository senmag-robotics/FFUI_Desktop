#include "FFUIDesktop_object.h"

FFUIObject::FFUIObject(FFUIObject_Meta meta){
	objectMeta = meta;
	//objectInit();
}

Vector3 FFUIObject::updateForces(Location cursorLocation) {
	Vector3 force = { 0,0,0 };

	//process interaction with any child objects
	for (int x = 0; x < children.size(); x++) force += children[x].updateForces(cursorLocation);
	//force += calculateInteractionForce(cursorLocation);

	return force;
}




Vector3	FFUIObject::getLocalStylusPosition(Location stylusLocation) {
	Vector3 localPosition = stylusLocation.position - objectMeta.globalPosition;
    // Rotate by inverse of object orientation
    Quaternion invRot = objectMeta.orientation.inverse();
    return invRot.rotate(localPosition);
}