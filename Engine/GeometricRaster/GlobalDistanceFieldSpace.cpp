//============================================================================================================================================
//                                                GLOBALDISTANCEFIELDSPACE.CPP
//============================================================================================================================================
// 📦 World-space composite distance field volume and cascading 3D clipmaps accelerating scene-wide ray marching and coarse visibility queries.

#include "GlobalDistanceFieldSpace.h"
#include <algorithm>
#include <cmath>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                           CASCADED GLOBAL DISTANCE FIELD
//------------------------------------------------------------------------------------------------------------------------

CascadedGlobalDistanceField::CascadedGlobalDistanceField() noexcept
{
    Initialize(64u, 64u, 48u);
}

void CascadedGlobalDistanceField::Initialize(uint32_t ResX, uint32_t ResY, uint32_t ResZ) noexcept
{
    // Level 0: Fine Near-Field (sub-centimeter to 6cm resolution, 4m x 4m x 3m extent)
    Levels[0].LevelIndex = 0u;
    Levels[0].HalfExtent = Vector3{ 2.0f, 2.0f, 1.5f };
    Levels[0].VoxelSize  = (Levels[0].HalfExtent.x * 2.0f) / static_cast<float>(ResX);
    Levels[0].Center     = Vector3{ 0.0f, 0.0f, 0.75f };
    Levels[0].Volume     = DistanceFieldSpace(ResX, ResY, ResZ,
                                              Levels[0].Center - Levels[0].HalfExtent,
                                              Levels[0].Center + Levels[0].HalfExtent);

    // Level 1: Medium Field (18cm resolution, 14m x 14m x 6m extent)
    Levels[1].LevelIndex = 1u;
    Levels[1].HalfExtent = Vector3{ 7.0f, 7.0f, 3.0f };
    Levels[1].VoxelSize  = (Levels[1].HalfExtent.x * 2.0f) / static_cast<float>(ResX);
    Levels[1].Center     = Vector3{ 0.0f, 0.0f, 1.5f };
    Levels[1].Volume     = DistanceFieldSpace(ResX, ResY, ResZ,
                                              Levels[1].Center - Levels[1].HalfExtent,
                                              Levels[1].Center + Levels[1].HalfExtent);

    // Level 2: Coarse Far-Field (70cm resolution, 48m x 48m x 16m extent)
    Levels[2].LevelIndex = 2u;
    Levels[2].HalfExtent = Vector3{ 24.0f, 24.0f, 8.0f };
    Levels[2].VoxelSize  = (Levels[2].HalfExtent.x * 2.0f) / static_cast<float>(ResX);
    Levels[2].Center     = Vector3{ 0.0f, 0.0f, 4.0f };
    Levels[2].Volume     = DistanceFieldSpace(ResX, ResY, ResZ,
                                              Levels[2].Center - Levels[2].HalfExtent,
                                              Levels[2].Center + Levels[2].HalfExtent);
}

void CascadedGlobalDistanceField::UpdateCameraPosition(Vector3 NewCameraPosition,
                                                       const std::vector<DistanceFieldPlacement>& Placements) noexcept
{
    CameraPosition = NewCameraPosition;
    bool NeedsUpdate = false;

    for (uint32_t L = 0u; L < kLevelCount; ++L)
    {
        // Snap center to voxel grid steps to prevent crawling/shimmering artifacts
        const float Step = Levels[L].VoxelSize * 2.0f;
        const Vector3 DesiredCenter = {
            std::floor(NewCameraPosition.x / Step + 0.5f) * Step,
            std::floor(NewCameraPosition.y / Step + 0.5f) * Step,
            std::max(Levels[L].HalfExtent.z * 0.5f, std::floor(NewCameraPosition.z / Step + 0.5f) * Step)
        };

        if ((DesiredCenter - Levels[L].Center).Length() > Levels[L].VoxelSize)
        {
            Levels[L].Center = DesiredCenter;
            const uint32_t ResX = Levels[L].Volume.GetResolutionX();
            const uint32_t ResY = Levels[L].Volume.GetResolutionY();
            const uint32_t ResZ = Levels[L].Volume.GetResolutionZ();
            Levels[L].Volume = DistanceFieldSpace(ResX, ResY, ResZ,
                                                  Levels[L].Center - Levels[L].HalfExtent,
                                                  Levels[L].Center + Levels[L].HalfExtent);
            NeedsUpdate = true;
        }
    }

    if (NeedsUpdate)
    {
        UpdateClipmaps(Placements);
    }
}

void CascadedGlobalDistanceField::UpdateClipmaps(const std::vector<DistanceFieldPlacement>& Placements) noexcept
{
    for (uint32_t L = 0u; L < kLevelCount; ++L)
    {
        auto& Vol = Levels[L].Volume;
        const uint32_t ResX = Vol.GetResolutionX();
        const uint32_t ResY = Vol.GetResolutionY();
        const uint32_t ResZ = Vol.GetResolutionZ();
        const Vector3 BMin = Vol.GetBoundingMinimum();
        const Vector3 BMax = Vol.GetBoundingMaximum();
        const Vector3 Span = BMax - BMin;

        for (uint32_t Z = 0u; Z < ResZ; ++Z)
        {
            const float RatioZ = (static_cast<float>(Z) + 0.5f) / static_cast<float>(ResZ);
            const float WorldZ = BMin.z + RatioZ * Span.z;

            for (uint32_t Y = 0u; Y < ResY; ++Y)
            {
                const float RatioY = (static_cast<float>(Y) + 0.5f) / static_cast<float>(ResY);
                const float WorldY = BMin.y + RatioY * Span.y;

                for (uint32_t X = 0u; X < ResX; ++X)
                {
                    const float RatioX = (static_cast<float>(X) + 0.5f) / static_cast<float>(ResX);
                    const float WorldX = BMin.x + RatioX * Span.x;

                    const Vector3 WorldPosition = { WorldX, WorldY, WorldZ };
                    float MinDist = 1e6f;

                    for (const auto& P : Placements)
                    {
                        if (!P.LocalField) continue;

                        const Vector3 LocalPos = {
                            (WorldPosition.x - P.WorldTranslation.x) / P.WorldScale.x,
                            (WorldPosition.y - P.WorldTranslation.y) / P.WorldScale.y,
                            (WorldPosition.z - P.WorldTranslation.z) / P.WorldScale.z
                        };

                        const float LocalDist = P.LocalField->SampleDistance(LocalPos);
                        const float MinScale  = std::min({ P.WorldScale.x, P.WorldScale.y, P.WorldScale.z });
                        const float ScaledDist = LocalDist * MinScale;

                        if (ScaledDist < MinDist)
                            MinDist = ScaledDist;
                    }

                    Vol.SetVoxelSample(X, Y, Z, MinDist);
                }
            }
        }
    }
}

float CascadedGlobalDistanceField::SampleDistance(Vector3 WorldPosition, uint32_t* OutLevelUsed) const noexcept
{
    // Try clipmap levels from finest (0) to coarsest (2)
    for (uint32_t L = 0u; L < kLevelCount; ++L)
    {
        const Vector3 LocalDelta = {
            std::abs(WorldPosition.x - Levels[L].Center.x),
            std::abs(WorldPosition.y - Levels[L].Center.y),
            std::abs(WorldPosition.z - Levels[L].Center.z)
        };

        // Inner margin to allow smooth trilinear sampling
        const Vector3 Margin = Levels[L].HalfExtent * 0.90f;
        if (LocalDelta.x <= Margin.x && LocalDelta.y <= Margin.y && LocalDelta.z <= Margin.z)
        {
            if (OutLevelUsed) *OutLevelUsed = L;
            return Levels[L].Volume.SampleDistance(WorldPosition);
        }
    }

    if (OutLevelUsed) *OutLevelUsed = kLevelCount - 1u;
    return Levels[kLevelCount - 1u].Volume.SampleDistance(WorldPosition);
}

Vector3 CascadedGlobalDistanceField::SampleNormal(Vector3 WorldPosition) const noexcept
{
    uint32_t LevelIdx = 0u;
    SampleDistance(WorldPosition, &LevelIdx);
    return Levels[LevelIdx].Volume.SampleNormal(WorldPosition);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

GlobalDistanceFieldSpace::GlobalDistanceFieldSpace(uint32_t InResolutionX,
                                                   uint32_t InResolutionY,
                                                   uint32_t InResolutionZ,
                                                   Vector3 InWorldBoundingMinimum,
                                                   Vector3 InWorldBoundingMaximum) noexcept
    : GlobalVolume(InResolutionX, InResolutionY, InResolutionZ, InWorldBoundingMinimum, InWorldBoundingMaximum)
    , WorldBoundingMinimum(InWorldBoundingMinimum)
    , WorldBoundingMaximum(InWorldBoundingMaximum)
{
    InitializeCascades(InResolutionX, InResolutionY, InResolutionZ);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                REGISTRATION & UPDATE
//------------------------------------------------------------------------------------------------------------------------

uint32_t GlobalDistanceFieldSpace::RegisterPlacement(const DistanceFieldSpace* LocalField,
                                                     Vector3 Translation,
                                                     Vector3 Scale) noexcept
{
    if (!LocalField) return 0xFFFFFFFFu;

    DistanceFieldPlacement Placement{};
    Placement.WorldTranslation = Translation;
    Placement.WorldScale       = Scale;
    Placement.LocalField       = LocalField;
    Placement.InstanceIdentity = static_cast<uint32_t>(Placements.size());

    const Vector3 LocalMin = LocalField->GetBoundingMinimum();
    const Vector3 LocalMax = LocalField->GetBoundingMaximum();

    Placement.WorldBoundMin = Translation + Vector3{ LocalMin.x * Scale.x, LocalMin.y * Scale.y, LocalMin.z * Scale.z };
    Placement.WorldBoundMax = Translation + Vector3{ LocalMax.x * Scale.x, LocalMax.y * Scale.y, LocalMax.z * Scale.z };

    Placements.push_back(Placement);
    return Placement.InstanceIdentity;
}

void GlobalDistanceFieldSpace::UpdatePlacementTransform(uint32_t InstanceIdentity,
                                                        Vector3 NewTranslation,
                                                        Vector3 NewScale) noexcept
{
    if (InstanceIdentity >= Placements.size()) return;

    auto& Placement = Placements[InstanceIdentity];
    Placement.WorldTranslation = NewTranslation;
    Placement.WorldScale       = NewScale;

    if (Placement.LocalField)
    {
        const Vector3 LocalMin = Placement.LocalField->GetBoundingMinimum();
        const Vector3 LocalMax = Placement.LocalField->GetBoundingMaximum();
        Placement.WorldBoundMin = NewTranslation + Vector3{ LocalMin.x * NewScale.x, LocalMin.y * NewScale.y, LocalMin.z * NewScale.z };
        Placement.WorldBoundMax = NewTranslation + Vector3{ LocalMax.x * NewScale.x, LocalMax.y * NewScale.y, LocalMax.z * NewScale.z };
    }
}

void GlobalDistanceFieldSpace::UpdateGlobalGrid() noexcept
{
    const uint32_t ResX = GlobalVolume.GetResolutionX();
    const uint32_t ResY = GlobalVolume.GetResolutionY();
    const uint32_t ResZ = GlobalVolume.GetResolutionZ();
    const Vector3 BMin = GlobalVolume.GetBoundingMinimum();
    const Vector3 BMax = GlobalVolume.GetBoundingMaximum();
    const Vector3 Span = BMax - BMin;

    for (uint32_t Z = 0u; Z < ResZ; ++Z)
    {
        const float RatioZ = (static_cast<float>(Z) + 0.5f) / static_cast<float>(ResZ);
        const float WorldZ = BMin.z + RatioZ * Span.z;

        for (uint32_t Y = 0u; Y < ResY; ++Y)
        {
            const float RatioY = (static_cast<float>(Y) + 0.5f) / static_cast<float>(ResY);
            const float WorldY = BMin.y + RatioY * Span.y;

            for (uint32_t X = 0u; X < ResX; ++X)
            {
                const float RatioX = (static_cast<float>(X) + 0.5f) / static_cast<float>(ResX);
                const float WorldX = BMin.x + RatioX * Span.x;

                const Vector3 WorldPosition = { WorldX, WorldY, WorldZ };
                float MinimumSceneDistance = 1e6f;

                for (const auto& Placement : Placements)
                {
                    if (!Placement.LocalField) continue;

                    const Vector3 LocalPosition = {
                        (WorldPosition.x - Placement.WorldTranslation.x) / Placement.WorldScale.x,
                        (WorldPosition.y - Placement.WorldTranslation.y) / Placement.WorldScale.y,
                        (WorldPosition.z - Placement.WorldTranslation.z) / Placement.WorldScale.z
                    };

                    const float LocalDistance = Placement.LocalField->SampleDistance(LocalPosition);
                    const float MinimumScale  = std::min({ Placement.WorldScale.x, Placement.WorldScale.y, Placement.WorldScale.z });
                    const float ScaledDistance = LocalDistance * MinimumScale;

                    if (ScaledDistance < MinimumSceneDistance)
                    {
                        MinimumSceneDistance = ScaledDistance;
                    }
                }

                GlobalVolume.SetVoxelSample(X, Y, Z, MinimumSceneDistance);
            }
        }
    }

    Cascades.UpdateClipmaps(Placements);
}

void GlobalDistanceFieldSpace::InitializeCascades(uint32_t ResX, uint32_t ResY, uint32_t ResZ) noexcept
{
    Cascades.Initialize(ResX, ResY, ResZ);
}

void GlobalDistanceFieldSpace::UpdateCascades(Vector3 CameraPosition) noexcept
{
    Cascades.UpdateCameraPosition(CameraPosition, Placements);
}

float GlobalDistanceFieldSpace::SampleCascadedDistance(Vector3 WorldPosition, uint32_t* OutLevel) const noexcept
{
    return Cascades.SampleDistance(WorldPosition, OutLevel);
}

Vector3 GlobalDistanceFieldSpace::SampleCascadedNormal(Vector3 WorldPosition) const noexcept
{
    return Cascades.SampleNormal(WorldPosition);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SCENE QUERIES
//------------------------------------------------------------------------------------------------------------------------

float GlobalDistanceFieldSpace::SampleSceneDistance(Vector3 WorldPosition) const noexcept
{
    float MinDist = 1e6f;
    for (const auto& Placement : Placements)
    {
        if (!Placement.LocalField) continue;

        const Vector3 LocalPosition = {
            (WorldPosition.x - Placement.WorldTranslation.x) / Placement.WorldScale.x,
            (WorldPosition.y - Placement.WorldTranslation.y) / Placement.WorldScale.y,
            (WorldPosition.z - Placement.WorldTranslation.z) / Placement.WorldScale.z
        };

        const float LocalDist = Placement.LocalField->SampleDistance(LocalPosition);
        const float MinScale  = std::min({ Placement.WorldScale.x, Placement.WorldScale.y, Placement.WorldScale.z });
        const float ScaledDist = LocalDist * MinScale;

        if (ScaledDist < MinDist)
        {
            MinDist = ScaledDist;
        }
    }
    return MinDist;
}

Vector3 GlobalDistanceFieldSpace::SampleSceneNormal(Vector3 WorldPosition) const noexcept
{
    const float Epsilon = 0.005f;
    const float Dx = SampleSceneDistance({ WorldPosition.x + Epsilon, WorldPosition.y, WorldPosition.z }) -
                     SampleSceneDistance({ WorldPosition.x - Epsilon, WorldPosition.y, WorldPosition.z });
    const float Dy = SampleSceneDistance({ WorldPosition.x, WorldPosition.y + Epsilon, WorldPosition.z }) -
                     SampleSceneDistance({ WorldPosition.x, WorldPosition.y - Epsilon, WorldPosition.z });
    const float Dz = SampleSceneDistance({ WorldPosition.x, WorldPosition.y, WorldPosition.z + Epsilon }) -
                     SampleSceneDistance({ WorldPosition.x, WorldPosition.y, WorldPosition.z - Epsilon });

    const Vector3 Grad = { Dx, Dy, Dz };
    const float Len = Grad.Length();
    return (Len > 1e-7f) ? (Grad / Len) : Vector3{ 0.0f, 0.0f, 1.0f };
}

//------------------------------------------------------------------------------------------------------------------------
//                                            HIERARCHICAL SCENE RAY MARCH
//------------------------------------------------------------------------------------------------------------------------

DistanceFieldHitRecord GlobalDistanceFieldSpace::MarchSceneRay(Vector3 RayOrigin,
                                                               Vector3 RayDirection,
                                                               float MinimumDistance,
                                                               float MaximumDistance,
                                                               float SurfaceThreshold,
                                                               uint32_t MaximumSteps,
                                                               float StepRelaxation,
                                                               float /*TransitionDistance*/) const noexcept
{
    DistanceFieldHitRecord ResultRecord{};
    ResultRecord.TravelDistance   = MinimumDistance;
    ResultRecord.StepCount        = 0u;
    ResultRecord.InstanceIdentity = 0xFFFFFFFFu;
    ResultRecord.HasHit           = false;

    // Analytical ground plane intersection at z = 0
    float GroundT = MaximumDistance;
    if (std::abs(RayDirection.z) > 1e-6f)
    {
        const float T = -RayOrigin.z / RayDirection.z;
        if (T >= MinimumDistance && T < MaximumDistance)
        {
            GroundT = T;
        }
    }

    float CurrentDistance = MinimumDistance;
    const float MarchLimit = std::min(MaximumDistance, GroundT);

    for (uint32_t Step = 0u; Step < MaximumSteps && CurrentDistance < MarchLimit; ++Step)
    {
        ResultRecord.StepCount++;
        const Vector3 SamplePosition = RayOrigin + RayDirection * CurrentDistance;

        // Query nearest placement
        float NearestDistance = 1e6f;
        const DistanceFieldPlacement* HitPlacement = nullptr;
        Vector3 HitLocalPosition{};

        for (const auto& Placement : Placements)
        {
            if (!Placement.LocalField) continue;

            const float OutDx = std::max(0.0f, std::max(Placement.WorldBoundMin.x - SamplePosition.x, SamplePosition.x - Placement.WorldBoundMax.x));
            const float OutDy = std::max(0.0f, std::max(Placement.WorldBoundMin.y - SamplePosition.y, SamplePosition.y - Placement.WorldBoundMax.y));
            const float OutDz = std::max(0.0f, std::max(Placement.WorldBoundMin.z - SamplePosition.z, SamplePosition.z - Placement.WorldBoundMax.z));
            const float DistToBound = std::sqrt(OutDx * OutDx + OutDy * OutDy + OutDz * OutDz);

            if (DistToBound > 0.35f)
            {
                if (DistToBound < NearestDistance)
                    NearestDistance = DistToBound;
                continue;
            }

            const Vector3 LocalPos = {
                (SamplePosition.x - Placement.WorldTranslation.x) / Placement.WorldScale.x,
                (SamplePosition.y - Placement.WorldTranslation.y) / Placement.WorldScale.y,
                (SamplePosition.z - Placement.WorldTranslation.z) / Placement.WorldScale.z
            };

            const float LocalDist = Placement.LocalField->SampleDistance(LocalPos);
            const float MinScale  = std::min({ Placement.WorldScale.x, Placement.WorldScale.y, Placement.WorldScale.z });
            const float ScaledDist = LocalDist * MinScale;

            if (ScaledDist < NearestDistance)
            {
                NearestDistance = ScaledDist;
                HitPlacement = &Placement;
                HitLocalPosition = LocalPos;
            }
        }

        if (NearestDistance <= SurfaceThreshold)
        {
            ResultRecord.HasHit           = true;
            ResultRecord.TravelDistance   = CurrentDistance;
            ResultRecord.HitPosition      = SamplePosition;
            ResultRecord.InstanceIdentity = HitPlacement ? HitPlacement->InstanceIdentity : 0u;

            if (HitPlacement && HitPlacement->LocalField)
            {
                const Vector3 LocalNorm = HitPlacement->LocalField->SampleNormal(HitLocalPosition);
                ResultRecord.SurfaceNormal = LocalNorm.Normalized();
            }
            else
            {
                ResultRecord.SurfaceNormal = Vector3{ 0.0f, 0.0f, 1.0f };
            }
            return ResultRecord;
        }

        const float SafeAdvance = std::max(NearestDistance * StepRelaxation, SurfaceThreshold * 0.5f);
        CurrentDistance += SafeAdvance;
    }

    if (GroundT < MaximumDistance && GroundT >= MinimumDistance && RayDirection.z < 0.0f)
    {
        ResultRecord.HasHit           = true;
        ResultRecord.TravelDistance   = GroundT;
        ResultRecord.HitPosition      = RayOrigin + RayDirection * GroundT;
        ResultRecord.SurfaceNormal    = Vector3{ 0.0f, 0.0f, 1.0f };
        ResultRecord.InstanceIdentity = 0xFFFFFFFFu;
        return ResultRecord;
    }

    return ResultRecord;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                SCENE SOFT SHADOWS
//------------------------------------------------------------------------------------------------------------------------

float GlobalDistanceFieldSpace::MarchSceneSoftShadow(Vector3 ShadingPosition,
                                                     Vector3 IlluminantDirection,
                                                     float MinimumDistance,
                                                     float MaximumDistance,
                                                     float LightAngularSize,
                                                     uint32_t MaximumSteps,
                                                     uint32_t ReceiverInstance) const noexcept
{
    float MinPenumbra = 1.0f;

    for (const auto& Placement : Placements)
    {
        if (!Placement.LocalField) continue;

        const Vector3 InvScale = {
            1.0f / Placement.WorldScale.x,
            1.0f / Placement.WorldScale.y,
            1.0f / Placement.WorldScale.z
        };

        const Vector3 LocalOrigin = {
            (ShadingPosition.x - Placement.WorldTranslation.x) * InvScale.x,
            (ShadingPosition.y - Placement.WorldTranslation.y) * InvScale.y,
            (ShadingPosition.z - Placement.WorldTranslation.z) * InvScale.z
        };

        const Vector3 ScaledDir = {
            IlluminantDirection.x * InvScale.x,
            IlluminantDirection.y * InvScale.y,
            IlluminantDirection.z * InvScale.z
        };

        const float DirLength = ScaledDir.Length();
        if (DirLength < 1e-6f) continue;
        const Vector3 LocalDir = ScaledDir / DirLength;

        const float ObjectScale = std::min({ Placement.WorldScale.x, Placement.WorldScale.y, Placement.WorldScale.z });
        const float EffectiveMinDist = (Placement.InstanceIdentity == ReceiverInstance)
            ? std::max(MinimumDistance, 0.065f * ObjectScale)
            : MinimumDistance;

        const float LocalMinT = EffectiveMinDist * DirLength;
        const float LocalMaxT = MaximumDistance * DirLength;

        const float PlacementShadow = Placement.LocalField->MarchSoftShadow(
            LocalOrigin, LocalDir, LocalMinT, LocalMaxT, LightAngularSize, MaximumSteps
        );

        if (PlacementShadow < MinPenumbra)
        {
            MinPenumbra = PlacementShadow;
            if (MinPenumbra <= 0.001f)
            {
                return 0.0f;
            }
        }
    }

    return std::clamp(MinPenumbra, 0.0f, 1.0f);
}

} // namespace Frontier
