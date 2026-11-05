/*==========================================================================================================================================
                                                            FACETARCHIVE.CPP
==========================================================================================================================================*/

#include "Frontier/Engine/Scene/FacetArchive.h"

#include <cstring>
#include <fstream>

namespace Frontier
{

namespace
{
constexpr size_t FixedRecordExtent = 48;   // [B] header ahead of the payload
constexpr uint32_t CornerNormalsPresent = 1;

uint32_t ReadU32(const uint8_t* At) noexcept
{
    uint32_t Value = 0;
    std::memcpy(&Value, At, 4);
    return Value;
}

float ReadF32(const uint8_t* At) noexcept
{
    float Value = 0.0f;
    std::memcpy(&Value, At, 4);
    return Value;
}
}

FacetArchiveData DecodeFacetArchive(const uint8_t* Bytes, size_t ByteCount)
{
    FacetArchiveData Data{};
    if (!Bytes || ByteCount < FixedRecordExtent)
        return Data;
    if (std::memcmp(Bytes, "FCT1", 4) != 0)
        return Data;

    const uint32_t VertexCount = ReadU32(Bytes + 4);
    const uint32_t FacetCount = ReadU32(Bytes + 8);
    const uint32_t Flags = ReadU32(Bytes + 12);
    size_t Cursor = FixedRecordExtent
        + static_cast<size_t>(VertexCount) * 12
        + static_cast<size_t>(FacetCount) * 12
        + static_cast<size_t>(FacetCount) * 4
        + ((Flags & CornerNormalsPresent) ? static_cast<size_t>(FacetCount) * 36 : 0);
    if (Cursor != ByteCount)
        return Data;

    Data.VertexCount = VertexCount;
    Data.FacetCount = FacetCount;
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        Data.BoundsMinimum[Axis] = ReadF32(Bytes + 16 + Axis * 4);
        Data.BoundsMaximum[Axis] = ReadF32(Bytes + 28 + Axis * 4);
    }
    size_t At = FixedRecordExtent;
    Data.Positions.resize(static_cast<size_t>(VertexCount) * 3);
    std::memcpy(Data.Positions.data(), Bytes + At, Data.Positions.size() * 4);
    At += Data.Positions.size() * 4;
    Data.Facets.resize(static_cast<size_t>(FacetCount) * 3);
    std::memcpy(Data.Facets.data(), Bytes + At, Data.Facets.size() * 4);
    At += Data.Facets.size() * 4;
    Data.Families.resize(FacetCount);
    std::memcpy(Data.Families.data(), Bytes + At, Data.Families.size() * 4);
    At += Data.Families.size() * 4;
    if (Flags & CornerNormalsPresent)
    {
        Data.CornerNormals.resize(static_cast<size_t>(FacetCount) * 9);
        std::memcpy(Data.CornerNormals.data(), Bytes + At, Data.CornerNormals.size() * 4);
    }
    return Data;
}

FacetArchiveData LoadFacetArchive(const std::string& Path)
{
    std::ifstream Stream(Path, std::ios::binary | std::ios::ate);
    if (!Stream)
        return {};
    const std::streamsize Size = Stream.tellg();
    if (Size <= 0)
        return {};
    std::vector<uint8_t> Bytes(static_cast<size_t>(Size));
    Stream.seekg(0);
    Stream.read(reinterpret_cast<char*>(Bytes.data()), Size);
    if (!Stream)
        return {};
    return DecodeFacetArchive(Bytes.data(), Bytes.size());
}

} // namespace Frontier
