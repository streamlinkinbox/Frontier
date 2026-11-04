//============================================================================================================================================
//                                                         ANALYTICLAYOUTS.SLANG
//============================================================================================================================================
// 🧩 The analytic illumination's refresh profile and bindings: camera, sun, sky gradient and the scene records it reads (occupants,
//    materials) plus the sun's depth map — one group shared by every analytic program.

#pragma once
#include "IntrinsicParity.slang"
#include "HostConstants.slang"
#include "SceneLayouts.slang"

// 📝 Written by AnalyticIlluminationSequence.cpp (EncodeAnalyticConstants), 76 floats in float4 rows.
struct AnalyticConstants
{
    float4x4 ViewProjection;          // [-] camera, reversed-Z (1 near, 0 far)
    float4x4 InverseViewProjection;   // [-]
    float4x4 SolarViewProjection;     // [-] sun orthographic, standard depth (0 near, 1 far)
    float4   Camera;                  // xyz [m] position, w [-] exposure
    float4   Sun;                     // xyz [-] unit toward the sun, w [-] cosine of the disc radius
    float4   SunIrradiance;           // rgb [-] scene-referred irradiance, w [texel⁻¹] solar depth texel size
    float4   SkyZenith;               // rgb [-] radiance, w [-] solar depth bias
    float4   SkyHorizon;              // rgb [-] radiance, w [-] hemisphere fill scale
    float4   Ground;                  // rgb [-] albedo of the lower hemisphere, w [-] specular scale
    float4   Screen;                  // xy [px] extent, zw [px⁻¹] inverse extent
};

  [[vk::binding(0, 0)]] ConstantBuffer<AnalyticConstants>  Analytic;
  [[vk::binding(1, 0)]] StructuredBuffer<OccupantProfile>  Occupants;
  [[vk::binding(2, 0)]] StructuredBuffer<MaterialProfile>  Materials;
  [[vk::binding(3, 0)]] Texture2D<float>                   SolarDepth;
  [[vk::binding(4, 0)]] SamplerComparisonState             SolarComparison;

static const uint NoFamilyOverride = 0xFFFFFFFFu;   // [-] OccupantProfile.MaterialOverride keeping the per-facet family
static const uint FamilyBlockBit   = 0x80000000u;   // [-] beyond the demo: the override offsets the family into the object's material block
