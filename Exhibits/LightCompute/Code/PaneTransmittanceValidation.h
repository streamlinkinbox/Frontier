/*==========================================================================================================================================
                                                   PANETRANSMITTANCEVALIDATION.H
==========================================================================================================================================*/
// 🧩 Spectral thin-pane transmittance across area-emitter quadrature, volumetric fog shafts and multi-stratum mixtures.

namespace PaneTransmittanceReference
{
using namespace TransportReference;

//------------------------------------------------------------------------------------------------------------------------
//                                             NATIVE SEGMENT AND QUADRATURE ORACLES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Mirror MaterialShadowTransmission in double precision across up to seven thin-sheet panes (eight shadow queries).
SpectrumRgb EvaluateSegmentTransmittance(
    const LightComputeArchive&     Scene,
    const TriangleVisibilitySpace& Space,
    SpatialVector                  Origin,
    SpatialVector                  Direction,
    double                         Far)
{
    if (Far <= .001) return {1, 1, 1};
    std::array<double, 3> Throughput{1, 1, 1};
    for (uint32_t Pane = 0; Pane < 8; ++Pane)
    {
        const auto Hit = Space.Trace(Origin, Direction, .001, Far);
        if (!Hit.Hit) return {Throughput[0], Throughput[1], Throughput[2]};
        const auto& F = Scene.Surfaces[Hit.Facet];
        const uint32_t Address = F.Emissive[3] > 0 ? uint32_t(F.Emissive[3]) : 0;
        if (!Address || Scene.Angles[Address] != 2) return {};
        const auto Normal = Unit(Vector(F.NormalA));
        const double Cosine = std::max(.05, std::abs(Dot(Normal, Direction)));
        const double Fresnel = .04 + .96 * std::pow(1 - Cosine, 5);
        constexpr double σ[]{.8, .15, .4};                                      // [1/m] - Spectral sheet attenuation
        for (uint32_t C = 0; C < 3; ++C)
            Throughput[C] *= (1 - Fresnel) / (1 + Fresnel) * std::exp(-σ[C] * .006 / Cosine);
        Origin = Add(Origin, Mul(Direction, Hit.Travel + .003));
        Far -= Hit.Travel + .003;
        if (Far <= .001) return {Throughput[0], Throughput[1], Throughput[2]};
    }
    return {};
}

SpectrumRgb IntegrateReceiverFacets(
    const LightComputeArchive&     Scene,
    const TriangleVisibilitySpace& Space,
    const LightComputeReceiver&    R,
    uint32_t                       Pixel,
    uint32_t                       Seed,
    uint32_t                       Samples)
{
    const uint32_t Offset = uint32_t(Scene.Camera[10][2]);
    const uint32_t Count = uint32_t(Scene.Angles[Offset]);
    uint32_t Bits = Seed ^ (Pixel * 747796405u + 2891336453u);
    SpectrumRgb Sum{};
    if (!Count) return Sum;
    for (uint32_t I = 0; I < Samples; ++I)
    {
        const double Draw = EmissiveQuadrature::Random(Bits);
        uint32_t Selected = Count - 1;
        for (uint32_t J = 0; J < Count; ++J)
            if (Draw < Scene.Angles[Offset + 1 + J * 4 + 3])
            {
                Selected = J;
                break;
            }
        const uint32_t Entry = Offset + 1 + Selected * 4;
        const auto& F = Scene.Surfaces[uint32_t(Scene.Angles[Entry])];
        const double Root = std::sqrt(EmissiveQuadrature::Random(Bits));
        const double V = EmissiveQuadrature::Random(Bits);
        const auto Position = Add(Mul(Vector(F.A), 1 - Root),
                                  Add(Mul(Vector(F.B), Root * (1 - V)), Mul(Vector(F.C), Root * V)));
        const auto Delta = Sub(Position, Vector(R.PositionHit));
        const double Squared = Dot(Delta, Delta);
        if (Squared < 1e-8) continue;
        const auto Direction = Mul(Delta, 1 / std::sqrt(Squared));
        const auto Geometric = Unit(Cross(Sub(Vector(F.B), Vector(F.A)), Sub(Vector(F.C), Vector(F.A))));
        const double Geometry =
            std::max(0., Dot(Vector(R.NormalRoughness), Direction)) * std::abs(Dot(Geometric, Direction));
        if (Geometry == 0) continue;
        const auto Origin = Add(Vector(R.PositionHit), Mul(Vector(R.NormalRoughness), .002));
        const auto Segment = Sub(Position, Origin);
        const double Length = std::sqrt(Dot(Segment, Segment));
        if (Length <= .004) continue;
        const auto Transmittance =
            EvaluateSegmentTransmittance(Scene, Space, Origin, Mul(Segment, 1 / Length), Length - .002);
        if (Transmittance.R == 0 && Transmittance.G == 0 && Transmittance.B == 0) continue;
        const auto Response = EmissiveQuadrature::Response(R, Direction);
        const double Weight = Geometry * Scene.Angles[Entry + 2] / (Squared * Scene.Angles[Entry + 1] * Samples);
        Sum.R += Response.R * F.Emissive[0] * Transmittance.R * Weight;
        Sum.G += Response.G * F.Emissive[1] * Transmittance.G * Weight;
        Sum.B += Response.B * F.Emissive[2] * Transmittance.B * Weight;
    }
    return Sum;
}

SpectrumRgb IntegrateVolumeFacets(
    const LightComputeArchive&     Scene,
    const TriangleVisibilitySpace& Space,
    const FogParameters&           Medium,
    SpatialVector                  At,
    SpatialVector                  ViewDirection,
    double                         Anisotropy,
    double                         LightDistance,
    bool                           Occlusion,
    uint32_t&                      Bits,
    uint32_t                       Samples)
{
    SpectrumRgb Sum{};
    const uint32_t Offset = uint32_t(Scene.Camera[10][2]);
    const uint32_t Count = uint32_t(Scene.Angles[Offset]);
    if (!Count) return Sum;
    for (uint32_t I = 0; I < Samples; ++I)
    {
        const double Draw = EmissiveQuadrature::Random(Bits);
        uint32_t Selected = Count - 1;
        for (uint32_t J = 0; J < Count; ++J)
            if (Draw < Scene.Angles[Offset + 1 + J * 4 + 3])
            {
                Selected = J;
                break;
            }
        const uint32_t Entry = Offset + 1 + Selected * 4;
        const auto& F = Scene.Surfaces[uint32_t(Scene.Angles[Entry])];
        const double Root = std::sqrt(EmissiveQuadrature::Random(Bits));
        const double V = EmissiveQuadrature::Random(Bits);
        const auto Position = Add(Mul(Vector(F.A), 1 - Root),
                                  Add(Mul(Vector(F.B), Root * (1 - V)), Mul(Vector(F.C), Root * V)));
        const auto Delta = Sub(Position, At);
        const double Squared = Dot(Delta, Delta);
        const double Distance = std::sqrt(Squared);
        if (Distance <= .004) continue;
        const auto Toward = Mul(Delta, 1 / Distance);
        const auto Normal = Unit(Cross(Sub(Vector(F.B), Vector(F.A)), Sub(Vector(F.C), Vector(F.A))));
        const double Geometry = std::abs(Dot(Normal, Toward));
        if (Geometry == 0) continue;
        const auto Transmittance =
            Occlusion ? EvaluateSegmentTransmittance(Scene, Space, Add(At, Mul(Toward, .002)), Toward, Distance - .004)
                      : SpectrumRgb{1, 1, 1};
        if (Transmittance.R == 0 && Transmittance.G == 0 && Transmittance.B == 0) continue;
        const double Weight = HenyeyGreensteinPhase(Dot(ViewDirection, Toward), Anisotropy) * Geometry *
                              FogTransmittance(Medium, At, Toward, std::min(Distance, LightDistance)) *
                              Scene.Angles[Entry + 2] / (Squared * Scene.Angles[Entry + 1] * Samples);
        Sum.R += F.Emissive[0] * Transmittance.R * Weight;
        Sum.G += F.Emissive[1] * Transmittance.G * Weight;
        Sum.B += F.Emissive[2] * Transmittance.B * Weight;
    }
    return Sum;
}

FogRadianceIntegral IntegratePaneFogColumn(
    const LightComputeArchive& Scene,
    const RaySpace&            Geometry,
    uint32_t                   FogOffset,
    SpatialVector              Origin,
    SpatialVector              Ray,
    double                     Distance,
    uint32_t                   Pixel,
    uint32_t                   Seed)
{
    const float* F = Scene.Angles.data() + FogOffset;
    FogParameters Medium;
    Medium.DensityAtBase = F[0];
    Medium.BaseHeightM = F[1];
    Medium.FalloffHeightM = F[2];
    uint32_t Bits = Seed ^ (Pixel * 747796405u + 2891336453u);
    auto Incident = [&](SpatialVector At)
    {
        SpectrumRgb Sum{F[8], F[9], F[10]};
        if (F[13] != 0)
            for (const auto& L : Geometry.Sources)
            {
                SpatialVector Toward;
                double Far;
                const auto Light = ReferenceLightArrival(L, At, Toward, Far);
                if (Light.R == 0 && Light.G == 0 && Light.B == 0) continue;
                Far = std::min(Far, double(F[14]));
                const auto Transmittance =
                    F[11] != 0 ? EvaluateSegmentTransmittance(Scene, Geometry.Space, Add(At, Mul(Toward, .002)),
                                                              Toward, std::max(Far - .004, 0.))
                               : SpectrumRgb{1, 1, 1};
                if (Transmittance.R == 0 && Transmittance.G == 0 && Transmittance.B == 0) continue;
                const double Scale =
                    HenyeyGreensteinPhase(Dot(Ray, Toward), F[4]) * FogTransmittance(Medium, At, Toward, Far);
                Sum.R += Light.R * Transmittance.R * Scale;
                Sum.G += Light.G * Transmittance.G * Scale;
                Sum.B += Light.B * Transmittance.B * Scale;
            }
        if (F[15] > 0)
        {
            const auto Area = IntegrateVolumeFacets(Scene, Geometry.Space, Medium, At, Ray, F[4], F[14],
                                                    F[11] != 0, Bits, uint32_t(F[15]));
            Sum.R += Area.R;
            Sum.G += Area.G;
            Sum.B += Area.B;
        }
        return Sum;
    };
    return IntegrateFogRadiance(Medium, Origin, Ray, Distance, uint32_t(F[6]), F[3], Incident);
}

PreviewScattering UnpackReceiverScattering(const LightComputeArchive& Scene, const LightComputeReceiver& R)
{
    const uint32_t Address = uint32_t(R.ExpectedDirect[3]) - 1;
    PreviewScattering S{};
    S.Configuration[0] = uint32_t(Scene.Angles[Address]);
    for (uint32_t I = 0; I < S.Configuration[0]; ++I)
    {
        auto& Stratum = S.Strata[I];
        const uint32_t P = Address + 8 + I * 24;
        std::copy_n(Scene.Angles.data() + P, 4, Stratum.AlbedoRoughness.begin());
        std::copy_n(Scene.Angles.data() + P + 4, 4, Stratum.Optics.begin());
        std::copy_n(Scene.Angles.data() + P + 8, 4, Stratum.AbsorptionThickness.begin());
        Stratum.Classification[0] = uint32_t(Scene.Angles[P + 12]);
        std::copy_n(Scene.Angles.data() + P + 16, 4, Stratum.Sheen.begin());
        std::copy_n(Scene.Angles.data() + P + 20, 4, Stratum.SheenFinish.begin());
    }
    S.Strata[0].AlbedoRoughness = {R.AlbedoIor[0], R.AlbedoIor[1], R.AlbedoIor[2], R.NormalRoughness[3]};
    S.Strata[0].Optics[0] = R.ViewMetallic[3];
    return S;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              NATIVE ARITHMETIC AND BOUNDS CHECKS
//------------------------------------------------------------------------------------------------------------------------

void ValidateNativePaneArithmetic(const std::string& Shaders)
{
    LightComputeArchive Base;
    Prepare(Base, 8, 4, false);
    const auto Materials = AppendSceneMaterials(Base, Shaders);
    const uint32_t Sheet = DuplicateSceneMaterial(Base, Materials[3], 2, {1, 1, 1, 0}, 0);
    auto Build = [&](uint32_t Panes, double TiltRadians, bool OpaqueTail)
    {
        auto Scene = Base;
        for (uint32_t I = 0; I < Panes; ++I)
        {
            const double Y = -1.0 - .08 * I;
            const SpatialVector U{2, 0, 0};
            const SpatialVector V{0, -std::sin(TiltRadians) * 2, std::cos(TiltRadians) * 2};
            MaterialRectangle(Scene, {0, Y, 1}, U, V, Sheet, 1500 + I);
        }
        if (OpaqueTail) MaterialRectangle(Scene, {0, -2.2, 1}, {2, 0, 0}, {0, 0, 2}, 0, 1700);
        RebuildMaterialVisibility(Scene);
        return Scene;
    };
    const auto Empty = Build(0, 0, false);
    RaySpace EmptySpace(Empty);
    const auto Clear = EvaluateSegmentTransmittance(Empty, EmptySpace.Space, {0, 0, 1}, {0, -1, 0}, 3.0);
    Require(Clear.R == 1 && Clear.G == 1 && Clear.B == 1, "Unobstructed segment did not preserve unit transmittance");

    const auto One = Build(1, 0, false);
    RaySpace OneSpace(One);
    const auto Single = EvaluateSegmentTransmittance(One, OneSpace.Space, {0, 0, 1}, {0, -1, 0}, 3.0);
    constexpr double σ[]{.8, .15, .4};                                          // [1/m] - Spectral sheet attenuation
    const double NormalFactor = (1 - .04) / (1 + .04);
    Require(std::abs(Single.R - NormalFactor * std::exp(-σ[0] * .006)) < 1e-12 &&
                std::abs(Single.G - NormalFactor * std::exp(-σ[1] * .006)) < 1e-12 &&
                std::abs(Single.B - NormalFactor * std::exp(-σ[2] * .006)) < 1e-12,
            "Normal-incidence thin-pane formula mismatch");
    Require(Single.G > Single.B && Single.B > Single.R && Single.G < 1, "Spectral pane ordering failed");

    const auto Tilted = Build(1, 1.0471975511965976, false);                    // [rad] - 60° tilt => cos θ = 0.5
    RaySpace TiltedSpace(Tilted);
    const auto Oblique = EvaluateSegmentTransmittance(Tilted, TiltedSpace.Space, {0, 0, 1}, {0, -1, 0}, 3.0);
    Require(Oblique.R < Single.R && Oblique.G < Single.G && Oblique.B < Single.B &&
                Oblique.G / Oblique.R > Single.G / Single.R,
            "Oblique incidence failed Fresnel/path-length strengthening");

    const auto Two = Build(2, 0, false);
    RaySpace TwoSpace(Two);
    const auto Double = EvaluateSegmentTransmittance(Two, TwoSpace.Space, {0, 0, 1}, {0, -1, 0}, 3.0);
    Require(std::abs(Double.R - Single.R * Single.R) < 1e-12 &&
                std::abs(Double.G - Single.G * Single.G) < 1e-12 &&
                std::abs(Double.B - Single.B * Single.B) < 1e-12,
            "Double-pane multiplicative transmittance failed");

    const auto Seven = Build(7, 0, false);
    RaySpace SevenSpace(Seven);
    const auto SevenResult = EvaluateSegmentTransmittance(Seven, SevenSpace.Space, {0, 0, 1}, {0, -1, 0}, 3.0);
    Require(SevenResult.G > 0 && std::abs(SevenResult.G - std::pow(Single.G, 7)) < 1e-12,
            "Seven-pane boundary failed");

    const auto Eight = Build(8, 0, false);
    RaySpace EightSpace(Eight);
    const auto EightResult = EvaluateSegmentTransmittance(Eight, EightSpace.Space, {0, 0, 1}, {0, -1, 0}, 3.0);
    Require(EightResult.R == 0 && EightResult.G == 0 && EightResult.B == 0,
            "Eighth pane did not trigger eight-query shadow cap");

    const auto Blocked = Build(1, 0, true);
    RaySpace BlockedSpace(Blocked);
    const auto BlockedResult = EvaluateSegmentTransmittance(Blocked, BlockedSpace.Space, {0, 0, 1}, {0, -1, 0}, 3.0);
    Require(BlockedResult.R == 0 && BlockedResult.G == 0 && BlockedResult.B == 0,
            "Opaque blocker behind thin pane leaked transmittance");

    // Cross-check native balance-heuristic mixture quadrature against pure cosine hemisphere integration.
    PreviewScattering Coat{};
    Coat.Configuration[0] = 2;
    Coat.Strata[0].AlbedoRoughness = {.12f, .38f, .22f, .35f};
    Coat.Strata[0].Optics = {.65f, 1.5f, 1.f, 0};
    Coat.Strata[1].AlbedoRoughness = {1.f, 1.f, 1.f, .18f};
    Coat.Strata[1].Optics = {0.f, 1.5f, 1.f, 0};
    Coat.Strata[1].Classification[0] = 1;
    const SpatialVector N{0, 0, 1}, V = Unit({.35, -.2, .915});
    constexpr double π = 3.141592653589793;
    constexpr uint32_t VerifySamples = 8192;
    auto MixtureDensity = [&](SpatialVector L)
    {
        const double NL = std::max(0., Dot(N, L));
        double Sum = NL / π;
        const auto H = Unit(Add(V, L));
        const double NH = std::clamp(Dot(N, H), 0., 1.);
        const double VH = std::max(1e-6, 4 * std::abs(Dot(V, H)));
        for (uint32_t J = 0; J < Coat.Configuration[0]; ++J)
            Sum += EvaluateGgxDistribution(NH, std::max(.04f, Coat.Strata[J].AlbedoRoughness[3])) * NH / VH;
        return Sum;
    };
    double CosineIntegral = 0, MixtureIntegral = 0;
    for (uint32_t D = 0; D < VerifySamples; ++D)
    {
        const double U = (D + .5) / VerifySamples;
        const double φ = D * π * (3 - std::sqrt(5.));
        const auto Lc = SpatialVector{std::sqrt(U) * std::cos(φ), std::sqrt(U) * std::sin(φ), std::sqrt(1 - U)};
        const auto Hc = Unit(Add(V, Lc));
        const double IncidentC = 1.2 + .7 * Lc.X - .4 * Lc.Y;
        CosineIntegral += EvaluateScatteringSequence(Coat, Dot(N, V), Dot(N, Lc), std::clamp(Dot(N, Hc), 0., 1.),
                                                     std::clamp(Dot(V, Hc), 0., 1.))[1] *
                          IncidentC * π / VerifySamples;
        for (uint32_t Proposal = 0; Proposal <= Coat.Configuration[0]; ++Proposal)
        {
            SpatialVector Lm = Lc;
            if (Proposal > 0)
            {
                const double Roughness = std::max(.04f, Coat.Strata[Proposal - 1].AlbedoRoughness[3]);
                const double A = Roughness * Roughness;
                const double CosH = std::sqrt((1 - U) / (1 + (A * A - 1) * U));
                const double SinH = std::sqrt(std::max(0., 1 - CosH * CosH));
                const SpatialVector Hm{SinH * std::cos(φ), SinH * std::sin(φ), CosH};
                Lm = Sub(Mul(Hm, 2 * Dot(V, Hm)), V);
            }
            if (Dot(N, Lm) <= 0) continue;
            const auto Hm = Unit(Add(V, Lm));
            const double IncidentM = 1.2 + .7 * Lm.X - .4 * Lm.Y;
            MixtureIntegral += EvaluateScatteringSequence(Coat, Dot(N, V), Dot(N, Lm), std::clamp(Dot(N, Hm), 0., 1.),
                                                          std::clamp(Dot(V, Hm), 0., 1.))[1] *
                               Dot(N, Lm) * IncidentM / (MixtureDensity(Lm) * VerifySamples);
        }
    }
    Require(std::abs(CosineIntegral - MixtureIntegral) < 1e-3,
            "Native multi-stratum balance-heuristic quadrature disagreed with cosine integral");

    std::cout << "PANE_TRANSMITTANCE_NATIVE checks=8 single_rgb=" << Single.R << ',' << Single.G << ',' << Single.B
              << " oblique_rgb=" << Oblique.R << ',' << Oblique.G << ',' << Oblique.B
              << " seven_green=" << SevenResult.G << " mixture_cosine_diff="
              << std::abs(CosineIntegral - MixtureIntegral) << " eight_cap_zero=1" << std::endl;
}
} // namespace PaneTransmittanceReference

//------------------------------------------------------------------------------------------------------------------------
//                                           VULKAN SURFACE, MIXTURE AND FOG VALIDATION
//------------------------------------------------------------------------------------------------------------------------

void ValidatePaneTransmittance(
    const char*        Driver,
    const std::string& Shaders,
    const std::string& Output,
    const std::string& SavedSpectrum,
    bool               NativeOnly = false,
    bool               Original   = false)
{
    using namespace PaneTransmittanceReference;
    std::filesystem::create_directories(Output);
    if (!Original) ValidateNativePaneArithmetic(Shaders);
    if (NativeOnly) return;

    constexpr uint32_t FrameWidth = 320, FrameHeight = 200;
    constexpr uint32_t SurfaceSamples = 128;

    auto MakeSurfaceFixture = [&](uint32_t Panes, double TiltRadians, double EmitterHalfExtent, double PaneHalfExtent)
    {
        LightComputeArchive Scene;
        Prepare(Scene, SurfaceSamples, 4, false);
        const auto Materials = AppendSceneMaterials(Scene, Shaders);
        const uint32_t Sheet = DuplicateSceneMaterial(Scene, Materials[3], 2, {1, 1, 1, 0}, 0);
        MaterialRectangle(Scene, {0, 0, 1}, {.55, 0, 0}, {0, 0, .40}, 0, 1300);
        for (auto& F : Scene.Surfaces)
        {
            F.AlbedoRoughness = {.85f, .85f, .85f, .6f};
            F.OpticalToken[0] = 0;
        }
        for (uint32_t I = 0; I < Panes; ++I)
        {
            const double Y = -4.2 - .08 * I;
            const SpatialVector U{PaneHalfExtent, 0, 0};
            const SpatialVector V{0, -std::sin(TiltRadians) * PaneHalfExtent, std::cos(TiltRadians) * PaneHalfExtent};
            MaterialRectangle(Scene, {0, Y, 1}, U, V, Sheet, 1500 + I);
        }
        MaterialRectangle(Scene, {0, -5.6, 1}, {-EmitterHalfExtent, 0, 0}, {0, 0, EmitterHalfExtent}, 0, 1400);
        for (size_t I = Scene.Surfaces.size() - 2; I < Scene.Surfaces.size(); ++I)
            Scene.Surfaces[I].Emissive = {3.2f, 3.2f, 3.2f, 0};
        RebuildMaterialVisibility(Scene);
        MaterialCamera(Scene, {0, -2.0, 1}, {0, 0, 1}, FrameWidth, FrameHeight);
        Scene.Camera[8][2] = 1e9f;
        EmissiveQuadrature::AppendDistribution(Scene);
        return Scene;
    };

    // Build Studio archive up front so one shared LightComputeSubmission avoids repeated CPU pipeline JIT.
    const auto Coefficients = ReadGlazingSpectra(SavedSpectrum);
    LightComputeArchive Studio;
    Prepare(Studio, 32, 16, false);
    const auto StudioMaterials = AppendSceneMaterials(Studio, Shaders);
    const uint32_t Green = StudioMaterials[0];
    const uint32_t Fabric = StudioMaterials[4];
    const uint32_t StudioSheet = DuplicateSceneMaterial(Studio, StudioMaterials[3], 2, {1, 1, 1, 0}, 0);
    const uint32_t Orange = DuplicateSceneMaterial(Studio, StudioMaterials[3], 3, {.85f, .105f, .009f, .22f}, 0);
    const uint32_t Band = DuplicateSceneMaterial(Studio, StudioMaterials[3], 3, {.82f, .85f, .88f, .12f}, .65f);
    const uint32_t Glass = DuplicateSceneMaterial(Studio, StudioMaterials[2], 4, {1, 1, 1, 0}, 0);
    BindGlazingSpectrum(Studio, Glass, Coefficients[3]);

    auto DarkQuad = [&](SpatialVector Centre, SpatialVector U, SpatialVector V, std::array<float, 4> Albedo,
                        uint32_t Token)
    {
        MaterialRectangle(Studio, Centre, U, V, 0, Token);
        for (size_t I = Studio.Surfaces.size() - 2; I < Studio.Surfaces.size(); ++I)
        {
            Studio.Surfaces[I].AlbedoRoughness = Albedo;
            Studio.Surfaces[I].OpticalToken[0] = 0;
        }
    };

    DarkQuad({0, -.8, 0}, {5.2, 0, 0}, {0, 6.2, 0}, {.38f, .39f, .41f, .62f}, 3000);
    DarkQuad({0, 4.4, 2.3}, {5.2, 0, 0}, {0, 0, -2.3}, {.09f, .10f, .12f, .8f}, 3001);
    DarkQuad({-5.2, -.8, 2.3}, {0, 6.2, 0}, {0, 0, 2.3}, {.09f, .10f, .12f, .8f}, 3002);
    DarkQuad({5.2, -.8, 2.3}, {0, -6.2, 0}, {0, 0, 2.3}, {.09f, .10f, .12f, .8f}, 3003);
    DarkQuad({0, -6.2, 4.4}, {5.2, 0, 0}, {0, 0, 1.2}, {.05f, .05f, .06f, .9f}, 3004);
    DarkQuad({0, -3.55, 3.2}, {5.2, 0, 0}, {0, -2.65, 0}, {.07f, .075f, .09f, .8f}, 3010);
    DarkQuad({0, 3.15, 3.2}, {5.2, 0, 0}, {0, -2.25, 0}, {.07f, .075f, .09f, .8f}, 3011);
    DarkQuad({-3.775, 0, 3.2}, {1.425, 0, 0}, {0, -.9, 0}, {.07f, .075f, .09f, .8f}, 3012);
    DarkQuad({0, 0, 3.2}, {.75, 0, 0}, {0, -.9, 0}, {.07f, .075f, .09f, .8f}, 3013);
    DarkQuad({3.775, 0, 3.2}, {1.425, 0, 0}, {0, -.9, 0}, {.07f, .075f, .09f, .8f}, 3014);
    DarkQuad({0, 0, 4.45}, {0, 3.2, 0}, {0, 0, 1.25}, {.04f, .04f, .05f, .9f}, 3015);
    DarkQuad({0, 0, 4.45}, {0, -3.2, 0}, {0, 0, 1.25}, {.04f, .04f, .05f, .9f}, 3016);
    // Five-pane laminated skylight in the right bay (within the seven-pane shadow transmittance budget).
    for (uint32_t Layer = 0; Layer < 5; ++Layer)
        MaterialRectangle(Studio, {1.55, 0, 3.20 + .02 * Layer}, {.80, 0, 0}, {0, .9, 0}, StudioSheet, 3100 + Layer);
    for (double Sign : {-1., 1.})
    {
        MaterialRectangle(Studio, {Sign * 1.55, -.60, 5.4}, {Sign * .62, 0, 0}, {0, -.55, 0}, 0,
                          Sign < 0 ? 3200u : 3201u);
        for (size_t I = Studio.Surfaces.size() - 2; I < Studio.Surfaces.size(); ++I)
            Studio.Surfaces[I].Emissive = {240.f, 226.f, 204.f, 0};
    }
    MaterialCone(Studio, -1.55, .68, Green, Band);
    MaterialCone(Studio, 1.55, .68, Orange, Band);
    MaterialRectangle(Studio, {-2.55, 1.15, .58}, {.42, .18, 0}, {0, 0, .58}, Fabric, 3300);
    for (size_t I = Studio.Surfaces.size() - 2; I < Studio.Surfaces.size(); ++I)
    {
        auto& F = Studio.Surfaces[I];
        F.A[3] = 0; F.B[3] = 1; F.C[3] = 1;
        F.NormalA[3] = 0; F.NormalB[3] = 0; F.NormalC[3] = 1;
    }
    const SpatialVector Block{0, -.85, .42};
    const SpatialVector Hx{.42, 0, 0}, Hy{0, .20, 0}, Hz{0, 0, .42};
    MaterialRectangle(Studio, Sub(Block, Hy), Hx, Hz, Glass, 3400);
    MaterialRectangle(Studio, Add(Block, Hy), Mul(Hx, -1), Hz, Glass, 3400);
    MaterialRectangle(Studio, Add(Block, Hx), Hy, Hz, Glass, 3400);
    MaterialRectangle(Studio, Sub(Block, Hx), Mul(Hy, -1), Hz, Glass, 3400);
    MaterialRectangle(Studio, Add(Block, Hz), Hx, Hy, Glass, 3400);
    MaterialRectangle(Studio, Sub(Block, Hz), Hx, Mul(Hy, -1), Glass, 3400);
    RebuildMaterialVisibility(Studio);
    MaterialCamera(Studio, {0, -5.85, 1.55}, {0, .35, 1.38}, FrameWidth, FrameHeight);
    Studio.Camera[8][2] = 1e9f;

    LightComputeShape ShaftLight{};
    const auto SunDirection = Unit(SpatialVector{0, .42, .9075});
    ShaftLight.PositionReach = {float(SunDirection.X), float(SunDirection.Y), float(SunDirection.Z), 0};
    ShaftLight.RadianceCategory = {48.f, 44.f, 38.f, 0};
    ShaftLight.PeakActive = {1, 1, 0, 0};
    Studio.Lights = {ShaftLight};
    EmissiveQuadrature::AppendDistribution(Studio);
    const uint32_t FogOffset = uint32_t(Studio.Angles.size());
    FogRadianceSpecification Fog;
    Fog.Medium.DensityAtBase = .075;
    Fog.Medium.BaseHeightM = 0;
    Fog.Medium.FalloffHeightM = 60;
    Fog.ScatteringAlbedo = .95;
    Fog.PhaseAsymmetry = .62;
    Fog.MaximumViewDistanceM = 20;
    Fog.ViewSamples = 48;
    Fog.CloudShadowSamples = 8;
    Fog.AmbientRadiance = {};
    Fog.CloudShadowsEnabled = false;
    Fog.MaximumLightDistanceM = 40;
    Fog.FacetSamples = 4;
    const auto FogPayload = CompileFogRadiancePayload(Fog);
    Studio.Angles.insert(Studio.Angles.end(), FogPayload.begin(), FogPayload.end());

    auto Capacity = Studio;
    Capacity.Angles.resize(Capacity.Angles.size() + 8192);
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Capacity, Shaders);
    if (!Original)
        std::cout << "PANE_TRANSMITTANCE_DEVICE name=" << Compute.Properties.deviceName
                  << " type=" << Compute.Properties.deviceType << std::endl;

    // ① Compact surface area-emitter quadrature across 0, 1, 2, oblique and 8 thin panes.
    {
        auto AirScene = MakeSurfaceFixture(0, 0, .45, 12.0);
        auto SingleScene = MakeSurfaceFixture(1, 0, .45, 12.0);
        auto DoubleScene = MakeSurfaceFixture(2, 0, .45, 12.0);
        auto ObliqueScene = MakeSurfaceFixture(1, 1.05, .45, 12.0);
        auto WideSingleScene = MakeSurfaceFixture(1, 0, 16.0, 24.0);
        auto EightScene = MakeSurfaceFixture(8, 0, 16.0, 24.0);

        auto MeanReceiverRgb = [](const std::vector<std::array<float, 4>>& Pixels,
                                  const std::vector<LightComputeReceiver>& Receivers, uint32_t& Count)
        {
            std::array<double, 3> Sum{};
            Count = 0;
            for (size_t I = 0; I < Pixels.size(); ++I)
            {
                if (Receivers[I].MotionPositionToken[3] != 1300) continue;
                for (uint32_t C = 0; C < 3; ++C)
                {
                    Require(std::isfinite(Pixels[I][C]) && Pixels[I][C] >= 0, "Nonfinite pane receiver radiance");
                    Sum[C] += Pixels[I][C];
                }
                ++Count;
            }
            Require(Count > 4000, "Insufficient compact pane receiver coverage");
            return std::array<double, 3>{Sum[0] / Count, Sum[1] / Count, Sum[2] / Count};
        };

        const auto AirNee = Compute.Execute(AirScene, 0, 16, 71, 64, 0, true, true, SurfaceSamples, false, true);
        const auto AirReceivers = Compute.PrimaryReceivers();
        const auto SingleNee = Compute.Execute(SingleScene, 0, 16, 71, 64, 0, true, true, SurfaceSamples, false, true);
        const auto SingleReceivers = Compute.PrimaryReceivers();
        const auto WideNee = Compute.Execute(WideSingleScene, 0, 16, 71, 64, 0, true, true, SurfaceSamples, false, true);
        const auto WideReceivers = Compute.PrimaryReceivers();
        const auto WideBsdf = Compute.Execute(WideSingleScene, 0, 16, 71, 64, 0, true, true, SurfaceSamples, false, false);
        const auto EightNee = Compute.Execute(EightScene, 0, 16, 71, 64, 0, true, true, SurfaceSamples, false, true);
        const auto EightReceivers = Compute.PrimaryReceivers();
        const auto EightBsdf = Compute.Execute(EightScene, 0, 16, 71, 64, 0, true, true, SurfaceSamples, false, false);

        uint32_t Count = 0;
        const auto AirMean = MeanReceiverRgb(AirNee, AirReceivers, Count);
        const auto SingleMean = MeanReceiverRgb(SingleNee, SingleReceivers, Count);
        const auto WideMean = MeanReceiverRgb(WideNee, WideReceivers, Count);
        const auto WideBsdfMean = MeanReceiverRgb(WideBsdf, WideReceivers, Count);
        const auto EightMean = MeanReceiverRgb(EightNee, EightReceivers, Count);
        const auto EightBsdfMean = MeanReceiverRgb(EightBsdf, EightReceivers, Count);

        if (Original)
        {
            Require(AirNee == SingleNee, "Pinned shader no longer leaks unattenuated NEE through a thin pane");
            Require(WideMean[0] > WideBsdfMean[0] * 1.07,
                    "Pinned shader did not over-predict BSDF behind a thin pane");
            Require(EightMean[0] == 0 && EightMean[1] == 0 && EightMean[2] == 0 && EightBsdfMean[1] > 0,
                    "Pinned shader did not drop eight-pane NEE+BSDF energy");
            std::cout << "PANE_TRANSMITTANCE_ORIGINAL single_nee_equals_air=1 nee_over_bsdf_red="
                      << WideMean[0] / WideBsdfMean[0] << " eight_pane_nee_sum=0 eight_pane_bsdf_green="
                      << EightBsdfMean[1] << std::endl;
        }
        else
        {
            const auto DoubleNee = Compute.Execute(DoubleScene, 0, 16, 71, 64, 0, true, true, SurfaceSamples, false, true);
            const auto DoubleReceivers = Compute.PrimaryReceivers();
            const auto ObliqueNee = Compute.Execute(ObliqueScene, 0, 16, 71, 64, 0, true, true, SurfaceSamples, false, true);
            const auto ObliqueReceivers = Compute.PrimaryReceivers();
            const auto DoubleMean = MeanReceiverRgb(DoubleNee, DoubleReceivers, Count);
            const auto ObliqueMean = MeanReceiverRgb(ObliqueNee, ObliqueReceivers, Count);

            double MaxOracleError = 0;
            uint32_t OracleProbes = 0;
            auto CheckOracle = [&](const LightComputeArchive& Scene, const std::vector<std::array<float, 4>>& Pixels,
                                   const std::vector<LightComputeReceiver>& Receivers)
            {
                RaySpace Space(Scene);
                for (uint32_t I = 0; I < Pixels.size(); I += 19)
                {
                    if (Receivers[I].MotionPositionToken[3] != 1300) continue;
                    const auto Expected =
                        IntegrateReceiverFacets(Scene, Space.Space, Receivers[I], I, 71, SurfaceSamples);
                    MaxOracleError = std::max({MaxOracleError, std::abs(Pixels[I][0] - Expected.R),
                                               std::abs(Pixels[I][1] - Expected.G),
                                               std::abs(Pixels[I][2] - Expected.B)});
                    ++OracleProbes;
                }
            };
            CheckOracle(AirScene, AirNee, AirReceivers);
            CheckOracle(SingleScene, SingleNee, SingleReceivers);
            CheckOracle(DoubleScene, DoubleNee, DoubleReceivers);
            CheckOracle(ObliqueScene, ObliqueNee, ObliqueReceivers);
            Require(OracleProbes > 1000 && MaxOracleError < 5e-6, "GPU/native thin-pane emitter quadrature mismatch");

            for (size_t I = 0; I < SingleNee.size(); ++I)
                if (SingleReceivers[I].MotionPositionToken[3] == 1300)
                    for (uint32_t C = 0; C < 3; ++C)
                        Require(SingleNee[I][C] < AirNee[I][C] && ObliqueNee[I][C] < SingleNee[I][C] &&
                                    DoubleNee[I][C] < SingleNee[I][C],
                                "Thin pane failed per-pixel monotonic/oblique emitter attenuation");
            Require(SingleMean[1] > SingleMean[2] && SingleMean[2] > SingleMean[0] &&
                        ObliqueMean[1] / ObliqueMean[0] > SingleMean[1] / SingleMean[0],
                    "Thin-pane emitter quadrature lost spectral tint ordering");

            double EstimatorError = 0;
            for (uint32_t C = 0; C < 3; ++C)
                EstimatorError =
                    std::max(EstimatorError, std::abs(WideMean[C] - WideBsdfMean[C]) / WideBsdfMean[C]);
            Require(EstimatorError < .015, "Thin-pane NEE and hemisphere BSDF estimators disagreed");
            Require(EightNee == EightBsdf && EightMean[1] > 0 && EightMean[1] < WideMean[1],
                    "Eight-pane BSDF continuation failed to match BSDF or attenuate");

            std::cout << "PANE_SURFACE_QUADRATURE probes=" << OracleProbes << " max_rgb_error=" << MaxOracleError
                      << " single_ratios=" << SingleMean[0] / AirMean[0] << ',' << SingleMean[1] / AirMean[1] << ','
                      << SingleMean[2] / AirMean[2] << " double_green_ratio=" << DoubleMean[1] / AirMean[1]
                      << " oblique_green_ratio=" << ObliqueMean[1] / AirMean[1]
                      << " nee_bsdf_rel_error=" << EstimatorError << " eight_pane_bsdf_continuation=1" << std::endl;
        }
    }

    // ② Multi-stratum authored mixture sampling (cosine + per-stratum GGX) conservation and native convergence.
    if (!Original)
    {
        LightComputeArchive Furnace;
        Prepare(Furnace, 128, 8, true);
        const auto Materials = AppendSceneMaterials(Furnace, Shaders);
        const uint32_t SkyOffset = uint32_t(Furnace.Camera[10][0]);
        const uint32_t SkyFloats = 6 * 8 * 8 * 3;
        for (uint32_t I = 0; I < SkyFloats; ++I) Furnace.Angles[SkyOffset + I] = 1.f;
        const std::array<uint32_t, 4> Presets{Materials[0], Materials[1], Materials[3], Materials[4]};
        for (uint32_t Card = 0; Card < 4; ++Card)
        {
            MaterialRectangle(Furnace, {-1.65 + 1.1 * Card, 0, 1}, {.46, 0, 0}, {0, 0, .85}, Presets[Card], 2100 + Card);
            for (size_t I = Furnace.Surfaces.size() - 2; I < Furnace.Surfaces.size(); ++I)
            {
                auto& F = Furnace.Surfaces[I];
                F.A[3] = F.A[0]; F.B[3] = F.B[0]; F.C[3] = F.C[0];
                F.NormalA[3] = F.A[2]; F.NormalB[3] = F.B[2]; F.NormalC[3] = F.C[2];
            }
        }
        RebuildMaterialVisibility(Furnace);
        MaterialCamera(Furnace, {0, -5.2, 1}, {0, 0, 1}, FrameWidth, FrameHeight);
        Furnace.Camera[8][2] = 1e9f;

        const auto White = Compute.Execute(Furnace, 0, 16, 71, 64, 0, true, true, 64, true);
        const auto WhiteReceivers = Compute.PrimaryReceivers();
        double MaxFurnace = 0;
        std::array<std::array<double, 3>, 4> CardFurnace{};
        std::array<uint32_t, 4> CardPixels{};
        for (size_t I = 0; I < White.size(); ++I)
        {
            const uint32_t Token = uint32_t(WhiteReceivers[I].MotionPositionToken[3]);
            if (Token < 2100 || Token > 2103) continue;
            ++CardPixels[Token - 2100];
            for (uint32_t C = 0; C < 3; ++C)
            {
                Require(std::isfinite(White[I][C]) && White[I][C] >= 0, "Invalid white-furnace mixture sample");
                CardFurnace[Token - 2100][C] += White[I][C];
            }
        }
        for (uint32_t Card = 0; Card < 4; ++Card)
        {
            Require(CardPixels[Card] > 1000, "Missing multi-stratum card coverage");
            for (uint32_t C = 0; C < 3; ++C)
            {
                const double Mean = CardFurnace[Card][C] / CardPixels[Card];
                Require(Mean > 0 && Mean <= 1.01, "Multi-stratum mixture violated white-furnace conservation");
                MaxFurnace = std::max(MaxFurnace, Mean);
            }
        }

        auto MixtureScene = Furnace;
        MixtureScene.Camera[10][3] = 0;
        const uint32_t Sheet = DuplicateSceneMaterial(MixtureScene, Materials[3], 2, {1, 1, 1, 0}, 0);
        MaterialRectangle(MixtureScene, {0, -5.8, 1}, {18, 0, 0}, {0, 0, 14}, Sheet, 2200);
        MaterialRectangle(MixtureScene, {-6.0, -7.2, 1}, {-6.0, 0, 0}, {0, 0, 9}, 0, 2300);
        for (size_t I = MixtureScene.Surfaces.size() - 2; I < MixtureScene.Surfaces.size(); ++I)
            MixtureScene.Surfaces[I].Emissive = {3.2f, 1.4f, .45f, 0};
        MaterialRectangle(MixtureScene, {6.0, -7.2, 1}, {-6.0, 0, 0}, {0, 0, 9}, 0, 2301);
        for (size_t I = MixtureScene.Surfaces.size() - 2; I < MixtureScene.Surfaces.size(); ++I)
            MixtureScene.Surfaces[I].Emissive = {.35f, 1.6f, 3.0f, 0};
        RebuildMaterialVisibility(MixtureScene);

        (void)Compute.Execute(MixtureScene, 0, 16, 71, 64, 0, true, true, 8, false);
        const auto Receivers = Compute.PrimaryReceivers();
        RaySpace Space(MixtureScene);

        std::array<std::array<double, 3>, 4> ReferenceMean{};
        std::array<uint32_t, 4> ReferenceCount{};
        constexpr uint32_t Directions = 512;
        constexpr double π = 3.141592653589793;
        for (size_t I = 0; I < Receivers.size(); I += 19)
        {
            const uint32_t Token = uint32_t(Receivers[I].MotionPositionToken[3]);
            if (Token < 2100 || Token > 2103) continue;
            const uint32_t Card = Token - 2100;
            const auto S = UnpackReceiverScattering(MixtureScene, Receivers[I]);
            const auto N = Unit(Vector(Receivers[I].NormalRoughness));
            const auto V = Unit(Vector(Receivers[I].ViewMetallic));
            const auto T = Unit(Cross(std::abs(N.Z) < .999 ? SpatialVector{0, 0, 1} : SpatialVector{0, 1, 0}, N));
            const auto B = Cross(N, T);
            const auto Origin = Add(Vector(Receivers[I].PositionHit), Mul(N, .003));
            auto Density = [&](SpatialVector L)
            {
                double Sum = std::max(0., Dot(N, L)) / π;
                const auto H = Unit(Add(V, L));
                const double NH = std::clamp(Dot(N, H), 0., 1.);
                const double VH = std::max(1e-6, 4 * std::abs(Dot(V, H)));
                for (uint32_t J = 0; J < S.Configuration[0]; ++J)
                    Sum += EvaluateGgxDistribution(NH, std::max(.04f, S.Strata[J].AlbedoRoughness[3])) * NH / VH;
                return Sum;
            };
            std::array<double, 3> PixelRef{};
            for (uint32_t D = 0; D < Directions; ++D)
            {
                const double U = (D + .5) / Directions;
                const double φ = D * π * (3 - std::sqrt(5.));
                for (uint32_t Proposal = 0; Proposal <= S.Configuration[0]; ++Proposal)
                {
                    SpatialVector L;
                    if (Proposal == 0)
                        L = Unit(Add(Mul(T, std::sqrt(U) * std::cos(φ)),
                                     Add(Mul(B, std::sqrt(U) * std::sin(φ)), Mul(N, std::sqrt(1 - U)))));
                    else
                    {
                        const double Roughness = std::max(.04f, S.Strata[Proposal - 1].AlbedoRoughness[3]);
                        const double A = Roughness * Roughness;
                        const double CosH = std::sqrt((1 - U) / (1 + (A * A - 1) * U));
                        const double SinH = std::sqrt(std::max(0., 1 - CosH * CosH));
                        const auto H = Unit(Add(Mul(T, SinH * std::cos(φ)),
                                                Add(Mul(B, SinH * std::sin(φ)), Mul(N, CosH))));
                        L = Sub(Mul(H, 2 * Dot(V, H)), V);
                    }
                    const double NL = Dot(N, L);
                    if (NL <= 0) continue;
                    auto Hit = Space.Space.Trace(Origin, L, .00001, 1e9);
                    if (!Hit.Hit) continue;
                    SpectrumRgb Pane{1, 1, 1};
                    if (MixtureScene.Surfaces[Hit.Facet].OpticalToken[3] == 2200)
                    {
                        Pane = EvaluateSegmentTransmittance(MixtureScene, Space.Space, Origin, L, Hit.Travel + .05);
                        Hit = Space.Space.Trace(Add(Origin, Mul(L, Hit.Travel + .01)), L, .001, 1e9);
                        if (!Hit.Hit) continue;
                    }
                    const auto& Facet = MixtureScene.Surfaces[Hit.Facet];
                    if (Facet.Emissive[0] == 0 && Facet.Emissive[1] == 0 && Facet.Emissive[2] == 0) continue;
                    const auto H = Unit(Add(V, L));
                    const auto F = EvaluateScatteringSequence(S, Dot(N, V), NL, std::clamp(Dot(N, H), 0., 1.),
                                                              std::clamp(Dot(V, H), 0., 1.));
                    const std::array<double, 3> PaneRgb{Pane.R, Pane.G, Pane.B};
                    const double Weight = NL / (std::max(1e-8, Density(L)) * Directions);
                    for (uint32_t C = 0; C < 3; ++C)
                        PixelRef[C] += F[C] * PaneRgb[C] * Facet.Emissive[C] * Weight;
                }
            }
            for (uint32_t C = 0; C < 3; ++C) ReferenceMean[Card][C] += PixelRef[C];
            ++ReferenceCount[Card];
        }
        for (uint32_t Card = 0; Card < 4; ++Card)
        {
            Require(ReferenceCount[Card] > 50, "Insufficient probed multi-stratum pixels");
            for (uint32_t C = 0; C < 3; ++C) ReferenceMean[Card][C] /= ReferenceCount[Card];
        }

        auto MeasureError = [&](uint32_t Samples)
        {
            const auto Pixels = Compute.Execute(MixtureScene, 0, 16, 71, 64, 0, true, true, Samples, false);
            std::array<std::array<double, 3>, 4> Sum{};
            double MaxCardError = 0;
            for (size_t I = 0; I < Pixels.size(); I += 19)
            {
                const uint32_t Token = uint32_t(Receivers[I].MotionPositionToken[3]);
                if (Token < 2100 || Token > 2103) continue;
                for (uint32_t C = 0; C < 3; ++C) Sum[Token - 2100][C] += Pixels[I][C];
            }
            for (uint32_t Card = 0; Card < 4; ++Card)
                for (uint32_t C = 0; C < 3; ++C)
                    MaxCardError =
                        std::max(MaxCardError, std::abs(Sum[Card][C] / ReferenceCount[Card] - ReferenceMean[Card][C]));
            return MaxCardError;
        };
        const double Error8 = MeasureError(8);
        const double Error32 = MeasureError(32);
        const double Error128 = MeasureError(128);
        Require(Error128 < Error8 && Error128 < .018,
                "Multi-stratum mixture failed native quadrature convergence");
        std::cout << "PANE_MATERIAL_MIXTURES cards=4 max_white_furnace=" << MaxFurnace << " error_8=" << Error8
                  << " error_32=" << Error32 << " error_128=" << Error128 << std::endl;
    }

    // ③ Clerestory studio hall: open bay vs thin-pane bay volumetric fog shafts, closed glass and reused presets.
    {
        // Subtest 3A: isolated analytic volumetric fog shaft probes (black surfaces, zero emissive facets).
        {
            auto AnalyticFog = Studio;
            for (auto& F : AnalyticFog.Surfaces)
            {
                if (F.OpticalToken[3] == 3200 || F.OpticalToken[3] == 3201) F.Emissive = {0, 0, 0, 0};
                if (F.Emissive[3] == 0) F.AlbedoRoughness = {0, 0, 0, .9f};
            }
            AnalyticFog.Angles[FogOffset + 15] = 0;
            const auto Pixels =
                Compute.Execute(AnalyticFog, 0, 16, 71, 64, 0, true, true, 0, false, false, false, UINT32_MAX, FogOffset);
            const auto Receivers = Compute.PrimaryReceivers();
            RaySpace Space(AnalyticFog);

            double MaxFogError = 0;
            uint32_t FogProbes = 0, LeftCount = 0, CentreCount = 0, RightCount = 0;
            std::array<double, 3> LeftSum{}, CentreSum{}, RightSum{};
            for (uint32_t Y = 55; Y < 115; Y += 3)
                for (uint32_t X = 40; X < 280; X += 4)
                {
                    const uint32_t Pixel = Y * AnalyticFog.Width + X;
                    if (Receivers[Pixel].MotionPositionToken[3] != 3001) continue;
                    const double HitDistance = std::min(
                        std::sqrt(Dot(Sub(Vector(Receivers[Pixel].PositionHit), Vector(AnalyticFog.Camera[0])),
                                      Sub(Vector(Receivers[Pixel].PositionHit), Vector(AnalyticFog.Camera[0])))),
                        double(AnalyticFog.Angles[FogOffset + 5]));
                    if (!Original)
                    {
                        const auto Expected =
                            IntegratePaneFogColumn(AnalyticFog, Space, FogOffset, Vector(AnalyticFog.Camera[0]),
                                                   FogCameraRay(AnalyticFog, Pixel), HitDistance, Pixel, 71);
                        MaxFogError = std::max({MaxFogError, std::abs(Pixels[Pixel][0] - Expected.Radiance.R),
                                                std::abs(Pixels[Pixel][1] - Expected.Radiance.G),
                                                std::abs(Pixels[Pixel][2] - Expected.Radiance.B)});
                        ++FogProbes;
                    }
                    const double HitX = Receivers[Pixel].PositionHit[0];
                    if (HitX > -2.05 && HitX < -1.05)
                    {
                        for (uint32_t C = 0; C < 3; ++C) LeftSum[C] += Pixels[Pixel][C];
                        ++LeftCount;
                    }
                    else if (std::abs(HitX) < .35)
                    {
                        for (uint32_t C = 0; C < 3; ++C) CentreSum[C] += Pixels[Pixel][C];
                        ++CentreCount;
                    }
                    else if (HitX > 1.05 && HitX < 2.05)
                    {
                        for (uint32_t C = 0; C < 3; ++C) RightSum[C] += Pixels[Pixel][C];
                        ++RightCount;
                    }
                }
            Require(LeftCount > 40 && CentreCount > 15 && RightCount > 40, "Missing studio fog shaft probe lanes");
            if (Original)
            {
                const double Ratio = (RightSum[0] / RightCount) / (LeftSum[0] / LeftCount);
                Require(Ratio > .95,
                        "Pinned shader did not leave analytic volumetric fog unattenuated behind thin pane");
                std::cout << "PANE_FOG_ANALYTIC_ORIGINAL right_over_left_red=" << Ratio << std::endl;
            }
            else
            {
                Require(FogProbes > 200 && MaxFogError < 1e-5, "Analytic pane fog shaft disagreed with native integral");
                Require(CentreSum[1] == 0 && RightSum[0] / RightCount < LeftSum[0] / LeftCount &&
                            (RightSum[1] / RightSum[0]) > (LeftSum[1] / LeftSum[0]),
                        "Analytic fog shaft failed mullion occlusion or spectral pane attenuation");
                std::cout << "PANE_FOG_ANALYTIC probes=" << FogProbes << " max_rgb_error=" << MaxFogError
                          << " right_over_left_rgb=" << (RightSum[0] / RightCount) / (LeftSum[0] / LeftCount) << ','
                          << (RightSum[1] / RightCount) / (LeftSum[1] / LeftCount) << ','
                          << (RightSum[2] / RightCount) / (LeftSum[2] / LeftCount) << " mullion_zero=1" << std::endl;
                WriteImage(Output + "/PaneTransmittanceAnalytic.ppm", Pixels, AnalyticFog.Width, AnalyticFog.Height);
            }
        }

        // Subtest 3B: isolated emissive-canopy surface + volumetric fog shafts (analytic lights disabled).
        {
            auto EmissiveHall = Studio;
            EmissiveHall.Lights[0].PeakActive[1] = 0;
            EmissiveHall.Angles[FogOffset + 13] = 0;
            EmissiveHall.Angles[FogOffset + 15] = 6;
            const auto Pixels =
                Compute.Execute(EmissiveHall, 0, 16, 71, 64, 0, true, true, 32, false, true, false, UINT32_MAX, FogOffset);
            const auto Receivers = Compute.PrimaryReceivers();
            WriteImage(Output + (Original ? "/PaneTransmittanceEmissiveOriginal.ppm"
                                          : "/PaneTransmittanceEmissiveAccepted.ppm"),
                       Pixels, EmissiveHall.Width, EmissiveHall.Height);

            std::array<double, 3> LeftShaft{}, RightShaft{}, LeftFloor{}, RightFloor{};
            uint32_t LeftShaftN = 0, RightShaftN = 0, LeftFloorN = 0, RightFloorN = 0;
            for (size_t I = 0; I < Pixels.size(); ++I)
            {
                const uint32_t Token = uint32_t(Receivers[I].MotionPositionToken[3]);
                const double X = Receivers[I].PositionHit[0], Y = Receivers[I].PositionHit[1];
                if (Token == 3001 && Receivers[I].PositionHit[2] > 1.3 && Receivers[I].PositionHit[2] < 2.6)
                {
                    if (X > -2.05 && X < -1.05)
                    {
                        for (uint32_t C = 0; C < 3; ++C) LeftShaft[C] += Pixels[I][C];
                        ++LeftShaftN;
                    }
                    if (X > 1.05 && X < 2.05)
                    {
                        for (uint32_t C = 0; C < 3; ++C) RightShaft[C] += Pixels[I][C];
                        ++RightShaftN;
                    }
                }
                if (Token == 3000 && std::abs(Y + .1) < .30)
                {
                    if (X > -2.05 && X < -1.05)
                    {
                        for (uint32_t C = 0; C < 3; ++C) LeftFloor[C] += Pixels[I][C];
                        ++LeftFloorN;
                    }
                    if (X > 1.05 && X < 2.05)
                    {
                        for (uint32_t C = 0; C < 3; ++C) RightFloor[C] += Pixels[I][C];
                        ++RightFloorN;
                    }
                }
            }
            Require(LeftShaftN > 50 && RightShaftN > 50 && LeftFloorN > 40 && RightFloorN > 40,
                    "Insufficient emissive hall shaft/floor coverage");

            if (Original)
            {
                const double FloorRatio = (RightFloor[0] / RightFloorN) / (LeftFloor[0] / LeftFloorN);
                Require(FloorRatio > .95, "Pinned shader did not leak unattenuated emissive floor light");
                std::cout << "PANE_FOG_EMISSIVE_ORIGINAL right_over_left_floor_red=" << FloorRatio << std::endl;
                return;
            }

            auto PureEmissiveFog = EmissiveHall;
            for (auto& F : PureEmissiveFog.Surfaces)
                if (F.OpticalToken[3] != 3200 && F.OpticalToken[3] != 3201 && F.Emissive[3] == 0)
                    F.AlbedoRoughness = {0, 0, 0, .9f};
            const auto FogOnly = Compute.Execute(PureEmissiveFog, 0, 16, 71, 64, 0, true, true, 1, false, true, false,
                                                 UINT32_MAX, FogOffset);
            const auto FogReceivers = Compute.PrimaryReceivers();
            RaySpace Space(PureEmissiveFog);
            double MaxEmissiveFogError = 0;
            uint32_t EmissiveFogProbes = 0;
            for (uint32_t Y = 55; Y < 115; Y += 4)
                for (uint32_t X = 40; X < 280; X += 5)
                {
                    const uint32_t Pixel = Y * PureEmissiveFog.Width + X;
                    if (FogReceivers[Pixel].MotionPositionToken[3] != 3001) continue;
                    const double HitDistance = std::min(
                        std::sqrt(Dot(Sub(Vector(FogReceivers[Pixel].PositionHit), Vector(PureEmissiveFog.Camera[0])),
                                      Sub(Vector(FogReceivers[Pixel].PositionHit), Vector(PureEmissiveFog.Camera[0])))),
                        double(PureEmissiveFog.Angles[FogOffset + 5]));
                    const auto Expected =
                        IntegratePaneFogColumn(PureEmissiveFog, Space, FogOffset, Vector(PureEmissiveFog.Camera[0]),
                                               FogCameraRay(PureEmissiveFog, Pixel), HitDistance, Pixel, 71);
                    MaxEmissiveFogError = std::max({MaxEmissiveFogError,
                                                    std::abs(FogOnly[Pixel][0] - Expected.Radiance.R),
                                                    std::abs(FogOnly[Pixel][1] - Expected.Radiance.G),
                                                    std::abs(FogOnly[Pixel][2] - Expected.Radiance.B)});
                    ++EmissiveFogProbes;
                }
            Require(EmissiveFogProbes > 150 && MaxEmissiveFogError < 2e-5,
                    "Emissive pane fog shaft disagreed with native volume quadrature");
            Require(RightShaft[0] / RightShaftN < LeftShaft[0] / LeftShaftN * .80 &&
                        RightFloor[0] / RightFloorN < LeftFloor[0] / LeftFloorN * .80 &&
                        (RightFloor[1] / RightFloor[0]) > (LeftFloor[1] / LeftFloor[0]),
                    "Emissive hall failed window shaft/floor spectral attenuation");
            std::cout << "PANE_FOG_EMISSIVE probes=" << EmissiveFogProbes << " max_rgb_error=" << MaxEmissiveFogError
                      << " floor_right_over_left_red=" << (RightFloor[0] / RightFloorN) / (LeftFloor[0] / LeftFloorN)
                      << " shaft_right_over_left_red=" << (RightShaft[0] / RightShaftN) / (LeftShaft[0] / LeftShaftN)
                      << std::endl;
        }

        // Subtest 3C: full combined studio scene (analytic + emissive + fog + closed glass + presets), repeat and blackout.
        const auto Full =
            Compute.Execute(Studio, 0, 16, 71, 64, 0, true, true, 32, false, true, false, UINT32_MAX, FogOffset);
        const auto FullReceivers = Compute.PrimaryReceivers();
        const auto Repeat =
            Compute.Execute(Studio, 0, 16, 71, 64, 0, true, true, 32, false, true, false, UINT32_MAX, FogOffset);
        Require(Full == Repeat, "Combined studio pane scene failed seeded determinism");

        uint32_t SolidPixels = 0, SheetPixels = 0, PaintPixels = 0, FabricPixels = 0, SsrPixels = 0;
        for (size_t I = 0; I < Full.size(); ++I)
        {
            const auto& R = FullReceivers[I];
            for (uint32_t C = 0; C < 3; ++C)
                Require(std::isfinite(Full[I][C]) && Full[I][C] >= 0, "Invalid combined studio radiance");
            if (R.ExpectedDirect[0] == 5)
            {
                Require(uint32_t(R.ExpectedDirect[1]) / 65536 == 0 && R.ExpectedDirect[2] == 0,
                        "Closed glass block capped or unsupported in studio scene");
                ++SolidPixels;
            }
            if (R.ExpectedDirect[0] == 2)
            {
                Require(R.ExpectedDirect[2] == 0, "Primary thin window reported capped/unsupported path");
                ++SheetPixels;
            }
            PaintPixels += R.ExpectedDirect[3] == Green;
            FabricPixels += R.ExpectedDirect[3] == Fabric;
            SsrPixels += R.ExpectedDirect[0] == 3 || R.ExpectedDirect[0] == 4;
        }
        Require(SolidPixels > 100 && SheetPixels > 100 && PaintPixels > 100 && FabricPixels > 40 && SsrPixels > 100,
                "Combined studio scene missed closed-glass, window, paint, fabric or SSR coverage");
        WriteImage(Output + "/PaneTransmittanceScene.ppm", Full, Studio.Width, Studio.Height);

        auto Dark = Studio;
        Dark.Lights[0].PeakActive[1] = 0;
        for (auto& F : Dark.Surfaces) F.Emissive[0] = F.Emissive[1] = F.Emissive[2] = 0;
        Dark.Camera[10][3] = 0;
        const size_t EmitterAt = size_t(Dark.Camera[10][2]);
        Dark.Angles[EmitterAt] = 0;
        const auto Black =
            Compute.Execute(Dark, 0, 16, 71, 64, 0, false, true, 32, false, false, false, UINT32_MAX, FogOffset);
        for (const auto& P : Black)
            Require(P[0] == 0 && P[1] == 0 && P[2] == 0, "Combined studio scene retained radiance with sources off");

        std::cout << "PANE_TRANSMITTANCE_PASS pixels=" << Full.size() << " solid_glass_pixels=" << SolidPixels
                  << " window_pixels=" << SheetPixels << " paint_pixels=" << PaintPixels
                  << " fabric_pixels=" << FabricPixels << " ssr_pixels=" << SsrPixels << " source_off_black=1"
                  << std::endl;
    }
}
