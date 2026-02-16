#pragma once
#include "FFUIDesktop_object.h"

class ButtonObject : public FFUIObject {
public:
    ButtonObject(FFUIObject_Meta meta);
    Vector3 calculateInteractionForce(Location localLoc) override;

private:

    float attractionRadius = 1.0f;  // Distance at which attraction starts
    float attractionStrength = 0.002f;  // Force magnitude
};