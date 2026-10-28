/*==========================================================================================================================================
                                                CLUSTERSPOTIESVALIDATION.H
==========================================================================================================================================*/
// 🧩 Test non-overflow analytic cells with isolated spot and synthetic IES sources.
#pragma once

namespace
{
struct ClusterSpotFixture
{
    LightComputeArchive Scene;
    uint32_t ShadowedMain = 0;
    double MainEnergy = 0;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  INDEPENDENT RECTANGLE SHADOW
//------------------------------------------------------------------------------------------------------------------------

bool ClusterFixtureShadow(SpatialVector Point, SpatialVector Direction, double Distance, bool Blocker)
{
    if (!Blocker || Direction.Z <= 0) return false;
    // Match the shader's explicit normal offset and open source endpoint; do not use its triangle or BVH code.
    const double T = (1.05 - .002) / Direction.Z;
    if (T < 0 || T > Distance - .004) return false;
    const double X = Point.X + T * Direction.X, Y = Point.Y + T * Direction.Y;
    return X >= -.37 && X <= .37 && Y >= -.65 && Y <= .65;
}

ClusterSpotFixture MakeClusterSpotFixture(uint32_t Kind, double MainX, bool Blocker = true,
                                          bool RotateProfile = false, bool Active = true, bool Outside = false)
{
    ReferenceScene Reference;
    ReferenceLight Main;
    Main.Vector = {MainX, 0, 2};
    Main.Reach = 5;
    Main.Radiance = {25, 18, 12};
    Main.Active = Active;
    Main.Kind = Kind == 1 ? ReferenceLight::Kind::Point : ReferenceLight::Kind::Spot;
    Main.InnerCosine = .96;
    Main.OuterCosine = .72;
    if (Kind != 0)
    {
        std::istringstream Text("IESNA:LM-63-2002\nTILT=NONE\n1 -1 1 3 3 1 2 0 0 "
                                "0\n1 1 10\n0 45 90\n0 "
                                "180 360\n100 80 0 100 20 0 100 80 0");
        Main.Photometry = std::make_shared<CandelaDistribution>(CandelaDistribution::Read(Text));
        if (RotateProfile) Main.Up = {1, 0, 0};
    }
    Reference.Lights.push_back(Main);
    auto Duplicate = Main;
    Duplicate.Active = false; // A spatially overlapping inactive entry forces capacity-one fallback.
    Reference.Lights.push_back(Duplicate);
    for (int Y : {-1, 1})
        for (int X : {-1, 1})
            for (int Radius : {8, 12})
            {
                ReferenceLight Light;
                Light.Vector = {double(X * Radius), double(Y * Radius), 2};
                Light.Reach = 2.8;
                Light.Radiance = {3, 2, 1};
                Reference.Lights.push_back(Light);
            }
    ReferenceMesh Panel;
    Panel.Geometry.Vertices = {{-.37, -.65, 1.05}, {.37, -.65, 1.05},
                               {.37, .65, 1.05}, {-.37, .65, 1.05}};
    Panel.Geometry.Indices = {0, 1, 2, 0, 2, 3};
    if (!Blocker) Panel.Translation.Z = 20;
    Reference.Meshes.push_back(Panel);
    ReferenceImage Image;
    Image.Width = 128;
    Image.Height = 128;
    Image.Pixels.resize(size_t(Image.Width) * Image.Height);
    Image.SurfaceSamples.resize(Image.Pixels.size());
    ClusterSpotFixture Fixture;
    for (size_t I = 0; I < Image.Pixels.size(); ++I)
    {
        auto& R = Image.SurfaceSamples[I];
        R.Hit = true;
        R.Position = {-16 + (double(I % Image.Width) + .5) * .25,
                      -16 + (double(I / Image.Width) + .5) * .25, 0};
        if (Outside && I == 0) R.Position = {16, 0, 0};
        if (Outside && I == 1) R.Position = {-16.25, 0, 0};
        R.Normal = {0, 0, 1};
        R.View = {0, 0, 1};
        R.Material.Albedo = {.4, .6, .7};
        for (size_t Index = 0; Index < Reference.Lights.size(); ++Index)
        {
            const auto& L = Reference.Lights[Index];
            SpatialVector Direction;
            double Distance;
            const auto C = ReferenceLightArrival(L, R.Position, Direction, Distance);
            const auto F = EvaluateReferenceBrdf(R.Material, R.Normal, R.View, Direction);
            const double Cosine = std::max(Direction.Z, 0.);
            if (Index == 0) Fixture.MainEnergy += (F.R * C.R + F.G * C.G + F.B * C.B) * Cosine;
            if (ClusterFixtureShadow(R.Position, Direction, Distance, Blocker))
            {
                if (Index == 0 && (C.R + C.G + C.B) > 0) ++Fixture.ShadowedMain;
                continue;
            }
            R.Direct.R += F.R * C.R * Cosine;
            R.Direct.G += F.G * C.G * Cosine;
            R.Direct.B += F.B * C.B * Cosine;
        }
        Image.Pixels[I] = R.Direct;
    }
    Fixture.Scene = PackLightCompute(Reference, Image);
    return Fixture;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  CLUSTER AND EXHAUSTIVE COMPARISONS
//------------------------------------------------------------------------------------------------------------------------

struct ClusterSpotResult
{
    std::vector<std::array<float,4>> Pixels;
    uint32_t WholeVisits = 0, CellVisits = 0, Fallbacks = 0;
    double NativeError = 0;
};

ClusterSpotResult CheckClusterSpot(LightComputeSubmission& Compute, const ClusterSpotFixture& Fixture,
                                   uint32_t Capacity, uint32_t ExpectedFallbacks, const char* Label)
{
    ClusterSpotResult Result;
    const auto Whole = Compute.Execute(Fixture.Scene, 0, Capacity);
    Result.WholeVisits = Compute.Statistics()[0];
    Result.Pixels = Compute.Execute(Fixture.Scene, 1, Capacity);
    const auto Counts = Compute.Statistics();
    Result.CellVisits = Counts[0];
    Result.Fallbacks = Counts[1];
    Require(Whole == Result.Pixels, "Analytic light cell output differs from exhaustive Vulkan summation");
    if (ExpectedFallbacks != UINT32_MAX)
        Require(Result.Fallbacks == ExpectedFallbacks, "Unexpected analytic cell fallback count");
    else
        Require(Result.Fallbacks > 0, "Forced analytic cell overflow did not fall back");
    for (size_t I = 0; I < Whole.size(); ++I)
        for (uint32_t C = 0; C < 3; ++C)
        {
            Require(std::isfinite(Whole[I][C]), "Nonfinite analytic light readback");
            Result.NativeError = std::max(Result.NativeError,
                std::abs(double(Whole[I][C]) - Fixture.Scene.Receivers[I].ExpectedDirect[C]));
        }
    Require(Result.NativeError < 3e-5, "Analytic light readback differs from native BRDF/profile/rectangle reference");
    std::printf("CLUSTER_%s capacity=%u visits=%u/%u fallback=%u shadowed_main=%u main_energy=%.9g "
                "native_max=%.9g\n", Label, Capacity, Result.CellVisits, Result.WholeVisits, Result.Fallbacks,
                Fixture.ShadowedMain, Fixture.MainEnergy, Result.NativeError);
    return Result;
}

double ClusterImageDifference(const std::vector<std::array<float,4>>& A,
                              const std::vector<std::array<float,4>>& B)
{
    Require(A.size() == B.size(), "Mismatched cluster control images");
    double Total = 0;
    for (size_t I = 0; I < A.size(); ++I)
        for (uint32_t C = 0; C < 3; ++C) Total += std::abs(double(A[I][C]) - B[I][C]);
    return Total;
}

void ValidateClusterSpotIes(const char* Driver, const std::string& Shaders)
{
    const char* Names[] = {"SPOT", "IES", "IES_SPOT"};
    for (uint32_t Kind = 0; Kind < 3; ++Kind)
    {
        auto Initial = MakeClusterSpotFixture(Kind, 0);
        Require(Initial.Scene.Lights.size() == 10 && Initial.ShadowedMain > 0 && Initial.MainEnergy > 1,
                "Spot/IES test fixture did not exercise the active source and its blocker");
        LightComputeSubmission Compute(Driver);
        std::printf("CLUSTER preparing %s on %s\n", Names[Kind], Compute.Properties.deviceName);
        std::fflush(stdout);
        Compute.Prepare(Initial.Scene, Shaders, true);
        const auto Base = CheckClusterSpot(Compute, Initial, 16, 0, Names[Kind]);
        Require(Base.CellVisits * 2 < Base.WholeVisits, "Non-overflow cells did not meaningfully cull lights");
        auto Open = MakeClusterSpotFixture(Kind, 0, false);
        const auto OpenResult = CheckClusterSpot(Compute, Open, 16, 0, "OPEN");
        const double ShadowChange = ClusterImageDifference(Base.Pixels, OpenResult.Pixels);
        Require(ShadowChange > 1, "Opaque panel did not visibly block the main source");
        auto Off = MakeClusterSpotFixture(Kind, 0, true, false, false);
        const auto OffResult = CheckClusterSpot(Compute, Off, 16, 0, "OFF");
        const double DisableChange = ClusterImageDifference(Base.Pixels, OffResult.Pixels);
        Require(DisableChange > 1, "Disabling the active main light did not change the readback");
        for (double X : {4.0, 15.5, -15.5})
        {
            auto Moved = MakeClusterSpotFixture(Kind, X);
            Require(Moved.MainEnergy > 1, "Moved main light is not sampled by the receiver grid");
            CheckClusterSpot(Compute, Moved, 16, 0, "MOVED");
        }
        if (Kind != 0)
        {
            auto Rotated = MakeClusterSpotFixture(Kind, 0, true, true);
            const auto RotatedResult = CheckClusterSpot(Compute, Rotated, 16, 0, "ROTATED_IES");
            const double RotationChange = ClusterImageDifference(Base.Pixels, RotatedResult.Pixels);
            Require(RotationChange > 1, "Rotating the asymmetric synthetic IES profile had no effect");
            std::printf("CLUSTER_PROFILE kind=%s rotation_abs_sum=%.9g\n", Names[Kind], RotationChange);
        }
        const auto Overflow = CheckClusterSpot(Compute, Initial, 1, UINT32_MAX, "OVERFLOW");
        auto Outside = MakeClusterSpotFixture(Kind, 0, true, false, true, true);
        CheckClusterSpot(Compute, Outside, 16, 2, "OUTSIDE");
        std::printf("CLUSTER_CONTROLS kind=%s blocker_abs_sum=%.9g disable_abs_sum=%.9g overflow_queries=%u\n",
                    Names[Kind], ShadowChange, DisableChange, Overflow.Fallbacks);
    }
    std::printf("PASS non-overflow spot, IES and IES-spot cells, movement, shadows and fallbacks\n");
}
} // namespace
