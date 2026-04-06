#pragma once
#include "HapticObjects/FFUIDesktop_object.h"
#include "WindowWallObject.h"

class GravityWellObject : public FFUIObject {
public:
    GravityWellObject(FFUIObject_Meta meta);

    Vector3 calculateInteractionForce(Location localLoc) override;
    WindowWallMeta correspondingWindowMeta;
    
private:
    float attractionRadius = 10;  // How close the stylus needs to be to feel the pull
};