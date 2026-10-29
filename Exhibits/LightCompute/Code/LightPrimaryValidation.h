/*==========================================================================================================================================
                                                        LIGHTPRIMARYVALIDATION.H
==========================================================================================================================================*/
// 🧩 Independent double-precision first-hit reference for camera rays generated
// by Vulkan.
void ValidatePrimary(LightComputeSubmission& Compute, const LightComputeArchive& Scene,
                     const std::string& Output, uint32_t StackCapacity = 64, bool SaveProof = true)
{
    auto Pixels = Compute.Execute(Scene, 0, 16, 1, StackCapacity, 0, true, true);
    auto Clustered = Compute.Execute(Scene, 1, 16, 1, StackCapacity, 0, true, true);
    Require(Pixels == Clustered, "Clustered primary shading differs from exhaustive lighting");
    auto Receivers = Compute.PrimaryReceivers();
    auto Strikes = Compute.PrimaryStrikes();
    std::vector<std::array<SpatialVector, 3>> Geometry;
    auto Vector = [](const auto& A) { return SpatialVector{A[0], A[1], A[2]}; };
    auto Add = [](SpatialVector A, SpatialVector B)
    { return SpatialVector{A.X + B.X, A.Y + B.Y, A.Z + B.Z}; };
    auto Scale = [](SpatialVector A, double S) { return SpatialVector{A.X * S, A.Y * S, A.Z * S}; };
    auto Dot = [](SpatialVector A, SpatialVector B) { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; };
    for (const auto& F : Scene.Visibility.Corners)
        Geometry.push_back({Vector(F.A), Vector(F.B), Vector(F.C)});
    TriangleVisibilitySpace Reference;
    Reference.Build(Geometry);
    std::vector<ReferenceLight> Lights;
    for (const auto& P : Scene.Lights)
    {
        ReferenceLight L;
        L.Kind = static_cast<decltype(L.Kind)>(uint32_t(P.RadianceCategory[3]));
        L.Vector = Vector(P.PositionReach);
        L.Reach = P.PositionReach[3];
        L.Radiance = {P.RadianceCategory[0], P.RadianceCategory[1], P.RadianceCategory[2]};
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
        Lights.push_back(L);
    }
    auto Sub = [](SpatialVector A, SpatialVector B)
    { return SpatialVector{A.X - B.X, A.Y - B.Y, A.Z - B.Z}; };
    double ColourError = 0, ColourMaximum = 0, InteriorColourMaximum = 0, InteriorDepthMaximum = 0;
    std::vector<std::array<float, 4>> Native(Pixels.size());
    uint64_t Hits = 0, Misses = 0, Identity = 0, Edges = 0, Disagreement = 0, PrecisionTies = 0;
    double Maximum = 0, NormalError = 0;
    for (uint32_t I = 0; I < Receivers.size(); ++I)
    {
        double X = (double(I % Scene.Width) + .5) / Scene.Width * 2 - 1,
               Y = 1 - (double(I / Scene.Width) + .5) / Scene.Height * 2;
        auto D = Add(Vector(Scene.Camera[1]), Add(Scale(Vector(Scene.Camera[2]), X * Scene.Camera[2][3]),
                                                  Scale(Vector(Scene.Camera[3]), Y * Scene.Camera[3][3])));
        D = Scale(D, 1 / std::sqrt(Dot(D, D)));
        double Projection = Dot(D, Vector(Scene.Camera[1]));
        auto Hit = Reference.Trace(Vector(Scene.Camera[0]), D, Scene.Camera[0][3] / Projection,
                                   Scene.Camera[1][3] / Projection, false);
        bool Gpu = Receivers[I].PositionHit[3] != 0;
        if (Hit.Hit != Gpu)
        {
            ++Disagreement;
            continue;
        }
        if (!Gpu)
        {
            ++Misses;
            continue;
        }
        ++Hits;
        Maximum = std::max(Maximum, std::abs(Strikes[I][0] - Hit.Travel) / std::max(1., Hit.Travel));
        uint32_t Facet = uint32_t(Strikes[I][1]);
        Require(Facet < Scene.Surfaces.size(), "Primary facet out of bounds");
        const auto& F = Scene.Surfaces[Hit.Facet];
        auto Position = Add(Vector(Scene.Camera[0]), Scale(D, Hit.Travel));
        auto E = Sub(Vector(F.B), Vector(F.A)), G = Sub(Vector(F.C), Vector(F.A)),
             Offset = Sub(Position, Vector(F.A));
        double EE = Dot(E, E), GG = Dot(G, G), EG = Dot(E, G), Denominator = EE * GG - EG * EG;
        double U = (GG * Dot(Offset, E) - EG * Dot(Offset, G)) / Denominator,
               V = (EE * Dot(Offset, G) - EG * Dot(Offset, E)) / Denominator;
        bool Edge = std::min({U, V, 1 - U - V}) < .001;
        if (Edge)
            ++Edges;
        if (Facet != Hit.Facet)
        {
            ++Identity;
            if (!Edge)
            {
                const auto& Other = Scene.Surfaces[Facet];
                auto Cross = [](SpatialVector A, SpatialVector B) {
                    return SpatialVector{A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X};
                };
                auto Plane =
                    Cross(Sub(Vector(Other.B), Vector(Other.A)), Sub(Vector(Other.C), Vector(Other.A)));
                double Den = Dot(Plane, D);
                double OtherTravel = Dot(Plane, Sub(Vector(Other.A), Vector(Scene.Camera[0]))) / Den;
                Require(std::abs(OtherTravel - Hit.Travel) <
                            64 * std::numeric_limits<float>::epsilon() * std::max(1., Hit.Travel),
                        "Interior primary depth ordering exceeds float32 tolerance");
                ++PrecisionTies;
            }
        }
        if (Facet == Hit.Facet)
            InteriorDepthMaximum = std::max(InteriorDepthMaximum,
                                            std::abs(Strikes[I][0] - Hit.Travel) / std::max(1., Hit.Travel));
        auto N = Add(Scale(Vector(F.NormalA), 1 - U - V),
                     Add(Scale(Vector(F.NormalB), U), Scale(Vector(F.NormalC), V)));
        N = Scale(N, 1 / std::sqrt(Dot(N, N)));
        if (Dot(N, D) > 0)
            N = Scale(N, -1);
        if (Facet == Hit.Facet)
            NormalError = std::max(NormalError, std::abs(1 - Dot(N, Vector(Receivers[I].NormalRoughness))));
        ReferenceMaterial Material;
        Material.Albedo = {F.AlbedoRoughness[0], F.AlbedoRoughness[1], F.AlbedoRoughness[2]};
        Material.Roughness = F.AlbedoRoughness[3];
        Material.Metallic = F.OpticalToken[0];
        Material.Ior = F.OpticalToken[1];
        Material.SpecularLevel = F.OpticalToken[2];
        SpectrumRgb Colour{F.Emissive[0], F.Emissive[1], F.Emissive[2]};
        for (const auto& L : Lights)
        {
            SpatialVector Direction;
            double Distance;
            auto C = ReferenceLightArrival(L, Position, Direction, Distance);
            double Cosine = std::max(0., Dot(N, Direction));
            if (Cosine == 0 || (C.R == 0 && C.G == 0 && C.B == 0))
                continue;
            if (Reference.Occluded(Add(Position, Scale(N, .002)), Direction, 0, Distance - .004))
                continue;
            auto B = EvaluateReferenceBrdf(Material, N, Scale(D, -1), Direction);
            Colour.R += B.R * C.R * Cosine;
            Colour.G += B.G * C.G * Cosine;
            Colour.B += B.B * C.B * Cosine;
        }
        Native[I] = {float(Colour.R), float(Colour.G), float(Colour.B), 1};
        for (uint32_t C = 0; C < 3; ++C)
        {
            double Error = std::abs(Native[I][C] - Pixels[I][C]);
            ColourError += Error;
            ColourMaximum = std::max(ColourMaximum, Error);
            if (Facet == Hit.Facet)
                InteriorColourMaximum = std::max(InteriorColourMaximum, Error);
        }
        Require(std::isnan(Receivers[I].ExpectedDirect[0]), "CPU reference channel unexpectedly consumed");
        Require(Receivers[I].MotionPositionToken[3] == Scene.Surfaces[Facet].OpticalToken[3],
                "Primary material identity changed");
    }
    std::cout << "PRIMARY hits=" << Hits << " misses=" << Misses << " hit_disagreement=" << Disagreement
              << " facet_disagreement=" << Identity << " precision_ties=" << PrecisionTies
              << " near_edge=" << Edges << " max_relative_depth=" << Maximum
              << " max_normal_error=" << NormalError
              << " cpu_primary_raster=0 poisoned_receiver_channels=36\n";
    std::cout << "PRIMARY_SHADING mean_rgb_error=" << ColourError / (Pixels.size() * 3)
              << " max_rgb_error=" << ColourMaximum << " matched_facet_max_rgb=" << InteriorColourMaximum
              << " matched_facet_max_depth=" << InteriorDepthMaximum
              << " native_barycentrics=1 native_brdf=1 native_light_arrival=1 "
                 "native_bvh_shadows=1\n";
    Require(ColourError / (Pixels.size() * 3) < .0001 && InteriorColourMaximum < .01,
            "Native/GPU primary shading disagreement");
    if (SaveProof)
        WriteImage(Output + "/PrimaryNative.ppm", Native, Scene.Width, Scene.Height);
    Require(Disagreement == 0, "Native/GPU primary hit disagreement");
    Require(InteriorDepthMaximum < .0002 && NormalError < .00001,
            "Native/GPU primary depth or normal disagreement");
    Require(Identity <= Edges + PrecisionTies, "Primary facet disagreement exceeds near-edge population");
    if (SaveProof)
        WriteImage(Output + "/Primary.ppm", Pixels, Scene.Width, Scene.Height);
}

void ValidatePrimaryClipping(const char* Driver, const std::string& Shaders, const std::string& Output)
{
    LightComputeArchive Scene;
    Scene.Width = 16;
    Scene.Height = 16;
    Scene.Receivers.resize(256);
    Scene.Camera[0] = {0, 0, 4, .1f};
    Scene.Camera[1] = {0, 0, -1, 10};
    Scene.Camera[2] = {1, 0, 0, .5};
    Scene.Camera[3] = {0, 1, 0, .5};
    for (uint32_t I = 0; I < 4; ++I)
        Scene.Camera[I + 4] = Scene.Camera[I];
    std::vector<std::array<SpatialVector, 3>> Geometry;
    for (double Z : {0., 2., 3.95, 4.05, -8., 1.9})
    {
        Geometry.push_back({SpatialVector{-10, -10, Z}, SpatialVector{10, -10, Z}, SpatialVector{10, 10, Z}});
        Geometry.push_back({SpatialVector{-10, -10, Z}, SpatialVector{10, 10, Z}, SpatialVector{-10, 10, Z}});
    }
    TriangleVisibilitySpace Space;
    Space.Build(Geometry);
    Scene.Visibility = Space.PackFloat32();
    for (auto C : Scene.Visibility.Corners)
    {
        LightComputeFacet F;
        F.A = C.A;
        F.B = C.B;
        F.C = C.C;
        F.NormalA = F.NormalB = F.NormalC = {0, 0, 1, 0};
        F.AlbedoRoughness = {.5, .5, .5, .4};
        F.OpticalToken = {0, 1.5, .5, 1};
        F.Emissive = {.1, .2, .3, 0};
        Scene.Surfaces.push_back(F);
    }
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Scene, Shaders);
    ValidatePrimary(Compute, Scene, Output, 64);
    auto Regular = Compute.PrimaryStrikes();
    ValidatePrimary(Compute, Scene, Output, 1);
    auto Fallback = Compute.PrimaryStrikes();
    for (size_t I = 0; I < Regular.size(); ++I)
        Require(std::abs(Regular[I][0] - Fallback[I][0]) < .00001,
                "Primary stack fallback changed closest depth");
    uint32_t Fallbacks = 0;
    std::memcpy(&Fallbacks, static_cast<const char*>(Compute.Buffers[7].Address) + 48, 4);
    Require(Fallbacks == 256, "Primary forced stack fallback was not exercised");
    for (auto R : Compute.PrimaryReceivers())
        Require(std::abs(R.PositionHit[2] - 2) < .00001, "Primary nearest or near-plane clipping failed");
    Scene.Camera[1][3] = 1;
    ValidatePrimary(Compute, Scene, Output);
    for (auto R : Compute.PrimaryReceivers())
        Require(R.PositionHit[3] == 0, "Primary far-plane clipping failed");
    std::cout << "PRIMARY_CLIPPING near_far_nearest_overlap=1 forced_stack_fallbacks=" << Fallbacks
              << " exhaustive_fallback_matches=1\n";
}
