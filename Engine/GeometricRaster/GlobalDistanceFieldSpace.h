//============================================================================================================================================
//                                                 GLOBALDISTANCEFIELDSPACE.H
//============================================================================================================================================
// 📦 World-space composite distance field volume and cascading 3D clipmaps accelerating scene-wide ray marching and coarse visibility queries.

#pragma once

#if defined(_MSC_VER)
    #pragma warning(disable: 4324)                              // Disable structure padding alignment warning under /WX
#endif

#include "DistanceFieldSpace.h"
#include <vector>
#include <memory>
#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                           MESH DISTANCE FIELD PLACEMENT
//------------------------------------------------------------------------------------------------------------------------

struct DistanceFieldPlacement
{
    Vector3                 WorldTranslation;                   // [m] world translation coordinate
    Vector3                 WorldScale        = { 1.0f, 1.0f, 1.0f }; // [-] axis-aligned scale factor
    Vector3                 WorldBoundMin;                      // [m] transformed axis-aligned bounding minimum
    Vector3                 WorldBoundMax;                      // [m] transformed axis-aligned bounding maximum
    const DistanceFieldSpace* LocalField      = nullptr;        // [-] reference to local object-space distance field
    uint32_t                InstanceIdentity  = 0u;             // [-] unique placement identifier
};

//------------------------------------------------------------------------------------------------------------------------
//                                    CASCADED GLOBAL DISTANCE FIELD (CLIPMAPS)
//------------------------------------------------------------------------------------------------------------------------

struct GDFClipmapLevel
{
    DistanceFieldSpace      Volume;                             // [-] 3D distance field voxel storage
    Vector3                 Center     = { 0.0f, 0.0f, 0.0f };  // [m] current world-space center
    Vector3                 HalfExtent = { 2.0f, 2.0f, 1.5f };  // [m] half-extent radius in world space
    float                   VoxelSize  = 0.05f;                 // [m] world extent per voxel cell
    uint32_t                LevelIndex = 0u;                    // [-] 0 = finest near-field, 2 = coarsest far-field
};

class CascadedGlobalDistanceField
{
public:
    static constexpr uint32_t kLevelCount = 3u;

    CascadedGlobalDistanceField() noexcept;

    // Initializes the 3 cascading clipmap levels with resolution and radii
    void Initialize(uint32_t ResX = 64u, uint32_t ResY = 64u, uint32_t ResZ = 48u) noexcept;

    // Updates camera position and re-centers clipmaps when camera moves
    void UpdateCameraPosition(Vector3 NewCameraPosition, const std::vector<DistanceFieldPlacement>& Placements) noexcept;

    // Rasterizes all registered placements into all 3 clipmap levels
    void UpdateClipmaps(const std::vector<DistanceFieldPlacement>& Placements) noexcept;

    // Continuous distance query: samples finest valid clipmap for given world position
    [[nodiscard]] float SampleDistance(Vector3 WorldPosition, uint32_t* OutLevelUsed = nullptr) const noexcept;

    // Continuous normal derived from central differences on the finest valid clipmap
    [[nodiscard]] Vector3 SampleNormal(Vector3 WorldPosition) const noexcept;

    // Accessors
    [[nodiscard]] const GDFClipmapLevel& GetLevel(uint32_t LevelIndex) const noexcept { return Levels[LevelIndex]; }
    [[nodiscard]] GDFClipmapLevel& GetMutableLevel(uint32_t LevelIndex) noexcept { return Levels[LevelIndex]; }
    [[nodiscard]] Vector3 GetCameraPosition() const noexcept { return CameraPosition; }

private:
    GDFClipmapLevel         Levels[kLevelCount];
    Vector3                 CameraPosition = { 0.0f, 0.0f, 0.0f };
};

//------------------------------------------------------------------------------------------------------------------------
//                                           GLOBAL DISTANCE FIELD SPACE
//------------------------------------------------------------------------------------------------------------------------

class GlobalDistanceFieldSpace
{
public:
    GlobalDistanceFieldSpace() noexcept = default;

    GlobalDistanceFieldSpace(uint32_t InResolutionX,
                             uint32_t InResolutionY,
                             uint32_t InResolutionZ,
                             Vector3 InWorldBoundingMinimum,
                             Vector3 InWorldBoundingMaximum) noexcept;

    // Registers a local distance field placement into the global scene collection
    uint32_t RegisterPlacement(const DistanceFieldSpace* LocalField,
                               Vector3 Translation,
                               Vector3 Scale = { 1.0f, 1.0f, 1.0f }) noexcept;

    // Updates the transformation of an existing placement (for moving objects dynamically)
    void UpdatePlacementTransform(uint32_t InstanceIdentity,
                                  Vector3 NewTranslation,
                                  Vector3 NewScale = { 1.0f, 1.0f, 1.0f }) noexcept;

    // Rasterizes registered local placements into the world volume grid
    void UpdateGlobalGrid() noexcept;

    // Cascaded GDF clipmap initialization and updates
    void InitializeCascades(uint32_t ResX = 64u, uint32_t ResY = 64u, uint32_t ResZ = 48u) noexcept;
    void UpdateCascades(Vector3 CameraPosition) noexcept;

    // Continuous distance query sampling the composited global volume
    [[nodiscard]] float SampleSceneDistance(Vector3 WorldPosition) const noexcept;

    // Continuous surface normal query derived by central differences from global volume
    [[nodiscard]] Vector3 SampleSceneNormal(Vector3 WorldPosition) const noexcept;

    // Cascaded continuous distance & normal queries
    [[nodiscard]] float SampleCascadedDistance(Vector3 WorldPosition, uint32_t* OutLevel = nullptr) const noexcept;
    [[nodiscard]] Vector3 SampleCascadedNormal(Vector3 WorldPosition) const noexcept;

    // Two-tier hierarchical ray march: coarse steps in global grid, refined in local fields near surfaces
    [[nodiscard]] DistanceFieldHitRecord MarchSceneRay(Vector3 RayOrigin,
                                                       Vector3 RayDirection,
                                                       float MinimumDistance,
                                                       float MaximumDistance,
                                                       float SurfaceThreshold   = 0.002f,
                                                       uint32_t MaximumSteps    = 128u,
                                                       float StepRelaxation     = 0.85f,
                                                       float TransitionDistance = 0.12f) const noexcept;

    // Scene-wide distance field soft shadow with contact hardening and receiver self-shadow bias
    [[nodiscard]] float MarchSceneSoftShadow(Vector3 ShadingPosition,
                                             Vector3 IlluminantDirection,
                                             float MinimumDistance,
                                             float MaximumDistance,
                                             float LightAngularSize  = 0.175f,
                                             uint32_t MaximumSteps   = 36u,
                                             uint32_t ReceiverInstance = 0xFFFFFFFFu) const noexcept;

    // Accessors
    [[nodiscard]] uint32_t GetPlacementCount() const noexcept { return static_cast<uint32_t>(Placements.size()); }
    [[nodiscard]] const DistanceFieldPlacement& GetPlacement(uint32_t Index) const noexcept { return Placements[Index]; }
    [[nodiscard]] const DistanceFieldSpace& GetGlobalVolume() const noexcept { return GlobalVolume; }
    [[nodiscard]] DistanceFieldSpace& GetMutableGlobalVolume() noexcept { return GlobalVolume; }
    [[nodiscard]] const CascadedGlobalDistanceField& GetCascadedGDF() const noexcept { return Cascades; }
    [[nodiscard]] CascadedGlobalDistanceField& GetMutableCascadedGDF() noexcept { return Cascades; }

private:
    std::vector<DistanceFieldPlacement> Placements;             // [-] registered scene mesh placements
    DistanceFieldSpace      GlobalVolume;                       // [m] world-space coarse composite volume
    CascadedGlobalDistanceField Cascades;                       // [-] 3-level nested cascading clipmaps
    Vector3                 WorldBoundingMinimum = { -5.0f, -5.0f, -0.5f }; // [m] scene bounding minimum
    Vector3                 WorldBoundingMaximum = {  5.0f,  5.0f,  3.5f }; // [m] scene bounding maximum
};

} // namespace Frontier
