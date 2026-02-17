#pragma once
#include "FFUIDesktop_object.h"

class ButtonObject : public FFUIObject {
public:
    ButtonObject(FFUIObject_Meta meta);

private:
    Vector3 calculateInteractionForce(Location localLoc) override;
    
    float attractionRadius = 0.0f;  // Distance at which attraction starts
    float attractionStrength = 0.002f;  // Force magnitude
};