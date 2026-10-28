/*==========================================================================================================================================
                                                        LIGHTCOMPUTEFORMAT.H
==========================================================================================================================================*/
// 🧩 Explicit float32 scene buffers shared by native reference export and
// Vulkan compute validation.
#pragma once
#include "Engine/Rendering/ReferenceRenderer.h"
#include "Engine/Rendering/PathReplayFootprint.h"
#include <array>
#include <fstream>
namespace Frontier
{
struct alignas(16) LightComputeShape
{
    std::array<float, 4> PositionReach{}, RadianceCategory{}, DirectionInner{}, UpOuter{};
    std::array<uint32_t, 4> Profile{};
    std::array<float, 4> PeakActive{};
};
using LightComputeReceiver = PathReplayReceiver;
using LightComputeFacet = PathReplayFacet;
struct LightComputeArchive
{
    uint32_t Width = 0, Height = 0;
    std::vector<LightComputeShape> Lights;
    std::vector<LightComputeReceiver> Receivers;
    TriangleVisibilitySpace::FloatAtlas Visibility;
    std::vector<float> Angles;
    std::vector<LightComputeFacet> Surfaces;
    std::array<std::array<float, 4>, 12> Camera{};
    template <class T> static void Write(std::ostream& Stream, const std::vector<T>& Sequence)
    {
        uint32_t Count = uint32_t(Sequence.size());
        Stream.write(reinterpret_cast<const char*>(&Count), 4);
        Stream.write(reinterpret_cast<const char*>(Sequence.data()),
                     std::streamsize(Sequence.size() * sizeof(T)));
    }
    template <class T> static void Read(std::istream& Stream, std::vector<T>& Sequence, uint32_t Limit)
    {
        uint32_t Count = 0;
        Stream.read(reinterpret_cast<char*>(&Count), 4);
        if (Count > Limit)
            throw std::runtime_error("Compute archive extent exceeds budget");
        Sequence.resize(Count);
        Stream.read(reinterpret_cast<char*>(Sequence.data()), std::streamsize(Count * sizeof(T)));
    }
    void Save(const std::string& Path) const
    {
        std::ofstream Stream(Path, std::ios::binary);
        Stream.exceptions(std::ios::failbit | std::ios::badbit);
        const uint32_t Magic = 0x33434c46;
        Stream.write(reinterpret_cast<const char*>(&Magic), 4);
        Stream.write(reinterpret_cast<const char*>(&Width), 4);
        Stream.write(reinterpret_cast<const char*>(&Height), 4);
        Write(Stream, Lights);
        Write(Stream, Receivers);
        Write(Stream, Visibility.Enclosures);
        Write(Stream, Visibility.Corners);
        Write(Stream, Angles);
        Write(Stream, Surfaces);
        Stream.write(reinterpret_cast<const char*>(Camera.data()), sizeof(Camera));
    }
    static LightComputeArchive Load(const std::string& Path)
    {
        std::ifstream Stream(Path, std::ios::binary);
        Stream.exceptions(std::ios::failbit | std::ios::badbit);
        LightComputeArchive Archive;
        uint32_t Magic = 0;
        Stream.read(reinterpret_cast<char*>(&Magic), 4);
        if (Magic != 0x33434c46)
            throw std::runtime_error("Compute archive signature rejected");
        Stream.read(reinterpret_cast<char*>(&Archive.Width), 4);
        Stream.read(reinterpret_cast<char*>(&Archive.Height), 4);
        Read(Stream, Archive.Lights, 32768);
        Read(Stream, Archive.Receivers, 4194304);
        Read(Stream, Archive.Visibility.Enclosures, 2097152);
        Read(Stream, Archive.Visibility.Corners, 1048576);
        Read(Stream, Archive.Angles, 2097152);
        Read(Stream, Archive.Surfaces, 1048576);
        Stream.read(reinterpret_cast<char*>(Archive.Camera.data()), sizeof(Archive.Camera));
        if (uint64_t(Archive.Width) * Archive.Height != Archive.Receivers.size())
            throw std::runtime_error("Compute receiver extent mismatch");
        return Archive;
    }
};
inline LightComputeArchive PackLightCompute(const ReferenceScene& Scene, const ReferenceImage& Image,
                                            const CullCamera* Camera = nullptr)
{
    LightComputeArchive Archive;
    Archive.Width = Image.Width;
    Archive.Height = Image.Height;
    auto Vector = [](SpatialVector P, double W)
    { return std::array<float, 4>{float(P.X), float(P.Y), float(P.Z), float(W)}; };
    auto Color = [](SpectrumRgb C, double W)
    { return std::array<float, 4>{float(C.R), float(C.G), float(C.B), float(W)}; };
    for (const auto& Light : Scene.Lights)
    {
        LightComputeShape Packed;
        Packed.PositionReach = Vector(Light.Vector, Light.Reach);
        Packed.RadianceCategory = Color(Light.Radiance, Light.Kind == ReferenceLight::Kind::Directional ? 0
                                                        : Light.Kind == ReferenceLight::Kind::Point     ? 1
                                                                                                        : 2);
        Packed.DirectionInner = Vector(Light.Direction, Light.InnerCosine);
        Packed.UpOuter = Vector(Light.Up, Light.OuterCosine);
        Packed.PeakActive[1] = Light.Active ? 1.f : 0.f;
        if (Light.Photometry)
        {
            const auto& P = *Light.Photometry;
            Packed.Profile = {uint32_t(Archive.Angles.size()), uint32_t(P.VerticalAngles().size()), 0,
                              uint32_t(P.HorizontalAngles().size())};
            for (auto A : P.VerticalAngles())
                Archive.Angles.push_back(float(A));
            Packed.Profile[2] = uint32_t(Archive.Angles.size());
            for (auto A : P.HorizontalAngles())
                Archive.Angles.push_back(float(A));
            Packed.PeakActive[2] = float(Archive.Angles.size());
            Packed.PeakActive[0] = 1.f;
            for (auto C : P.CandelaSamples())
                Archive.Angles.push_back(float(C / P.PeakCandela()));
        }
        Archive.Lights.push_back(Packed);
    }
    std::vector<std::array<SpatialVector, 3>> Facets;
    std::vector<LightComputeFacet> Surfaces;
    auto Sub = [](SpatialVector A, SpatialVector B)
    { return SpatialVector{A.X - B.X, A.Y - B.Y, A.Z - B.Z}; };
    auto Cross = [](SpatialVector A, SpatialVector B)
    { return SpatialVector{A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X}; };
    auto Unit = [](SpatialVector P)
    {
        double Length = std::sqrt(P.X * P.X + P.Y * P.Y + P.Z * P.Z);
        return Length > 0 ? SpatialVector{P.X / Length, P.Y / Length, P.Z / Length} : SpatialVector{0, 0, 1};
    };
    for (uint32_t Index = 0; Index < Scene.Meshes.size(); ++Index)
    {
        const auto& Surface = Scene.Meshes[Index];
        std::vector<SpatialVector> Vertices, Normals(Surface.Geometry.Vertices.size());
        for (auto P : Surface.Geometry.Vertices)
            Vertices.push_back({P.X * Surface.Scale + Surface.Translation.X,
                                P.Y * Surface.Scale + Surface.Translation.Y,
                                P.Z * Surface.Scale + Surface.Translation.Z});
        const auto& Indices = Surface.Geometry.Indices;
        for (size_t I = 0; I < Indices.size(); I += 3)
        {
            auto N = Cross(Sub(Vertices[Indices[I + 1]], Vertices[Indices[I]]),
                           Sub(Vertices[Indices[I + 2]], Vertices[Indices[I]]));
            for (uint32_t J = 0; J < 3; ++J)
            {
                auto& P = Normals[Indices[I + J]];
                P = {P.X + N.X, P.Y + N.Y, P.Z + N.Z};
            }
        }
        for (auto& N : Normals)
            N = Unit(N);
        for (size_t I = 0; I < Indices.size(); I += 3)
        {
            uint32_t A = Indices[I], B = Indices[I + 1], C = Indices[I + 2];
            Facets.push_back({Vertices[A], Vertices[B], Vertices[C]});
            LightComputeFacet F;
            F.A = Vector(Vertices[A], 0);
            F.B = Vector(Vertices[B], 0);
            F.C = Vector(Vertices[C], 0);
            F.NormalA = Vector(Normals[A], 0);
            F.NormalB = Vector(Normals[B], 0);
            F.NormalC = Vector(Normals[C], 0);
            F.AlbedoRoughness = Color(Surface.Material.Albedo, Surface.Material.Roughness);
            F.OpticalToken = {float(Surface.Material.Metallic), float(Surface.Material.Ior),
                              float(Surface.Material.SpecularLevel), float(Index + 1)};
            F.Emissive = Color(Surface.Emissive, 0);
            Surfaces.push_back(F);
        }
    }
    TriangleVisibilitySpace Space;
    Space.Build(std::move(Facets));
    Archive.Visibility = Space.PackFloat32();
    for (auto Index : Archive.Visibility.OriginalFacets)
        Archive.Surfaces.push_back(Surfaces[Index]);
    if (Camera)
    {
        Archive.Camera[0] = Vector(Camera->Position, Camera->NearPlane);
        Archive.Camera[1] = Vector(Camera->Forward, Camera->FarPlane);
        Archive.Camera[2] = Vector(Camera->Right, std::tan(Camera->FovYRadians * .5) * Camera->Aspect);
        Archive.Camera[3] = Vector(Camera->Up, std::tan(Camera->FovYRadians * .5));
        for (uint32_t I = 0; I < 4; ++I)
            Archive.Camera[4 + I] = Archive.Camera[I];
        Archive.Camera[8][2] = 3;
        Archive.Camera[11] = {1, 0, 1, 0};
    }
    if (Image.SurfaceSamples.size() != Image.Pixels.size())
        throw std::runtime_error("Missing exact reference surface samples");
    for (size_t I = 0; I < Image.Pixels.size(); ++I)
    {
        const auto& S = Image.SurfaceSamples[I];
        auto C = Image.Pixels[I];
        LightComputeReceiver R;
        R.PositionHit = Vector(S.Position, S.Hit ? 1 : 0);
        R.NormalRoughness = Vector(S.Normal, S.Material.Roughness);
        R.ViewMetallic = Vector(S.View, S.Material.Metallic);
        R.AlbedoIor = Color(S.Material.Albedo, S.Material.Ior);
        R.ResidualSpecular =
            Color({C.R - S.Direct.R, C.G - S.Direct.G, C.B - S.Direct.B}, S.Material.SpecularLevel);
        R.ExpectedDirect = Color(S.Direct, 0);
        R.MotionPositionToken = Vector(S.Position, S.Hit ? (S.SurfaceToken ? S.SurfaceToken : 1) : 0);
        R.MotionNormal = Vector(S.Normal, 0);
        R.MotionPixel = {float(I % Image.Width), float(I / Image.Width), .05f, S.Hit ? 1.f : 0.f};
        Archive.Receivers.push_back(R);
    }
    return Archive;
}
inline void ProjectLightComputeMotion(LightComputeArchive& Archive, const CullCamera& PreviousCamera,
                                      double CurrentAngle, double PreviousAngle, uint32_t VehicleFirst)
{
    Archive.Camera[4] = {float(PreviousCamera.Position.X), float(PreviousCamera.Position.Y),
                         float(PreviousCamera.Position.Z), float(PreviousCamera.NearPlane)};
    Archive.Camera[5] = {float(PreviousCamera.Forward.X), float(PreviousCamera.Forward.Y),
                         float(PreviousCamera.Forward.Z), float(PreviousCamera.FarPlane)};
    Archive.Camera[6] = {float(PreviousCamera.Right.X), float(PreviousCamera.Right.Y),
                         float(PreviousCamera.Right.Z),
                         float(std::tan(PreviousCamera.FovYRadians * .5) * PreviousCamera.Aspect)};
    Archive.Camera[7] = {float(PreviousCamera.Up.X), float(PreviousCamera.Up.Y), float(PreviousCamera.Up.Z),
                         float(std::tan(PreviousCamera.FovYRadians * .5))};
    Archive.Camera[8] = {float(CurrentAngle), float(.8 * CurrentAngle), float(VehicleFirst), 0};
    Archive.Camera[9] = {float(PreviousAngle), float(.8 * PreviousAngle), float(VehicleFirst), 0};
    Archive.Camera[11] = {float(std::cos(CurrentAngle)), float(std::sin(CurrentAngle)),
                          float(std::cos(PreviousAngle)), float(std::sin(PreviousAngle))};
    auto Dot = [](SpatialVector A, SpatialVector B) { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; };
    const double TanY = std::tan(PreviousCamera.FovYRadians * .5), TanX = TanY * PreviousCamera.Aspect;
    for (auto& R : Archive.Receivers)
    {
        if (R.PositionHit[3] == 0)
        {
            R.MotionPixel[3] = 0;
            continue;
        }
        SpatialVector P{R.PositionHit[0], R.PositionHit[1], R.PositionHit[2]};
        SpatialVector N{R.NormalRoughness[0], R.NormalRoughness[1], R.NormalRoughness[2]};
        if (R.MotionPositionToken[3] > VehicleFirst)
        {
            double X = P.X - .8 * CurrentAngle, Y = P.Y;
            double LocalX = std::cos(CurrentAngle) * X + std::sin(CurrentAngle) * Y;
            double LocalY = -std::sin(CurrentAngle) * X + std::cos(CurrentAngle) * Y;
            P.X = std::cos(PreviousAngle) * LocalX - std::sin(PreviousAngle) * LocalY + .8 * PreviousAngle;
            P.Y = std::sin(PreviousAngle) * LocalX + std::cos(PreviousAngle) * LocalY;
            X = N.X;
            Y = N.Y;
            double Angle = PreviousAngle - CurrentAngle;
            N.X = std::cos(Angle) * X - std::sin(Angle) * Y;
            N.Y = std::sin(Angle) * X + std::cos(Angle) * Y;
        }
        R.MotionPositionToken[0] = float(P.X);
        R.MotionPositionToken[1] = float(P.Y);
        R.MotionPositionToken[2] = float(P.Z);
        R.MotionNormal = {float(N.X), float(N.Y), float(N.Z), 0};
        SpatialVector Relative{P.X - PreviousCamera.Position.X, P.Y - PreviousCamera.Position.Y,
                               P.Z - PreviousCamera.Position.Z};
        double Depth = Dot(Relative, PreviousCamera.Forward);
        if (Depth <= PreviousCamera.NearPlane)
        {
            R.MotionPixel[3] = 0;
            continue;
        }
        R.MotionPixel = {
            float((Dot(Relative, PreviousCamera.Right) / (Depth * TanX) * .5 + .5) * Archive.Width - .5),
            float((.5 - Dot(Relative, PreviousCamera.Up) / (Depth * TanY) * .5) * Archive.Height - .5),
            float(std::max(.03, Depth * TanY * 4 / Archive.Height)), 1};
    }
}

static_assert(sizeof(LightComputeShape) == 96 && sizeof(LightComputeReceiver) == 144,
              "Slang storage stride mismatch");
} // namespace Frontier
