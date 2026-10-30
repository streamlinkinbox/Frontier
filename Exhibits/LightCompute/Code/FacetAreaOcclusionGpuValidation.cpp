/*============================================================================================================================================
                                                FACETAREAOCCLUSIONGPUVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Compare fresh Vulkan receiver/proposal/selection/ray observations with unchanged native APIs and analytic
// controls.
#include "FacetAreaOcclusionGpuSubmission.h"
#define main NativeOcclusionEntry
#include "FacetAreaOcclusionValidation.cpp"
#undef main

//------------------------------------------------------------------------------------------------------------------------
//                                                GPU DIAGNOSTIC COMPARISONS
//------------------------------------------------------------------------------------------------------------------------
namespace
{
bool WithinTolerance(float First, float Second)
{
    return std::isfinite(First) && std::isfinite(Second) && std::abs(First - Second) <= 2e-5f;
}
uint32_t DecodeIdentity(float Encoded)
{
    uint32_t Identity;
    std::memcpy(&Identity, &Encoded, 4);
    return Identity;
}
void ValidateGpuUnits()
{
    Require(WithinTolerance(0, 0), "Exact comparison rejected");
    Require(WithinTolerance(1, 1.000001f), "Small error rejected");
    Require(!WithinTolerance(0, .001f), "Large error missed");
    Require(!WithinTolerance(NAN, 0), "NaN missed");
    Require(!WithinTolerance(INFINITY, INFINITY), "Infinity missed");
    Require(DecodeIdentity(0) == 0, "Zero identity decode differs");
    uint32_t Packed = 0x30007u;
    float Encoded;
    std::memcpy(&Encoded, &Packed, 4);
    Require(DecodeIdentity(Encoded) == Packed, "Visibility identity decode differs");
    Require((DecodeIdentity(Encoded) >> 16) == 3, "Visibility mask decode differs");
    std::cout << "OCCLUSION_GPU_UNIT checks=8 pass=1\n";
}

void ValidateGpuCapture(const char *Driver, const std::string &Shaders, const std::string &Directory)
{
    constexpr uint32_t Count = 12288;
    uint64_t PrimaryCount = 0, CandidateChecks = 0, PathCount = 0;
    double MaximumRay = 0, MaximumTarget = 0, MaximumNormalizer = 0;
    std::filesystem::create_directories(Directory);
    for (uint32_t Layout = 0; Layout < 3; ++Layout)
        for (uint32_t Variant = 0; Variant < 4; ++Variant)
        {
            const auto Native = ConstructOcclusionScene(Layout, Variant);
            LightComputeArchive Scene;
            Scene.Width      = 32;
            Scene.Height     = 384;
            Scene.Visibility = Native.DoublePrecisionBvh.PackFloat32();
            for (const auto &Surface : Native.Surfaces)
            {
                LightComputeFacet Facet{};
                for (uint32_t Axis = 0; Axis < 3; ++Axis)
                {
                    Facet.A[Axis]               = Surface.Triangle.A[Axis];
                    Facet.B[Axis]               = Surface.Triangle.B[Axis];
                    Facet.C[Axis]               = Surface.Triangle.C[Axis];
                    Facet.NormalA[Axis]         = Surface.Normals[0][Axis];
                    Facet.NormalB[Axis]         = Surface.Normals[1][Axis];
                    Facet.NormalC[Axis]         = Surface.Normals[2][Axis];
                    Facet.AlbedoRoughness[Axis] = Surface.Albedo[Axis];
                }
                Facet.AlbedoRoughness[3] = 1;
                Scene.Surfaces.push_back(Facet);
            }
            Scene.Receivers.resize(Count);
            for (uint32_t Pixel = 0; Pixel < Count; ++Pixel)
            {
                const uint32_t Packet = Pixel % 4096, ImagePixel = Packet % 1024;
                auto &Receiver      = Scene.Receivers[Pixel];
                const uint32_t Seed = HashFacetAreaBits((Packet + 0x82c4d719u) ^ (Layout * 0x6c8e9cf5u));
                const std::array<uint32_t, 2> Identity{Seed, Pixel / 4096};
                std::memcpy(Receiver.PositionHit.data(), Identity.data(), 8);
                std::memcpy(Receiver.AlbedoIor.data(), Native.SourceIndices.data(), 8);
                const auto Camera =
                    TransformWorldPoint(Layouts[Layout], -.2f + .4f * (float(ImagePixel % 32) + .5f) / 32,
                                        -.2f + .4f * (float(ImagePixel / 32) + .5f) / 32, 1.5f);
                for (uint32_t Axis = 0; Axis < 3; ++Axis)
                    Receiver.ViewMetallic[Axis] = Camera[Axis];
                Receiver.NormalRoughness = {0, 0, 1, 0};
            }
            LightComputeSubmission Compute(Driver);
            Compute.Prepare(Scene, Shaders, false, false, true, false, false, false, false, 0, 40);
            if (Layout == 0 && Variant == 0)
                std::cout << "OCCLUSION_GPU_DEVICE name=" << Compute.Properties.deviceName
                          << " type=" << Compute.Properties.deviceType << " driver_sanitized=0\n";
            Compute.ExecuteDiffuse(Scene, 2, 17, false, false, true);
            const auto *Storage = static_cast<const Quartet *>(Compute.Buffers[6].Address);
            for (uint32_t Pixel = 0; Pixel < Count; ++Pixel)
            {
                const uint32_t Method = Pixel / 4096, Packet = Pixel % 4096;
                const auto &Input = Scene.Receivers[Pixel];
                const RayVector Camera{Input.ViewMetallic[0], Input.ViewMetallic[1], Input.ViewMetallic[2]};
                const auto Primary = Native.Transport.Primary(Camera, {0, 0, 1});
                Require(Primary.Accepted && DecodeIdentity(Storage[7 * Count + Pixel][0]) == Primary.Index,
                        "GPU primary identity differs");
                for (uint32_t Axis = 0; Axis < 3; ++Axis)
                {
                    Require(WithinTolerance(Storage[4 * Count + Pixel][Axis], Primary.Position[Axis]),
                            "GPU primary position differs");
                    Require(WithinTolerance(Storage[5 * Count + Pixel][Axis], Primary.Normal[Axis]),
                            "GPU primary normal differs");
                    Require(WithinTolerance(Storage[6 * Count + Pixel][Axis], Primary.Albedo[Axis]),
                            "GPU primary albedo differs");
                    Require(WithinTolerance(Storage[7 * Count + Pixel][Axis + 1], Primary.Strike[Axis]),
                            "GPU primary strike differs");
                }
                ++PrimaryCount;
                const uint32_t Seed = HashFacetAreaBits((Packet + 0x82c4d719u) ^ (Layout * 0x6c8e9cf5u));
                uint32_t Slot       = 0;
                for (uint32_t Lane = 0; Lane < 4; ++Lane)
                {
                    std::array<FacetAreaProposal, 2> Proposals{};
                    std::array<FacetAreaScore, 2> Scores{};
                    const uint32_t CandidateCount = Method == 1 ? 1 : 2;
                    for (uint32_t Ordinal = 0; Ordinal < CandidateCount; ++Ordinal)
                    {
                        auto &Proposal = Proposals[Ordinal];
                        Proposal =
                            Native.Proposals.Draw(Seed, 2 * Lane + Ordinal, FacetAreaMeasureCategory::UniformFacet);
                        Proposal.Index  = Native.SourceIndices[Proposal.Index];
                        const auto &Gpu = Storage[(8 + 2 * Lane + Ordinal) * Count + Pixel];
                        Require(DecodeIdentity(Gpu[0]) == Proposal.Index && Gpu[1] == Proposal.U &&
                                    Gpu[2] == Proposal.V && Gpu[3] == Proposal.Density,
                                "GPU proposal identity/coordinates/density differ");
                        const auto Source = Native.Transport.Source(Proposal);
                        FacetAreaScoreInput ScoreInput;
                        ScoreInput.Triangle       = Native.Surfaces[Proposal.Index].Triangle;
                        ScoreInput.U              = Proposal.U;
                        ScoreInput.V              = Proposal.V;
                        ScoreInput.SourceNormal   = Source.Normal;
                        ScoreInput.SourceAlbedo   = Source.Albedo;
                        ScoreInput.Receiver       = Primary.Position;
                        ScoreInput.ReceiverNormal = Primary.Normal;
                        ScoreInput.ReceiverAlbedo = Primary.Albedo;
                        Scores[Ordinal]           = EvaluateFacetAreaScore(ScoreInput);
                        Require(Source.Accepted && Scores[Ordinal].Accepted, "Native source/score rejected");
                        const float GpuTarget = Storage[(16 + Lane) * Count + Pixel][Ordinal];
                        Require(WithinTolerance(GpuTarget, Scores[Ordinal].Target), "GPU target differs");
                        MaximumTarget = std::max(MaximumTarget, double(std::abs(GpuTarget - Scores[Ordinal].Target)));
                        ++CandidateChecks;
                    }
                    uint32_t Selected = 0;
                    float Normalizer  = Method == 1 ? 1 / Proposals[0].Density : 0;
                    if (Method == 0)
                    {
                        const auto Pair = SelectFacetAreaPair(
                            {{{Scores[0].Target, Proposals[0].Density}, {Scores[1].Target, Proposals[1].Density}}},
                            DrawFacetAreaSelectionUniform(Seed, Lane));
                        Require(Pair.Accepted, "Native pair rejected");
                        Selected   = Pair.Selected;
                        Normalizer = float(Pair.Normalizer);
                    }
                    const auto &Telemetry = Storage[(16 + Lane) * Count + Pixel];
                    Require(Telemetry[3] == float(Selected), "GPU selected ordinal differs");
                    const double Error =
                        std::abs(double(Telemetry[2]) - Normalizer) / std::max(1., double(std::abs(Normalizer)));
                    Require(std::isfinite(Telemetry[2]) && Error <= 2e-5, "GPU normalizer differs");
                    MaximumNormalizer = std::max(MaximumNormalizer, Error);
                    for (uint32_t Ordinal = 0; Ordinal < CandidateCount; ++Ordinal)
                    {
                        if (Method == 0 && Ordinal != Selected)
                            continue;
                        const auto Path = Native.Transport.Connect(Proposals[Ordinal], Primary);
                        Require(Path.Accepted, "Native path rejected");
                        const bool SuffixExpected = (Variant & 2) == 0, ConnectionExpected = (Variant & 1) == 0;
                        Require(Path.SuffixVisible == SuffixExpected && Path.ConnectionVisible == ConnectionExpected,
                                "Native analytic mask differs");
                        Require(!Native.DoublePrecisionBvh.Occluded(ConvertSpatialVector(Path.Origin),
                                                                    {double(.8f), 0, double(.6f)}, .00001f,
                                                                    100) == SuffixExpected,
                                "Double suffix differs");
                        Require(!Native.DoublePrecisionBvh.Occluded(ConvertSpatialVector(Path.Origin),
                                                                    ConvertSpatialVector(Path.Direction), .00001f,
                                                                    Path.Maximum) == ConnectionExpected,
                                "Double connection differs");
                        const auto &Origin            = Storage[(20 + Slot) * Count + Pixel];
                        const auto &Direction         = Storage[(28 + Slot) * Count + Pixel];
                        const uint32_t ExpectedPacked = Proposals[Ordinal].Index | (SuffixExpected ? 1u << 16 : 0) |
                                                        (ConnectionExpected ? 1u << 17 : 0);
                        Require(DecodeIdentity(Origin[3]) == ExpectedPacked,
                                "GPU visibility/selected identity differs");
                        for (uint32_t Axis = 0; Axis < 3; ++Axis)
                        {
                            Require(WithinTolerance(Origin[Axis], Path.Origin[Axis]) &&
                                        WithinTolerance(Direction[Axis], Path.Direction[Axis]),
                                    "GPU emitted ray differs");
                            MaximumRay = std::max({MaximumRay, double(std::abs(Origin[Axis] - Path.Origin[Axis])),
                                                   double(std::abs(Direction[Axis] - Path.Direction[Axis]))});
                        }
                        Require(WithinTolerance(Direction[3], Path.Maximum), "GPU ray interval differs");
                        MaximumRay = std::max(MaximumRay, double(std::abs(Direction[3] - Path.Maximum)));
                        ++Slot;
                        ++PathCount;
                    }
                }
                const auto &Work = Storage[36 * Count + Pixel];
                Require(Work[0] == float(Slot) && Work[1] == float(Slot) && Work[2] == float(Method == 1 ? 4 : 8) &&
                            Work[3] == float(Slot),
                        "GPU query telemetry differs");
            }
            std::ofstream Capture(Directory + "/OcclusionGpu" + std::to_string(Layout) + std::to_string(Variant) +
                                      ".f32",
                                  std::ios::binary);
            Capture.exceptions(std::ios::failbit | std::ios::badbit);
            Capture.write(reinterpret_cast<const char *>(Storage), 40 * Count * 16);
            std::cout << "OCCLUSION_GPU_CASE layout=" << Layout << " variant=" << Variant << " packets=" << Count
                      << " pass=1\n";
        }
    Require(PrimaryCount == 147456 && CandidateChecks == 983040 && PathCount == 786432, "GPU audit totals differ");
    std::cout << std::setprecision(17) << "OCCLUSION_GPU_TOTAL primary=" << PrimaryCount
              << " candidates=" << CandidateChecks << " paths=" << PathCount << " visibility=" << 2 * PathCount
              << " target_error=" << MaximumTarget << " normalizer_error=" << MaximumNormalizer
              << " ray_error=" << MaximumRay << " pass=1\n";
}
} // namespace

int main(int Count, char **Arguments)
{
    try
    {
        if (Count == 2 && std::string(Arguments[1]) == "--unit")
        {
            ValidateGpuUnits();
            return 0;
        }
        Require(Count == 4, "Use driver shader-directory capture-directory or --unit");
        ValidateGpuCapture(Arguments[1], Arguments[2], Arguments[3]);
        return 0;
    }
    catch (const std::exception &Error)
    {
        std::cerr << Error.what() << '\n';
        return 1;
    }
}
