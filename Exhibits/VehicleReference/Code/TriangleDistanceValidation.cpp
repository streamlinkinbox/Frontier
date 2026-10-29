/*==========================================================================================================================================
                                                     TRIANGLEDISTANCEVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Independent long-double proximity/ray oracles, conservative grid bounds and RGB secondary-light transport.

#include "Engine/Rendering/ReferenceRenderer.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
using namespace Frontier;
using ExtendedPoint = std::array<long double, 3>;
ExtendedPoint Convert(SpatialVector A)
{
    return {A.X, A.Y, A.Z};
}
ExtendedPoint Subtract(ExtendedPoint A, ExtendedPoint B)
{
    return {A[0] - B[0], A[1] - B[1], A[2] - B[2]};
}
ExtendedPoint Cross(ExtendedPoint A, ExtendedPoint B)
{
    return {A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0]};
}
long double Dot(ExtendedPoint A, ExtendedPoint B)
{
    return A[0] * B[0] + A[1] * B[1] + A[2] * B[2];
}
long double Distance(const std::vector<std::array<SpatialVector, 3>> &Facets, SpatialVector Position)
{
    long double Best = INFINITY;
    for (auto Facet : Facets)
    {
        auto A = Convert(Facet[0]), B = Convert(Facet[1]), C = Convert(Facet[2]), P = Convert(Position);
        auto E = Subtract(B, A), F = Subtract(C, A), V = Subtract(P, A);
        auto EE = Dot(E, E), EF = Dot(E, F), FF = Dot(F, F), VE = Dot(V, E), VF = Dot(V, F),
             Determinant = EE * FF - EF * EF;
        if (Determinant > 0)
        {
            auto U = (VE * FF - VF * EF) / Determinant, W = (VF * EE - VE * EF) / Determinant;
            if (U >= 0 && W >= 0 && U + W <= 1)
            {
                ExtendedPoint Delta{V[0] - U * E[0] - W * F[0], V[1] - U * E[1] - W * F[1],
                                    V[2] - U * E[2] - W * F[2]};
                Best = std::min(Best, Dot(Delta, Delta));
            }
        }
        for (uint32_t Edge = 0; Edge < 3; ++Edge)
        {
            auto Start = Convert(Facet[Edge]), End = Convert(Facet[(Edge + 1) % 3]);
            auto Segment = Subtract(End, Start), Offset = Subtract(P, Start);
            auto Length = Dot(Segment, Segment);
            auto Fraction = Length > 0 ? std::clamp(Dot(Offset, Segment) / Length, 0.L, 1.L) : 0.L;
            ExtendedPoint Delta{Offset[0] - Fraction * Segment[0], Offset[1] - Fraction * Segment[1],
                                Offset[2] - Fraction * Segment[2]};
            Best = std::min(Best, Dot(Delta, Delta));
        }
    }
    return std::sqrt(Best);
}
long double Strike(const std::vector<std::array<SpatialVector, 3>> &Facets, SpatialVector Origin,
                   SpatialVector Direction, double Minimum, double Maximum)
{
    long double Best = Maximum;
    for (auto Facet : Facets)
    {
        auto A = Convert(Facet[0]), B = Convert(Facet[1]), C = Convert(Facet[2]);
        auto N = Cross(Subtract(B, A), Subtract(C, A));
        auto Denominator = Dot(N, Convert(Direction));
        if (std::abs(Denominator) < 1e-14L)
            continue;
        auto T = Dot(N, Subtract(A, Convert(Origin))) / Denominator;
        if (T <= Minimum || T >= Best)
            continue;
        ExtendedPoint P{Origin.X + T * Direction.X, Origin.Y + T * Direction.Y, Origin.Z + T * Direction.Z};
        if (Dot(Cross(Subtract(B, A), Subtract(P, A)), N) >= 0 &&
            Dot(Cross(Subtract(C, B), Subtract(P, B)), N) >= 0 &&
            Dot(Cross(Subtract(A, C), Subtract(P, C)), N) >= 0)
            Best = T;
    }
    return Best;
}
// Actual geometry is decoded separately from the capture importer and checked against its saved bake.
void ValidateVehicle()
{
    std::vector<std::array<SpatialVector, 3>> Facets;
    auto ReadArchive = [&](const std::string &Part, SpatialVector Offset, double Side) {
        std::ifstream Stream(
            "Experimental/SlateReference/A/Exhibits/VehicleProvingGrounds/Archive/ControlVehicle" + Part +
                ".facet",
            std::ios::binary);
        Stream.exceptions(std::ios::failbit | std::ios::badbit);
        char Magic[4];
        uint32_t Vertices = 0, Triangles = 0;
        Stream.read(Magic, 4);
        Stream.read(reinterpret_cast<char *>(&Vertices), 4);
        Stream.read(reinterpret_cast<char *>(&Triangles), 4);
        if (std::memcmp(Magic, "FCT1", 4) || !Vertices || Vertices > 1000000 || Triangles > 1000000)
            throw std::runtime_error("Invalid vehicle validation archive");
        Stream.seekg(48);
        std::vector<SpatialVector> Positions;
        for (uint32_t Index = 0; Index < Vertices; ++Index)
        {
            std::array<float, 3> P{};
            Stream.read(reinterpret_cast<char *>(P.data()), 12);
            Positions.push_back({P[0] + Offset.X, Side * P[1] + Offset.Y, P[2] + Offset.Z});
        }
        for (uint32_t Index = 0; Index < Triangles; ++Index)
        {
            std::array<uint32_t, 3> Corners{};
            Stream.read(reinterpret_cast<char *>(Corners.data()), 12);
            for (auto Corner : Corners)
                if (Corner >= Vertices)
                    throw std::runtime_error("Invalid vehicle validation index");
            Facets.push_back({Positions[Corners[0]], Positions[Corners[1]], Positions[Corners[2]]});
        }
    };
    ReadArchive("Body", {0, 0, .41715}, 1);
    for (auto Mount : {SpatialVector{1.7274, 1.0475, .50855}, SpatialVector{1.7274, -1.0475, .50855},
                       SpatialVector{-1.6686, 1.0475, .50855}, SpatialVector{-1.6686, -1.0475, .50855}})
        for (auto Part : {"Tyre", "Rim"})
            ReadArchive(Part, Mount, Mount.Y < 0 ? -1 : 1);
    if (Facets.size() != 25068)
        throw std::runtime_error("Vehicle validation facet count mismatch");
    std::array<double, 3> Low{}, High{};
    Low.fill(INFINITY);
    High.fill(-INFINITY);
    for (auto Facet : Facets)
        for (auto P : Facet)
        {
            Low[0] = std::min(Low[0], P.X);
            Low[1] = std::min(Low[1], P.Y);
            Low[2] = std::min(Low[2], P.Z);
            High[0] = std::max(High[0], P.X);
            High[1] = std::max(High[1], P.Y);
            High[2] = std::max(High[2], P.Z);
        }
    for (uint32_t Axis = 0; Axis < 3; ++Axis)
    {
        Low[Axis] -= .15;
        High[Axis] += .15;
    }
    std::vector<double> Grid(64 * 32 * 24);
    std::ifstream Saved("_ClaudeScratch/vehicle-reference/Distance0.bin", std::ios::binary | std::ios::ate);
    Saved.exceptions(std::ios::failbit | std::ios::badbit);
    if (Saved.tellg() != std::streamoff(Grid.size() * 8))
        throw std::runtime_error("Vehicle grid snapshot extent mismatch");
    Saved.seekg(0);
    Saved.read(reinterpret_cast<char *>(Grid.data()), std::streamsize(Grid.size() * 8));
    for (auto Number : Grid)
        if (!std::isfinite(Number) || Number < 0)
            throw std::runtime_error("Nonfinite vehicle grid distance");
    double MaximumError = 0;
    for (uint32_t Probe = 0; Probe < 128; ++Probe)
    {
        uint32_t Index = (Probe * 7919) % uint32_t(Grid.size()), X = Index % 64, Y = (Index / 64) % 32,
                 Z = Index / (64 * 32);
        SpatialVector P{Low[0] + X * ((High[0] - Low[0]) / 63), Low[1] + Y * ((High[1] - Low[1]) / 31),
                        Low[2] + Z * ((High[2] - Low[2]) / 23)};
        MaximumError = std::max(MaximumError, std::abs(Grid[Index] - double(Distance(Facets, P))));
    }
    if (MaximumError > 1e-9)
        throw std::runtime_error("Actual vehicle grid differs from exhaustive point/triangle oracle");
    TriangleDistanceVolume Volume;
    Volume.Bake(Facets, {64, 32, 24});
    for (size_t Index = 0; Index < Grid.size(); ++Index)
        if (std::abs(Grid[Index] - Volume.Distances()[Index]) > 1e-10)
            throw std::runtime_error("Independent vehicle import does not reproduce saved distance bake");
    std::mt19937 Generator(9013);
    std::uniform_real_distribution<double> Uniform(-1, 1);
    double MaximumTravelError = 0;
    uint32_t Hits = 0;
    for (uint32_t Probe = 0; Probe < 256; ++Probe)
    {
        SpatialVector Origin{5 * Uniform(Generator), 3 * Uniform(Generator), 1.5 + 1.5 * Uniform(Generator)};
        SpatialVector Direction{-Origin.X + Uniform(Generator), -Origin.Y + Uniform(Generator),
                                .6 - Origin.Z + .4 * Uniform(Generator)};
        double Length = std::hypot(Direction.X, Direction.Y, Direction.Z);
        Direction = {Direction.X / Length, Direction.Y / Length, Direction.Z / Length};
        auto Expected = Strike(Facets, Origin, Direction, .002, 20);
        auto Actual = Volume.Trace(Origin, Direction, .002, 20);
        if (Actual.Hit != (Expected < 20))
            throw std::runtime_error("Actual vehicle refined ray hit/miss mismatch");
        if (Actual.Hit)
        {
            ++Hits;
            MaximumTravelError = std::max(MaximumTravelError, std::abs(Actual.Travel - double(Expected)));
        }
    }
    if (MaximumTravelError > 1e-8)
        throw std::runtime_error("Actual vehicle refined ray travel mismatch");
    std::cout << "PASS actual 25068-facet ControlVehicle: 49152 finite grid samples reproduced; "
              << "128 independent exhaustive grid probes max error=" << MaximumError << "m; "
              << "256 exhaustive ray probes hits=" << Hits << ", max travel error=" << MaximumTravelError
              << "m\n";
}

int main(int Count, char **Arguments)
{
    try
    {
        if (Count == 2 && std::string(Arguments[1]) == "--vehicle")
        {
            ValidateVehicle();
            return 0;
        }
        if (Count != 1)
            throw std::runtime_error("Usage: TriangleDistanceValidation [--vehicle]");
        std::mt19937 Generator(37027);
        std::uniform_real_distribution<double> Uniform(-4, 4);
        auto Point = [&]() {
            return SpatialVector{Uniform(Generator), Uniform(Generator), Uniform(Generator)};
        };
        std::vector<std::array<SpatialVector, 3>> Facets;
        for (uint32_t Index = 0; Index < 128; ++Index)
            Facets.push_back({Point(), Point(), Point()});
        TriangleDistanceVolume Volume;
        Volume.Bake(Facets, {24, 20, 16});
        double MaximumError = 0, MaximumExcess = 0;
        for (uint32_t Index = 0; Index < 2048; ++Index)
        {
            auto P = Point();
            auto Expected = Distance(Facets, P);
            double Error = std::abs(double(Expected) - Volume.ExactDistance(P).Distance);
            MaximumError = std::max(MaximumError, Error);
            MaximumExcess = std::max(MaximumExcess, Volume.LowerDistance(P) - double(Expected));
            if (Error > 1e-10 || Volume.LowerDistance(P) > Expected + 1e-10)
                throw std::runtime_error("Triangle proximity or conservative grid bound mismatch");
        }
        double MaximumTravelError = 0;
        uint32_t Hits = 0;
        for (uint32_t Index = 0; Index < 4096; ++Index)
        {
            auto Origin = Point(), Direction = Point();
            const double Minimum = (Index % 3) * .1, Maximum = .5 + Index % 13;
            auto Expected = Strike(Facets, Origin, Direction, Minimum, Maximum);
            auto Actual = Volume.Trace(Origin, Direction, Minimum, Maximum);
            if (Actual.Hit != (Expected < Maximum))
                throw std::runtime_error("Grid-refined hit/miss differs from exhaustive oracle");
            if (Actual.Hit)
            {
                ++Hits;
                MaximumTravelError = std::max(MaximumTravelError, std::abs(double(Expected) - Actual.Travel));
                if (std::abs(Expected - Actual.Travel) > 1e-10 || Actual.Facet >= Facets.size())
                    throw std::runtime_error("Grid-refined travel/identity mismatch");
            }
        }
        std::cout << "PASS: 2048 independent proximity/conservative-bound checks; max distance error="
                  << MaximumError << "m; max lower-bound excess=" << MaximumExcess << "m\n";
        std::cout << "PASS: 4096 exhaustive long-double bounded ray checks; hits=" << Hits
                  << "; max travel error=" << MaximumTravelError << "m; no exclusions\n";
        auto Count = Volume.SampleCount();
        uint32_t Rejected = 0;
        for (auto Extent : {std::array<uint32_t, 3>{1, 4, 4}, std::array<uint32_t, 3>{256, 256, 256}})
            try
            {
                Volume.Bake(Facets, Extent);
            }
            catch (const std::runtime_error &)
            {
                ++Rejected;
            }
        auto Invalid = Facets;
        Invalid[0][0].X = NAN;
        try
        {
            Volume.Bake(Invalid, {4, 4, 4});
        }
        catch (const std::runtime_error &)
        {
            ++Rejected;
        }
        try
        {
            Volume.Bake({}, {4, 4, 4});
        }
        catch (const std::runtime_error &)
        {
            ++Rejected;
        }
        if (Rejected != 4 || Volume.SampleCount() != Count)
            throw std::runtime_error("Failed rebake damaged prior field");
        if (Volume.Trace({0, 0, 0}, {0, 0, 0}, 0, 1).Hit || Volume.Trace({NAN, 0, 0}, {1, 0, 0}, 0, 1).Hit ||
            Volume.Trace({0, 0, 0}, {1, 0, 0}, 2, 1).Hit)
            throw std::runtime_error("Invalid volume ray accepted");
        std::vector<std::array<SpatialVector, 3>> Plane{
            {SpatialVector{-10, -10, 2}, {10, -10, 2}, {0, 10, 2}}};
        auto Canopy = std::make_shared<TriangleDistanceVolume>();
        Canopy->Bake(Plane, {16, 16, 4});
        if (!Canopy->Trace({0, 0, 0}, {0, 0, 1}, 0, 3).Hit || Canopy->Trace({0, 0, 0}, {0, 0, 1}, 0, 2).Hit ||
            Canopy->Trace({0, 0, 0}, {0, 0, 1}, 2, 3).Hit)
            throw std::runtime_error("Exact endpoint ray guard mismatch");
        ReferenceScene Scene;
        ReferenceMesh Ground;
        Ground.Geometry.Vertices = {{-3, -3, 0}, {3, -3, 0}, {3, 3, 0}, {-3, 3, 0}};
        Ground.Geometry.Indices = {0, 1, 2, 0, 2, 3};
        Scene.Meshes.push_back(Ground);
        Scene.TriangleDistance = Canopy;
        Scene.TriangleRadiance = {{0, 0, 0}};
        ReferenceRenderer Renderer;
        Renderer.SetScene(Scene);
        CullCamera Camera;
        Camera.Position = {0, -5, 2};
        Camera.Forward = {0, 5 / std::sqrt(29.), -2 / std::sqrt(29.)};
        Camera.Right = {1, 0, 0};
        Camera.Up = {0, 2 / std::sqrt(29.), 5 / std::sqrt(29.)};
        Camera.FovYRadians = .6;
        Camera.Aspect = 1.6;
        Camera.NearPlane = .1;
        Camera.FarPlane = 100;
        ReferenceRenderSettings Settings;
        Settings.Width = 32;
        Settings.Height = 20;
        Settings.AtmosphereEnabled = false;
        Settings.EnvironmentSamples = 16;
        Settings.IndirectSamples = 0;
        Settings.AccumulationFrames = 1;
        Settings.Denoise = false;
        auto Dark = Renderer.Render(Camera, Settings);
        Scene.TriangleRadiance = {{4, 0, 0}};
        Renderer.SetScene(Scene);
        auto Red = Renderer.Render(Camera, Settings);
        double AddedRed = 0, AddedOther = 0;
        for (size_t Index = 0; Index < Red.Pixels.size(); ++Index)
        {
            AddedRed += Red.Pixels[Index].R - Dark.Pixels[Index].R;
            AddedOther += std::abs(Red.Pixels[Index].G - Dark.Pixels[Index].G) +
                          std::abs(Red.Pixels[Index].B - Dark.Pixels[Index].B);
        }
        if (AddedRed < 1 || AddedOther > 1e-10)
            throw std::runtime_error("RGB triangle radiance not transported through renderer");
        Scene.TriangleRadiance.clear();
        bool Guard = false;
        try
        {
            Renderer.SetScene(Scene);
        }
        catch (const std::runtime_error &)
        {
            Guard = true;
        }
        if (!Guard)
            throw std::runtime_error("Mismatched triangle radiance accepted");
        std::cout << "PASS: invalid/rebake/axis/endpoint guards; RGB emitter transport with sky disabled: "
                     "added red="
                  << AddedRed << ", other channels=" << AddedOther << "; radiance-count guard\n";
    }
    catch (const std::exception &Error)
    {
        std::cerr << Error.what() << '\n';
        return 1;
    }
}
