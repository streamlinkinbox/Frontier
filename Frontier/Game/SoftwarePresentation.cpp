/*==========================================================================================================================================
                                                         SOFTWAREPRESENTATION.CPP
==========================================================================================================================================*/

#include "Frontier/Game/SoftwarePresentation.h"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace Frontier
{

namespace
{

constexpr float Pi = 3.14159265358979f;

struct Vec3
{
    float X = 0.0f, Y = 0.0f, Z = 0.0f;
    Vec3 operator+(const Vec3& O) const { return { X + O.X, Y + O.Y, Z + O.Z }; }
    Vec3 operator-(const Vec3& O) const { return { X - O.X, Y - O.Y, Z - O.Z }; }
    Vec3 operator*(float S) const { return { X * S, Y * S, Z * S }; }
};

float Dot(const Vec3& A, const Vec3& B) { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
Vec3 Cross(const Vec3& A, const Vec3& B) { return { A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X }; }
Vec3 Normalize(Vec3 V)
{
    const float Length = std::sqrt(Dot(V, V));
    return Length > 1e-8f ? V * (1.0f / Length) : Vec3{ 0.0f, 0.0f, 1.0f };
}

struct Frame
{
    int Width = 0, Height = 0;
    std::vector<std::array<float, 3>> Colour;
    std::vector<float> Depth;

    void Clear()
    {
        for (int Y = 0; Y < Height; ++Y)
        {
            const float T = static_cast<float>(Y) / static_cast<float>(std::max(Height - 1, 1));
            for (int X = 0; X < Width; ++X)
                Colour[Y * Width + X] = { 0.16f + 0.5f * T, 0.17f + 0.52f * T, 0.19f + 0.55f * T };   // studio haze
        }
        std::fill(Depth.begin(), Depth.end(), 1e30f);
    }
};

struct Projector
{
    Vec3 Eye, Forward, Right, Up;
    float TanHalfFov = 0.52f;
    float Aspect = 1.6f;

    [[nodiscard]] bool Project(const Vec3& Point, float& OutX, float& OutY, float& OutZ) const
    {
        const Vec3 Rel = Point - Eye;
        OutZ = Dot(Rel, Forward);
        if (OutZ < 0.15f)
            return false;
        OutX = Dot(Rel, Right) / (OutZ * TanHalfFov * Aspect);
        OutY = Dot(Rel, Up) / (OutZ * TanHalfFov);
        return true;
    }
};

void Triangle(Frame& Target, const Projector& Camera, const Vec3 World[3], const std::array<float, 3>& Albedo, float Emission)
{
    float Sx[3], Sy[3], Sz[3];
    for (int Corner = 0; Corner < 3; ++Corner)
        if (!Camera.Project(World[Corner], Sx[Corner], Sy[Corner], Sz[Corner]))
            return;
    const Vec3 Normal = Normalize(Cross(World[1] - World[0], World[2] - World[0]));
    const Vec3 Light = Normalize(Vec3{ 0.25f, 0.15f, 1.0f });   // the softbox, overhead
    const float Lit = 0.28f + 0.72f * std::max(0.0f, std::fabs(Dot(Normal, Light)));

    float Px[3], Py[3];
    for (int Corner = 0; Corner < 3; ++Corner)
    {
        Px[Corner] = (Sx[Corner] * 0.5f + 0.5f) * Target.Width;
        Py[Corner] = (0.5f - Sy[Corner] * 0.5f) * Target.Height;
    }
    const int MinX = std::max(0, static_cast<int>(std::floor(std::min({ Px[0], Px[1], Px[2] }))));
    const int MaxX = std::min(Target.Width - 1, static_cast<int>(std::ceil(std::max({ Px[0], Px[1], Px[2] }))));
    const int MinY = std::max(0, static_cast<int>(std::floor(std::min({ Py[0], Py[1], Py[2] }))));
    const int MaxY = std::min(Target.Height - 1, static_cast<int>(std::ceil(std::max({ Py[0], Py[1], Py[2] }))));
    const float Denominator = (Py[1] - Py[2]) * (Px[0] - Px[2]) + (Px[2] - Px[1]) * (Py[0] - Py[2]);
    if (std::fabs(Denominator) < 1e-9f)
        return;
    for (int Y = MinY; Y <= MaxY; ++Y)
        for (int X = MinX; X <= MaxX; ++X)
        {
            const float W0 = ((Py[1] - Py[2]) * (X - Px[2]) + (Px[2] - Px[1]) * (Y - Py[2])) / Denominator;
            const float W1 = ((Py[2] - Py[0]) * (X - Px[2]) + (Px[0] - Px[2]) * (Y - Py[2])) / Denominator;
            const float W2 = 1.0f - W0 - W1;
            if (W0 < 0.0f || W1 < 0.0f || W2 < 0.0f)
                continue;
            const float Z = 1.0f / (W0 / Sz[0] + W1 / Sz[1] + W2 / Sz[2]);
            const int Index = Y * Target.Width + X;
            if (Z >= Target.Depth[Index])
                continue;
            Target.Depth[Index] = Z;
            const float Brightness = std::min(1.6f, Lit + Emission);
            Target.Colour[Index] = { std::min(1.0f, Albedo[0] * Brightness), std::min(1.0f, Albedo[1] * Brightness),
                                     std::min(1.0f, Albedo[2] * Brightness) };
        }
}

void Edge(Frame& Target, const Projector& Camera, const Vec3& A, const Vec3& B, const std::array<float, 3>& Colour)
{
    float Ax, Ay, Az, Bx, By, Bz;
    if (!Camera.Project(A, Ax, Ay, Az) || !Camera.Project(B, Bx, By, Bz))
        return;
    const float X0 = (Ax * 0.5f + 0.5f) * Target.Width, Y0 = (0.5f - Ay * 0.5f) * Target.Height;
    const float X1 = (Bx * 0.5f + 0.5f) * Target.Width, Y1 = (0.5f - By * 0.5f) * Target.Height;
    const int Steps = static_cast<int>(std::max(std::fabs(X1 - X0), std::fabs(Y1 - Y0)));
    if (Steps <= 0)
        return;
    for (int Step = 0; Step <= Steps; ++Step)
    {
        const float T = static_cast<float>(Step) / static_cast<float>(Steps);
        const int X = static_cast<int>(X0 + (X1 - X0) * T), Y = static_cast<int>(Y0 + (Y1 - Y0) * T);
        if (X < 0 || Y < 0 || X >= Target.Width || Y >= Target.Height)
            continue;
        const float Z = 1.0f / ((1.0f - T) / Az + T / Bz);
        const int Index = Y * Target.Width + X;
        if (Z < Target.Depth[Index])
        {
            Target.Depth[Index] = Z;
            Target.Colour[Index] = Colour;
        }
    }
}

void Quad(Frame& Target, const Projector& Camera, const Vec3 Corners[4], const std::array<float, 3>& Albedo, float Emission)
{
    const Vec3 First[3] = { Corners[0], Corners[1], Corners[2] };
    const Vec3 Second[3] = { Corners[0], Corners[2], Corners[3] };
    Triangle(Target, Camera, First, Albedo, Emission);
    Triangle(Target, Camera, Second, Albedo, Emission);
}

// 📝 Material families are the renderer's CourseMaterial indices, shared with the HTML prototype.
std::array<float, 3> FamilyAlbedo(uint32_t Family, const std::array<float, 3>& Paint, float& Emission)
{
    Emission = 0.0f;
    switch (Family)
    {
        case 6: return Paint;                                   // BodyCoat
        case 7: return { 0.05f, 0.07f, 0.09f };                 // VehicleGlass
        case 8: case 12: return { 0.03f, 0.03f, 0.035f };       // plastic / trim
        case 9: return { 0.012f, 0.012f, 0.012f };              // Tyre
        case 10: Emission = 0.55f; return { 0.10f, 0.45f, 0.55f };  // Hub: the wheel light
        case 11: return { 0.25f, 0.05f, 0.04f };                // Brake
        case 13: return { 0.65f, 0.70f, 0.75f };                // Headlight
        case 14: Emission = 0.25f; return { 0.55f, 0.05f, 0.06f };  // Taillight
        case 70: case 71: case 76: case 77: return { 0.04f, 0.04f, 0.045f };  // cabin
        case 73: return { 0.05f, 0.05f, 0.055f };               // SteeringWheel
        case 74: Emission = 0.04f; return { 0.02f, 0.025f, 0.03f };          // DisplayGlass (tablet, empty)
        case 101: return { 0.85f, 0.86f, 0.87f };               // PlateFace
        case 102: return { 0.02f, 0.02f, 0.02f };               // PlateInk
        default: return { 0.16f, 0.16f, 0.17f };
    }
}

Vec3 Transform(const Vec3& Local, const Vec3& Origin, float CosYaw, float SinYaw)
{
    return { Origin.X + Local.X * CosYaw - Local.Y * SinYaw,
             Origin.Y + Local.X * SinYaw + Local.Y * CosYaw,
             Origin.Z + Local.Z };
}

void DrawArchive(Frame& Target, const Projector& Camera, const FacetArchiveData& Archive,
                 const Vec3& Origin, float YawDegrees, float ZRebase, float YSign,
                 const std::array<float, 3>& Paint, const VehicleInspection& Inspection = {})
{
    const float Yaw = YawDegrees * Pi / 180.0f;
    const float Cos = std::cos(Yaw), Sin = std::sin(Yaw);
    const float Opacity = Inspection.ResolveFaceOpacity();
    const float Length = std::max(Archive.BoundsMaximum[0] - Archive.BoundsMinimum[0], 1e-4f);
    for (uint32_t Facet = 0; Facet < Archive.FacetCount; ++Facet)
    {
        Vec3 World[3];
        float CentreX = 0.0f;
        for (int Corner = 0; Corner < 3; ++Corner)
        {
            const uint32_t Vertex = Archive.Facets[Facet * 3 + Corner];
            const Vec3 Local = { Archive.Positions[Vertex * 3], YSign * Archive.Positions[Vertex * 3 + 1],
                                 Archive.Positions[Vertex * 3 + 2] - ZRebase };
            CentreX += Local.X;
            World[Corner] = Transform(Local, Origin, Cos, Sin);
        }
        // 📝 The X-ray scan: a bright band travelling nose → tail (fraction of the authored length).
        const float ScanFraction = (CentreX / 3.0f - Archive.BoundsMinimum[0]) / Length;
        const bool InBand = Inspection.View == InspectionView::Scan
            && std::fabs(ScanFraction - Inspection.ResolveScanFraction()) < 0.07f;
        if (Inspection.View == InspectionView::Wireframe)
        {
            Edge(Target, Camera, World[0], World[1], { 0.65f, 0.85f, 0.90f });
            Edge(Target, Camera, World[1], World[2], { 0.65f, 0.85f, 0.90f });
            Edge(Target, Camera, World[2], World[0], { 0.65f, 0.85f, 0.90f });
            continue;
        }
        float Emission = 0.0f;
        auto Albedo = FamilyAlbedo(Archive.Families[Facet], Paint, Emission);
        if (Opacity < 1.0f)   // ghost toward the studio haze
        {
            const std::array<float, 3> Haze = { 0.62f, 0.66f, 0.70f };
            for (int Channel = 0; Channel < 3; ++Channel)
                Albedo[Channel] = Albedo[Channel] * Opacity + Haze[Channel] * (1.0f - Opacity);
        }
        if (InBand)
        {
            Albedo = { 0.35f, 0.85f, 0.95f };
            Emission += 1.1f;
        }
        Triangle(Target, Camera, World, Albedo, Emission);
        if (Inspection.ShowsHiddenEdges() && InBand)
            Edge(Target, Camera, World[0], World[1], { 0.75f, 0.95f, 1.0f });
    }
}

} // namespace

bool RenderLobbyPpm(const std::string& Path, const std::vector<PresentationVehicle>& Vehicles,
                    float CameraYawDegrees, float CameraPitchDegrees, float CameraDistanceMetres,
                    int Width, int Height)
{
    Frame Target;
    Target.Width = Width;
    Target.Height = Height;
    Target.Colour.resize(static_cast<size_t>(Width) * Height);
    Target.Depth.resize(static_cast<size_t>(Width) * Height);
    Target.Clear();

    const float Yaw = CameraYawDegrees * Pi / 180.0f;
    const float Pitch = CameraPitchDegrees * Pi / 180.0f;
    Projector Camera;
    const Vec3 Focus = { 0.0f, 0.0f, 0.55f };
    Camera.Eye = { Focus.X + CameraDistanceMetres * std::cos(Pitch) * std::cos(Yaw),
                   Focus.Y + CameraDistanceMetres * std::cos(Pitch) * std::sin(Yaw),
                   Focus.Z + CameraDistanceMetres * std::sin(Pitch) };
    Camera.Forward = Normalize(Focus - Camera.Eye);
    Camera.Right = Normalize(Cross(Camera.Forward, Vec3{ 0.0f, 0.0f, 1.0f }));
    Camera.Up = Cross(Camera.Right, Camera.Forward);
    Camera.TanHalfFov = std::tan(27.5f * Pi / 180.0f);
    Camera.Aspect = static_cast<float>(Width) / static_cast<float>(Height);

    // The studio: dark surround, white cove floor disc, the one softbox overhead.
    const Vec3 Surround[4] = { { -40.0f, -40.0f, -0.02f }, { 40.0f, -40.0f, -0.02f }, { 40.0f, 40.0f, -0.02f }, { -40.0f, 40.0f, -0.02f } };
    Quad(Target, Camera, Surround, { 0.05f, 0.05f, 0.06f }, 0.0f);
    constexpr int Segments = 64;
    for (int Segment = 0; Segment < Segments; ++Segment)
    {
        const float A0 = 2.0f * Pi * Segment / Segments, A1 = 2.0f * Pi * (Segment + 1) / Segments;
        const Vec3 Fan[3] = { { 0.0f, 0.0f, 0.0f },
                              { 17.6f * std::cos(A0), 17.6f * std::sin(A0), 0.0f },
                              { 17.6f * std::cos(A1), 17.6f * std::sin(A1), 0.0f } };
        Triangle(Target, Camera, Fan, { 0.82f, 0.83f, 0.84f }, 0.0f);
        const Vec3 Turntable[3] = { { 0.0f, 0.0f, 0.005f },
                                    { 4.65f * std::cos(A0), 4.65f * std::sin(A0), 0.005f },
                                    { 4.65f * std::cos(A1), 4.65f * std::sin(A1), 0.005f } };
        Triangle(Target, Camera, Turntable, { 0.72f, 0.73f, 0.74f }, 0.0f);
    }
    const Vec3 Softbox[4] = { { -12.0f, -5.5f, 5.85f }, { 12.0f, -5.5f, 5.85f }, { 12.0f, 5.5f, 5.85f }, { -12.0f, 5.5f, 5.85f } };
    Quad(Target, Camera, Softbox, { 0.98f, 0.99f, 1.0f }, 0.9f);

    // The vehicles, from the same archives as the HTML renderer (body rebased onto the centre of mass).
    for (const PresentationVehicle& Vehicle : Vehicles)
    {
        const Vec3 Origin = { Vehicle.PositionMetres[0], Vehicle.PositionMetres[1], Vehicle.PositionMetres[2] };
        if (Vehicle.Body && Vehicle.Body->IsValid())
            DrawArchive(Target, Camera, *Vehicle.Body, Origin, Vehicle.YawDegrees, 0.11285f, 1.0f, Vehicle.Paint, Vehicle.Inspection);
        if (Vehicle.Tyre && Vehicle.Tyre->IsValid())
        {
            // Hubs at the axle mounts (body frame), mirrored on the right side.
            const float MountX[2] = { 1.7274f, -1.6686f };
            for (int Axle = 0; Axle < 2; ++Axle)
                for (int Side = 0; Side < 2; ++Side)
                {
                    const float Sign = Side == 0 ? 1.0f : -1.0f;
                    const Vec3 Local = { MountX[Axle], Sign * 1.0475f, -0.02145f };
                    const float YawRad = Vehicle.YawDegrees * Pi / 180.0f;
                    const Vec3 Hub = Transform(Local, Origin, std::cos(YawRad), std::sin(YawRad));
                    DrawArchive(Target, Camera, *Vehicle.Tyre, Hub, Vehicle.YawDegrees, 0.0f, Sign, Vehicle.Paint);
                    if (Vehicle.Rim && Vehicle.Rim->IsValid())
                        DrawArchive(Target, Camera, *Vehicle.Rim, Hub, Vehicle.YawDegrees, 0.0f, Sign, Vehicle.Paint);
                }
        }
    }

    std::ofstream Stream(Path, std::ios::binary);
    if (!Stream)
        return false;
    Stream << "P6\n" << Width << " " << Height << "\n255\n";
    for (const auto& Pixel : Target.Colour)
        for (int Channel = 0; Channel < 3; ++Channel)
        {
            const float Srgb = std::pow(std::clamp(Pixel[Channel], 0.0f, 1.0f), 1.0f / 2.2f);
            Stream.put(static_cast<char>(std::lround(Srgb * 255.0f)));
        }
    return static_cast<bool>(Stream);
}

} // namespace Frontier
