/*==========================================================================================================================================
                                                SOLAR REFERENCE RENDER VALIDATION
==========================================================================================================================================*/
// 🧩 Exercise solar filtering and lens visibility through the native reference renderer.

#include "Engine/Rendering/ReferenceRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <vector>

using namespace Frontier;

static SpatialVector Normalize(SpatialVector V)
{
    const double M = std::sqrt(V.X*V.X+V.Y*V.Y+V.Z*V.Z);
    return {V.X/M,V.Y/M,V.Z/M};
}
static double MaxDifference(const ReferenceImage& A, const ReferenceImage& B)
{
    double Result = 0;
    for (size_t I = 0; I < A.Pixels.size(); ++I)
        Result = std::max({Result, std::abs(A.Pixels[I].R-B.Pixels[I].R),
                          std::abs(A.Pixels[I].G-B.Pixels[I].G), std::abs(A.Pixels[I].B-B.Pixels[I].B)});
    return Result;
}
static double SumSignal(const ReferenceImage& A, const ReferenceImage& B)
{
    double Result = 0;
    for (size_t I = 0; I < A.Pixels.size(); ++I)
        Result += std::abs(A.Pixels[I].R-B.Pixels[I].R) + std::abs(A.Pixels[I].G-B.Pixels[I].G) +
                  std::abs(A.Pixels[I].B-B.Pixels[I].B);
    return Result;
}

int main(int ArgumentCount, char** Arguments)
{
    constexpr double Pi = 3.14159265358979323846;
    CullCamera Camera; Camera.Position = {0,0,2}; Camera.Forward = {0,0,1};
    Camera.Right = {1,0,0}; Camera.Up = {0,1,0}; Camera.FovYRadians = 60.0*Pi/180.0; Camera.Aspect = 1.0;
    ReferenceRenderSettings Settings; Settings.Width = 64; Settings.Height = 64; Settings.SunIrradiance = {1,1,1};
    Settings.IndirectSamples = 0; Settings.EnvironmentSamples = 0; Settings.AccumulationFrames = 1; Settings.Denoise = false;
    ReferenceRenderer Renderer; Renderer.SetScene({});
    TransmittanceLut Lut; if (!Lut.Bake(Settings.Atmosphere,128,32,48)) return 2;
    const double Tan = std::tan(Camera.FovYRadians*0.5), Delta = 2.0*Tan/Settings.Height;
    double WorstFlux = 0; int Samples = 0;
    auto CheckFlux = [&](double U, double V) {
        const double TY = std::tan(Camera.FovYRadians*0.5), TX = TY*Camera.Aspect;
        const double DX = 2.0*TX/Settings.Width, DY = 2.0*TY/Settings.Height;
        Settings.SunDirection = Normalize({U,V,1.0});
        Settings.SunDiscEnabled = false; const auto Without = Renderer.Render(Camera,Settings);
        Settings.SunDiscEnabled = true; const auto With = Renderer.Render(Camera,Settings);
        double Flux = 0;
        for (uint32_t Y = 0; Y < Settings.Height; ++Y) for (uint32_t X = 0; X < Settings.Width; ++X)
        {
            const size_t I = static_cast<size_t>(Y)*Settings.Width+X;
            const double P = (2.0*(X+0.5)/Settings.Width-1.0)*TX;
            const double Q = (1.0-2.0*(Y+0.5)/Settings.Height)*TY;
            const double Omega = DX*DY/std::pow(1.0+P*P+Q*Q,1.5);
            Flux += (With.Pixels[I].R-Without.Pixels[I].R)*Omega;
        }
        const auto T = Lut.Sample(Settings.Atmosphere, Settings.Atmosphere.GroundRadiusKm+0.002,Settings.SunDirection.Z);
        const double Error = std::abs(Flux/T.R-1.0);
        WorstFlux = std::max(WorstFlux, Error); ++Samples;
        std::printf("sun %ux%u FOV=%.5g U=%.6g integrated=%.9g expected=%.9g relative error=%.6g\n",
                    Settings.Width, Settings.Height, Camera.FovYRadians, U, Flux, T.R, Error);
    };
    for (double U : {0.0, 0.17*Delta, 0.49*Delta, 0.26, -0.32}) CheckFlux(U,0.08);
    Camera.Aspect = 16.0/9.0; Settings.Width = 96; Settings.Height = 54; CheckFlux(0.35,0.08);
    Camera.Aspect = 1.0; Settings.Width = 64; Settings.Height = 64;
    Camera.FovYRadians = 1.5*Pi/180.0; CheckFlux(0.001,0.001);
    Camera.FovYRadians = 60.0*Pi/180.0;
    Settings.SunDirection = Normalize({0.10,0.08,1.0}); Settings.SunDiscEnabled = false;
    Settings.LensFlaresEnabled = false; const auto WithoutFlares = Renderer.Render(Camera,Settings);
    Settings.LensFlaresEnabled = true;
    auto& P = Settings.LensFlares;
    P.GhostStrength = P.StarburstStrength = P.GlowStrength = P.HaloStrength = P.VeilingStrength = 0.0;
    P.AnamorphicStrength = 0.0; P.StreakStrength = 0.35; const auto Warm = Renderer.Render(Camera,Settings);
    P.AnamorphicStrength = 1.0; P.StreakStrength = 0.0; const auto Blue = Renderer.Render(Camera,Settings);
    P.StreakStrength = 0.35; const auto Both = Renderer.Render(Camera,Settings);
    double AdditiveError = 0;
    for (size_t I = 0; I < WithoutFlares.Pixels.size(); ++I)
    {
        AdditiveError = std::max({AdditiveError, std::abs(Both.Pixels[I].R-Warm.Pixels[I].R-Blue.Pixels[I].R+WithoutFlares.Pixels[I].R),
            std::abs(Both.Pixels[I].G-Warm.Pixels[I].G-Blue.Pixels[I].G+WithoutFlares.Pixels[I].G),
            std::abs(Both.Pixels[I].B-Warm.Pixels[I].B-Blue.Pixels[I].B+WithoutFlares.Pixels[I].B)});
    }
    const double WarmSignal = SumSignal(Warm,WithoutFlares), BlueSignal = SumSignal(Blue,WithoutFlares);
    ReferenceScene BlockedScene; ReferenceMesh Blocker;
    Blocker.Geometry.Vertices = {{-10,-10,3},{10,-10,3},{10,10,3},{-10,10,3}};
    Blocker.Geometry.Indices = {0,1,2,0,2,3}; BlockedScene.Meshes.push_back(Blocker);
    Renderer.SetScene(BlockedScene);
    Settings.LensFlaresEnabled = false; const auto BlockedWithout = Renderer.Render(Camera,Settings);
    Settings.LensFlaresEnabled = true; const auto BlockedWith = Renderer.Render(Camera,Settings);
    const double BlockedError = MaxDifference(BlockedWith,BlockedWithout);
    Renderer.SetScene({}); Camera.Forward = {0,1,0}; Camera.Right = {1,0,0}; Camera.Up = {0,0,1};
    Settings.SunDirection = Normalize({0,1,-0.10}); Settings.SunDiscEnabled = false;
    Settings.LensFlaresEnabled = false; const auto BelowPlanetEdgeWithout = Renderer.Render(Camera,Settings);
    Settings.SunDiscEnabled = true; Settings.LensFlaresEnabled = true;
    const auto BelowPlanetEdge = Renderer.Render(Camera,Settings);
    const double PlanetBlockError = MaxDifference(BelowPlanetEdge,BelowPlanetEdgeWithout);
    const bool Success = Samples == 7 && WorstFlux < 0.005 && WarmSignal > 0.01 && BlueSignal > 0.01 &&
                         AdditiveError < 1e-10 && BlockedError < 1e-10 && PlanetBlockError < 1e-10;
    std::printf("%s renderer finite-pixel flux worst relative error %.9g, %d camera positions\n",
                Success ? "PASS" : "FAIL", WorstFlux, Samples);
    std::printf("warm signal %.9g, blue signal %.9g, additive error %.9g, mesh-blocked difference %.9g, planet-blocked difference %.9g\n",
                WarmSignal, BlueSignal, AdditiveError, BlockedError, PlanetBlockError);
    if (Success && ArgumentCount == 2)
    {
        Renderer.SetScene({}); Camera.Forward = {0,1,0}; Camera.Right = {1,0,0}; Camera.Up = {0,0,1};
        Camera.FovYRadians = 70.0*Pi/180.0; Camera.Aspect = 16.0/9.0;
        ReferenceRenderSettings Capture; Capture.Width = 640; Capture.Height = 360; Capture.AccumulationFrames = 1;
        Capture.IndirectSamples = 0; Capture.EnvironmentSamples = 0; Capture.Denoise = false;
        Capture.SunDirection = Normalize({0.22,1.0,0.24}); Capture.LensFlaresEnabled = true;
        const auto Image = Renderer.Render(Camera,Capture);
        std::vector<uint8_t> Encoded; EncodeDisplayRgb8(Image,0.20,Encoded);
        std::ofstream Output(Arguments[1],std::ios::binary);
        Output << "P6\n" << Image.Width << ' ' << Image.Height << "\n255\n";
        Output.write(reinterpret_cast<const char*>(Encoded.data()),static_cast<std::streamsize>(Encoded.size()));
        if (!Output) return 3;
        std::printf("Wrote illustrative reference capture %s (not a lens calibration)\n",Arguments[1]);
    }
    return Success ? 0 : 1;
}
