/*============================================================================================================================================
                                                   FACETCAUSALGPUVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Compare actual selected-proposal Vulkan causal mutations and shader faults with native transport.
#include "FacetCausalGpuSubmission.h"
#define Require RequireCausalGeometry
#include "FacetAreaProposalScenes.h"
#undef Require
#include "Engine/Rendering/FacetAreaTransport.h"

namespace
{
using CausalQuartet = std::array<float, 4>;
using CausalRay     = std::array<float, 3>;
uint32_t DecodeCausalIdentity(float Encoded)
{
    uint32_t Index;
    std::memcpy(&Index, &Encoded, 4);
    return Index;
}
float EncodeCausalIdentity(uint32_t Index)
{
    float Encoded;
    std::memcpy(&Encoded, &Index, 4);
    return Encoded;
}
bool CausalRgbEqual(const CausalRay &First, const CausalRay &Second)
{
    float Maximum = std::max({std::abs(Second[0]), std::abs(Second[1]), std::abs(Second[2])});
    for (uint32_t Axis = 0; Axis < 3; ++Axis)
        if (!std::isfinite(First[Axis]) || !std::isfinite(Second[Axis]) ||
            std::abs(First[Axis] - Second[Axis]) > 1e-10f + 2e-4f * Maximum)
            return false;
    return true;
}
CausalRay CausalTriplet(const CausalQuartet &Quartet)
{
    return {Quartet[0], Quartet[1], Quartet[2]};
}
bool IsGpuCausalOcclusion(const CausalQuartet *Storage, const CausalRay &Original, uint32_t Operation)
{
    const CausalRay Zero{};
    const auto      Correct  = CausalTriplet(Storage[0]);
    const auto      Restored = CausalTriplet(Storage[Operation == 0 ? 1 : 2]);
    const auto      Wrong    = CausalTriplet(Storage[Operation == 0 ? 2 : 1]);
    return Correct == Zero && Restored != Zero && CausalRgbEqual(Restored, Original) && Wrong == Zero &&
           Storage[12][0] == float(Operation == 1) && Storage[12][1] == float(Operation == 0);
}
std::vector<FacetAreaShadingTriangle> CausalShading(const LightComputeArchive &Scene)
{
    std::vector<FacetAreaShadingTriangle> Surfaces;
    for (const auto &Facet : Scene.Surfaces)
    {
        FacetAreaShadingTriangle Surface;
        Surface.Triangle = {CausalTriplet(Facet.A), CausalTriplet(Facet.B), CausalTriplet(Facet.C)};
        Surface.Normals  = {CausalTriplet(Facet.NormalA), CausalTriplet(Facet.NormalB), CausalTriplet(Facet.NormalC)};
        Surface.Albedo   = CausalTriplet(Facet.AlbedoRoughness);
        Surfaces.push_back(Surface);
    }
    return Surfaces;
}
void ValidateCausalGpuUnits()
{
    Require(CausalRgbEqual({1, 2, 3}, {1, 2, 3}), "RGB identity rejected");
    Require(!CausalRgbEqual({0, 0, 0}, {1e-5f, 0, 0}), "Zero fault accepted");
    Require(!CausalRgbEqual({NAN, 0, 0}, {0, 0, 0}), "NaN accepted");
    Require(DecodeCausalIdentity(EncodeCausalIdentity(17)) == 17, "Identity conversion differs");
    std::cout << "CAUSAL_GPU_UNIT checks=4 pass=1\n";
}
void ValidateCausalGpu(
    const char *Driver,
    const std::string &Shaders,
    const std::string &NativeDirectory,
    const std::string &Directory)
{
    std::ifstream Input(NativeDirectory + "/FacetCausalMutations.f32", std::ios::binary);
    Input.exceptions(std::ios::badbit | std::ios::failbit);
    std::vector<CausalQuartet> Native(36 * 18);
    Input.read(reinterpret_cast<char *>(Native.data()), Native.size() * 16);
    Require(Input.peek() == std::ifstream::traits_type::eof(), "Native mutation size differs");
    std::filesystem::create_directories(Directory);
    std::vector<CausalQuartet> Captures;
    double                     MaximumRgb = 0, MaximumCoordinate = 0;
    uint32_t                   Faults = 0, Executions = 0;
    for (uint32_t Mutation = 0; Mutation < 36; ++Mutation)
    {
        const auto    *Row     = Native.data() + 18 * Mutation;
        const uint32_t Fixture = uint32_t(Row[0][0]), Operation = uint32_t(Row[0][2]);
        const auto     ExpectedRgb = CausalTriplet(Row[10]);
        for (uint32_t Variant = 0; Variant < 3; ++Variant)
        {
            auto     Scene       = ConstructDeformedAreaScene(Fixture);
            uint32_t SourceIndex = uint32_t(Row[1][1]);
            if (Variant != 0)
            {
                LightComputeFacet Blocker{};
                Blocker.A = Row[7];
                Blocker.B = Row[8];
                Blocker.C = Row[9];
                TraceVector First{}, Second{};
                for (uint32_t Axis = 0; Axis < 3; ++Axis)
                {
                    First[Axis]  = double(Blocker.B[Axis]) - Blocker.A[Axis];
                    Second[Axis] = double(Blocker.C[Axis]) - Blocker.A[Axis];
                }
                const auto Normal = TraceNormalize(TraceCross(First, Second));
                for (uint32_t Axis = 0; Axis < 3; ++Axis)
                    Blocker.NormalA[Axis] = Blocker.NormalB[Axis] = Blocker.NormalC[Axis] = float(Normal[Axis]);
                Scene.Surfaces.push_back(Blocker);
            }
            std::vector<std::array<SpatialVector, 3>> Corners;
            for (const auto &Facet : Scene.Surfaces)
                Corners.push_back({SpatialVector{Facet.A[0], Facet.A[1], Facet.A[2]},
                                   SpatialVector{Facet.B[0], Facet.B[1], Facet.B[2]},
                                   SpatialVector{Facet.C[0], Facet.C[1], Facet.C[2]}});
            TriangleVisibilitySpace Reference;
            Reference.Build(Corners);
            Scene.Visibility    = Reference.PackFloat32();
            const auto Original = Scene.Surfaces;
            Scene.Surfaces.clear();
            for (uint32_t Packed = 0; Packed < Scene.Visibility.OriginalFacets.size(); ++Packed)
            {
                const auto Ordinal = Scene.Visibility.OriginalFacets[Packed];
                Scene.Surfaces.push_back(Original[Ordinal]);
                if (Ordinal == uint32_t(Row[1][1]))
                    SourceIndex = Packed;
            }
            Scene.Width = Scene.Height = 1;
            Scene.Receivers.resize(1);
            auto &Receiver           = Scene.Receivers[0];
            Receiver.ViewMetallic    = Row[5];
            Receiver.NormalRoughness = Row[6];
            Receiver.PositionHit     = {EncodeCausalIdentity(SourceIndex),
                                        EncodeCausalIdentity(Variant == 2 ? Operation + 1 : 0), 0, 0};
            Receiver.AlbedoIor       = {Row[1][2], Row[1][3], Row[2][1], 0};
            FacetAreaTransport Transport;
            Require(Transport.Prepare(CausalShading(Scene), Scene.Visibility), "Native scene rejected");
            const auto        Primary = Transport.Primary(CausalTriplet(Row[5]), CausalTriplet(Row[6]));
            FacetAreaProposal Proposal;
            Proposal.Accepted = true;
            Proposal.Index    = SourceIndex;
            Proposal.U        = Row[1][2];
            Proposal.V        = Row[1][3];
            Proposal.Density  = Row[2][0];
            const auto Source = Transport.Source(Proposal);
            const auto Path   = Transport.Connect(Proposal, Primary);
            Require(Path.Accepted && Source.Accepted, "Native path rejected");
            const CausalRay Light{.8f, 0, .6f};
            for (uint32_t Which = 0; Which < 2; ++Which)
            {
                const auto Direction = Which == 0 ? Path.Direction : Light;
                const bool Visible   = !Reference.Occluded({Path.Origin[0], Path.Origin[1], Path.Origin[2]},
                                                           {Direction[0], Direction[1], Direction[2]}, .00001f,
                                                         Which == 0 ? Path.Maximum : 100);
                Require(Visible == (Which == 0 ? Path.ConnectionVisible : Path.SuffixVisible),
                        "Double visibility differs");
            }
            LightComputeSubmission Compute(Driver);
            Compute.Prepare(Scene, Shaders, false, false, true, false, false, false, false, 0, 32);
            if (Executions == 0)
                std::cout << "CAUSAL_GPU_DEVICE name=" << Compute.Properties.deviceName
                          << " type=" << Compute.Properties.deviceType << " driver_sanitized=0 leaks=0\n";
            Compute.ExecuteDiffuse(Scene, 1, 17, false, false, true);
            const auto *Storage = static_cast<const CausalQuartet *>(Compute.Buffers[6].Address);
            Require(DecodeCausalIdentity(Storage[6][0]) == Primary.Index &&
                        DecodeCausalIdentity(Storage[13][0]) == SourceIndex,
                    "GPU facet identity differs");
            const std::array<CausalRay, 9> Coordinates{Primary.Position, Primary.Normal, Primary.Albedo,
                                                       Source.Position,  Source.Normal,  Source.Albedo,
                                                       Path.Origin,      Path.Direction, Primary.Strike};
            const std::array<uint32_t, 9>  Slabs{3, 4, 5, 7, 8, 9, 10, 11, 6};
            for (uint32_t Entry = 0; Entry < 9; ++Entry)
                for (uint32_t Axis = 0; Axis < 3; ++Axis)
                {
                    const float  Observed = Storage[Slabs[Entry]][Entry == 8 ? Axis + 1 : Axis];
                    const double Error    = std::abs(double(Observed) - Coordinates[Entry][Axis]);
                    Require(std::isfinite(Observed) && Error <= 2e-5, "GPU coordinate differs");
                    MaximumCoordinate = std::max(MaximumCoordinate, Error);
                }
            Require(std::abs(Storage[10][3] - Path.Maximum) <= 2e-5, "GPU ray range differs");
            Require(Storage[12] == CausalQuartet{float(Path.ConnectionVisible), float(Path.SuffixVisible), 1, 1},
                    "GPU mask/work differs");
            Require(Storage[13][1] == Proposal.U && Storage[13][2] == Proposal.V && Storage[13][3] == Row[2][1],
                    "GPU selected proposal differs");
            CausalRay NativeRgb{}, OmitConnection{}, OmitSuffix{};
            for (uint32_t Axis = 0; Axis < 3; ++Axis)
            {
                NativeRgb[Axis]      = Path.Rgb[Axis] * Row[2][1];
                OmitConnection[Axis] = Path.SuffixOnly[Axis] * Row[2][1];
                OmitSuffix[Axis]     = Path.ConnectionOnly[Axis] * Row[2][1];
            }
            if (Variant == 0)
                Require(CausalRgbEqual(NativeRgb, ExpectedRgb) && CausalTriplet(Storage[0]) != CausalRay{},
                        "Baseline contribution differs");
            if (Variant == 1)
                Require(IsGpuCausalOcclusion(Storage, ExpectedRgb, Operation), "GPU causal mutation failed");
            if (Variant == 2)
            {
                Require(!IsGpuCausalOcclusion(Storage, ExpectedRgb, Operation), "GPU shader bypass fault escaped");
                ++Faults;
                NativeRgb = Operation == 0 ? OmitConnection : OmitSuffix;
            }
            for (uint32_t Slab = 0; Slab < 3; ++Slab)
            {
                const auto &Expected = Slab == 0 ? NativeRgb : Slab == 1 ? OmitConnection : OmitSuffix;
                Require(CausalRgbEqual(CausalTriplet(Storage[Slab]), Expected), "GPU RGB differs");
                for (uint32_t Axis = 0; Axis < 3; ++Axis)
                    MaximumRgb = std::max(MaximumRgb, double(std::abs(Storage[Slab][Axis] - Expected[Axis])));
            }
            Captures.insert(Captures.end(), Storage, Storage + 16);
            ++Executions;
        }
        std::cout << "CAUSAL_GPU_CASE mutation=" << Mutation << " executions=3 pass=1\n";
    }
    std::ofstream Output(Directory + "/FacetCausalGpu.f32", std::ios::binary);
    Output.exceptions(std::ios::badbit | std::ios::failbit);
    Output.write(reinterpret_cast<const char *>(Captures.data()), Captures.size() * 16);
    Require(Executions == 108 && Faults == 36, "GPU totals differ");
    std::cout << std::setprecision(17) << "CAUSAL_GPU_TOTAL executions=" << Executions
              << " visibility=" << 2 * Executions << " faults_rejected=" << Faults << " rgb_error=" << MaximumRgb
              << " coordinate_error=" << MaximumCoordinate << " pass=1\n";
}
} // namespace
int main(int Count, char **Arguments)
{
    try
    {
        if (Count == 2 && std::strcmp(Arguments[1], "--unit") == 0)
        {
            ValidateCausalGpuUnits();
            return 0;
        }
        Require(Count == 5, "Use driver shaders native-captures output-directory or --unit");
        ValidateCausalGpu(Arguments[1], Arguments[2], Arguments[3], Arguments[4]);
        return 0;
    }
    catch (const std::exception &Error)
    {
        std::cerr << Error.what() << '\n';
        return 1;
    }
}
