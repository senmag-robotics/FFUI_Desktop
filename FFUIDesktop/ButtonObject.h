#pragma once
#include "FFUIDesktop_object.h"
#include "WindowScanner.h"

class ButtonObject : public FFUIObject {
public:
    ButtonObject(FFUIObject_UIMeta meta);
    Vector3 calculateInteractionForce(Location localLoc) override;

private:

    float attractionRadius = 0.0f;  // Distance at which attraction starts
    float attractionStrength = 0.002f;  // Force magnitude
};