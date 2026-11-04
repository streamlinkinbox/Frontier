/*============================================================================================================================================
                                                  PAINTSTAMPENGINE.CPP
============================================================================================================================================*/
// 🧱 Surface half of the stamp engine port: canvases, CPU raster (the Canvas2D equivalents the HTML
//    stamps drew with), surface fields, the per-event sampler, drips, tuft packing and the HTML-parity
//    sphere adapter. The eleven stamps themselves live in PaintStampTools.cpp.

#include "UI/TextureStudio/PaintStampEngine.h"
#include <algorithm>
#include <cmath>

namespace Frontier
{
namespace
{
constexpr float Pi = 3.14159265358979323846f;
// 🌐 HTML px-per-world at the sphere equator (RES 1024 over a 2π·0.72 circumference) - the reference
//    every HTML px constant was tuned against; Frame::PxScale rescales them to any other surface.
constexpr float ReferencePxPerWorld = 1024.f / (2.f * Pi * 0.72f);
constexpr float DripWorldRadius = 0.72f; // HTML stepDrips moves on this sphere; kept for run-length parity

template <typename T> T Clamp(T V, T Lo, T Hi) noexcept { return V < Lo ? Lo : (V > Hi ? Hi : V); }
uint8_t ToByte(float V) noexcept { return uint8_t(Clamp(V, 0.f, 255.f) + 0.5f); }
float Dot3(const float A[3], const float B[3]) noexcept { return A[0] * B[0] + A[1] * B[1] + A[2] * B[2]; }
void Cross3(const float A[3], const float B[3], float Out[3]) noexcept
{
    Out[0] = A[1] * B[2] - A[2] * B[1];
    Out[1] = A[2] * B[0] - A[0] * B[2];
    Out[2] = A[0] * B[1] - A[1] * B[0];
}
float Len3(const float A[3]) noexcept { return std::sqrt(Dot3(A, A)); }
void Normalize3(float A[3]) noexcept
{
    const float L = Len3(A);
    if (L > 1e-12f) { A[0] /= L; A[1] /= L; A[2] /= L; }
}
std::array<float, 2> SphereUv(const float P[3], float Radius) noexcept
{
    // inverse of the HTML worldFromUV: p = (-cos φ·sin θ, cos θ, sin φ·sin θ)·R, u = φ/2π, v = θ/π
    const float Theta = std::acos(Clamp(P[1] / Radius, -1.f, 1.f));
    float Phi = std::atan2(P[2], -P[0]);
    if (Phi < 0) Phi += 2.f * Pi;
    return {Phi / (2.f * Pi), Theta / Pi};
}
} // namespace

PaintStampEngine::PaintStampEngine()
{
    Configure(1024, 1024, 256, 256, 1.f, 1.f, {212.f, 207.f, 200.f}, true);
}

void PaintStampEngine::Configure(int WidthPx, int HeightPx, int FieldW, int FieldH, float UvRangeU, float UvRangeV,
    std::array<float, 3> BaseColour, bool Wrap)
{
    W = std::max(8, WidthPx);
    H = std::max(8, HeightPx);
    FW = std::max(8, FieldW);
    FH = std::max(8, FieldH);
    RangeU = UvRangeU > 0 ? UvRangeU : 1.f;
    RangeV = UvRangeV > 0 ? UvRangeV : 1.f;
    Base = BaseColour;
    WrapU = Wrap;
    ColourCanvas.assign(size_t(W) * H, {ToByte(Base[0]), ToByte(Base[1]), ToByte(Base[2]), 255});
    HeightCanvas.assign(size_t(W) * H, 128);
    TackCanvas.assign(size_t(W) * H, 0);
    CoverageCanvas.assign(size_t(W) * H, 0);
    WetField.assign(size_t(FW) * FH, 0.f);
    DryField.assign(size_t(FW) * FH, 0.f);
    SolventField.assign(size_t(FW) * FH, 0.f);
    Clear();
    RebuildTuft();
}

void PaintStampEngine::Clear() noexcept
{
    std::fill(ColourCanvas.begin(), ColourCanvas.end(), std::array<uint8_t, 4>{ToByte(Base[0]), ToByte(Base[1]), ToByte(Base[2]), 255});
    std::fill(HeightCanvas.begin(), HeightCanvas.end(), uint8_t(128));
    std::fill(TackCanvas.begin(), TackCanvas.end(), uint8_t(0));
    std::fill(CoverageCanvas.begin(), CoverageCanvas.end(), uint8_t(0));
    std::fill(WetField.begin(), WetField.end(), 0.f);
    std::fill(DryField.begin(), DryField.end(), 0.f);
    std::fill(SolventField.begin(), SolventField.end(), 0.f);
    Drips.clear();
    Painted = false;
    HaveYaw = false;
    BrushZ = 0;
    PenWear = 0;
    SprayEnv = 0;
    CanHeat = 1;
    SprayAcc = 0;
    HaveSprayClock = false;
    SandGrains.clear();
    SplatEvidence = {};
    for (auto &Hair : Hairs) Hair.HasPrev = Hair.HasTip = false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     FRAME
//------------------------------------------------------------------------------------------------------------------------

PaintStampEngine::Frame PaintStampEngine::MakeFrame(const StampHit &Hit) const noexcept
{
    Frame F;
    F.U = Hit.U / RangeU;
    F.V = Hit.V / RangeV;
    F.Dx[0] = Hit.DUvDx[0] / RangeU;
    F.Dx[1] = Hit.DUvDx[1] / RangeV;
    F.Dy[0] = Hit.DUvDy[0] / RangeU;
    F.Dy[1] = Hit.DUvDy[1] / RangeV;
    for (int i = 0; i < 3; i++) { F.AxisX[i] = Hit.AxisX[i]; F.AxisY[i] = Hit.AxisY[i]; F.Normal[i] = Hit.Normal[i]; F.Point[i] = Hit.Point[i]; }
    F.Pressure = Hit.Pressure > 0 ? Hit.Pressure : 0.7f;
    const float PxX = std::hypot(F.Dx[0] * W, F.Dx[1] * H);
    const float PxY = std::hypot(F.Dy[0] * W, F.Dy[1] * H);
    F.PxScale = Clamp(0.5f * (PxX + PxY) / ReferencePxPerWorld, 0.2f, 5.f);
    return F;
}

void PaintStampEngine::RotateFrameToMotion(Frame &F, const float FromPoint[3]) const noexcept
{
    const float Move[3]{F.Point[0] - FromPoint[0], F.Point[1] - FromPoint[1], F.Point[2] - FromPoint[2]};
    const float Along = Dot3(Move, F.AxisX), Across = Dot3(Move, F.AxisY);
    if (Along * Along + Across * Across <= 1e-8f) return;
    const float Mag = std::hypot(Along, Across);
    float NewX[3], NewY[3];
    for (int i = 0; i < 3; i++) NewX[i] = F.AxisX[i] * (Along / Mag) + F.AxisY[i] * (Across / Mag);
    Normalize3(NewX);
    Cross3(F.Normal, NewX, NewY);
    Normalize3(NewY);
    const float A = Dot3(NewX, F.AxisX), B = Dot3(NewX, F.AxisY);
    const float C = Dot3(NewY, F.AxisX), D = Dot3(NewY, F.AxisY);
    const float Dx[2]{F.Dx[0] * A + F.Dy[0] * B, F.Dx[1] * A + F.Dy[1] * B};
    const float Dy[2]{F.Dx[0] * C + F.Dy[0] * D, F.Dx[1] * C + F.Dy[1] * D};
    F.Dx[0] = Dx[0]; F.Dx[1] = Dx[1]; F.Dy[0] = Dy[0]; F.Dy[1] = Dy[1];
    for (int i = 0; i < 3; i++) { F.AxisX[i] = NewX[i]; F.AxisY[i] = NewY[i]; }
}

std::array<float, 2> PaintStampEngine::UvAt(const Frame &F, float Ox, float Oy) const noexcept
{
    return {F.U + Ox * F.Dx[0] + Oy * F.Dy[0], F.V + Ox * F.Dx[1] + Oy * F.Dy[1]};
}

void PaintStampEngine::Downhill(const Frame &F, float &OutX, float &OutY, float &OutSlope) const noexcept
{
    const float Ny = F.Normal[1];
    const float Proj[3]{F.Normal[0] * Ny, -1.f + Ny * Ny, F.Normal[2] * Ny}; // gravity projected on the tangent plane
    OutSlope = Len3(Proj);
    if (OutSlope <= 1e-6f) { OutX = OutY = 0; return; }
    const float Unit[3]{Proj[0] / OutSlope, Proj[1] / OutSlope, Proj[2] / OutSlope};
    OutX = Dot3(F.AxisX, Unit);
    OutY = Dot3(F.AxisY, Unit);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     CPU RASTER
//------------------------------------------------------------------------------------------------------------------------

void PaintStampEngine::BlendPixel(int X, int Y, const std::array<float, 3> &Rgb, float Alpha, Blend Mode) noexcept
{
    if (X < 0 || X >= W || Y < 0 || Y >= H || Alpha <= 0.001f) return;
    const size_t Index = size_t(Y) * W + X;
    auto &Dst = ColourCanvas[Index];
    const float A = Clamp(Alpha, 0.f, 1.f);
    for (int C = 0; C < 3; C++)
    {
        const float Src = Mode == Blend::Multiply ? (Rgb[C] * Dst[C] / 255.f) : Rgb[C];
        Dst[C] = ToByte(Src * A + Dst[C] * (1.f - A));
    }
    Painted = true;
    uint8_t &Cov = CoverageCanvas[Index];
    if (EraseCoverage) Cov = ToByte(Cov * (1.f - A));
    else Cov = ToByte(Cov + (255.f - Cov) * A); // source-over, like the colour: passes build up
}

void PaintStampEngine::HeightPixel(int X, int Y, float Grey, float Alpha) noexcept
{
    if (X < 0 || X >= W || Y < 0 || Y >= H || Alpha <= 0.001f) return;
    uint8_t &Dst = HeightCanvas[size_t(Y) * W + X];
    const float A = Clamp(Alpha, 0.f, 1.f);
    Dst = ToByte(Grey * A + Dst * (1.f - A));
}

void PaintStampEngine::TackPixel(int X, int Y, float Grey, float Alpha) noexcept
{
    if (X < 0 || X >= W || Y < 0 || Y >= H || Alpha <= 0.001f) return;
    uint8_t &Dst = TackCanvas[size_t(Y) * W + X];
    const float A = Clamp(Alpha, 0.f, 1.f);
    Dst = ToByte(Grey * A + Dst * (1.f - A));
}

namespace
{
// 🧩 Run a px-space draw lambda at x, plus seam copies at x±W when the shape crosses an edge.
template <typename DrawFn>
void WithWrap(bool Wrap, int W, float MinX, float MaxX, DrawFn &&Draw) noexcept
{
    Draw(0.f);
    if (!Wrap) return;
    if (MinX < 0) Draw(float(W));
    if (MaxX > W) Draw(float(-W));
}
} // namespace

void PaintStampEngine::FillCirclePx(float Cx, float Cy, float R, const std::array<float, 3> &Rgb, float Alpha, Blend Mode) noexcept
{
    if (R <= 0) return;
    WithWrap(WrapU, W, Cx - R, Cx + R, [&](float Ox) noexcept {
        const int X0 = std::max(0, int(std::floor(Cx + Ox - R))), X1 = std::min(W - 1, int(std::ceil(Cx + Ox + R)));
        const int Y0 = std::max(0, int(std::floor(Cy - R))), Y1 = std::min(H - 1, int(std::ceil(Cy + R)));
        for (int Y = Y0; Y <= Y1; Y++)
            for (int X = X0; X <= X1; X++)
            {
                const float Dx = X + 0.5f - (Cx + Ox), Dy = Y + 0.5f - Cy;
                if (Dx * Dx + Dy * Dy <= R * R) BlendPixel(X, Y, Rgb, Alpha, Mode);
            }
    });
}

void PaintStampEngine::FillEllipsePx(float Cx, float Cy, float Rx, float Ry, float Rot, const std::array<float, 3> &Rgb, float Alpha, Blend Mode) noexcept
{
    const float Extent = std::max(Rx, Ry);
    if (Extent <= 0) return;
    const float CosR = std::cos(Rot), SinR = std::sin(Rot);
    WithWrap(WrapU, W, Cx - Extent, Cx + Extent, [&](float Ox) noexcept {
        const int X0 = std::max(0, int(std::floor(Cx + Ox - Extent))), X1 = std::min(W - 1, int(std::ceil(Cx + Ox + Extent)));
        const int Y0 = std::max(0, int(std::floor(Cy - Extent))), Y1 = std::min(H - 1, int(std::ceil(Cy + Extent)));
        for (int Y = Y0; Y <= Y1; Y++)
            for (int X = X0; X <= X1; X++)
            {
                const float Dx = X + 0.5f - (Cx + Ox), Dy = Y + 0.5f - Cy;
                const float Ex = (Dx * CosR + Dy * SinR) / Rx, Ey = (-Dx * SinR + Dy * CosR) / Ry;
                if (Ex * Ex + Ey * Ey <= 1.f) BlendPixel(X, Y, Rgb, Alpha, Mode);
            }
    });
}

void PaintStampEngine::FillPolygonPx(const std::vector<std::array<float, 2>> &Pts, const std::array<float, 3> &Rgb, float Alpha, Blend Mode) noexcept
{
    if (Pts.size() < 3) return;
    float MinX = Pts[0][0], MaxX = Pts[0][0], MinY = Pts[0][1], MaxY = Pts[0][1];
    for (const auto &P : Pts) { MinX = std::min(MinX, P[0]); MaxX = std::max(MaxX, P[0]); MinY = std::min(MinY, P[1]); MaxY = std::max(MaxY, P[1]); }
    WithWrap(WrapU, W, MinX, MaxX, [&](float Ox) noexcept {
        const int X0 = std::max(0, int(std::floor(MinX + Ox))), X1 = std::min(W - 1, int(std::ceil(MaxX + Ox)));
        const int Y0 = std::max(0, int(std::floor(MinY))), Y1 = std::min(H - 1, int(std::ceil(MaxY)));
        for (int Y = Y0; Y <= Y1; Y++)
            for (int X = X0; X <= X1; X++)
            {
                const float Px = X + 0.5f - Ox, Py = Y + 0.5f;
                bool Inside = false;
                for (size_t I = 0, J = Pts.size() - 1; I < Pts.size(); J = I++)
                {
                    const auto &A = Pts[I], &B = Pts[J];
                    if ((A[1] > Py) != (B[1] > Py) && Px < (B[0] - A[0]) * (Py - A[1]) / (B[1] - A[1]) + A[0]) Inside = !Inside;
                }
                if (Inside) BlendPixel(X, Y, Rgb, Alpha, Mode);
            }
    });
}

namespace
{
float SegmentDistance(float Px, float Py, float X0, float Y0, float X1, float Y1) noexcept
{
    const float Dx = X1 - X0, Dy = Y1 - Y0;
    const float LenSq = Dx * Dx + Dy * Dy;
    float T = LenSq > 0 ? ((Px - X0) * Dx + (Py - Y0) * Dy) / LenSq : 0.f;
    T = T < 0 ? 0 : (T > 1 ? 1 : T);
    return std::hypot(Px - (X0 + Dx * T), Py - (Y0 + Dy * T));
}
} // namespace

void PaintStampEngine::StrokeLinePx(float X0, float Y0, float X1, float Y1, float Width, const std::array<float, 3> &Rgb, float Alpha, Blend Mode) noexcept
{
    const float Half = std::max(0.05f, Width * 0.5f);
    WithWrap(WrapU, W, std::min(X0, X1) - Half, std::max(X0, X1) + Half, [&](float Ox) noexcept {
        const int Xa = std::max(0, int(std::floor(std::min(X0, X1) + Ox - Half))), Xb = std::min(W - 1, int(std::ceil(std::max(X0, X1) + Ox + Half)));
        const int Ya = std::max(0, int(std::floor(std::min(Y0, Y1) - Half))), Yb = std::min(H - 1, int(std::ceil(std::max(Y0, Y1) + Half)));
        for (int Y = Ya; Y <= Yb; Y++)
            for (int X = Xa; X <= Xb; X++)
                if (SegmentDistance(X + 0.5f, Y + 0.5f, X0 + Ox, Y0, X1 + Ox, Y1) <= Half) BlendPixel(X, Y, Rgb, Alpha, Mode);
    });
}

void PaintStampEngine::HeightCirclePx(float Cx, float Cy, float R, float Grey, float Alpha) noexcept
{
    if (R <= 0) return;
    WithWrap(WrapU, W, Cx - R, Cx + R, [&](float Ox) noexcept {
        const int X0 = std::max(0, int(std::floor(Cx + Ox - R))), X1 = std::min(W - 1, int(std::ceil(Cx + Ox + R)));
        const int Y0 = std::max(0, int(std::floor(Cy - R))), Y1 = std::min(H - 1, int(std::ceil(Cy + R)));
        for (int Y = Y0; Y <= Y1; Y++)
            for (int X = X0; X <= X1; X++)
            {
                const float Dx = X + 0.5f - (Cx + Ox), Dy = Y + 0.5f - Cy;
                if (Dx * Dx + Dy * Dy <= R * R) HeightPixel(X, Y, Grey, Alpha);
            }
    });
}

void PaintStampEngine::HeightEllipsePx(float Cx, float Cy, float Rx, float Ry, float Rot, float Grey, float Alpha) noexcept
{
    const float Extent = std::max(Rx, Ry);
    if (Extent <= 0) return;
    const float CosR = std::cos(Rot), SinR = std::sin(Rot);
    WithWrap(WrapU, W, Cx - Extent, Cx + Extent, [&](float Ox) noexcept {
        const int X0 = std::max(0, int(std::floor(Cx + Ox - Extent))), X1 = std::min(W - 1, int(std::ceil(Cx + Ox + Extent)));
        const int Y0 = std::max(0, int(std::floor(Cy - Extent))), Y1 = std::min(H - 1, int(std::ceil(Cy + Extent)));
        for (int Y = Y0; Y <= Y1; Y++)
            for (int X = X0; X <= X1; X++)
            {
                const float Dx = X + 0.5f - (Cx + Ox), Dy = Y + 0.5f - Cy;
                const float Ex = (Dx * CosR + Dy * SinR) / Rx, Ey = (-Dx * SinR + Dy * CosR) / Ry;
                if (Ex * Ex + Ey * Ey <= 1.f) HeightPixel(X, Y, Grey, Alpha);
            }
    });
}

void PaintStampEngine::HeightLinePx(float X0, float Y0, float X1, float Y1, float Width, float Grey, float Alpha) noexcept
{
    const float Half = std::max(0.05f, Width * 0.5f);
    WithWrap(WrapU, W, std::min(X0, X1) - Half, std::max(X0, X1) + Half, [&](float Ox) noexcept {
        const int Xa = std::max(0, int(std::floor(std::min(X0, X1) + Ox - Half))), Xb = std::min(W - 1, int(std::ceil(std::max(X0, X1) + Ox + Half)));
        const int Ya = std::max(0, int(std::floor(std::min(Y0, Y1) - Half))), Yb = std::min(H - 1, int(std::ceil(std::max(Y0, Y1) + Half)));
        for (int Y = Ya; Y <= Yb; Y++)
            for (int X = Xa; X <= Xb; X++)
                if (SegmentDistance(X + 0.5f, Y + 0.5f, X0 + Ox, Y0, X1 + Ox, Y1) <= Half) HeightPixel(X, Y, Grey, Alpha);
    });
}

void PaintStampEngine::SoftDabPx(bool Height, float Cx, float Cy, float Rx, float Ry, const std::array<float, 3> &Rgb, float Alpha, float Inner) noexcept
{
    const float Extent = std::max(Rx, Ry);
    if (Extent <= 0) return;
    WithWrap(WrapU, W, Cx - Rx, Cx + Rx, [&](float Ox) noexcept {
        const int X0 = std::max(0, int(std::floor(Cx + Ox - Rx))), X1 = std::min(W - 1, int(std::ceil(Cx + Ox + Rx)));
        const int Y0 = std::max(0, int(std::floor(Cy - Ry))), Y1 = std::min(H - 1, int(std::ceil(Cy + Ry)));
        for (int Y = Y0; Y <= Y1; Y++)
            for (int X = X0; X <= X1; X++)
            {
                const float Ex = (X + 0.5f - (Cx + Ox)) / Rx, Ey = (Y + 0.5f - Cy) / Ry;
                const float T = std::sqrt(Ex * Ex + Ey * Ey);
                if (T > 1.f) continue;
                float A = Alpha;
                if (Inner < 0.98f) A *= T <= Inner ? 1.f : std::max(0.f, 1.f - (T - Inner) / std::max(0.001f, 1.f - Inner));
                if (Height) HeightPixel(X, Y, Rgb[0], A);
                else BlendPixel(X, Y, Rgb, A, Blend::Over);
            }
    });
}

void PaintStampEngine::UvLine(float U0, float V0, float U1, float V1, const std::function<void(float, float, float, float)> &Draw) const
{
    const float Du = U1 - U0;
    if (WrapU && Du < -0.5f)
    {
        Draw(U0 * W, V0 * H, (U1 + 1.f) * W, V1 * H);
        Draw((U0 - 1.f) * W, V0 * H, U1 * W, V1 * H);
    }
    else if (WrapU && Du > 0.5f)
    {
        Draw(U0 * W, V0 * H, (U1 - 1.f) * W, V1 * H);
        Draw((U0 + 1.f) * W, V0 * H, U1 * W, V1 * H);
    }
    else
    {
        Draw(U0 * W, V0 * H, U1 * W, V1 * H);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     FIELDS / NOISE / SAMPLER
//------------------------------------------------------------------------------------------------------------------------

float PaintStampEngine::FieldAt(const std::vector<float> &F, float U, float V) const noexcept
{
    const int X = Clamp(int(U * FW), 0, FW - 1);
    const int Y = Clamp(int(V * FH), 0, FH - 1);
    return F[size_t(Y) * FW + X];
}

void PaintStampEngine::FieldDisc(std::vector<float> &F, float U, float V, float RadiusUV, float Amount, bool Additive) noexcept
{
    const int RcX = std::max(1, int(std::lround(RadiusUV * FW)));
    const int RcY = std::max(1, int(std::lround(RadiusUV * FH)));
    const int Cx = int(std::lround(U * FW)), Cy = int(std::lround(V * FH));
    for (int Y = -RcY; Y <= RcY; Y++)
    {
        const int Yy = Cy + Y;
        if (Yy < 0 || Yy >= FH) continue;
        const float Yn = float(Y) / RcY;
        const int Span = int(std::floor(RcX * std::sqrt(std::max(0.f, 1.f - Yn * Yn))));
        for (int X = -Span; X <= Span; X++)
        {
            int Xx = Cx + X;
            if (WrapU) Xx = ((Xx % FW) + FW) % FW;
            else if (Xx < 0 || Xx >= FW) continue;
            float &Cell = F[size_t(Yy) * FW + Xx];
            if (Additive) Cell = Clamp(Cell + Amount, 0.f, 1.2f);
            else if (Amount > Cell) Cell = Amount;
        }
    }
}

void PaintStampEngine::WetDisc(float U, float V, float RadiusUV, float Amount) noexcept { FieldDisc(WetField, U, V, RadiusUV, Amount, false); }
void PaintStampEngine::DryDisc(float U, float V, float RadiusUV, float Amount) noexcept { FieldDisc(DryField, U, V, RadiusUV, Amount, true); }
void PaintStampEngine::SolventDisc(float U, float V, float RadiusUV, float Amount) noexcept { FieldDisc(SolventField, U, V, RadiusUV, Amount, false); }

void PaintStampEngine::WetSegment(float U0, float V0, float U1, float V1, float Amount) noexcept
{
    float Du = U1 - U0;
    if (WrapU) { if (Du > 0.5f) Du -= 1; if (Du < -0.5f) Du += 1; }
    const float Dv = V1 - V0;
    const int Steps = std::max(1, int(std::ceil(std::hypot(Du, Dv) * FW * 1.5f)));
    for (int S = 0; S <= Steps; S++)
    {
        float Uu = U0 + Du * (float(S) / Steps);
        if (WrapU) Uu -= std::floor(Uu);
        const float Vv = V0 + Dv * (float(S) / Steps);
        const int X = Clamp(int(Uu * FW), 0, FW - 1);
        const int Y = Clamp(int(Vv * FH), 0, FH - 1);
        float &Cell = WetField[size_t(Y) * FW + X];
        if (Amount > Cell) Cell = Amount;
    }
}

float PaintStampEngine::Rand() noexcept
{
    return std::uniform_real_distribution<float>(0.f, 1.f)(Rng);
}

float PaintStampEngine::Gauss() noexcept
{
    float U = 0, V = 0;
    while (U == 0) U = Rand();
    while (V == 0) V = Rand();
    return std::sqrt(-2.f * std::log(U)) * std::cos(2.f * Pi * V);
}

float PaintStampEngine::ToothAt(float U, float V) noexcept
{
    const float X = std::floor(U * 900.f), Y = std::floor(V * 900.f);
    float N = std::sin(X * 127.1f + Y * 311.7f) * 43758.5453f;
    return N - std::floor(N);
}

float PaintStampEngine::ValueNoise(float X, float Y) noexcept
{
    const float X0 = std::floor(X), Y0 = std::floor(Y);
    const float Fx = X - X0, Fy = Y - Y0;
    const float Sx = Fx * Fx * (3 - 2 * Fx), Sy = Fy * Fy * (3 - 2 * Fy);
    auto Hash = [](float I, float J) noexcept {
        float N = std::sin(I * 127.1f + J * 311.7f) * 43758.5453f;
        return N - std::floor(N);
    };
    const float A = Hash(X0, Y0), B = Hash(X0 + 1, Y0), C = Hash(X0, Y0 + 1), D = Hash(X0 + 1, Y0 + 1);
    return A + (B - A) * Sx + (C - A) * Sy + (A - B - C + D) * Sx * Sy;
}

float PaintStampEngine::ToothRidge(float U, float V) noexcept
{
    return ValueNoise(U * 150.f, V * 150.f) * 0.62f + ValueNoise(U * 420.f + 37.f, V * 420.f + 11.f) * 0.38f;
}

std::array<float, 3> PaintStampEngine::PigmentMix(const std::array<float, 3> &A, float MassA, const std::array<float, 3> &B, float MassB) noexcept
{
    const float Mt = MassA + MassB;
    if (Mt <= 1e-6f) return A;
    const float Wa = MassA / Mt, Wb = MassB / Mt;
    return {std::sqrt(A[0] * A[0] * Wa + B[0] * B[0] * Wb),
        std::sqrt(A[1] * A[1] * Wa + B[1] * B[1] * Wb),
        std::sqrt(A[2] * A[2] * Wa + B[2] * B[2] * Wb)};
}

PaintStampEngine::Sampler PaintStampEngine::MakeSampler(float MinU, float MinV, float MaxU, float MaxV) const
{
    Sampler S;
    const int X0 = std::max(0, int(std::floor(MinU * W))), Y0 = std::max(0, int(std::floor(MinV * H)));
    const int X1 = std::min(W, int(std::ceil(MaxU * W))), Y1 = std::min(H, int(std::ceil(MaxV * H)));
    S.X0 = X0; S.Y0 = Y0;
    S.Wd = std::max(1, X1 - X0);
    S.Ht = std::max(1, Y1 - Y0);
    S.Colour.resize(size_t(S.Wd) * S.Ht);
    S.Height.resize(size_t(S.Wd) * S.Ht);
    for (int Y = 0; Y < S.Ht; Y++)
        for (int X = 0; X < S.Wd; X++)
        {
            const int SX = std::min(W - 1, X0 + X), SY = std::min(H - 1, Y0 + Y);
            S.Colour[size_t(Y) * S.Wd + X] = ColourCanvas[size_t(SY) * W + SX];
            S.Height[size_t(Y) * S.Wd + X] = HeightCanvas[size_t(SY) * W + SX];
        }
    return S;
}

std::array<float, 4> PaintStampEngine::Sampler::Sample(const PaintStampEngine &Engine, float U, float V) const noexcept
{
    int Px = int(std::lround(U * Engine.W)) - X0;
    const int Py = int(std::lround(V * Engine.H)) - Y0;
    if (Engine.WrapU)
    {
        if (Px < 0) Px += Engine.W;
        if (Px >= Wd && Px - Engine.W >= 0) Px -= Engine.W;
    }
    if (Px < 0 || Px >= Wd || Py < 0 || Py >= Ht) return {Engine.Base[0], Engine.Base[1], Engine.Base[2], 0.f};
    const size_t Index = size_t(Py) * Wd + Px;
    const auto &C = Colour[Index];
    return {float(C[0]), float(C[1]), float(C[2]), std::max(0.f, (float(Height[Index]) - 128.f) / 127.f)};
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     READBACK
//------------------------------------------------------------------------------------------------------------------------

std::array<float, 4> PaintStampEngine::SampleCanvas(float U, float V) const noexcept
{
    const int X = Clamp(int(U / RangeU * W), 0, W - 1);
    const int Y = Clamp(int(V / RangeV * H), 0, H - 1);
    const size_t Index = size_t(Y) * W + X;
    const auto &C = ColourCanvas[Index];
    return {C[0] / 255.f, C[1] / 255.f, C[2] / 255.f, CoverageCanvas[Index] / 255.f};
}

float PaintStampEngine::SampleHeight(float U, float V) const noexcept
{
    const int X = Clamp(int(U / RangeU * W), 0, W - 1);
    const int Y = Clamp(int(V / RangeV * H), 0, H - 1);
    return std::max(0.f, (float(HeightCanvas[size_t(Y) * W + X]) - 128.f) / 127.f);
}

float PaintStampEngine::WetAt(float U, float V) const noexcept { return FieldAt(WetField, U / RangeU, V / RangeV); }
float PaintStampEngine::DryAt(float U, float V) const noexcept { return FieldAt(DryField, U / RangeU, V / RangeV); }

float PaintStampEngine::TackAt(float U, float V) const noexcept
{
    float X = U - std::floor(U);
    const float Y = Clamp(V, 0.f, 0.999f);
    return TackCanvas[size_t(int(Y * H)) * W + int(X * W)] / 255.f;
}

void PaintStampEngine::EatTack(float U, float V, float Amount) noexcept
{
    const float X = (U - std::floor(U)) * W;
    const float Y = Clamp(V, 0.f, 1.f) * H;
    for (int Dy = -1; Dy <= 1; Dy++)
        for (int Dx = -1; Dx <= 1; Dx++)
            TackPixel(int(X) + Dx, int(Y) + Dy, 0.f, Amount);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     FIELDS STEP + DRIPS
//------------------------------------------------------------------------------------------------------------------------

void PaintStampEngine::StepFields(double DeltaSeconds)
{
    const double Dt = std::min(0.25, DeltaSeconds > 0 ? DeltaSeconds : 0.016);
    const double Tau = P.Medium == PaintMedium::Oil ? 110.0 : P.Medium == PaintMedium::Acrylic ? 16.0 : 7.0;
    const float Dk = float(std::exp(-Dt / Tau));
    for (float &Wv : WetField) if (Wv > 0.002f) Wv *= Dk;
    const float DkS = float(std::exp(-Dt / 2.6));
    for (float &Sv : SolventField) if (Sv > 0.003f) Sv *= DkS;
    StepDrips();
    AutoSettle();
}

void PaintStampEngine::SpawnDrip(const Frame &F, float Ox, float Oy, const std::array<float, 3> &Colour, float Mass, float Speed, float Width, int Delay, bool FromSpray)
{
    if (Drips.size() >= 140) return;
    const auto Uv = UvAt(F, Ox, Oy);
    float Dhx = 0, Dhy = 0, Slope = 0;
    Downhill(F, Dhx, Dhy, Slope);
    Drip D;
    D.U = D.PrevU = Uv[0];
    D.V = D.PrevV = Uv[1];
    D.HasPrev = true;
    D.DirU = F.Dx[0] * Dhx + F.Dy[0] * Dhy; // canvas-UV per world unit, downhill
    D.DirV = F.Dx[1] * Dhx + F.Dy[1] * Dhy;
    D.Slope = Slope;
    D.Ny = F.Normal[1];
    D.Colour = Colour;
    D.Mass = Mass;
    D.Speed = Speed;
    D.Width = Width;
    D.Delay = Delay;
    D.FromSpray = FromSpray;
    Drips.push_back(D);
}

void PaintStampEngine::StepDrips() noexcept
{
    if (Drips.empty()) return;
    const float Visc = P.Visc;
    const float G = 0.012f + (1.f - Visc) * 0.022f + P.Drip * 0.008f;
    for (int I = int(Drips.size()) - 1; I >= 0; I--)
    {
        Drip &D = Drips[size_t(I)];
        D.Age++;
        if (D.Age < D.Delay) continue;
        const float Yield = D.FromSpray ? (0.10f + (1.f - P.SprayRuns) * 0.38f)
                                        : (P.Medium == PaintMedium::Acrylic ? 0.55f : P.Medium == PaintMedium::Oil ? 0.28f : 0.08f);
        const float Drive = D.Mass * D.Slope;
        if (Drive < Yield * (0.7f + Visc)) { Drips.erase(Drips.begin() + I); continue; }
        const float StepWorld = G * D.Speed * D.Mass * DripWorldRadius * std::max(0.f, 1.f - Yield / std::max(Drive, 1e-4f));
        if (StepWorld < 0.00025f) { Drips.erase(Drips.begin() + I); continue; }
        D.PrevU = D.U; D.PrevV = D.V;
        D.U += D.DirU * StepWorld;
        D.V += D.DirV * StepWorld;
        const float Life = std::max(0.08f, D.Mass);
        const float Wpx = D.Width * (P.Medium == PaintMedium::Water ? 1.7f : 1.35f) * (0.55f + Life);
        const float A = std::min(0.9f, P.Alpha * (0.5f + Life * 0.5f));
        UvLine(D.PrevU, D.PrevV, D.U, D.V, [&](float X0, float Y0, float X1, float Y1) noexcept {
            StrokeLinePx(X0, Y0, X1, Y1, std::max(0.8f, Wpx), D.Colour, A, Blend::Over);
        });
        D.Mass *= 0.972f - Visc * 0.012f;
        D.Travel += StepWorld;
        const float MaxRun = (0.08f + D.Mass * 0.35f) * DripWorldRadius *
            (D.FromSpray ? (0.9f + P.SprayRuns * 1.3f)
                         : (P.Medium == PaintMedium::Water ? 2.2f : P.Medium == PaintMedium::Oil ? 1.15f : 0.55f));
        if (D.Mass < 0.08f || D.Travel > MaxRun || D.V < 0.f || D.V > 1.f) Drips.erase(Drips.begin() + I);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     TUFT
//------------------------------------------------------------------------------------------------------------------------

void PaintStampEngine::Dip()
{
    for (auto &Hair : Hairs)
    {
        Hair.Ink = P.Load;
        Hair.Colour = P.Colour;
    }
}

void PaintStampEngine::RebuildTuft()
{
    struct Pt { float Ox, Oy, Len; };
    std::vector<Pt> Pts;
    const int N = std::max(1, std::min(400, P.Hairs));
    if (P.Shape == BrushShape::Flat)
    {
        const int Cols = std::max(4, int(std::lround(std::sqrt(N * 3.2f))));
        const int Rows = std::max(2, int(std::ceil(float(N) / Cols)));
        for (int I = 0; I < N; I++)
        {
            const int C = I % Cols, R = I / Cols;
            Pts.push_back({(float(C) / std::max(1, Cols - 1)) * 2.f - 1.f,
                (float(R) / std::max(1, Rows - 1) - 0.5f) * 0.28f, 0.82f + Rand() * 0.18f});
        }
    }
    else if (P.Shape == BrushShape::Filbert)
    {
        const int Cols = std::max(4, int(std::lround(std::sqrt(N * 2.6f))));
        int I = 0, Guard = 0;
        while (int(Pts.size()) < N && Guard++ < N * 8)
        {
            const int C = I % Cols, R = I / Cols;
            I++;
            const float Ox = (float(C) / std::max(1, Cols - 1)) * 2.f - 1.f;
            const float Oy = R / 6.f - 0.35f;
            if (Ox * Ox + (Oy * 3.4f) * (Oy * 3.4f) > 1.05f) continue;
            Pts.push_back({Ox * (1.f - std::fabs(Ox) * 0.35f), Oy * 0.9f, 0.75f + (1.f - std::fabs(Ox)) * 0.22f});
        }
        while (int(Pts.size()) < N) Pts.push_back({0, 0, 0.8f});
    }
    else if (P.Shape == BrushShape::Fan)
    {
        for (int I = 0; I < N; I++)
        {
            const float T = N == 1 ? 0.5f : float(I) / (N - 1);
            const float Ang = (T - 0.5f) * 1.15f;
            const float Rad = 0.55f + Rand() * 0.45f;
            Pts.push_back({std::sin(Ang) * Rad, (std::cos(Ang) - 0.85f) * Rad * 0.4f, 0.65f + std::fabs(T - 0.5f) * 0.4f});
        }
    }
    else
    {
        for (int I = 0; I < N; I++)
        {
            const float Ring = std::pow((I + 0.5f) / N, 0.72f);
            const float A = I * 2.399963f;
            Pts.push_back({std::cos(A) * Ring, std::sin(A) * Ring, 0.55f + (1.f - Ring) * 0.45f});
        }
    }
    Hairs.clear();
    Hairs.reserve(Pts.size());
    for (const auto &Pt : Pts)
    {
        Hair H;
        H.Ox = Pt.Ox; H.Oy = Pt.Oy; H.Len = Pt.Len;
        H.Ink = P.Load;
        H.Colour = P.Colour;
        H.Seed = Rand();
        Hairs.push_back(H);
    }
    Dip();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     STROKE LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

void PaintStampEngine::BeginStroke(StampTool StrokeTool)
{
    Tool = StrokeTool;
    InStroke = true;
    HaveLast = false;
    FreshTouch = true;
    if (Tool == StampTool::Splat) SplatArmed = false;
    if (Tool == StampTool::Marker) { /* reservoir top-up happens by wick refill between stamps */ }
}

void PaintStampEngine::EndStroke()
{
    if (Tool == StampTool::Splat && SplatArmed) SplatBurst(SplatA, SplatB);
    InStroke = false;
    HaveLast = false;
    BrushZ = 0; // tuft lifts off between strokes (HTML decays it on hover moves)
    SplatArmed = false;
    SandGrains.clear();
    for (auto &Hair : Hairs) Hair.HasPrev = false;
}

void PaintStampEngine::Stamp(const StampHit &Hit, double TimeSeconds)
{
    if (!InStroke) BeginStroke(Tool);
    {
        const Frame Probe = MakeFrame(Hit);
        LastUvPerWorld = 0.5f * (std::hypot(Probe.Dx[0], Probe.Dx[1]) + std::hypot(Probe.Dy[0], Probe.Dy[1]));
        if (!HaveLast) { LastU = Probe.U; LastV = Probe.V; }
    }
    switch (Tool)
    {
    case StampTool::PaintBrush: StampPaintBrush(Hit, TimeSeconds); break;
    case StampTool::Spray: StampSpray(Hit, TimeSeconds); break;
    case StampTool::Dry: StampDry(Hit, TimeSeconds); break;
    case StampTool::Pencil: StampPencil(Hit, TimeSeconds); break;
    case StampTool::Marker: StampMarker(Hit, TimeSeconds); break;
    case StampTool::Sand: StampSand(Hit, TimeSeconds); break;
    case StampTool::Splat:
        if (!SplatArmed) { SplatA = Hit; SplatArmed = true; }
        SplatB = Hit;
        break;
    case StampTool::Dust: StampDust(Hit, TimeSeconds); break;
    case StampTool::Soot: StampSoot(Hit, TimeSeconds); break;
    case StampTool::Eraser: StampEraser(Hit, TimeSeconds); break;
    case StampTool::Digital: StampDigital(Hit, TimeSeconds); break;
    }
    for (int i = 0; i < 3; i++) LastPoint[i] = Hit.Point[i];
    {
        const Frame Probe = MakeFrame(Hit);
        LastU = Probe.U;
        LastV = Probe.V;
    }
    HaveLast = true;
    LastTime = TimeSeconds;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SPHERE ADAPTER (HTML parity)
//------------------------------------------------------------------------------------------------------------------------

void PaintStampEngine::SpherePointFromUv(float U, float V, float Radius, float Out[3]) noexcept
{
    const float Phi = U * 2.f * Pi, Theta = V * Pi;
    const float St = std::sin(Theta);
    Out[0] = -std::cos(Phi) * St * Radius;
    Out[1] = std::cos(Theta) * Radius;
    Out[2] = std::sin(Phi) * St * Radius;
}

StampHit PaintStampEngine::MakeSphereHit(const float Point[3], float Radius, float Pressure)
{
    StampHit Hit;
    for (int i = 0; i < 3; i++) { Hit.Point[i] = Point[i]; Hit.Normal[i] = Point[i]; }
    Normalize3(Hit.Normal);
    const float Up[3]{0, 1, 0};
    Cross3(Up, Hit.Normal, Hit.AxisX);
    if (Len3(Hit.AxisX) < 1e-6f) { Hit.AxisX[0] = 1; Hit.AxisX[1] = 0; Hit.AxisX[2] = 0; }
    Normalize3(Hit.AxisX);
    Cross3(Hit.Normal, Hit.AxisX, Hit.AxisY);
    Normalize3(Hit.AxisY);
    for (int i = 0; i < 3; i++) Hit.ViewDir[i] = Hit.Normal[i]; // head-on unless the caller overrides
    const auto Uv = SphereUv(Point, Radius);
    Hit.U = Uv[0];
    Hit.V = Uv[1];
    // UV Jacobian by polar probing of the exact equirect mapping
    const float Eps = 0.001f;
    auto Probe = [&](const float Axis[3], float Out[2]) noexcept {
        float Q[3]{Point[0] + Axis[0] * Eps, Point[1] + Axis[1] * Eps, Point[2] + Axis[2] * Eps};
        Normalize3(Q);
        Q[0] *= Radius; Q[1] *= Radius; Q[2] *= Radius;
        const auto UvQ = SphereUv(Q, Radius);
        float Du = UvQ[0] - Uv[0];
        if (Du > 0.5f) Du -= 1;
        if (Du < -0.5f) Du += 1;
        Out[0] = Du / Eps;
        Out[1] = (UvQ[1] - Uv[1]) / Eps;
    };
    Probe(Hit.AxisX, Hit.DUvDx);
    Probe(Hit.AxisY, Hit.DUvDy);
    Hit.Pressure = Pressure;
    return Hit;
}

} // namespace Frontier
