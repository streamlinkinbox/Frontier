/*============================================================================================================================================
                                                       FACETNORMVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Evaluate a support-positive RGB RMS target on fresh held-out geometry without changing engine policy.
#include "FacetNormScenes.h"

//------------------------------------------------------------------------------------------------------------------------
//                                                   FRESH HELD-OUT RIGS
//------------------------------------------------------------------------------------------------------------------------
namespace
{
struct NormSource
{
    std::array<float, 4> Bounds; // [m] - Local rectangle x/y limits.
    RayVector            Colour; // [0-1] - Constant diffuse RGB.
};
struct NormRig
{
    MixedLayout              Layout;
    std::array<NormSource, 2> Sources;
    std::array<RayVector, 2>  ReceiverColours;
    std::array<float, 4>      Connection, Suffix;
};
const std::array<NormRig, 3> NormRigs{
    {{{1, {0, 0, 0}},
      {{{{-.9f, -.5f, -.25f, .25f}, {.9f, .035f, .02f}}, {{.05f, .75f, -.2f, .2f}, {.2f, .45f, .8f}}}},
      {{{.55f, .5f, .45f}, {.25f, .65f, .45f}}},
      {-.20f, .11f, -.12f, .12f},
      {1.65f, 2.25f, -.6f, .6f}},
     {{.8f, {-.9f, .2f, .15f}},
      {{{{-.75f, -.3f, -.3f, .3f}, {.025f, .85f, .05f}}, {{.15f, .85f, -.25f, .25f}, {.5f, .45f, .5f}}}},
      {{{.65f, .3f, .6f}, {.3f, .7f, .2f}}},
      {-.18f, .14f, -.15f, .08f},
      {.68f, 1.f, -.6f, .6f}},
     {{1.25f, {.5f, -.4f, -.2f}},
      {{{{-.85f, -.45f, -.15f, .15f}, {.75f, .65f, .015f}}, {{.12f, .82f, -.32f, .32f}, {.015f, .4f, .85f}}}},
      {{{.5f, .7f, .25f}, {.75f, .25f, .6f}}},
      {-.25f, .08f, -.06f, .17f},
      {1.55f, 2.12f, -.6f, .6f}}}};
uint64_t NormEvaluations = 0, NormDraws = 0, NormPaths = 0, NormBaselines = 0, VisibilityDisagreements = 0;

MixedScene ConstructNormScene(uint32_t RigIndex, uint32_t Variant)
{
    const auto                           &Rig = NormRigs[RigIndex];
    MixedScene                            Scene;
    std::vector<FacetAreaShadingTriangle> Original;
    auto AssembleRectangle = [&](const std::array<float, 4> &Bounds, float Height, const RayVector &Colour)
    {
        const auto      A      = TransformMixedPoint(Rig.Layout, Bounds[0], Bounds[2], Height);
        const auto      B      = TransformMixedPoint(Rig.Layout, Bounds[1], Bounds[2], Height);
        const auto      C      = TransformMixedPoint(Rig.Layout, Bounds[1], Bounds[3], Height);
        const auto      D      = TransformMixedPoint(Rig.Layout, Bounds[0], Bounds[3], Height);
        const RayVector Normal = Height == 2 ? RayVector{0, 0, -1} : RayVector{0, 0, 1};
        for (const auto &Triangle : std::array<FacetAreaTriangle, 2>{{{A, B, C}, {A, C, D}}})
        {
            FacetAreaShadingTriangle Surface;
            Surface.Triangle = Triangle;
            Surface.Normals  = {Normal, Normal, Normal};
            Surface.Albedo   = Colour;
            Original.push_back(Surface);
        }
    };
    for (const auto &Source : Rig.Sources)
        AssembleRectangle(Source.Bounds, 0, Source.Colour);
    AssembleRectangle({-.35f, .35f, -.35f, .35f}, 2, Rig.ReceiverColours[0]);
    Original[5].Albedo = Rig.ReceiverColours[1];
    for (uint32_t Bit = 0; Bit < 2; ++Bit)
        if (Variant & (1u << Bit))
        {
            const auto &Bounds = Bit ? Rig.Suffix : Rig.Connection;
            AssembleRectangle(Bounds, 1, {0, 0, 0});
            Scene.Blockers.push_back({TransformMixedPoint(Rig.Layout, Bounds[0], Bounds[2], 1),
                                      TransformMixedPoint(Rig.Layout, Bounds[1], Bounds[3], 1)});
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
    for (uint32_t Ordinal : {1u, 3u, 0u, 2u})
    {
        Scene.Profile.push_back(Mapping[Ordinal]);
        SourceTriangles.push_back(Original[Ordinal].Triangle);
    }
    VerifyOcclusion(Scene.Transport.Prepare(Scene.Surfaces, Scene.Atlas));
    VerifyOcclusion(Scene.Proposals.Prepare(SourceTriangles));
    return Scene;
}

//------------------------------------------------------------------------------------------------------------------------
//                                               EXHIBIT-ONLY NORM TARGET
//------------------------------------------------------------------------------------------------------------------------
float ComputeNormTarget(const RayVector &Proxy)
{
    for (float Channel : Proxy)
        if (!std::isfinite(Channel) || Channel < 0)
            return NAN;
    const float Maximum = std::max({Proxy[0], Proxy[1], Proxy[2]});
    if (Maximum == 0)
        return 1e-6f;
    const float First = Proxy[0] / Maximum, Second = Proxy[1] / Maximum, Third = Proxy[2] / Maximum;
    return 1e-6f + Maximum * std::sqrt(((First * First + Second * Second) + Third * Third) / 3.f);
}
void ValidateNormPath(const MixedScene &Scene, const FacetAreaPath &Path)
{
    VerifyOcclusion(Path.Accepted);
    const RayVector Light{.8f, 0, .6f};
    const bool      AnalyticSuffix     = !AnalyticMixedBlocked(Scene, Path.Origin, Light, 100);
    const bool      AnalyticConnection = !AnalyticMixedBlocked(Scene, Path.Origin, Path.Direction, Path.Maximum);
    const bool      DoubleSuffix =
        !Scene.Reference.Occluded(ConvertSpatialVector(Path.Origin), ConvertSpatialVector(Light), .00001f, 100);
    const bool DoubleConnection = !Scene.Reference.Occluded(
        ConvertSpatialVector(Path.Origin), ConvertSpatialVector(Path.Direction), .00001f, Path.Maximum);
    if (Path.SuffixVisible != AnalyticSuffix || Path.ConnectionVisible != AnalyticConnection ||
        Path.SuffixVisible != DoubleSuffix || Path.ConnectionVisible != DoubleConnection)
    {
        ++VisibilityDisagreements;
        if (VisibilityDisagreements <= 8)
            std::cout << "FACET_NORM_VISIBILITY_FAILURE path=" << NormPaths << " native_suffix=" << Path.SuffixVisible
                      << " analytic_suffix=" << AnalyticSuffix << " double_suffix=" << DoubleSuffix
                      << " native_connection=" << Path.ConnectionVisible
                      << " analytic_connection=" << AnalyticConnection << " double_connection=" << DoubleConnection
                      << '\n';
    }
    ++NormPaths;
}
FacetPacketEstimate EvaluateNormPacket(
    const MixedScene &Scene,
    const RayVector &Camera,
    uint32_t Seed,
    uint32_t Arm)
{
    const uint32_t      Estimator = Arm % 4;
    const auto          Measure   = FacetAreaMeasureCategory(Arm / 4);
    FacetPacketEstimate Output;
    Output.Primary = Scene.Transport.Primary(Camera, {0, 0, 1});
    VerifyOcclusion(Output.Primary.Accepted);
    ++NormEvaluations;
    for (uint32_t Lane = 0; Lane < 4; ++Lane)
    {
        const uint32_t                   CandidateCount = Estimator == 2 ? 1 : 2;
        std::array<FacetAreaProposal, 2> Proposals{};
        std::array<FacetAreaScore, 2>    Scores{};
        for (uint32_t Ordinal = 0; Ordinal < CandidateCount; ++Ordinal)
        {
            auto &Proposal = Proposals[Ordinal];
            Proposal       = Scene.Proposals.Draw(Seed, 2 * Lane + Ordinal, Measure);
            VerifyOcclusion(Proposal.Accepted);
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
            if (Estimator == 1)
                Scores[Ordinal].Target = ComputeNormTarget(Scores[Ordinal].ProxyRgb);
            VerifyOcclusion(std::isfinite(Scores[Ordinal].Target) && Scores[Ordinal].Target > 0);
            ++Output.CandidateCount;
            ++NormDraws;
        }
        uint32_t Selected   = 0;
        float    Normalizer = 0;
        if (Estimator < 2)
        {
            const auto Pair = SelectFacetAreaPair(
                {{{Scores[0].Target, Proposals[0].Density}, {Scores[1].Target, Proposals[1].Density}}},
                DrawFacetAreaSelectionUniform(Seed, Lane));
            VerifyOcclusion(Pair.Accepted && Pair.Selected < 2);
            Selected   = Pair.Selected;
            Normalizer = float(Pair.Normalizer);
        }
        for (uint32_t Ordinal = 0; Ordinal < CandidateCount; ++Ordinal)
        {
            if (Estimator < 2 && Ordinal != Selected)
                continue;
            const auto Path = Scene.Transport.Connect(Proposals[Ordinal], Output.Primary);
            ValidateNormPath(Scene, Path);
            const float Weight =
                (Estimator < 2 ? Normalizer : 1 / Proposals[Ordinal].Density) * (Estimator == 3 ? .5f : 1.f) / 4;
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
//                                                 UNITS AND COHORTS
//------------------------------------------------------------------------------------------------------------------------
void ValidateNormUnits()
{
    uint32_t Count   = 0;
    auto     Confirm = [&](bool Condition)
    {
        ++Count;
        VerifyOcclusion(Condition);
    };
    Confirm(ComputeNormTarget({0, 0, 0}) == 1e-6f);
    Confirm(std::abs(ComputeNormTarget({1, 1, 1}) - (1.f + 1e-6f)) < 1e-7f);
    Confirm(std::abs(ComputeNormTarget({3, 4, 0}) - (std::sqrt(25.f / 3) + 1e-6f)) < 1e-6f);
    Confirm(!std::isfinite(ComputeNormTarget({NAN, 0, 1})));
    Confirm(!std::isfinite(ComputeNormTarget({-1, 0, 1})));
    Confirm(!std::isfinite(ComputeNormTarget({INFINITY, 0, 1})));
    Confirm(std::isfinite(ComputeNormTarget({std::numeric_limits<float>::max(), 0, 0})));
    Confirm(ComputeNormTarget({std::numeric_limits<float>::denorm_min(), 0, 0}) > 0);
    const float Limit = std::numeric_limits<float>::max();
    Confirm(ComputeNormTarget({Limit, Limit, Limit}) == Limit);
    Confirm(ComputeNormTarget({0, 0, 7}) == ComputeNormTarget({7, 0, 0}));
    Confirm(ComputeNormTarget({1e-30f, 1e-30f, 1e-30f}) == 1e-6f);
    const auto Old      = ConstructMixedScene(0, 0);
    const auto Camera   = TransformMixedPoint(MixedLayouts[0], .005f, 0, 1.5f);
    const auto Baseline = EvaluateSeparatePacket(Old, Camera, 7, 0);
    const auto Mean     = EvaluateNormPacket(Old, Camera, 7, 0);
    Confirm(Mean.Rgb == Baseline.Rgb && Mean.OmitBoth == Baseline.OmitBoth);
    for (uint32_t Rig = 0; Rig < 3; ++Rig)
        for (uint32_t Variant = 0; Variant < 4; ++Variant)
        {
            const auto      Scene = ConstructNormScene(Rig, Variant);
            FacetAreaPacket Packet;
            Confirm(Packet.Prepare(Scene.Surfaces, Scene.Atlas, Scene.Profile));
            const auto Origin = TransformMixedPoint(NormRigs[Rig].Layout, .005f, 0, 1.5f);
            for (uint32_t Arm = 0; Arm < 8; ++Arm)
            {
                const auto Result = EvaluateNormPacket(Scene, Origin, 37, Arm);
                Confirm(Result.Accepted && Result.CandidateCount == ((Arm % 4) == 2 ? 4u : 8u) &&
                        Result.PathCount == ((Arm % 4) == 3 ? 8u : 4u));
                if (Arm % 4 != 1)
                {
                    const auto Method = Arm % 4 == 0   ? FacetPacketEstimator::Ris
                                        : Arm % 4 == 2 ? FacetPacketEstimator::Raw4
                                                       : FacetPacketEstimator::Raw8;
                    const auto Direct =
                        Packet.Evaluate(Origin, {0, 0, 1}, 37, Method, FacetAreaMeasureCategory(Arm / 4));
                    Confirm(Direct.Accepted && Direct.Rgb == Result.Rgb && Direct.OmitBoth == Result.OmitBoth);
                }
            }
        }
    Confirm(VisibilityDisagreements == 0);
    std::cout << "FACET_NORM_UNIT checks=" << Count << " pass=1\n";
}
bool EncodeNormCaptures(const std::string &Directory)
{
    constexpr uint32_t Cohorts = 8, Arms = 8, ImageCount = 1024;
    std::filesystem::create_directories(Directory);
    for (uint32_t Rig = 0; Rig < 3; ++Rig)
        for (uint32_t Variant = 0; Variant < 4; ++Variant)
        {
            const auto      Scene = ConstructNormScene(Rig, Variant);
            FacetAreaPacket Packet;
            VerifyOcclusion(Packet.Prepare(Scene.Surfaces, Scene.Atlas, Scene.Profile));
            std::vector<double> Output(Cohorts * 4 * Arms * ImageCount * 3);
            for (uint32_t Cohort = 0; Cohort < Cohorts; ++Cohort)
                for (uint32_t Arm = 0; Arm < Arms; ++Arm)
                    for (uint32_t Sample = 0; Sample < 4096; ++Sample)
                    {
                        const uint32_t Pixel  = Sample % ImageCount;
                        const float    X      = -.195f + .4f * (float(Pixel % 32) + .5f) / 32,
                                    Y         = -.2f + .4f * (float(Pixel / 32) + .5f) / 32;
                        const auto     Camera = TransformMixedPoint(NormRigs[Rig].Layout, X, Y, 1.5f);
                        const uint32_t Seed =
                            HashFacetAreaBits((Sample + 0x6db41895u) ^ (Rig * 0x6c8e9cf5u) ^ (Cohort * 0x9e3779b9u));
                        const auto Result = EvaluateNormPacket(Scene, Camera, Seed, Arm);
                        VerifyOcclusion(Result.CandidateCount == (Arm % 4 == 2 ? 4u : 8u) &&
                            Result.PathCount == (Arm % 4 == 3 ? 8u : 4u) && Result.VisibilityCount == 2 * Result.PathCount);
                        if (Arm % 4 != 1)
                        {
                            const auto Method = Arm % 4 == 0   ? FacetPacketEstimator::Ris
                                                : Arm % 4 == 2 ? FacetPacketEstimator::Raw4
                                                               : FacetPacketEstimator::Raw8;
                            const auto Direct =
                                Packet.Evaluate(Camera, {0, 0, 1}, Seed, Method, FacetAreaMeasureCategory(Arm / 4));
                            VerifyOcclusion(Direct.Accepted && Result.Rgb == Direct.Rgb &&
                                            Result.OmitConnection == Direct.OmitConnection &&
                                            Result.OmitSuffix == Direct.OmitSuffix &&
                                            Result.OmitBoth == Direct.OmitBoth &&
                                            Result.Primary.Index == Direct.Primary.Index &&
                                            Result.Primary.Position == Direct.Primary.Position &&
                                            Result.Primary.Normal == Direct.Primary.Normal &&
                                            Result.Primary.Albedo == Direct.Primary.Albedo &&
                                            Result.Primary.Strike == Direct.Primary.Strike &&
                                            Result.CandidateCount == Direct.CandidateCount &&
                                            Result.PathCount == Direct.PathCount &&
                                            Result.VisibilityCount == Direct.VisibilityCount);
                            ++NormBaselines;
                        }
                        const auto Position = TransformMixedPoint(NormRigs[Rig].Layout, X, Y, 2);
                        const auto Albedo   = NormRigs[Rig].ReceiverColours[X > Y ? 0 : 1];
                        for (uint32_t Axis = 0; Axis < 3; ++Axis)
                            VerifyOcclusion(std::abs(Result.Primary.Position[Axis] - Position[Axis]) <= 2e-5 &&
                                            std::abs(Result.Primary.Normal[Axis] - (Axis == 2 ? -1.f : 0.f)) <= 2e-5 &&
                                            Result.Primary.Albedo[Axis] == Albedo[Axis]);
                        const std::array<RayVector, 4> Rgb{Result.Rgb, Result.OmitConnection, Result.OmitSuffix,
                                                           Result.OmitBoth};
                        for (uint32_t Slab = 0; Slab < 4; ++Slab)
                            for (uint32_t Axis = 0; Axis < 3; ++Axis)
                            {
                                VerifyOcclusion(std::isfinite(Rgb[Slab][Axis]) && Rgb[Slab][Axis] >= 0);
                                Output[((((Cohort * 4 + Slab) * Arms + Arm) * ImageCount + Pixel) * 3) + Axis] +=
                                    double(Rgb[Slab][Axis]) / 4;
                            }
                    }
            std::ofstream Capture(Directory + "/FacetNorm" + std::to_string(Rig) + std::to_string(Variant) + ".f64",
                                  std::ios::binary);
            Capture.exceptions(std::ios::failbit | std::ios::badbit);
            Capture.write(reinterpret_cast<const char *>(Output.data()), Output.size() * sizeof(double));
            std::cout << "FACET_NORM_CASE rig=" << Rig << " variant=" << Variant << " cohorts=8 arms=8 pass=1\n";
        }
    VerifyOcclusion(NormEvaluations == 3145728 && NormDraws == 22020096 && NormPaths == 15728640 &&
                    NormBaselines == 2359296);
    std::cout << "FACET_NORM_TOTAL evaluations=" << NormEvaluations << " candidates=" << NormDraws
              << " paths=" << NormPaths << " visibility=" << 2 * NormPaths << " api_baselines=" << NormBaselines
              << " disagreements=" << VisibilityDisagreements << '\n';
    return VisibilityDisagreements == 0;
}
} // namespace
int main(int Count, char **Arguments)
{
    try
    {
        if (Count != 2)
            throw std::runtime_error("Use --unit or output-directory");
        if (std::strcmp(Arguments[1], "--unit") == 0)
        {
            ValidateNormUnits();
            return 0;
        }
        return EncodeNormCaptures(Arguments[1]) ? 0 : 1;
    }
    catch (const std::exception &Error)
    {
        std::cerr << Error.what() << '\n';
        return 1;
    }
}
