/*============================================================================================================================================
                                                     FACETPROFILEGPUVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Validate runtime-sized proposal profiles against production native packet arithmetic.
#include "FacetProfileGpuComparisons.h"

//------------------------------------------------------------------------------------------------------------------------
//                                                    IMAGE INDEXING
//------------------------------------------------------------------------------------------------------------------------

namespace
{
constexpr uint32_t ProfilePixels = 1024;
constexpr std::array<uint32_t, 6> ProfileSizes{1, 3, 7, 16, 31, 65};
bool ProfileLayoutAccepted(
    uint32_t Support,
    uint32_t Cameras,
    uint32_t Start,
    uint32_t Capacity,
    uint32_t Stride,
    uint32_t Facets)
{
    return Support > 0 && Support <= 16777216u && Support <= Facets && Cameras < Stride &&
           Start == Cameras + 1 && Start <= Stride && Capacity == (Support + 3) / 4 &&
           Capacity == Stride - Start;
}

LightComputeArchive ConstructProfileScene(uint32_t Fixture, uint32_t Support)
{
    auto Scene = ConstructDeformedAreaScene(Fixture);
    while (Scene.Surfaces.size() < Support)
    {
        uint32_t Largest = 0;
        for (uint32_t Index = 1; Index < Scene.Surfaces.size(); ++Index)
            if (ContinuousFacetArea(Scene.Surfaces[Index]) > ContinuousFacetArea(Scene.Surfaces[Largest]))
                Largest = Index;
        const auto Facet = Scene.Surfaces[Largest];
        const std::array<CausalQuartet, 3> Points{Facet.A, Facet.B, Facet.C};
        const std::array<CausalQuartet, 3> Normals{Facet.NormalA, Facet.NormalB, Facet.NormalC};
        uint32_t Edge = 0;
        double Longest = -1;
        for (uint32_t Side = 0; Side < 3; ++Side)
        {
            double Squared = 0;
            for (uint32_t Axis = 0; Axis < 3; ++Axis)
                Squared += std::pow(double(Points[Side][Axis]) - Points[(Side + 1) % 3][Axis], 2);
            if (Squared > Longest)
            {
                Longest = Squared;
                Edge = Side;
            }
        }
        CausalQuartet Midpoint{}, Midnormal{};
        for (uint32_t Axis = 0; Axis < 4; ++Axis)
        {
            Midpoint[Axis] = (Points[Edge][Axis] + Points[(Edge + 1) % 3][Axis]) * .5f;
            Midnormal[Axis] = (Normals[Edge][Axis] + Normals[(Edge + 1) % 3][Axis]) * .5f;
        }
        auto First = Facet, Second = Facet;
        First.A = Points[Edge];
        First.B = Midpoint;
        First.C = Points[(Edge + 2) % 3];
        First.NormalA = Normals[Edge];
        First.NormalB = Midnormal;
        First.NormalC = Normals[(Edge + 2) % 3];
        Second.A = Midpoint;
        Second.B = Points[(Edge + 1) % 3];
        Second.C = Points[(Edge + 2) % 3];
        Second.NormalA = Midnormal;
        Second.NormalB = Normals[(Edge + 1) % 3];
        Second.NormalC = Normals[(Edge + 2) % 3];
        Scene.Surfaces[Largest] = First;
        Scene.Surfaces.push_back(Second);
    }
    return Scene;
}

void ValidateProfileUnits()
{
    for (uint32_t Support : ProfileSizes)
        Require(ProfileLayoutAccepted(Support, 1024, 1025, (Support + 3) / 4, 1025 + (Support + 3) / 4, 65),
                "Valid profile layout rejected");
    Require(!ProfileLayoutAccepted(0, 1024, 1025, 4, 1029, 16), "Empty profile accepted");
    Require(!ProfileLayoutAccepted(17, 1024, 1025, 4, 1029, 16), "Excessive profile accepted");
    Require(!ProfileLayoutAccepted(16, 1024, 1025, 3, 1029, 16), "Truncated profile accepted");
    const auto Large = ConstructProfileScene(0, 65), Medium = ConstructProfileScene(0, 31);
    Require(Large.Surfaces.size() == 65, "Large split count differs");
    Require(Medium.Surfaces.size() == 31, "Medium split count differs");
    double OriginalArea = 0, SplitArea = 0;
    for (const auto &Facet : ConstructDeformedAreaScene(0).Surfaces)
        OriginalArea += ContinuousFacetArea(Facet);
    for (const auto &Facet : Large.Surfaces)
        SplitArea += ContinuousFacetArea(Facet);
    Require(std::abs(OriginalArea - SplitArea) < 1e-5 * std::max(1., OriginalArea), "Split area changed");
    std::cout << "PROFILE_GPU_UNIT checks=12 pass=1\n";
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    IMAGE VALIDATION
//------------------------------------------------------------------------------------------------------------------------

bool ValidateProfiles(const char *Driver, const std::string &Shaders, const std::string &Directory)
{
    std::filesystem::create_directories(Directory);
    std::ofstream Gpu(Directory + "/FacetProfileGpu.f32", std::ios::binary);
    std::ofstream Native(Directory + "/FacetProfileNative.f32", std::ios::binary);
    std::ofstream Valid(Directory + "/FacetProfileValid.u8", std::ios::binary);
    std::ofstream First(Directory + "/FacetProfileFirstFailure.txt");
    Gpu.exceptions(std::ios::badbit | std::ios::failbit);
    Native.exceptions(std::ios::badbit | std::ios::failbit);
    Valid.exceptions(std::ios::badbit | std::ios::failbit);
    First.exceptions(std::ios::badbit | std::ios::failbit);
    uint64_t Failures = 0, NativeFailures = 0, TailFailures = 0;
    std::ofstream Rejections(Directory + "/FacetProfileRejections.f32", std::ios::binary);
    Rejections.exceptions(std::ios::badbit | std::ios::failbit);
    uint64_t Rejected = 0, RejectionFailures = 0, TailQuartets = 0;
    for (uint32_t Fixture = 0; Fixture < 3; ++Fixture)
        for (uint32_t Support : ProfileSizes)
        {
            auto Scene = ConstructProfileScene(Fixture, Support);
            const auto Original = Scene.Surfaces;
            std::vector<uint32_t> SourceOrdinals(Support);
            std::vector<FacetAreaTriangle> Triangles;
            const auto Shading = CausalShading(Scene);
            for (uint32_t Ordinal = 0; Ordinal < Support; ++Ordinal)
            {
                SourceOrdinals[Ordinal] = Support == 16 ? Ordinal : Support - 1 - Ordinal;
                Triangles.push_back(Shading[SourceOrdinals[Ordinal]].Triangle);
            }
            FacetAreaProposals Proposals;
            Require(Proposals.Prepare(Triangles), "Profile proposal support rejected");
            std::vector<std::array<SpatialVector, 3>> Corners;
            for (const auto &Facet : Original)
                Corners.push_back({SpatialVector{Facet.A[0], Facet.A[1], Facet.A[2]},
                                   SpatialVector{Facet.B[0], Facet.B[1], Facet.B[2]},
                                   SpatialVector{Facet.C[0], Facet.C[1], Facet.C[2]}});
            TriangleVisibilitySpace Reference;
            Reference.Build(Corners);
            Scene.Visibility = Reference.PackFloat32();
            Scene.Surfaces.clear();
            std::vector<uint32_t> OriginalToPacked(Original.size()), Profile(Support);
            for (uint32_t Packed = 0; Packed < Original.size(); ++Packed)
            {
                const auto Ordinal = Scene.Visibility.OriginalFacets[Packed];
                Scene.Surfaces.push_back(Original[Ordinal]);
                OriginalToPacked[Ordinal] = Packed;
            }
            for (uint32_t Ordinal = 0; Ordinal < Support; ++Ordinal)
                Profile[Ordinal] = OriginalToPacked[SourceOrdinals[Ordinal]];
            const uint32_t ProfileStart = ProfilePixels + 1, Capacity = (Support + 3) / 4;
            const uint32_t ProfileStride = ProfileStart + Capacity;
            Require(ProfileLayoutAccepted(Support, ProfilePixels, ProfileStart, Capacity, ProfileStride,
                                          uint32_t(Scene.Surfaces.size())),
                    "Host profile layout rejected");
            Scene.Width = ProfileStride;
            Scene.Height = 1;
            Scene.Receivers.resize(ProfileStride);
            Scene.Receivers[0].PositionHit = {
                EncodeCausalIdentity(Support), EncodeCausalIdentity(ProfilePixels),
                EncodeCausalIdentity(ProfileStart), EncodeCausalIdentity(Capacity)};
            for (uint32_t Index = ProfileStart; Index < ProfileStride; ++Index)
                Scene.Receivers[Index].PositionHit.fill(EncodeCausalIdentity(UINT32_MAX));
            for (uint32_t Ordinal = 0; Ordinal < Support; ++Ordinal)
                Scene.Receivers[ProfileStart + Ordinal / 4].PositionHit[Ordinal % 4] =
                    EncodeCausalIdentity(Profile[Ordinal]);
            LightComputeSubmission Compute(Driver);
            Compute.Prepare(Scene, Shaders, false, false, true, false, false, false, false, 0, 80);
            if (Fixture == 0 && Support == 1)
                std::cout << "PROFILE_GPU_DEVICE name=" << Compute.Properties.deviceName
                          << " type=" << Compute.Properties.deviceType << " driver_sanitized=0 leaks=0\n";
            for (uint32_t Method = 0; Method < 6; ++Method)
                for (uint32_t Sample = 0; Sample < 1; ++Sample)
                {
                    for (uint32_t Pixel = 0; Pixel < ProfilePixels; ++Pixel)
                    {
                        auto &Camera = Scene.Receivers[1 + Pixel];
                        Camera = ConstructPerspectiveAreaRay(Pixel, Fixture);
                        const uint32_t Seed = HashFacetAreaBits(
                            (Sample * ProfilePixels + Pixel + 0x5e92b341u) ^ (Fixture * 0x6c8e9cf5u));
                        Camera.PositionHit = {EncodeCausalIdentity(Seed), EncodeCausalIdentity(Method), 0, 0};
                    }
                    const CausalQuartet Sentinel{-12345, -12345, -12345, -12345};
                    auto *Storage = static_cast<CausalQuartet *>(Compute.Buffers[6].Address);
                    std::fill(Storage, Storage + 80 * ProfileStride, Sentinel);
                    Compute.ExecuteDiffuse(Scene, 1, 17, false, false, true);
                    uint32_t ImageFailures = 0, ImageNativeFailures = 0, ImageTailFailures = 0;
                    for (uint32_t Slab = 0; Slab < 80; ++Slab)
                        for (uint32_t Pixel = ProfilePixels; Pixel < ProfileStride; ++Pixel)
                        {
                            ++TailQuartets;
                            if (Storage[size_t(Slab) * ProfileStride + Pixel] != Sentinel)
                                ++ImageTailFailures;
                        }
                    for (uint32_t Pixel = 0; Pixel < ProfilePixels; ++Pixel)
                    {
                        std::array<CausalQuartet, 80> Actual{};
                        for (uint32_t Slab = 0; Slab < 80; ++Slab)
                            Actual[Slab] = Storage[size_t(Slab) * ProfileStride + Pixel];
                        std::vector<CausalQuartet> Expected(80);
                        uint8_t Accepted = 1;
                        std::string Diagnostic;
                        try
                        {
                            auto Single = Scene;
                            Single.Receivers[0] = Scene.Receivers[1 + Pixel];
                            Expected = AssemblePacketReference(
                                Single, Profile, Proposals, Reference,
                                DecodeCausalIdentity(Single.Receivers[0].PositionHit[0]), Method, 0);
                        }
                        catch (const std::exception &Error)
                        {
                            Accepted = 0;
                            Diagnostic = Error.what();
                            ++ImageNativeFailures;
                        }
                        if (Accepted)
                            try
                            {
                                ComparePacketReadback(Actual.data(), Expected.data());
                                for (uint32_t Slab = 0; Slab < 80; ++Slab)
                                    for (uint32_t Axis = 0; Axis < 4; ++Axis)
                                        Require(std::isfinite(Actual[Slab][Axis]),
                                                "Nonfinite image diagnostic");
                                for (uint32_t Slab = 0; Slab < 80; ++Slab)
                                    if (Slab < 7 || (Slab >= 20 && Slab < 44) || (Slab >= 61 && Slab < 80))
                                        Require(Actual[Slab][3] == 0,
                                                "Nonzero reserved diagnostic component");
                                for (uint32_t Slot = uint32_t(Expected[60][1]); Slot < 8; ++Slot)
                                    for (uint32_t Start : {20u, 28u, 36u, 44u, 52u, 61u, 69u})
                                        Require(Actual[Start + Slot] == CausalQuartet{},
                                                "Unused trace slot differs");
                                for (uint32_t Slab = 77; Slab < 80; ++Slab)
                                    Require(Actual[Slab] == Expected[Slab], "Reserved image slabs differ");
                            }
                            catch (const std::exception &Error)
                            {
                                Diagnostic = Error.what();
                            }
                        if (!Diagnostic.empty())
                        {
                            if (Failures + ImageFailures == 0)
                                First << "fixture=" << Fixture << " support=" << Support
                                      << " method=" << Method << " sample=" << Sample << " pixel=" << Pixel
                                      << " diagnostic=" << Diagnostic << '\n';
                            ++ImageFailures;
                        }
                        Gpu.write(reinterpret_cast<const char *>(Actual.data()), 80 * 16);
                        Native.write(reinterpret_cast<const char *>(Expected.data()), 80 * 16);
                        Valid.write(reinterpret_cast<const char *>(&Accepted), 1);
                    }
                    Failures += ImageFailures;
                    NativeFailures += ImageNativeFailures;
                    TailFailures += ImageTailFailures;
                    std::cout << "PROFILE_GPU_IMAGE fixture=" << Fixture << " support=" << Support
                              << " method=" << Method << " sample=" << Sample
                              << " pixels=1024 failures=" << ImageFailures
                              << " native_failures=" << ImageNativeFailures
                              << " tail_failures=" << ImageTailFailures << '\n';
                }
            if (Fixture == 0 && Support == 16)
            {
                const std::array<uint32_t, 6> Codes{1, 1, 1, 2, 2, 3};
                for (uint32_t Mutation = 0; Mutation < Codes.size(); ++Mutation)
                {
                    auto Bad = Scene;
                    if (Mutation == 0)
                        Bad.Receivers[0].PositionHit[0] = EncodeCausalIdentity(0);
                    if (Mutation == 1)
                        Bad.Receivers[0].PositionHit[0] = EncodeCausalIdentity(17);
                    if (Mutation == 2)
                        Bad.Receivers[0].PositionHit[3] = EncodeCausalIdentity(3);
                    if (Mutation == 3)
                        Bad.Receivers[ProfileStart].PositionHit[0] = EncodeCausalIdentity(UINT32_MAX);
                    if (Mutation == 4)
                        Bad.Receivers[ProfileStart].PositionHit[1] =
                            Bad.Receivers[ProfileStart].PositionHit[0];
                    if (Mutation == 5)
                    {
                        auto &Facet = Bad.Surfaces[Profile[0]];
                        Facet.B = Facet.C = Facet.A;
                    }
                    const CausalQuartet Sentinel{-12345, -12345, -12345, -12345};
                    auto *Storage = static_cast<CausalQuartet *>(Compute.Buffers[6].Address);
                    std::fill(Storage, Storage + 80 * ProfileStride, Sentinel);
                    Compute.ExecuteDiffuse(Bad, 1, 17, false, false, true);
                    uint32_t BadPixels = 0;
                    for (uint32_t Pixel = 0; Pixel < ProfilePixels; ++Pixel)
                    {
                        bool Accepted = true;
                        for (uint32_t Slab = 0; Slab < 80; ++Slab)
                        {
                            const auto Actual = Storage[size_t(Slab) * ProfileStride + Pixel];
                            const CausalQuartet Expected =
                                Slab == 79 ? CausalQuartet{float(Codes[Mutation]), 0, 0, 0} : CausalQuartet{};
                            Accepted &= Actual == Expected;
                            Rejections.write(reinterpret_cast<const char *>(Actual.data()), 16);
                        }
                        if (Accepted)
                            ++Rejected;
                        else
                        {
                            ++BadPixels;
                            ++RejectionFailures;
                        }
                    }
                    for (uint32_t Slab = 0; Slab < 80; ++Slab)
                        for (uint32_t Pixel = ProfilePixels; Pixel < ProfileStride; ++Pixel)
                        {
                            ++TailQuartets;
                            if (Storage[size_t(Slab) * ProfileStride + Pixel] != Sentinel)
                                ++TailFailures;
                        }
                    std::cout << "PROFILE_GPU_REJECTION case=" << Mutation << " expected=" << Codes[Mutation]
                              << " pixels=1024 failures=" << BadPixels << '\n';
                }
            }
        }
    if (Failures == 0)
        First << "No packet comparison failure.\n";
    std::cout << std::setprecision(17)
              << "PROFILE_GPU_TOTAL images=108 packets=110592 candidates=" << PacketDraws
              << " paths=" << PacketPaths << " pairs=" << PacketPairs << " visibility=" << 2 * PacketPaths
              << " failures=" << Failures << " native_failures=" << NativeFailures
              << " tail_failures=" << TailFailures << " rgb_error=" << PacketRgbError
              << " coordinate_error=" << PacketCoordinateError << " scalar_error=" << PacketScalarError
              << " rejected=" << Rejected << " rejection_failures=" << RejectionFailures
              << " tail_quartets=" << TailQuartets
              << " pass=" << (Failures + TailFailures + RejectionFailures == 0 && Rejected == 6144) << '\n';
    return Failures + TailFailures + RejectionFailures == 0 && Rejected == 6144;
}
} // namespace

int main(int Count, char **Arguments)
{
    try
    {
        std::cout << std::unitbuf;
        if (Count == 2 && std::strcmp(Arguments[1], "--unit") == 0)
        {
            ValidateProfileUnits();
            return 0;
        }
        Require(Count == 4, "Use driver shaders output-directory or --unit");
        return ValidateProfiles(Arguments[1], Arguments[2], Arguments[3]) ? 0 : 1;
    }
    catch (const std::exception &Error)
    {
        std::cerr << Error.what() << '\n';
        return 2;
    }
}
