#pragma once
#include "HapticObjects/FFUIDesktop_object.h"
#include "DetentTuning.h"
#include <string>

//A synthetic, world-axis-aligned haptic tile. The FFUI Settings grid and every menu rendered by
//MenuSystem (the two top-level settings entries, voice choices, the voice-speed slider, "back",
//etc.) are all built from these - see GridTileObject.cpp and MenuSystem.h.
//
//Deliberately modeled on ButtonObject's simple, already-proven detent pattern rather than
//WindowWallObject's rotated local frame: this class uses the same near-identity orientation
//(Quaternion().setFromEuler(1,0,0), already used by ButtonObject/GravityWellObject/
//ObjectFactory::createDemoObject throughout this codebase) so its local X/Y/Z axes line up with
//world X/Y/Z directly - every position and force below can be reasoned about in plain
//world-space terms, with no rotation math to get wrong.
class GridTileObject : public FFUIObject {
public:
    GridTileObject(FFUIObject_Meta meta, std::wstring label, int tileIndex);

    Vector3 calculateInteractionForce(Location localLoc) override;

    //True the instant this tile's own updateForces() ran and found the stylus within its
    //bounds - read back by WindowManager's section-magnetism pass and by MenuSystem to find
    //which tile (if any) is currently highlighted, mirroring WindowWallMeta::stylusOnThis /
    //GravityWellObject's correspondingWindowMeta.stylusOnThis.
    bool stylusIsOnThis() const { return stylusOnThis; }

    const std::wstring& getLabel() const { return label; }
    int getTileIndex() const { return tileIndex; }

private:
    std::wstring label;
    int tileIndex;
    bool stylusOnThis = false;

    //Same dead-zone detent pattern as ButtonObject - see that class's own comment for why the
    //dead zone exists (a continuous, non-buzzing pull-to-center feel rather than a spring that
    //reverses sign every time the tip crosses the exact center). Default from DetentTuning.h -
    //the single shared "global setting" every synthetic tile's detent (this one, and Program
    //Tray's own copy in WindowWallObject.cpp) now pulls from - rather than a fixed literal
    //here, so per-instance members still exist if a future tile ever needs its own feel, but
    //changing DetentTuning.h alone retunes all of them at once.
    float detentDeadzoneFraction = DetentTuning::DEADZONE_FRACTION;
    float detentStiffnessGain = DetentTuning::STIFFNESS_GAIN;
    float detentStabilityFloor = DetentTuning::STABILITY_FLOOR;
};
