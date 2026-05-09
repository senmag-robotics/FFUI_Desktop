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
};