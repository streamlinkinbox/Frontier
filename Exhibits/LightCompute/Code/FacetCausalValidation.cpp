/*============================================================================================================================================
                                                       FACETCAUSALVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Attribute historical deformed transport blind spots and verify geometry-conditioned visibility fault rejection.
#define main HistoricalTransportEntry
#include "FacetAreaTransportValidation.cpp"
#undef main
#include <algorithm>

//------------------------------------------------------------------------------------------------------------------------
//                                                    PATH ENERGY ATTRIBUTION
//------------------------------------------------------------------------------------------------------------------------
namespace
{
using CausalVector = std::array<float, 3>;
struct CausalSelection
{
    bool              Present = false;
    uint32_t          Packet = 0, Lane = 0;
    FacetAreaProposal Proposal;
    FacetAreaSurface  Primary;
    FacetAreaPath     Path;
    CausalVector      CameraOrigin{}, CameraDirection{};
    float             Weight = 0;
};
std::array<std::array<CausalSelection, 6>, 3> CausalSelections;
// [fixture,method,operation] -> blocked,zero proxy,positive proxy hidden by opposite gate,restorable.
std::array<std::array<std::array<std::array<uint64_t, 8>, 2>, 6>, 3> Attribution{};
float                                                                MaximumChannel(const CausalVector &Rgb)
{
    return std::max({Rgb[0], Rgb[1], Rgb[2]});
}
void AttributeCausalPath(
    uint32_t Fixture,
    uint32_t Method,
    uint32_t Packet,
    uint32_t Lane,
    const FacetAreaProposal &Proposal,
    const FacetAreaSurface &Primary,
    const FacetAreaPath &Path,
    const FacetAreaScore &Score,
    const CausalVector &CameraOrigin,
    const CausalVector &CameraDirection,
    float Weight)
{
    for (uint32_t Operation = 0; Operation < 2; ++Operation)
    {
        const bool Visible  = Operation == 0 ? Path.ConnectionVisible : Path.SuffixVisible;
        const bool Opposite = Operation == 0 ? Path.SuffixVisible : Path.ConnectionVisible;
        if (Visible)
            continue;
        auto &Counts = Attribution[Fixture][Method][Operation];
        ++Counts[0];
        ++Counts[MaximumChannel(Score.ProxyRgb) == 0 ? 1 : !Opposite ? 2 : 3];
        if (MaximumChannel(Score.ProxyRgb) == 0)
            ++Counts[4 + (Score.SourceProjection == 0 ? 1 : 0) + (Score.ReceiverProjection == 0 ? 2 : 0)];
    }
    auto      &Selection = CausalSelections[Fixture][Method];
    const auto Cross =
        TraceCross(TraceVector{Path.Direction[0], Path.Direction[1], Path.Direction[2]}, TraceVector{.8f, 0, .6f});
    if (!Selection.Present && Path.ConnectionVisible && Path.SuffixVisible && MaximumChannel(Path.Rgb) > 1e-10f &&
        std::sqrt(TraceDot(Cross, Cross)) > .1)
        Selection = {true, Packet, Lane, Proposal, Primary, Path, CameraOrigin, CameraDirection, Weight};
}
#include "FacetCausalReplay.h"

//------------------------------------------------------------------------------------------------------------------------
//                                                  CAUSAL GEOMETRY MUTATIONS
//------------------------------------------------------------------------------------------------------------------------
bool IsCausalOcclusion(const FacetAreaPath &Original, const FacetAreaPath &Mutated, uint32_t Operation)
{
    const CausalVector Zero{};
    if (!Original.Accepted || !Mutated.Accepted || !Original.ConnectionVisible || !Original.SuffixVisible ||
        MaximumChannel(Original.Rgb) <= 1e-10f || Mutated.Rgb != Zero)
        return false;
    if (Operation == 0)
        return !Mutated.ConnectionVisible && Mutated.SuffixVisible && Mutated.SuffixOnly == Original.Rgb &&
               Mutated.ConnectionOnly == Zero;
    return Mutated.ConnectionVisible && !Mutated.SuffixVisible && Mutated.ConnectionOnly == Original.Rgb &&
           Mutated.SuffixOnly == Zero;
}
double DistanceToRay(const TraceVector &Point, const CausalVector &Origin, const CausalVector &Direction)
{
    TraceVector Delta{};
    for (uint32_t Axis = 0; Axis < 3; ++Axis)
        Delta[Axis] = Point[Axis] - Origin[Axis];
    const TraceVector Vector{Direction[0], Direction[1], Direction[2]};
    const double      Travel = std::max(0., TraceDot(Delta, Vector) / TraceDot(Vector, Vector));
    for (uint32_t Axis = 0; Axis < 3; ++Axis)
        Delta[Axis] -= Travel * Vector[Axis];
    return std::sqrt(TraceDot(Delta, Delta));
}
void EncodeCausalMutations(const std::string &Directory)
{
    std::vector<Quartet> Captures;
    uint32_t             Mutations = 0, FaultRejections = 0;
    for (uint32_t Fixture = 0; Fixture < 3; ++Fixture)
        for (uint32_t Method = 0; Method < 6; ++Method)
            for (uint32_t Operation = 0; Operation < 2; ++Operation)
            {
                const auto &Selection = CausalSelections[Fixture][Method];
                Check(Selection.Present);
                auto               Scene = ConstructDeformedAreaScene(Fixture);
                FacetAreaTransport OriginalTransport;
                Check(OriginalTransport.Prepare(Shading(Scene), Scene.Visibility));
                const auto         OriginalSource = OriginalTransport.Source(Selection.Proposal);
                const CausalVector Light{.8f, 0, .6f};
                const auto         Target   = Operation == 0 ? Selection.Path.Direction : Light;
                const auto         Opposite = Operation == 0 ? Light : Selection.Path.Direction;
                const float        Travel   = std::min(.125f, .125f * Selection.Path.Maximum);
                TraceVector        Centre{};
                for (uint32_t Axis = 0; Axis < 3; ++Axis)
                    Centre[Axis] = double(Selection.Path.Origin[Axis]) + Travel * Target[Axis];
                const float Radius =
                    float(std::min({.01, .05 * DistanceToRay(Centre, Selection.Path.Origin, Opposite),
                                    .05 * DistanceToRay(Centre, Selection.CameraOrigin, Selection.CameraDirection)}));
                Check(Radius > 1e-6f);
                const auto        Normal = TraceNormalize(TraceVector{Target[0], Target[1], Target[2]});
                const TraceVector Axis   = std::abs(Normal[2]) < .9 ? TraceVector{0, 0, 1} : TraceVector{0, 1, 0};
                const auto        First  = TraceNormalize(TraceCross(Normal, Axis));
                const auto        Second = TraceCross(Normal, First);
                LightComputeFacet Triangle{};
                for (uint32_t Component = 0; Component < 3; ++Component)
                {
                    Triangle.A[Component] = float(Centre[Component] + Radius * First[Component]);
                    Triangle.B[Component] = float(
                        Centre[Component] + Radius * (-.5 * First[Component] + std::sqrt(.75) * Second[Component]));
                    Triangle.C[Component] = float(
                        Centre[Component] + Radius * (-.5 * First[Component] - std::sqrt(.75) * Second[Component]));
                    Triangle.NormalA[Component] = Triangle.NormalB[Component] = Triangle.NormalC[Component] =
                        float(Normal[Component]);
                }
                Scene.Surfaces.push_back(Triangle);
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
                auto     Proposal     = Selection.Proposal;
                uint32_t PrimaryIndex = UINT32_MAX;
                for (uint32_t Packed = 0; Packed < Scene.Visibility.OriginalFacets.size(); ++Packed)
                {
                    const auto Ordinal = Scene.Visibility.OriginalFacets[Packed];
                    Scene.Surfaces.push_back(Original[Ordinal]);
                    if (Ordinal == Selection.Proposal.Index)
                        Proposal.Index = Packed;
                    if (Ordinal == Selection.Primary.Index)
                        PrimaryIndex = Packed;
                }
                FacetAreaTransport Transport;
                Check(Transport.Prepare(Shading(Scene), Scene.Visibility));
                const auto Primary = Transport.Primary(Selection.CameraOrigin, Selection.CameraDirection);
                Check(Primary.Accepted && Primary.Index == PrimaryIndex &&
                      Primary.Position == Selection.Primary.Position && Primary.Normal == Selection.Primary.Normal &&
                      Primary.Albedo == Selection.Primary.Albedo && Primary.Strike == Selection.Primary.Strike);
                const auto Source = Transport.Source(Proposal);
                Check(Source.Accepted && OriginalSource.Accepted && Source.Position == OriginalSource.Position &&
                      Source.Normal == OriginalSource.Normal && Source.Albedo == OriginalSource.Albedo);
                const auto Path = Transport.Connect(Proposal, Primary);
                Check(Path.Origin == Selection.Path.Origin && Path.Direction == Selection.Path.Direction &&
                      Path.Maximum == Selection.Path.Maximum);
                Check(IsCausalOcclusion(Selection.Path, Path, Operation));
                ++Mutations;
                for (uint32_t Which = 0; Which < 2; ++Which)
                {
                    const auto  Direction = Which == 0 ? Path.Direction : Light;
                    const float Maximum   = Which == 0 ? Path.Maximum : 100;
                    const bool  DoubleVisible =
                        !Reference.Occluded(Spatial(Path.Origin), Spatial(Direction), .00001f, Maximum);
                    const bool BruteVisible =
                        Brute(Scene, Path.Origin, Direction, .00001f, Maximum).Index == UINT32_MAX;
                    Check(DoubleVisible == BruteVisible &&
                          DoubleVisible == (Which == 0 ? Path.ConnectionVisible : Path.SuffixVisible));
                }
                auto Bypassed = Path;
                Bypassed.Rgb  = Operation == 0 ? Path.SuffixOnly : Path.ConnectionOnly;
                Check(!IsCausalOcclusion(Selection.Path, Bypassed, Operation));
                ++FaultRejections;
                const size_t Offset = Captures.size();
                Captures.resize(Offset + 18);
                Captures[Offset]     = {float(Fixture), float(Method), float(Operation), float(Selection.Packet)};
                Captures[Offset + 1] = {float(Selection.Lane), float(Selection.Proposal.Index), Proposal.U, Proposal.V};
                Captures[Offset + 2] = {Proposal.Density, Selection.Weight, Radius, Travel};
                for (uint32_t Component = 0; Component < 3; ++Component)
                {
                    Captures[Offset + 3][Component]  = Path.Origin[Component];
                    Captures[Offset + 4][Component]  = Path.Direction[Component];
                    Captures[Offset + 5][Component]  = Selection.CameraOrigin[Component];
                    Captures[Offset + 6][Component]  = Selection.CameraDirection[Component];
                    Captures[Offset + 7][Component]  = Triangle.A[Component];
                    Captures[Offset + 8][Component]  = Triangle.B[Component];
                    Captures[Offset + 9][Component]  = Triangle.C[Component];
                    Captures[Offset + 10][Component] = Selection.Path.Rgb[Component] * Selection.Weight;
                    Captures[Offset + 11][Component] = Path.Rgb[Component] * Selection.Weight;
                    Captures[Offset + 12][Component] =
                        (Operation == 0 ? Path.SuffixOnly[Component] : Path.ConnectionOnly[Component]) *
                        Selection.Weight;
                    Captures[Offset + 13][Component] =
                        (Operation == 0 ? Path.ConnectionOnly[Component] : Path.SuffixOnly[Component]) *
                        Selection.Weight;
                    Captures[Offset + 15][Component] = Primary.Position[Component];
                    Captures[Offset + 16][Component] = Primary.Normal[Component];
                    Captures[Offset + 17][Component] = Primary.Albedo[Component];
                }
                Captures[Offset + 3][3] = Path.Maximum;
                Captures[Offset + 14]   = {float(Path.ConnectionVisible), float(Path.SuffixVisible),
                                           float(Operation == 1), float(Operation == 0)};
                std::cout << "FACET_CAUSAL_MUTATION fixture=" << Fixture << " method=" << Method
                          << " operation=" << Operation << " packet=" << Selection.Packet << " lane=" << Selection.Lane
                          << " pass=1\n";
            }
    std::ofstream Buffer(Directory + "/FacetCausalMutations.f32", std::ios::binary);
    Buffer.exceptions(std::ios::badbit | std::ios::failbit);
    Buffer.write(reinterpret_cast<const char *>(Captures.data()), Captures.size() * sizeof(Quartet));
    std::ofstream Table(Directory + "/FacetCausalAttribution.csv");
    Table.exceptions(std::ios::badbit | std::ios::failbit);
    Table << "Fixture,Method,Operation,Blocked,ZeroProxy,OtherBlocked,Restorable,NeitherProjectionZero,SourceZeroOnly,"
             "ReceiverZeroOnly,BothProjectionsZero\n";
    for (uint32_t Fixture = 0; Fixture < 3; ++Fixture)
        for (uint32_t Method = 0; Method < 6; ++Method)
            for (uint32_t Operation = 0; Operation < 2; ++Operation)
            {
                const auto &Counts = Attribution[Fixture][Method][Operation];
                Check(Counts[0] == Counts[1] + Counts[2] + Counts[3]);
                Table << Fixture << ',' << Method << ',' << Operation;
                for (auto Count : Counts)
                    Table << ',' << Count;
                Table << '\n';
            }
    Check(Mutations == 36 && FaultRejections == 36);
    std::cout << "FACET_CAUSAL_TOTAL mutations=" << Mutations << " fault_rejections=" << FaultRejections << " pass=1\n";
}
void ValidateCausalUnits()
{
    FacetAreaPath Original;
    Original.Accepted = Original.ConnectionVisible = Original.SuffixVisible = true;
    Original.Rgb                                                            = {1, 2, 3};
    uint32_t Count                                                          = 0;
    auto     Confirm                                                        = [&](bool Condition)
    {
        ++Count;
        Check(Condition);
    };
    for (uint32_t Operation = 0; Operation < 2; ++Operation)
    {
        auto Mutated              = Original;
        Mutated.Rgb               = {};
        Mutated.ConnectionVisible = Operation == 1;
        Mutated.SuffixVisible     = Operation == 0;
        Mutated.SuffixOnly        = Operation == 0 ? Original.Rgb : CausalVector{};
        Mutated.ConnectionOnly    = Operation == 1 ? Original.Rgb : CausalVector{};
        Confirm(IsCausalOcclusion(Original, Mutated, Operation));
        auto Bypassed = Mutated;
        Bypassed.Rgb  = Original.Rgb;
        Confirm(!IsCausalOcclusion(Original, Bypassed, Operation));
        auto Wrong              = Mutated;
        Wrong.ConnectionVisible = Wrong.SuffixVisible = false;
        Confirm(!IsCausalOcclusion(Original, Wrong, Operation));
    }
    Confirm(DistanceToRay({0, 1, 0}, {0, 0, 0}, {1, 0, 0}) == 1);
    Confirm(DistanceToRay({-1, 0, 0}, {0, 0, 0}, {1, 0, 0}) == 1);
    std::cout << "FACET_CAUSAL_UNIT checks=" << Count << " pass=1\n";
}
} // namespace
int main(int Count, char **Arguments)
{
    try
    {
        if (Count != 2)
            throw std::runtime_error("Use --unit or output directory");
        if (std::strcmp(Arguments[1], "--unit") == 0)
            ValidateCausalUnits();
        else
        {
            CaptureCausal(Arguments[1]);
            EncodeCausalMutations(Arguments[1]);
        }
        return 0;
    }
    catch (const std::exception &Error)
    {
        std::cerr << Error.what() << '\n';
        return 1;
    }
}
