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


    float halfX = objectMeta.scale.x * 0.5f;
    float halfY = objectMeta.scale.y * 0.5f;
    float halfZ = objectMeta.scale.z * 0.5f;


  /*  float xAttractionRange = attractionRadius + objectMeta.scale.x;
    float yAttractionRange = attractionRadius + objectMeta.scale.y;
    float zAttractionRange = attractionRadius + objectMeta.scale.z;*/

    float xAttractionRange = attractionRadius + halfX;
    float yAttractionRange = attractionRadius + halfY;
    float zAttractionRange = attractionRadius + halfZ; 

    //Attraction will not start unless cursor is within (actual boundary - boundaryMinimizationRange)
    float boundaryMinimizationRange = 0;

    bool withinX = std::abs(adjustedLocalLoc.x) < xAttractionRange - boundaryMinimizationRange;
    bool withinY = std::abs(adjustedLocalLoc.y) < yAttractionRange - boundaryMinimizationRange;
    bool withinZ = std::abs(adjustedLocalLoc.z) < zAttractionRange - boundaryMinimizationRange; // works for small or large

    if (withinX && withinY && withinZ) {
        float distance = toCenter.length();

        force = toCenter * objectMeta.hapticSolidProperties.stiffness * 5;
      //  if (!std::isfinite(distance) ) {
      //      return force;
      //  }

      //  Vector3 dir = toCenter / distance;
      //  float attractionMagnitude = objectMeta.hapticSolidProperties.stiffness * distance * 10;

      //  //force = dir * attractionMagnitude;
        float maxForce = objectMeta.hapticSolidProperties.solidForceLimit;

        Vector3 dir = toCenter / distance;

    
        float minDimension = std::min(objectMeta.scale.x, objectMeta.scale.y);

       
     
        float stabilityFactor = std::max(minDimension, 10.0f);

        // Calculate Effective Stiffness
        // We ensure we don't exceed the stiffness of a 'stabilityFactor' sized button
        float effectiveStiffness = (objectMeta.hapticSolidProperties.stiffness * 10) / stabilityFactor;

        force = dir * (effectiveStiffness * distance);
        force.z = 0;

        if (force.length() > maxForce) {
            force *= maxForce / force.length();
        }

    }

    return force;
}