
#include "GravityWellObject.h"

GravityWellObject::GravityWellObject(FFUIObject_Meta meta) : FFUIObject(meta) {

}


Vector3 GravityWellObject::calculateInteractionForce(Location localLoc) {

    Vector3 force(0, 0, 0);
    Vector3 stylusPosition = localLoc.position;

   

    // Guard against divide-by-zero or NaN




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
    bool withinZ = std::abs(stylusPosition.z) < zAttractionRange - boundaryMinimizationRange; // works for small or large z


    const Vector3 center(0, 0, 0);

    Vector3 toCenter = center - stylusPosition;

    if (!std::isfinite(toCenter.x) || !std::isfinite(toCenter.y) || !std::isfinite(toCenter.z)) {
        return force;
    }

    correspondingWindowMeta.stylusOnThis = false;

    if (withinX && withinY && withinZ) {
        correspondingWindowMeta.stylusOnThis = true;
    
        force = toCenter * objectMeta.hapticSolidProperties.stiffness;

        float forceLimit = objectMeta.hapticSolidProperties.solidForceLimit;
        if (force.length() > forceLimit) {
            force = force.normalized() * forceLimit;
        }

        //Narrate which slot the stylus is currently hovering while grabbing/dragging a window -
        //same Slot-N/app-name rule as ordinary focus narration, no special-cased phrasing.
        WindowManager::getInstance().narrateWindowFocus(correspondingWindowMeta.windowHandle);

    }
    else {
        WindowManager::getInstance().resetNarratedWindow(correspondingWindowMeta.windowHandle);
    }

    return force;
}
