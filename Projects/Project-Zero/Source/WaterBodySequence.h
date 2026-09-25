#pragma once
#include "../../../Engine/GeometricRaster/SceneStructure.h"
#include "../../Project-Fluid/Source/PondWave.h"

namespace Frontier::ProjectZero {
// Load-time authoring bridge. Call before Finalise/BVH creation/UploadScene.
// Does NOT update resident GPU vertex buffers or provide a live fluid inspector.
struct WaterBodySettings {
    Vector3 Origin{0,0,.2f}; // metres, engine Z-up
    float Width=12, Length=8;
    uint32_t Rows=48;
    float Disturbance=.65f, SettleSeconds=.25f;
};
struct WaterBodyRegistration {
    uint32_t Placement, FirstInstance, InstanceCount, Material;
};
void BuildPondGeometry(const ProjectFluid::PondWave& Pond, Vector3 Origin, GeometryStructure& Mesh);
WaterBodyRegistration AppendPondSnapshot(SceneStructure& Level,const WaterBodySettings& Settings={});
}
