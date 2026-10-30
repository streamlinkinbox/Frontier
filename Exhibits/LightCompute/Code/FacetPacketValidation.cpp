/*============================================================================================================================================
                                                      FACETPACKETVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Exercise the opt-in packet caller,transactional support intake and allocation-free fresh captures.
#include "Engine/Rendering/FacetAreaPacket.h"
#include <cstdlib>
#include <new>
static bool AllocationAuditEnabled = false;
static uint64_t AllocationCount    = 0;
void *operator new(std::size_t Bytes)
{
    if (AllocationAuditEnabled)
        ++AllocationCount;
    if (void *Address = std::malloc(Bytes ? Bytes : 1))
        return Address;
    throw std::bad_alloc();
}
void operator delete(void *Address) noexcept
{
    std::free(Address);
}
void operator delete(void *Address, std::size_t) noexcept
{
    std::free(Address);
}
void *operator new[](std::size_t Bytes)
{
    return ::operator new(Bytes);
}
void operator delete[](void *Address) noexcept
{
    ::operator delete(Address);
}
void operator delete[](void *Address, std::size_t) noexcept
{
    ::operator delete(Address);
}
#define main NativeOcclusionEntry
#include "FacetAreaOcclusionValidation.cpp"
#undef main

namespace
{
std::vector<uint32_t> SourceProfile(const OcclusionScene &Scene)
{
    return {Scene.SourceIndices[0], Scene.SourceIndices[1]};
}
void ValidatePacketUnits()
{
    const auto Scene  = ConstructOcclusionScene(0, 0);
    const auto Packed = Scene.DoublePrecisionBvh.PackFloat32();
    FacetAreaPacket Packet;
    uint32_t Count = 0;
    auto Confirm   = [&](bool Accepted)
    {
        ++Count;
        if (!Accepted)
            throw std::runtime_error("Packet unit failed " + std::to_string(Count));
    };
    Confirm(!Packet.Evaluate({0, 0, 1.5f}, {0, 0, 1}, 11).Accepted);
    Confirm(Packet.Prepare(Scene.Surfaces, Packed, SourceProfile(Scene)));
    const auto Baseline = Packet.Evaluate({0, 0, 1.5f}, {0, 0, 1}, 11);
    Confirm(Baseline.Accepted && Baseline.Rgb[0] > 0);
    Confirm(!Packet.Prepare(Scene.Surfaces, Packed, {}));
    Confirm(!Packet.Prepare(Scene.Surfaces, Packed, {UINT32_MAX}));
    Confirm(!Packet.Prepare(Scene.Surfaces, Packed, {Scene.SourceIndices[0], Scene.SourceIndices[0]}));
    Confirm(!Packet.Prepare({}, Packed, {0}));
    Confirm(!Packet.Prepare(Scene.Surfaces, {}, SourceProfile(Scene)));
    Confirm(Packet.Evaluate({0, 0, 1.5f}, {0, 0, 1}, 11).Rgb == Baseline.Rgb);
    Confirm(!Packet.Evaluate({0, 0, 1.5f}, {0, 0, 0}, 11).Accepted);
    Confirm(!Packet.Evaluate({NAN, 0, 1.5f}, {0, 0, 1}, 11).Accepted);
    Confirm(!Packet.Evaluate({0, 0, 1.5f}, {0, INFINITY, 1}, 11).Accepted);
    Confirm(!Packet.Evaluate({10, 10, 1.5f}, {0, 0, 1}, 11).Accepted);
    Confirm(!Packet.Evaluate({0, 0, 1.5f}, {0, 0, 1}, 11, FacetPacketEstimator(3)).Accepted);
    Confirm(
        !Packet.Evaluate({0, 0, 1.5f}, {0, 0, 1}, 11, FacetPacketEstimator::Ris, FacetAreaMeasureCategory(2)).Accepted);
    for (uint32_t Method = 0; Method < 3; ++Method)
    {
        const auto Result = Packet.Evaluate({0, 0, 1.5f}, {0, 0, 1}, 11, FacetPacketEstimator(Method));
        Confirm(Result.Accepted && Result.CandidateCount == (Method == 1 ? 4u : 8u) &&
                Result.PathCount == (Method == 2 ? 8u : 4u) && Result.VisibilityCount == 2 * Result.PathCount);
    }
    auto MutableSurfaces = Scene.Surfaces;
    Confirm(Packet.Prepare(MutableSurfaces, Packed, SourceProfile(Scene)));
    MutableSurfaces.clear();
    Confirm(Packet.Evaluate({0, 0, 1.5f}, {0, 0, 1}, 11).Rgb == Baseline.Rgb);
    auto Invalid                                  = Scene.Surfaces;
    Invalid[Scene.SourceIndices[0]].Triangle.A[0] = NAN;
    Confirm(!Packet.Prepare(Invalid, Packed, SourceProfile(Scene)));
    Confirm(Packet.Evaluate({0, 0, 1.5f}, {0, 0, 1}, 11).Rgb == Baseline.Rgb);
    auto Excessive = Scene.Surfaces;
    for (auto &Surface : Excessive)
        Surface.Albedo.fill(std::numeric_limits<float>::max());
    Confirm(Packet.Prepare(Excessive, Packed, SourceProfile(Scene)));
    const auto Rejected = Packet.Evaluate({0, 0, 1.5f}, {0, 0, 1}, 11);
    Confirm(!Rejected.Accepted && Rejected.Rgb == RayVector{} && Rejected.OmitBoth == RayVector{} &&
            Rejected.CandidateCount == 0 && Rejected.PathCount == 0);
    auto Black = Scene.Surfaces;
    for (auto &Surface : Black)
        Surface.Albedo = {0, 0, 0};
    Confirm(Packet.Prepare(Black, Packed, SourceProfile(Scene)));
    const auto BlackResult = Packet.Evaluate({0, 0, 1.5f}, {0, 0, 1}, 11);
    Confirm(BlackResult.Accepted && BlackResult.Rgb == RayVector{} && BlackResult.OmitBoth == RayVector{});
    const auto Blocked = ConstructOcclusionScene(0, 3);
    Confirm(Packet.Prepare(Blocked.Surfaces, Blocked.DoublePrecisionBvh.PackFloat32(), SourceProfile(Blocked)));
    Confirm(Packet.Evaluate({0, 0, 1.5f}, {0, 0, 1}, 11).Rgb == RayVector{});
    // Unequal source areas exercise both profile measures and packed ordinal remapping.
    auto Unequal   = Scene.Surfaces;
    auto &Triangle = Unequal[Scene.SourceIndices[0]].Triangle;
    for (uint32_t Axis = 0; Axis < 3; ++Axis)
        Triangle.B[Axis] = Triangle.A[Axis] + .5f * (Triangle.B[Axis] - Triangle.A[Axis]);
    std::vector<std::array<SpatialVector, 3>> Corners;
    for (const auto &Surface : Unequal)
        Corners.push_back({ConvertSpatialVector(Surface.Triangle.A), ConvertSpatialVector(Surface.Triangle.B),
                           ConvertSpatialVector(Surface.Triangle.C)});
    TriangleVisibilitySpace Space;
    Space.Build(Corners);
    const auto Atlas = Space.PackFloat32();
    std::vector<FacetAreaShadingTriangle> Reordered;
    std::vector<uint32_t> Profile;
    for (uint32_t Index = 0; Index < Atlas.OriginalFacets.size(); ++Index)
    {
        const auto Original = Atlas.OriginalFacets[Index];
        Reordered.push_back(Unequal[Original]);
        if (Original == Scene.SourceIndices[0] || Original == Scene.SourceIndices[1])
            Profile.push_back(Index);
    }
    Confirm(Packet.Prepare(Reordered, Atlas, Profile));
    const auto Uniform = Packet.Evaluate({0, 0, 1.5f}, {0, 0, 1}, 11);
    const auto Area    = Packet.Evaluate({0, 0, 1.5f}, {0, 0, 1}, 11, FacetPacketEstimator::Ris,
                                         FacetAreaMeasureCategory::ProportionalArea);
    Confirm(Uniform.Accepted && Area.Accepted && Uniform.Rgb != Area.Rgb);
    const auto Before      = AllocationCount;
    AllocationAuditEnabled = true;
    bool Accepted          = true;
    for (uint32_t Seed = 0; Seed < 1024; ++Seed)
        for (uint32_t Method = 0; Method < 3; ++Method)
            for (uint32_t Measure = 0; Measure < 2; ++Measure)
                Accepted = Accepted && Packet
                                           .Evaluate({0, 0, 1.5f}, {0, 0, 1}, Seed, FacetPacketEstimator(Method),
                                                     FacetAreaMeasureCategory(Measure))
                                           .Accepted;
    AllocationAuditEnabled = false;
    Confirm(Accepted && AllocationCount == Before);
    std::cout << "FACET_PACKET_UNIT checks=" << Count << " allocation_calls=" << AllocationCount - Before
              << " evaluations=6144 pass=1\n";
}

void EncodePacketCaptures(const std::string &Directory)
{
    constexpr uint32_t Count = 12288;
    uint64_t Evaluations = 0, Draws = 0, Traced = 0;
    std::filesystem::create_directories(Directory);
    for (uint32_t Layout = 0; Layout < 3; ++Layout)
        for (uint32_t Variant = 0; Variant < 4; ++Variant)
        {
            const auto Scene = ConstructOcclusionScene(Layout, Variant);
            FacetAreaPacket Packet;
            VerifyOcclusion(
                Packet.Prepare(Scene.Surfaces, Scene.DoublePrecisionBvh.PackFloat32(), SourceProfile(Scene)));
            std::vector<Quartet> Output(5 * Count);
            for (uint32_t Pixel = 0; Pixel < Count; ++Pixel)
            {
                const uint32_t Method = Pixel / 4096, Sample = Pixel % 4096, ImagePixel = Sample % 1024;
                const auto Camera =
                    TransformWorldPoint(Layouts[Layout], -.2f + .4f * (float(ImagePixel % 32) + .5f) / 32,
                                        -.2f + .4f * (float(ImagePixel / 32) + .5f) / 32, 1.5f);
                const uint32_t Seed = HashFacetAreaBits((Sample + 0x82c4d719u) ^ (Layout * 0x6c8e9cf5u));
                const auto Uniform  = Packet.Evaluate(Camera, {0, 0, 1}, Seed, FacetPacketEstimator(Method));
                const auto Area     = Packet.Evaluate(Camera, {0, 0, 1}, Seed, FacetPacketEstimator(Method),
                                                      FacetAreaMeasureCategory::ProportionalArea);
                VerifyOcclusion(Uniform.Accepted && Area.Accepted);
                VerifyOcclusion(Uniform.Rgb == Area.Rgb && Uniform.OmitConnection == Area.OmitConnection &&
                                Uniform.OmitSuffix == Area.OmitSuffix && Uniform.OmitBoth == Area.OmitBoth);
                const std::array<RayVector, 5> Slabs{Uniform.Rgb, Uniform.OmitConnection, Uniform.OmitSuffix,
                                                     Uniform.OmitBoth, Uniform.Primary.Position};
                for (uint32_t Slab = 0; Slab < 5; ++Slab)
                    for (uint32_t Axis = 0; Axis < 3; ++Axis)
                        Output[Slab * Count + Pixel][Axis] = Slabs[Slab][Axis];
                Evaluations += 2;
                Draws += Uniform.CandidateCount + Area.CandidateCount;
                Traced += Uniform.PathCount + Area.PathCount;
            }
            std::ofstream Capture(Directory + "/FacetPacket" + std::to_string(Layout) + std::to_string(Variant) +
                                      ".f32",
                                  std::ios::binary);
            Capture.exceptions(std::ios::failbit | std::ios::badbit);
            Capture.write(reinterpret_cast<const char *>(Output.data()), Output.size() * sizeof(Quartet));
            std::cout << "FACET_PACKET_CASE layout=" << Layout << " variant=" << Variant << " pass=1\n";
        }
    std::cout << "FACET_PACKET_TOTAL evaluations=" << Evaluations << " candidates=" << Draws << " paths=" << Traced
              << " pass=1\n";
}
} // namespace
int main(int Count, char **Arguments)
{
    try
    {
        if (Count != 2)
            throw std::runtime_error("Use --unit or output-directory");
        if (std::strcmp(Arguments[1], "--unit") == 0)
            ValidatePacketUnits();
        else
            EncodePacketCaptures(Arguments[1]);
        return 0;
    }
    catch (const std::exception &Error)
    {
        std::cerr << Error.what() << '\n';
        return 1;
    }
}
