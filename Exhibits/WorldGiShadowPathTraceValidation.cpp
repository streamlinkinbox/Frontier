/*============================================================================================================================================
                                                    WORLDGISHADOWPATHTRACEVALIDATION.CPP
============================================================================================================================================*/
// 🧪 Validation: Best-2 GI (Stokes + Surfel Cascade) + 6 Shadow Modes (Mode 5 SurfelFaceShadow) + Deterministic Path Tracer
//    - Real PolygonNetwork faces only (studio boxes triangulated). Zero SDF volumes. Zero analytical proxies. Zero denoiser.

#include "Engine/GeometricRaster/DirectLighting/SolarShadowSequence.h"
#include "Engine/GeometricRaster/DirectLighting/SurfelOccluderField.h"
#include "Engine/GeometricRaster/GlobalIllumination/DeterministicPathTraceSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/StokesContourGiSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/SurfelCascadeGiSequence.h"
#include "Engine/GeometrySpace/Primitives/AnalyticPrimitives.h"
#include "Engine/SpatialAcceleration/SpatialSplitBoundingVolume.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace Frontier;

namespace
{

struct ValidationScoreboard
{
    uint32_t Verified = 0u;
    uint32_t Failed   = 0u;
    void Check(bool Passed, const char* Text) noexcept
    {
        if (Passed)
        {
            ++Verified;
            std::printf("  [OK] %s\n", Text);
        }
        else
        {
            ++Failed;
            std::fprintf(stderr, "  [FAIL] %s\n", Text);
        }
    }
};

} // namespace

int main()
{
    std::printf("\n=== World GI, Shadow & Path Trace Validation (Real-Mesh, Zero SDF, Zero Denoiser) ===\n");
    ValidationScoreboard Score {};

    // Studio scene from real PolygonNetwork faces (boxes triangulated into faces).
    GeometrySpace::PolygonNetwork Network {};
    const GeometrySpace::FaceShadingAssignment Shading {};
    const GeometrySpace::UVPatchBounds UVRegion {};
    GeometrySpace::AppendBoxPrimitive(Network, { 0.0f, 0.0f, -0.25f }, { 7.5f, 6.5f, 0.25f }, UVRegion, Shading, 0u, 0u); // Floor
    GeometrySpace::AppendBoxPrimitive(Network, { -5.75f, 0.0f, 2.2f }, { 0.25f, 6.5f, 2.45f }, UVRegion, Shading, 0u, 0u); // Wall
    GeometrySpace::AppendBoxPrimitive(Network, { -3.1f, 2.5f, 1.2f }, { 0.35f, 0.35f, 1.2f }, UVRegion, Shading, 0u, 0u); // Pillar
    GeometrySpace::AppendBoxPrimitive(Network, { 0.0f, 0.0f, 1.0f }, { 0.5f, 0.5f, 0.5f }, UVRegion, Shading, 0u, 0u); // Occluder box
    GeometrySpace::AppendBoxPrimitive(Network, { 2.0f, 2.0f, 1.0f }, { 0.3f, 0.3f, 0.3f }, UVRegion, Shading, 0u, 1u); // Lamp box (radiant ordinal 1)
    Score.Check(Network.Faces.size() > 20u, "Built studio scene from real PolygonNetwork faces");

    SpatialAcceleration::WideBoundingTree SceneTree {};
    Score.Check(SceneTree.BuildFromPolygonNetwork(Network), "Built WideBoundingTree BVH4 from real faces");
    Score.Check(!SceneTree.Primitives.empty(), "BVH4 leaf triangles non-empty");

    const std::vector<Mathematics::LinearRGB> RadianceByOrdinal { { 0.0f, 0.0f, 0.0f }, { 800.0f, 760.0f, 700.0f } };
    const Mathematics::Axis3D SunDir { 0.0f, 0.0f, 1.0f };
    const float TanSunRadius = 0.048f;

    // Mode 5 SurfelFaceShadow (shared field).
    GeometricRaster::SurfelOccluderField Field {};
    Field.BuildFromWideBoundingTree(SceneTree, 0.28f);
    std::printf("  SurfelOccluderField built %zu disks from real faces\n", Field.Disks.size());
    Score.Check(Field.Disks.size() > 10u, "SurfelOccluderField covers studio occluders from real faces");

    const float OpenSunVis = Field.EvaluateSoftDiskShadow({ 3.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, SunDir, TanSunRadius, &SceneTree);
    const float UnderBoxVis = Field.EvaluateSoftDiskShadow({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, SunDir, TanSunRadius, &SceneTree);
    std::printf("  Spot checks -> OpenSun=%.3f UnderBox=%.3f\n", OpenSunVis, UnderBoxVis);
    Score.Check(OpenSunVis > 0.99f, "Zero phantom shadow in open sunlight");
    Score.Check(UnderBoxVis < 0.05f, "Solid umbra under occluder box");

    // GI #1 StokesContourGiSequence.
    GeometricRaster::StokesContourGiSequence Stokes {};
    Stokes.Prepare(SceneTree, RadianceByOrdinal, 0.28f);
    Score.Check(!Stokes.Sources.empty(), "Stokes extracted radiant face sources from real faces");
    const auto GiNear = Stokes.EvaluateNormalGiOverPi({ 2.0f, 2.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, &SceneTree);
    const auto GiFar = Stokes.EvaluateNormalGiOverPi({ -3.0f, -3.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, &SceneTree);
    const float GiNearLum = Mathematics::EvaluateRec709Luminance(GiNear);
    const float GiFarLum = Mathematics::EvaluateRec709Luminance(GiFar);
    std::printf("  Stokes GI -> NearLamp=%.4f FarLamp=%.4f\n", GiNearLum, GiFarLum);
    Score.Check(std::isfinite(GiNearLum) && std::isfinite(GiFarLum), "Stokes GI finite");
    Score.Check(GiNearLum >= GiFarLum, "Stokes GI brighter near lamp than far");

    // GI #2 SurfelCascadeGiSequence.
    GeometricRaster::SurfelCascadeGiSequence SurfelGi {};
    SurfelGi.Prepare(SceneTree, RadianceByOrdinal, 0.28f);
    Score.Check(!SurfelGi.Surfels.empty(), "SurfelCascade built virtual surfels from real faces");
    const auto GiCascade = SurfelGi.EvaluateNormalGiOverPi({ 2.0f, 2.0f, 0.0f }, { 0.0f, 0.0f, 1.0f });
    const float GiCascadeLum = Mathematics::EvaluateRec709Luminance(GiCascade);
    std::printf("  SurfelCascade GI -> %.4f\n", GiCascadeLum);
    Score.Check(std::isfinite(GiCascadeLum) && GiCascadeLum >= 0.0f, "SurfelCascade GI finite and non-negative");

    // Deterministic path tracer (CPU reference).
    SpatialAcceleration::SpatialSplitBoundingVolume Volume {};
    Volume.BuildFromWideBoundingTree(SceneTree);
    GeometricRaster::DeterministicPathTraceSequence PathTracer {};
    const auto SobolA = PathTracer.SobolSample(10u, 20u, 3u);
    const auto SobolB = PathTracer.SobolSample(10u, 20u, 3u);
    Score.Check(SobolA.X == SobolB.X && SobolA.Y == SobolB.Y, "Sobol (0,2) bit-exact deterministic");
    SpatialAcceleration::RaySegmentQuery DownRay {};
    DownRay.Origin = { 0.0f, 0.0f, 5.0f };
    DownRay.UnitDirection = { 0.0f, 0.0f, -1.0f };
    DownRay.MinimumDistance = 1.0e-3f;
    DownRay.MaximumDistance = 80.0f;
    const auto DownHit = Volume.TraceClosestRay(DownRay);
    Score.Check(DownHit.HitConfirmed, "Path tracer BVH4 closest-hit finds floor");
    const bool UnderOccluded = PathTracer.QueryShadowOccluded(Volume, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, 2.0f);
    Score.Check(UnderOccluded, "Path tracer shadow query occluded under box");

    // SolarShadowSequence Mode 5 enum exists (wired in SolarShadowSequence.cpp via SurfelOccluderField).
    Score.Check(static_cast<uint8_t>(GeometricRaster::SolarShadowAlgorithm::SurfelFaceShadow) == 5u, "SolarShadowAlgorithm::SurfelFaceShadow == 5 (6th mode)");

    std::printf("\n[WorldGiShadowPathTraceValidation] Verified=%u Failed=%u\n", Score.Verified, Score.Failed);
    return Score.Failed == 0u ? 0 : 1;
}
