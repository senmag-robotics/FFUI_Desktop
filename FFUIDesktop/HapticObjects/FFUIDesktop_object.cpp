#include "FFUIDesktop_object.h"

FFUIObject::FFUIObject(FFUIObject_Meta meta) {
	objectMeta = meta;

	//objectInit();
}

Vector3 FFUIObject::updateForces(Location cursorLocation) {
	Vector3 force = { 0,0,0 };

	//process interaction with any child objects
	//for (int x = 0; x < children.size(); x++) force += children[x]->updateForces(cursorLocation);		//pass the global location to children for processing...
	force += forceLocalToGlobal(calculateInteractionForce(getLocalStylusLocation(cursorLocation)));//pass the local position of the cursor to this object for processing...
	
	
	return force;
}


Location FFUIObject::getLocalStylusLocation(Location stylusLocation) {
	Location localLoc;
	Vector3 localPosition = stylusLocation.position - objectMeta.globalPosition;
	// Rotate by inverse of object orientation
	Quaternion invRot = objectMeta.orientation.inverse();
	localLoc.position = invRot.rotate(localPosition);
	return localLoc;
}

Vector3 FFUIObject::forceLocalToGlobal(Vector3 force) {
	return objectMeta.orientation.rotate(force);
}

Vector3 FFUIObject::calculateInteractionForce(Location localPos) {
	// base version (maybe just zero force)
	return Vector3(0, 0, 0);
}

Vector3 FFUIObject::calculateSnapForceToThis(Location stylusLocation)
{

	return Vector3();
}

