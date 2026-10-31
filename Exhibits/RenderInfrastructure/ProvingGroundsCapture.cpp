// 🧩 CPU capture of the Proving Grounds frame: engine geometry + matrices, SurfaceRaster.slang maths mirrored.

#include "Engine/Foundation/Math.h"
#include "Projects/Project-ProvingGrounds/Source/ProvingGrounds.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

// 📝 This is a proof harness, not the renderer: it rasterises ProvingGroundsGeometry with the
//    snapshot matrices on the CPU, using the same flat shading as Engine/Rendering/Shaders/
//    SurfaceRaster.slang (fixed key light, colour * (0.18 + 0.95 * N·L)), then applies an
//    sRGB display encode so the PPM frames read correctly. It exists so the owner can SEE the
//    resolved frame before the SDK Vulkan path lands; it fakes no GPU work — pixels come from
//    the same geometry, transforms and spin the validation proves.
// 📝 The cube (first 36 indices) takes the snapshot model matrix; the ground plane is drawn
//    unrotated — the SDK unit will need per-object transforms, noted for that phase.

namespace
{

struct Raster
{
    uint32_t             Width;
    uint32_t             Height;
    std::vector<float>   Depth;
    std::vector<uint8_t> Pixels;   // [px] - RGB8 after display encode

    void Clear()
    {
        std::fill(Depth.begin(), Depth.end(), 1.0f);
        for (size_t Pixel = 0; Pixel < Depth.size(); ++Pixel)
        {
            Pixels[Pixel * 3 + 0] = 11;   // near-black green-tinted void, like the workspace
            Pixels[Pixel * 3 + 1] = 14;
            Pixels[Pixel * 3 + 2] = 12;
        }
    }
};

float EdgeFunction(Frontier::Vec2 A, Frontier::Vec2 B, Frontier::Vec2 P)
{
    return (P.X - A.X) * (B.Y - A.Y) - (P.Y - A.Y) * (B.X - A.X);
}

uint8_t EncodeChannel(float Linear)
{
    const float Clamped = std::min(1.0f, std::max(0.0f, Linear));
    return static_cast<uint8_t>(std::pow(Clamped, 1.0f / 2.2f) * 255.0f + 0.5f);
}

// Sutherland-Hodgman against one Vulkan clip plane in homogeneous coordinates
// (0 <= Z, -W <= X <= W, -W <= Y <= W, Z <= W). Signed distances make the
// behind-camera half-space intersection exact, so a partially-visible triangle
// is cut to the frustum instead of being dropped whole.
std::vector<Frontier::Vec4> ClipAgainstPlane(const std::vector<Frontier::Vec4>& Polygon, int Plane)
{
    const auto Distance = [Plane](const Frontier::Vec4& Vertex) {
        switch (Plane)
        {
            case 0: return Vertex.Z;              // Near: Z >= 0
            case 1: return Vertex.W - Vertex.Z;   // Far: Z <= W
            case 2: return Vertex.W + Vertex.X;   // Left: X >= -W
            case 3: return Vertex.W - Vertex.X;   // Right: X <= W
            case 4: return Vertex.W + Vertex.Y;   // Bottom: Y >= -W
            default: return Vertex.W - Vertex.Y;  // Top: Y <= W
        }
    };

    std::vector<Frontier::Vec4> Output;
    if (Polygon.empty())
    {
        return Output;
    }
    Frontier::Vec4 Previous = Polygon.back();
    float          PreviousDistance = Distance(Previous);
    for (const Frontier::Vec4& Current : Polygon)
    {
        const float CurrentDistance = Distance(Current);
        if (CurrentDistance >= 0)
        {
            if (PreviousDistance < 0)
            {
                const float Fraction = PreviousDistance / (PreviousDistance - CurrentDistance);
                Output.push_back(Previous + (Current - Previous) * Fraction);
            }
            Output.push_back(Current);
        }
        else if (PreviousDistance >= 0)
        {
            const float Fraction = PreviousDistance / (PreviousDistance - CurrentDistance);
            Output.push_back(Previous + (Current - Previous) * Fraction);
        }
        Previous = Current;
        PreviousDistance = CurrentDistance;
    }
    return Output;
}

void DrawTriangle(Raster& Target, Frontier::Vec3 A, Frontier::Vec3 B, Frontier::Vec3 C, Frontier::Vec3 Colour)
{
    const float MinX = std::max(0.0f, std::min({A.X, B.X, C.X}));
    const float MaxX = std::min(static_cast<float>(Target.Width - 1), std::max({A.X, B.X, C.X}));
    const float MinY = std::max(0.0f, std::min({A.Y, B.Y, C.Y}));
    const float MaxY = std::min(static_cast<float>(Target.Height - 1), std::max({A.Y, B.Y, C.Y}));
    const float Area = EdgeFunction({A.X, A.Y}, {B.X, B.Y}, {C.X, C.Y});
    if (Area == 0)
    {
        return;
    }

    for (int32_t Y = static_cast<int32_t>(MinY); Y <= static_cast<int32_t>(MaxY); ++Y)
    {
        for (int32_t X = static_cast<int32_t>(MinX); X <= static_cast<int32_t>(MaxX); ++X)
        {
            const Frontier::Vec2 P{static_cast<float>(X) + 0.5f, static_cast<float>(Y) + 0.5f};
            const float W0 = EdgeFunction({B.X, B.Y}, {C.X, C.Y}, P) / Area;
            const float W1 = EdgeFunction({C.X, C.Y}, {A.X, A.Y}, P) / Area;
            const float W2 = EdgeFunction({A.X, A.Y}, {B.X, B.Y}, P) / Area;
            if (W0 < 0 || W1 < 0 || W2 < 0)
            {
                continue;
            }
            const float   Z     = W0 * A.Z + W1 * B.Z + W2 * C.Z;   // NDC z is screen-linear
            const size_t  Index = static_cast<size_t>(Y) * Target.Width + static_cast<size_t>(X);
            if (Z < 0 || Z >= Target.Depth[Index])
            {
                continue;
            }
            Target.Depth[Index]        = Z;
            Target.Pixels[Index * 3 + 0] = EncodeChannel(Colour.X);
            Target.Pixels[Index * 3 + 1] = EncodeChannel(Colour.Y);
            Target.Pixels[Index * 3 + 2] = EncodeChannel(Colour.Z);
        }
    }
}

bool WritePpm(const Raster& Target, const char* Path)
{
    FILE* File = std::fopen(Path, "wb");
    if (File == nullptr)
    {
        return false;
    }
    std::fprintf(File, "P6\n%u %u\n255\n", Target.Width, Target.Height);
    std::fwrite(Target.Pixels.data(), 1, Target.Pixels.size(), File);
    std::fclose(File);
    return true;
}

} // namespace

int main(int ArgumentCount, char** Arguments)
{
    using namespace Frontier;

    if (ArgumentCount < 2)
    {
        std::printf("usage: ProvingGroundsCapture <output-directory> [frames]\n");
        return 2;
    }
    const uint32_t Frames = ArgumentCount > 2 ? static_cast<uint32_t>(std::atoi(Arguments[2])) : 48;
    const uint32_t Width  = 640;
    const uint32_t Height = 360;

    const ProvingGroundsGeometry Geometry = BuildProvingGroundsGeometry();
    ProvingGroundsSimulation     Simulation;

    Raster Target;
    Target.Width  = Width;
    Target.Height = Height;
    Target.Depth.assign(static_cast<size_t>(Width) * Height, 1.0f);
    Target.Pixels.assign(static_cast<size_t>(Width) * Height * 3, 0);

    // One full revolution across the sequence (2π / AngularRate seconds).
    const double Step = (2.0 * 3.141592653589793 / ProvingGroundsSimulation::AngularRate) / Frames;
    for (uint32_t Frame = 0; Frame < Frames; ++Frame)
    {
        Simulation.Advance(Step);
        const ProvingGroundsSnapshot Snapshot = Simulation.TakeSnapshot(Width, Height);
        Target.Clear();

        const Vec3 LightDirection = Normalise(Vec3{0.45f, 0.8f, 0.35f});
        for (size_t Triangle = 0; Triangle + 2 < Geometry.Indices.size(); Triangle += 3)
        {
            const bool IsCube = Triangle < 36;
            Vec3       Screen[3];
            Vec3       Shaded{};

            std::vector<Vec4> Polygon;
            for (uint32_t Corner = 0; Corner < 3; ++Corner)
            {
                const ProvingGroundsVertex& Vertex = Geometry.Vertices[Geometry.Indices[Triangle + Corner]];
                const Vec3 World = IsCube ? TransformPoint(Snapshot.ModelToWorld, Vertex.Position) : Vertex.Position;
                if (Corner == 0)
                {
                    const Vec3 Normal  = IsCube ? Normalise(TransformDirection(Snapshot.ModelToWorld, Vertex.Normal)) : Vertex.Normal;
                    const float Diffuse = std::min(1.0f, std::max(0.0f, Dot(Normal, LightDirection)));
                    Shaded              = Vertex.Colour * (0.18f + 0.95f * Diffuse);
                }
                Polygon.push_back(Transform(Snapshot.ViewProjection, {World.X, World.Y, World.Z, 1.0f}));
            }
            // Clip in homogeneous coordinates before the divide (the front ground triangle
            // crosses behind the camera; dropping it whole sliced the plane in two).
            for (int Plane = 0; Plane < 6 && Polygon.size() >= 3; ++Plane)
            {
                Polygon = ClipAgainstPlane(Polygon, Plane);
            }
            for (size_t Corner = 1; Corner + 1 < Polygon.size(); ++Corner)
            {
                const Vec4* Fan[3] = {&Polygon[0], &Polygon[Corner], &Polygon[Corner + 1]};
                for (uint32_t Index = 0; Index < 3; ++Index)
                {
                    const Vec3 Ndc{Fan[Index]->X / Fan[Index]->W, Fan[Index]->Y / Fan[Index]->W,
                                   Fan[Index]->Z / Fan[Index]->W};
                    Screen[Index] = {(Ndc.X * 0.5f + 0.5f) * static_cast<float>(Width),
                                     (Ndc.Y * 0.5f + 0.5f) * static_cast<float>(Height), Ndc.Z};
                }
                DrawTriangle(Target, Screen[0], Screen[1], Screen[2], Shaded);
            }
        }

        char Path[512] = {};
        std::snprintf(Path, sizeof(Path), "%s/frame-%02u.ppm", Arguments[1], Frame);
        if (!WritePpm(Target, Path))
        {
            std::printf("cannot write %s\n", Path);
            return 1;
        }
    }

    std::printf("ProvingGroundsCapture: %u frames at %ux%u -> %s\n", Frames, Width, Height, Arguments[1]);
    return 0;
}
