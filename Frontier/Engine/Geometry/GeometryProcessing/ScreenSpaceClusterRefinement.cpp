/*==========================================================================================================================================
                                              SCREEN-SPACE CLUSTER REFINEMENT DEFINITION
==========================================================================================================================================*/
// 🧩 Locked-border Garland-Heckbert QEM Cluster-DAG, curved-glass IOR error boost, Karis group-atomic antichain cut,
//    two-phase Hi-Z occlusion, 64 KB pinned-root LRU pages, 3-tier Toksvig/proxy material virtualization, & RAM/VRAM stats.

#include "Engine/Geometry/GeometryProcessing/ScreenSpaceClusterRefinement.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Frontier
{
namespace
{

inline ClusterFloat3 Sub3(ClusterFloat3 A, ClusterFloat3 B) noexcept { return {A.X - B.X, A.Y - B.Y, A.Z - B.Z}; }
inline ClusterFloat3 Mul3(ClusterFloat3 A, float Scalar) noexcept { return {A.X * Scalar, A.Y * Scalar, A.Z * Scalar}; }
inline float         Dot3(ClusterFloat3 A, ClusterFloat3 B) noexcept { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
inline ClusterFloat3 Cross3(ClusterFloat3 A, ClusterFloat3 B) noexcept
{
    return {A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X};
}
inline float Length3(ClusterFloat3 V) noexcept { return std::sqrt(Dot3(V, V)); }
inline ClusterFloat3 Normalize3(ClusterFloat3 V) noexcept
{
    const float L = Length3(V);
    return (L > 1.0e-12f) ? Mul3(V, 1.0f / L) : ClusterFloat3{0.0f, 1.0f, 0.0f};
}

inline ClusterFloat3 ToFloat3(const FacetPoint3D& Pt) noexcept
{
    return {static_cast<float>(Pt.X), static_cast<float>(Pt.Y), static_cast<float>(Pt.Z)};
}

double ClusterSurfaceDistance(const FacetPoint3D& SphereCentre,
                              double              SphereRadius,
                              ClusterFloat3       CameraOrigin) noexcept
{
    const double Dx = SphereCentre.X - static_cast<double>(CameraOrigin.X);
    const double Dy = SphereCentre.Y - static_cast<double>(CameraOrigin.Y);
    const double Dz = SphereCentre.Z - static_cast<double>(CameraOrigin.Z);
    return std::max(0.05, std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz) - SphereRadius);
}

double ProjectedErrorPixels(double              ErrorMeters,
                            float               OpticalBoost,
                            const FacetPoint3D& SphereCentre,
                            double              SphereRadius,
                            ClusterFloat3       CameraOrigin,
                            double              ProjectionScalePx) noexcept
{
    if (ErrorMeters <= 0.0)
    {
        return 0.0;
    }
    const double Dist = ClusterSurfaceDistance(SphereCentre, SphereRadius, CameraOrigin);
    return (ErrorMeters * static_cast<double>(OpticalBoost) * ProjectionScalePx) / Dist;
}

struct CameraBasis
{
    ClusterFloat3 Forward{};
    ClusterFloat3 Right{};
    ClusterFloat3 Up{};
    float         TanHalfV = 0.45f;
    float         TanHalfH = 0.80f;
};

CameraBasis BuildCameraBasis(const ClusterCameraView& Cam) noexcept
{
    CameraBasis B{};
    B.Forward                 = Normalize3(Sub3(Cam.CameraTarget, Cam.CameraOrigin));
    const ClusterFloat3 RefUp = (std::abs(Dot3(B.Forward, Cam.CameraUp)) > 0.98f)
                                    ? ClusterFloat3{0.0f, 0.0f, 1.0f}
                                    : Cam.CameraUp;
    B.Right                   = Normalize3(Cross3(B.Forward, RefUp));
    B.Up                      = Normalize3(Cross3(B.Right, B.Forward));
    B.TanHalfV                = std::tan(0.5f * Cam.VerticalFovRad);
    const float Aspect        = static_cast<float>(std::max(1u, Cam.ViewportWidthPx)) /
                                static_cast<float>(std::max(1u, Cam.ViewportHeightPx));
    B.TanHalfH                = B.TanHalfV * Aspect;
    return B;
}

bool ProjectPointToScreen(ClusterFloat3            WorldPt,
                          const ClusterCameraView& Cam,
                          const CameraBasis&       Basis,
                          float&                   OutScreenX,
                          float&                   OutScreenY,
                          float&                   OutViewZ) noexcept
{
    const ClusterFloat3 Rel = Sub3(WorldPt, Cam.CameraOrigin);
    OutViewZ                = Dot3(Rel, Basis.Forward);
    if (OutViewZ <= 0.04f)
    {
        return false;
    }
    const float NdcX = Dot3(Rel, Basis.Right) / (OutViewZ * Basis.TanHalfH);
    const float NdcY = Dot3(Rel, Basis.Up) / (OutViewZ * Basis.TanHalfV);
    OutScreenX       = (NdcX * 0.5f + 0.5f) * static_cast<float>(Cam.ViewportWidthPx);
    OutScreenY       = (0.5f - NdcY * 0.5f) * static_cast<float>(Cam.ViewportHeightPx);
    return true;
}

} // namespace

float EvaluateDistanceQualityFactor(float DistanceMeters, const ClusterDetailParameters& Parameters) noexcept
{
    const float NearM     = std::max(0.0f, Parameters.NearFullDetailMeters);
    const float FarM      = std::max(NearM + 0.5f, Parameters.FarHorizonMeters);
    const float Retention = std::clamp(Parameters.MinimumRetentionFraction, 0.02f, 1.0f);
    const float Power     = std::clamp(Parameters.FalloffCurvePower, 0.25f, 4.0f);
    const float NormPos   = std::clamp((DistanceMeters - NearM) / (FarM - NearM), 0.0f, 1.0f);
    return std::clamp(1.0f - (1.0f - Retention) * std::pow(NormPos, Power), Retention, 1.0f);
}

void ScreenSpaceClusterRefinement::RegisterMaterialProfile(const ClusterMaterialProfile& Profile)
{
    for (ClusterMaterialProfile& Existing : Materials_)
    {
        if (Existing.ClassificationId == Profile.ClassificationId)
        {
            Existing = Profile;
            return;
        }
    }
    Materials_.push_back(Profile);
}

void ScreenSpaceClusterRefinement::RegisterControlVehicleMaterials()
{
    // Ground & architectural proving-ground materials (0..5)
    RegisterMaterialProfile({0u, false, 1.50f, {}, {0.23f, 0.25f, 0.28f}, 0.68f, 0.0f, 0.0f, 0.2f, {}, 0.22f});
    RegisterMaterialProfile({1u, false, 1.50f, {}, {0.58f, 0.60f, 0.64f}, 0.52f, 0.05f, 0.0f, 0.2f, {}, 0.18f});
    RegisterMaterialProfile({2u, false, 1.50f, {}, {0.76f, 0.54f, 0.26f}, 0.28f, 0.82f, 0.35f, 0.08f, {}, 0.14f});
    RegisterMaterialProfile({3u, true,  1.54f, {0.10f, 0.04f, 0.03f}, {0.82f, 0.92f, 0.96f}, 0.04f, 0.0f, 1.0f, 0.02f, {}, 0.0f});

    // ControlVehicle automotive material suite (6..16)
    // 6: Deep Sapphire-Slate Metallic Clearcoat Bodywork
    RegisterMaterialProfile({6u, false, 1.52f, {}, {0.14f, 0.30f, 0.56f}, 0.18f, 0.78f, 1.0f, 0.04f, {}, 0.12f});
    // 7: Cabin Laminated Acoustic Glazing (Transmissive Glass, IOR = 1.52, automotive green-cyan iron tint)
    RegisterMaterialProfile({7u, true,  1.52f, {0.22f, 0.06f, 0.08f}, {0.76f, 0.90f, 0.92f}, 0.03f, 0.0f, 1.0f, 0.02f, {}, 0.0f});
    // 8: Lower Satin Carbon-Weave Aero Splitter & Sills
    RegisterMaterialProfile({8u, false, 1.50f, {}, {0.11f, 0.12f, 0.14f}, 0.36f, 0.22f, 0.35f, 0.12f, {}, 0.25f});
    // 9: Matte Anodized Chassis Trim & Brake Caliper Hub
    RegisterMaterialProfile({9u, false, 1.50f, {}, {0.16f, 0.17f, 0.19f}, 0.58f, 0.35f, 0.0f, 0.2f, {}, 0.10f});
    // 10: Diamond-Cut Brushed Machined Forged Alloy Rim
    RegisterMaterialProfile({10u, false, 1.50f, {}, {0.80f, 0.83f, 0.87f}, 0.16f, 0.92f, 0.45f, 0.06f, {}, 0.15f});
    // 11: Vulcanized Silica-Compound Tread Tyre Rubber
    RegisterMaterialProfile({11u, false, 1.50f, {}, {0.065f, 0.068f, 0.074f}, 0.82f, 0.0f, 0.0f, 0.3f, {}, 0.30f});
    // 12: Polycarbonate Headlamp Outer Cover (Transmissive Glass, IOR = 1.58)
    RegisterMaterialProfile({12u, true,  1.58f, {0.04f, 0.04f, 0.03f}, {0.92f, 0.95f, 0.99f}, 0.02f, 0.0f, 1.0f, 0.02f, {}, 0.0f});
    // 13: Cool-White LED Matrix Headlamp Projector
    RegisterMaterialProfile({13u, false, 1.50f, {}, {0.95f, 0.97f, 1.00f}, 0.08f, 0.5f, 0.0f, 0.1f, {2.8f, 3.1f, 3.6f}, 0.0f});
    // 14: Sculpted Crimson OLED Taillamp Lightbar
    RegisterMaterialProfile({14u, false, 1.50f, {}, {0.86f, 0.08f, 0.10f}, 0.12f, 0.2f, 0.8f, 0.05f, {2.2f, 0.14f, 0.12f}, 0.0f});
    // 15: Dark Honeycomb Intake Grille
    RegisterMaterialProfile({15u, false, 1.50f, {}, {0.08f, 0.085f, 0.095f}, 0.64f, 0.18f, 0.0f, 0.2f, {}, 0.32f});
    // 16: Warm Alcantara / Saddle-Tan Cabin Seats & Console (visible behind Class 7 windshield!)
    RegisterMaterialProfile({16u, false, 1.50f, {}, {0.68f, 0.48f, 0.30f}, 0.62f, 0.0f, 0.0f, 0.25f, {}, 0.20f});
}

const ClusterMaterialProfile& ScreenSpaceClusterRefinement::FindMaterial(uint32_t ClassificationId) const noexcept
{
    for (const ClusterMaterialProfile& Mat : Materials_)
    {
        if (Mat.ClassificationId == ClassificationId)
        {
            return Mat;
        }
    }
    return DefaultMaterial_;
}

bool ScreenSpaceClusterRefinement::BuildFromFacetField(const FacetField&             SourceFacets,
                                           const TierBuildSpecification& Specification)
{
    Records_.clear();
    TierClusterToGlobal_.clear();
    Pages_.clear();
    Sequence_             = ClusterTierSequence{};
    RawUnindexedRamBytes_ = SourceFacets.Corners.size() * sizeof(FacetPoint3D) +
                            SourceFacets.CornerUVs.size() * sizeof(std::array<double, 2>) +
                            SourceFacets.Classification.size() * sizeof(uint32_t) +
                            SourceFacets.SharpMask.size() * sizeof(uint8_t);

    if (SourceFacets.Classification.empty() || SourceFacets.Corners.size() != SourceFacets.Classification.size() * 3u)
    {
        return false;
    }

    WeldSpecification WeldSpec{};
    WeldSpec.UseCreaseAngle = Specification.UseCreaseAngle;
    WeldSpec.CreaseCosine   = Specification.CreaseCosine;
    const WeldOutcome Welded = WeldFacetField(SourceFacets, WeldSpec);
    if (Welded.Complex.Triangles.empty())
    {
        return false;
    }

    Sequence_ = BuildClusterTierSequence(Welded.Complex, Specification);
    if (Sequence_.Tiers.empty())
    {
        return false;
    }

    TierClusterToGlobal_.resize(Sequence_.Tiers.size());
    for (size_t TierIdx = 0u; TierIdx < Sequence_.Tiers.size(); ++TierIdx)
    {
        const ClusterTier& Tier = Sequence_.Tiers[TierIdx];
        TierClusterToGlobal_[TierIdx].resize(Tier.Clusters.size(), 0u);

        for (size_t ClIdx = 0u; ClIdx < Tier.Clusters.size(); ++ClIdx)
        {
            const FacetCluster&           Fc  = Tier.Clusters[ClIdx];
            const ClusterMaterialProfile& Mat = FindMaterial(Fc.Classification);

            ClusterRefinementRecord Rec{};
            Rec.TierIndex          = static_cast<uint32_t>(TierIdx);
            Rec.ClusterIndexInTier = static_cast<uint32_t>(ClIdx);
            Rec.GlobalClusterId    = static_cast<uint32_t>(Records_.size());
            Rec.ClassificationId   = Fc.Classification;
            Rec.TransmissiveGlass  = Mat.TransmissiveGlass;
            Rec.RefractiveIndexIor = Mat.TransmissiveGlass ? Mat.RefractiveIndexIor : 1.0f;

            if (Mat.TransmissiveGlass)
            {
                // Dual-interface (air->glass + glass->air) Snell angular sensitivity + curved-shell focal magnification:
                // For wide-angle wraparound glazing (ConeCutoff <= 0 -> half-angle >= 90 deg), sin(half_cone) saturates at 1.0
                const float ClampedCos  = std::clamp(static_cast<float>(Fc.ConeCutoff), 0.0f, 1.0f);
                const float SinHalfCone = std::sqrt(std::max(0.0f, 1.0f - ClampedCos * ClampedCos));
                Rec.OpticalDeviationBoost =
                    1.0f + std::max(0.0f, Mat.RefractiveIndexIor - 1.0f) * (1.25f + 4.0f * SinHalfCone);
            }
            else
            {
                Rec.OpticalDeviationBoost = 1.0f;
            }

            Rec.SelfDeviationMeters       = (ClIdx < Tier.ClusterDeviation.size()) ? Tier.ClusterDeviation[ClIdx] : 0.0;
            Rec.CoarserDeviationMeters    = 1.0e12;
            Rec.SphereCentre              = Fc.SphereCentre;
            Rec.SphereRadiusMeters        = Fc.SphereRadius;
            Rec.CoarserSphereCentre       = Fc.SphereCentre;
            Rec.CoarserSphereRadiusMeters = Fc.SphereRadius;
            Rec.ConeAxis                  = Fc.ConeAxis;
            Rec.ConeCutoff                = Fc.ConeCutoff;
            Rec.TriangleCount             = static_cast<uint32_t>(Fc.Triangles.size());

            ClusterFloat3 BMin{1.0e9f, 1.0e9f, 1.0e9f};
            ClusterFloat3 BMax{-1.0e9f, -1.0e9f, -1.0e9f};
            for (uint32_t Tri : Fc.Triangles)
            {
                for (uint32_t Corner = 0u; Corner < 3u; ++Corner)
                {
                    const ClusterFloat3 P = ToFloat3(Tier.Complex.Positions[Tier.Complex.Triangles[Tri * 3u + Corner]]);
                    BMin.X                = std::min(BMin.X, P.X);
                    BMin.Y                = std::min(BMin.Y, P.Y);
                    BMin.Z                = std::min(BMin.Z, P.Z);
                    BMax.X                = std::max(BMax.X, P.X);
                    BMax.Y                = std::max(BMax.Y, P.Y);
                    BMax.Z                = std::max(BMax.Z, P.Z);
                }
            }
            Rec.BoundsMin = BMin;
            Rec.BoundsMax = BMax;

            TierClusterToGlobal_[TierIdx][ClIdx] = Rec.GlobalClusterId;
            Records_.push_back(std::move(Rec));
        }
    }

    for (size_t TierIdx = 1u; TierIdx < Sequence_.Tiers.size(); ++TierIdx)
    {
        const ClusterTier& CoarserTier = Sequence_.Tiers[TierIdx];
        for (size_t ClIdx = 0u; ClIdx < CoarserTier.Clusters.size(); ++ClIdx)
        {
            const uint32_t CoarserGlobal = TierClusterToGlobal_[TierIdx][ClIdx];
            auto&          CoarserRec    = Records_[CoarserGlobal];
            if (ClIdx >= CoarserTier.EnclosedClusters.size())
            {
                continue;
            }
            for (uint32_t FinerInPrev : CoarserTier.EnclosedClusters[ClIdx])
            {
                if (FinerInPrev < TierClusterToGlobal_[TierIdx - 1u].size())
                {
                    const uint32_t FinerGlobal = TierClusterToGlobal_[TierIdx - 1u][FinerInPrev];
                    CoarserRec.EnclosedFinerGlobals.push_back(FinerGlobal);

                    auto& FinerRec = Records_[FinerGlobal];
                    if (FinerRec.CoarserDeviationMeters > 1.0e11 ||
                        CoarserRec.SelfDeviationMeters > FinerRec.CoarserDeviationMeters)
                    {
                        FinerRec.CoarserDeviationMeters =
                            std::max(CoarserRec.SelfDeviationMeters, FinerRec.SelfDeviationMeters + 1.0e-6);
                        FinerRec.CoarserSphereCentre       = CoarserRec.SphereCentre;
                        FinerRec.CoarserSphereRadiusMeters = CoarserRec.SphereRadiusMeters;
                    }
                }
            }
        }
    }

    // Pack clusters root-first into fixed 64 KB streaming pages (coarsest root tier pinned permanently in VRAM)
    const uint32_t       CoarsestTier = static_cast<uint32_t>(Sequence_.Tiers.size() - 1u);
    ClusterStreamingPage CurrentPage{};
    CurrentPage.PageIndex        = 0u;
    CurrentPage.PinnedRoot       = true;
    CurrentPage.ResidentInMemory = true;

    for (int TierInt = static_cast<int>(CoarsestTier); TierInt >= 0; --TierInt)
    {
        const uint32_t TierIdx    = static_cast<uint32_t>(TierInt);
        const bool     PinnedTier = (TierIdx == CoarsestTier);

        for (uint32_t GlobalId : TierClusterToGlobal_[TierIdx])
        {
            const uint32_t ClusterBytes = 96u + Records_[GlobalId].TriangleCount * 36u;
            if (!CurrentPage.MemberGlobalIds.empty() &&
                (CurrentPage.ByteSize + ClusterBytes > StreamingPageCapacityBytes || (CurrentPage.PinnedRoot && !PinnedTier)))
            {
                Pages_.push_back(std::move(CurrentPage));
                CurrentPage                  = ClusterStreamingPage{};
                CurrentPage.PageIndex        = static_cast<uint32_t>(Pages_.size());
                CurrentPage.PinnedRoot       = PinnedTier;
                CurrentPage.ResidentInMemory = true;
            }
            CurrentPage.PinnedRoot           = CurrentPage.PinnedRoot || PinnedTier;
            CurrentPage.ByteSize            += ClusterBytes;
            Records_[GlobalId].PageSlotIndex = CurrentPage.PageIndex;
            CurrentPage.MemberGlobalIds.push_back(GlobalId);
        }
    }
    if (!CurrentPage.MemberGlobalIds.empty())
    {
        Pages_.push_back(std::move(CurrentPage));
    }

    return true;
}

void ScreenSpaceClusterRefinement::EnforcePageResidencyBudget(uint32_t                 MaxResidentBytes,
                                                  const ClusterCameraView& Camera,
                                                  uint64_t                 FrameTick)
{
    if (Pages_.empty())
    {
        return;
    }

    const double ProjScale = (0.5 * static_cast<double>(Camera.ViewportHeightPx)) /
                             std::tan(0.5 * static_cast<double>(Camera.VerticalFovRad));

    struct PageScore
    {
        uint32_t PageIdx  = 0u;
        double   Priority = 0.0;
    };
    std::vector<PageScore> Scores;
    Scores.reserve(Pages_.size());

    for (const ClusterStreamingPage& Page : Pages_)
    {
        double MaxPx = 0.0;
        for (uint32_t Gid : Page.MemberGlobalIds)
        {
            const auto&  Rec  = Records_[Gid];
            const double ErrM = std::max(1.0e-3, Rec.CoarserDeviationMeters < 1.0e11 ? Rec.CoarserDeviationMeters : Rec.SphereRadiusMeters);
            const double Px   = ProjectedErrorPixels(
                ErrM, Rec.OpticalDeviationBoost, Rec.SphereCentre, Rec.SphereRadiusMeters, Camera.CameraOrigin, ProjScale);
            MaxPx = std::max(MaxPx, Px);
        }
        if (Page.PinnedRoot)
        {
            MaxPx = 1.0e15;
        }
        Scores.push_back({Page.PageIndex, MaxPx});
    }

    std::sort(Scores.begin(), Scores.end(), [](const PageScore& A, const PageScore& B) { return A.Priority > B.Priority; });

    uint32_t UsedBytes = 0u;
    for (const PageScore& Score : Scores)
    {
        ClusterStreamingPage& Page = Pages_[Score.PageIdx];
        if (Page.PinnedRoot || UsedBytes + Page.ByteSize <= MaxResidentBytes)
        {
            Page.ResidentInMemory = true;
            Page.LastTouchedTick  = FrameTick;
            UsedBytes += Page.ByteSize;
        }
        else
        {
            Page.ResidentInMemory = false;
        }
    }
}

EvaluatedClusterMaterial ScreenSpaceClusterRefinement::EvaluateClusterMaterial(
    uint32_t                       GlobalClusterId,
    float                          DistanceMeters,
    float                          ProjectedErrorPx,
    const ClusterDetailParameters& Detail) const noexcept
{
    EvaluatedClusterMaterial Out{};
    if (GlobalClusterId >= Records_.size())
    {
        return Out;
    }

    const ClusterRefinementRecord& Rec = Records_[GlobalClusterId];
    const ClusterMaterialProfile& Mat = FindMaterial(Rec.ClassificationId);
    const float Quality = EvaluateDistanceQualityFactor(DistanceMeters, Detail) * std::clamp(Detail.MaterialQualityScale, 0.05f, 1.0f);

    // Toksvig normal-variance roughness lengthening derived from the cluster's normal-cone spread:
    // As geometry simplifies to coarser tiers, ConeCutoff decreases -> normal variance σ² = (1 - ConeCutoff) increases ->
    // specular roughness lengthens automatically so distant clusters never sparkle or alias!
    const float ConeCos     = std::clamp(static_cast<float>(Rec.ConeCutoff), -1.0f, 1.0f);
    const float NormalVar   = (Rec.ConeCutoff > -0.95) ? std::max(0.0f, 0.35f * (1.0f - ConeCos)) : 0.08f;
    const float BaseAlpha   = Mat.Roughness * Mat.Roughness;
    const float ToksvigRgh  = std::clamp(std::sqrt(BaseAlpha + NormalVar * (Rec.TierIndex > 0u ? 1.0f : 0.15f)), 0.03f, 1.0f);

    if (Mat.TransmissiveGlass || (Quality >= 0.72f && Rec.TierIndex == 0u && ProjectedErrorPx <= Detail.BaseErrorThresholdPixels * 1.1f))
    {
        Out.ActiveTier          = ClusterMaterialTier::FullStrata;
        Out.EffectiveAlbedoRgb  = Mat.AlbedoRgb;
        Out.EffectiveRoughness  = Mat.Roughness;
        Out.EffectiveMetallic   = Mat.Metallic;
        Out.EffectiveClearcoat  = Mat.ClearcoatWeight;
        Out.TextureMipBias      = 0.0f;
        Out.EstimatedFetchBytes = 64u;
    }
    else if (Quality >= 0.36f && Rec.TierIndex <= 2u)
    {
        Out.ActiveTier          = ClusterMaterialTier::FilteredToksvig;
        Out.EffectiveAlbedoRgb  = Mat.AlbedoRgb;
        Out.EffectiveRoughness  = ToksvigRgh;
        Out.EffectiveMetallic   = Mat.Metallic;
        Out.EffectiveClearcoat  = Mat.ClearcoatWeight * 0.65f;
        Out.TextureMipBias      = 1.5f + 0.85f * static_cast<float>(Rec.TierIndex);
        Out.EstimatedFetchBytes = 24u;
    }
    else
    {
        // Pre-integrated zero-texture horizon proxy: folds clearcoat Fresnel energy into diffuse/lobe proxy
        const float CoatLift    = 0.04f * Mat.ClearcoatWeight;
        Out.ActiveTier          = ClusterMaterialTier::CompactProxy;
        Out.EffectiveAlbedoRgb  = {std::min(1.0f, Mat.AlbedoRgb.X + CoatLift),
                                   std::min(1.0f, Mat.AlbedoRgb.Y + CoatLift),
                                   std::min(1.0f, Mat.AlbedoRgb.Z + CoatLift)};
        Out.EffectiveRoughness  = std::max(ToksvigRgh, 0.32f);
        Out.EffectiveMetallic   = Mat.Metallic * 0.85f;
        Out.EffectiveClearcoat  = 0.0f;
        Out.TextureMipBias      = 4.0f;
        Out.EstimatedFetchBytes = 16u;
    }
    return Out;
}

ClusterRuntimeFrameReport ScreenSpaceClusterRefinement::ExecuteCutAndTwoPhaseHiZ(const ClusterCameraView& Camera) const
{
    ClusterRuntimeFrameReport Report{};
    Report.SourceLeafTriangles = Sequence_.SourceTriangleCount;
    if (Sequence_.Tiers.empty() || Records_.empty())
    {
        return Report;
    }

    const double ProjScale  = (0.5 * static_cast<double>(Camera.ViewportHeightPx)) /
                              std::tan(0.5 * static_cast<double>(Camera.VerticalFovRad));
    const double BaseThresh = std::max(0.05, static_cast<double>(Camera.Detail.BaseErrorThresholdPixels));

    std::vector<uint32_t> FrontierStack;
    for (const ClusterRefinementRecord& Rec : Records_)
    {
        if (Rec.CoarserDeviationMeters > 1.0e11)
        {
            FrontierStack.push_back(Rec.GlobalClusterId);
        }
    }
    if (FrontierStack.empty())
    {
        FrontierStack = TierClusterToGlobal_[Sequence_.Tiers.size() - 1u];
    }

    std::vector<std::vector<uint32_t>> FinerToCoarser(Records_.size());
    for (const ClusterRefinementRecord& Rec : Records_)
    {
        for (uint32_t FinerGid : Rec.EnclosedFinerGlobals)
        {
            if (FinerGid < FinerToCoarser.size())
            {
                FinerToCoarser[FinerGid].push_back(Rec.GlobalClusterId);
            }
        }
    }

    std::unordered_set<uint32_t> Visited;
    std::vector<uint32_t>        CutGlobals;

    while (!FrontierStack.empty())
    {
        const uint32_t SeedGid = FrontierStack.back();
        FrontierStack.pop_back();
        if (Visited.count(SeedGid) != 0u)
        {
            continue;
        }

        const ClusterRefinementRecord& SeedRec = Records_[SeedGid];
        if (SeedRec.EnclosedFinerGlobals.empty())
        {
            Visited.insert(SeedGid);
            CutGlobals.push_back(SeedGid);
            continue;
        }

        std::vector<uint32_t>        SameTierGroup = {SeedGid};
        std::unordered_set<uint32_t> GroupFinerSet;
        Visited.insert(SeedGid);

        for (size_t Idx = 0u; Idx < SameTierGroup.size(); ++Idx)
        {
            const uint32_t PeerGid = SameTierGroup[Idx];
            for (uint32_t FinerGid : Records_[PeerGid].EnclosedFinerGlobals)
            {
                if (GroupFinerSet.insert(FinerGid).second && FinerGid < FinerToCoarser.size())
                {
                    for (uint32_t OtherCoarser : FinerToCoarser[FinerGid])
                    {
                        if (Records_[OtherCoarser].TierIndex == SeedRec.TierIndex && Visited.insert(OtherCoarser).second)
                        {
                            SameTierGroup.push_back(OtherCoarser);
                        }
                    }
                }
            }
        }

        double GroupMaxPx       = 0.0;
        double GroupMinDistM    = 1.0e12;
        bool   AllFinerResident = true;
        for (uint32_t PeerGid : SameTierGroup)
        {
            const ClusterRefinementRecord& Rec = Records_[PeerGid];
            GroupMinDistM = std::min(GroupMinDistM, ClusterSurfaceDistance(Rec.SphereCentre, Rec.SphereRadiusMeters, Camera.CameraOrigin));
            GroupMaxPx    = std::max(
                GroupMaxPx,
                ProjectedErrorPixels(Rec.SelfDeviationMeters,
                                     Rec.OpticalDeviationBoost,
                                     Rec.SphereCentre,
                                     Rec.SphereRadiusMeters,
                                     Camera.CameraOrigin,
                                     ProjScale));
        }

        // Modulate screen-space error budget smoothly by the user's distance-falloff quality curve
        const float  QualityFactor    = EvaluateDistanceQualityFactor(static_cast<float>(GroupMinDistM), Camera.Detail);
        const double EffectiveThresh  = BaseThresh / std::max(0.05, static_cast<double>(QualityFactor * QualityFactor));

        for (uint32_t FinerGid : GroupFinerSet)
        {
            const uint32_t PageIdx = Records_[FinerGid].PageSlotIndex;
            if (PageIdx < Pages_.size() && !Pages_[PageIdx].ResidentInMemory)
            {
                AllFinerResident = false;
                break;
            }
        }

        if (GroupMaxPx <= EffectiveThresh)
        {
            for (uint32_t PeerGid : SameTierGroup)
            {
                CutGlobals.push_back(PeerGid);
            }
            continue;
        }

        if (!AllFinerResident)
        {
            Report.FallbackToResidentCoarser += static_cast<uint32_t>(SameTierGroup.size());
            for (uint32_t PeerGid : SameTierGroup)
            {
                CutGlobals.push_back(PeerGid);
            }
            continue;
        }

        for (uint32_t FinerGid : GroupFinerSet)
        {
            FrontierStack.push_back(FinerGid);
        }
    }

    Report.TotalCutClusters = static_cast<uint32_t>(CutGlobals.size());

    const CameraBasis Basis = BuildCameraBasis(Camera);
    struct ScreenBoundCluster
    {
        uint32_t GlobalId = 0u;
        int      MinX     = 0;
        int      MaxX     = 0;
        int      MinY     = 0;
        int      MaxY     = 0;
        float    NearZ    = 0.0f;
        float    FarZ     = 0.0f;
    };

    std::vector<ScreenBoundCluster> OpaqueCandidates;
    std::vector<ScreenBoundCluster> GlassCandidates;

    for (uint32_t Gid : CutGlobals)
    {
        const ClusterRefinementRecord& Rec    = Records_[Gid];
        const ClusterFloat3           Centre = ToFloat3(Rec.SphereCentre);
        const float                   Radius = static_cast<float>(Rec.SphereRadiusMeters);
        const ClusterFloat3           ToCam  = Sub3(Centre, Camera.CameraOrigin);
        const float                   ViewZ  = Dot3(ToCam, Basis.Forward);

        if (ViewZ + Radius <= 0.05f)
        {
            ++Report.FrustumCulledCount;
            continue;
        }
        const float HalfSpanX = ViewZ * Basis.TanHalfH + Radius * std::sqrt(1.0f + Basis.TanHalfH * Basis.TanHalfH);
        const float HalfSpanY = ViewZ * Basis.TanHalfV + Radius * std::sqrt(1.0f + Basis.TanHalfV * Basis.TanHalfV);
        if (std::abs(Dot3(ToCam, Basis.Right)) > HalfSpanX || std::abs(Dot3(ToCam, Basis.Up)) > HalfSpanY)
        {
            ++Report.FrustumCulledCount;
            continue;
        }

        const float DistToCentre = Length3(ToCam);
        if (Rec.ConeCutoff > 0.05 && DistToCentre > Radius + 1.0e-3f)
        {
            const ClusterFloat3 ViewDir      = Mul3(ToCam, 1.0f / DistToCentre);
            const ClusterFloat3 Axis         = ToFloat3(Rec.ConeAxis);
            const float         SinConeHalf  = std::sqrt(std::max(0.0f, 1.0f - static_cast<float>(Rec.ConeCutoff * Rec.ConeCutoff)));
            const float         SinSphereSub = std::min(1.0f, Radius / DistToCentre);
            const float         RejectThresh = std::min(0.99f, SinConeHalf + SinSphereSub);
            if (Dot3(ViewDir, Axis) > RejectThresh)
            {
                if (Rec.TransmissiveGlass)
                {
                    ++Report.GlassExemptFromBackface;
                }
                else
                {
                    ++Report.BackfaceConeCulledCount;
                    continue;
                }
            }
        }

        int   SMinX    = static_cast<int>(Camera.ViewportWidthPx) - 1;
        int   SMaxX    = 0;
        int   SMinY    = static_cast<int>(Camera.ViewportHeightPx) - 1;
        int   SMaxY    = 0;
        float NearZ    = std::max(0.05f, ViewZ - Radius);
        float FarZ     = std::max(NearZ, ViewZ + Radius);
        bool  AnyFront = false;

        for (int Corner = 0; Corner < 8; ++Corner)
        {
            const ClusterFloat3 P{(Corner & 1) ? Rec.BoundsMax.X : Rec.BoundsMin.X,
                                  (Corner & 2) ? Rec.BoundsMax.Y : Rec.BoundsMin.Y,
                                  (Corner & 4) ? Rec.BoundsMax.Z : Rec.BoundsMin.Z};
            float Sx = 0.0f, Sy = 0.0f, Sz = 0.0f;
            if (ProjectPointToScreen(P, Camera, Basis, Sx, Sy, Sz))
            {
                AnyFront = true;
                SMinX    = std::min(SMinX, static_cast<int>(std::floor(Sx)));
                SMaxX    = std::max(SMaxX, static_cast<int>(std::ceil(Sx)));
                SMinY    = std::min(SMinY, static_cast<int>(std::floor(Sy)));
                SMaxY    = std::max(SMaxY, static_cast<int>(std::ceil(Sy)));
            }
        }
        if (!AnyFront)
        {
            ++Report.FrustumCulledCount;
            continue;
        }

        SMinX = std::clamp(SMinX, 0, static_cast<int>(Camera.ViewportWidthPx) - 1);
        SMaxX = std::clamp(SMaxX, 0, static_cast<int>(Camera.ViewportWidthPx) - 1);
        SMinY = std::clamp(SMinY, 0, static_cast<int>(Camera.ViewportHeightPx) - 1);
        SMaxY = std::clamp(SMaxY, 0, static_cast<int>(Camera.ViewportHeightPx) - 1);

        const ScreenBoundCluster Bound{Gid, SMinX, SMaxX, SMinY, SMaxY, NearZ, FarZ};
        if (Rec.TransmissiveGlass)
        {
            GlassCandidates.push_back(Bound);
        }
        else
        {
            OpaqueCandidates.push_back(Bound);
        }
    }

    // Two-phase coarse Hi-Z pyramid occlusion: ONLY opaque clusters write depth; glass never occludes geometry behind it
    std::sort(OpaqueCandidates.begin(),
              OpaqueCandidates.end(),
              [](const ScreenBoundCluster& A, const ScreenBoundCluster& B) { return A.NearZ < B.NearZ; });

    constexpr uint32_t TileDiv = 4u;
    const uint32_t     HiZW    = std::max(1u, (Camera.ViewportWidthPx + TileDiv - 1u) / TileDiv);
    const uint32_t     HiZH    = std::max(1u, (Camera.ViewportHeightPx + TileDiv - 1u) / TileDiv);
    std::vector<float> CoarseOpaqueDepth(static_cast<size_t>(HiZW) * HiZH, 1.0e9f);

    std::vector<uint8_t> GlassCoveredTile(static_cast<size_t>(HiZW) * HiZH, 0u);
    std::vector<float>   GlassNearTile(static_cast<size_t>(HiZW) * HiZH, 1.0e9f);
    for (const ScreenBoundCluster& Gc : GlassCandidates)
    {
        const int Tx0 = Gc.MinX / static_cast<int>(TileDiv);
        const int Tx1 = Gc.MaxX / static_cast<int>(TileDiv);
        const int Ty0 = Gc.MinY / static_cast<int>(TileDiv);
        const int Ty1 = Gc.MaxY / static_cast<int>(TileDiv);
        for (int Ty = Ty0; Ty <= Ty1; ++Ty)
        {
            for (int Tx = Tx0; Tx <= Tx1; ++Tx)
            {
                const size_t Idx      = static_cast<size_t>(Ty) * HiZW + static_cast<size_t>(Tx);
                GlassCoveredTile[Idx] = 1u;
                GlassNearTile[Idx]    = std::min(GlassNearTile[Idx], Gc.NearZ);
            }
        }
    }

    auto RasterClusterIntoCoarseHiZ = [&](uint32_t Gid) {
        const ClusterRefinementRecord& Rec  = Records_[Gid];
        const ClusterTier&            Tier = Sequence_.Tiers[Rec.TierIndex];
        const FacetCluster&           Fc   = Tier.Clusters[Rec.ClusterIndexInTier];
        for (uint32_t Tri : Fc.Triangles)
        {
            float               Sx[3]{}, Sy[3]{}, Sz[3]{};
            const ClusterFloat3 Vp0 = ToFloat3(Tier.Complex.Positions[Tier.Complex.Triangles[Tri * 3u + 0u]]);
            const ClusterFloat3 Vp1 = ToFloat3(Tier.Complex.Positions[Tier.Complex.Triangles[Tri * 3u + 1u]]);
            const ClusterFloat3 Vp2 = ToFloat3(Tier.Complex.Positions[Tier.Complex.Triangles[Tri * 3u + 2u]]);
            if (!ProjectPointToScreen(Vp0, Camera, Basis, Sx[0], Sy[0], Sz[0]) ||
                !ProjectPointToScreen(Vp1, Camera, Basis, Sx[1], Sy[1], Sz[1]) ||
                !ProjectPointToScreen(Vp2, Camera, Basis, Sx[2], Sy[2], Sz[2]))
            {
                continue;
            }
            const float Area2 = (Sx[1] - Sx[0]) * (Sy[2] - Sy[0]) - (Sy[1] - Sy[0]) * (Sx[2] - Sx[0]);
            if (std::abs(Area2) < 1.0e-3f)
            {
                continue;
            }
            const int   MaxHx   = static_cast<int>(HiZW) - 1;
            const int   MaxHy   = static_cast<int>(HiZH) - 1;
            const int   Tx0     = std::clamp(static_cast<int>(std::floor(std::min({Sx[0], Sx[1], Sx[2]}) / TileDiv)), 0, MaxHx);
            const int   Tx1     = std::clamp(static_cast<int>(std::floor(std::max({Sx[0], Sx[1], Sx[2]}) / TileDiv)), 0, MaxHx);
            const int   Ty0     = std::clamp(static_cast<int>(std::floor(std::min({Sy[0], Sy[1], Sy[2]}) / TileDiv)), 0, MaxHy);
            const int   Ty1     = std::clamp(static_cast<int>(std::floor(std::max({Sy[0], Sy[1], Sy[2]}) / TileDiv)), 0, MaxHy);
            const float InvArea = 1.0f / Area2;
            const float MaxTriZ = std::max({Sz[0], Sz[1], Sz[2]});

            for (int Ty = Ty0; Ty <= Ty1; ++Ty)
            {
                for (int Tx = Tx0; Tx <= Tx1; ++Tx)
                {
                    const float Px = (static_cast<float>(Tx) + 0.5f) * static_cast<float>(TileDiv);
                    const float Py = (static_cast<float>(Ty) + 0.5f) * static_cast<float>(TileDiv);
                    const float W0 = ((Sx[1] - Px) * (Sy[2] - Py) - (Sy[1] - Py) * (Sx[2] - Px)) * InvArea;
                    const float W1 = ((Sx[2] - Px) * (Sy[0] - Py) - (Sy[2] - Py) * (Sx[0] - Px)) * InvArea;
                    const float W2 = 1.0f - W0 - W1;
                    if (W0 >= -0.05f && W1 >= -0.05f && W2 >= -0.05f)
                    {
                        const size_t Idx       = static_cast<size_t>(Ty) * HiZW + static_cast<size_t>(Tx);
                        CoarseOpaqueDepth[Idx] = std::min(CoarseOpaqueDepth[Idx], MaxTriZ);
                    }
                }
            }
        }
    };

    for (const ScreenBoundCluster& Oc : OpaqueCandidates)
    {
        const int Tx0 = Oc.MinX / static_cast<int>(TileDiv);
        const int Tx1 = Oc.MaxX / static_cast<int>(TileDiv);
        const int Ty0 = Oc.MinY / static_cast<int>(TileDiv);
        const int Ty1 = Oc.MaxY / static_cast<int>(TileDiv);

        float FarthestOccluderBounds = 0.0f;
        bool  BehindCloserGlass      = false;
        for (int Ty = Ty0; Ty <= Ty1; ++Ty)
        {
            for (int Tx = Tx0; Tx <= Tx1; ++Tx)
            {
                const size_t Idx       = static_cast<size_t>(Ty) * HiZW + static_cast<size_t>(Tx);
                FarthestOccluderBounds = std::max(FarthestOccluderBounds, CoarseOpaqueDepth[Idx]);
                if (GlassCoveredTile[Idx] != 0u && Oc.NearZ > GlassNearTile[Idx])
                {
                    BehindCloserGlass = true;
                }
            }
        }

        if (Oc.NearZ > FarthestOccluderBounds + 0.05f)
        {
            ++Report.HiZOccludedOpaqueCount;
            continue;
        }
        if (BehindCloserGlass)
        {
            ++Report.OpaqueBehindGlassSurvived;
        }

        const auto& Rec   = Records_[Oc.GlobalId];
        const float DistM = static_cast<float>(ClusterSurfaceDistance(Rec.SphereCentre, Rec.SphereRadiusMeters, Camera.CameraOrigin));
        const float ErrPx = static_cast<float>(ProjectedErrorPixels(
            Rec.SelfDeviationMeters, Rec.OpticalDeviationBoost, Rec.SphereCentre, Rec.SphereRadiusMeters, Camera.CameraOrigin, ProjScale));
        const auto MatEval = EvaluateClusterMaterial(Oc.GlobalId, DistM, ErrPx, Camera.Detail);

        Report.SelectedOpaqueGlobals.push_back(Oc.GlobalId);
        Report.SelectedOpaqueMaterialTiers.push_back(MatEval.ActiveTier);
        ++Report.MaterialTierClusterCounts[static_cast<size_t>(MatEval.ActiveTier)];
        Report.ActiveTrianglesAfterCull += Rec.TriangleCount;
        RasterClusterIntoCoarseHiZ(Oc.GlobalId);
    }

    for (const ScreenBoundCluster& Gc : GlassCandidates)
    {
        const int Tx0 = Gc.MinX / static_cast<int>(TileDiv);
        const int Tx1 = Gc.MaxX / static_cast<int>(TileDiv);
        const int Ty0 = Gc.MinY / static_cast<int>(TileDiv);
        const int Ty1 = Gc.MaxY / static_cast<int>(TileDiv);

        float FarthestOccluder = 0.0f;
        for (int Ty = Ty0; Ty <= Ty1; ++Ty)
        {
            for (int Tx = Tx0; Tx <= Tx1; ++Tx)
            {
                FarthestOccluder =
                    std::max(FarthestOccluder, CoarseOpaqueDepth[static_cast<size_t>(Ty) * HiZW + static_cast<size_t>(Tx)]);
            }
        }
        if (Gc.NearZ > FarthestOccluder + 0.05f)
        {
            continue;
        }
        Report.SelectedGlassGlobals.push_back(Gc.GlobalId);
        ++Report.MaterialTierClusterCounts[0];
        Report.ActiveTrianglesAfterCull += Records_[Gc.GlobalId].TriangleCount;
    }

    return Report;
}

ClusterMemoryTelemetry ScreenSpaceClusterRefinement::QueryMemoryTelemetry(
    const ClusterRuntimeFrameReport& LastFrameReport,
    uint32_t                         ConfiguredVramBudgetBytes) const noexcept
{
    ClusterMemoryTelemetry Out{};
    Out.RawUnindexedRamBytes      = RawUnindexedRamBytes_;
    Out.RefinementTierCount        = static_cast<uint32_t>(Sequence_.Tiers.size());
    Out.TotalClusterCount         = static_cast<uint32_t>(Records_.size());
    Out.TotalStreamingPages       = static_cast<uint32_t>(Pages_.size());
    Out.ConfiguredVramBudgetBytes = ConfiguredVramBudgetBytes;
    Out.SourceLeafTriangles       = Sequence_.SourceTriangleCount;
    Out.ActiveCutTriangles        = LastFrameReport.ActiveTrianglesAfterCull;
    Out.TriangleRetentionFraction = (Sequence_.SourceTriangleCount > 0u)
                                        ? (static_cast<float>(LastFrameReport.ActiveTrianglesAfterCull) /
                                           static_cast<float>(Sequence_.SourceTriangleCount))
                                        : 0.0f;
    Out.MaterialTierClusterCounts = LastFrameReport.MaterialTierClusterCounts;

    for (size_t TierIdx = 0u; TierIdx < Sequence_.Tiers.size(); ++TierIdx)
    {
        const ClusterTier& Tier = Sequence_.Tiers[TierIdx];
        const uint64_t TierBytes =
            Tier.Complex.Positions.size() * sizeof(FacetPoint3D) +
            Tier.Complex.Triangles.size() * sizeof(uint32_t) +
            Tier.Complex.CornerUVs.size() * sizeof(std::array<double, 2>) +
            Tier.Complex.Classification.size() * sizeof(uint32_t);
        if (TierIdx == 0u)
        {
            Out.WeldedLeafRamBytes = TierBytes;
        }
        Out.RefinementDagRamBytes += TierBytes;
    }

    Out.ClusterMetadataRamBytes = Records_.size() * sizeof(ClusterRefinementRecord);
    for (const ClusterRefinementRecord& Rec : Records_)
    {
        Out.ClusterMetadataRamBytes += Rec.EnclosedFinerGlobals.size() * sizeof(uint32_t);
    }
    Out.TotalHostRamBytes = Out.RefinementDagRamBytes + Out.ClusterMetadataRamBytes;

    for (const ClusterStreamingPage& Page : Pages_)
    {
        Out.TotalPagedVramBytes += Page.ByteSize;
        if (Page.PinnedRoot)
        {
            ++Out.PinnedRootPages;
        }
        if (Page.ResidentInMemory)
        {
            ++Out.ResidentStreamingPages;
            Out.ResidentVramBytes += Page.ByteSize;
        }
        else
        {
            Out.EvictedPageBytes += Page.ByteSize;
        }
    }

    const uint32_t ActiveClusters = Out.MaterialTierClusterCounts[0] +
                                    Out.MaterialTierClusterCounts[1] +
                                    Out.MaterialTierClusterCounts[2];
    if (ActiveClusters > 0u)
    {
        const float TotalBytes = static_cast<float>(Out.MaterialTierClusterCounts[0] * 64u +
                                                    Out.MaterialTierClusterCounts[1] * 24u +
                                                    Out.MaterialTierClusterCounts[2] * 16u);
        Out.MeanMaterialBytesPerCluster = TotalBytes / static_cast<float>(ActiveClusters);
    }
    return Out;
}

} // namespace Frontier
