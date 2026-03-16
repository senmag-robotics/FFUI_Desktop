#include "WindowWallObject.h"

WindowWallObject::WindowWallObject(WindowWallMeta wallMeta, Vector3 position, float thickness, float stiffness,
    float solidForceLimit, float height, float width)
    : FFUIObject({
        position,                                    // globalPosition
        {width, thickness , height},                             // scale 
        Quaternion().setFromEuler(0, 0, 90),                                // orientation
        {solidForceLimit, stiffness, 0.0f, 0.0f, 0.0f},                  // hapticSolidProperties
        std::string(wallMeta.windowTitle.begin(), wallMeta.windowTitle.end()), // customName (wstring to string conversion)
        false                                                    // snappedToThis
        }),
    windowMeta(wallMeta) // Initialize the UI properties

{
}

Vector3 WindowWallObject::calculateInteractionForce(Location localLoc) {
    Vector3 force(0, 0, 0);
    if (isArchived()) return force;
    //printf("title: %s\n", objectMeta.customName);
  //  printf("%f \n", localLoc.position.x);
    Vector3 stylusPosition = localLoc.position;

    float halfX = objectMeta.scale.x * 0.5f;
    float halfY = objectMeta.scale.y * 0.5f;
    float halfZ = objectMeta.scale.z * 0.5f;
    bool withinX = std::abs(stylusPosition.x) < halfX;
    bool withinY = std::abs(stylusPosition.y) < halfY;
    bool withinZ = std::abs(stylusPosition.z) < halfZ;

    if (withinY
        && withinX
        && withinZ
        ) {
        printf("inside\n");
        printf("done\n");

    }
    


    return force;
}