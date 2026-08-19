#pragma once

// Include the HapticObjects directory in the search paths
#include "HapticObjects/FFUIDesktop_object.h"

#include "WindowScanner.h"

class ButtonObject : public FFUIObject {
public:
    ButtonObject(FFUIObject_Meta meta);
    Vector3 calculateInteractionForce(Location localLoc) override;

private:

    float attractionRadius = 0.0f;  // Distance at which attraction starts (starting from edge)
    float attractionStrength = 0.002f;  // Force magnitude

    //Detent tuning.
    //The centre of the button holds a dead zone in which no attraction force is produced.
    //Without it the spring reverses sign every time the tip crosses the centre, which the
    //device renders as a buzz rather than a detent.
    //Expressed as a fraction of each half extent, so the dead zone spans
    //(detentDeadzoneFraction * scale) of the element on that axis - 0.25f = the middle
    //quarter of the button's width and height.
    float detentDeadzoneFraction = 0.25f;

    //Gain applied to the element's stiffness before the size normalisation below.
    //Lowered from 13.0f to take some energy out of the loop.
    float detentStiffnessGain = 10.0f;

    //Buttons smaller than this (in workspace units) are treated as this size when
    //normalising stiffness, so tiny elements don't end up with a huge effective gain.
    float detentStabilityFloor = 15.0f;
};