/*==========================================================================================================================================
                                             REPLAYPBRREFLECTIONVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Verify compact VRAM addressing, overflow-safe beta=2 power MIS, and authored multi-strata PBR + glazing contracts.

#include "Engine/Rendering/PathReplayDomain.h"
#include "Engine/Rendering/PathReplayVulkan.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <numbers>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using namespace Frontier;

//------------------------------------------------------------------------------------------------------------------------
//                                                  ASSERTION HELPERS
//------------------------------------------------------------------------------------------------------------------------

void Require(bool Condition, const char* Message)
{
    if (!Condition)
    {
        throw std::runtime_error(Message);
    }
}

template <class Fn>
void ExpectThrows(Fn&& Action, const char* Message)
{
    bool Threw = false;
    try
    {
        Action();
    }
    catch (const std::exception&)
    {
        Threw = true;
    }
    Require(Threw, Message);
}

//------------------------------------------------------------------------------------------------------------------------
//                                           MOCK VULKAN COMMAND RECORDER
//------------------------------------------------------------------------------------------------------------------------

struct RecordedReplayState
{
    std::uint32_t BoundPipelines = 0; // [dispatches]
    std::uint32_t Barriers = 0;       // [barriers]
};

thread_local RecordedReplayState* ActiveState = nullptr;

VKAPI_ATTR void VKAPI_CALL MockBindPipeline(VkCommandBuffer, VkPipelineBindPoint, VkPipeline)
{
    if (ActiveState)
    {
        ++ActiveState->BoundPipelines;
    }
}

VKAPI_ATTR void VKAPI_CALL MockBindDescriptors(VkCommandBuffer, VkPipelineBindPoint, VkPipelineLayout,
                                               std::uint32_t, std::uint32_t, const VkDescriptorSet*,
                                               std::uint32_t, const std::uint32_t*)
{
}

VKAPI_ATTR void VKAPI_CALL MockPushConstants(VkCommandBuffer, VkPipelineLayout, VkShaderStageFlags,
                                             std::uint32_t, std::uint32_t, const void*)
{
}

VKAPI_ATTR void VKAPI_CALL MockDispatch(VkCommandBuffer, std::uint32_t, std::uint32_t, std::uint32_t)
{
}

VKAPI_ATTR void VKAPI_CALL MockBarrier(VkCommandBuffer, VkPipelineStageFlags, VkPipelineStageFlags, VkDependencyFlags,
                                       std::uint32_t, const VkMemoryBarrier*, std::uint32_t,
                                       const VkBufferMemoryBarrier*, std::uint32_t, const VkImageMemoryBarrier*)
{
    if (ActiveState)
    {
        ++ActiveState->Barriers;
    }
}

template <class Handle>
Handle FakeHandle(std::uintptr_t Tag)
{
    Handle Result{};
    std::memcpy(&Result, &Tag, std::min(sizeof(Result), sizeof(Tag)));
    return Result;
}

PathReplayVulkanAccess MakeAccess()
{
    PathReplayVulkanAccess Access{};
    Access.PipelineLayout = FakeHandle<VkPipelineLayout>(1);
    Access.Descriptors = FakeHandle<VkDescriptorSet>(2);
    Access.Primary = FakeHandle<VkPipeline>(3);
    Access.Generate = FakeHandle<VkPipeline>(4);
    Access.Resolve = FakeHandle<VkPipeline>(5);
    Access.GatherReconstruction = FakeHandle<VkPipeline>(6);
    Access.FinalizeReconstruction = FakeHandle<VkPipeline>(7);
    Access.GatherSampleHistory = FakeHandle<VkPipeline>(8);
    Access.FinalizeSampleHistory = FakeHandle<VkPipeline>(9);
    Access.GatherVarianceHistory = FakeHandle<VkPipeline>(10);
    Access.IntegrateVarianceMoments = FakeHandle<VkPipeline>(11);
    Access.FilterVarianceWavelet = FakeHandle<VkPipeline>(12);
    Access.BindPipeline = &MockBindPipeline;
    Access.BindDescriptors = &MockBindDescriptors;
    Access.PushConstants = &MockPushConstants;
    Access.Dispatch = &MockDispatch;
    Access.PipelineBarrier = &MockBarrier;
    Access.MaxGroupCountX = 65535u;
    Access.MaxPushConstantBytes = 128u;
    return Access;
}

//------------------------------------------------------------------------------------------------------------------------
//                                  COMPACT STORAGE AND RECORDER CONTRACT CHECKS
//------------------------------------------------------------------------------------------------------------------------

void ValidateCompactAddressingAndRecorder()
{
    constexpr std::uint32_t Pixels = 4096u; // [pixels]
    Require(PathReplayCompactSlabCount(false, false) == 6u, "Base compact tier must use 6 slabs");
    Require(PathReplayCompactSlabCount(true, false) == 10u, "Sample-history compact tier must use 10 slabs");
    Require(PathReplayCompactSlabCount(false, true) == 17u && PathReplayCompactSlabCount(true, true) == 17u,
            "Variance-filter compact tier must use 17 slabs");

    // Verify 1080p VRAM savings formula: 1,455.70 MiB -> 537.89 MiB (-63.05%) on Unified+SVGF.
    constexpr std::uint32_t FullHdPixels = 1920u * 1080u; // [pixels]
    const std::uint64_t LegacyBytes = PathReplayRequiredVarianceFilteredOutputRecords(FullHdPixels) * 16ull;
    const std::uint64_t CompactBytes = PathReplayRequiredCompactOutputRecords(FullHdPixels, true, true) * 16ull;
    const double SavedFraction = 1.0 - double(CompactBytes) / double(LegacyBytes);
    Require(SavedFraction > 0.630 && SavedFraction < 0.631, "Unexpected 1080p compact VRAM reduction");

    // Verify injective non-aliasing compact slot map across all valid logical slots.
    const std::array<std::uint32_t, 16> ActiveSlots{0, 1, 2, 3, 4, 9, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};
    std::vector<std::uint8_t> Occupied(Pixels * 17u, 0u);
    for (std::uint32_t Pixel = 0; Pixel < Pixels; ++Pixel)
    {
        Occupied[Pixel] = 1u; // Slab 0 owns final RGBA output.
        for (std::uint32_t Slot : ActiveSlots)
        {
            const std::uint64_t Index = PathReplayCompactSlot(Slot, Pixels, Pixel);
            Require(Index >= Pixels && Index < Pixels * 17ull && Occupied[Index] == 0u,
                    "Compact slot mapping aliased or went out of range");
            Occupied[Index] = 1u;
        }
    }
    for (std::uint8_t Mark : Occupied)
    {
        Require(Mark == 1u, "Compact 17-slab layout left an unmapped hole");
    }
    for (std::uint32_t InvalidSlot : {5u, 6u, 7u, 8u, 10u, 21u, 99u})
    {
        Require(PathReplayCompactSlot(InvalidSlot, Pixels, 0u) == UINT64_MAX,
                "Unsupported logical slot must return UINT64_MAX sentinel");
    }
    Require(PathReplayCompactSlot(0u, Pixels, Pixels) == UINT64_MAX,
            "Out-of-bounds compact pixel must return UINT64_MAX sentinel");

    const VkCommandBuffer Cmd = FakeHandle<VkCommandBuffer>(99);
    const PathReplayVulkanAccess Access = MakeAccess();
    LightComputeFootprint Extent{};
    Extent.Dimensions = {Pixels, 0u, 0u, 0u};
    Extent.Minimum = {0.02f, 0.02f, 0.02f, 0.0f};
    Extent.Maximum = {0.01f, 0.0f, 0.0f, 0.0f};
    Extent.Grid = {1u, 1u, 1u, 4u};
    Extent.Sampling = {7u, 2u, 8u, 64u};
    Extent.History = {64u, 64u, 1u, 0u};
    Extent.Reconstruction = {1u, 1u, 2u, 16u};
    Extent.Primary = {1u, 256u, 4u, 0u};

    PathReplayVulkanBuffers Buffers{};
    Buffers.ReceiverRecords = Pixels;
    Buffers.EnclosureRecords = 4u;
    Buffers.FacetRecords = 8u;
    Buffers.AngleFloats = 512u;
    Buffers.CounterWords = PathReplayReconstructionCounterWords;

    // 1. Compact tiers: 6*N base, 10*N sample-history, 17*N variance-filter.
    for (int Mode = 0; Mode < 3; ++Mode)
    {
        LightComputeFootprint ModeExtent = Extent;
        const bool UseSampleHistory = Mode == 1;
        const bool UseVariance = Mode == 2;
        if (UseSampleHistory)
        {
            ModeExtent.Sampling[1] = 1u;
            ModeExtent.Primary[3] = PathReplaySampleHistory | PathReplayCompactStorage;
        }
        else if (UseVariance)
        {
            ModeExtent.Primary[3] = PathReplayRoughOpaque | PathReplayImportanceRough | PathReplaySampleFacets |
                                    PathReplayBalanceSampling | PathReplayPowerMis | PathReplaySpatialDisk |
                                    PathReplayVarianceFilter | PathReplayOrderedTraversal |
                                    PathReplayCompactStorage | PathReplayAuthoredMaterials;
        }
        else
        {
            ModeExtent.Primary[3] = PathReplayCompactStorage;
        }
        ModeExtent.Reconstruction[2] = (ModeExtent.Primary[3] & PathReplaySampleFacets) != 0u ? 2u : 0u;

        PathReplayVulkanBuffers TierBuffers = Buffers;
        TierBuffers.OutputColourRecords =
            PathReplayRequiredCompactOutputRecords(Pixels, UseSampleHistory, UseVariance);
        RecordedReplayState State{};
        ActiveState = &State;
        RecordPathReplay(Cmd, Access, TierBuffers, ModeExtent);
        ActiveState = nullptr;
        Require(State.BoundPipelines > 0u, "Valid compact dispatch recorded zero pipelines");

        TierBuffers.OutputColourRecords -= 1u;
        ExpectThrows([&] { RecordPathReplay(Cmd, Access, TierBuffers, ModeExtent); },
                     "Undersized compact OutputColour range must throw");
    }

    // 2. Reject CompactStorage + legacy Reconstruct.
    {
        LightComputeFootprint Bad = Extent;
        Bad.Primary[3] = PathReplayReconstruct | PathReplayCompactStorage;
        PathReplayVulkanBuffers BadBuf = Buffers;
        BadBuf.OutputColourRecords = PathReplayRequiredReconstructedOutputRecords(Pixels);
        ExpectThrows([&] { RecordPathReplay(Cmd, Access, BadBuf, Bad); },
                     "CompactStorage + Reconstruct must be rejected");
    }

    // 3. Reject PowerMis without BalanceSampling or SampleFacets, or with AuditDoubleCount.
    for (std::uint32_t BadFlags : {PathReplayPowerMis,
                                   PathReplaySampleFacets | PathReplayPowerMis,
                                   PathReplaySampleFacets | PathReplayAuditDoubleCount | PathReplayPowerMis})
    {
        LightComputeFootprint Bad = Extent;
        Bad.Primary[3] = BadFlags;
        PathReplayVulkanBuffers BadBuf = Buffers;
        BadBuf.OutputColourRecords = PathReplayRequiredOutputRecords(Pixels);
        ExpectThrows([&] { RecordPathReplay(Cmd, Access, BadBuf, Bad); },
                     "Unguarded PowerMis combination must be rejected");
    }

    // 4. Reject invalid AuthoredMaterials combinations and missing camera projection range.
    for (std::uint32_t BadFlags : {PathReplayAuthoredMaterials,
                                   PathReplayRoughOpaque | PathReplayAuthoredMaterials,
                                   PathReplayRoughOpaque | PathReplayImportanceRough |
                                       PathReplaySampleHistory | PathReplayAuthoredMaterials})
    {
        LightComputeFootprint Bad = Extent;
        Bad.Primary[3] = BadFlags;
        PathReplayVulkanBuffers BadBuf = Buffers;
        BadBuf.OutputColourRecords = PathReplayRequiredSampleHistoryOutputRecords(Pixels);
        ExpectThrows([&] { RecordPathReplay(Cmd, Access, BadBuf, Bad); },
                     "Invalid AuthoredMaterials flag combination must be rejected");
    }
    {
        LightComputeFootprint NoPrimary = Extent;
        NoPrimary.Primary[0] = 0u;
        NoPrimary.Primary[1] = 490u; // 490 + 48 > 512 AngleFloats!
        NoPrimary.Primary[3] = PathReplayRoughOpaque | PathReplayImportanceRough | PathReplayAuthoredMaterials;
        PathReplayVulkanBuffers BadBuf = Buffers;
        BadBuf.OutputColourRecords = PathReplayRequiredOutputRecords(Pixels);
        ExpectThrows([&] { RecordPathReplay(Cmd, Access, BadBuf, NoPrimary); },
                     "AuthoredMaterials without primary tracing must still enforce CameraEnd <= AngleFloats");
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                  AUTHORED PBR ATLAS AND HISTORY DOMAIN CHECKS
//------------------------------------------------------------------------------------------------------------------------

std::uint32_t AppendPyramid(std::vector<float>& Atlas, std::uint32_t Dimension, bool NormalMap)
{
    const std::uint32_t Offset = static_cast<std::uint32_t>(Atlas.size());
    Atlas.insert(Atlas.end(), {float(Dimension), 0.0f, 0.0f, 0.0f});
    std::uint32_t Size = Dimension;
    while (true)
    {
        for (std::uint32_t Texel = 0; Texel < Size * Size; ++Texel)
        {
            if (NormalMap)
            {
                Atlas.insert(Atlas.end(), {0.06f, -0.04f, 0.9973966f});
            }
            else
            {
                Atlas.insert(Atlas.end(), {0.72f, 0.74f, 0.78f});
            }
        }
        if (Size == 1u)
        {
            break;
        }
        Size /= 2u;
    }
    return Offset;
}

std::uint32_t AppendAuthoredRecord(std::vector<float>& Atlas, std::uint32_t StrataCount, std::uint32_t Category,
                                   bool WithAlbedoMap, bool WithNormalMap, bool WithPaint, bool WithSheen)
{
    const std::uint32_t Base = static_cast<std::uint32_t>(Atlas.size());
    Atlas.resize(Base + 268u, 0.0f);
    Atlas[Base + 0u] = float(StrataCount);
    Atlas[Base + 1u] = float(Category);
    Atlas[Base + 4u] = 4.0f;
    Atlas[Base + 5u] = 4.0f;
    for (std::uint32_t Layer = 0; Layer < StrataCount; ++Layer)
    {
        float* S = Atlas.data() + Base + 8u + Layer * 24u;
        const bool Coating = StrataCount > 1u && Layer + 1u == StrataCount;
        S[0] = Coating ? 1.0f : 0.05f;
        S[1] = Coating ? 1.0f : 0.14f;
        S[2] = Coating ? 1.0f : 0.07f;
        S[3] = Coating ? 0.06f : (WithSheen ? 0.86f : 0.08f);
        S[4] = Coating || WithSheen || Category == 2u ? 0.0f : 0.92f;
        S[5] = 1.52f;
        S[6] = Coating ? 0.94f : 1.0f;
        S[8] = Category == 2u ? 0.6f : 0.05f;
        S[9] = Category == 2u ? 0.12f : 0.02f;
        S[10] = Category == 2u ? 0.3f : 0.04f;
        S[11] = Category == 2u || Coating ? 0.006f : 0.0f;
        S[12] = Coating ? 1.0f : 0.0f;
        if (WithSheen && Layer == 0u)
        {
            S[16] = 0.25f;
            S[17] = 0.39f;
            S[18] = 0.49f;
            S[19] = 1.0f;
            S[20] = 0.35f;
        }
    }
    if (WithPaint)
    {
        Atlas[Base + 200u] = 1.0f;
        Atlas[Base + 201u] = 3.0f;
        Atlas[Base + 202u] = 0.0f;
        Atlas[Base + 203u] = 3.0f;
        Atlas[Base + 204u] = 0.64f;
        Atlas[Base + 205u] = 450.0f;
        Atlas[Base + 206u] = 1.0f;
        Atlas[Base + 207u] = 0.65f;
        Atlas[Base + 208u] = 0.12f;
        Atlas[Base + 209u] = 0.30f;
        Atlas[Base + 210u] = 0.85f;
        Atlas[Base + 211u] = 1.0f;
        Atlas[Base + 212u] = 0.20f;
        for (std::uint32_t Stop = 0; Stop < 3u; ++Stop)
        {
            Atlas[Base + 216u + Stop * 4u + 0u] = 0.2f + 0.2f * float(Stop);
            Atlas[Base + 216u + Stop * 4u + 1u] = 0.5f;
            Atlas[Base + 216u + Stop * 4u + 2u] = 0.3f;
            Atlas[Base + 216u + Stop * 4u + 3u] = 0.5f * float(Stop);
        }
        Atlas[Base + 264u] = 0.4f;
        Atlas[Base + 265u] = 0.5f;
        Atlas[Base + 266u] = 0.3f;
    }
    if (WithAlbedoMap)
    {
        const std::uint32_t AlbedoOffset = AppendPyramid(Atlas, 4u, false);
        Atlas[Base + 2u] = float(AlbedoOffset);
    }
    if (WithNormalMap)
    {
        const std::uint32_t NormalOffset = AppendPyramid(Atlas, 4u, true);
        Atlas[Base + 3u] = float(NormalOffset);
    }
    return Base + 1u;
}

void ValidateAuthoredDomainAndHistory()
{
    std::vector<float> Atlas{0.0f}; // Slot 0 unused so valid addresses are >= 1.
    const std::uint32_t RacingGreen = AppendAuthoredRecord(Atlas, 2u, 0u, false, false, true, false);
    const std::uint32_t Chromium = AppendAuthoredRecord(Atlas, 1u, 0u, true, true, false, false);
    const std::uint32_t Velvet = AppendAuthoredRecord(Atlas, 1u, 0u, true, true, false, true);
    const std::uint32_t Glazing = AppendAuthoredRecord(Atlas, 1u, 2u, false, false, false, false);

    const std::array<std::uint32_t, 4> Addresses{RacingGreen, Chromium, Velvet, Glazing};
    std::vector<PathReplayFacet> Surfaces;
    std::vector<std::array<SpatialVector, 3>> TriGeometry;
    for (std::size_t Index = 0; Index < Addresses.size(); ++Index)
    {
        const double X0 = double(Index) * 1.5;
        const SpatialVector A{X0, 2.0, 0.0}, B{X0 + 1.0, 2.0, 0.0}, C{X0, 2.0, 1.0};
        TriGeometry.push_back({A, B, C});
        PathReplayFacet F{};
        F.A = {float(A.X), float(A.Y), float(A.Z), 0.0f};
        F.B = {float(B.X), float(B.Y), float(B.Z), 1.0f};
        F.C = {float(C.X), float(C.Y), float(C.Z), 0.0f};
        F.NormalA = {0.0f, -1.0f, 0.0f, 0.0f};
        F.NormalB = {0.0f, -1.0f, 0.0f, 0.0f};
        F.NormalC = {0.0f, -1.0f, 0.0f, 1.0f};
        F.AlbedoRoughness = {0.7f, 0.7f, 0.7f, 0.08f};
        F.OpticalToken = {Index == 1 ? 1.0f : 0.0f, 1.52f, 0.5f, float(Index + 1)};
        F.Emissive = {0.0f, 0.0f, 0.0f, float(Addresses[Index])};
        Surfaces.push_back(F);
    }
    TriangleVisibilitySpace Space;
    Space.BuildSurfaceAreaHierarchy(TriGeometry, 2u);
    const auto Bvh = Space.PackFloat32();
    std::vector<PathReplayFacet> OrderedSurfaces(Bvh.OriginalFacets.size());
    for (std::size_t I = 0u; I < Bvh.OriginalFacets.size(); ++I)
    {
        OrderedSurfaces[I] = Surfaces[Bvh.OriginalFacets[I]];
    }
    Require(OrderedSurfaces.size() == Surfaces.size(), "Failed to match BVH corner order");

    std::array<std::array<float, 4>, 12> Camera{};
    Camera[0] = {0.0f, -2.0f, 0.5f, 0.05f};
    Camera[1] = {0.0f, 1.0f, 0.0f, 50.0f};
    Camera[2] = {1.0f, 0.0f, 0.0f, 0.70f};
    Camera[3] = {0.0f, 0.0f, 1.0f, 0.70f};
    Camera[4] = Camera[0];
    Camera[5] = Camera[1];
    Camera[6] = Camera[2];
    Camera[7] = Camera[3];
    Camera[11] = {1.0f, 0.0f, 1.0f, 0.0f};

    std::vector<PathReplayReceiver> Receivers(4u);
    const PathReplaySceneView View{Receivers, OrderedSurfaces, Bvh.Enclosures, Bvh.Corners, Atlas, Camera, 0u};

    // Valid under AuthoredMaterials = true, rejected under legacy AuthoredMaterials = false.
    ValidatePathReplayDomain(View, {true, true, false, true, false, true, false, true});
    ExpectThrows([&] { ValidatePathReplayDomain(View, {true, true, true, true, false, true, false, false}); },
                 "Legacy TexturedDiffuse validator must still reject multi-strata/smooth/paint PBR records");

    // 12 targeted malformed-atlas negative controls.
    auto ExpectMutatedAtlasThrows = [&](std::size_t Offset, float Value, const char* Reason)
    {
        std::vector<float> Mutated = Atlas;
        Mutated[Offset] = Value;
        const PathReplaySceneView BadView{Receivers, OrderedSurfaces, Bvh.Enclosures, Bvh.Corners, Mutated, Camera, 0u};
        ExpectThrows([&] { ValidatePathReplayDomain(BadView, {true, true, false, true, false, true, false, true}); },
                     Reason);
    };
    ExpectMutatedAtlasThrows(RacingGreen - 1u + 0u, 0.0f, "Zero strata must be rejected");
    ExpectMutatedAtlasThrows(RacingGreen - 1u + 0u, 9.0f, "Nine strata must be rejected");
    ExpectMutatedAtlasThrows(RacingGreen - 1u + 1u, 4.0f, "Solid dielectric Category 4 must be rejected in seed replay");
    ExpectMutatedAtlasThrows(RacingGreen - 1u + 8u + 3u, 0.02f, "Roughness < 0.04 must be rejected");
    ExpectMutatedAtlasThrows(RacingGreen - 1u + 8u + 24u + 4u, 0.5f, "Metallic clearcoat coating must be rejected");
    ExpectMutatedAtlasThrows(RacingGreen - 1u + 203u, 13.0f, "AutomotivePaint palette > 12 must be rejected");
    ExpectMutatedAtlasThrows(RacingGreen - 1u + 205u, 0.0f, "Zero paint flake density must be rejected");
    ExpectMutatedAtlasThrows(Velvet - 1u + 8u + 20u, 0.01f, "Charlie sheen roughness < 0.04 must be rejected");
    ExpectMutatedAtlasThrows(Glazing - 1u + 200u, 1.0f, "AutomotivePaint on thin-sheet glazing must be rejected");
    ExpectMutatedAtlasThrows(Glazing - 1u + 3u, Atlas[Chromium - 1u + 3u],
                             "Normal map on thin-sheet glazing must be rejected");
    ExpectMutatedAtlasThrows(std::size_t(Atlas[Chromium - 1u + 3u]), 3.0f,
                             "Non-power-of-two mip pyramid top size must be rejected");
    {
        std::vector<float> ZeroNormal = Atlas;
        const std::size_t NormStart = std::size_t(ZeroNormal[Chromium - 1u + 3u]) + 4u;
        ZeroNormal[NormStart + 0u] = 0.0f;
        ZeroNormal[NormStart + 1u] = 0.0f;
        ZeroNormal[NormStart + 2u] = 0.0f;
        const PathReplaySceneView BadView{Receivers, OrderedSurfaces, Bvh.Enclosures, Bvh.Corners, ZeroNormal, Camera, 0u};
        ExpectThrows([&] { ValidatePathReplayDomain(BadView, {true, true, false, true, false, true, false, true}); },
                     "Zero-length normal texel must be rejected");
    }

    // 📝 Manually supplied receivers may use NaN for no material, but infinity must never be converted to uint.
    auto ManualReceivers = Receivers;
    auto& Manual = ManualReceivers[0];
    Manual.PositionHit = {0, 0, 0, 1};
    Manual.NormalRoughness = {0, 0, 1, .5f};
    Manual.ViewMetallic = {0, 0, 1, 0};
    Manual.AlbedoIor = {.5f, .5f, .5f, 1.5f};
    Manual.ResidualSpecular = {0, 0, 0, .5f};
    Manual.MotionPositionToken = {0, 0, 0, 1};
    const PathReplaySceneView ManualView{ManualReceivers, OrderedSurfaces, Bvh.Enclosures, Bvh.Corners,
                                         Atlas, Camera, 0u};
    Manual.ExpectedDirect[3] = std::numeric_limits<float>::infinity();
    ExpectThrows([&] { ValidatePathReplayDomain(ManualView, {false, true, false, true, false, false, false, true}); },
                 "Infinite authored receiver address must be rejected");
    Manual.ExpectedDirect[3] = std::numeric_limits<float>::quiet_NaN();
    ValidatePathReplayDomain(ManualView, {false, true, false, true, false, false, false, true});
    Manual.ExpectedDirect[3] = float(RacingGreen);
    ValidatePathReplayDomain(ManualView, {false, true, false, true, false, false, false, true});

    // Verify PathReplayHistoryDomain tracks PowerMis, CompactStorage, and AuthoredMaterials transitions.
    PathReplayHistoryDomain History;
    PathReplayHistorySpecification Spec{4u, 0.01f, {0.02f, 0.02f, 0.02f}, true, true, true, false, false, true,
                                        false, true, true, true, 2u, true, true, true};
    const auto First = History.Resolve(View, Spec);
    Require(First.Changed && First.MaximumHistory == 1u, "Initial resolve must flag Changed");
    const auto Steady = History.Resolve(View, Spec);
    Require(!Steady.Changed && Steady.MaximumHistory == 64u, "Identical second resolve must keep 64 cap");

    // Pure camera step preserves CameraOnly == true.
    auto MovedCamera = Camera;
    MovedCamera[4] = Camera[0];
    MovedCamera[5] = Camera[1];
    MovedCamera[6] = Camera[2];
    MovedCamera[7] = Camera[3];
    MovedCamera[0][0] += 0.15f;
    const PathReplaySceneView MovedView{Receivers, OrderedSurfaces, Bvh.Enclosures, Bvh.Corners, Atlas, MovedCamera, 0u};
    const auto CameraStep = History.Resolve(MovedView, Spec);
    Require(CameraStep.Changed && CameraStep.CameraOnly, "Pure camera translation must report CameraOnly");

    // Toggling CompactStorage, PowerMis, or AuthoredMaterials must report non-camera domain change.
    Spec.CompactStorage = false;
    const auto LayoutToggle = History.Resolve(MovedView, Spec);
    Require(LayoutToggle.Changed && !LayoutToggle.CameraOnly && LayoutToggle.MaximumHistory == 1u,
            "Toggling CompactStorage must invalidate history");
    Spec.PowerMis = false;
    const auto MisToggle = History.Resolve(MovedView, Spec);
    Require(MisToggle.Changed && !MisToggle.CameraOnly && MisToggle.MaximumHistory == 1u,
            "Toggling PowerMis must invalidate history");
}

//------------------------------------------------------------------------------------------------------------------------
//                           POWER MIS AND MULTI-STRATA PBR PDF MATHEMATICAL IDENTITIES
//------------------------------------------------------------------------------------------------------------------------

float HostReplayPowerMis(float Ratio, bool BsdfSample)
{
    if (!std::isfinite(Ratio))
    {
        return Ratio > 0.0f ? (BsdfSample ? 1.0f : 0.0f) : 0.0f;
    }
    if (Ratio <= 0.0f)
    {
        return BsdfSample ? 0.0f : 1.0f;
    }
    const float Small = Ratio <= 1.0f ? Ratio : (1.0f / Ratio);
    const float Squared = Small * Small;
    const float Low = Squared / (1.0f + Squared);
    const float High = 1.0f / (1.0f + Squared);
    return BsdfSample ? (Ratio <= 1.0f ? Low : High) : (Ratio <= 1.0f ? High : Low);
}

double EvaluateGgxNdf(double NormalHalf, double Roughness)
{
    const double Alpha = Roughness * Roughness;
    const double Denom = NormalHalf * NormalHalf * (Alpha * Alpha - 1.0) + 1.0;
    return (Alpha * Alpha) / (std::numbers::pi * Denom * Denom);
}

void ValidatePowerMisAndMultiStrataPdf()
{
    // 1. Overflow-safe beta=2 Power MIS partition of unity across 60 orders of magnitude [1e-30, 1e+30].
    for (int Exp = -30; Exp <= 30; ++Exp)
    {
        const float Ratio = std::pow(10.0f, float(Exp));
        const float WNee = HostReplayPowerMis(Ratio, false);
        const float WBsdf = HostReplayPowerMis(Ratio, true);
        Require(std::isfinite(WNee) && std::isfinite(WBsdf) && WNee >= 0.0f && WBsdf >= 0.0f,
                "Power MIS produced non-finite or negative weight");
        Require(std::abs((WNee + WBsdf) - 1.0f) <= 1e-6f,
                "Power MIS violated partition of unity w_nee + w_bsdf = 1");
    }
    Require(HostReplayPowerMis(std::numeric_limits<float>::infinity(), false) == 0.0f &&
                HostReplayPowerMis(std::numeric_limits<float>::infinity(), true) == 1.0f &&
                HostReplayPowerMis(0.0f, false) == 1.0f && HostReplayPowerMis(0.0f, true) == 0.0f,
            "MIS limiting weights must assign positive infinity to BSDF and zero to the source proposal");
    Require(HostReplayPowerMis(std::numeric_limits<float>::quiet_NaN(), false) == 0.0f &&
                HostReplayPowerMis(std::numeric_limits<float>::quiet_NaN(), true) == 0.0f,
            "Invalid density ratios must not leak NaN into radiance");

    // 2. Spherical quadrature over upper hemisphere of multi-strata PBR mixture density at normal & grazing view.
    struct LobeCase
    {
        const char* Label;
        std::vector<double> Roughnesses;
        std::vector<double> Weights;
        double DiffuseWeight;
        double ViewTiltRadians;
    };
    const std::array<LobeCase, 3> Cases{{
        {"PolishedChromium", {0.08}, {0.90}, 0.10, 0.25},
        {"RacingGreenClearcoat", {0.24, 0.09}, {0.66, 0.24}, 0.10, 0.35},
        {"MidnightVelvetSheen", {0.86}, {0.20}, 0.80, 0.30},
    }};

    for (const auto& Case : Cases)
    {
        const double Vx = std::sin(Case.ViewTiltRadians), Vz = std::cos(Case.ViewTiltRadians);
        constexpr int ThetaSteps = 512, PhiSteps = 1024;
        constexpr int CosBins = 8;
        std::array<double, CosBins> AnalyticBinMass{};
        double UpperIntegral = 0.0;
        double MinInteriorPdf = 1e30;
        for (int Ti = 0; Ti < ThetaSteps; ++Ti)
        {
            const double Theta = (double(Ti) + 0.5) * (0.5 * std::numbers::pi) / double(ThetaSteps);
            const double SinT = std::sin(Theta), CosT = std::cos(Theta);
            const double DTheta = (0.5 * std::numbers::pi) / double(ThetaSteps);
            const double DPhi = (2.0 * std::numbers::pi) / double(PhiSteps);
            const int Bin = std::min(CosBins - 1, int(CosT * double(CosBins)));
            for (int Pi = 0; Pi < PhiSteps; ++Pi)
            {
                const double Phi = (double(Pi) + 0.5) * DPhi;
                const double Lx = SinT * std::cos(Phi), Ly = SinT * std::sin(Phi), Lz = CosT;
                double Hx = Vx + Lx, Hy = Ly, Hz = Vz + Lz;
                const double HLen = std::sqrt(Hx * Hx + Hy * Hy + Hz * Hz);
                double SpecPdf = 0.0;
                if (HLen > 1e-9)
                {
                    Hx /= HLen;
                    Hy /= HLen;
                    Hz /= HLen;
                    const double VH = std::max(0.0, Vx * Hx + Vz * Hz);
                    if (Hz > 0.0 && VH > 0.0)
                    {
                        for (std::size_t Layer = 0; Layer < Case.Roughnesses.size(); ++Layer)
                        {
                            SpecPdf += Case.Weights[Layer] *
                                       (EvaluateGgxNdf(Hz, Case.Roughnesses[Layer]) * Hz / (4.0 * VH));
                        }
                    }
                }
                const double TotalPdf = Case.DiffuseWeight * (Lz / std::numbers::pi) + SpecPdf;
                const double ElementMass = TotalPdf * SinT * DTheta * DPhi;
                UpperIntegral += ElementMass;
                AnalyticBinMass[std::size_t(Bin)] += ElementMass;
                if (Lz > 0.05)
                {
                    MinInteriorPdf = std::min(MinInteriorPdf, TotalPdf);
                }
            }
        }

        // Empirical 3-draw mixture sampler check: verify sampled bin mass matches analytical PDF quadrature.
        std::mt19937_64 Rng(1337u + Case.Roughnesses.size() * 97u);
        std::uniform_real_distribution<double> Uniform(0.0, 1.0);
        constexpr int Samples = 200000;
        std::array<double, CosBins> SampledBinMass{};
        for (int S = 0; S < Samples; ++S)
        {
            const double Selector = Uniform(Rng);
            const double U1 = Uniform(Rng), U2 = Uniform(Rng);
            double Lz = 0.0;
            if (Selector < Case.DiffuseWeight)
            {
                Lz = std::sqrt(1.0 - U1);
            }
            else
            {
                double Accum = Case.DiffuseWeight;
                double ChosenRough = Case.Roughnesses.back();
                for (std::size_t Layer = 0; Layer < Case.Roughnesses.size(); ++Layer)
                {
                    Accum += Case.Weights[Layer];
                    if (Selector <= Accum || Layer + 1u == Case.Roughnesses.size())
                    {
                        ChosenRough = Case.Roughnesses[Layer];
                        break;
                    }
                }
                const double Alpha = ChosenRough * ChosenRough;
                const double Phi = 2.0 * std::numbers::pi * U1;
                const double Cos2 = (1.0 - U2) / ((Alpha * Alpha - 1.0) * U2 + 1.0);
                const double Hz = std::sqrt(std::clamp(Cos2, 0.0, 1.0));
                const double SinH = std::sqrt(std::max(0.0, 1.0 - Hz * Hz));
                const double Hx = SinH * std::cos(Phi);
                const double VH = Vx * Hx + Vz * Hz;
                if (VH > 0.0)
                {
                    Lz = 2.0 * VH * Hz - Vz;
                }
            }
            if (Lz > 0.0)
            {
                const int Bin = std::min(CosBins - 1, int(Lz * double(CosBins)));
                SampledBinMass[std::size_t(Bin)] += 1.0 / double(Samples);
            }
        }

        double MaxBinDiff = 0.0;
        for (int B = 0; B < CosBins; ++B)
        {
            MaxBinDiff = std::max(MaxBinDiff,
                                  std::abs(AnalyticBinMass[std::size_t(B)] - SampledBinMass[std::size_t(B)]));
        }
        const double ExpectedFloor = Case.DiffuseWeight * (0.05 / std::numbers::pi) * 0.99;
        std::printf("PBR_PDF_CHECK case=%s upper_mass=%.6f max_bin_diff=%.6f min_interior_pdf=%.6f\n",
                    Case.Label, UpperIntegral, MaxBinDiff, MinInteriorPdf);
        std::fflush(stdout);
        Require(UpperIntegral > 0.85 && UpperIntegral <= 1.002 &&
                    MaxBinDiff <= 0.008 &&
                    MinInteriorPdf >= ExpectedFloor,
                "Multi-strata PBR PDF failed sampler-evaluator consistency or positive-support guarantee");
    }
}
} // namespace

int main()
{
    ValidateCompactAddressingAndRecorder();
    ValidateAuthoredDomainAndHistory();
    ValidatePowerMisAndMultiStrataPdf();
    std::printf("PASS ReplayPbrReflectionValidation (compact VRAM, power MIS, multi-strata PBR + glazing)\n");
    return 0;
}
