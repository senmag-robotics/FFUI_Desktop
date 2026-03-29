#pragma once
#include "HapticObjects/FFUIDesktop_object.h"

class GravityWellObject : public FFUIObject {
public:
    GravityWellObject(FFUIObject_Meta meta);

    Vector3 calculateInteractionForce(Location localLoc) override;

private:
    float attractionRadius = 10;  // How close the stylus needs to be to feel the pull
};