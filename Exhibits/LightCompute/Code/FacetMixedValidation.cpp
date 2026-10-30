/*============================================================================================================================================
                                                       FACETMIXEDVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Validate mixed-area diffuse support and partially blocked transport through the unchanged packet API.
#include "Engine/Rendering/FacetAreaPacket.h"
#define main NativeOcclusionEntry
#include "FacetAreaOcclusionValidation.cpp"
#undef main

//------------------------------------------------------------------------------------------------------------------------
//                                                DECLARED MIXED GEOMETRY
//------------------------------------------------------------------------------------------------------------------------
namespace
{
struct MixedLayout
{
    float Scale;
    RayVector Offset;
};
const std::array<MixedLayout, 3> MixedLayouts{{{1, {0, 0, 0}}, {.65f, {-1.2f, .3f, .2f}}, {1.4f, {.6f, -.5f, -.1f}}}};
struct MixedRectangle
{
    RayVector Minimum, Maximum;
};
struct MixedScene
{
    std::vector<FacetAreaShadingTriangle> Surfaces;
    TriangleVisibilitySpace Reference;
    TriangleVisibilitySpace::FloatAtlas Atlas;
    std::vector<uint32_t> Profile;
    std::vector<MixedRectangle> Blockers;
    FacetAreaProposals Proposals;
    FacetAreaTransport Transport;
};
uint64_t MixedPrimary = 0, MixedCandidates = 0, MixedPaths = 0;
RayVector TransformMixedPoint(
    const MixedLayout &Layout,
    float X,
    float Y,
    float Z)
{
    return {Layout.Offset[0] + Layout.Scale * X, Layout.Offset[1] + Layout.Scale * Y,
            Layout.Offset[2] + Layout.Scale * Z};
}
MixedScene ConstructMixedScene(uint32_t LayoutIndex, uint32_t Variant)
{
    const auto &Layout = MixedLayouts[LayoutIndex];
    MixedScene Scene;
    std::vector<FacetAreaShadingTriangle> Original;
    auto AssembleRectangle =
        [&](float Left,
            float Right,
            float Bottom,
            float Top,
            float Height,
            const RayVector &Colour)
    {
        const auto A = TransformMixedPoint(Layout, Left, Bottom, Height),
                   B = TransformMixedPoint(Layout, Right, Bottom, Height);
        const auto C = TransformMixedPoint(Layout, Right, Top, Height),
                   D = TransformMixedPoint(Layout, Left, Top, Height);
        for (const auto &Triangle : std::array<FacetAreaTriangle, 2>{{{A, B, C}, {A, C, D}}})
        {
            FacetAreaShadingTriangle Surface;
            Surface.Triangle       = Triangle;
            const RayVector Normal = Height == 2 ? RayVector{0, 0, -1} : RayVector{0, 0, 1};
            Surface.Normals        = {Normal, Normal, Normal};
            Surface.Albedo         = Colour;
            Original.push_back(Surface);
        }
    };
    AssembleRectangle(-.8f, -.4f, -.2f, .2f, 0, {.8f, .1f, .05f});
    AssembleRectangle(.1f, .7f, -.3f, .3f, 0, {.05f, .3f, .9f});
    AssembleRectangle(-.35f, .35f, -.35f, .35f, 2, {.6f, .5f, .4f});
    Original[5].Albedo = {.2f, .7f, .3f};
    if (Variant & 1)
    {
        AssembleRectangle(-.22f, .07f, -.1f, .15f, 1, {0, 0, 0});
        Scene.Blockers.push_back(
            {TransformMixedPoint(Layout, -.22f, -.1f, 1), TransformMixedPoint(Layout, .07f, .15f, 1)});
    }
    if (Variant & 2)
    {
        AssembleRectangle(1.5f, 2.2f, -.6f, .6f, 1, {0, 0, 0});
        Scene.Blockers.push_back(
            {TransformMixedPoint(Layout, 1.5f, -.6f, 1), TransformMixedPoint(Layout, 2.2f, .6f, 1)});
    }
    std::vector<std::array<SpatialVector, 3>> Corners;
    for (const auto &Surface : Original)
        Corners.push_back({ConvertSpatialVector(Surface.Triangle.A), ConvertSpatialVector(Surface.Triangle.B),
                           ConvertSpatialVector(Surface.Triangle.C)});
    Scene.Reference.Build(Corners);
    Scene.Atlas = Scene.Reference.PackFloat32();
    std::vector<uint32_t> Mapping(Original.size());
    for (uint32_t Index = 0; Index < Scene.Atlas.OriginalFacets.size(); ++Index)
    {
        const auto Ordinal = Scene.Atlas.OriginalFacets[Index];
        Mapping[Ordinal]   = Index;
        Scene.Surfaces.push_back(Original[Ordinal]);
    }
    std::vector<FacetAreaTriangle> SourceTriangles;
    for (uint32_t Ordinal : {3u, 0u, 2u, 1u})
    {
        Scene.Profile.push_back(Mapping[Ordinal]);
        SourceTriangles.push_back(Original[Ordinal].Triangle);
    }
    VerifyOcclusion(Scene.Transport.Prepare(Scene.Surfaces, Scene.Atlas));
    VerifyOcclusion(Scene.Proposals.Prepare(SourceTriangles));
    return Scene;
}
bool AnalyticMixedBlocked(
    const MixedScene &Scene,
    const RayVector &Origin,
    const RayVector &Direction,
    float Maximum)
{
    if (Direction[2] == 0)
        return false;
    for (const auto &Rectangle : Scene.Blockers)
    {
        const double Distance = (double(Rectangle.Minimum[2]) - Origin[2]) / Direction[2];
        const double X = Origin[0] + Distance * Direction[0], Y = Origin[1] + Distance * Direction[1];
        if (Distance >= .00001f && Distance <= Maximum && X >= Rectangle.Minimum[0] && X <= Rectangle.Maximum[0] &&
            Y >= Rectangle.Minimum[1] && Y <= Rectangle.Maximum[1])
            return true;
    }
    return false;
}
void ValidateMixedPath(const MixedScene &Scene, const FacetAreaPath &Path)
{
    VerifyOcclusion(Path.Accepted);
    const RayVector Light{.8f, 0, .6f};
    const bool Suffix     = !AnalyticMixedBlocked(Scene, Path.Origin, Light, 100);
    const bool Connection = !AnalyticMixedBlocked(Scene, Path.Origin, Path.Direction, Path.Maximum);
    VerifyOcclusion(Path.SuffixVisible == Suffix && Path.ConnectionVisible == Connection);
    VerifyOcclusion(!Scene.Reference.Occluded(ConvertSpatialVector(Path.Origin), ConvertSpatialVector(Light), .00001f,
                                              100) == Suffix);
    VerifyOcclusion(!Scene.Reference.Occluded(ConvertSpatialVector(Path.Origin), ConvertSpatialVector(Path.Direction),
                                              .00001f, Path.Maximum) == Connection);
    ++MixedPaths;
}

//------------------------------------------------------------------------------------------------------------------------
//                                            SEPARATELY ASSEMBLED LOWER-LEVEL CALLER
//------------------------------------------------------------------------------------------------------------------------
FacetPacketEstimate EvaluateSeparatePacket(
    const MixedScene &Scene,
    const RayVector &Camera,
    uint32_t Seed,
    uint32_t Method)
{
    const uint32_t Estimator = Method / 2;
    const auto Measure       = FacetAreaMeasureCategory(Method % 2);
    FacetPacketEstimate Output;
    Output.Primary = Scene.Transport.Primary(Camera, {0, 0, 1});
    VerifyOcclusion(Output.Primary.Accepted);
    ++MixedPrimary;
    for (uint32_t Lane = 0; Lane < 4; ++Lane)
    {
        const uint32_t CandidateCount = Estimator == 1 ? 1 : 2;
        std::array<FacetAreaProposal, 2> Proposals{};
        std::array<FacetAreaScore, 2> Scores{};
        for (uint32_t Ordinal = 0; Ordinal < CandidateCount; ++Ordinal)
        {
            auto &Proposal    = Proposals[Ordinal];
            Proposal          = Scene.Proposals.Draw(Seed, 2 * Lane + Ordinal, Measure);
            Proposal.Index    = Scene.Profile[Proposal.Index];
            const auto Source = Scene.Transport.Source(Proposal);
            VerifyOcclusion(Source.Accepted);
            FacetAreaScoreInput Input;
            Input.Triangle       = Scene.Surfaces[Proposal.Index].Triangle;
            Input.U              = Proposal.U;
            Input.V              = Proposal.V;
            Input.SourceNormal   = Source.Normal;
            Input.SourceAlbedo   = Source.Albedo;
            Input.Receiver       = Output.Primary.Position;
            Input.ReceiverNormal = Output.Primary.Normal;
            Input.ReceiverAlbedo = Output.Primary.Albedo;
            Scores[Ordinal]      = EvaluateFacetAreaScore(Input);
            VerifyOcclusion(Scores[Ordinal].Accepted);
            ++Output.CandidateCount;
            ++MixedCandidates;
        }
        uint32_t Selected = 0;
        float Normalizer  = 0;
        if (Estimator == 0)
        {
            const auto Pair = SelectFacetAreaPair(
                {{{Scores[0].Target, Proposals[0].Density}, {Scores[1].Target, Proposals[1].Density}}},
                DrawFacetAreaSelectionUniform(Seed, Lane));
            VerifyOcclusion(Pair.Accepted);
            Selected   = Pair.Selected;
            Normalizer = float(Pair.Normalizer);
        }
        for (uint32_t Ordinal = 0; Ordinal < CandidateCount; ++Ordinal)
        {
            if (Estimator == 0 && Ordinal != Selected)
                continue;
            const auto Path = Scene.Transport.Connect(Proposals[Ordinal], Output.Primary);
            ValidateMixedPath(Scene, Path);
            const float Weight =
                (Estimator == 0 ? Normalizer : 1 / Proposals[Ordinal].Density) * (Estimator == 2 ? .5f : 1.f) / 4;
            for (uint32_t Axis = 0; Axis < 3; ++Axis)
            {
                Output.Rgb[Axis] += Path.Rgb[Axis] * Weight;
                Output.OmitConnection[Axis] += Path.SuffixOnly[Axis] * Weight;
                Output.OmitSuffix[Axis] += Path.ConnectionOnly[Axis] * Weight;
                Output.OmitBoth[Axis] += Scores[Ordinal].ProxyRgb[Axis] * Weight;
            }
            ++Output.PathCount;
            Output.VisibilityCount += 2;
        }
    }
    Output.Accepted = true;
    return Output;
}

//------------------------------------------------------------------------------------------------------------------------
//                                               FOCUSED AND CAPTURE VALIDATION
//------------------------------------------------------------------------------------------------------------------------
void ValidateMixedUnits()
{
    const auto Before = Checks;
    for (uint32_t Layout = 0; Layout < 3; ++Layout)
        for (uint32_t Variant = 0; Variant < 4; ++Variant)
        {
            const auto Scene     = ConstructMixedScene(Layout, Variant);
            const auto &Geometry = MixedLayouts[Layout];
            FacetAreaPacket Packet;
            VerifyOcclusion(Packet.Prepare(Scene.Surfaces, Scene.Atlas, Scene.Profile));
            const auto Camera = TransformMixedPoint(Geometry, .005f, 0, 1.5f);
            const auto Result = Packet.Evaluate(Camera, {0, 0, 1}, 0x492a1u);
            VerifyOcclusion(Result.Accepted && Result.Primary.Albedo == RayVector{.6f, .5f, .4f});
            const auto First = Scene.Proposals.Draw(7, 0, FacetAreaMeasureCategory::UniformFacet);
            const auto Area  = Scene.Proposals.Draw(7, 0, FacetAreaMeasureCategory::ProportionalArea);
            VerifyOcclusion(First.Accepted && Area.Accepted && First.Density != Area.Density);
            for (uint32_t Method = 0; Method < 6; ++Method)
            {
                const auto Baseline = EvaluateSeparatePacket(Scene, Camera, 0x492a1u, Method);
                const auto Estimate = Packet.Evaluate(Camera, {0, 0, 1}, 0x492a1u, FacetPacketEstimator(Method / 2),
                                                      FacetAreaMeasureCategory(Method % 2));
                VerifyOcclusion(Estimate.Accepted && Estimate.Rgb == Baseline.Rgb &&
                                Estimate.OmitBoth == Baseline.OmitBoth);
            }
        }
    std::cout << "FACET_MIXED_UNIT checks=" << Checks - Before << " pass=1\n";
}
void EncodeMixedCaptures(const std::string &Directory)
{
    constexpr uint32_t Count = 24576;
    std::filesystem::create_directories(Directory);
    for (uint32_t Layout = 0; Layout < 3; ++Layout)
        for (uint32_t Variant = 0; Variant < 4; ++Variant)
        {
            const auto Scene = ConstructMixedScene(Layout, Variant);
            FacetAreaPacket Packet;
            VerifyOcclusion(Packet.Prepare(Scene.Surfaces, Scene.Atlas, Scene.Profile));
            std::vector<Quartet> Output(6 * Count);
            for (uint32_t Pixel = 0; Pixel < Count; ++Pixel)
            {
                const uint32_t Method = Pixel / 4096, Sample = Pixel % 4096, ImagePixel = Sample % 1024;
                const float X       = -.195f + .4f * (float(ImagePixel % 32) + .5f) / 32;
                const float Y       = -.2f + .4f * (float(ImagePixel / 32) + .5f) / 32;
                const auto Camera   = TransformMixedPoint(MixedLayouts[Layout], X, Y, 1.5f);
                const uint32_t Seed = HashFacetAreaBits((Sample + 0x391b7ca5u) ^ (Layout * 0x6c8e9cf5u));
                const auto Estimate = Packet.Evaluate(Camera, {0, 0, 1}, Seed, FacetPacketEstimator(Method / 2),
                                                      FacetAreaMeasureCategory(Method % 2));
                const auto Baseline = EvaluateSeparatePacket(Scene, Camera, Seed, Method);
                VerifyOcclusion(Estimate.Accepted && Estimate.Rgb == Baseline.Rgb &&
                                Estimate.OmitConnection == Baseline.OmitConnection &&
                                Estimate.OmitSuffix == Baseline.OmitSuffix && Estimate.OmitBoth == Baseline.OmitBoth);
                VerifyOcclusion(Estimate.CandidateCount == Baseline.CandidateCount &&
                                Estimate.PathCount == Baseline.PathCount &&
                                Estimate.VisibilityCount == Baseline.VisibilityCount);
                VerifyOcclusion(Estimate.Primary.Index == Baseline.Primary.Index &&
                                Estimate.Primary.Position == Baseline.Primary.Position &&
                                Estimate.Primary.Normal == Baseline.Primary.Normal &&
                                Estimate.Primary.Albedo == Baseline.Primary.Albedo &&
                                Estimate.Primary.Strike == Baseline.Primary.Strike);
                const RayVector ExpectedAlbedo = X > Y ? RayVector{.6f, .5f, .4f} : RayVector{.2f, .7f, .3f};
                const auto Position            = TransformMixedPoint(MixedLayouts[Layout], X, Y, 2);
                for (uint32_t Axis = 0; Axis < 3; ++Axis)
                    VerifyOcclusion(std::abs(Estimate.Primary.Position[Axis] - Position[Axis]) <= 2e-5 &&
                                    std::abs(Estimate.Primary.Normal[Axis] - (Axis == 2 ? -1.f : 0.f)) <= 2e-5 &&
                                    Estimate.Primary.Albedo[Axis] == ExpectedAlbedo[Axis]);
                const std::array<RayVector, 5> Slabs{Estimate.Rgb, Estimate.OmitConnection, Estimate.OmitSuffix,
                                                     Estimate.OmitBoth, Estimate.Primary.Position};
                for (uint32_t Slab = 0; Slab < 5; ++Slab)
                    for (uint32_t Axis = 0; Axis < 3; ++Axis)
                        Output[Slab * Count + Pixel][Axis] = Slabs[Slab][Axis];
                Output[5 * Count + Pixel] = {float(Estimate.CandidateCount), float(Estimate.PathCount),
                                             float(Estimate.VisibilityCount), 0};
            }
            std::ofstream Capture(Directory + "/FacetMixed" + std::to_string(Layout) + std::to_string(Variant) + ".f32",
                                  std::ios::binary);
            Capture.exceptions(std::ios::failbit | std::ios::badbit);
            Capture.write(reinterpret_cast<const char *>(Output.data()), Output.size() * sizeof(Quartet));
            std::cout << "FACET_MIXED_CASE layout=" << Layout << " variant=" << Variant << " pass=1\n";
        }
    VerifyOcclusion(MixedPrimary == 294912 && MixedCandidates == 1966080 && MixedPaths == 1572864);
    std::cout << "FACET_MIXED_TOTAL primary=" << MixedPrimary << " candidates=" << MixedCandidates
              << " paths=" << MixedPaths << " visibility=" << 2 * MixedPaths << " pass=1\n";
}
} // namespace
int main(int Count, char **Arguments)
{
    try
    {
        if (Count != 2)
            throw std::runtime_error("Use --unit or output-directory");
        if (std::strcmp(Arguments[1], "--unit") == 0)
            ValidateMixedUnits();
        else
            EncodeMixedCaptures(Arguments[1]);
        return 0;
    }
    catch (const std::exception &Error)
    {
        std::cerr << Error.what() << '\n';
        return 1;
    }
}
