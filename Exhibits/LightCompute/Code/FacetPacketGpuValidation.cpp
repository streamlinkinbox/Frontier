/*============================================================================================================================================
                                                     FACETPACKETGPUVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Audit GPU-generated deformed packets against independently assembled native proposal and transport calls.
#include "Engine/Rendering/FacetAreaReservoir.h"
#include "FacetPacketGpuComparisons.h"

namespace
{
uint64_t PacketDraws = 0, PacketPaths = 0, PacketPairs = 0;
double   PacketRgbError = 0, PacketCoordinateError = 0, PacketScalarError = 0;
bool     PacketScalarEqual(float Observed, float Expected)
{
    return std::isfinite(Observed) && std::isfinite(Expected) &&
           std::abs(double(Observed) - Expected) <= 2e-5 * std::max(1., double(std::abs(Expected)));
}
bool IsPacketCausal(
    const CausalQuartet *Storage,
    uint32_t Slot,
    const CausalRay &Original,
    uint32_t Operation)
{
    const auto     Correct = CausalTriplet(Storage[20 + Slot]);
    const auto     Restore = CausalTriplet(Storage[(Operation == 0 ? 28 : 36) + Slot]);
    const auto     Wrong   = CausalTriplet(Storage[(Operation == 0 ? 36 : 28) + Slot]);
    const uint32_t Mask    = DecodeCausalIdentity(Storage[52 + Slot][3]) >> 16;
    return Correct == CausalRay{} && Restore != CausalRay{} && CausalRgbEqual(Restore, Original) &&
           Wrong == CausalRay{} && Mask == (Operation == 0 ? 1u : 2u);
}
std::vector<CausalQuartet> AssemblePacketReference(
    const LightComputeArchive &Scene,
    const std::vector<uint32_t> &Profile,
    const FacetAreaProposals &Proposals,
    TriangleVisibilitySpace &Reference,
    uint32_t Seed,
    uint32_t Method,
    uint32_t Fault)
{
    FacetAreaTransport Transport;
    const auto         Surfaces = CausalShading(Scene);
    Require(Transport.Prepare(Surfaces, Scene.Visibility), "Native packet scene rejected");
    const auto &Camera  = Scene.Receivers[0];
    const auto  Primary = Transport.Primary(CausalTriplet(Camera.ViewMetallic), CausalTriplet(Camera.NormalRoughness));
    Require(Primary.Accepted, "Native packet primary rejected");
    std::vector<CausalQuartet> Output(80);
    for (uint32_t Axis = 0; Axis < 3; ++Axis)
    {
        Output[4][Axis]     = Primary.Position[Axis];
        Output[5][Axis]     = Primary.Normal[Axis];
        Output[6][Axis]     = Primary.Albedo[Axis];
        Output[7][Axis + 1] = Primary.Strike[Axis];
    }
    Output[7][0]  = EncodeCausalIdentity(Primary.Index);
    uint32_t Slot = 0, Draws = 0;
    for (uint32_t Lane = 0; Lane < 4; ++Lane)
    {
        const uint32_t                   CandidateCount = Method == 2 || Method == 3 ? 1 : 2;
        std::array<FacetAreaProposal, 2> Candidates{};
        std::array<FacetAreaScore, 2>    Scores{};
        for (uint32_t Ordinal = 0; Ordinal < CandidateCount; ++Ordinal)
        {
            auto &Candidate = Candidates[Ordinal];
            Candidate       = Proposals.Draw(Seed, 2 * Lane + Ordinal, FacetAreaMeasureCategory(Method % 2));
            Require(Candidate.Accepted, "Native candidate rejected");
            Candidate.Index   = Profile[Candidate.Index];
            const auto Source = Transport.Source(Candidate);
            Require(Source.Accepted, "Native source rejected");
            FacetAreaScoreInput Input;
            Input.Triangle       = Surfaces[Candidate.Index].Triangle;
            Input.U              = Candidate.U;
            Input.V              = Candidate.V;
            Input.SourceNormal   = Source.Normal;
            Input.SourceAlbedo   = Source.Albedo;
            Input.Receiver       = Primary.Position;
            Input.ReceiverNormal = Primary.Normal;
            Input.ReceiverAlbedo = Primary.Albedo;
            Scores[Ordinal]      = EvaluateFacetAreaScore(Input);
            Require(Scores[Ordinal].Accepted, "Native score rejected");
            Output[8 + 2 * Lane + Ordinal] = {EncodeCausalIdentity(Candidate.Index), Candidate.U, Candidate.V,
                                              Candidate.Density};
            ++Draws;
            ++PacketDraws;
        }
        uint32_t Selected   = 0;
        float    Normalizer = 0;
        if (Method < 2)
        {
            const auto Pair = SelectFacetAreaPair(
                {{{Scores[0].Target, Candidates[0].Density}, {Scores[1].Target, Candidates[1].Density}}},
                DrawFacetAreaSelectionUniform(Seed, Lane));
            Require(Pair.Accepted, "Native pair rejected");
            Selected   = Pair.Selected;
            Normalizer = float(Pair.Normalizer);
            ++PacketPairs;
        }
        Output[16 + Lane] = {Scores[0].Target, Scores[1].Target, Normalizer, float(Selected)};
        for (uint32_t Ordinal = 0; Ordinal < CandidateCount; ++Ordinal)
        {
            if (Method < 2 && Ordinal != Selected)
                continue;
            const auto &Candidate = Candidates[Ordinal];
            const auto  Path      = Transport.Connect(Candidate, Primary);
            Require(Path.Accepted, "Native path rejected");
            for (uint32_t Which = 0; Which < 2; ++Which)
            {
                const auto Direction = Which == 0 ? Path.Direction : CausalRay{.8f, 0, .6f};
                const bool Visible   = !Reference.Occluded({Path.Origin[0], Path.Origin[1], Path.Origin[2]},
                                                           {Direction[0], Direction[1], Direction[2]}, .00001f,
                                                         Which == 0 ? Path.Maximum : 100);
                Require(Visible == (Which == 0 ? Path.ConnectionVisible : Path.SuffixVisible),
                        "Double packet visibility differs");
            }
            const float Weight = (Method < 2 ? Normalizer : 1 / Candidate.Density) * (Method >= 4 ? .5f : 1.f) / 4;
            for (uint32_t Axis = 0; Axis < 3; ++Axis)
            {
                const float WithoutConnection = Path.SuffixOnly[Axis] * Weight,
                            WithoutSuffix     = Path.ConnectionOnly[Axis] * Weight;
                const float Correct           = (Fault == 1   ? Path.SuffixOnly[Axis]
                                                 : Fault == 2 ? Path.ConnectionOnly[Axis]
                                                              : Path.Rgb[Axis]) *
                                      Weight;
                const float Proxy       = Scores[Ordinal].ProxyRgb[Axis] * Weight;
                Output[20 + Slot][Axis] = Correct;
                Output[28 + Slot][Axis] = WithoutConnection;
                Output[36 + Slot][Axis] = WithoutSuffix;
                Output[44 + Slot][Axis] = Path.Origin[Axis];
                Output[52 + Slot][Axis] = Path.Direction[Axis];
                Output[61 + Slot][Axis] = Proxy;
                Output[0][Axis] += Correct;
                Output[1][Axis] += WithoutConnection;
                Output[2][Axis] += WithoutSuffix;
                Output[3][Axis] += Proxy;
            }
            Output[44 + Slot][3] = Path.Maximum;
            Output[52 + Slot][3] = EncodeCausalIdentity(Candidate.Index | (Path.ConnectionVisible ? 1u << 17 : 0) |
                                                        (Path.SuffixVisible ? 1u << 16 : 0));
            Output[69 + Slot]    = {float(Lane), float(Ordinal), Weight, 0};
            ++Slot;
            ++PacketPaths;
        }
    }
    Output[60] = {float(Draws), float(Slot), float(Slot), float(Slot)};
    return Output;
}
void ComparePacketReadback(const CausalQuartet *Actual, const CausalQuartet *Expected)
{
    Require(DecodeCausalIdentity(Actual[7][0]) == DecodeCausalIdentity(Expected[7][0]), "GPU primary identity differs");
    Require(Actual[60] == Expected[60], "GPU packet work differs");
    for (uint32_t Slab = 4; Slab < 8; ++Slab)
        for (uint32_t Axis = Slab == 7 ? 1 : 0; Axis < 4; ++Axis)
        {
            const double Error = std::abs(double(Actual[Slab][Axis]) - Expected[Slab][Axis]);
            Require(std::isfinite(Actual[Slab][Axis]) && Error <= 2e-5, "GPU primary differs");
            PacketCoordinateError = std::max(PacketCoordinateError, Error);
        }
    for (uint32_t Lane = 0; Lane < 4; ++Lane)
    {
        Require(Actual[16 + Lane][3] == Expected[16 + Lane][3], "GPU selected ordinal differs");
        for (uint32_t Ordinal = 0; Ordinal < 2; ++Ordinal)
        {
            const auto &Observed = Actual[8 + 2 * Lane + Ordinal];
            const auto &Native   = Expected[8 + 2 * Lane + Ordinal];
            Require(DecodeCausalIdentity(Observed[0]) == DecodeCausalIdentity(Native[0]) && Observed[1] == Native[1] &&
                        Observed[2] == Native[2],
                    "GPU generated proposal differs");
            Require(PacketScalarEqual(Observed[3], Native[3]), "GPU PDF differs");
        }
        for (uint32_t Axis = 0; Axis < 3; ++Axis)
        {
            Require(PacketScalarEqual(Actual[16 + Lane][Axis], Expected[16 + Lane][Axis]),
                    "GPU target/normalizer differs");
            PacketScalarError =
                std::max(PacketScalarError, std::abs(double(Actual[16 + Lane][Axis]) - Expected[16 + Lane][Axis]) /
                                                std::max(1., double(std::abs(Expected[16 + Lane][Axis]))));
        }
    }
    for (uint32_t Slab : std::array<uint32_t, 4>{0, 1, 2, 3})
    {
        Require(CausalRgbEqual(CausalTriplet(Actual[Slab]), CausalTriplet(Expected[Slab])), "GPU packet RGB differs");
        for (uint32_t Axis = 0; Axis < 3; ++Axis)
            PacketRgbError = std::max(PacketRgbError, std::abs(double(Actual[Slab][Axis]) - Expected[Slab][Axis]));
    }
    for (uint32_t Slot = 0; Slot < 8; ++Slot)
    {
        Require(Actual[69 + Slot][0] == Expected[69 + Slot][0] && Actual[69 + Slot][1] == Expected[69 + Slot][1] &&
                    PacketScalarEqual(Actual[69 + Slot][2], Expected[69 + Slot][2]),
                "GPU lane/path weight differs");
        Require(DecodeCausalIdentity(Actual[52 + Slot][3]) == DecodeCausalIdentity(Expected[52 + Slot][3]),
                "GPU path identity/visibility differs");
        for (uint32_t Slab : std::array<uint32_t, 4>{20 + Slot, 28 + Slot, 36 + Slot, 61 + Slot})
        {
            Require(CausalRgbEqual(CausalTriplet(Actual[Slab]), CausalTriplet(Expected[Slab])),
                    "GPU weighted path differs");
            for (uint32_t Axis = 0; Axis < 3; ++Axis)
                PacketRgbError = std::max(PacketRgbError, std::abs(double(Actual[Slab][Axis]) - Expected[Slab][Axis]));
        }
        for (uint32_t Slab : std::array<uint32_t, 2>{44 + Slot, 52 + Slot})
            for (uint32_t Axis = 0; Axis < (Slab == 52 + Slot ? 3u : 4u); ++Axis)
            {
                const double Error = std::abs(double(Actual[Slab][Axis]) - Expected[Slab][Axis]);
                Require(std::isfinite(Actual[Slab][Axis]) && Error <= 2e-5, "GPU path ray differs");
                PacketCoordinateError = std::max(PacketCoordinateError, Error);
            }
    }
}
void ValidatePacketGpuUnits()
{
    Require(PacketScalarEqual(1, 1.000001f), "Scalar tolerance rejected");
    Require(!PacketScalarEqual(NAN, 0), "Invalid scalar accepted");
    Require(!PacketScalarEqual(0, 1), "Bad scalar accepted");
    Require(!IsPacketCausal(std::vector<CausalQuartet>(80).data(), 0, {1, 1, 1}, 0), "Empty path accepted");
    for (uint32_t Operation = 0; Operation < 2; ++Operation)
    {
        std::array<CausalQuartet, 80> Output{};
        Output[(Operation == 0 ? 28 : 36)] = {1, 1, 1, 0};
        Output[52][3]                      = EncodeCausalIdentity(Operation == 0 ? 1u << 16 : 1u << 17);
        Require(IsPacketCausal(Output.data(), 0, {1, 1, 1}, Operation), "Valid causal path rejected");
        Output[20] = {1, 1, 1, 0};
        Require(!IsPacketCausal(Output.data(), 0, {1, 1, 1}, Operation), "RGB bypass accepted");
        Output[20]    = {};
        Output[52][3] = 0;
        Require(!IsPacketCausal(Output.data(), 0, {1, 1, 1}, Operation), "Wrong mask accepted");
    }
    std::cout << "PACKET_GPU_UNIT checks=10 pass=1\n";
}
void ValidatePacketGpu(
    const char *Driver,
    const std::string &Shaders,
    const std::string &NativeDirectory,
    const std::string &Directory)
{
    std::ifstream Input(NativeDirectory + "/FacetCausalMutations.f32", std::ios::binary);
    Input.exceptions(std::ios::badbit | std::ios::failbit);
    std::vector<CausalQuartet> Rows(36 * 18);
    Input.read(reinterpret_cast<char *>(Rows.data()), Rows.size() * 16);
    std::array<std::vector<CausalQuartet>, 3> Historical;
    for (uint32_t Fixture = 0; Fixture < 3; ++Fixture)
    {
        Historical[Fixture].resize(41 * 24576);
        std::ifstream File(NativeDirectory + "/NativeAreaTransport2" + std::to_string(Fixture) + ".f32",
                           std::ios::binary);
        File.exceptions(std::ios::badbit | std::ios::failbit);
        File.read(reinterpret_cast<char *>(Historical[Fixture].data()), Historical[Fixture].size() * 16);
    }
    std::filesystem::create_directories(Directory);
    std::vector<CausalQuartet> Captures, References, Qualified;
    uint32_t                   Executions = 0, Faults = 0, Mutations = 0;
    for (uint32_t Case = 0; Case < 36; ++Case)
    {
        const auto    *Row     = Rows.data() + Case * 18;
        const uint32_t Fixture = uint32_t(Row[0][0]), Method = uint32_t(Row[0][1]), Operation = uint32_t(Row[0][2]),
                       Packet               = uint32_t(Row[0][3]);
        const uint32_t                 Seed = HashFacetAreaBits((Packet + 0x5e92b341u) ^ (Fixture * 0x6c8e9cf5u));
        const auto                     OriginalScene   = ConstructDeformedAreaScene(Fixture);
        const auto                     OriginalShading = CausalShading(OriginalScene);
        std::vector<FacetAreaTriangle> SourceTriangles;
        for (const auto &Surface : OriginalShading)
            SourceTriangles.push_back(Surface.Triangle);
        FacetAreaProposals Proposals;
        Require(Proposals.Prepare(SourceTriangles), "Original source profile rejected");
        for (uint32_t Variant = 0; Variant < 3; ++Variant)
        {
            auto Scene = OriginalScene;
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
            std::vector<uint32_t> Profile(16);
            for (uint32_t Packed = 0; Packed < Scene.Visibility.OriginalFacets.size(); ++Packed)
            {
                const uint32_t Ordinal = Scene.Visibility.OriginalFacets[Packed];
                Scene.Surfaces.push_back(Original[Ordinal]);
                if (Ordinal < 16)
                    Profile[Ordinal] = Packed;
            }
            const uint32_t Fault = Variant == 2 ? Operation + 1 : 0;
            Scene.Width          = 5;
            Scene.Height         = 1;
            Scene.Receivers.resize(5);
            Scene.Receivers[0].ViewMetallic    = Row[5];
            Scene.Receivers[0].NormalRoughness = Row[6];
            Scene.Receivers[0].PositionHit     = {EncodeCausalIdentity(Seed), EncodeCausalIdentity(Method),
                                                  EncodeCausalIdentity(Fault), 0};
            for (uint32_t Ordinal = 0; Ordinal < 16; ++Ordinal)
                Scene.Receivers[1 + Ordinal / 4].PositionHit[Ordinal % 4] = EncodeCausalIdentity(Profile[Ordinal]);
            const auto Expected   = AssemblePacketReference(Scene, Profile, Proposals, Reference, Seed, Method, Fault);
            uint32_t   TargetSlot = UINT32_MAX;
            for (uint32_t Slot = 0; Slot < uint32_t(Expected[60][1]); ++Slot)
            {
                const uint32_t Lane = uint32_t(Expected[69 + Slot][0]), Ordinal = uint32_t(Expected[69 + Slot][1]);
                const auto    &Candidate = Expected[8 + 2 * Lane + Ordinal];
                if (Lane == uint32_t(Row[1][0]) && DecodeCausalIdentity(Candidate[0]) == Profile[uint32_t(Row[1][1])] &&
                    Candidate[1] == Row[1][2] && Candidate[2] == Row[1][3])
                    TargetSlot = Slot;
            }
            Require(TargetSlot != UINT32_MAX, "Qualified path disappeared from generated packet");
            if (Variant == 0)
                Require(CausalRgbEqual(CausalTriplet(Expected[0]),
                                       CausalTriplet(Historical[Fixture][Method * 4096 + Packet])),
                        "Native original packet differs from historical RGB");
            LightComputeSubmission Compute(Driver);
            Compute.Prepare(Scene, Shaders, false, false, true, false, false, false, false, 0, 80);
            if (Executions == 0)
                std::cout << "PACKET_GPU_DEVICE name=" << Compute.Properties.deviceName
                          << " type=" << Compute.Properties.deviceType << " driver_sanitized=0 leaks=0\n";
            Compute.ExecuteDiffuse(Scene, 1, 17, false, false, true);
            const auto                   *Storage = static_cast<const CausalQuartet *>(Compute.Buffers[6].Address);
            std::array<CausalQuartet, 80> Actual{};
            for (uint32_t Slab = 0; Slab < 80; ++Slab)
                Actual[Slab] = Storage[5 * Slab];
            ComparePacketReadback(Actual.data(), Expected.data());
            if (Variant == 0)
                Require(CausalRgbEqual(CausalTriplet(Actual[20 + TargetSlot]), CausalTriplet(Row[10])),
                        "GPU original qualified contribution differs");
            if (Variant == 1)
            {
                Require(IsPacketCausal(Actual.data(), TargetSlot, CausalTriplet(Row[10]), Operation),
                        "Generated causal mutation failed");
                ++Mutations;
            }
            if (Variant == 2)
            {
                Require(!IsPacketCausal(Actual.data(), TargetSlot, CausalTriplet(Row[10]), Operation),
                        "Actual shader fault escaped");
                ++Faults;
            }
            Captures.insert(Captures.end(), Actual.begin(), Actual.end());
            References.insert(References.end(), Expected.begin(), Expected.end());
            Qualified.push_back({float(Case), float(Variant), float(TargetSlot), float(Operation)});
            ++Executions;
        }
        std::cout << "PACKET_GPU_CASE case=" << Case << " executions=3 pass=1\n";
    }
    for (const auto &Pair : std::array<std::pair<const char *, const std::vector<CausalQuartet> *>, 3>{
             {{"FacetPacketGpu.f32", &Captures},
              {"FacetPacketNative.f32", &References},
              {"FacetPacketQualified.f32", &Qualified}}})
    {
        std::ofstream File(Directory + "/" + Pair.first, std::ios::binary);
        File.exceptions(std::ios::badbit | std::ios::failbit);
        File.write(reinterpret_cast<const char *>(Pair.second->data()), Pair.second->size() * 16);
    }
    Require(Executions == 108 && PacketDraws == 720 && PacketPaths == 576 && PacketPairs == 144 && Mutations == 36 &&
                Faults == 36,
            "Packet totals differ");
    std::cout << std::setprecision(17) << "PACKET_GPU_TOTAL executions=" << Executions << " candidates=" << PacketDraws
              << " paths=" << PacketPaths << " pairs=" << PacketPairs << " visibility=" << 2 * PacketPaths
              << " mutations=" << Mutations << " faults=" << Faults << " rgb_error=" << PacketRgbError
              << " coordinate_error=" << PacketCoordinateError << " scalar_error=" << PacketScalarError << " pass=1\n";
}
} // namespace
int main(int Count, char **Arguments)
{
    try
    {
        if (Count == 2 && std::strcmp(Arguments[1], "--unit") == 0)
        {
            ValidatePacketGpuUnits();
            return 0;
        }
        Require(Count == 5, "Use driver shaders native-captures output-directory or --unit");
        ValidatePacketGpu(Arguments[1], Arguments[2], Arguments[3], Arguments[4]);
        return 0;
    }
    catch (const std::exception &Error)
    {
        std::cerr << Error.what() << '\n';
        return 1;
    }
}
