//============================================================================================================================================
// 📦 Frontier/DeviceExchange/ByteSpace.h — Monotonic Transient Storage Extent with Completion-Ordered Reset
//============================================================================================================================================

#pragma once

#include <cstdint>
#include <cstddef>
#include <concepts>
#include <span>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                    BYTE SPACE
//------------------------------------------------------------------------------------------------------------------------

class ByteSpace
{
public:
    explicit ByteSpace(size_t CapacityInBytes) noexcept;
    ~ByteSpace() noexcept;

    ByteSpace(const ByteSpace&) = delete;
    ByteSpace& operator=(const ByteSpace&) = delete;
    ByteSpace(ByteSpace&& Other) noexcept;
    ByteSpace& operator=(ByteSpace&& Other) noexcept;

    [[nodiscard]] void*     AcquireSpan(size_t SizeInBytes, size_t AlignmentInBytes = 16) noexcept;

    template<typename StructureType>
    [[nodiscard]] StructureType* Construct(auto&&... Arguments) noexcept
    {
        void* RawLocation = AcquireSpan(sizeof(StructureType), alignof(StructureType));
        if (!RawLocation)
        {
            return nullptr;
        }
        return ::new (RawLocation) StructureType(static_cast<decltype(Arguments)>(Arguments)...);
    }

    template<typename StructureType>
    [[nodiscard]] std::span<StructureType> AcquireSequence(size_t Count) noexcept
    {
        void* RawLocation = AcquireSpan(sizeof(StructureType) * Count, alignof(StructureType));
        if (!RawLocation)
        {
            return {};
        }
        return std::span<StructureType>(reinterpret_cast<StructureType*>(RawLocation), Count);
    }

    void                    ReclaimAll() noexcept;

    [[nodiscard]] size_t    QueryOccupiedBytes() const noexcept;
    [[nodiscard]] size_t    QueryTotalCapacity() const noexcept;
    [[nodiscard]] float     QueryOccupancyRatio() const noexcept;

    // Single unified conversion operator for occupancy inspection
    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

private:
    uint8_t*                StorageHead;                        // [ptr] root pointer of raw storage extent
    size_t                  TotalCapacity;                      // [B] maximum capacity in bytes
    size_t                  OccupiedBytes;                      // [B] current offset in bytes
    size_t                  PeakOccupiedBytes;                  // [B] high-water mark in bytes
};

template<>
inline size_t ByteSpace::Convert<size_t>() const noexcept
{
    return OccupiedBytes;
}

template<>
inline float ByteSpace::Convert<float>() const noexcept
{
    return QueryOccupancyRatio();
}

} // namespace Frontier
