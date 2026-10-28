/*==========================================================================================================================================
                                                     EMISSIVEFACETQUADRATURE.H
==========================================================================================================================================*/
// 🧩 Area/luminance triangle selection with native shadowed-area integration and convergence checks.

namespace EmissiveQuadrature
{
using namespace TransportReference;
void AppendDistribution(LightComputeArchive& Scene)
{
    // 📝 The archive controls its own offset; probability construction belongs to the shared Engine.
    Require(Scene.Angles.size() > 0 && Scene.Angles.size() <= RadiantFacetProbabilityTicks,
            "Radiant facet distribution offset exceeds float32 precision");
    const auto Distribution = CompileRadiantFacetDistribution(Scene.Surfaces);
    Scene.Camera[10][2] = float(Scene.Angles.size());
    Scene.Angles.insert(Scene.Angles.end(), Distribution.begin(), Distribution.end());
}
uint32_t Next(uint32_t& Bits)
{
    Bits = Bits * 747796405u + 2891336453u;
    uint32_t Word = ((Bits >> ((Bits >> 28u) + 4u)) ^ Bits) * 277803737u;
    return (Word >> 22u) ^ Word;
}
double Random(uint32_t& Bits) { return double(Next(Bits) >> 8) * (1. / 16777216.); }
SpectrumRgb Response(const LightComputeReceiver& R, SpatialVector Direction)
{
    constexpr double π = 3.141592653589793;
    auto M = Material(R);
    auto View = Unit(Vector(R.ViewMetallic));
    double η = std::max(1., M.Ior), Ratio = (η - 1) / (η + 1), Metallic = std::clamp(M.Metallic, 0., 1.);
    double Dielectric = Ratio * Ratio * std::clamp(M.SpecularLevel, 0., 1.) * 2,
           Grazing = std::pow(1 - std::clamp(Dot(View, Unit(Add(View, Direction))), 0., 1.), 5);
    auto Diffuse = [&](double A)
    {
        double F0 = Dielectric * (1 - Metallic) + A * Metallic;
        return (1 - F0) * (1 - Grazing) * A * (1 - Metallic) / π;
    };
    return {Diffuse(M.Albedo.R), Diffuse(M.Albedo.G), Diffuse(M.Albedo.B)};
}
SpectrumRgb Integrate(const LightComputeArchive& Scene, const TriangleVisibilitySpace& Space,
                      const LightComputeReceiver& R, uint32_t Pixel, uint32_t Seed, uint32_t Samples)
{
    uint32_t Offset = uint32_t(Scene.Camera[10][2]), Count = uint32_t(Scene.Angles[Offset]),
             Bits = Seed ^ (Pixel * 747796405u + 2891336453u);
    SpectrumRgb Sum;
    if (!Count)
        return Sum;
    for (uint32_t I = 0; I < Samples; ++I)
    {
        double Draw = Random(Bits);
        uint32_t Selected = Count - 1;
        for (uint32_t J = 0; J < Count; ++J)
            if (Draw < Scene.Angles[Offset + 1 + J * 4 + 3])
            {
                Selected = J;
                break;
            }
        uint32_t Entry = Offset + 1 + Selected * 4;
        const auto& F = Scene.Surfaces[uint32_t(Scene.Angles[Entry])];
        double Root = std::sqrt(Random(Bits)), V = Random(Bits);
        auto Position = Add(Mul(Vector(F.A), 1 - Root),
                            Add(Mul(Vector(F.B), Root * (1 - V)), Mul(Vector(F.C), Root * V)));
        auto Delta = Sub(Position, Vector(R.PositionHit));
        double Squared = Dot(Delta, Delta);
        if (Squared < 1e-8)
            continue;
        auto Direction = Mul(Delta, 1 / std::sqrt(Squared)),
             Normal = Unit(Cross(Sub(Vector(F.B), Vector(F.A)), Sub(Vector(F.C), Vector(F.A))));
        double Geometry =
            std::max(0., Dot(Vector(R.NormalRoughness), Direction)) * std::abs(Dot(Normal, Direction));
        if (Geometry == 0)
            continue;
        auto Origin = Add(Vector(R.PositionHit), Mul(Vector(R.NormalRoughness), .002)),
             Segment = Sub(Position, Origin);
        double Length = std::sqrt(Dot(Segment, Segment));
        if (Length <= .004 || Space.Occluded(Origin, Mul(Segment, 1 / Length), 0, Length - .002))
            continue;
        auto B = Response(R, Direction);
        double Weight = Geometry * Scene.Angles[Entry + 2] / (Squared * Scene.Angles[Entry + 1] * Samples);
        Sum.R += B.R * F.Emissive[0] * Weight;
        Sum.G += B.G * F.Emissive[1] * Weight;
        Sum.B += B.B * F.Emissive[2] * Weight;
    }
    return Sum;
}
} // namespace EmissiveQuadrature
void ValidateEmissiveQuadrature(const char* Driver, const std::string& Shaders, LightComputeArchive Scene,
                                const std::string& Output)
{
    constexpr uint32_t Samples = 16;
    TransportReference::Prepare(Scene, Samples, 48, true);
    EmissiveQuadrature::AppendDistribution(Scene);
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Scene, Shaders);
    auto Old = Compute.Execute(Scene, 0, 16, 71, 64, 0, true, true, Samples, true, false);
    Scene.Save(Output + "/EmissiveScene.bin");
    auto Improved = Compute.Execute(Scene, 0, 16, 71, 64, 0, true, true, Samples, true, true);
    WriteImage(Output + "/EmissiveArea.ppm", Improved, Scene.Width, Scene.Height);
    WriteImage(Output + "/EmissiveHemisphere.ppm", Old, Scene.Width, Scene.Height);
    // 📝 Isolate secondary emission so analytic and sky light cannot obscure the estimator comparison.
    auto Isolated = Scene;
    for (auto& L : Isolated.Lights)
        L.PeakActive[1] = 0;
    Isolated.Camera[10][3] = 0;
    auto Hemisphere = Compute.Execute(Isolated, 0, 16, 71, 64, 0, true, true, Samples, true, false);
    auto Baseline = Compute.PrimaryReceivers();
    auto Area = Compute.Execute(Isolated, 0, 16, 71, 64, 0, true, true, Samples, true, true);
    auto Receivers = Compute.PrimaryReceivers();
    auto NoEnvironment = Compute.Execute(Isolated, 0, 16, 71, 64, 0, true, true, Samples, false, true);
    Require(NoEnvironment == Area, "Disabled environment leaked radiance or cube metadata");
    TransportReference::RaySpace Native(Isolated);
    double Sum = 0, Maximum = 0, OldSquared = 0, NewSquared = 0;
    uint32_t Probes = 0;
    auto Strikes = Compute.PrimaryStrikes();
    for (uint32_t Pixel = 0; Pixel < Scene.Receivers.size(); Pixel += 1000)
    {
        const auto& R = Baseline[Pixel];
        if (!R.PositionHit[3])
            continue;
        auto Exact = EmissiveQuadrature::Integrate(Isolated, Native.Space, R, Pixel, 71, Samples);
        std::array<double, 3> E{Exact.R, Exact.G, Exact.B};
        auto Gold = EmissiveQuadrature::Integrate(Isolated, Native.Space, R, Pixel, 8171, 8192);
        std::array<double, 3> G{Gold.R, Gold.G, Gold.B};
        auto Facet = uint32_t(Strikes[Pixel][1]);
        for (uint32_t C = 0; C < 3; ++C)
        {
            double Emission = Scene.Surfaces[Facet].Emissive[C],
                   Actual = Receivers[Pixel].ResidualSpecular[C] - Emission;
            double Error = std::abs(Actual - E[C]);
            Sum += Error;
            Maximum = std::max(Maximum, Error);
            double O = Hemisphere[Pixel][C] - Emission - G[C], N = Area[Pixel][C] - Emission - G[C];
            OldSquared += O * O;
            NewSquared += N * N;
        }
        ++Probes;
    }
    std::cout << "EMISSIVE_AREA triangles=" << Scene.Angles[uint32_t(Scene.Camera[10][2])]
              << " samples=" << Samples << " native_probes=" << Probes
              << " native_mean_rgb=" << Sum / (Probes * 3) << " native_max_rgb=" << Maximum
              << " hemisphere_rmse=" << std::sqrt(OldSquared / (Probes * 3))
              << " area_rmse=" << std::sqrt(NewSquared / (Probes * 3)) << " reference_samples=8192\n";
    Require(Sum / (Probes * 3) < .001 && Maximum < .05, "Native/GPU emissive-area integration disagreement");
    Require(NewSquared < OldSquared, "Emissive-area sampling did not reduce high-sample reference error");
    auto Empty = Scene;
    for (auto& F : Empty.Surfaces)
        F.Emissive = {};
    EmissiveQuadrature::AppendDistribution(Empty);
    auto EmptyHemisphere = Compute.Execute(Empty, 0, 16, 71, 64, 0, true, true, Samples, true, false);
    auto EmptyArea = Compute.Execute(Empty, 0, 16, 71, 64, 0, true, true, Samples, true, true);
    Require(EmptyArea == EmptyHemisphere, "Empty emitter set changed analytic or environment lighting");
    auto Invalid = Scene;
    auto Offset = uint32_t(Invalid.Camera[10][2]);
    Invalid.Angles[Offset + 2] = 0;
    bool Rejected = false;
    try
    {
        (void)Compute.Execute(Invalid, 0, 16, 71, 64, 0, true, true, Samples, true, true);
    }
    catch (const std::runtime_error&)
    {
        Rejected = true;
    }
    Require(Rejected, "Invalid emitter probability reached Vulkan dispatch");
    std::cout << "EMISSIVE_SAFETY empty_set_exact_equal=1 invalid_probability_rejected=1 "
                 "environment_disabled_exact_equal=1\n";
}
