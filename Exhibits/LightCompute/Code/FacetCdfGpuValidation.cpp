/*============================================================================================================================================
                                                      FACETCDFGPUVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Validate GPU-prepared area/CDF reuse,paired uncached work and conservative profile invalidation.
#include "FacetCdfGpuComparisons.h"
#include <bit>

//------------------------------------------------------------------------------------------------------------------------
//                                                    PROFILE INPUTS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
struct FacetCdfScene
{
    LightComputeArchive Scene;
    std::vector<uint32_t> Profile;
    FacetAreaProposals Proposals;
    TriangleVisibilitySpace Reference;
};

void RepackCdfScene(FacetCdfScene &Domain)
{
    auto &Scene = Domain.Scene;
    const auto Original = Scene.Surfaces;
    std::vector<std::array<SpatialVector, 3>> Corners;
    for (const auto &Facet : Original)
        Corners.push_back({SpatialVector{Facet.A[0], Facet.A[1], Facet.A[2]},
                           SpatialVector{Facet.B[0], Facet.B[1], Facet.B[2]},
                           SpatialVector{Facet.C[0], Facet.C[1], Facet.C[2]}});
    Domain.Reference.Build(Corners);
    Scene.Visibility = Domain.Reference.PackFloat32();
    Scene.Surfaces.clear();
    std::vector<uint32_t> Permutation(Original.size());
    for (uint32_t Packed = 0; Packed < Original.size(); ++Packed)
    {
        const auto Ordinal = Scene.Visibility.OriginalFacets[Packed];
        Permutation[Ordinal] = Packed;
        Scene.Surfaces.push_back(Original[Ordinal]);
    }
    for (auto &Index : Domain.Profile)
        Index = Permutation[Index];
}

void UploadCdfProfile(FacetCdfScene &Domain)
{
    const uint32_t Support = uint32_t(Domain.Profile.size()), Start = 1025, Capacity = (Support + 3) / 4;
    auto &Scene = Domain.Scene;
    Scene.Width = Start + Capacity;
    Scene.Height = 1;
    Scene.Receivers.resize(Scene.Width);
    Scene.Receivers[0].PositionHit = {EncodeCausalIdentity(Support), EncodeCausalIdentity(1024),
                                      EncodeCausalIdentity(Start), EncodeCausalIdentity(Capacity)};
    for (uint32_t Index = Start; Index < Scene.Width; ++Index)
        Scene.Receivers[Index].PositionHit.fill(EncodeCausalIdentity(UINT32_MAX));
    for (uint32_t Index = 0; Index < Support; ++Index)
        Scene.Receivers[Start + Index / 4].PositionHit[Index % 4] =
            EncodeCausalIdentity(Domain.Profile[Index]);
    const auto Shading = CausalShading(Scene);
    std::vector<FacetAreaTriangle> Triangles;
    for (auto Index : Domain.Profile)
        Triangles.push_back(Shading[Index].Triangle);
    Require(Domain.Proposals.Prepare(Triangles), "CDF native proposals rejected");
}

void ConfigureCdfCameras(
    FacetCdfScene &Domain,
    uint32_t Fixture,
    uint32_t Method,
    bool Warm)
{
    for (uint32_t Pixel = 0; Pixel < 1024; ++Pixel)
    {
        auto &Camera = Domain.Scene.Receivers[1 + Pixel];
        Camera = ConstructPerspectiveAreaRay(Warm ? (Pixel + 1) % 1024 : Pixel, Fixture);
        uint32_t Seed = HashFacetAreaBits((Pixel + 0x5e92b341u) ^ (Fixture * 0x6c8e9cf5u));
        if (Warm)
            Seed ^= 0x31415927u;
        Camera.PositionHit = {EncodeCausalIdentity(Seed), EncodeCausalIdentity(Method), 0, 0};
    }
}

void ValidateCdfUnits()
{
    auto Scene = ConstructProfileScene(0, 16);
    FacetCdfScene Domain{Scene, std::vector<uint32_t>(16), {}, {}};
    for (uint32_t Index = 0; Index < 16; ++Index)
        Domain.Profile[Index] = Index;
    UploadCdfProfile(Domain);
    const auto Original = CaptureCdfInputs(Domain.Scene);
    Require((16 + 5) * 4 == 84, "Cache capacity differs");
    Require((65 + 5) * 4 == 280, "Large cache capacity differs");
    Require(Original.size() == 5, "Profile snapshot extent differs");
    Require(CdfInputsEqual(Original, CaptureCdfInputs(Domain.Scene)), "Identical profile changed");
    Domain.Scene.Receivers[1].ViewMetallic[0] += .1f;
    Require(CdfInputsEqual(Original, CaptureCdfInputs(Domain.Scene)), "Camera invalidates profile");
    Domain.Scene.Receivers[1].PositionHit[1] = EncodeCausalIdentity(5);
    Require(CdfInputsEqual(Original, CaptureCdfInputs(Domain.Scene)), "Method invalidates profile");
    Domain.Scene.Receivers[0].PositionHit[0] = EncodeCausalIdentity(15);
    Require(!CdfInputsEqual(Original, CaptureCdfInputs(Domain.Scene)), "Header change missed");
    Domain.Scene.Receivers[0].PositionHit[0] = EncodeCausalIdentity(16);
    Domain.Scene.Receivers[1025].PositionHit[0] = EncodeCausalIdentity(9);
    Require(!CdfInputsEqual(Original, CaptureCdfInputs(Domain.Scene)), "Profile change missed");
    std::cout << "CDF_GPU_UNIT checks=8 pass=1\n";
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CACHE AND IMAGE AUDITS
//------------------------------------------------------------------------------------------------------------------------

struct FacetCdfAudit
{
    std::ofstream Gpu, Native, Valid, Rejections, Cache, Work, First;
    uint64_t Failures = 0, Rejected = 0, GuardFailures = 0, Areas = 0, BaselineAreas = 0, Preparations = 0;
    uint32_t Images = 0, Batches = 0, PreviousRuns = 0;
    double CacheError = 0;
    explicit FacetCdfAudit(const std::string &Directory)
    {
        std::filesystem::create_directories(Directory);
        Gpu.open(Directory + "/FacetCdfGpu.f32", std::ios::binary);
        Native.open(Directory + "/FacetCdfNative.f32", std::ios::binary);
        Valid.open(Directory + "/FacetCdfValid.u8", std::ios::binary);
        Rejections.open(Directory + "/FacetCdfRejections.f32", std::ios::binary);
        Cache.open(Directory + "/FacetCdfCache.u32", std::ios::binary);
        Work.open(Directory + "/FacetCdfWork.u32", std::ios::binary);
        First.open(Directory + "/FacetCdfFirstFailure.txt");
        for (auto *File : {&Gpu, &Native, &Valid, &Rejections, &Cache, &Work, &First})
            File->exceptions(std::ios::failbit | std::ios::badbit);
    }

    void ValidateCache(const FacetCdfScene &Domain, const uint32_t *Storage)
    {
        const auto &Scene = Domain.Scene;
        std::vector<double> Areas;
        double Total = 0, Boundary = 0;
        for (auto Index : Domain.Profile)
        {
            const double Area = ContinuousFacetArea(Scene.Surfaces[Index]);
            Areas.push_back(Area);
            Total += Area;
        }
        const auto Check = [&](float Actual, double Expected) {
            const double Error = std::abs(double(Actual) - Expected) / std::max(1., std::abs(Expected));
            CacheError = std::max(CacheError, Error);
            Require(std::isfinite(Actual) && Error <= 2e-5, "GPU area/CDF cache differs");
        };
        Check(std::bit_cast<float>(Storage[4]), Total);
        Check(std::bit_cast<float>(Storage[5]),
              Domain.Proposals.Draw(11, 0, FacetAreaMeasureCategory::ProportionalArea).Density);
        for (uint32_t Ordinal = 0; Ordinal < Domain.Profile.size(); ++Ordinal)
        {
            Boundary += Areas[Ordinal] / Total;
            Check(std::bit_cast<float>(Storage[16 + 4 * Ordinal]), Areas[Ordinal]);
            Check(std::bit_cast<float>(Storage[17 + 4 * Ordinal]), Boundary);
            Check(std::bit_cast<float>(Storage[18 + 4 * Ordinal]),
                  1. / Domain.Profile.size() / Areas[Ordinal]);
            Require(Storage[19 + 4 * Ordinal] == Domain.Profile[Ordinal],
                    "GPU cached source identity differs");
        }
    }

    void CompareSubmission(
        FacetCdfScene          &Domain,
        LightComputeSubmission &Cached,
        LightComputeSubmission &Baseline,
        uint32_t                Fixture,
        uint32_t                Support,
        uint32_t                Method,
        uint32_t                Category,
        uint32_t                Revision,
        uint32_t                Runs,
        uint32_t                Rejection)
    {
        auto &Scene = Domain.Scene;
        const uint32_t Stride = uint32_t(Scene.Receivers.size()), Geometry = uint32_t(Scene.Surfaces.size());
        const CausalQuartet Sentinel{-12345, -12345, -12345, -12345};
        for (auto *Compute : {&Cached, &Baseline})
        {
            auto *Storage = static_cast<CausalQuartet *>(Compute->Buffers[6].Address);
            std::fill(Storage, Storage + 80 * Stride, Sentinel);
            Compute->ExecuteDiffuse(Scene, 1, 17, false, false, true);
        }
        const auto *CacheStorage = static_cast<const uint32_t *>(Cached.Buffers[5].Address);
        const auto *Actual = static_cast<const CausalQuartet *>(Cached.Buffers[6].Address);
        const auto *Uncached = static_cast<const CausalQuartet *>(Baseline.Buffers[6].Address);
        const uint32_t CurrentAreas = static_cast<const uint32_t *>(Cached.Buffers[7].Address)[20];
        const uint32_t OtherAreas = static_cast<const uint32_t *>(Baseline.Buffers[7].Address)[20];
        const uint32_t NewRuns = CacheStorage[2] - PreviousRuns;
        uint64_t Bad = 0;
        try
        {
            Require(CacheStorage[0] == Rejection &&
                        CacheStorage[1] == DecodeCausalIdentity(Scene.Receivers[0].PositionHit[0]),
                    "Cache status differs");
            Require(CacheStorage[2] == Runs && NewRuns <= 1 && CacheStorage[8] == 1,
                    "Cache invalidation differs");
            Require(CurrentAreas == (NewRuns ? CacheStorage[3] : 0), "Sampling evaluated facet area");
            if (Rejection == 0)
                ValidateCache(Domain, CacheStorage);
        }
        catch (const std::exception &Error)
        {
            ++Bad;
            if (Failures == 0)
                First << Error.what() << '\n';
        }
        std::array<uint32_t, 288> Snapshot{};
        Snapshot[0] = Fixture;
        Snapshot[1] = Support;
        Snapshot[2] = Method;
        Snapshot[3] = Category;
        Snapshot[4] = Revision;
        Snapshot[5] = Geometry;
        Snapshot[6] = Runs;
        Snapshot[7] = Rejection;
        std::copy(CacheStorage, CacheStorage + (Geometry + 5) * 4, Snapshot.begin() + 8);
        Cache.write(reinterpret_cast<const char *>(Snapshot.data()), sizeof(Snapshot));
        const std::array<uint32_t, 12> Observation{
            Fixture,      Support,    Method,  Category,        Revision,         Runs,
            CurrentAreas, OtherAreas, NewRuns, CacheStorage[9], CacheStorage[10], Rejection};
        Work.write(reinterpret_cast<const char *>(Observation.data()), sizeof(Observation));
        for (uint32_t Index = (Geometry + 4) * 4; Index < (Geometry + 5) * 4; ++Index)
            if (CacheStorage[Index] != 0x5a5a5a5au)
                ++GuardFailures;
        for (uint32_t Slab = 0; Slab < 80; ++Slab)
            for (uint32_t Pixel = 1024; Pixel < Stride; ++Pixel)
                if (Actual[size_t(Slab) * Stride + Pixel] != Sentinel ||
                    Uncached[size_t(Slab) * Stride + Pixel] != Sentinel)
                    ++GuardFailures;
        for (uint32_t Pixel = 0; Pixel < 1024; ++Pixel)
        {
            std::array<CausalQuartet, 80> Output{};
            std::vector<CausalQuartet> Expected(80);
            uint8_t Accepted = 1;
            std::string Diagnostic;
            for (uint32_t Slab = 0; Slab < 80; ++Slab)
                Output[Slab] = Actual[size_t(Slab) * Stride + Pixel];
            try
            {
                for (uint32_t Slab = 0; Slab < 80; ++Slab)
                    Require(std::memcmp(&Output[Slab], &Uncached[size_t(Slab) * Stride + Pixel], 16) == 0,
                            "Paired uncached GPU differs");
                if (Rejection)
                {
                    for (uint32_t Slab = 0; Slab < 80; ++Slab)
                        Require(Output[Slab] ==
                                    (Slab == 79 ? CausalQuartet{float(Rejection), 0, 0, 0} : CausalQuartet{}),
                                "Rejected output differs");
                    ++Rejected;
                }
                else
                {
                    auto Single = Scene;
                    Single.Receivers[0] = Scene.Receivers[1 + Pixel];
                    try
                    {
                        Expected = AssemblePacketReference(
                            Single, Domain.Profile, Domain.Proposals, Domain.Reference,
                            DecodeCausalIdentity(Single.Receivers[0].PositionHit[0]), Method, 0);
                    }
                    catch (...)
                    {
                        Accepted = 0;
                        throw;
                    }
                    ComparePacketReadback(Output.data(), Expected.data());
                    for (uint32_t Slab = 77; Slab < 80; ++Slab)
                        Require(Output[Slab] == CausalQuartet{}, "Reserved output differs");
                }
            }
            catch (const std::exception &Error)
            {
                Diagnostic = Error.what();
                ++Bad;
            }
            if (!Diagnostic.empty() && Failures == 0 && Bad == 1)
                First << "fixture=" << Fixture << " support=" << Support << " method=" << Method
                      << " category=" << Category << " revision=" << Revision << " pixel=" << Pixel << " "
                      << Diagnostic << '\n';
            if (Rejection)
                Rejections.write(reinterpret_cast<const char *>(Output.data()), sizeof(Output));
            else
            {
                Gpu.write(reinterpret_cast<const char *>(Output.data()), sizeof(Output));
                Native.write(reinterpret_cast<const char *>(Expected.data()), 80 * 16);
                Valid.write(reinterpret_cast<const char *>(&Accepted), 1);
            }
        }
        Areas += CurrentAreas;
        BaselineAreas += OtherAreas;
        Preparations += NewRuns;
        PreviousRuns = CacheStorage[2];
        Failures += Bad;
        ++Batches;
        if (!Rejection)
            ++Images;
        std::cout << "CDF_GPU_BATCH fixture=" << Fixture << " support=" << Support << " method=" << Method
                  << " category=" << Category << " revision=" << Revision << " preparations=" << Runs
                  << " cached_areas=" << CurrentAreas << " uncached_areas=" << OtherAreas
                  << " failures=" << Bad << '\n';
    }
};

bool ValidateCdf(const char *Driver, const std::string &Shaders, const std::string &Directory)
{
    FacetCdfAudit Audit(Directory);
    for (uint32_t Fixture = 0; Fixture < 3; ++Fixture)
        for (uint32_t Support : ProfileSizes)
        {
            FacetCdfScene Domain;
            Domain.Scene = ConstructProfileScene(Fixture, Support);
            Domain.Profile.resize(Support);
            for (uint32_t Ordinal = 0; Ordinal < Support; ++Ordinal)
                Domain.Profile[Ordinal] = Support == 16 ? Ordinal : Support - 1 - Ordinal;
            RepackCdfScene(Domain);
            UploadCdfProfile(Domain);
            LightComputeSubmission Cached(Driver), Baseline(Driver);
            Cached.Prepare(Domain.Scene, Shaders, false, false, true, false, false, false, false, 0, 80);
            Baseline.Prepare(Domain.Scene, Shaders + "/uncached", false, false, true, false, false, false,
                             false, 0, 80);
            Audit.PreviousRuns = 0;
            if (Fixture == 0 && Support == 1)
                std::cout << "CDF_GPU_DEVICE name=" << Cached.Properties.deviceName
                          << " type=" << Cached.Properties.deviceType << " driver_sanitized=0 leaks=0\n";
            for (uint32_t Method = 0; Method < 6; ++Method)
            {
                ConfigureCdfCameras(Domain, Fixture, Method, false);
                Audit.CompareSubmission(Domain, Cached, Baseline, Fixture, Support, Method, 0, 0, 1, 0);
            }
            if (Fixture != 0 || Support != 16)
                continue;
            const auto Original = Domain;
            const std::array<uint32_t, 6> Codes{1, 1, 1, 2, 2, 3};
            for (uint32_t Mutation = 0; Mutation < 6; ++Mutation)
            {
                Domain = Original;
                auto &Scene = Domain.Scene;
                if (Mutation == 0)
                    Scene.Receivers[0].PositionHit[0] = EncodeCausalIdentity(0);
                if (Mutation == 1)
                    Scene.Receivers[0].PositionHit[0] = EncodeCausalIdentity(17);
                if (Mutation == 2)
                    Scene.Receivers[0].PositionHit[3] = EncodeCausalIdentity(3);
                if (Mutation == 3)
                    Scene.Receivers[1025].PositionHit[0] = EncodeCausalIdentity(UINT32_MAX);
                if (Mutation == 4)
                    Scene.Receivers[1025].PositionHit[1] = Scene.Receivers[1025].PositionHit[0];
                if (Mutation == 5)
                {
                    auto &Facet = Scene.Surfaces[Domain.Profile[0]];
                    Facet.B = Facet.C = Facet.A;
                }
                Audit.CompareSubmission(Domain, Cached, Baseline, Fixture, Support, 5, 1, Mutation, Mutation + 2,
                            Codes[Mutation]);
            }
            const std::array<uint32_t, 8> Runs{8, 8, 9, 10, 11, 12, 12, 13};
            for (uint32_t Revision = 0; Revision < 8; ++Revision)
            {
                if (Revision == 0 || Revision == 5)
                    Domain = Original;
                if (Revision == 1)
                    ConfigureCdfCameras(Domain, Fixture, 5, true);
                if (Revision == 2)
                {
                    std::reverse(Domain.Profile.begin(), Domain.Profile.end());
                    UploadCdfProfile(Domain);
                }
                if (Revision == 3)
                {
                    Domain.Scene.Surfaces[Domain.Profile[0]].AlbedoRoughness[0] *= .9f;
                    UploadCdfProfile(Domain);
                }
                if (Revision == 4)
                {
                    for (auto &Facet : Domain.Scene.Surfaces)
                        for (auto *Point : {&Facet.A, &Facet.B, &Facet.C})
                            (*Point)[0] += .001f;
                    RepackCdfScene(Domain);
                    UploadCdfProfile(Domain);
                }
                if (Revision == 6)
                    ConfigureCdfCameras(Domain, Fixture, 0, false);
                if (Revision == 7)
                {
                    Domain.Profile.pop_back();
                    UploadCdfProfile(Domain);
                }
                Audit.CompareSubmission(Domain, Cached, Baseline, Fixture, Revision == 7 ? 15 : Support,
                            Revision >= 6 ? 0 : 5, 2, Revision, Runs[Revision], 0);
            }
        }
    const bool Passed = Audit.Failures == 0 && Audit.GuardFailures == 0 && Audit.Preparations == 30 &&
                        Audit.Areas == 466 && Audit.Rejected == 6144;
    if (Audit.Failures == 0)
        Audit.First << "No comparison failure.\n";
    std::cout << std::setprecision(17) << "CDF_GPU_TOTAL images=" << Audit.Images
              << " batches=" << Audit.Batches << " packets=" << Audit.Images * 1024
              << " candidates=" << PacketDraws << " paths=" << PacketPaths << " pairs=" << PacketPairs
              << " visibility=" << PacketPaths * 2 << " preparations=" << Audit.Preparations
              << " cached_areas=" << Audit.Areas << " uncached_areas=" << Audit.BaselineAreas
              << " rejected=" << Audit.Rejected << " failures=" << Audit.Failures
              << " guard_failures=" << Audit.GuardFailures << " rgb_error=" << PacketRgbError
              << " coordinate_error=" << PacketCoordinateError << " scalar_error=" << PacketScalarError
              << " cache_error=" << Audit.CacheError << " pass=" << Passed << '\n';
    return Passed;
}
} // namespace

int main(int Count, char **Arguments)
{
    try
    {
        std::cout << std::unitbuf;
        if (Count == 2 && std::strcmp(Arguments[1], "--unit") == 0)
        {
            ValidateCdfUnits();
            return 0;
        }
        Require(Count == 4, "Use driver shaders output-directory or --unit");
        return ValidateCdf(Arguments[1], Arguments[2], Arguments[3]) ? 0 : 1;
    }
    catch (const std::exception &Error)
    {
        std::cerr << Error.what() << '\n';
        return 2;
    }
}
