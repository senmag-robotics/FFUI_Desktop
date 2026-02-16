#include "ObjectsFactory.h"

static Vector3 screenToWorkspace(Vector2 screenPos,
    Vector2 screenSize,
    float workspaceX,
    float workspaceY) {

    float x = (screenPos.x / screenSize.x - 0.5f) * workspaceX;
    float y = (0.5f - screenPos.y / screenSize.y) * workspaceY;
    return Vector3(x, y, 0);
}

