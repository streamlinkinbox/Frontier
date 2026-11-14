/*============================================================================================================================================
                                                         PARTICLECHANNELBANK.CPP
============================================================================================================================================*/
// 🧩 Implementation of modular 64-byte-aligned Structure-of-Arrays ParticleChannelBank with synchronized channel spawning & retirement

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Particles/ParticleChannelBank.h"

#include <algorithm>
#include <cstring>
#include <new>

namespace Frontier::SimulationSpace::Particles
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

inline constexpr std::size_t MinimumColumnAlignmentBytes = 64u; // [B] - Cache-line alignment for SIMD & GPU staging

[[nodiscard]] std::size_t ComputeEffectiveAlignment(uint32_t RequestedAlign) noexcept
{
    std::size_t Align = MinimumColumnAlignmentBytes;
    while (Align < static_cast<std::size_t>(RequestedAlign))
    {
        Align <<= 1u;
    }
    return Align;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     LIFECYCLE & MOVE
//------------------------------------------------------------------------------------------------------------------------

ParticleChannelBank::~ParticleChannelBank()
{
    ReleaseAllColumnStorage();
}

ParticleChannelBank::ParticleChannelBank(ParticleChannelBank&& Other) noexcept
    : Capacity(Other.Capacity)
    , ActiveCount(Other.ActiveCount)
    , StoragePolicy(Other.StoragePolicy)
    , Columns(std::move(Other.Columns))
{
    Other.Capacity    = 0u;
    Other.ActiveCount = 0u;
}

ParticleChannelBank& ParticleChannelBank::operator=(ParticleChannelBank&& Other) noexcept
{
    if (this != &Other)
    {
        ReleaseAllColumnStorage();
        Capacity      = Other.Capacity;
        ActiveCount   = Other.ActiveCount;
        StoragePolicy = Other.StoragePolicy;
        Columns       = std::move(Other.Columns);

        Other.Capacity    = 0u;
        Other.ActiveCount = 0u;
    }
    return *this;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 CAPACITY & SLOT LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

void ParticleChannelBank::ConfigureCapacity(uint32_t MaxCapacity, ChannelStoragePolicy Policy)
{
    StoragePolicy = Policy;
    if (MaxCapacity == Capacity)
    {
        ActiveCount = std::min(ActiveCount, Capacity);
        return;
    }

    const uint32_t PreviousCapacity = Capacity;
    const uint32_t PreservedCount   = std::min(ActiveCount, MaxCapacity);
    Capacity                        = MaxCapacity;
    ActiveCount                     = PreservedCount;

    for (ChannelColumn& Column : Columns)
    {
        std::byte* const OldBytes  = Column.AlignedBytes;
        const std::size_t OldAlign = ComputeEffectiveAlignment(Column.AlignBytes);

        Column.AlignedBytes   = nullptr;
        Column.AllocatedBytes = 0u;

        AllocateColumnStorage(Column, Capacity);

        if (OldBytes != nullptr && Column.AlignedBytes != nullptr && PreservedCount > 0u && PreviousCapacity > 0u)
        {
            const std::size_t CopyBytes = static_cast<std::size_t>(PreservedCount) * static_cast<std::size_t>(Column.StrideBytes);
            std::memcpy(Column.AlignedBytes, OldBytes, CopyBytes);
        }

        if (OldBytes != nullptr)
        {
            ::operator delete[](OldBytes, std::align_val_t{ OldAlign });
        }
    }
}

void ParticleChannelBank::ClearActiveSlots() noexcept
{
    ActiveCount = 0u;
}

void ParticleChannelBank::ResetAllChannels() noexcept
{
    for (ChannelColumn& Column : Columns)
    {
        if (Column.AlignedBytes == nullptr || Capacity == 0u)
        {
            continue;
        }
        if (Column.ResetSlot != nullptr)
        {
            for (uint32_t Slot = 0u; Slot < Capacity; ++Slot)
            {
                Column.ResetSlot(Column.AlignedBytes + static_cast<std::size_t>(Slot) * Column.StrideBytes);
            }
        }
        else
        {
            std::memset(Column.AlignedBytes, 0, Column.AllocatedBytes);
        }
    }
    ActiveCount = 0u;
}

uint32_t ParticleChannelBank::SpawnSlots(uint32_t RequestedCount, uint32_t& OutBeginSlot) noexcept
{
    OutBeginSlot = ActiveCount;
    if (RequestedCount == 0u || ActiveCount >= Capacity)
    {
        return 0u;
    }

    const uint32_t Available = Capacity - ActiveCount;
    const uint32_t Spawned   = std::min(RequestedCount, Available);

    for (ChannelColumn& Column : Columns)
    {
        if (Column.AlignedBytes == nullptr)
        {
            continue;
        }
        for (uint32_t Offset = 0u; Offset < Spawned; ++Offset)
        {
            const std::size_t ByteOffset = static_cast<std::size_t>(OutBeginSlot + Offset) * Column.StrideBytes;
            if (Column.ResetSlot != nullptr)
            {
                Column.ResetSlot(Column.AlignedBytes + ByteOffset);
            }
            else
            {
                std::memset(Column.AlignedBytes + ByteOffset, 0, Column.StrideBytes);
            }
        }
    }

    ActiveCount += Spawned;
    return Spawned;
}

void ParticleChannelBank::RetireSlot(uint32_t SlotIndex) noexcept
{
    if (SlotIndex >= ActiveCount)
    {
        return;
    }

    if (StoragePolicy == ChannelStoragePolicy::FixedSlotOrder)
    {
        for (ChannelColumn& Column : Columns)
        {
            if (Column.AlignedBytes == nullptr)
            {
                continue;
            }
            std::byte* Target = Column.AlignedBytes + static_cast<std::size_t>(SlotIndex) * Column.StrideBytes;
            if (Column.ResetSlot != nullptr)
            {
                Column.ResetSlot(Target);
            }
            else
            {
                std::memset(Target, 0, Column.StrideBytes);
            }
        }
        return;
    }

    const uint32_t LastSlot = ActiveCount - 1u;
    if (SlotIndex != LastSlot)
    {
        for (ChannelColumn& Column : Columns)
        {
            if (Column.AlignedBytes == nullptr)
            {
                continue;
            }
            std::byte* Dst       = Column.AlignedBytes + static_cast<std::size_t>(SlotIndex) * Column.StrideBytes;
            const std::byte* Src = Column.AlignedBytes + static_cast<std::size_t>(LastSlot) * Column.StrideBytes;
            std::memcpy(Dst, Src, Column.StrideBytes);
        }
    }
    --ActiveCount;
}

void ParticleChannelBank::AssignActiveCount(uint32_t Count) noexcept
{
    ActiveCount = std::min(Count, Capacity);
}

//------------------------------------------------------------------------------------------------------------------------
//                                              RAW BYTE ACCESS & INTROSPECTION
//------------------------------------------------------------------------------------------------------------------------

std::span<std::byte> ParticleChannelBank::AccessRawChannelBytes(uint64_t SignatureHash) noexcept
{
    for (ChannelColumn& Column : Columns)
    {
        if (Column.SignatureHash == SignatureHash && Column.AlignedBytes != nullptr)
        {
            return { Column.AlignedBytes, Column.AllocatedBytes };
        }
    }
    return {};
}

std::span<const std::byte> ParticleChannelBank::QueryRawChannelBytes(uint64_t SignatureHash) const noexcept
{
    for (const ChannelColumn& Column : Columns)
    {
        if (Column.SignatureHash == SignatureHash && Column.AlignedBytes != nullptr)
        {
            return { Column.AlignedBytes, Column.AllocatedBytes };
        }
    }
    return {};
}

std::vector<ChannelDescriptorSummary> ParticleChannelBank::QueryChannelSummaries() const
{
    std::vector<ChannelDescriptorSummary> Summaries{};
    Summaries.reserve(Columns.size());
    for (const ChannelColumn& Column : Columns)
    {
        Summaries.push_back(ChannelDescriptorSummary{
            Column.SignatureHash,
            Column.DebugLabel,
            Column.StrideBytes,
            Column.AlignBytes,
            Column.Residency,
        });
    }
    return Summaries;
}

std::size_t ParticleChannelBank::QueryOccupiedMemoryBytes() const noexcept
{
    std::size_t TotalBytes = 0u;
    for (const ChannelColumn& Column : Columns)
    {
        TotalBytes += Column.AllocatedBytes;
    }
    return TotalBytes;
}

bool ParticleChannelBank::QueryAllChannelsGpuResident() const noexcept
{
    for (const ChannelColumn& Column : Columns)
    {
        if (Column.Residency != ChannelResidency::CpuAndGpu)
        {
            return false;
        }
    }
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  INTERNAL COLUMN MANAGEMENT
//------------------------------------------------------------------------------------------------------------------------

ParticleChannelBank::ChannelColumn& ParticleChannelBank::RequireRawColumn(
    uint64_t         SignatureHash,
    const char*      DebugLabel,
    uint32_t         StrideBytes,
    uint32_t         AlignBytes,
    ChannelResidency Residency,
    ResetSlotFn      ResetCallback)
{
    const int32_t Existing = FindColumnIndex(SignatureHash, StrideBytes);
    if (Existing >= 0)
    {
        return Columns[static_cast<std::size_t>(Existing)];
    }

    ChannelColumn Fresh{};
    Fresh.SignatureHash = SignatureHash;
    Fresh.DebugLabel    = (DebugLabel != nullptr) ? DebugLabel : "";
    Fresh.StrideBytes   = StrideBytes;
    Fresh.AlignBytes    = AlignBytes;
    Fresh.Residency     = Residency;
    Fresh.ResetSlot     = ResetCallback;

    AllocateColumnStorage(Fresh, Capacity);
    Columns.push_back(std::move(Fresh));
    return Columns.back();
}

int32_t ParticleChannelBank::FindColumnIndex(uint64_t SignatureHash, uint32_t ExpectedStrideBytes) const noexcept
{
    for (std::size_t Index = 0u; Index < Columns.size(); ++Index)
    {
        if (Columns[Index].SignatureHash == SignatureHash && Columns[Index].StrideBytes == ExpectedStrideBytes)
        {
            return static_cast<int32_t>(Index);
        }
    }
    return -1;
}

void ParticleChannelBank::AllocateColumnStorage(ChannelColumn& Column, uint32_t SlotCapacity)
{
    if (SlotCapacity == 0u || Column.StrideBytes == 0u)
    {
        Column.AlignedBytes   = nullptr;
        Column.AllocatedBytes = 0u;
        return;
    }

    const std::size_t EffectiveAlign = ComputeEffectiveAlignment(Column.AlignBytes);
    const std::size_t TotalBytes     = static_cast<std::size_t>(SlotCapacity) * static_cast<std::size_t>(Column.StrideBytes);
    std::byte* Buffer                = static_cast<std::byte*>(::operator new[](TotalBytes, std::align_val_t{ EffectiveAlign }));

    if (Column.ResetSlot != nullptr)
    {
        for (uint32_t Slot = 0u; Slot < SlotCapacity; ++Slot)
        {
            Column.ResetSlot(Buffer + static_cast<std::size_t>(Slot) * Column.StrideBytes);
        }
    }
    else
    {
        std::memset(Buffer, 0, TotalBytes);
    }

    Column.AlignedBytes   = Buffer;
    Column.AllocatedBytes = TotalBytes;
}

void ParticleChannelBank::ReleaseAllColumnStorage() noexcept
{
    for (ChannelColumn& Column : Columns)
    {
        if (Column.AlignedBytes != nullptr)
        {
            const std::size_t EffectiveAlign = ComputeEffectiveAlignment(Column.AlignBytes);
            ::operator delete[](Column.AlignedBytes, std::align_val_t{ EffectiveAlign });
            Column.AlignedBytes   = nullptr;
            Column.AllocatedBytes = 0u;
        }
    }
    Columns.clear();
}

} // namespace Frontier::SimulationSpace::Particles
