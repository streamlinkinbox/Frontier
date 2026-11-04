//============================================================================================================================================
//                                                       ENVIRONMENTVISIBILITY.SLANG
//============================================================================================================================================
// 🧩 Hands the resolved illumination to the PlanetaryAtmosphere environment: the four layers its VisibilityProjection would write
//    (radiance + reversed depth, routed ray origin, routed ray direction + hit distance, glass tint) on its y-up axes.

#pragma once
#include "IntrinsicParity.slang"
#include "RefreshDescriptor.slang"

// 📝 Not part of the VehicleHMI demo: this unit only bridges the ported refresh to the environment drawn over it.
  [[vk::binding(1, 0)]] Texture2D<float4>                    ResolvedRadiance;
  [[vk::binding(2, 0)]] Texture2D<float>                     SurfaceDepth;
  [[vk::binding(3, 0)]] [format("rgba32f")] RWTexture2D<float4> VisibilityRadiance;   // layer 0: radiance, reversed depth
  [[vk::binding(4, 0)]] [format("rgba32f")] RWTexture2D<float4> VisibilityOrigin;     // layer 1: ray origin, realm
  [[vk::binding(5, 0)]] [format("rgba32f")] RWTexture2D<float4> VisibilityDirection;  // layer 2: ray direction, hit distance
  [[vk::binding(6, 0)]] [format("rgba32f")] RWTexture2D<float4> VisibilityGlass;      // layer 3: glass tint, portal + 1

// 📝 RadianceScale converts the refresh's radiometric scale into the environment's (its sun irradiance ≈ 1.38 where the refresh's
//    is 12); RestoreEnvironmentRadiance undoes it on the overlaid image, so the environment's own constants keep their designed
//    balance and the refresh's eye adaptation never sees the conversion.
struct EnvironmentImportProfile
{
    float  RadianceScale;   // [-]
    float3 Reserved;
};
  [[vk::binding(7, 0)]] ConstantBuffer<EnvironmentImportProfile> Import;
  [[vk::binding(8, 0)]] [format("rgba16f")] RWTexture2D<float4> EnvironmentRadiance;   // the overlaid image present tone-maps

// 📝 The illumination world is right-handed z-up (x east, y north, z up); the environment's axes are the same world rotated
//    to y-up (x east, y up, z south). A proper rotation, so the star basis and every cross product keep their handedness.
float3 ProjectEnvironmentAxes(float3 Vector)
{
    return float3(Vector.x, Vector.z, -Vector.y);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ProjectEnvironmentVisibility(uint3 Thread : SV_DispatchThreadID)
{
    let Extent = uint2(Refresh.ScreenExtent);
    if (Thread.x >= Extent.x || Thread.y >= Extent.y)
    {
        return;
    }
    let Pixel     = int2(Thread.xy);
    let Centre    = float2(Thread.xy) + 0.5;
    let Depth     = LoadTexel(SurfaceDepth, Pixel, 0);
    let NearPoint = ReconstructWorldPosition(Centre, 1.0);
    let Direction = normalize(NearPoint - Refresh.CameraPosition);
    var Distance  = -1.0;
    var Reversed  = 0.0;
    if (Depth > 0.0)
    {
        // 📝 Same convention as VisibilityProjection.slang: reversed depth 0.1 / distance, 0 for the open sky.
        Distance = length(ReconstructWorldPosition(Centre, Depth) - Refresh.CameraPosition);
        Reversed = clamp(0.1 / max(Distance, 0.1), 1.0e-6, 1.0);
    }
    let Radiance = LoadTexel(ResolvedRadiance, Pixel, 0).rgb * Import.RadianceScale;
    VisibilityRadiance[Pixel]  = float4(Radiance, Reversed);
    VisibilityOrigin[Pixel]    = float4(ProjectEnvironmentAxes(Refresh.CameraPosition), 0.0);
    VisibilityDirection[Pixel] = float4(ProjectEnvironmentAxes(Direction), Distance);
    VisibilityGlass[Pixel]     = float4(1.0, 1.0, 1.0, 0.0);
}

// 📝 After the environment overlay: back to the refresh's radiometric scale, in place, before present.
[shader("compute")]
[numthreads(8, 8, 1)]
void RestoreEnvironmentRadiance(uint3 Thread : SV_DispatchThreadID)
{
    let Extent = uint2(Refresh.ScreenExtent);
    if (Thread.x >= Extent.x || Thread.y >= Extent.y)
    {
        return;
    }
    let Pixel  = int2(Thread.xy);
    let Linear = EnvironmentRadiance[Pixel];
    EnvironmentRadiance[Pixel] = float4(Linear.rgb / max(Import.RadianceScale, 1.0e-6), Linear.a);
}
