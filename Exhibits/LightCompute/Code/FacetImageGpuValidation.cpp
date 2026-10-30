/*============================================================================================================================================
                                                     FACETIMAGEGPUVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Compare complete perspective packet images using unchanged native packet and Vulkan submission
// arithmetic.
#include "FacetImageGpuComparisons.h"

//------------------------------------------------------------------------------------------------------------------------
//                                                    IMAGE INDEXING
//------------------------------------------------------------------------------------------------------------------------

namespace
{
constexpr uint32_t ImagePixels = 1024, ImageStride = ImagePixels + 4;
size_t ImageSlabOffset(uint32_t Slab, uint32_t Pixel)
{
    return size_t(Slab) * ImageStride + Pixel;
}

void ValidateImageUnits()
{
    Require(ImageSlabOffset(0, 0) == 0, "First pixel offset differs");
    Require(ImageSlabOffset(0, 1023) == 1023, "Last pixel offset differs");
    Require(ImageSlabOffset(1, 0) == 1028, "Profile tail overlaps next slab");
    Require(ImageSlabOffset(79, 1023) < 80 * ImageStride, "Last diagnostic exceeds allocation");
    Require(ImageStride - ImagePixels == 4, "Profile capacity differs");
    Require(ImageSlabOffset(79, ImageStride - 1) == 80 * ImageStride - 1, "Tail extent differs");
    std::cout << "IMAGE_GPU_UNIT checks=6 pass=1\n";
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    IMAGE VALIDATION
//------------------------------------------------------------------------------------------------------------------------

bool ValidateImages(const char *Driver, const std::string &Shaders, const std::string &Directory)
{
    std::filesystem::create_directories(Directory);
    std::ofstream Gpu(Directory + "/FacetImageGpu.f32", std::ios::binary);
    std::ofstream Native(Directory + "/FacetImageNative.f32", std::ios::binary);
    std::ofstream Valid(Directory + "/FacetImageValid.u8", std::ios::binary);
    std::ofstream First(Directory + "/FacetImageFirstFailure.txt");
    Gpu.exceptions(std::ios::badbit | std::ios::failbit);
    Native.exceptions(std::ios::badbit | std::ios::failbit);
    Valid.exceptions(std::ios::badbit | std::ios::failbit);
    First.exceptions(std::ios::badbit | std::ios::failbit);
    uint64_t Failures = 0, NativeFailures = 0, TailFailures = 0;
    for (uint32_t Fixture = 0; Fixture < 3; ++Fixture)
    {
        auto Scene = ConstructDeformedAreaScene(Fixture);
        const auto Original = Scene.Surfaces;
        std::vector<FacetAreaTriangle> Triangles;
        for (const auto &Surface : CausalShading(Scene))
            Triangles.push_back(Surface.Triangle);
        FacetAreaProposals Proposals;
        Require(Proposals.Prepare(Triangles), "Image proposal support rejected");
        std::vector<std::array<SpatialVector, 3>> Corners;
        for (const auto &Facet : Original)
            Corners.push_back({SpatialVector{Facet.A[0], Facet.A[1], Facet.A[2]},
                               SpatialVector{Facet.B[0], Facet.B[1], Facet.B[2]},
                               SpatialVector{Facet.C[0], Facet.C[1], Facet.C[2]}});
        TriangleVisibilitySpace Reference;
        Reference.Build(Corners);
        Scene.Visibility = Reference.PackFloat32();
        Scene.Surfaces.clear();
        std::vector<uint32_t> Profile(16);
        for (uint32_t Packed = 0; Packed < 16; ++Packed)
        {
            const auto Ordinal = Scene.Visibility.OriginalFacets[Packed];
            Scene.Surfaces.push_back(Original[Ordinal]);
            Profile[Ordinal] = Packed;
        }
        Scene.Width = ImageStride;
        Scene.Height = 1;
        Scene.Receivers.resize(ImageStride);
        for (uint32_t Ordinal = 0; Ordinal < 16; ++Ordinal)
            Scene.Receivers[ImagePixels + Ordinal / 4].PositionHit[Ordinal % 4] =
                EncodeCausalIdentity(Profile[Ordinal]);
        LightComputeSubmission Compute(Driver);
        Compute.Prepare(Scene, Shaders, false, false, true, false, false, false, false, 0, 80);
        if (Fixture == 0)
            std::cout << "IMAGE_GPU_DEVICE name=" << Compute.Properties.deviceName
                      << " type=" << Compute.Properties.deviceType << " driver_sanitized=0 leaks=0\n";
        for (uint32_t Method = 0; Method < 6; ++Method)
            for (uint32_t Sample = 0; Sample < 2; ++Sample)
            {
                for (uint32_t Pixel = 0; Pixel < ImagePixels; ++Pixel)
                {
                    auto &Camera = Scene.Receivers[Pixel];
                    Camera = ConstructPerspectiveAreaRay(Pixel, Fixture);
                    const uint32_t Seed = HashFacetAreaBits((Sample * ImagePixels + Pixel + 0x5e92b341u) ^
                                                            (Fixture * 0x6c8e9cf5u));
                    Camera.PositionHit = {EncodeCausalIdentity(Seed), EncodeCausalIdentity(Method), 0, 0};
                }
                const CausalQuartet Sentinel{-12345, -12345, -12345, -12345};
                auto *Storage = static_cast<CausalQuartet *>(Compute.Buffers[6].Address);
                std::fill(Storage, Storage + 80 * ImageStride, Sentinel);
                Compute.ExecuteDiffuse(Scene, 1, 17, false, false, true);
                uint32_t FrameFailures = 0, FrameNativeFailures = 0, FrameTailFailures = 0;
                for (uint32_t Slab = 0; Slab < 80; ++Slab)
                    for (uint32_t Pixel = ImagePixels; Pixel < ImageStride; ++Pixel)
                        if (Storage[ImageSlabOffset(Slab, Pixel)] != Sentinel)
                            ++FrameTailFailures;
                for (uint32_t Pixel = 0; Pixel < ImagePixels; ++Pixel)
                {
                    std::array<CausalQuartet, 80> Actual{};
                    for (uint32_t Slab = 0; Slab < 80; ++Slab)
                        Actual[Slab] = Storage[ImageSlabOffset(Slab, Pixel)];
                    std::vector<CausalQuartet> Expected(80);
                    uint8_t Accepted = 1;
                    std::string Diagnostic;
                    try
                    {
                        auto Single = Scene;
                        Single.Receivers[0] = Scene.Receivers[Pixel];
                        Expected = AssemblePacketReference(
                            Single, Profile, Proposals, Reference,
                            DecodeCausalIdentity(Single.Receivers[0].PositionHit[0]), Method, 0);
                    }
                    catch (const std::exception &Error)
                    {
                        Accepted = 0;
                        Diagnostic = Error.what();
                        ++FrameNativeFailures;
                    }
                    if (Accepted)
                        try
                        {
                            ComparePacketReadback(Actual.data(), Expected.data());
                            for (uint32_t Slab = 0; Slab < 80; ++Slab)
                                for (uint32_t Axis = 0; Axis < 4; ++Axis)
                                    Require(std::isfinite(Actual[Slab][Axis]), "Nonfinite image diagnostic");
                            for (uint32_t Slab = 0; Slab < 80; ++Slab)
                                if (Slab < 7 || (Slab >= 20 && Slab < 44) || (Slab >= 61 && Slab < 80))
                                    Require(Actual[Slab][3] == 0, "Nonzero reserved diagnostic component");
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
                        if (Failures + FrameFailures == 0)
                            First << "fixture=" << Fixture << " method=" << Method << " sample=" << Sample
                                  << " pixel=" << Pixel << " diagnostic=" << Diagnostic << '\n';
                        ++FrameFailures;
                    }
                    Gpu.write(reinterpret_cast<const char *>(Actual.data()), 80 * 16);
                    Native.write(reinterpret_cast<const char *>(Expected.data()), 80 * 16);
                    Valid.write(reinterpret_cast<const char *>(&Accepted), 1);
                }
                Failures += FrameFailures;
                NativeFailures += FrameNativeFailures;
                TailFailures += FrameTailFailures;
                std::cout << "IMAGE_GPU_FRAME fixture=" << Fixture << " method=" << Method
                          << " sample=" << Sample << " pixels=1024 failures=" << FrameFailures
                          << " native_failures=" << FrameNativeFailures
                          << " tail_failures=" << FrameTailFailures << '\n';
            }
    }
    if (Failures == 0)
        First << "No packet comparison failure.\n";
    std::cout << std::setprecision(17) << "IMAGE_GPU_TOTAL images=36 packets=36864 candidates=" << PacketDraws
              << " paths=" << PacketPaths << " pairs=" << PacketPairs << " visibility=" << 2 * PacketPaths
              << " failures=" << Failures << " native_failures=" << NativeFailures
              << " tail_failures=" << TailFailures << " rgb_error=" << PacketRgbError
              << " coordinate_error=" << PacketCoordinateError << " scalar_error=" << PacketScalarError
              << " pass=" << (Failures + TailFailures == 0) << '\n';
    return Failures + TailFailures == 0;
}
} // namespace

int main(int Count, char **Arguments)
{
    try
    {
        std::cout << std::unitbuf;
        if (Count == 2 && std::strcmp(Arguments[1], "--unit") == 0)
        {
            ValidateImageUnits();
            return 0;
        }
        Require(Count == 4, "Use driver shaders output-directory or --unit");
        return ValidateImages(Arguments[1], Arguments[2], Arguments[3]) ? 0 : 1;
    }
    catch (const std::exception &Error)
    {
        std::cerr << Error.what() << '\n';
        return 2;
    }
}
