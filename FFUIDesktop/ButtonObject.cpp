#include "ButtonObject.h"
#include <cmath>

ButtonObject::ButtonObject(FFUIObject_Meta meta) : FFUIObject(meta) {
	//objectInit();
	//Should I add a vibration effect for the button? Maybe a short pulse when the button is pressed?


}

Vector3 ButtonObject::calculateInteractionForce(Location localLoc) {
    Vector3 force(0, 0, 0);



    //We adjust z axis.
    Vector3 adjustedLocalLoc(localLoc.position.x, localLoc.position.y, localLoc.position.z - 100);
    // Button center is at the local origin
    const Vector3 buttonCenter(0, 0, 0);

    Vector3 toCenter = buttonCenter - adjustedLocalLoc;

    // Guard against divide-by-zero / NaN
    if (!std::isfinite(toCenter.x) || !std::isfinite(toCenter.y) || !std::isfinite(toCenter.z)) {
        return force;
    }

    float xAttractionRange = attractionRadius + objectMeta.scale.x;
    float yAttractionRange = attractionRadius + objectMeta.scale.y;
    float zAttractionRange = attractionRadius + objectMeta.scale.z;

    bool withinX = std::abs(adjustedLocalLoc.x) < xAttractionRange;
    bool withinY = std::abs(adjustedLocalLoc.y) < yAttractionRange;
    bool withinZ = std::abs(adjustedLocalLoc.z) < zAttractionRange; // works for small or large

    if (withinX && withinY && withinZ) {
        float distance = toCenter.length();
        if (!std::isfinite(distance) || distance <= 2) {
            return force;
        }
        Vector3 dir = toCenter / distance;
        float attractionMagnitude = objectMeta.hapticSolidProperties.stiffness * distance;
        force = dir * attractionMagnitude;
        float maxForce = objectMeta.hapticSolidProperties.solidForceLimit;

        if(force.x > maxForce){ force.x = maxForce;}
        if (force.y > maxForce) { force.y = maxForce; }
        if (force.z > maxForce) { force.z = maxForce; }

    }

    return force;
}