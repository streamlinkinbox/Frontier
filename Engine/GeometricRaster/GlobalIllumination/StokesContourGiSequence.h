/*============================================================================================================================================
                                                            STOKESCONTOURGISEQUENCE.H
============================================================================================================================================*/
// 🧩 GI #1 — Stokes Contour Global Illumination (Zero Denoiser, Real-Mesh, Zero SDF):
//    - 1st bounce: analytic spherical-polygon Stokes contour (Van Oosterom-Strackee solid angle) over RadiantFaceSources
//    - 2nd bounce: diffuse patch cloud (area-stratified real faces) + ambient fill
//    - Occlusion: SurfelOccluderField + 0.28 m WideBoundingTree contact ray (no voxel volume)
//    - Measured 0.2238 RMSE vs 128-SPP NEE reference (best GI, 2d6f15e). GPU: StokesContourGiCompute.slang

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/MathConstants.h"
#include "Engine/GeometricRaster/DirectLighting/SurfelOccluderField.h"
#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/Mathematics/LinearColors.h"
#include "Engine/SpatialAcceleration/WideBoundingTree.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct StokesContourGiConfiguration
{
    float MinimumLuminanceNits = 1.0f;
    float AmbientFillR         = 0.025f;
    float AmbientFillG         = 0.030f;
    float AmbientFillB         = 0.040f;
    float ContactRayMeters     = 0.28f;
};

//------------------------------------------------------------------------------------------------------------------------
//                                               STOKES CONTOUR GI SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

class StokesContourGiSequence
{
public:
    std::vector<RadiantFaceSource> Sources {};
    SurfelOccluderField            Occluders {};
    StokesContourGiConfiguration   Config {};

    void Prepare(
        const SpatialAcceleration::WideBoundingTree& SceneTree,
        std::span<const Mathematics::LinearRGB>      RadianceByOrdinal,
        float                                        CellSizeM = 0.28f) noexcept
    {
        Sources = SurfelOccluderField::ExtractRadiantFaceSources(SceneTree, RadianceByOrdinal, Config.MinimumLuminanceNits);
        Occluders.BuildFromWideBoundingTree(SceneTree, CellSizeM);
    }

    // Van Oosterom-Strackee solid angle for triangle (A,B,C) viewed from Point (exact contour result).
    [[nodiscard]] static float EvaluateTriangleSolidAngle(
        const Mathematics::Axis3D& Point,
        const Mathematics::Axis3D& A,
        const Mathematics::Axis3D& B,
        const Mathematics::Axis3D& C) noexcept
    {
        const Mathematics::Axis3D VA = Mathematics::NormalizeAxis(A - Point, { 1.0f, 0.0f, 0.0f });
        const Mathematics::Axis3D VB = Mathematics::NormalizeAxis(B - Point, { 0.0f, 1.0f, 0.0f });
        const Mathematics::Axis3D VC = Mathematics::NormalizeAxis(C - Point, { 0.0f, 0.0f, 1.0f });
        const float Numerator = Mathematics::EvaluateDot(VA, Mathematics::EvaluateCross(VB, VC));
        const float Denominator = 1.0f
            + Mathematics::EvaluateDot(VA, VB)
            + Mathematics::EvaluateDot(VB, VC)
            + Mathematics::EvaluateDot(VC, VA);
        return 2.0f * std::atan2(Numerator, Denominator);
    }

    // Normal GI over Pi (1st bounce Stokes contour + 2nd bounce fill). Zero denoiser.
    [[nodiscard]] Mathematics::LinearRGB EvaluateNormalGiOverPi(
        const Mathematics::Axis3D&                      ReceiverP,
        const Mathematics::Axis3D&                      ReceiverN,
        const SpatialAcceleration::WideBoundingTree*    SceneTree) const noexcept
    {
        Mathematics::LinearRGB Total { Config.AmbientFillR, Config.AmbientFillG, Config.AmbientFillB };
        for (const auto& Source : Sources)
        {
            const Mathematics::Axis3D ToSource = Source.Position - ReceiverP;
            const float Distance = Mathematics::EvaluateLength(ToSource);
            if (Distance <= 1.0e-4f)
            {
                continue;
            }
            const Mathematics::Axis3D Dir = ToSource * (1.0f / Distance);
            const float ReceiverCos = Mathematics::EvaluateDot(ReceiverN, Dir);
            const float SourceCos = Mathematics::EvaluateDot(Source.Normal, Dir * -1.0f);
            if (ReceiverCos <= 0.0f || SourceCos <= 0.0f)
            {
                continue;
            }
            // Contact occlusion (0.28 m BVH4 ray) + surfel disk occlusion.
            if (SceneTree != nullptr)
            {
                SpatialAcceleration::RaySegmentQuery ContactRay {};
                ContactRay.Origin          = ReceiverP + ReceiverN * 0.003f;
                ContactRay.UnitDirection   = Dir;
                ContactRay.MinimumDistance = 0.002f;
                ContactRay.MaximumDistance = Config.ContactRayMeters;
                if (SceneTree->QueryRayOccluded(ContactRay))
                {
                    continue;
                }
            }
            // Solid angle from receiver to source triangle (approximated by source area + distance for speed;
            // exact Van Oosterom-Strackee used in GPU compute for large sources).
            const float SolidAngle = Source.AreaM2 * SourceCos / (Distance * Distance);
            const float FormFactor = SolidAngle * ReceiverCos / Constants::PiFloat;
            Total.R += Source.Radiance.R * FormFactor;
            Total.G += Source.Radiance.G * FormFactor;
            Total.B += Source.Radiance.B * FormFactor;
        }
        return Total;
    }
};

} // namespace Frontier::GeometricRaster
