/*==========================================================================================================================================
                                            RECTANGLERECONNECTIONGPUVALIDATION.H
==========================================================================================================================================*/
// 🧩 Test moved-receiver, translated-endpoint and spatial shifts against independent area quadrature.
#pragma once

namespace
{
LightComputeArchive RectangleGpuFixture(float CentreX = 0, float HalfWidth = .5f, float Brightness = 2,
                                              float Height = 1)
{
    auto Scene = DirectGpuFixture(false);
    Scene.Lights.clear();
    for (auto& R : Scene.Receivers)
        if (R.PositionHit[3] != 0) R.AlbedoIor = {.5f,.5f,.5f,1.5f};
    const double L = CentreX - HalfWidth, H = CentreX + HalfWidth, Y = HalfWidth;
    const SpatialVector A{L,-Y,Height}, B{H,-Y,Height}, C{H,Y,Height}, D{L,Y,Height};
    TriangleVisibilitySpace Space;
    Space.Build({{A,B,C}, {A,C,D}});
    Scene.Visibility = Space.PackFloat32();
    for (const auto& T : Scene.Visibility.Corners)
    {
        LightComputeFacet F;
        F.A = T.A; F.B = T.B; F.C = T.C;
        F.NormalA = F.NormalB = F.NormalC = {0,0,-1,0};
        F.AlbedoRoughness = {0,0,0,1};
        F.OpticalToken = {0,1.5f,0,1};
        F.Emissive = {Brightness,Brightness,Brightness,0};
        Scene.Surfaces.push_back(F);
    }
    return Scene;
}
double RectangleAreaIntegral(double ReceiverX, double EmitterX, double HalfWidth = .5, double Height = 1,
                             uint32_t Steps = 1000)
{
    const double H = Height - .001, Delta = 2 * HalfWidth / Steps;
    double Sum = 0;
    for (uint32_t Y = 0; Y < Steps; ++Y)
        for (uint32_t X = 0; X < Steps; ++X)
        {
            const double Dx = EmitterX - HalfWidth + (X + .5) * Delta - ReceiverX;
            const double Dy = -HalfWidth + (Y + .5) * Delta;
            const double R2 = Dx * Dx + Dy * Dy + H * H;
            Sum += H * H / (R2 * R2);
        }
    // Lambertian 0.5 × emission 2 = 1.
    return Sum * Delta * Delta / 3.14159265358979323846;
}
void AddRectangleCentres(DirectGpuMoments& Stats, const std::vector<std::array<float,4>>& Image, uint32_t Width)
{
    for (uint32_t Y = 0; Y < 32; ++Y)
        for (uint32_t X = 0; X < 32; ++X)
            Stats.Accumulate(Image[size_t(3 * Y + 1) * Width + 3 * X + 1][0]);
}
void ValidateRectangleReconnectionGpu(const char* Driver, const std::string& Shaders)
{
    const auto Initial = RectangleGpuFixture();
    LightComputeSubmission Compute(Driver);
    std::printf("RECTANGLE preparing Vulkan pipelines on %s\n", Compute.Properties.deviceName);
    std::fflush(stdout);
    Compute.Prepare(Initial, Shaders, false, false, false, true);
    for (float Shift : {.25f,.5f,1.0f})
    {
        auto Current = Initial;
        for (auto& R : Current.Receivers) if (R.PositionHit[3] != 0) R.PositionHit[0] = Shift;
        const double Exact = RectangleAreaIntegral(Shift, 0);
        DirectGpuMoments Fresh, Corrected, Omitted;
        uint32_t Accepted = 0;
        for (uint32_t Batch = 0; Batch < 4; ++Batch)
        {
            const uint32_t Seed = 1000 + 100 * Batch + uint32_t(Shift * 10);
            AddRectangleCentres(Fresh, Compute.ExecuteRectangle(Current, Seed + 50, false, false, true), Current.Width);
            for (uint32_t Frame = 0; Frame < 8; ++Frame)
                (void)Compute.ExecuteRectangle(Initial, Seed + Frame, true, false, Frame == 0);
            AddRectangleCentres(Corrected, Compute.ExecuteRectangle(Current, Seed + 51, true, false), Current.Width);
            Accepted += Compute.ExtendedStatistics()[20];
            for (uint32_t Frame = 0; Frame < 8; ++Frame)
                (void)Compute.ExecuteRectangle(Initial, Seed + Frame, true, false, Frame == 0);
            AddRectangleCentres(Omitted, Compute.ExecuteRectangle(Current, Seed + 51, true, false, false, true), Current.Width);
        }
        std::printf("RECTANGLE_MOVING shift=%.2f exact=%.12g fresh=%.12g se=%.12g corrected=%.12g se=%.12g "
                    "omitted=%.12g se=%.12g accepted=%u n=%u\n", Shift, Exact, Fresh.Mean(), Fresh.StandardError(),
                    Corrected.Mean(), Corrected.StandardError(), Omitted.Mean(), Omitted.StandardError(), Accepted, Corrected.Count);
        Require(Accepted == 4 * 5120 && std::abs(Corrected.Mean() - Exact) < 6 * Corrected.StandardError() + .002 &&
                    std::abs(Fresh.Mean() - Exact) < 6 * Fresh.StandardError() + .002,
                "Moved-receiver rectangle reconnection failed independent integral");
        if (Shift == 1)
            Require(std::abs(Omitted.Mean() - Exact) > 5 * Omitted.StandardError() + .03 * Exact,
                    "Omitted Jacobian did not expose expected bias");
    }
    // Centre receiver is at x=1; its four donor domains are at x=0.
    auto Spatial = Initial;
    for (uint32_t Y = 0; Y < 32; ++Y)
        for (uint32_t X = 0; X < 32; ++X)
            Spatial.Receivers[size_t(3 * Y + 1) * Spatial.Width + 3 * X + 1].PositionHit[0] = 1;
    DirectGpuMoments SpatialFresh, SpatialCorrected, SpatialOmitted;
    uint32_t Links = 0;
    for (uint32_t Batch = 0; Batch < 4; ++Batch)
    {
        const uint32_t Seed = 3000 + 100 * Batch;
        AddRectangleCentres(SpatialFresh, Compute.ExecuteRectangle(Spatial, Seed, false, false, true), Spatial.Width);
        AddRectangleCentres(SpatialCorrected, Compute.ExecuteRectangle(Spatial, Seed, false, true, true), Spatial.Width);
        Links += Compute.ExtendedStatistics()[22];
        AddRectangleCentres(SpatialOmitted, Compute.ExecuteRectangle(Spatial, Seed, false, true, true, true), Spatial.Width);
    }
    const double SpatialExact = RectangleAreaIntegral(1, 0);
    std::printf("RECTANGLE_SPATIAL exact=%.12g fresh=%.12g se=%.12g corrected=%.12g se=%.12g "
                "omitted=%.12g se=%.12g links=%u n=%u\n", SpatialExact, SpatialFresh.Mean(), SpatialFresh.StandardError(),
                SpatialCorrected.Mean(), SpatialCorrected.StandardError(), SpatialOmitted.Mean(), SpatialOmitted.StandardError(),
                Links, SpatialCorrected.Count);
    Require(Links > 0 && std::abs(SpatialCorrected.Mean() - SpatialExact) < 6 * SpatialCorrected.StandardError() + .002 &&
                std::abs(SpatialOmitted.Mean() - SpatialExact) > 5 * SpatialOmitted.StandardError() + .03 * SpatialExact,
            "Spatial reconnection failed independent integral or negative control");
    // Combine moved-receiver temporal history and different-domain spatial donors in the same frame.
    DirectGpuMoments Combined;
    uint32_t CombinedAccepted = 0, CombinedLinks = 0;
    for (uint32_t Batch = 0; Batch < 4; ++Batch)
    {
        const uint32_t Seed = 4500 + 100 * Batch;
        for (uint32_t Frame = 0; Frame < 8; ++Frame)
            (void)Compute.ExecuteRectangle(Initial, Seed + Frame, true, false, Frame == 0);
        AddRectangleCentres(Combined, Compute.ExecuteRectangle(Spatial, Seed + 9, true, true), Spatial.Width);
        const auto Counts = Compute.ExtendedStatistics();
        CombinedAccepted += Counts[20]; CombinedLinks += Counts[22];
    }
    std::printf("RECTANGLE_COMBINED exact=%.12g mean=%.12g se=%.12g accepted=%u links=%u n=%u\n",
                SpatialExact, Combined.Mean(), Combined.StandardError(), CombinedAccepted, CombinedLinks, Combined.Count);
    Require(CombinedAccepted == 4 * 5120 && CombinedLinks > 0 &&
                std::abs(Combined.Mean() - SpatialExact) < 6 * Combined.StandardError() + .002,
            "Combined temporal/spatial rectangle reuse failed independent integral");
    // Uniform area-preserving translation of the entire emitting rectangle is carried to the endpoint.
    const auto Translated = RectangleGpuFixture(1);
    const double MovedExact = RectangleAreaIntegral(0, 1);
    DirectGpuMoments TranslatedCorrected, TranslatedOmitted;
    uint32_t Carried = 0;
    for (uint32_t Batch = 0; Batch < 4; ++Batch)
    {
        const uint32_t Seed = 5000 + 100 * Batch;
        for (uint32_t Frame = 0; Frame < 8; ++Frame)
            (void)Compute.ExecuteRectangle(Initial, Seed + Frame, true, false, Frame == 0);
        AddRectangleCentres(TranslatedCorrected, Compute.ExecuteRectangle(Translated, Seed + 9, true, false), Translated.Width);
        Carried += Compute.ExtendedStatistics()[23];
        Require(!Compute.LastRectangleReset, "Pure translation unexpectedly reset history");
        for (uint32_t Frame = 0; Frame < 8; ++Frame)
            (void)Compute.ExecuteRectangle(Initial, Seed + Frame, true, false, Frame == 0);
        AddRectangleCentres(TranslatedOmitted, Compute.ExecuteRectangle(Translated, Seed + 9, true, false, false, true), Translated.Width);
    }
    std::printf("RECTANGLE_ENDPOINT translation=1 exact=%.12g corrected=%.12g se=%.12g "
                "omitted=%.12g se=%.12g carried=%u n=%u\n", MovedExact, TranslatedCorrected.Mean(),
                TranslatedCorrected.StandardError(), TranslatedOmitted.Mean(), TranslatedOmitted.StandardError(), Carried,
                TranslatedCorrected.Count);
    Require(Carried == 4 * 5120 && std::abs(TranslatedCorrected.Mean() - MovedExact) < 6 * TranslatedCorrected.StandardError() + .002 &&
                std::abs(TranslatedOmitted.Mean() - MovedExact) > 5 * TranslatedOmitted.StandardError() + .03 * MovedExact,
            "Translated endpoint reconnection failed independent integral or negative control");
    // Coloured transport exercises luminance selection without assuming equal RGB values.
    auto ColourPrevious = Initial;
    for (auto& F : ColourPrevious.Surfaces) F.Emissive = {2,.5f,1,0};
    for (auto& R : ColourPrevious.Receivers)
        if (R.PositionHit[3] != 0) R.AlbedoIor = {.25f,.5f,.75f,1.5f};
    auto ColourCurrent = ColourPrevious;
    for (auto& R : ColourCurrent.Receivers) if (R.PositionHit[3] != 0) R.PositionHit[0] = 1;
    DirectGpuMoments Colour[3];
    for (uint32_t Batch = 0; Batch < 4; ++Batch)
    {
        const uint32_t Seed = 5500 + 100 * Batch;
        for (uint32_t Frame = 0; Frame < 8; ++Frame)
            (void)Compute.ExecuteRectangle(ColourPrevious, Seed + Frame, true, false, Frame == 0);
        const auto Image = Compute.ExecuteRectangle(ColourCurrent, Seed + 9, true, false);
        for (uint32_t Y = 0; Y < 32; ++Y)
            for (uint32_t X = 0; X < 32; ++X)
                for (uint32_t C = 0; C < 3; ++C)
                    Colour[C].Accumulate(Image[size_t(3 * Y + 1) * ColourCurrent.Width + 3 * X + 1][C]);
    }
    const double ColourExpected[] = {.5 * SpatialExact, .25 * SpatialExact, .75 * SpatialExact};
    for (uint32_t C = 0; C < 3; ++C)
        Require(std::abs(Colour[C].Mean() - ColourExpected[C]) < 6 * Colour[C].StandardError() + .001,
                "RGB moving rectangle estimate failed");
    std::printf("RECTANGLE_RGB exact=(%.9g,%.9g,%.9g) mean=(%.9g,%.9g,%.9g) "
                "se=(%.9g,%.9g,%.9g) n=%u\n", ColourExpected[0], ColourExpected[1], ColourExpected[2],
                Colour[0].Mean(), Colour[1].Mean(), Colour[2].Mean(), Colour[0].StandardError(),
                Colour[1].StandardError(), Colour[2].StandardError(), Colour[0].Count);
    // Receiver and endpoint move together, including a change in source-to-receiver height.
    auto Joint = RectangleGpuFixture(.25f, .5f, 2, 1.5f);
    for (auto& R : Joint.Receivers) if (R.PositionHit[3] != 0) R.PositionHit[0] = .5f;
    const double JointExact = RectangleAreaIntegral(.5, .25, .5, 1.5);
    const double JointCoarse = RectangleAreaIntegral(.5, .25, .5, 1.5, 500);
    DirectGpuMoments JointCorrected, JointOmitted;
    uint32_t JointCarried = 0;
    for (uint32_t Batch = 0; Batch < 4; ++Batch)
    {
        const uint32_t Seed = 6000 + 100 * Batch;
        for (uint32_t Frame = 0; Frame < 8; ++Frame)
            (void)Compute.ExecuteRectangle(Initial, Seed + Frame, true, false, Frame == 0);
        AddRectangleCentres(JointCorrected, Compute.ExecuteRectangle(Joint, Seed + 9, true, false), Joint.Width);
        JointCarried += Compute.ExtendedStatistics()[23];
        for (uint32_t Frame = 0; Frame < 8; ++Frame)
            (void)Compute.ExecuteRectangle(Initial, Seed + Frame, true, false, Frame == 0);
        AddRectangleCentres(JointOmitted, Compute.ExecuteRectangle(Joint, Seed + 9, true, false, false, true), Joint.Width);
    }
    std::printf("RECTANGLE_JOINT receiver_x=.5 endpoint_x=.25 endpoint_z=.5 exact=%.12g quadrature_delta=%.9g "
                "corrected=%.12g se=%.12g omitted=%.12g se=%.12g carried=%u n=%u\n", JointExact,
                std::abs(JointExact - JointCoarse), JointCorrected.Mean(), JointCorrected.StandardError(),
                JointOmitted.Mean(), JointOmitted.StandardError(), JointCarried, JointCorrected.Count);
    Require(JointCarried == 4 * 5120 && std::abs(JointExact - JointCoarse) < 1e-6 &&
                std::abs(JointCorrected.Mean() - JointExact) < 6 * JointCorrected.StandardError() + .002 &&
                std::abs(JointOmitted.Mean() - JointExact) > 5 * JointOmitted.StandardError() + .03 * JointExact,
            "Joint receiver/endpoint motion failed independent integral or negative control");
    // A changed receiver material invalidates that pixel even when support and rectangle are unchanged.
    for (uint32_t Frame = 0; Frame < 4; ++Frame)
        (void)Compute.ExecuteRectangle(Initial, 6900 + Frame, true, false, Frame == 0);
    auto ChangedMaterial = Initial;
    for (auto& R : ChangedMaterial.Receivers) if (R.PositionHit[3] != 0) R.AlbedoIor[0] += 5e-5f;
    const auto MaterialReuse = Compute.ExecuteRectangle(ChangedMaterial, 6910, true, false);
    const auto MaterialCounts = Compute.ExtendedStatistics();
    const bool MaterialGlobalReset = Compute.LastRectangleReset;
    const auto MaterialCold = Compute.ExecuteRectangle(ChangedMaterial, 6910, true, false, true);
    Require(!MaterialGlobalReset && MaterialCounts[21] == 5120 && MaterialReuse == MaterialCold,
            "Changed receiver material must reject pixel history and match cold");
    std::printf("RECTANGLE_MATERIAL rejected=%u global_reset=%u matches_cold=1\n", MaterialCounts[21],
                MaterialGlobalReset ? 1u : 0u);
    for (uint32_t Frame = 0; Frame < 4; ++Frame)
        (void)Compute.ExecuteRectangle(Initial, 7000 + Frame, true, false, Frame == 0);
    const auto Resized = RectangleGpuFixture(0, .375f);
    const auto Change = Compute.ExecuteRectangle(Resized, 7010, true, false);
    const bool ShapeReset = Compute.LastRectangleReset;
    const auto Cold = Compute.ExecuteRectangle(Resized, 7010, true, false, true);
    Require(ShapeReset && Change == Cold, "Changed rectangle support did not reset history");
    for (uint32_t Frame = 0; Frame < 4; ++Frame)
        (void)Compute.ExecuteRectangle(Initial, 7100 + Frame, true, false, Frame == 0);
    const auto Dimmed = RectangleGpuFixture(0, .5f, 1);
    const auto Emission = Compute.ExecuteRectangle(Dimmed, 7110, true, false);
    const bool EmissionReset = Compute.LastRectangleReset;
    const auto EmissionCold = Compute.ExecuteRectangle(Dimmed, 7110, true, false, true);
    Require(EmissionReset && Emission == EmissionCold, "Changed rectangle radiance did not reset history");
    for (uint32_t Frame = 0; Frame < 4; ++Frame)
        (void)Compute.ExecuteRectangle(Initial, 7200 + Frame, true, false, Frame == 0);
    auto Reshaped = Initial;
    Reshaped.Width = 192; Reshaped.Height = 48;
    const auto ReshapedResult = Compute.ExecuteRectangle(Reshaped, 7210, true, false);
    const bool ExtentReset = Compute.LastRectangleReset;
    const auto ReshapedCold = Compute.ExecuteRectangle(Reshaped, 7210, true, false, true);
    Require(ExtentReset && ReshapedResult == ReshapedCold, "Changed viewport shape did not reset history");
    auto Invalid = Initial;
    Invalid.Surfaces.push_back(Invalid.Surfaces[0]);
    bool Rejected = false;
    try { (void)Compute.ExecuteRectangle(Invalid, 1, true, false); }
    catch (const std::exception&) { Rejected = true; }
    Require(Rejected, "Additional geometry must be rejected by rectangle domain");
    std::printf("RECTANGLE_GUARDS shape_reset=%u emission_reset=%u viewport_reset=%u extra_geometry_rejected=%u matches_cold=1\n",
                ShapeReset ? 1u : 0u, EmissionReset ? 1u : 0u, ExtentReset ? 1u : 0u, Rejected ? 1u : 0u);
    std::printf("PASS restricted rectangle reconnection; not general dynamic or multibounce ReSTIR GI\n");
}
} // namespace
