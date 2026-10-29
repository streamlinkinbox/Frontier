/*==========================================================================================================================================
                                                     TRIANGLEVISIBILITYVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Exhaustive long-double interval comparison and an off-camera shadow-caster renderer regression.

#include "Engine/Rendering/ReferenceRenderer.h"
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
using namespace Frontier;

//------------------------------------------------------------------------------------------------------------------------
//                                                 INDEPENDENT PLANE INTERSECTION
//------------------------------------------------------------------------------------------------------------------------

using Precise = std::array<long double, 3>;
Precise Subtract(Precise A, Precise B)
{
    return {A[0] - B[0], A[1] - B[1], A[2] - B[2]};
}
Precise Cross(Precise A, Precise B)
{
    return {A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0]};
}
long double Dot(Precise A, Precise B)
{
    return A[0] * B[0] + A[1] * B[1] + A[2] * B[2];
}
Precise Convert(SpatialVector A)
{
    return {A.X, A.Y, A.Z};
}
bool Exhaustive(const std::vector<std::array<SpatialVector, 3>> &Facets, SpatialVector Origin,
                SpatialVector Direction, double Minimum, double Maximum)
{
    for (auto Facet : Facets)
    {
        auto A = Convert(Facet[0]), B = Convert(Facet[1]), C = Convert(Facet[2]);
        auto N           = Cross(Subtract(B, A), Subtract(C, A));
        auto Denominator = Dot(N, Convert(Direction));
        if (std::abs(Denominator) < 1e-14L)
            continue;
        auto Travel = Dot(N, Subtract(A, Convert(Origin))) / Denominator;
        if (!(Travel > Minimum && Travel < Maximum))
            continue;
        Precise Hit{Origin.X + Travel * Direction.X, Origin.Y + Travel * Direction.Y,
                    Origin.Z + Travel * Direction.Z};
        if (Dot(Cross(Subtract(B, A), Subtract(Hit, A)), N) >= 0 &&
            Dot(Cross(Subtract(C, B), Subtract(Hit, B)), N) >= 0 &&
            Dot(Cross(Subtract(A, C), Subtract(Hit, C)), N) >= 0)
            return true;
    }
    return false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   INTERVAL AND RENDER CHECKS
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    try
    {
        std::mt19937 Generator(80317);
        std::uniform_real_distribution<double> Distribution(-5, 5);
        auto Point = [&]() {
            return SpatialVector{Distribution(Generator), Distribution(Generator), Distribution(Generator)};
        };
        std::vector<std::array<SpatialVector, 3>> Facets;
        for (uint32_t Index = 0; Index < 512; ++Index)
            Facets.push_back({Point(), Point(), Point()});
        TriangleVisibilitySpace Space;
        Space.Build(Facets);
        for (uint32_t Index = 0; Index < 8192; ++Index)
        {
            auto Origin = Point(), Direction = Point();
            double Minimum = Index % 3 * .1, Maximum = .5 + (Index % 31);
            if (Space.Occluded(Origin, Direction, Minimum, Maximum) !=
                Exhaustive(Facets, Origin, Direction, Minimum, Maximum))
                throw std::runtime_error("Visibility differs from exhaustive long-double plane oracle");
        }
        const std::vector<std::array<SpatialVector, 3>> Plane{
            {SpatialVector{-1, -1, 2}, {1, -1, 2}, {0, 1, 2}}};
        Space.Build(Plane);
        if (!Space.Occluded({0, 0, 0}, {0, 0, 1}, 0, 3) || Space.Occluded({0, 0, 0}, {0, 0, 1}, 0, 2) ||
            Space.Occluded({0, 0, 0}, {0, 0, 1}, 2, 3) || Space.Occluded({3, 0, 0}, {0, 0, 1}, 0, 3) ||
            Space.Occluded({0, 0, 0}, {0, 0, 0}, 0, 3) || Space.Occluded({0, 0, 0}, {0, 0, 1}, 3, 2) ||
            Space.Occluded({0, 0, 0}, {0, 0, 1}, -1, 3) ||
            Space.Occluded({0, 0, std::numeric_limits<double>::quiet_NaN()}, {0, 0, 1}, 0, 3))
            throw std::runtime_error("Axis, finite, empty or endpoint interval guard mismatch");
        Space.Build({});
        if (Space.Occluded({0, 0, 0}, {0, 0, 1}, 0, 3))
            throw std::runtime_error("Stale empty acceleration bounds");
        bool Rejected   = false;
        auto Invalid    = Plane;
        Invalid[0][0].X = std::numeric_limits<double>::infinity();
        try
        {
            Space.Build(Invalid);
        }
        catch (const std::runtime_error &)
        {
            Rejected = true;
        }
        if (!Rejected)
            throw std::runtime_error("Nonfinite visibility build accepted");
        std::cout
            << "PASS: 8192 exhaustive long-double interval comparisons; endpoint/axis/invalid/empty guards\n";
        ReferenceScene Scene;
        ReferenceMesh Ground;
        Ground.Geometry.Vertices = {{-3, -3, 0}, {3, -3, 0}, {3, 3, 0}, {-3, 3, 0}};
        Ground.Geometry.Indices  = {0, 1, 2, 0, 2, 3};
        Scene.Meshes.push_back(Ground);
        Scene.Lights.push_back(
            {ReferenceLight::Kind::Directional, {-std::sqrt(.5), 0, std::sqrt(.5)}, {4, 4, 4}});
        CullCamera Camera;
        Camera.Position    = {0, -5, 2};
        Camera.Forward     = {0, 5 / std::sqrt(29.), -2 / std::sqrt(29.)};
        Camera.Right       = {1, 0, 0};
        Camera.Up          = {0, 2 / std::sqrt(29.), 5 / std::sqrt(29.)};
        Camera.FovYRadians = .6;
        Camera.Aspect      = 1.6;
        Camera.NearPlane   = .1;
        Camera.FarPlane    = 100;
        ReferenceRenderSettings Settings;
        Settings.Width              = 64;
        Settings.Height             = 40;
        Settings.AccumulationFrames = 1;
        Settings.RisCandidates      = 1;
        Settings.IndirectSamples    = 0;
        Settings.EnvironmentSamples = 0;
        Settings.AtmosphereEnabled  = false;
        Settings.Denoise            = false;
        Settings.SpatialReuse       = false;
        ReferenceRenderer Renderer;
        Renderer.SetScene(Scene);
        auto Clear = Renderer.Render(Camera, Settings);
        ReferenceMesh Blocker;
        Blocker.Geometry.Vertices = {{-8, -2, 7}, {-8, 2, 7}, {-8, 2, 9}, {-8, -2, 9}};
        Blocker.Geometry.Indices  = {0, 1, 2, 0, 2, 3};
        Scene.Meshes.push_back(Blocker);
        Renderer.SetScene(Scene);
        auto Shadow = Renderer.Render(Camera, Settings);
        if (Clear.Stats.TrianglesVisible != Shadow.Stats.TrianglesVisible ||
            !(Shadow.Stats.MeanLuminance < Clear.Stats.MeanLuminance * .95))
            throw std::runtime_error("Off-camera shadow caster was lost to primary frustum culling");
        std::cout << "PASS: off-camera caster; primary visible triangles=" << Clear.Stats.TrianglesVisible
                  << " unchanged; mean luminance " << Clear.Stats.MeanLuminance << " -> "
                  << Shadow.Stats.MeanLuminance << '\n';
    }
    catch (const std::exception &Error)
    {
        std::cerr << Error.what() << '\n';
        return 1;
    }
}
