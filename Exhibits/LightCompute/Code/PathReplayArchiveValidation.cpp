/*==========================================================================================================================================
                                             PATHREPLAYARCHIVEVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Confirm the shared replay footprint preserves the existing C++17 light-compute archive bytes.

#include "LightComputeFormat.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace
{
using namespace Frontier;

//------------------------------------------------------------------------------------------------------------------------
//                                                  LEGACY ARCHIVE FIXTURE
//------------------------------------------------------------------------------------------------------------------------

// 📝 This test-only footprint reconstructs the previous unaligned archive entry, not a second engine implementation.
struct PriorFacetFootprint
{
    std::array<float, 4> A{}, B{}, C{}, NormalA{}, NormalB{}, NormalC{}, AlbedoRoughness{}, OpticalToken{},
        Emissive{};
};
static_assert(sizeof(PriorFacetFootprint) == 144 && alignof(PriorFacetFootprint) == alignof(float));
static_assert(sizeof(PathReplayFacet) == sizeof(PriorFacetFootprint));
static_assert(offsetof(PathReplayFacet, NormalA) == offsetof(PriorFacetFootprint, NormalA));
static_assert(offsetof(PathReplayFacet, AlbedoRoughness) == offsetof(PriorFacetFootprint, AlbedoRoughness));
static_assert(offsetof(PathReplayFacet, Emissive) == offsetof(PriorFacetFootprint, Emissive));

template <class T> void AppendBytes(std::vector<char>& Bytes, const T& Input)
{
    const auto* First = reinterpret_cast<const char*>(&Input);
    Bytes.insert(Bytes.end(), First, First + sizeof(T));
}
template <class T> void AppendSequence(std::vector<char>& Bytes, const std::vector<T>& Sequence)
{
    AppendBytes(Bytes, uint32_t(Sequence.size()));
    for (const auto& Input : Sequence) AppendBytes(Bytes, Input);
}
void Require(bool Condition, const char* Message)
{
    if (!Condition) throw std::runtime_error(Message);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  BYTE AND LOAD VALIDATION
//------------------------------------------------------------------------------------------------------------------------

void ValidateArchiveFootprints(const char* Path)
{
    LightComputeArchive Archive;
    Archive.Width = 2;
    Archive.Height = 1;
    Archive.Receivers.resize(2);
    Archive.Receivers[0].PositionHit = {1, 2, 3, 1};
    Archive.Receivers[0].MotionPositionToken = {4, 5, 6, 7};
    Archive.Receivers[1].AlbedoIor = {.2f, .3f, .4f, 1.5f};
    Archive.Lights.resize(1);
    Archive.Lights[0].PositionReach = {8, 9, 10, 11};
    Archive.Visibility.Enclosures.resize(1);
    Archive.Visibility.Enclosures[0].Links = {0, 1, 1, 0};
    Archive.Visibility.Corners.resize(1);
    Archive.Visibility.Corners[0].A = {12, 13, 14, 15};
    Archive.Angles = {.1f, .2f, .3f};
    PriorFacetFootprint Prior;
    Prior.A = {1, 2, 3, 4}; Prior.B = {5, 6, 7, 8}; Prior.C = {9, 10, 11, 12};
    Prior.NormalA = {13, 14, 15, 16}; Prior.NormalB = {17, 18, 19, 20}; Prior.NormalC = {21, 22, 23, 24};
    Prior.AlbedoRoughness = {25, 26, 27, 28}; Prior.OpticalToken = {29, 30, 31, 32}; Prior.Emissive = {33, 34, 35, 36};
    Archive.Surfaces.resize(1);
    auto& Facet = Archive.Surfaces[0];
    Facet.A = Prior.A; Facet.B = Prior.B; Facet.C = Prior.C;
    Facet.NormalA = Prior.NormalA; Facet.NormalB = Prior.NormalB; Facet.NormalC = Prior.NormalC;
    Facet.AlbedoRoughness = Prior.AlbedoRoughness; Facet.OpticalToken = Prior.OpticalToken; Facet.Emissive = Prior.Emissive;
    Archive.Camera[11] = {37, 38, 39, 40};
    Require(std::memcmp(&Prior, &Facet, sizeof(Prior)) == 0, "Legacy facet bytes changed");
    std::vector<char> Expected;
    AppendBytes(Expected, uint32_t(0x33434c46));
    AppendBytes(Expected, Archive.Width); AppendBytes(Expected, Archive.Height);
    AppendSequence(Expected, Archive.Lights); AppendSequence(Expected, Archive.Receivers);
    AppendSequence(Expected, Archive.Visibility.Enclosures); AppendSequence(Expected, Archive.Visibility.Corners);
    AppendSequence(Expected, Archive.Angles);
    AppendSequence(Expected, std::vector<PriorFacetFootprint>{Prior});
    for (const auto& Projection : Archive.Camera) AppendBytes(Expected, Projection);
    Archive.Save(Path);
    std::ifstream Stream(Path, std::ios::binary);
    const std::vector<char> Actual{std::istreambuf_iterator<char>(Stream), std::istreambuf_iterator<char>()};
    Require(Actual == Expected, "Serialized archive differs from legacy footprint");
    const auto Reloaded = LightComputeArchive::Load(Path);
    Require(Reloaded.Width == 2 && Reloaded.Height == 1 && Reloaded.Surfaces.size() == 1 &&
                std::memcmp(&Reloaded.Surfaces[0], &Prior, sizeof(Prior)) == 0 && Reloaded.Angles == Archive.Angles &&
                Reloaded.Camera == Archive.Camera, "Archive load did not preserve the fixture");
    std::printf("ENGINE_REPLAY_ARCHIVE cxx17=1 legacy_bytes=%zu matches=1 load=1\n", Actual.size());
}
} // namespace

int main(int Count, char** Arguments)
{
    try
    {
        if (Count != 2) throw std::runtime_error("Expected a scratch archive path");
        ValidateArchiveFootprints(Arguments[1]);
        std::printf("PASS C++17 legacy archive footprint and round trip\n");
        return 0;
    }
    catch (const std::exception& Error)
    {
        std::fprintf(stderr, "FAIL replay archive: %s\n", Error.what());
        return 1;
    }
}
