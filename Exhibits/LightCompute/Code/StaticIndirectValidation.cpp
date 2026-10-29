/*==========================================================================================================================================
                                               STATIC INDIRECT VALIDATION
==========================================================================================================================================*/
// 🧩 Compare static temporal SDF-emitter reservoirs with an analytic sphere and exercise the reference renderer.

#include "Engine/Rendering/ReferenceRenderer.h"
#include "Engine/Rendering/StaticIndirectReservoir.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>

using namespace Frontier;

struct Moments
{
    double Sum = 0.0, SquareSum = 0.0;
    uint32_t Count = 0;
    void Add(double X) { Sum += X; SquareSum += X*X; ++Count; }
    double Mean() const { return Sum/Count; }
    double StandardError() const
    {
        const double MeanValue = Mean();
        return std::sqrt(std::max(0.0, SquareSum/Count-MeanValue*MeanValue)/Count);
    }
};

static bool WithinSamplingError(const Moments& Samples, double Expected, double ModelTolerance)
{
    return std::isfinite(Samples.Mean()) && std::abs(Samples.Mean()-Expected) < 5.0*Samples.StandardError()+ModelTolerance;
}

int main()
{
    constexpr double Pi = 3.14159265358979323846;
    DistanceFieldScene Sphere;
    Sphere.AddPrimitive({DistancePrimitiveKind::Sphere,{0,0,1},{0.3,0,0},1.0});
    const SpatialVector Position{0,0,0}, Normal{0,0,1};
    const double AnalyticIrradiance = Pi*std::pow(0.3/(1.0-0.001),2);
    constexpr int Experiments = 12000, Frames = 24;
    Moments Fresh, TemporalFinal, TemporalAverage;
    for (int Run = 0; Run < Experiments; ++Run)
    {
        StaticIndirectReservoir Previous;
        double Average = 0.0;
        for (int Frame = 0; Frame < Frames; ++Frame)
        {
            DeterministicRandom Random(0x91e10da5ull+static_cast<uint64_t>(Run)*0x9e3779b97f4a7c15ull, 0x1100ull+Frame);
            StaticIndirectReservoir Current;
            Current.Stream(Sphere,Position,Normal,1,Random);
            if (Frame == 0)
            {
                StaticIndirectReservoir Initial = Current;
                Initial.Finalize(Sphere); Fresh.Add(Initial.Irradiance(Sphere));
            }
            if (Frame > 0) Current.Reuse(Previous,Sphere,20,Random);
            Current.Finalize(Sphere);
            Average += Current.Irradiance(Sphere)/Frames;
            Previous = Current;
        }
        TemporalFinal.Add(Previous.Irradiance(Sphere));
        TemporalAverage.Add(Average);
    }
    const double Approximation = AnalyticIrradiance*0.004; // permits the SDF trace epsilon, not sampling noise
    bool Success = WithinSamplingError(Fresh,AnalyticIrradiance,Approximation) &&
                   WithinSamplingError(TemporalFinal,AnalyticIrradiance,Approximation) &&
                   WithinSamplingError(TemporalAverage,AnalyticIrradiance,Approximation);
    std::printf("analytic sphere irradiance %.9g; %d independent sequences, %d frames\n", AnalyticIrradiance, Experiments, Frames);
    std::printf("fresh mean %.9g SE %.9g; temporal final mean %.9g SE %.9g; temporal average %.9g SE %.9g\n",
                Fresh.Mean(), Fresh.StandardError(), TemporalFinal.Mean(), TemporalFinal.StandardError(),
                TemporalAverage.Mean(), TemporalAverage.StandardError());

    DistanceFieldScene Pair;
    Pair.AddPrimitive({DistancePrimitiveKind::Sphere,{-0.45,0,1},{0.18,0,0},1.0});
    Pair.AddPrimitive({DistancePrimitiveKind::Sphere,{0.50,0,1},{0.22,0,0},3.0});
    const auto SphereIntegral = [&](double X, double Radius, double Radiance) {
        const double Z = 1.0-0.001, Distance = std::hypot(X,Z);
        return Pi*Radius*Radius*Z/(Distance*Distance*Distance)*Radiance;
    };
    const double PairAnalytic = SphereIntegral(-0.45,0.18,1.0)+SphereIntegral(0.50,0.22,3.0);
    Moments PairFinal;
    for (int Run = 0; Run < 4000; ++Run)
    {
        StaticIndirectReservoir Previous;
        for (int Frame = 0; Frame < 12; ++Frame)
        {
            DeterministicRandom Rng(0x32c19a11ull+static_cast<uint64_t>(Run)*0x9e3779b97f4a7c15ull, 0x2120ull+Frame);
            StaticIndirectReservoir Current; Current.Stream(Pair,Position,Normal,4,Rng);
            if (Frame > 0) Current.Reuse(Previous,Pair,80,Rng);
            Current.Finalize(Pair); Previous = Current;
        }
        PairFinal.Add(Previous.Irradiance(Pair));
    }
    Success &= WithinSamplingError(PairFinal,PairAnalytic,PairAnalytic*0.006);
    std::printf("two unequal off-axis sphere emitters: analytic %.9g, temporal mean %.9g SE %.9g\n",
                PairAnalytic, PairFinal.Mean(), PairFinal.StandardError());

    DistanceFieldScene Black;
    Black.AddPrimitive({DistancePrimitiveKind::HalfSpace,{0,0,1},{0,0,-1},0.0});
    DeterministicRandom Random(91,7);
    StaticIndirectReservoir Empty, Reused;
    Empty.Stream(Black,Position,Normal,1,Random); Empty.Finalize(Black);
    Reused.Stream(Black,Position,Normal,1,Random); Reused.Reuse(Empty,Black,20,Random); Reused.Finalize(Black);
    const bool BlackTally = Empty.SampleCount() == 1 && Reused.SampleCount() == 2 && Reused.Irradiance(Black) == 0.0;
    StaticIndirectReservoir Capped;
    for (int Frame = 0; Frame < 100; ++Frame)
    {
        StaticIndirectReservoir Next; Next.Stream(Black,Position,Normal,1,Random);
        if (Frame > 0) Next.Reuse(Capped,Black,20,Random);
        Next.Finalize(Black); Capped = Next;
    }
    const bool HistoryCapped = Capped.SampleCount() == 21;
    StaticIndirectReservoir Shifted;
    Shifted.Stream(Black,{0.01,0,0},Normal,1,Random); Shifted.Reuse(Empty,Black,20,Random);
    DistanceFieldScene OtherBlack = Black;
    StaticIndirectReservoir OtherScene;
    OtherScene.Stream(OtherBlack,Position,Normal,1,Random); OtherScene.Reuse(Empty,OtherBlack,20,Random);
    StaticIndirectReservoir InvalidNormal;
    InvalidNormal.Stream(Black,Position,{0,0,0},1,Random);
    const bool ContextGuard = Shifted.SampleCount() == 1 && OtherScene.SampleCount() == 1 && InvalidNormal.SampleCount() == 0;
    DistanceFieldScene Obscured = Sphere;
    Obscured.AddPrimitive({DistancePrimitiveKind::Sphere,{0,0,0.5},{0.25,0,0},0.0});
    StaticIndirectReservoir Shadowed;
    Shadowed.Stream(Obscured,Position,Normal,512,Random); Shadowed.Finalize(Obscured);
    const bool Occluded = Shadowed.Irradiance(Obscured) == 0.0;
    DistanceFieldScene Uniform;
    Uniform.AddPrimitive({DistancePrimitiveKind::HalfSpace,{0,0,1},{0,0,-1},1.0});
    StaticIndirectReservoir Sheet;
    Sheet.Stream(Uniform,Position,Normal,64,Random); Sheet.Finalize(Uniform);
    const double UniformError = std::abs(Sheet.Irradiance(Uniform)-Pi);
    Success &= BlackTally && HistoryCapped && ContextGuard && Occluded && UniformError < 1e-10;
    std::printf("zero-weight tally %u -> %u, capped tally %u; context guard %s; occluded irradiance %.9g; uniform-plane error %.9g\n",
                Empty.SampleCount(), Reused.SampleCount(), Capped.SampleCount(), ContextGuard ? "yes" : "no",
                Shadowed.Irradiance(Obscured), UniformError);

    ReferenceScene Scene; Scene.Distance = Sphere;
    ReferenceMesh Ground;
    Ground.Geometry.Vertices = {{-2,-2,0},{2,-2,0},{0,2,0}}; Ground.Geometry.Indices = {0,1,2};
    Ground.Material.Albedo = {1,1,1}; Ground.Material.Metallic = 0;
    Scene.Meshes.push_back(Ground);
    ReferenceRenderer Renderer; Renderer.SetScene(Scene);
    CullCamera Camera; Camera.Position = {0,0,2}; Camera.Forward = {0,0,-1}; Camera.Right = {1,0,0};
    Camera.Up = {0,1,0}; Camera.Aspect = 1; Camera.FovYRadians = 0.6;
    ReferenceRenderSettings Settings; Settings.Width = Settings.Height = 1; Settings.AtmosphereEnabled = false;
    Settings.IndirectSamples = 1; Settings.IndirectTemporalReuse = true; Settings.EnvironmentSamples = 0;
    Settings.AccumulationFrames = 16; Settings.Denoise = false; Settings.SpatialReuse = false;
    Moments RendererMean; bool Surface = true;
    for (uint64_t Seed = 1; Seed <= 1024; ++Seed)
    {
        Settings.Seed = Seed*0x9e3779b97f4a7c15ull;
        const auto Image = Renderer.Render(Camera,Settings);
        Surface &= Image.Stats.SurfacePixels == 1 && std::isfinite(Image.Pixels[0].R);
        RendererMean.Add(Image.Pixels[0].R);
    }
    const double ExpectedExitance = AnalyticIrradiance/Pi;
    Scene.Distance.Clear(); Renderer.SetScene(Scene);
    const auto SourceRemoved = Renderer.Render(Camera,Settings);
    const double RemovedRadiance = SourceRemoved.Pixels[0].R;
    Success &= Surface && RemovedRadiance == 0.0 && WithinSamplingError(RendererMean,ExpectedExitance,ExpectedExitance*0.004);
    std::printf("reference renderer mean %.9g SE %.9g; analytic diffuse radiance %.9g; surface=%s, source removed %.9g\n",
                RendererMean.Mean(), RendererMean.StandardError(), ExpectedExitance, Surface ? "yes" : "no", RemovedRadiance);
    std::printf("%s static temporal reservoir; no spatial shifts, moving geometry, RGB cards, sky or multibounce\n", Success ? "PASS" : "FAIL");
    return Success ? 0 : 1;
}
