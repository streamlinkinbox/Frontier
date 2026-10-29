/*==========================================================================================================================================
                                                       LIGHTSURFACETRANSPORT.H
==========================================================================================================================================*/
// 🧩 Native-reference probes for Vulkan one-bounce transport and a shared atmospheric radiance cube.

namespace TransportReference
{
SpatialVector Add(SpatialVector A, SpatialVector B) { return {A.X + B.X, A.Y + B.Y, A.Z + B.Z}; }
SpatialVector Mul(SpatialVector A, double B) { return {A.X * B, A.Y * B, A.Z * B}; }
SpatialVector Sub(SpatialVector A, SpatialVector B) { return Add(A, Mul(B, -1)); }
double Dot(SpatialVector A, SpatialVector B) { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
SpatialVector Cross(SpatialVector A, SpatialVector B)
{
    return {A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X};
}
SpatialVector Unit(SpatialVector A)
{
    double L = std::sqrt(Dot(A, A));
    return L > 0 ? Mul(A, 1 / L) : SpatialVector{0, 0, 1};
}
template <class T> SpatialVector Vector(const T& A) { return {A[0], A[1], A[2]}; }
SpectrumRgb Colour(const std::array<float, 4>& A) { return {A[0], A[1], A[2]}; }
ReferenceMaterial Material(const LightComputeReceiver& R)
{
    ReferenceMaterial M;
    M.Albedo = Colour(R.AlbedoIor);
    M.Ior = R.AlbedoIor[3];
    M.Roughness = R.NormalRoughness[3];
    M.Metallic = R.ViewMetallic[3];
    M.SpecularLevel = R.ResidualSpecular[3];
    return M;
}
void Prepare(LightComputeArchive& Scene, uint32_t Samples, uint32_t Size, bool Sky,
             const SpectrumRgb* TopSun = nullptr)
{
    constexpr double π = 3.141592653589793;
    Scene.Camera[8][3] = float(Scene.Angles.size());
    Scene.Camera[9][3] = float(Samples);
    for (uint32_t I = 0; I < Samples; ++I)
    {
        double Radius = std::sqrt((I + .5) / Samples), Angle = I * π * (3 - std::sqrt(5.));
        Scene.Angles.push_back(float(Radius * std::cos(Angle)));
        Scene.Angles.push_back(float(Radius * std::sin(Angle)));
        Scene.Angles.push_back(float(std::sqrt(1 - Radius * Radius)));
    }
    Scene.Camera[10] = {float(Scene.Angles.size()), float(Size), 0, Sky ? 1.f : 0.f};
    AtmosphereParameters Parameters;
    TransmittanceLut Lut;
    Require(Lut.Bake(Parameters), "Atmosphere LUT bake failed");
    SpatialVector Sun = Unit({-.4, -.6, .7});
    SpectrumRgb Sunlight{4.7, 4.7, 4.7};
    for (const auto& L : Scene.Lights)
        if (L.RadianceCategory[3] == 0)
        {
            Sun = Unit(Vector(L.PositionReach));
            Sunlight = Colour(L.RadianceCategory);
            break;
        }
    if (TopSun)
        Sunlight = *TopSun;
    for (uint32_t Face = 0; Face < 6; ++Face)
        for (uint32_t Y = 0; Y < Size; ++Y)
            for (uint32_t X = 0; X < Size; ++X)
            {
                double U = 2 * (X + .5) / Size - 1, V = 2 * (Y + .5) / Size - 1;
                SpatialVector D = Face == 0   ? SpatialVector{1, U, V}
                                  : Face == 1 ? SpatialVector{-1, U, V}
                                  : Face == 2 ? SpatialVector{U, 1, V}
                                  : Face == 3 ? SpatialVector{U, -1, V}
                                  : Face == 4 ? SpatialVector{U, V, 1}
                                              : SpatialVector{U, V, -1};
                D = Unit(D);
                SpectrumRgb C = Sky && D.Z > 0
                                    ? SingleScattering(Parameters, {0, 0, Parameters.GroundRadiusKm + .004},
                                                       D, Sun, Sunlight, &Lut, 24)
                                    : SpectrumRgb{};
                Scene.Angles.push_back(float(C.R));
                Scene.Angles.push_back(float(C.G));
                Scene.Angles.push_back(float(C.B));
            }
}
SpectrumRgb Environment(const LightComputeArchive& Scene, SpatialVector D)
{
    uint32_t Size = uint32_t(Scene.Camera[10][1]), Offset = uint32_t(Scene.Camera[10][0]);
    if (!Size)
        return {};
    double X = std::abs(D.X), Y = std::abs(D.Y), Z = std::abs(D.Z), U, V;
    uint32_t Face;
    if (X >= Y && X >= Z)
    {
        Face = D.X >= 0 ? 0 : 1;
        U = D.Y / X;
        V = D.Z / X;
    }
    else if (Y >= Z)
    {
        Face = D.Y >= 0 ? 2 : 3;
        U = D.X / Y;
        V = D.Z / Y;
    }
    else
    {
        Face = D.Z >= 0 ? 4 : 5;
        U = D.X / Z;
        V = D.Y / Z;
    }
    U = (U * .5 + .5) * Size - .5;
    V = (V * .5 + .5) * Size - .5;
    int A = int(std::floor(U)), B = int(std::floor(V));
    U -= A;
    V -= B;
    SpectrumRgb Result;
    for (uint32_t J = 0; J < 2; ++J)
        for (uint32_t I = 0; I < 2; ++I)
        {
            uint32_t Index =
                Offset + ((Face * Size + uint32_t(std::clamp(B + int(J), 0, int(Size) - 1))) * Size +
                          uint32_t(std::clamp(A + int(I), 0, int(Size) - 1))) *
                             3;
            double W = (I ? U : 1 - U) * (J ? V : 1 - V) * Scene.Camera[10][3];
            Result.R += Scene.Angles[Index] * W;
            Result.G += Scene.Angles[Index + 1] * W;
            Result.B += Scene.Angles[Index + 2] * W;
        }
    return Result;
}
std::vector<ReferenceLight> Lights(const LightComputeArchive& Scene)
{
    std::vector<ReferenceLight> Result;
    for (const auto& P : Scene.Lights)
    {
        ReferenceLight L;
        L.Kind = static_cast<decltype(L.Kind)>(uint32_t(P.RadianceCategory[3]));
        L.Vector = Vector(P.PositionReach);
        L.Reach = P.PositionReach[3];
        L.Radiance = Colour(P.RadianceCategory);
        L.Direction = Vector(P.DirectionInner);
        L.Up = Vector(P.UpOuter);
        L.InnerCosine = P.DirectionInner[3];
        L.OuterCosine = P.UpOuter[3];
        L.Active = P.PeakActive[1] != 0;
        if (P.Profile[1])
        {
            std::ostringstream Text;
            Text.precision(17);
            Text << "IESNA:LM-63-2002\nTILT=NONE\n1 -1 1 " << P.Profile[1] << " " << P.Profile[3]
                 << " 1 2 0 0 0\n1 1 10\n";
            for (uint32_t J = 0; J < P.Profile[1]; ++J)
                Text << Scene.Angles[P.Profile[0] + J] << " ";
            for (uint32_t J = 0; J < P.Profile[3]; ++J)
                Text << Scene.Angles[P.Profile[2] + J] << " ";
            for (uint32_t J = 0; J < P.Profile[1] * P.Profile[3]; ++J)
                Text << Scene.Angles[uint32_t(P.PeakActive[2]) + J] << " ";
            std::istringstream Input(Text.str());
            L.Photometry = std::make_shared<CandelaDistribution>(CandelaDistribution::Read(Input));
        }
        Result.push_back(L);
    }
    return Result;
}
struct RaySpace
{
    const LightComputeArchive& Scene;
    TriangleVisibilitySpace Space;
    std::vector<ReferenceLight> Sources;
    const CloudParameters* Clouds = nullptr;
    SpatialVector CloudSun{};
    explicit RaySpace(const LightComputeArchive& Input, const CloudParameters* Density = nullptr,
                      SpatialVector Sun = {})
        : Scene(Input), Sources(Lights(Input)), Clouds(Density), CloudSun(Sun)
    {
        std::vector<std::array<SpatialVector, 3>> Geometry;
        for (const auto& F : Scene.Visibility.Corners)
            Geometry.push_back({Vector(F.A), Vector(F.B), Vector(F.C)});
        Space.Build(std::move(Geometry));
    }
    SpectrumRgb Integrate(const LightComputeReceiver& R, uint32_t Samples) const
    {
        constexpr double π = 3.141592653589793;
        auto N = Vector(R.NormalRoughness),
             T = Unit(Cross(std::abs(N.Z) < .999 ? SpatialVector{0, 0, 1} : SpatialVector{0, 1, 0}, N)),
             B = Cross(N, T);
        SpectrumRgb Result;
        uint32_t Offset = uint32_t(Scene.Camera[8][3]);
        for (uint32_t I = 0; I < Samples; ++I)
        {
            auto D =
                Unit(Add(Add(Mul(T, Scene.Angles[Offset + I * 3]), Mul(B, Scene.Angles[Offset + I * 3 + 1])),
                         Mul(N, Scene.Angles[Offset + I * 3 + 2])));
            auto Origin = Add(Vector(R.PositionHit), Mul(N, .002));
            auto Hit = Space.Trace(Origin, D, 0, 1e30, false);
            SpectrumRgb Incoming;
            if (!Hit.Hit)
                Incoming = Environment(Scene, D);
            else
            {
                const auto& F = Scene.Surfaces[Hit.Facet];
                auto Position = Add(Origin, Mul(D, Hit.Travel));
                auto E = Sub(Vector(F.B), Vector(F.A)), G = Sub(Vector(F.C), Vector(F.A)),
                     V = Sub(Position, Vector(F.A));
                double EE = Dot(E, E), GG = Dot(G, G), EG = Dot(E, G), Den = EE * GG - EG * EG;
                double U = (GG * Dot(V, E) - EG * Dot(V, G)) / Den,
                       W = (EE * Dot(V, G) - EG * Dot(V, E)) / Den;
                auto Normal = Unit(Add(Mul(Vector(F.NormalA), 1 - U - W),
                                       Add(Mul(Vector(F.NormalB), U), Mul(Vector(F.NormalC), W))));
                if (Dot(Normal, D) > 0)
                    Normal = Mul(Normal, -1);
                ReferenceMaterial M;
                M.Albedo = Colour(F.AlbedoRoughness);
                M.Roughness = F.AlbedoRoughness[3];
                M.Metallic = F.OpticalToken[0];
                M.Ior = F.OpticalToken[1];
                M.SpecularLevel = F.OpticalToken[2];
                Incoming = Colour(F.Emissive);
                for (const auto& L : Sources)
                {
                    SpatialVector Light;
                    double Distance;
                    auto C = ReferenceLightArrival(L, Position, Light, Distance);
                    if (Clouds && L.Kind == ReferenceLight::Kind::Directional && Dot(L.Vector, CloudSun) > .9999)
                    {
                        double T = MarchClouds(*Clouds, Position, CloudSun, CloudSun, {}, 40000, 64, 0).Transmittance;
                        C = {C.R * T, C.G * T, C.B * T};
                    }
                    double Cosine = std::max(0., Dot(Normal, Light));
                    if (Cosine == 0 || (C.R == 0 && C.G == 0 && C.B == 0))
                        continue;
                    if (Space.Occluded(Add(Position, Mul(Normal, .002)), Light, 0, Distance - .004))
                        continue;
                    auto F = EvaluateReferenceBrdf(M, Normal, Mul(D, -1), Light);
                    Incoming.R += F.R * C.R * Cosine;
                    Incoming.G += F.G * C.G * Cosine;
                    Incoming.B += F.B * C.B * Cosine;
                }
            }
            auto M = Material(R);
            auto View = Unit(Vector(R.ViewMetallic));
            auto H = Unit(Add(View, D));
            double η = std::max(1., M.Ior), Ratio = (η - 1) / (η + 1),
                   Metallic = std::clamp(M.Metallic, 0., 1.);
            double Dielectric = Ratio * Ratio * std::clamp(M.SpecularLevel, 0., 1.) * 2;
            double Grazing = std::pow(1 - std::clamp(Dot(View, H), 0., 1.), 5);
            auto Diffuse = [&](double Albedo)
            {
                double F0 = Dielectric * (1 - Metallic) + Albedo * Metallic;
                return (1 - F0 - (1 - F0) * Grazing) * Albedo * (1 - Metallic) / π;
            };
            SpectrumRgb F{Diffuse(M.Albedo.R), Diffuse(M.Albedo.G), Diffuse(M.Albedo.B)};
            Result.R += F.R * Incoming.R * π / Samples;
            Result.G += F.G * Incoming.G * π / Samples;
            Result.B += F.B * Incoming.B * π / Samples;
        }
        return Result;
    }
};
} // namespace TransportReference
void ValidateSurfaceTransport(const char* Driver, const std::string& Shaders, LightComputeArchive Scene,
                              const std::string& Output)
{
    constexpr uint32_t Samples = 16;
    TransportReference::Prepare(Scene, Samples, 48, true);
    Scene.Save(Output + "/TransportScene.bin");
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Scene, Shaders);
    auto Direct = Compute.Execute(Scene, 0, 16, 1, 64, 0, true, true, 0, true);
    auto Baseline = Compute.PrimaryReceivers();
    auto Transport = Compute.Execute(Scene, 0, 16, 1, 64, 0, true, true, Samples, true);
    auto Receivers = Compute.PrimaryReceivers();
    auto Counts = Compute.HistoryStatistics();
    TransportReference::RaySpace Oracle(Scene);
    double Sum = 0, Maximum = 0, Energy = 0;
    uint32_t Probes = 0;
    for (uint32_t I = 0; I < Scene.Receivers.size(); I += 250)
    {
        const auto& R = Baseline[I];
        if (R.PositionHit[3] == 0)
            continue;
        auto Expected = Oracle.Integrate(R, Samples);
        std::array<double, 3> C{Expected.R, Expected.G, Expected.B};
        for (uint32_t J = 0; J < 3; ++J)
        {
            double Actual = Receivers[I].ResidualSpecular[J] - R.ResidualSpecular[J];
            double Error = std::abs(Actual - C[J]);
            Sum += Error;
            Maximum = std::max(Maximum, Error);
            Energy += Actual;
        }
        ++Probes;
    }
    Require(Probes > 0 && Energy > 0, "Surface transport probes were not illuminated");
    std::cout << "TRANSPORT samples=" << Samples << " probes=" << Probes
              << " mean_rgb_error=" << Sum / (Probes * 3) << " max_rgb_error=" << Maximum
              << " secondary_rays=" << Counts[13] << " secondary_hits=" << Counts[14]
              << " environment_misses=" << Counts[15] << "\n";
    Require(Sum / (Probes * 3) < .001 && Maximum < .02, "Native/GPU one-bounce transport disagreement");
    Require(Counts[13] == Counts[10] * Samples && Counts[14] > 0 && Counts[15] > 0,
            "Transport ray routes not exercised");
    WriteImage(Output + "/Transport.ppm", Transport, Scene.Width, Scene.Height);
    WriteImage(Output + "/TransportDirect.ppm", Direct, Scene.Width, Scene.Height);
    std::vector<std::array<double, 3>> Parts(Receivers.size());
    for (uint32_t Route = 0; Route < 3; ++Route)
    {
        auto Isolated = Scene;
        if (Route != 0)
            for (auto& L : Isolated.Lights)
                L.PeakActive[1] = 0;
        if (Route != 1)
            for (auto& F : Isolated.Surfaces)
                F.Emissive = {};
        if (Route != 2)
            Isolated.Camera[10][3] = 0;
        (void)Compute.Execute(Isolated, 0, 16, 1, 64, 0, true, true, Samples, true);
        auto R = Compute.PrimaryReceivers();
        double RouteEnergy = 0;
        for (size_t I = 0; I < R.size(); ++I)
            if (R[I].PositionHit[3])
                for (uint32_t C = 0; C < 3; ++C)
                {
                    // 📝 Visible primary emission is distinct from emission transported from a secondary hit.
                    double LocalEmission = Route == 1 ? Baseline[I].ResidualSpecular[C] : 0;
                    double Value = R[I].ResidualSpecular[C] - LocalEmission;
                    Parts[I][C] += Value;
                    RouteEnergy += Value;
                }
        std::cout << "TRANSPORT_SOURCE route=" << Route << " indirect_rgb_sum=" << RouteEnergy << "\n";
        Require(RouteEnergy > 0, "An indirect lighting family was not exercised");
    }
    double Linearity = 0;
    for (size_t I = 0; I < Parts.size(); ++I)
        if (Baseline[I].PositionHit[3])
            for (uint32_t C = 0; C < 3; ++C)
                Linearity = std::max(Linearity, std::abs(Parts[I][C] - (Receivers[I].ResidualSpecular[C] -
                                                                        Baseline[I].ResidualSpecular[C])));
    Require(Linearity < .0001, "Analytic, emissive and environment transport were double counted");
    std::cout << "TRANSPORT_LINEARITY max_rgb_error=" << Linearity << "\n";
    auto Dark = Scene;
    for (auto& L : Dark.Lights)
        L.PeakActive[1] = 0;
    for (auto& F : Dark.Surfaces)
        F.Emissive = {};
    Dark.Camera[10][3] = 0;
    auto Black = Compute.Execute(Dark, 0, 16, 1, 64, 0, true, true, Samples, true);
    for (auto P : Black)
        Require(P[0] == 0 && P[1] == 0 && P[2] == 0,
                "Transport retained radiance after all sources disabled");
    std::cout << "TRANSPORT_RESET all_sources_off_black=1 cpu_primary_raster=0 cpu_per_pixel_radiance=0 "
                 "cpu_atmosphere_cube_bake=1 gpu_one_bounce_transport=1\n";
}

void ValidateTransportMotion(const char* Driver, const std::string& Shaders, const std::string& Directory,
                             const std::string& Output,
                             void (*AppendEmissive)(LightComputeArchive&) = nullptr)
{
    auto First = LightComputeArchive::Load(Directory + "/VehicleMotion0.bin");
    TransportReference::Prepare(First, 16, 48, true);
    if (AppendEmissive)
        AppendEmissive(First);
    LightComputeSubmission History(Driver);
    History.Prepare(First, Shaders);
    LightComputeSubmission Exact(Driver);
    Exact.Prepare(First, Shaders);
    for (uint32_t Pose = 0; Pose < 6; ++Pose)
    {
        auto Scene = LightComputeArchive::Load(Directory + "/VehicleMotion" + std::to_string(Pose) + ".bin");
        TransportReference::Prepare(Scene, 16, 48, true);
        if (AppendEmissive)
            AppendEmissive(Scene);
        auto Gold =
            Exact.Execute(Scene, 0, 16, 101 + Pose, 64, 0, true, true, 16, true, AppendEmissive != nullptr);
        auto Current = Exact.PrimaryReceivers();
        auto Image = History.Execute(Scene, 3, 16, 101 + Pose, 64, 7, false, true, 16, true,
                                     AppendEmissive != nullptr);
        auto Reused = History.PrimaryReceivers();
        for (size_t I = 0; I < Current.size(); ++I)
            Require(Current[I].ResidualSpecular == Reused[I].ResidualSpecular,
                    "Moving reconstruction retained stale indirect radiance");
        auto Counts = History.HistoryStatistics();
        if (Pose == 0 || Pose == 3)
            Require(Counts[4] == 0, "Transport history failed light revision reset");
        else
            Require(Counts[4] > 0 && Counts[5] > 0 && Counts[9] > 0,
                    "Moving transport reprojection not exercised");
        std::cout << (AppendEmissive ? "EMISSIVE_MOTION pose=" : "TRANSPORT_MOTION pose=") << Pose
                  << " accepted=" << Counts[4] << " rejected=" << Counts[5]
                  << " current_indirect_equal=1 exact_rmse=" << ImageError(Image, Gold) << "\n";
        WriteImage(Output + (AppendEmissive ? "/EmissiveMotion" : "/TransportMotion") + std::to_string(Pose) +
                       ".ppm",
                   Image, Scene.Width, Scene.Height);
    }
}
