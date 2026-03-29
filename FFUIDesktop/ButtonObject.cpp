#include "ButtonObject.h"
#include <cmath>
#include "ObjectsFactory.h"


ButtonObject::ButtonObject(FFUIObject_Meta meta) : FFUIObject(meta) {

	//objectInit();
    // 
	//Should I add a vibration effect for the button? Maybe a short pulse when the button is pressed?
}

Vector3 ButtonObject::calculateInteractionForce(Location localLoc) {
    Vector3 force(0, 0, 0);



    ////We adjust z axis.
    //Vector3 adjustedLocalLoc(localLoc.position.x, localLoc.position.y, localLoc.position.z);
    Vector3 stylusPosition = localLoc.position;
    // Button center is at the local origin
    const Vector3 buttonCenter(0, 0, 0);

    Vector3 toCenter = buttonCenter - stylusPosition;

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

    bool withinX = std::abs(stylusPosition.x) < xAttractionRange - boundaryMinimizationRange;
    bool withinY = std::abs(stylusPosition.y) < yAttractionRange - boundaryMinimizationRange;
    bool withinZ = std::abs(stylusPosition.z) < zAttractionRange - boundaryMinimizationRange; // works for small or large

    if (withinX && withinY && withinZ && !FFUIDesktop::currentSnapAnchor.isTracking) {
        float distance = toCenter.length();

        force = toCenter * objectMeta.hapticSolidProperties.stiffness * 5;

        float maxForce = objectMeta.hapticSolidProperties.solidForceLimit;
    
        float minDimension = (std::min)(objectMeta.scale.x, objectMeta.scale.y);

        float stabilityFactor = (std::max)(minDimension, 15.0f);

        // Calculate Effective Stiffness
        // We ensure we don't exceed the stiffness of a 'stabilityFactor' sized button
        float effectiveStiffness = (objectMeta.hapticSolidProperties.stiffness * 13) / stabilityFactor;

        force = toCenter * (effectiveStiffness);
        force.z = 0;

        

        if (force.length() > maxForce) {
            force *= maxForce / force.length();
        }

 
    }
    
    //This anchoring feature doesn't work currently due to buttons actually
    //having a specific z range they exist in (because of the widnows setup). 
        if (objectMeta.snappedToThis ) {
            //printf("anchored to this \n");
            float minDimension = (std::min)(objectMeta.scale.x, objectMeta.scale.y);


            float stabilityFactor = (std::max)(minDimension, 15.0f);
            float effectiveStiffness = (objectMeta.hapticSolidProperties.stiffness * 13) / stabilityFactor;

            Vector3 here(0, 0, 150);
            force = here - stylusPosition;
            force = force * effectiveStiffness * 8;
            if (force.length() > objectMeta.hapticSolidProperties.solidForceLimit * 3) {
                force *= objectMeta.hapticSolidProperties.solidForceLimit / force.length();
            }
        }
  
    return force;
}