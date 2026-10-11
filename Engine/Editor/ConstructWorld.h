#pragma once
#include "../GeometricRaster/SceneStructure.h"
#include "../DisplayPresentation/IconArt.h"
#include <string>

namespace Frontier {
// Authoring-thread API. Consumers must rebuild/upload traversal and invalidate history
// before rendering a changed scene; never call this from a render-thread draw callback.
enum class ConstructKind { Cube, Sphere, Cylinder, Cone, Plane, Torus, Area, Camera, Empty, PointLight, SpotLight, DirectionalLight, RectangleLight, TubeLight, StripLight, Count };
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
// The five base meshes the shipped editor opens with carry their own artwork, and everything downstream
//    reads the primitive back off it (FractureSpecification.js Describe). Anything else is Count.
IconSymbol  ConstructArtwork(ConstructKind Kind);
const char* ConstructPrimitive(ConstructKind Kind);
ConstructResult ConstructEntity(SceneStructure& World, const ConstructRequest& Request,
                                uint32_t SlabLimit = 1);
}
