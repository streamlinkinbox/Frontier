//============================================================================================================================================
// 📦 Frontier/DeviceExchange/ByteSpace.cpp — Monotonic Transient Storage Implementation
//============================================================================================================================================

#include "ByteSpace.h"
#include <cstdlib>
#include <algorithm>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

ByteSpace::ByteSpace(size_t CapacityInBytes) noexcept
    : StorageHead(nullptr)
    , TotalCapacity(CapacityInBytes)
    , OccupiedBytes(0)
    , PeakOccupiedBytes(0)
{
    if (TotalCapacity > 0)
    {
        StorageHead = static_cast<uint8_t*>(std::malloc(TotalCapacity));
    }
}

ByteSpace::~ByteSpace() noexcept
{
    if (StorageHead)
    {
        std::free(StorageHead);
        StorageHead = nullptr;
    }
}

ByteSpace::ByteSpace(ByteSpace&& Other) noexcept
    : StorageHead(Other.StorageHead)
    , TotalCapacity(Other.TotalCapacity)
    , OccupiedBytes(Other.OccupiedBytes)
    , PeakOccupiedBytes(Other.PeakOccupiedBytes)
{
    Other.StorageHead = nullptr;
    Other.TotalCapacity = 0;
    Other.OccupiedBytes = 0;
    Other.PeakOccupiedBytes = 0;
}

ByteSpace& ByteSpace::operator=(ByteSpace&& Other) noexcept
{
    if (this != &Other)
    {
        if (StorageHead)
        {
            std::free(StorageHead);
        }
        StorageHead = Other.StorageHead;
        TotalCapacity = Other.TotalCapacity;
        OccupiedBytes = Other.OccupiedBytes;
        PeakOccupiedBytes = Other.PeakOccupiedBytes;

        Other.StorageHead = nullptr;
        Other.TotalCapacity = 0;
        Other.OccupiedBytes = 0;
        Other.PeakOccupiedBytes = 0;
    }
    return *this;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                EXTENT OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

void* ByteSpace::AcquireSpan(size_t SizeInBytes, size_t AlignmentInBytes) noexcept
{
    if (!StorageHead || SizeInBytes == 0)
    {
        return nullptr;
    }

    size_t CurrentAddress = reinterpret_cast<size_t>(StorageHead + OccupiedBytes);
    size_t Misalignment = CurrentAddress % AlignmentInBytes;
    size_t Padding = (Misalignment == 0) ? 0 : (AlignmentInBytes - Misalignment);

    size_t DesiredBytes = OccupiedBytes + Padding + SizeInBytes;
    if (DesiredBytes > TotalCapacity)
    {
        return nullptr;
    }

    uint8_t* ResultLocation = StorageHead + OccupiedBytes + Padding;
    OccupiedBytes = DesiredBytes;
    PeakOccupiedBytes = std::max(PeakOccupiedBytes, OccupiedBytes);

    return ResultLocation;
}

void ByteSpace::ReclaimAll() noexcept
{
    OccupiedBytes = 0;
}

size_t ByteSpace::QueryOccupiedBytes() const noexcept
{
    return OccupiedBytes;
}

size_t ByteSpace::QueryTotalCapacity() const noexcept
{
    return TotalCapacity;
}

float ByteSpace::QueryOccupancyRatio() const noexcept
{
    if (TotalCapacity == 0)
    {
        return 0.0f;
    }
    return static_cast<float>(OccupiedBytes) / static_cast<float>(TotalCapacity);
}

} // namespace Frontier
