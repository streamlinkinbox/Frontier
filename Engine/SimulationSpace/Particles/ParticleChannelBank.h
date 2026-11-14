/*============================================================================================================================================
                                                          PARTICLECHANNELBANK.H
============================================================================================================================================*/
// 🧩 Modular typed Structure-of-Arrays (SoA) particle channel bank supporting native 16B GPU channels and custom user C++ attribute types

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Frontier::SimulationSpace::Particles
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ChannelStoragePolicy : uint8_t
{
    DenseSwapRemove = 0u, // [-] - O(1) swap-with-last retirement keeping [0 .. ActiveCount) tightly packed
    FixedSlotOrder  = 1u, // [-] - Preserves deterministic slot indices across ticks (strands, grids, surfaces)
};

enum class ChannelResidency : uint8_t
{
    CpuOnly   = 0u, // [-] - Pure C++ gameplay or custom user attribute channel
    DeviceMirrored = 1u, // [-] - 16B-aligned trivially-copyable channel eligible for Vulkan SSBO mirroring
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  CONSTEXPR SIGNATURE HASH
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] consteval uint64_t HashChannelSignature(std::string_view ChannelName) noexcept
{
    uint64_t Hash = 14695981039346656037ull;
    for (const char Ch : ChannelName)
    {
        Hash ^= static_cast<uint64_t>(static_cast<unsigned char>(Ch));
        Hash *= 1099511628211ull;
    }
    return Hash;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

template <typename AttributeT>
    requires std::is_trivially_copyable_v<AttributeT> && std::is_default_constructible_v<AttributeT>
struct ChannelTag
{
    using AttributeType = AttributeT;

    uint64_t         SignatureHash = 0u;                          // [hash] - Unique FNV-1a 64-bit channel signature
    const char*      DebugLabel    = "";                          // [-]    - Human-readable channel name
    ChannelResidency Residency     = ChannelResidency::DeviceMirrored; // [-]    - CPU-only or GPU-mirrorable residency
};

struct ChannelDescriptorSummary
{
    uint64_t         SignatureHash = 0u;                         // [hash]  - Unique channel identifier
    std::string      DebugLabel    {};                           // [-]     - Channel label
    uint32_t         StrideBytes   = 0u;                         // [B]     - Per-particle attribute stride
    uint32_t         AlignBytes    = 0u;                         // [B]     - Required alignment
    ChannelResidency Residency     = ChannelResidency::CpuOnly;  // [-]     - Residency policy
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   PARTICLE CHANNEL BANK
//------------------------------------------------------------------------------------------------------------------------

class ParticleChannelBank
{
public:
    ParticleChannelBank() = default;
    ~ParticleChannelBank();

    ParticleChannelBank(const ParticleChannelBank&)            = delete;
    ParticleChannelBank& operator=(const ParticleChannelBank&) = delete;
    ParticleChannelBank(ParticleChannelBank&& Other) noexcept;
    ParticleChannelBank& operator=(ParticleChannelBank&& Other) noexcept;

    void ConfigureCapacity(uint32_t MaxCapacity, ChannelStoragePolicy Policy = ChannelStoragePolicy::DenseSwapRemove);
    void ClearActiveSlots() noexcept;
    void ResetAllChannels() noexcept;

    // Allocates `RequestedCount` slots; returns actual count allocated and writes starting index to `OutBeginSlot`.
    [[nodiscard]] uint32_t AcquireSlots(uint32_t RequestedCount, uint32_t& OutBeginSlot) noexcept;

    // Reclaims `SlotIndex`. Under DenseSwapRemove, swaps last active slot into `SlotIndex` across ALL channels.
    void ReclaimSlot(uint32_t SlotIndex) noexcept;

    // Sets ActiveCount explicitly (useful for FixedSlotOrder deterministic grids & strands).
    void AssignActiveCount(uint32_t Count) noexcept;

    // Idempotently registers & allocates a typed channel (native or user-defined C++ struct).
    template <typename AttributeT>
        requires std::is_trivially_copyable_v<AttributeT> && std::is_default_constructible_v<AttributeT>
    std::span<AttributeT> RequireChannel(const ChannelTag<AttributeT>& Tag)
    {
        ChannelColumn& Column = RequireRawColumn(
            Tag.SignatureHash,
            Tag.DebugLabel,
            static_cast<uint32_t>(sizeof(AttributeT)),
            static_cast<uint32_t>(alignof(AttributeT)),
            Tag.Residency,
            &DefaultConstructSlot<AttributeT>);
        return { reinterpret_cast<AttributeT*>(Column.AlignedBytes), Capacity };
    }

    template <typename AttributeT>
        requires std::is_trivially_copyable_v<AttributeT> && std::is_default_constructible_v<AttributeT>
    [[nodiscard]] bool VerifyChannelPresent(const ChannelTag<AttributeT>& Tag) const noexcept
    {
        return FindColumnIndex(Tag.SignatureHash, static_cast<uint32_t>(sizeof(AttributeT))) >= 0;
    }

    template <typename AttributeT>
        requires std::is_trivially_copyable_v<AttributeT> && std::is_default_constructible_v<AttributeT>
    [[nodiscard]] std::span<AttributeT> AccessChannel(const ChannelTag<AttributeT>& Tag) noexcept
    {
        const int32_t Index = FindColumnIndex(Tag.SignatureHash, static_cast<uint32_t>(sizeof(AttributeT)));
        if (Index < 0 || Capacity == 0u)
        {
            return {};
        }
        return { reinterpret_cast<AttributeT*>(Columns[static_cast<std::size_t>(Index)].AlignedBytes), Capacity };
    }

    template <typename AttributeT>
        requires std::is_trivially_copyable_v<AttributeT> && std::is_default_constructible_v<AttributeT>
    [[nodiscard]] std::span<const AttributeT> QueryChannel(const ChannelTag<AttributeT>& Tag) const noexcept
    {
        const int32_t Index = FindColumnIndex(Tag.SignatureHash, static_cast<uint32_t>(sizeof(AttributeT)));
        if (Index < 0 || Capacity == 0u)
        {
            return {};
        }
        return { reinterpret_cast<const AttributeT*>(Columns[static_cast<std::size_t>(Index)].AlignedBytes), Capacity };
    }

    [[nodiscard]] std::span<std::byte>            AccessRawChannelBytes(uint64_t SignatureHash) noexcept;
    [[nodiscard]] std::span<const std::byte>      QueryRawChannelBytes(uint64_t SignatureHash) const noexcept;
    [[nodiscard]] std::vector<ChannelDescriptorSummary> QueryChannelSummaries() const;

    [[nodiscard]] uint32_t             QueryCapacity() const noexcept { return Capacity; }
    [[nodiscard]] uint32_t             QueryActiveCount() const noexcept { return ActiveCount; }
    [[nodiscard]] ChannelStoragePolicy QueryStoragePolicy() const noexcept { return StoragePolicy; }
    [[nodiscard]] uint32_t             QueryRegisteredChannelCount() const noexcept { return static_cast<uint32_t>(Columns.size()); }
    [[nodiscard]] std::size_t          QueryOccupiedMemoryBytes() const noexcept;
    [[nodiscard]] bool                 QueryChannelsGpuResident() const noexcept;

private:
    using ResetSlotCallback = void (*)(std::byte* SlotAddress) noexcept;

    struct ChannelColumn
    {
        uint64_t          SignatureHash  = 0u;
        std::string       DebugLabel     {};
        uint32_t          StrideBytes    = 0u;
        uint32_t          AlignBytes     = 16u;
        ChannelResidency  Residency      = ChannelResidency::CpuOnly;
        ResetSlotCallback ResetSlot      = nullptr;
        std::byte*        AlignedBytes   = nullptr;
        std::size_t       AllocatedBytes = 0u;
    };

    template <typename AttributeT>
    static void DefaultConstructSlot(std::byte* SlotAddress) noexcept
    {
        const AttributeT DefaultSample{};
        std::byte*       DestinationBytes = SlotAddress;
        const std::byte* SourceBytes      = reinterpret_cast<const std::byte*>(&DefaultSample);
        for (std::size_t ByteOffset = 0u; ByteOffset < sizeof(AttributeT); ++ByteOffset)
        {
            DestinationBytes[ByteOffset] = SourceBytes[ByteOffset];
        }
    }

    ChannelColumn& RequireRawColumn(
        uint64_t          SignatureHash,
        const char*       DebugLabel,
        uint32_t          StrideBytes,
        uint32_t          AlignBytes,
        ChannelResidency  Residency,
        ResetSlotCallback ResetCallback);

    [[nodiscard]] int32_t FindColumnIndex(uint64_t SignatureHash, uint32_t ExpectedStrideBytes) const noexcept;
    void                  AllocateColumnStorage(ChannelColumn& Column, uint32_t SlotCapacity);
    void                  ReleaseAllColumnStorage() noexcept;

    uint32_t                   Capacity      = 0u;
    uint32_t                   ActiveCount   = 0u;
    ChannelStoragePolicy       StoragePolicy = ChannelStoragePolicy::DenseSwapRemove;
    std::vector<ChannelColumn> Columns       {};
};

} // namespace Frontier::SimulationSpace::Particles
