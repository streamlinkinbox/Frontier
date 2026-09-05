//============================================================================================================================================
//                                                    INTERFACERASTERTEST.CPP
//============================================================================================================================================
// 🧩 P0 proof for the spatial interface. Compiles Engine/Shaders/InterfaceSignedDistance.slang AS C++ (GlslShim.h,
//    FRONTIER_CPU_PORT) so the shapes are proven on exactly the text the GPU will run, drives the REAL
//    InterfaceStructure / InterfaceSequence / MotionIntegrator, and software-rasterises the result to PNG.
//
//    Build (from the repository root):
//      sed -E 's/\.(xyz|xy|yz|xz|zw)\b([^(])/.\1()\2/g' Engine/Shaders/InterfaceSignedDistance.slang > /tmp/InterfaceSignedDistance.port.inc
//      g++ -std=c++20 -O2 -I Scratchpad -I Engine -I ExternalPackages/stb \
//          Scratchpad/InterfaceRasterTest.cpp \
//          Engine/SpatialInterface/InterfaceStructure.cpp Engine/SpatialInterface/InterfaceSequence.cpp \
//          Engine/SpatialInterface/InterfaceLayoutCodec.cpp Engine/SpatialInterface/PaletteConfiguration.cpp \
//          Engine/DisplayPresentation/MotionIntegrator.cpp \
//          Projects/Project-Zero/Source/InterfaceTrialSequence.cpp -o /tmp/InterfaceRasterTest && /tmp/InterfaceRasterTest
//
//    Output: Diagnostics/SpatialInterface_P0_*.png plus a PASS/FAIL acceptance table (non-zero exit on failure).

#include "GlslShim.h"

#define FRONTIER_CPU_PORT
#include "/tmp/InterfaceSignedDistance.port.inc"

#include "SpatialInterface/InterfaceSequence.h"
#include "SpatialInterface/InterfaceLayoutCodec.h"
#include "SpatialInterface/InterfaceStructure.h"
#include "DisplayPresentation/MotionIntegrator.h"
#include "../Projects/Project-Zero/Source/InterfaceTrialSequence.h"

// stb_image_write when the submodule is present; the shim otherwise (identical signature).
#if __has_include(<stb_image_write.h>)
    #define STB_IMAGE_WRITE_IMPLEMENTATION
    #include <stb_image_write.h>
#else
    #include "PngWriteShim.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace Frontier;

namespace {

constexpr int   Width      = 900;
constexpr int   Height      = 560;
constexpr double Step      = 1.0 / 120.0;   // [s] simulated frame

int Failures = 0;

void Check(const char* Name, double Got, double Want, double Tolerance)
{
    const bool Ok = std::fabs(Got - Want) <= Tolerance;
    if (!Ok) ++Failures;
    std::printf("  %-56s %12.5f  (want %.4f +-%.4f)  %s\n", Name, Got, Want, Tolerance, Ok ? "PASS" : "FAIL");
}

void CheckTrue(const char* Name, bool Ok)
{
    if (!Ok) ++Failures;
    std::printf("  %-56s %12s                            %s\n", Name, Ok ? "true" : "false", Ok ? "PASS" : "FAIL");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  SOFTWARE RASTERISER
//------------------------------------------------------------------------------------------------------------------------
// Mirrors what the GPU does: for every pixel, project into each figure's local plane, evaluate the same
//    DistanceFigure, take coverage, composite back-to-front. Deliberately brute force — this is a proof, not a
//    renderer. An orthographic view keeps the geometry auditable; the tilted image below adds perspective.

struct Camera
{
    float CentreX = 0.0f, CentreY = 0.0f;   // [m] world point at the image centre
    float MetresPerPixel = 0.0011f;
    bool  Perspective = false;
    float EyeDistance = 1.2f;               // [m]
    float TiltX = 0.0f, TiltY = 0.0f;       // [rad]
};

struct Canvas
{
    std::vector<float> Rgb;
    Canvas() : Rgb(static_cast<size_t>(Width) * Height * 3u, 0.0f) {}

    void Fill(float R, float G, float B)
    {
        for (size_t I = 0; I < Rgb.size(); I += 3) { Rgb[I] = R; Rgb[I + 1] = G; Rgb[I + 2] = B; }
    }

    void Blend(int X, int Y, float R, float G, float B, float A)
    {
        if (X < 0 || Y < 0 || X >= Width || Y >= Height || A <= 0.0f) return;
        float* P = &Rgb[(static_cast<size_t>(Y) * Width + X) * 3u];
        P[0] = P[0] * (1.0f - A) + R * A;
        P[1] = P[1] * (1.0f - A) + G * A;
        P[2] = P[2] * (1.0f - A) + B * A;
    }

    void Write(const std::string& Path) const
    {
        std::vector<unsigned char> Bytes(Rgb.size());
        for (size_t I = 0; I < Rgb.size(); ++I)
        {
            const float Linear = std::clamp(Rgb[I], 0.0f, 1.0f);
            const float Encoded = Linear <= 0.0031308f ? Linear * 12.92f
                                                       : 1.055f * std::pow(Linear, 1.0f / 2.4f) - 0.055f;
            Bytes[I] = static_cast<unsigned char>(Encoded * 255.0f + 0.5f);
        }
        stbi_write_png(Path.c_str(), Width, Height, 3, Bytes.data(), Width * 3);
    }
};

// A pixel becomes a ray. Orthographic: parallel rays along +Y. Perspective: rays from an eye placed back along −Y
//    and swung by the tilt angles. Both cases are then intersected against each figure's own plane, which is exactly
//    the operation P2's pointer raycast will perform — so this harness exercises that geometry too.
struct ViewRayPair
{
    float OriginX, OriginY, OriginZ;
    float DirectionX, DirectionY, DirectionZ;
};

ViewRayPair ConstructPixelRay(const Camera& View, float PixelX, float PixelY)
{
    const float PlaneX = (PixelX - static_cast<float>(Width)  * 0.5f) * View.MetresPerPixel + View.CentreX;
    const float PlaneZ = (static_cast<float>(Height) * 0.5f - PixelY) * View.MetresPerPixel + View.CentreY;

    ViewRayPair Ray{};
    if (!View.Perspective)
    {
        Ray.OriginX = PlaneX; Ray.OriginY = -10.0f; Ray.OriginZ = PlaneZ;
        Ray.DirectionX = 0.0f; Ray.DirectionY = 1.0f; Ray.DirectionZ = 0.0f;
        return Ray;
    }

    // Pinhole: the eye sits EyeDistance back from the image plane, then the whole camera is rotated by the tilt.
    const float LocalX = PlaneX, LocalZ = PlaneZ, LocalY = View.EyeDistance;

    const float Cx = std::cos(View.TiltX), Sx = std::sin(View.TiltX);
    const float Cy = std::cos(View.TiltY), Sy = std::sin(View.TiltY);

    // Rotate a vector by yaw (about Z) then pitch (about X).
    const auto Rotate = [&](float X, float Y, float Z, float& OutX, float& OutY, float& OutZ)
    {
        const float Ax =  X * Cy - Y * Sy;
        const float Ay =  X * Sy + Y * Cy;
        OutX = Ax;
        OutY = Ay * Cx - Z * Sx;
        OutZ = Ay * Sx + Z * Cx;
    };

    float EyeX, EyeY, EyeZ;
    Rotate(0.0f, -View.EyeDistance, 0.0f, EyeX, EyeY, EyeZ);

    float DirX, DirY, DirZ;
    Rotate(LocalX, LocalY, LocalZ, DirX, DirY, DirZ);
    DirX -= EyeX; DirY -= EyeY; DirZ -= EyeZ;

    const float Length = std::sqrt(DirX * DirX + DirY * DirY + DirZ * DirZ);
    const float Inverse = Length > 1e-9f ? 1.0f / Length : 0.0f;

    Ray.OriginX = EyeX; Ray.OriginY = EyeY; Ray.OriginZ = EyeZ;
    Ray.DirectionX = DirX * Inverse; Ray.DirectionY = DirY * Inverse; Ray.DirectionZ = DirZ * Inverse;
    return Ray;
}

// World point (x, y, z) → image pixel, used only to bound the pixel walk.
void ProjectWorld(const Camera& View, float WorldX, float WorldY, float WorldZ, float& PixelX, float& PixelY)
{
    float X = WorldX, Z = WorldZ;

    if (View.Perspective)
    {
        const float Cx = std::cos(View.TiltX), Sx = std::sin(View.TiltX);
        const float Cy = std::cos(View.TiltY), Sy = std::sin(View.TiltY);

        // Inverse of the camera rotation, then a pinhole divide.
        float Ex, Ey, Ez;
        {
            const float Ax = 0.0f * Cy - (-View.EyeDistance) * Sy;
            const float Ay = 0.0f * Sy + (-View.EyeDistance) * Cy;
            Ex = Ax; Ey = Ay * Cx - 0.0f * Sx; Ez = Ay * Sx + 0.0f * Cx;
        }
        const float Dx = WorldX - Ex, Dy = WorldY - Ey, Dz = WorldZ - Ez;

        // Undo pitch, then yaw.
        const float Ux =  Dx;
        const float Uy =  Dy * Cx + Dz * Sx;
        const float Uz = -Dy * Sx + Dz * Cx;
        const float Vx =  Ux * Cy + Uy * Sy;
        const float Vy = -Ux * Sy + Uy * Cy;

        const float Scale = View.EyeDistance / std::max(Vy, 0.02f);
        X = Vx * Scale;
        Z = Uz * Scale;
        (void)Vy;
    }

    PixelX = static_cast<float>(Width)  * 0.5f + (X - View.CentreX) / View.MetresPerPixel;
    PixelY = static_cast<float>(Height) * 0.5f - (Z - View.CentreY) / View.MetresPerPixel;
}

// World point → the figure's local plane coordinates. The placement rows are orthogonal up to a uniform scale, so
//    the inverse is the transpose divided by the squared scale — no general matrix inverse needed.
void WorldToLocalPlane(const WorldPlacement& Placement, float WorldX, float WorldY, float WorldZ,
                       float& LocalX, float& LocalY)
{
    const float Dx = WorldX - Placement.Row[0][3];
    const float Dy = WorldY - Placement.Row[1][3];
    const float Dz = WorldZ - Placement.Row[2][3];

    const float ScaleSquared = Placement.Row[0][0] * Placement.Row[0][0]
                             + Placement.Row[1][0] * Placement.Row[1][0]
                             + Placement.Row[2][0] * Placement.Row[2][0];
    const float Inverse = ScaleSquared > 1.0e-12f ? 1.0f / ScaleSquared : 0.0f;

    LocalX = (Placement.Row[0][0] * Dx + Placement.Row[1][0] * Dy + Placement.Row[2][0] * Dz) * Inverse;
    LocalY = (Placement.Row[0][1] * Dx + Placement.Row[1][1] * Dy + Placement.Row[2][1] * Dz) * Inverse;
}

// Ray ∩ the figure's plane → the local plane coordinates the shader samples at. Returns false behind the eye.
bool IntersectFigurePlane(const WorldPlacement& Placement, const ViewRayPair& Ray, float& LocalX, float& LocalY)
{
    // Plane normal = the figure's local +Z axis in world space (third column of the rotation part).
    const float Nx = Placement.Row[0][2], Ny = Placement.Row[1][2], Nz = Placement.Row[2][2];
    const float Denominator = Nx * Ray.DirectionX + Ny * Ray.DirectionY + Nz * Ray.DirectionZ;
    if (std::fabs(Denominator) < 1e-9f) return false;

    const float Ox = Placement.Row[0][3] - Ray.OriginX;
    const float Oy = Placement.Row[1][3] - Ray.OriginY;
    const float Oz = Placement.Row[2][3] - Ray.OriginZ;
    const float Travel = (Nx * Ox + Ny * Oy + Nz * Oz) / Denominator;
    if (Travel <= 0.0f) return false;

    const float HitX = Ray.OriginX + Ray.DirectionX * Travel;
    const float HitY = Ray.OriginY + Ray.DirectionY * Travel;
    const float HitZ = Ray.OriginZ + Ray.DirectionZ * Travel;

    WorldToLocalPlane(Placement, HitX, HitY, HitZ, LocalX, LocalY);
    return true;
}

void RasteriseInterface(Canvas& Target, const InterfaceSequence& Sequence, const Camera& View)
{
    const InterfaceInstanceFigure* Slots = Sequence.QueryInstances();
    const uint32_t                 Count = Sequence.QueryInstanceCount();
    if (Slots == nullptr || Count == 0u) return;

    // Submission order is already correct (opaque first, then back-to-front) — composite in that order.
    for (uint32_t I = 0u; I < Count; ++I)
    {
        const InterfaceInstanceFigure& Slot = Slots[I];

        WorldPlacement  Placement;
        InterfaceFigure Figure;
        InterfaceLayoutCodec::Decode(Slot, Figure, Placement);

        const uint32_t Category = Slot.CategoryPalette >> 24;
        const ColourValue Tint  = UnpackColourValue(Slot.Tint);

        // Screen bound: project the four plane corners, pad for the AA feather, and walk only that rectangle.
        float MinX = 1e30f, MinY = 1e30f, MaxX = -1e30f, MaxY = -1e30f;
        const float Corners[4][2] = { { -Slot.HalfWidth, -Slot.HalfHeight }, {  Slot.HalfWidth, -Slot.HalfHeight },
                                      { -Slot.HalfWidth,  Slot.HalfHeight }, {  Slot.HalfWidth,  Slot.HalfHeight } };
        for (const auto& Corner : Corners)
        {
            float Wx, Wy, Wz, Px, Py;
            TransformPlanePoint(Placement, Corner[0], Corner[1], 0.0f, Wx, Wy, Wz);
            ProjectWorld(View, Wx, Wy, Wz, Px, Py);
            MinX = std::min(MinX, Px); MaxX = std::max(MaxX, Px);
            MinY = std::min(MinY, Py); MaxY = std::max(MaxY, Py);
        }

        // The projected bound is an optimisation for the orthographic images. Under perspective the harness walks
        //    the whole frame instead: correctness beats speed in a proof, and 14 figures × 0.5 Mpx costs milliseconds.
        const int X0 = View.Perspective ? 0          : std::max(0, static_cast<int>(std::floor(MinX)) - 2);
        const int X1 = View.Perspective ? Width  - 1 : std::min(Width  - 1, static_cast<int>(std::ceil(MaxX)) + 2);
        const int Y0 = View.Perspective ? 0          : std::max(0, static_cast<int>(std::floor(MinY)) - 2);
        const int Y1 = View.Perspective ? Height - 1 : std::min(Height - 1, static_cast<int>(std::ceil(MaxY)) + 2);

        const float PixelWidth = View.MetresPerPixel;   // one screen pixel in local metres (uniform scale, no tilt)

        for (int Y = Y0; Y <= Y1; ++Y)
        {
            for (int X = X0; X <= X1; ++X)
            {
                const ViewRayPair Ray = ConstructPixelRay(View, static_cast<float>(X) + 0.5f,
                                                                static_cast<float>(Y) + 0.5f);
                float LocalX, LocalY;
                if (!IntersectFigurePlane(Placement, Ray, LocalX, LocalY)) continue;

                const float Distance = DistanceFigure(Category, vec2(LocalX, LocalY),
                                                      vec2(Slot.HalfWidth, Slot.HalfHeight),
                                                      Slot.CornerRadius, Slot.ScalarAlpha, Slot.ScalarBeta);

                float Coverage = CoverageFromDistance(Distance, PixelWidth);
                if (Coverage <= 0.0f) continue;

                Coverage *= ClipCoverage(vec2(LocalX, LocalY),
                                         vec4(Slot.ClipMinimumX, Slot.ClipMinimumY, Slot.ClipMaximumX, Slot.ClipMaximumY),
                                         0.0f, PixelWidth);
                if (Coverage <= 0.0f) continue;

                const float Alpha = Coverage * Slot.Opacity * Tint.Alpha;
                Target.Blend(X, Y, Tint.Red, Tint.Green, Tint.Blue, Alpha);
            }
        }
    }
}

} // namespace

//============================================================================================================================================
//                                                          PROOF
//============================================================================================================================================

int main()
{
    std::printf("\n=== Spatial interface P0 — shape, codec and sequence proof ===\n\n");

    //──────────────────────────────────────────────────────────────────────────
    // 1. Slot contract
    //──────────────────────────────────────────────────────────────────────────
    std::printf("[Slot contract]\n");
    Check("sizeof(InterfaceInstanceFigure)", static_cast<double>(sizeof(InterfaceInstanceFigure)), 96.0, 0.0);

    //──────────────────────────────────────────────────────────────────────────
    // 2. Signed-distance sanity — the shapes the GPU will evaluate
    //──────────────────────────────────────────────────────────────────────────
    std::printf("\n[Signed distances]\n");
    Check("rounded rect centre (inside, -half)", DistanceRoundedRectangle(vec2(0, 0), vec2(0.1f, 0.05f), 0.01f), -0.05, 1e-6);
    Check("rounded rect on the right edge",      DistanceRoundedRectangle(vec2(0.1f, 0), vec2(0.1f, 0.05f), 0.01f), 0.0, 1e-6);
    Check("rounded rect 20 mm outside",          DistanceRoundedRectangle(vec2(0.12f, 0), vec2(0.1f, 0.05f), 0.01f), 0.02, 1e-6);
    Check("corner is rounded, not square",       DistanceRoundedRectangle(vec2(0.1f, 0.05f), vec2(0.1f, 0.05f), 0.02f),
          0.02 * (std::sqrt(2.0) - 1.0), 1e-4);

    Check("lamp radius",                         DistanceLamp(vec2(0.03f, 0.04f), 0.05f), 0.0, 1e-6);
    Check("needle on axis is inside",            DistanceNeedle(vec2(0.05f, 0.0f), 0.0f, 0.1f, 0.01f, 0.002f), -0.003, 5e-4);
    CheckTrue("needle beyond the tip is outside", DistanceNeedle(vec2(0.15f, 0.0f), 0.0f, 0.1f, 0.01f, 0.002f) > 0.0f);
    CheckTrue("needle off-axis is outside",       DistanceNeedle(vec2(0.05f, 0.05f), 0.0f, 0.1f, 0.01f, 0.002f) > 0.0f);

    // An arc at half fill: the midpoint of the drawn span is inside, past the end is not.
    {
        const float Radius = 0.08f, Thickness = 0.01f;
        const float MidAngle = kArcStart + kArcSweep * 0.25f;                 // quarter of the sweep = middle of a half fill
        const float PastAngle = kArcStart + kArcSweep * 0.85f;
        CheckTrue("arc half fill: inside the drawn span",
                  DistanceArc(vec2(std::cos(MidAngle) * Radius, std::sin(MidAngle) * Radius),
                              kArcStart, kArcSweep, 0.5f, Radius, Thickness) < 0.0f);
        CheckTrue("arc half fill: past the end is empty",
                  DistanceArc(vec2(std::cos(PastAngle) * Radius, std::sin(PastAngle) * Radius),
                              kArcStart, kArcSweep, 0.5f, Radius, Thickness) > 0.0f);
        CheckTrue("arc zero fill draws nothing",
                  DistanceArc(vec2(std::cos(MidAngle) * Radius, std::sin(MidAngle) * Radius),
                              kArcStart, kArcSweep, 0.0f, Radius, Thickness) > 1.0f);
    }

    // Seven-segment: digit 8 lights every bar, so it must cover strictly more than digit 1.
    {
        int Eight = 0, One = 0, Blank = 0;
        for (int Y = -30; Y <= 30; ++Y)
            for (int X = -20; X <= 20; ++X)
            {
                const vec2 P(static_cast<float>(X) * 0.001f, static_cast<float>(Y) * 0.001f);
                if (DistanceSegmentDigit(P, 8u, vec2(0.015f, 0.025f), 0.004f) < 0.0f) ++Eight;
                if (DistanceSegmentDigit(P, 1u, vec2(0.015f, 0.025f), 0.004f) < 0.0f) ++One;
                if (DistanceSegmentDigit(P, 10u, vec2(0.015f, 0.025f), 0.004f) < 0.0f) ++Blank;
            }
        CheckTrue("digit 8 covers more than digit 1", Eight > One * 2);
        CheckTrue("digit 1 draws something",          One > 0);
        CheckTrue("blank draws nothing",              Blank == 0);
    }

    // Coverage must be monotone across an edge — an inversion here would alias on the GPU.
    {
        bool Monotone = true;
        float Previous = 1.0f;
        for (int I = -8; I <= 8; ++I)
        {
            const float Distance = static_cast<float>(I) * 0.0002f;
            const float Coverage = CoverageFromDistance(Distance, 0.0011f);
            if (Coverage > Previous + 1e-6f) Monotone = false;
            Previous = Coverage;
        }
        CheckTrue("coverage is monotone across an edge", Monotone);
        Check("coverage at the edge is one half", CoverageFromDistance(0.0f, 0.0011f), 0.5, 1e-6);
    }

    //──────────────────────────────────────────────────────────────────────────
    // 3. Codec round trip
    //──────────────────────────────────────────────────────────────────────────
    std::printf("\n[Codec round trip]\n");
    {
        PaletteConfiguration Palette;
        InterfaceFigure Original;
        Original.Category     = InterfaceCategory::Arc;
        Original.HalfWidth    = 0.0731f;
        Original.HalfHeight   = 0.0642f;
        Original.CornerRadius = 0.0053f;
        Original.ScalarAlpha  = 0.4271f;
        Original.ScalarBeta   = 0.0091f;
        Original.Opacity      = 0.6250f;
        Original.Palette      = PaletteSlot::Warning;
        Original.Placement.Origin  = PlaneOrigin{ 0.11f, -0.23f, 0.37f };
        Original.Placement.RotationZ = 0.4f;
        Original.Placement.RotationX = -0.2f;
        Original.Placement.Scale     = 1.0f;

        const WorldPlacement Placement = ComposePlacement(Original.Placement);

        InterfaceInstanceFigure Slot{};
        InterfaceLayoutCodec::Encode(Original, Placement, Palette, Slot);

        InterfaceFigure Recovered;
        WorldPlacement  RecoveredPlacement;
        InterfaceLayoutCodec::Decode(Slot, Recovered, RecoveredPlacement);

        Check("half width round trip",   Recovered.HalfWidth,    Original.HalfWidth,   0.0);
        Check("half height round trip",  Recovered.HalfHeight,   Original.HalfHeight,  0.0);
        Check("scalar alpha round trip", Recovered.ScalarAlpha,  Original.ScalarAlpha, 0.0);
        Check("scalar beta round trip",  Recovered.ScalarBeta,   Original.ScalarBeta,  0.0);
        Check("opacity round trip",      Recovered.Opacity,      Original.Opacity,     0.0);
        CheckTrue("category round trip", Recovered.Category == Original.Category);
        CheckTrue("palette round trip",  Recovered.Palette  == Original.Palette);

        bool RowsMatch = true;
        for (int R = 0; R < 3; ++R)
            for (int C = 0; C < 4; ++C)
                if (Placement.Row[R][C] != RecoveredPlacement.Row[R][C]) RowsMatch = false;
        CheckTrue("placement rows round trip bit-exact", RowsMatch);

        // A rotation must not change a length — the composed rows stay orthonormal.
        const float LengthX = std::sqrt(Placement.Row[0][0] * Placement.Row[0][0]
                                      + Placement.Row[1][0] * Placement.Row[1][0]
                                      + Placement.Row[2][0] * Placement.Row[2][0]);
        Check("composed basis is unit length", LengthX, 1.0, 1e-5);
    }

    //──────────────────────────────────────────────────────────────────────────
    // 4. The trial interface: one draw, springs, ordering
    //──────────────────────────────────────────────────────────────────────────
    std::printf("\n[Trial interface]\n");

    InterfaceStructure   Structure;
    MotionIntegrator     Motion;
    ProjectZero::InterfaceTrialSequence Trial;
    Trial.Construct(Structure, Motion);

    InterfaceSequence Sequence;
    InterfaceViewConfiguration View;
    View.EyeX = 0.0f; View.EyeY = -1.2f; View.EyeZ = 0.0f;
    View.ForwardX = 0.0f; View.ForwardY = 1.0f; View.ForwardZ = 0.0f;
    Sequence.AssignView(View);
    Sequence.Advance(Structure);

    Check("figures constructed", static_cast<double>(Structure.QueryCount()),
          static_cast<double>(Trial.QueryFigureCount()), 0.0);
    Check("draw count", static_cast<double>(Sequence.QueryMetrics().DrawCount), 1.0, 0.0);
    CheckTrue("every visible figure emitted one instance",
              Sequence.QueryInstanceCount() + Sequence.QueryMetrics().SkippedCount == Structure.QueryCount());

    // Ordering: submission must be opaque-then-transparent, and the transparent tail back-to-front.
    {
        bool Ordered = true, SeenTransparent = false;
        float PreviousDepth = 1e30f;
        for (uint32_t I = 0u; I < Sequence.QueryInstanceCount(); ++I)
        {
            const InterfaceInstanceFigure& Slot = Sequence.QueryInstances()[I];
            const bool Transparent = Slot.Opacity < InterfaceSpecification::OpaqueThreshold;
            if (Transparent) SeenTransparent = true;
            else if (SeenTransparent) Ordered = false;   // an opaque figure after a transparent one

            if (Transparent)
            {
                const float Depth = (Slot.RowXw - View.EyeX) * View.ForwardX
                                  + (Slot.RowYw - View.EyeY) * View.ForwardY
                                  + (Slot.RowZw - View.EyeZ) * View.ForwardZ;
                if (Depth > PreviousDepth + 1e-5f) Ordered = false;   // must be non-increasing: far first
                PreviousDepth = Depth;
            }
        }
        CheckTrue("submission is opaque-first, then back-to-front", Ordered);
    }

    //──────────────────────────────────────────────────────────────────────────
    // 5. Needle spring: settle time and overshoot
    //──────────────────────────────────────────────────────────────────────────
    std::printf("\n[Needle spring]\n");
    {
        Trial.AssignSweepTarget(1.0f);          // full-scale step, the classic instrument test
        double Elapsed = 0.0, Peak = 0.0, SettleTime = -1.0;

        for (int I = 0; I < 1200; ++I)          // 10 s at 120 Hz
        {
            Trial.AdvanceTrial(Structure, Motion, Step, false);   // false = hold the target, do not run the loop
            Elapsed += Step;
            const double Value = Trial.QuerySweepValue();
            Peak = std::max(Peak, Value);
            if (SettleTime < 0.0 && std::fabs(Value - 1.0) < 0.02 && Trial.IsSweepSettled()) SettleTime = Elapsed;
        }

        CheckTrue("needle settles inside the 0.6 s envelope", SettleTime > 0.0 && SettleTime <= 0.6);
        std::printf("  %-56s %12.3f s\n", "(measured settle time)", SettleTime);
        CheckTrue("needle overshoots (hardware feel)", Peak > 1.02);
        CheckTrue("overshoot stays under 15 %",        Peak < 1.15);
        Check("needle rests at the target", Trial.QuerySweepValue(), 1.0, 0.01);
    }

    //──────────────────────────────────────────────────────────────────────────
    // 6. Images
    //──────────────────────────────────────────────────────────────────────────
    std::printf("\n[Images]\n");

    Camera Orthographic;
    Orthographic.CentreX = 0.0f;
    Orthographic.CentreY = 0.0f;
    Orthographic.MetresPerPixel = 0.00062f;

    // Reset the trial and step through one full loop, writing eight frames.
    InterfaceStructure   LoopStructure;
    MotionIntegrator     LoopMotion;
    ProjectZero::InterfaceTrialSequence Loop;
    Loop.Construct(LoopStructure, LoopMotion);

    InterfaceSequence LoopSequence;
    LoopSequence.AssignView(View);

    std::vector<Canvas> Frames;
    const double LoopSeconds = Loop.QueryLoopSeconds();

    for (int FrameIndex = 0; FrameIndex < 8; ++FrameIndex)
    {
        const double TargetTime = LoopSeconds * static_cast<double>(FrameIndex) / 8.0;
        // Advance from the previous frame's time to this one at a fixed step.
        static double Current = 0.0;
        while (Current < TargetTime)
        {
            Loop.AdvanceTrial(LoopStructure, LoopMotion, Step, true);
            Current += Step;
        }

        LoopSequence.Advance(LoopStructure);

        Canvas Frame;
        Frame.Fill(0.012f, 0.013f, 0.016f);
        RasteriseInterface(Frame, LoopSequence, Orthographic);

        char Path[128];
        std::snprintf(Path, sizeof(Path), "Diagnostics/SpatialInterface_P0_Trial_t%d.png", FrameIndex);
        Frame.Write(Path);
        std::printf("  wrote %s   (t = %.2f s, %u instances, %u draw)\n", Path, TargetTime,
                    LoopSequence.QueryInstanceCount(), LoopSequence.QueryMetrics().DrawCount);
        Frames.push_back(std::move(Frame));
    }

    // Contact sheet: the eight frames in two rows, so overshoot and settle read as a sequence.
    {
        constexpr int Columns = 4, Rows = 2;
        const int SheetWidth  = Width  / 2 * Columns;
        const int SheetHeight = Height / 2 * Rows;
        std::vector<unsigned char> Sheet(static_cast<size_t>(SheetWidth) * SheetHeight * 3u, 12u);

        for (size_t I = 0; I < Frames.size(); ++I)
        {
            const int Column = static_cast<int>(I) % Columns;
            const int Row    = static_cast<int>(I) / Columns;
            for (int Y = 0; Y < Height / 2; ++Y)
                for (int X = 0; X < Width / 2; ++X)
                {
                    const size_t Source = (static_cast<size_t>(Y * 2) * Width + X * 2) * 3u;
                    const size_t Target = (static_cast<size_t>(Row * (Height / 2) + Y) * SheetWidth
                                        + Column * (Width / 2) + X) * 3u;
                    for (int C = 0; C < 3; ++C)
                    {
                        const float Linear  = std::clamp(Frames[I].Rgb[Source + C], 0.0f, 1.0f);
                        const float Encoded = Linear <= 0.0031308f ? Linear * 12.92f
                                                                   : 1.055f * std::pow(Linear, 1.0f / 2.4f) - 0.055f;
                        Sheet[Target + C] = static_cast<unsigned char>(Encoded * 255.0f + 0.5f);
                    }
                }
        }
        stbi_write_png("Diagnostics/SpatialInterface_P0_ContactSheet.png", SheetWidth, SheetHeight, 3,
                       Sheet.data(), SheetWidth * 3);
        std::printf("  wrote Diagnostics/SpatialInterface_P0_ContactSheet.png\n");
    }

    // Tilted: the same interface under perspective, proving the shapes stay sharp off-axis.
    {
        Camera Tilted = Orthographic;
        Tilted.Perspective    = true;
        Tilted.TiltY          = 0.40f;
        Tilted.TiltX          = 0.16f;
        Tilted.EyeDistance    = 0.62f;
        Tilted.MetresPerPixel = 0.00082f;   // frames the whole panel at this eye distance

        Canvas Frame;
        Frame.Fill(0.012f, 0.013f, 0.016f);
        RasteriseInterface(Frame, LoopSequence, Tilted);
        Frame.Write("Diagnostics/SpatialInterface_P0_Tilted.png");
        std::printf("  wrote Diagnostics/SpatialInterface_P0_Tilted.png\n");
    }

    //──────────────────────────────────────────────────────────────────────────
    // 7. Verdict
    //──────────────────────────────────────────────────────────────────────────
    const InterfaceMetrics& Final = LoopSequence.QueryMetrics();
    std::printf("\n[Metrics]  figures %u | instances %u | opaque %u | skipped %u | draws %u | compose %.1f us | sort %.1f us\n",
                Final.FigureTotal, Final.InstanceCount, Final.OpaqueCount, Final.SkippedCount, Final.DrawCount,
                Final.ComposeMicroseconds, Final.SortMicroseconds);

    std::printf("\n=== %s (%d failure%s) ===\n\n", Failures == 0 ? "ALL PASS" : "FAILURES", Failures,
                Failures == 1 ? "" : "s");
    return Failures == 0 ? 0 : 1;
}
