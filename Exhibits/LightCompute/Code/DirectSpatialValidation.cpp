/*==========================================================================================================================================
                                         DIRECT LIGHT SPATIAL REUSE VALIDATION
==========================================================================================================================================*/
// 🧪 Static discrete direct lights: compare stochastic reuse to exhaustive renderer evaluation.

#include "Engine/Rendering/LightInfluenceSpace.h"
#include "Engine/Rendering/ReferenceRenderer.h"
#include "Engine/Rendering/ReservoirSampling.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

using namespace Frontier;

namespace
{

struct Moments
{
    double Sum = 0.0, SumSquared = 0.0;
    uint32_t Count = 0;
    void Add(double X) { Sum += X; SumSquared += X * X; ++Count; }
    double Mean() const { return Sum / Count; }
    double StandardError() const
    {
        const double M = Mean();
        return std::sqrt(std::max(0.0, SumSquared / Count - M * M) / Count);
    }
};

void Require(bool Condition, const char* Message)
{
    if (!Condition) throw std::runtime_error(Message);
}

ReferenceScene PlaneScene()
{
    ReferenceScene Scene;
    ReferenceMesh Plane;
    Plane.Geometry.Vertices = {{-10,-10,0}, {10,-10,0}, {10,10,0}, {-10,10,0}};
    Plane.Geometry.Indices = {0,1,2, 0,2,3};
    Plane.Material.Albedo = {1,1,1};
    Scene.Meshes.push_back(Plane);
    ReferenceLight Up;
    Up.Kind = ReferenceLight::Kind::Directional;
    Up.Vector = {0,0,1};
    ReferenceLight Down = Up;
    Down.Vector = {0,0,-1};
    Scene.Lights = {Up, Down};
    return Scene;
}

CullCamera Camera()
{
    CullCamera C;
    C.Position = {0,0,2}; C.Forward = {0,0,-1}; C.Right = {1,0,0}; C.Up = {0,1,0};
    C.Aspect = 1; C.FovYRadians = 0.6;
    return C;
}

ReferenceRenderSettings Settings()
{
    ReferenceRenderSettings S;
    S.Width = S.Height = 4;
    S.AtmosphereEnabled = false;
    S.IndirectSamples = S.EnvironmentSamples = 0;
    S.AccumulationFrames = 1;
    S.Denoise = false;
    S.RisCandidates = 1;
    S.SpatialReuse = true;
    S.ExhaustiveLights = true;
    S.LightGridMinimum = {-1,-1,-1};
    S.LightGridMaximum = {1,1,1};
    S.LightGridResolution = {16,16,8};
    return S;
}

void Compare(ReferenceRenderer& Renderer, ReferenceRenderSettings Settings, const char* Label,
             uint32_t Frames, uint32_t Trials, bool Clustered, bool Enforce)
{
    constexpr size_t Pixel = 5; // interior pixel with right and below neighbours
    Settings.ExhaustiveLights = true;
    Settings.ClusteredLights = false;
    const auto Exact = Renderer.Render(Camera(), Settings);
    Require(Exact.Stats.SurfacePixels == 16 && Exact.Pixels[Pixel].R > 0, "invalid exhaustive fixture");
    const double Expected = Exact.Pixels[Pixel].R;
    Settings.ExhaustiveLights = false;
    Settings.ClusteredLights = Clustered;
    Settings.AccumulationFrames = Frames;
    Moments Samples;
    for (uint32_t I = 1; I <= Trials; ++I)
    {
        Settings.Seed = static_cast<uint64_t>(I) * 0x9e3779b97f4a7c15ull;
        Samples.Add(Renderer.Render(Camera(), Settings).Pixels[Pixel].R);
    }
    std::printf("%s, %s, %u frames, %u sequences: mean %.9g SE %.9g; exhaustive %.9g; ratio %.9g\n",
                Label, Clustered ? "clustered" : "global", Frames, Trials, Samples.Mean(),
                Samples.StandardError(), Expected, Samples.Mean() / Expected);
    if (Enforce)
        Require(std::abs(Samples.Mean() - Expected) <= 6.0 * Samples.StandardError() + 1e-10,
                "spatial reuse differs from exhaustive direct lighting");
}

void DefaultProbe()
{
    auto Scene = PlaneScene();
    Scene.Distance.AddPrimitive({DistancePrimitiveKind::Sphere, {0,0,1}, {0.3,0,0}, 1.0});
    ReferenceRenderer Renderer;
    Renderer.SetScene(Scene);
    auto S = Settings();
    S.SpatialReuse = false; // this pass deliberately changed; isolate the default-off indirect option
    S.ExhaustiveLights = false;
    S.IndirectSamples = 8;
    S.AccumulationFrames = 4;
    Require(!S.IndirectTemporalReuse, "indirect temporal reuse must default to false");
    const auto Image = Renderer.Render(Camera(), S);
    Require(Image.Stats.SurfacePixels == 16, "invalid default probe scene");
    uint64_t Hash = 1469598103934665603ull;
    for (const auto& Pixel : Image.Pixels)
        for (const double Value : {Pixel.R, Pixel.G, Pixel.B})
        {
            Require(std::isfinite(Value), "nonfinite default probe pixel");
            uint64_t Bits = 0;
            static_assert(sizeof(Bits) == sizeof(Value), "expected binary64 doubles");
            std::memcpy(&Bits, &Value, sizeof(Bits));
            Hash ^= Bits;
            Hash *= 1099511628211ull;
        }
    std::printf("%016llx\n", static_cast<unsigned long long>(Hash));
}

} // namespace

int main(int Argc, char** Argv)
{
    // --observe characterizes a historical renderer without asserting the corrected expectation.
    const bool Enforce = Argc == 1 || std::string(Argv[1]) != "--observe";
    try
    {
        if (Argc > 1 && std::string(Argv[1]) == "--default-probe")
        {
            DefaultProbe();
            return 0;
        }
        // A pre-existing positional aggregate initializer must still bind all original fields.
        const ReferenceRenderSettings Legacy{4, 3, 2, true, false, false, {-1,-1,-1}, {1,1,1},
                                             {4,4,4}, 16, 7, 9, 5, false};
        Require(Legacy.IndirectSamples == 7 && Legacy.EnvironmentSamples == 9 &&
                Legacy.AccumulationFrames == 5 && !Legacy.Denoise && !Legacy.IndirectTemporalReuse,
                "legacy aggregate settings layout changed or temporal reuse is not default-off");
        Reservoir R;
        R.Update({0,0,0}, 6.0, 0.0);
        R.FinalizeWithNormalization(2.0, 3);
        Require(R.Weight() == 1.0, "custom spatial normalization incorrect");
        R.FinalizeWithNormalization(2.0, 0);
        Require(R.Weight() == 0.0, "zero spatial normalization must yield zero");

        ReferenceRenderer Renderer;
        auto Scene = PlaneScene();
        Renderer.SetScene(Scene);
        auto S = Settings();
        Compare(Renderer, S, "same support, sparse event", 1, 10000, false, Enforce);
        Compare(Renderer, S, "same support, sparse event", 24, 1200, false, Enforce);

        // Local point source is supported at pixel 5 but outside reach at its right/below
        // neighbours. The directional light is supported everywhere. No dynamic scenes.
        S.StoreSurfaceSamples = true;
        const auto Surface = Renderer.Render(Camera(), S);
        Require(Surface.SurfaceSamples.size() == 16 && Surface.SurfaceSamples[5].Hit, "receiver export missing");
        const auto Point = Surface.SurfaceSamples[5].Position;
        ReferenceLight Local;
        Local.Kind = ReferenceLight::Kind::Point;
        Local.Vector = {Point.X, Point.Y, Point.Z + 0.15};
        Local.Reach = 0.25;
        Scene.Lights[1] = Local;
        LightInfluenceSpace Grid;
        Grid.Build({{Scene.Lights[0].Vector, 0}, {Local.Vector, Local.Reach}}, S.LightGridMinimum,
                   S.LightGridMaximum, S.LightGridResolution, S.LightCellCapacity);
        const auto ContainsLocal = [&](size_t P) {
            bool Fallback = false;
            const auto& List = Grid.Candidates(Surface.SurfaceSamples[P].Position, &Fallback);
            Require(!Fallback, "unexpected clustered proposal fallback");
            return std::find(List.begin(), List.end(), 1u) != List.end();
        };
        Require(ContainsLocal(5) && !ContainsLocal(6) && !ContainsLocal(9),
                "fixture must exercise a candidate absent from both neighbours' proposal lists");
        std::puts("clustered proposal support: local light present at receiver, absent at both neighbours");
        Renderer.SetScene(Scene);
        S.StoreSurfaceSamples = false;
        Compare(Renderer, S, "unequal target support", 1, 8000, false, Enforce);
        Compare(Renderer, S, "unequal target support", 1, 8000, true, Enforce);
        Compare(Renderer, S, "unequal target support", 24, 1200, true, Enforce);
        std::printf("%s CPU static spatial direct-light checks; no motion or GPU execution\n",
                    Enforce ? "PASS" : "OBSERVED");
    }
    catch (const std::exception& Error)
    {
        std::fprintf(stderr, "FAIL: %s\n", Error.what());
        return 1;
    }
}
