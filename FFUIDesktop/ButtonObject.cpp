#include "ButtonObject.h"
#include <cmath>

ButtonObject::ButtonObject(FFUIObject_Meta meta) : FFUIObject(meta) {
	//objectInit();
	//Should I add a vibration effect for the button? Maybe a short pulse when the button is pressed?


}

Vector3 ButtonObject::calculateInteractionForce(Location localLoc) {
    Vector3 force(0, 0, 0);

    //We ignore the z axis for now.
    Vector3 adjustedLocalLoc(localLoc.position.x, localLoc.position.y, localLoc.position.z - 100);
    // Button center is at the local origin
    const Vector3 buttonCenter(0, 0, 0);

    Vector3 toCenter = buttonCenter - adjustedLocalLoc;
    float distance = toCenter.length();

    // Guard against divide-by-zero / NaN
    if (!std::isfinite(distance) || distance <= 0.0f) {

        return force;
    }

    // Apply attraction only within radius
    if (distance < (attractionRadius) && distance > 0.5f) {
        printf("%f", distance);

        Vector3 dir = toCenter / distance; // normalized
        float attractionMagnitude = objectMeta.hapticSolidProperties.stiffness * distance;
        force = dir * attractionMagnitude;
     /*   force.x =  attractionMagnitude;
        force.y = attractionMagnitude;*/
    }

    return force;
}