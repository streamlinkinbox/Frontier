/*============================================================================================================================================
                                                      PARTICLEVULKANEXECUTOR.H
============================================================================================================================================*/
// 🧩 Headless Vulkan 1.3 Compute executor for modular SoA particle channels, ForceField coupling, XPBD strands & 32 B async reduction

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/DistanceFieldSpace/SignedDistanceVolume.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldDescriptors.h"
#include "Engine/SimulationSpace/Particles/ParticleChannelBank.h"
#include "Engine/SimulationSpace/Particles/ParticleChannelLayouts.h"
#include "Engine/SimulationSpace/Particles/ParticleDynamics.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>

namespace Frontier::SimulationSpace::Particles
{

//------------------------------------------------------------------------------------------------------------------------
//                                             SPECIALIZED DOMAIN PASS SELECTOR
//------------------------------------------------------------------------------------------------------------------------

enum class SpecializedDomainPass : uint8_t
{
    None            = 0u,
    Ferrofluid      = 1u,
    PyroVolley      = 2u,
    BoidsFlock      = 3u,
    Fracture        = 4u,
    LatticeEffector = 5u,
    ShieldRipple    = 6u,
    SpaceRefraction = 7u,
};

//------------------------------------------------------------------------------------------------------------------------
//                                         SPECIALIZED DOMAIN UNIFORM BLOCK (128 B)
//------------------------------------------------------------------------------------------------------------------------

// Byte-exact mirror of GpuDomainUniforms in Engine/Shaders/Particles/ParticleChannelLayouts.slang
struct alignas(16) ParticleDomainUniformBlock
{
    std::array<float, 4u>    OriginScalarA    { 0.0f, 0.0f, 0.0f, 1.0f };
    std::array<float, 4u>    AuxVectorScalarB { 0.0f, 0.0f, 1.0f, 4.0f };
    std::array<float, 4u>    ScalarPackC      { 1.0f, 1.0f, 1.0f, 1.0f };
    std::array<float, 4u>    ScalarPackD      { 1.0f, 1.0f, 1.0f, 0.15f };
    std::array<float, 4u>    TimingCounts     { 1.0f / 60.0f, 0.0f, 0.0f, 0.45f };
    std::array<uint32_t, 4u> DomainControl    { 0u, 0u, 0u, 0u };
    std::array<float, 4u>    SdfBoundsMinVox  { 0.0f, 0.0f, 0.0f, 0.1f };
    std::array<uint32_t, 4u> SdfBrickDims     { 0u, 0u, 0u, 0x3F4CCCCDu };
};

static_assert(sizeof(ParticleDomainUniformBlock) == 128u, "ParticleDomainUniformBlock must remain exactly 128 bytes.");

//------------------------------------------------------------------------------------------------------------------------
//                                               GPU UNIFORM LAYOUT (288 B)
//------------------------------------------------------------------------------------------------------------------------

// Byte-exact mirror of GpuParticleUniforms in Engine/Shaders/Particles/ParticleChannelLayouts.slang
struct alignas(16) ParticleGpuUniformBlock
{
    float    DeltaTimeSec     = 1.0f / 60.0f; // [s]   - Fixed simulation substep
    float    TimeSeconds      = 0.0f;         // [s]   - Accumulated simulation time
    uint32_t ActiveCount      = 0u;           // [cnt] - Active slot range [0 .. ActiveCount)
    uint32_t Capacity         = 0u;           // [cnt] - Allocated SSBO capacity

    uint32_t ReleaseStartSlot = 0u;           // [idx] - First slot for Release compute dispatch
    uint32_t ReleaseCount     = 0u;           // [cnt] - Slots released this dispatch
    uint32_t SourceCount      = 0u;           // [cnt] - Active ForceSourceShaderEntry count
    uint32_t MasterSeed       = 1337u;        // [-]   - Deterministic PRNG seed

    std::array<float, 4u>    GravityDragCurl    { 1.0f, 0.0f, 0.25f, 0.0f };       // GravityScale, Buoyancy, Drag, CurlAmp
    std::array<float, 4u>    CurlTimingBounds   { 0.85f, 0.6f, 0.0f, 0.35f };      // CurlFreq, CurlScroll, FloorZ, FloorRestit
    std::array<float, 4u>    ForceCoupling      { 1.0f, 1.0f, 1.0f, 0.15f };       // Gain, AccelScale, DragScale, FloorFriction
    std::array<float, 4u>    SphereCenterRadius { 0.0f, 0.0f, 1.0f, 0.45f };       // xyz = Center [m], w = Radius [m]
    std::array<float, 4u>    SphereVelBounce    { 0.0f, 0.0f, 0.0f, 0.45f };       // xyz = Vel [m/s], w = Restitution
    std::array<uint32_t, 4u> ColliderControl      { 1u, 0u, 0u, 0u };                // FloorOn, SphereOn, StrandMode, TickIndex

    std::array<float, 4u>    ColorStopA         { 1.0f, 0.85f, 0.42f, 1.0f };      // Ramp stop 0.0
    std::array<float, 4u>    ColorStopC         { 0.96f, 0.48f, 0.22f, 0.92f };    // Ramp stop 0.5
    std::array<float, 4u>    ColorStopB         { 0.32f, 0.18f, 0.14f, 0.0f };     // Ramp stop 1.0
    std::array<float, 4u>    SizeSpinMetrics    { 0.08f, 0.03f, 1.0f, 1.2f };      // SizeStart, SizeEnd, SizeMul, SpinMax
    std::array<float, 4u>    ColorDriverNorm    { 10.0f, 6.0f, 0.0f, 0.92f };      // SpeedMax, HeightMax, Phase, StrandStiff
    std::array<uint32_t, 4u> ColorDriverMode    { 0u, 0u, 1u, 0x3F266666u };       // Driver, Wrap, Shape, CrossStiffBits

    std::array<float, 4u>    ReleaseOriginRadius{ 0.0f, 0.0f, 1.2f, 0.35f };       // xyz = Origin [m], w = Radius [m]
    std::array<float, 4u>    ReleaseDirSpread   { 0.0f, 0.0f, 1.0f, 0.52f };       // xyz = Dir, w = ConeSpread [rad]
    std::array<float, 4u>    ReleaseBoxExtents  { 1.0f, 1.0f, 1.0f, 1.2f };        // xyz = HalfExtents [m], w = SpeedMin
    std::array<float, 4u>    ReleaseSpeedLife   { 4.2f, 1.2f, 2.6f, 1.0f };        // SpeedMax, LifeMin, LifeMax, ImpulseScale
};

static_assert(sizeof(ParticleGpuUniformBlock) == 288u, "ParticleGpuUniformBlock must remain exactly 288 bytes.");

//------------------------------------------------------------------------------------------------------------------------
//                                                  GPU DISPATCH REQUEST
//------------------------------------------------------------------------------------------------------------------------

struct ParticleGpuStepRequest
{
    ParticleGpuUniformBlock                                       Uniforms              {};
    std::span<const ForceFields::ForceSourceShaderEntry>          PackedForceSources    {};
    std::span<const ForceFields::SpatialVector3>                  PackedForceWaypoints  {};
    uint32_t                                                      StrandRelaxIterations = 0u; // [iter] - 0 disables strand pass
    SpecializedDomainPass                                         DomainPass            = SpecializedDomainPass::None;
    ParticleDomainUniformBlock                                    DomainUniforms        {};
    bool                                                          ApplySdfCollider      = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                               PARTICLEVULKANEXECUTOR
//------------------------------------------------------------------------------------------------------------------------

class ParticleVulkanExecutor
{
public:
    ParticleVulkanExecutor() noexcept;
    ~ParticleVulkanExecutor() noexcept;

    ParticleVulkanExecutor(const ParticleVulkanExecutor&)            = delete;
    ParticleVulkanExecutor& operator=(const ParticleVulkanExecutor&) = delete;

    [[nodiscard]] bool Initialize(
        const std::filesystem::path& ShaderBinaryDir,
        const std::filesystem::path& SwiftShaderIcdDir,
        std::string&                 OutDiagnostic) noexcept;

    void Finalize() noexcept;

    [[nodiscard]] bool        QueryDeviceReady() const noexcept;
    [[nodiscard]] std::string QueryDeviceName() const;
    [[nodiscard]] void*       QueryNativeInstance() const noexcept;
    [[nodiscard]] void*       QueryNativePhysicalDevice() const noexcept;
    [[nodiscard]] void*       QueryNativeDevice() const noexcept;
    [[nodiscard]] void*       QueryNativeQueue() const noexcept;
    [[nodiscard]] uint32_t    QueryQueueFamilyIndex() const noexcept;
    [[nodiscard]] uint32_t    QueryAllocatedCapacity() const noexcept;

    [[nodiscard]] void*       QueryKinematicsBufferHandle() const noexcept;
    [[nodiscard]] void*       QueryLifespanBufferHandle() const noexcept;
    [[nodiscard]] void*       QueryAppearanceBufferHandle() const noexcept;
    [[nodiscard]] void*       QueryOrientationBufferHandle() const noexcept;
    [[nodiscard]] void*       QueryStrandLinkBufferHandle() const noexcept;
    [[nodiscard]] uint64_t    QueryKinematicsByteSize() const noexcept;
    [[nodiscard]] uint64_t    QueryLifespanByteSize() const noexcept;
    [[nodiscard]] uint64_t    QueryAppearanceByteSize() const noexcept;
    [[nodiscard]] uint64_t    QueryOrientationByteSize() const noexcept;
    [[nodiscard]] uint64_t    QueryStrandLinkByteSize() const noexcept;
    [[nodiscard]] uint32_t    QueryLastPyroAllocatedSlots() const noexcept;
    [[nodiscard]] bool        QueryDomainShadersLoaded() const noexcept;

    [[nodiscard]] bool EnsureChannelCapacity(
        uint32_t     SlotCapacity,
        bool         IncludeStrandChannels,
        std::string& OutDiagnostic) noexcept;

    [[nodiscard]] bool UploadSignedDistanceVolume(
        const DistanceFieldSpace::SignedDistanceVolume& Volume,
        std::string&                                    OutDiagnostic) noexcept;

    [[nodiscard]] bool UploadChannelBank(
        const ParticleChannelBank& SourceBank,
        std::string&               OutDiagnostic) noexcept;

    [[nodiscard]] bool DownloadChannelBank(
        ParticleChannelBank& TargetBank,
        std::string&         OutDiagnostic) noexcept;

    [[nodiscard]] bool DispatchStep(
        const ParticleGpuStepRequest& Request,
        ParticleReadbackSummary&      OutSummary,
        std::string&                  OutDiagnostic) noexcept;

private:
    struct DeviceImplementation;
    std::unique_ptr<DeviceImplementation> DeviceHandle{};
};

} // namespace Frontier::SimulationSpace::Particles
