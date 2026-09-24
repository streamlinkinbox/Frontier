#pragma once
#include "../GeometricRaster/SceneStructure.h"
#include <string>

namespace Frontier {
// Authoring-thread API. Consumers must rebuild/upload traversal and invalidate history
// before rendering a changed scene; never call this from a render-thread draw callback.
enum class ConstructKind { Cube, Sphere, Cylinder, Cone, Plane, Torus, Area, Camera, Empty, Count };
struct ConstructRequest {
    ConstructKind Kind = ConstructKind::Cube;
    std::string Name;
    float Position[3] = {0, 0, 0};
    float Size = 1;
};
struct ConstructResult {
    uint32_t Placement = kPlacementNone;
    std::string Error;
    explicit operator bool() const { return Placement != kPlacementNone; }
};
const char* ConstructName(ConstructKind Kind);
ConstructResult ConstructEntity(SceneStructure& World, const ConstructRequest& Request,
                                uint32_t SlabLimit = 1);
}
