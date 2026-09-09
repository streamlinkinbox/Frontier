// ==========================================================================================================
//  SkyRasterProof.cpp — renders the same sky from several camera angles at one hour, so the claim that
//  the exposure no longer moves with the camera can be LOOKED AT rather than read in a number.
// ==========================================================================================================

#include "DisplayPresentation/AtmosphereModel.h"
#include "DisplayPresentation/DaylightSolver.h"
#include "DisplayPresentation/ExposureIntegrator.h"
#include "PngWriteShim.h"
#include "BlockFontShim.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace Frontier::AtmosphereModel;

namespace
{

constexpr int kTile   = 150;   // one camera view
constexpr int kLabel  = 18;   // caption strip under each row
constexpr int kHeader = 14;   // the row's own heading, above its tiles    // caption strip under each row
constexpr int kMargin = 8;

Vec3 Cross(Vec3 A, Vec3 B)
{
    return Vec3 { A.y * B.z - A.z * B.y, A.z * B.x - A.x * B.z, A.x * B.y - A.y * B.x };
}

float Luma(Vec3 C) { return 0.2126f * C.x + 0.7152f * C.y + 0.0722f * C.z; }

// The engine's tone map, in the same order the shader applies it: exposure, then the curve, then sRGB.
Vec3 ToneMap(Vec3 Radiance, float Exposure)
{
    Vec3 C { Radiance.x * Exposure, Radiance.y * Exposure, Radiance.z * Exposure };
    auto Curve = [](float V) { return (V * (2.51f * V + 0.03f)) / (V * (2.43f * V + 0.59f) + 0.14f); };
    C = Vec3 { Curve(C.x), Curve(C.y), Curve(C.z) };
    auto Encode = [](float V)
    {
        V = std::fmin(std::fmax(V, 0.0f), 1.0f);
        return V <= 0.0031308f ? 12.92f * V : 1.055f * std::pow(V, 1.0f / 2.4f) - 0.055f;
    };
    return Vec3 { Encode(C.x), Encode(C.y), Encode(C.z) };
}

// A pinhole camera: yaw about the zenith, pitch above the horizon, 70° across.
Vec3 RayFor(float Yaw, float Pitch, float U, float V)
{
    const float Half = std::tan(35.0f * 3.14159265f / 180.0f);
    const Vec3 Forward { std::sin(Yaw) * std::cos(Pitch), std::cos(Yaw) * std::cos(Pitch), std::sin(Pitch) };
    const Vec3 Right   { std::cos(Yaw), -std::sin(Yaw), 0.0f };
    const Vec3 Up      = Cross(Right, Forward);
    return Normalize(Vec3 { Forward.x + Right.x * U * Half + Up.x * V * Half,
                            Forward.y + Right.y * U * Half + Up.y * V * Half,
                            Forward.z + Right.z * U * Half + Up.z * V * Half });
}


struct Canvas
{
    int W = 0, H = 0;
    std::vector<unsigned char> Pixels;
    void Reset(int Width, int Height) { W = Width; H = Height; Pixels.assign(std::size_t(W) * H * 3, 12); }
    void Plot(int X, int Y, Vec3 C)
    {
        if (X < 0 || Y < 0 || X >= W || Y >= H) { return; }
        const std::size_t I = (std::size_t(Y) * W + X) * 3;
        Pixels[I + 0] = static_cast<unsigned char>(std::fmin(std::fmax(C.x, 0.0f), 1.0f) * 255.0f + 0.5f);
        Pixels[I + 1] = static_cast<unsigned char>(std::fmin(std::fmax(C.y, 0.0f), 1.0f) * 255.0f + 0.5f);
        Pixels[I + 2] = static_cast<unsigned char>(std::fmin(std::fmax(C.z, 0.0f), 1.0f) * 255.0f + 0.5f);
    }
    void Text(int X, int Y, const std::string& Message, Vec3 C)
    {
        int Pen = X;
        for (const char Ch : Message)
        {
            if (const BlockFontShim::Glyph* G = BlockFontShim::Find(Ch))
            {
                for (int R = 0; R < 7; ++R)
                {
                    for (int Col = 0; Col < 5; ++Col)
                    {
                        if (G->Rows[R][Col] == '1') { Plot(Pen + Col, Y + R, C); }
                    }
                }
            }
            Pen += 6;
        }
    }
};

}   // namespace

int main()
{
    // Six directions around one sky, at a sun elevation where the old build broke worst: just above the
    //    horizon, where the bright band and the dark ground are five stops apart inside a single frame.
    struct View { const char* Name; float Yaw; float Pitch; };
    //    ⚠️ Named by angle from the SUN, not by compass point. AtmosphereModel puts the sun on +Y, which this
    //    camera calls yaw zero — the first sheet labelled yaw 180 "AT SUN" and was pointing away from it.
    //    Ordered brightest frame to darkest: straight into the sun, round to the anti-solar sky, then the ground.
    const View Views[] = {
        { "AT SUN",   0.0f,   2.0f }, { "45 OFF",  45.0f,  10.0f }, { "90 OFF",  90.0f,  10.0f },
        { "OPPOSITE",180.0f,  10.0f }, { "ZENITH",   0.0f,  70.0f }, { "GROUND",   0.0f, -25.0f },
    };
    const float Elevations[] = { 2.0f, -4.0f };
    constexpr int kCount = 6;

    Canvas Sheet;
    const int SheetW = kMargin + kCount * (kTile + kMargin);
    const int SheetH = kMargin + 2 * (kHeader + kTile + kLabel + kMargin);
    Sheet.Reset(SheetW, SheetH);

    Frontier::DaylightSolver Solver;
    std::printf("Sky rendered from six camera angles per hour. The exposure is solved ONCE per hour,\n");
    std::printf("from the sun's elevation alone, and reused for every view in that row.\n\n");

    bool AllHold = true;
    for (int Row = 0; Row < 2; ++Row)
    {
        const float Elev = Elevations[Row];
        const Vec3  Sun  = SunAtElevation(Elev);
        const float Rad  = Elev * 3.14159265f / 180.0f;

        Frontier::ExposureIntegrator Exposure;
        Exposure.ObserveIlluminance(Solver.QueryAnchorLuminance(kSunLux, Rad, 1.0f));

        // Each view meters its own frame too — that is the input that used to drag the exposure around.
        std::vector<float> Settings;
        for (int I = 0; I < kCount; ++I)
        {
            Frontier::ExposureIntegrator Probe;
            Probe.ObserveIlluminance(Solver.QueryAnchorLuminance(kSunLux, Rad, 1.0f));
            double Sum = 0.0; int Taken = 0;
            for (int Y = 0; Y < kTile; Y += 6)
            {
                for (int X = 0; X < kTile; X += 6)
                {
                    const float U = (X + 0.5f) / kTile * 2.0f - 1.0f;
                    const float V = 1.0f - (Y + 0.5f) / kTile * 2.0f;
                    const Vec3 Dir = RayFor(Views[I].Yaw * 3.14159265f / 180.0f,
                                            Views[I].Pitch * 3.14159265f / 180.0f, U, V);
                    const float L = Dir.z < 0.0f
                        ? Luma(Vec3(kGroundAlbedo * Solver.QueryIlluminance(kSunLux, Rad, 1.0f) / 3.14159265f))
                        : Luma(SkyRadiance(2.0f, Dir, Sun, Vec3(kSunLux), 24, 8));
                    Sum += std::log(std::fmax(L, 1e-6f)); ++Taken;
                }
            }
            Probe.ObserveLuminance(static_cast<float>(Sum / Taken));
            Probe.Snap();
            Settings.push_back(Probe.QueryExposure());
        }

        float Lo = Settings[0], Hi = Settings[0];
        for (const float S : Settings) { Lo = std::fmin(Lo, S); Hi = std::fmax(Hi, S); }
        const float Spread = std::fabs(std::log2(Hi / std::fmax(Lo, 1e-30f)));
        AllHold = AllHold && Spread < 0.01f;
        std::printf("  sun %+.0f deg : exposure %.6e across all six views, spread %.4f stops %s\n",
                    double(Elev), double(Settings[0]), double(Spread),
                    Spread < 0.01f ? "<- identical" : "<- MOVED");

        for (int I = 0; I < kCount; ++I)
        {
            const int OX = kMargin + I * (kTile + kMargin);
            const int OY = kMargin + Row * (kHeader + kTile + kLabel + kMargin) + kHeader;
            for (int Y = 0; Y < kTile; ++Y)
            {
                for (int X = 0; X < kTile; ++X)
                {
                    const float U = (X + 0.5f) / kTile * 2.0f - 1.0f;
                    const float V = 1.0f - (Y + 0.5f) / kTile * 2.0f;
                    const Vec3 Dir = RayFor(Views[I].Yaw * 3.14159265f / 180.0f,
                                            Views[I].Pitch * 3.14159265f / 180.0f, U, V);
                    Vec3 Radiance;
                    if (Dir.z < 0.0f)
                    {
                        const float G = kGroundAlbedo * Solver.QueryIlluminance(kSunLux, Rad, 1.0f) / 3.14159265f;
                        Radiance = Vec3 { G * 0.9f, G, G * 1.1f };
                    }
                    else
                    {
                        Radiance = SkyRadiance(2.0f, Dir, Sun, Vec3(kSunLux), 24, 8);
                    }
                    Sheet.Plot(OX + X, OY + Y, ToneMap(Radiance, Settings[I]));
                }
            }
            Sheet.Text(OX + 2, OY + kTile + 6, Views[I].Name, Vec3 { 0.72f, 0.72f, 0.76f });
        }
        char Caption[96];
        std::snprintf(Caption, sizeof Caption,
                      "SUN %+.0f deg   ONE EXPOSURE FOR ALL SIX VIEWS   SPREAD %.2f STOPS",
                      double(Elev), double(Spread));
        Sheet.Text(kMargin, kMargin + Row * (kHeader + kTile + kLabel + kMargin) + 3, Caption,
                   Vec3 { 0.55f, 0.88f, 0.58f });
    }

    if (!PngWriteShim::WritePng("Diagnostics/Sky_CameraAngles.png", Sheet.W, Sheet.H, 3, Sheet.Pixels.data(), Sheet.W * 3))
    {
        std::printf("\n  could not write the sheet\n");
        return 1;
    }
    std::printf("\n  wrote Diagnostics/Sky_CameraAngles.png\n");
    return AllHold ? 0 : 1;
}
