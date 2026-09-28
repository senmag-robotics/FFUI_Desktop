#include "GridTileObject.h"
#include <cmath>
#include <algorithm>

GridTileObject::GridTileObject(FFUIObject_Meta meta, std::wstring labelIn, int tileIndexIn)
    : FFUIObject(meta), label(std::move(labelIn)), tileIndex(tileIndexIn) {
}

//Removes the dead zone from a signed offset and keeps the result continuous - identical helper
//to ButtonObject.cpp's static applyDeadzone(); duplicated rather than shared across a header
//since it's a single three-line function and neither file currently has a shared "detent utils"
//home.
static float gridTileApplyDeadzone(float offset, float deadzone) {
    if (std::abs(offset) <= deadzone) return 0.0f;
    return offset - std::copysign(deadzone, offset);
}

Vector3 GridTileObject::calculateInteractionForce(Location localLoc) {
    Vector3 force(0, 0, 0);
    Vector3 stylusPosition = localLoc.position;
    const Vector3 tileCenter(0, 0, 0);
    Vector3 toCenter = tileCenter - stylusPosition;

    if (!std::isfinite(toCenter.x) || !std::isfinite(toCenter.y) || !std::isfinite(toCenter.z)) {
        return force;
    }

    float halfX = objectMeta.scale.x * 0.5f;
    float halfY = objectMeta.scale.y * 0.5f;
    float halfZ = objectMeta.scale.z * 0.5f;

    bool withinX = std::abs(stylusPosition.x) < halfX;
    bool withinY = std::abs(stylusPosition.y) < halfY;
    bool withinZ = std::abs(stylusPosition.z) < halfZ;

    stylusOnThis = false;

    if (withinX && withinY && withinZ) {
        stylusOnThis = true;

        float maxForce = objectMeta.hapticSolidProperties.solidForceLimit;
        float minDimension = (std::min)(objectMeta.scale.x, objectMeta.scale.y);
        float stabilityFactor = (std::max)(minDimension, detentStabilityFloor);
        float effectiveStiffness = (objectMeta.hapticSolidProperties.stiffness * detentStiffnessGain) / stabilityFactor;

        float deadzoneX = halfX * detentDeadzoneFraction;
        float deadzoneY = halfY * detentDeadzoneFraction;

        Vector3 pullOffset(
            gridTileApplyDeadzone(-toCenter.x, deadzoneX),
            gridTileApplyDeadzone(-toCenter.y, deadzoneY),
            0);

        force = -pullOffset * effectiveStiffness;
        force.z = 0;

        if (force.length() > maxForce) {
            force *= maxForce / force.length();
        }
    }

    return force;
}
