/*==========================================================================================================================================
                                                       FOGFACETVALIDATION.H
==========================================================================================================================================*/
// 🧩 Isolate emissive fog on the original vehicle, then verify area scattering, toggles and empty
// distributions.

//------------------------------------------------------------------------------------------------------------------------
//                                                    VULKAN COMPARISONS
//------------------------------------------------------------------------------------------------------------------------

void ValidateFogFacets(
    const char*         Driver,
    const std::string&  Shaders,
    LightComputeArchive Scene,
    const std::string&  Output,
    bool                Capture)
{
    using namespace TransportReference;
    SetFogCamera(Scene, Capture ? 160 : 64, Capture ? 100 : 40);
    // 📝 Explicit calibration panel, not a replacement vehicle or an undocumented source boost.
    auto Panel = [&](std::array<float, 4> A, std::array<float, 4> B, std::array<float, 4> C) {
        LightComputeFacet F;
        F.A = A;
        F.B = B;
        F.C = C;
        F.NormalA = F.NormalB = F.NormalC = {0, 0, 1, 0};
        F.AlbedoRoughness = {.2f, .2f, .2f, .5f};
        F.OpticalToken = {1.5f, 0, .5f, float(Scene.Surfaces.size() + 1)};
        F.Emissive = {35, 9, 2, 0};
        Scene.Surfaces.push_back(F);
    };
    Panel({-1, -1, 6, 0}, {1, -1, 6, 0}, {1, 1, 6, 0});
    Panel({-1, -1, 6, 0}, {1, 1, 6, 0}, {-1, 1, 6, 0});
    LightComputeArchive Rectangle;
    Rectangle.Angles.push_back(0);
    Rectangle.Surfaces = {Scene.Surfaces[Scene.Surfaces.size() - 2], Scene.Surfaces.back()};
    for (auto &Facet : Rectangle.Surfaces)
        Facet.A[2] = Facet.B[2] = Facet.C[2] = 2;
    EmissiveQuadrature::AppendDistribution(Rectangle);
    FogParameters Vacuum;
    Vacuum.DensityAtBase = 0;
    TriangleVisibilitySpace Unoccluded;
    uint32_t Bits = 71;
    auto Integral =
        IntegrateFogFacets(Rectangle, Unoccluded, Vacuum, {}, {0, 0, 1}, 0, 100, false, Bits, 131072);
    constexpr double π = 3.141592653589793;
    double SolidAngle = 4 * std::atan(1 / (2 * std::sqrt(6.)));
    double Expected = 35 * SolidAngle / (4 * π);
    double RelativeError = std::abs(Integral.R - Expected) / Expected;
    Require(RelativeError < .002, "Isotropic area scattering violates rectangle solid angle");
    std::cout << "FOG_FACET_ANALYTIC samples=131072 relative_error=" << RelativeError
              << " expected_red=" << Expected << " native_red=" << Integral.R << std::endl;
    std::vector<std::array<SpatialVector, 3>> Triangles;
    for (const auto &F : Scene.Surfaces)
        Triangles.push_back({Vector(F.A), Vector(F.B), Vector(F.C)});
    TriangleVisibilitySpace Space;
    Space.Build(std::move(Triangles));
    Scene.Visibility = Space.PackFloat32();
    auto Unsorted = Scene.Surfaces;
    for (uint32_t I = 0; I < Scene.Surfaces.size(); ++I)
        Scene.Surfaces[I] = Unsorted[Scene.Visibility.OriginalFacets[I]];
    auto Analytic = Scene.Lights;
    for (auto &L : Scene.Lights)
        L.PeakActive[1] = 0;
    Prepare(Scene, 4, 8, false);
    uint32_t Offset = AppendFogParameters(Scene);
    float *F = Scene.Angles.data() + Offset;
    F[0] = .07f;
    F[2] = 12;
    F[4] = .25f;
    F[5] = 70;
    F[6] = 128;
    F[8] = F[9] = F[10] = 0;
    F[12] = 0;
    F[13] = 0;
    F[15] = 16;
    EmissiveQuadrature::AppendDistribution(Scene);
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Scene, Shaders);
    auto Execute = [&](const LightComputeArchive &Shot, bool Resolve = true, uint32_t Seed = 71) {
        return Compute.Execute(Shot, 0, 16, Seed, 64, 0, true, true, 4, false, true, false, UINT32_MAX,
                               Offset, Resolve);
    };
    auto Background = Execute(Scene, false);
    auto Surfaces = Compute.PrimaryReceivers();
    double EnabledEnergy = 0, DisabledEnergy = 0;
    std::vector<std::array<float, 4>> Enabled;
    for (uint32_t Case = 0; Case < 4; ++Case)
    {
        auto Shot = Scene;
        if (Case == 0)
            Shot.Angles[Offset + 15] = 0;
        if (Case == 2)
            Shot.Angles[Offset + 11] = 0;
        if (Case == 3)
        {
            Shot.Lights = Analytic;
            Shot.Angles[Offset + 13] = 1;
        }
        auto Baseline = Case == 3 ? Execute(Shot, false) : Background;
        auto Pixels = Execute(Shot);
        FogRayReference Reference(Shot, Offset, {}, {});
        double Maximum = 0, Energy = 0;
        uint32_t Probes = 0;
        for (uint32_t Pixel = 0; Pixel < Pixels.size(); Pixel += Capture ? 137 : 23)
        {
            auto Origin = Vector(Shot.Camera[0]), Ray = FogCameraRay(Shot, Pixel);
            auto Delta = Sub(Vector(Surfaces[Pixel].PositionHit), Origin);
            double Distance = Surfaces[Pixel].PositionHit[3] != 0
                                  ? std::min(std::sqrt(Dot(Delta, Delta)), double(Shot.Angles[Offset + 5]))
                                  : Shot.Angles[Offset + 5];
            auto Integral = Reference.Integrate(Origin, Ray, Distance, 0, 0, Pixel);
            std::array<double, 3> RGB{Integral.Radiance.R, Integral.Radiance.G, Integral.Radiance.B};
            for (uint32_t C = 0; C < 3; ++C)
                Maximum = std::max(Maximum, std::abs(Baseline[Pixel][C] * Integral.Transmittance + RGB[C] -
                                                     Pixels[Pixel][C]));
            ++Probes;
        }
        for (const auto &RGB : Pixels)
            for (uint32_t C = 0; C < 3; ++C)
            {
                Require(std::isfinite(RGB[C]) && RGB[C] >= 0, "Invalid emissive fog radiance");
                Energy += RGB[C];
            }
        Require(Maximum < .003, "Native emissive-volume quadrature disagrees");
        if (Case == 0)
            DisabledEnergy = Energy;
        if (Case == 1)
        {
            Require(Energy > DisabledEnergy, "Area emitters did not illuminate the fog");
            EnabledEnergy = Energy;
            Enabled = Pixels;
        }
        if (Case == 2)
        {
            Require(Energy > EnabledEnergy, "Geometry does not occlude emissive volume scattering");
            for (uint32_t Pixel = 0; Pixel < Pixels.size(); ++Pixel)
                for (uint32_t C = 0; C < 3; ++C)
                    Require(Pixels[Pixel][C] + 1e-6 >= Enabled[Pixel][C],
                            "Removing volume occlusion reduced radiance");
        }
        if (Capture)
            WriteImage(Output + "/FogFacet" + std::to_string(Case) + ".ppm", Pixels, Shot.Width, Shot.Height);
        std::cout << "FOG_FACET case=" << Case << " probes=" << Probes << " max_rgb_error=" << Maximum
                  << " rgb_sum=" << Energy << " width=" << Scene.Width << " height=" << Scene.Height
                  << std::endl;
    }
    Require(Execute(Scene) == Enabled, "Emissive fog cold repeat differs");
    Require(Execute(Scene, false) == Background, "Resolved emitter fog contaminated surface radiance");
    auto Zero = Scene;
    Zero.Angles[Offset] = 0;
    Require(Execute(Zero) == Execute(Zero, false), "Zero-density emitter fog changed output");
    auto Empty = Scene;
    for (auto &Facet : Empty.Surfaces)
        Facet.Emissive = {};
    EmissiveQuadrature::AppendDistribution(Empty);
    auto EmptyOn = Execute(Empty);
    Empty.Angles[Offset + 15] = 0;
    Require(EmptyOn == Execute(Empty), "Empty emitter CDF changed fog radiance");
    for (float Bad : {-1.f, .5f, 65.f})
    {
        auto Invalid = Scene;
        Invalid.Angles[Offset + 15] = Bad;
        bool Rejected = false;
        try
        {
            (void)Execute(Invalid);
        }
        catch (const std::exception &)
        {
            Rejected = true;
        }
        Require(Rejected, "Invalid fog emitter sample budget accepted");
    }
    auto BadCdf = Scene;
    BadCdf.Angles[uint32_t(BadCdf.Camera[10][2]) + 2] = 0;
    bool Rejected = false;
    try
    {
        (void)Execute(BadCdf);
    }
    catch (const std::exception &)
    {
        Rejected = true;
    }
    Require(Rejected, "Invalid fog emitter CDF accepted");
    std::cout << "FOG_FACET_CHECKS cold_equal=1 surface_isolation_equal=1 zero_density_equal=1 "
                 "empty_cdf_equal=1 invalid_budget_rejected=1 invalid_cdf_rejected=1\n";
}
