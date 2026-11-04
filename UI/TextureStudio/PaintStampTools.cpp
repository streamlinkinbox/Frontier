/*============================================================================================================================================
                                                  PAINTSTAMPTOOLS.CPP
============================================================================================================================================*/
// 🖌️ The eleven stamp instruments, ported 1:1 from HarborPaintWorkshop.html (builder Stages C-J).
//    Translation rules, applied throughout: HTML's tmp.copy(p).addScaledVector(tan,ox).addScaledVector(bit,oy)
//    ->normalize()*R -> worldToUV chain becomes UvAt(frame, ox, oy) over the hit's UV Jacobian; pctx arcs
//    become FillCirclePx/FillEllipsePx in canvas px; HTML px constants scale by Cps (= canvas px / 1024);
//    field-disc radii written in world units convert through UvPerWorld x3 (the HTML field fudge factor).

#include "UI/TextureStudio/PaintStampEngine.h"
#include <algorithm>
#include <cmath>

namespace Frontier
{
namespace
{
constexpr float Pi = 3.14159265358979323846f;
template <typename T> T Clamp(T V, T Lo, T Hi) noexcept { return V < Lo ? Lo : (V > Hi ? Hi : V); }
float Dist3(const float A[3], const float B[3]) noexcept
{
    return std::hypot(A[0] - B[0], A[1] - B[1], A[2] - B[2]);
}
float Dot3(const float A[3], const float B[3]) noexcept { return A[0] * B[0] + A[1] * B[1] + A[2] * B[2]; }
void Cross3(const float A[3], const float B[3], float Out[3]) noexcept
{
    Out[0] = A[1] * B[2] - A[2] * B[1];
    Out[1] = A[2] * B[0] - A[0] * B[2];
    Out[2] = A[0] * B[1] - A[1] * B[0];
}
void Normalize3(float A[3]) noexcept
{
    const float L = std::sqrt(Dot3(A, A));
    if (L > 1e-12f) { A[0] /= L; A[1] /= L; A[2] /= L; }
}
} // namespace

// ── shared stamp prologue pieces (member-side helpers via macros-free lambdas in each stamp) ──────
#define STAMP_PROLOGUE()                                                                                             \
    const bool HadLast = HaveLast;                                                                                   \
    const float Moved = HadLast ? Dist3(Hit.Point, LastPoint) : 0.f;                                                 \
    Frame F = MakeFrame(Hit);                                                                                        \
    if (HadLast) RotateFrameToMotion(F, LastPoint);                                                                  \
    const float FromU = HadLast ? LastU : F.U, FromV = HadLast ? LastV : F.V;                                        \
    const float Cps = 0.5f * (W + H) / 1024.f;                                                                       \
    const float Upw = 0.5f * (std::hypot(F.Dx[0], F.Dx[1]) + std::hypot(F.Dy[0], F.Dy[1]));                          \
    (void)TimeSeconds

//------------------------------------------------------------------------------------------------------------------------
//                                                     PAINT BRUSH (tuft transfer)
//------------------------------------------------------------------------------------------------------------------------

void PaintStampEngine::DepositHair(Hair &Hr, float U, float V, const Sampler &S, float Press, float Speed, float Reach) noexcept
{
    const float PxScale = Clamp(LastUvPerWorld * std::sqrt(float(W) * H) / 226.4f, 0.2f, 5.f);
    if (!Hr.HasPrev) { Hr.PrevU = U; Hr.PrevV = V; Hr.HasPrev = true; }
    const float Visc = P.Visc;
    float Du = U - Hr.PrevU;
    if (WrapU) { if (Du > 0.5f) Du -= 1; if (Du < -0.5f) Du += 1; }
    const float Dv = V - Hr.PrevV;
    const float DistPx = std::hypot(Du * W, Dv * H);
    if (DistPx > W * 0.25f) { Hr.PrevU = U; Hr.PrevV = V; return; }

    const float Contact = Clamp((Hr.Len - Reach) * 5.f, 0.f, 1.f);
    const auto Under = S.Sample(*this, U, V);
    const float ThickNow = Under[3];
    const float Wet = FieldAt(WetField, U, V);

    const float Flow = (1.25f - Visc * 0.78f) / (1.f + Speed * 1.7f);
    float Dep = Contact * Flow * (0.22f + Hr.Ink * 1.0f);
    if (Dep > 1) Dep = 1;

    const float Pick = Wet * ThickNow * Contact * (0.9f - Visc * 0.45f) * (1.f - Dep * 0.55f);
    if (Pick > 0.004f)
    {
        Hr.Colour = PigmentMix(Hr.Colour, 0.25f + Hr.Ink, {Under[0], Under[1], Under[2]}, Pick * 1.6f);
        Hr.Ink = std::min(1.15f, Hr.Ink + Pick * 0.38f);
    }

    if (Contact > 0.02f && Dep > 0.015f)
    {
        const float Tooth = ToothAt(U, V);
        const float Micro = Tooth * (P.Medium == PaintMedium::Water ? 0.75f : 0.55f) + ThickNow * 0.85f;
        const float Dryness = Clamp((0.30f - Dep) / 0.30f, 0.f, 1.f);
        const float Valley = Clamp((0.62f - Micro) * 2.4f, 0.f, 1.f);
        const float Gate = 1.f - Dryness * Valley;
        float JitterFrac = Hr.Seed * 7.13f + DistPx * 0.017f / std::max(0.2f, PxScale);
        JitterFrac -= std::floor(JitterFrac);
        const float Jitter = 0.72f + 0.5f * JitterFrac;
        const float Cover = std::min(1.f, Gate * Jitter + Dep * 0.3f);
        if (Cover > 0.02f)
        {
            const float Wpx = std::max(0.85f, P.Width * 2.3f * Hr.Len * (0.55f + Press * 0.8f)) * (0.72f + Dep * 0.5f) * PxScale;
            const float A = std::min(1.f, P.Alpha * (0.3f + Dep * 0.95f)) * Cover;
            UvLine(Hr.PrevU, Hr.PrevV, U, V, [&](float X0, float Y0, float X1, float Y1) noexcept {
                StrokeLinePx(X0, Y0, X1, Y1, Wpx, Hr.Colour, A, Blend::Over);
                FillCirclePx(X1, Y1, Wpx * 0.4f, Hr.Colour, A, Blend::Over);
            });
            const float Watery = 1.f - Visc;
            if (Watery > 0.4f)
            {
                UvLine(Hr.PrevU, Hr.PrevV, U, V, [&](float X0, float Y0, float X1, float Y1) noexcept {
                    StrokeLinePx(X0, Y0, X1, Y1, Wpx * (1.9f + P.Bloom * 5.f), Hr.Colour, A * Watery * 0.22f, Blend::Over);
                });
            }
            const float Dh = std::min(0.85f, Dep * P.Thick * (0.35f + Visc * 0.55f)) * Cover;
            if (Dh > 0.004f)
            {
                UvLine(Hr.PrevU, Hr.PrevV, U, V, [&](float X0, float Y0, float X1, float Y1) noexcept {
                    HeightLinePx(X0, Y0, X1, Y1, std::max(1.1f, Wpx * 0.88f), 255.f, Dh);
                });
            }
            WetSegment(Hr.PrevU, Hr.PrevV, U, V, std::min(1.f, 0.45f + Dep * 0.6f));
            Hr.Ink -= Dep * (DistPx / std::max(0.2f, PxScale)) / (250.f + P.Load * 750.f) * (0.6f + (1.f - Visc) * 0.75f);
            if (Hr.Ink < 0) Hr.Ink = 0;
        }
    }
    const float Scrape = Contact * ThickNow * Wet * (1.f - Dep) * (P.Medium == PaintMedium::Water ? 0.3f : 1.f);
    if (Scrape > 0.07f && DistPx > 0.4f)
    {
        const float Wpx = std::max(0.9f, P.Width * 2.3f * Hr.Len * (0.55f + Press * 0.8f)) * PxScale;
        UvLine(Hr.PrevU, Hr.PrevV, U, V, [&](float X0, float Y0, float X1, float Y1) noexcept {
            HeightLinePx(X0, Y0, X1, Y1, std::max(1.f, Wpx * 0.8f), 30.f, Scrape * 0.32f);
        });
        UvLine(Hr.PrevU, Hr.PrevV, U, V, [&](float X0, float Y0, float X1, float Y1) noexcept {
            const float Nx = Y1 - Y0, Ny = X0 - X1;
            const float L = std::hypot(Nx, Ny);
            if (L <= 0) return;
            const float Ogx = (Nx / L) * Wpx * 0.62f, Ogy = (Ny / L) * Wpx * 0.62f;
            HeightLinePx(X0 + Ogx, Y0 + Ogy, X1 + Ogx, Y1 + Ogy, std::max(1.f, Wpx * 0.5f), 255.f, Scrape * 0.26f);
            HeightLinePx(X0 - Ogx, Y0 - Ogy, X1 - Ogx, Y1 - Ogy, std::max(1.f, Wpx * 0.5f), 255.f, Scrape * 0.26f);
        });
    }
    Hr.PrevU = U;
    Hr.PrevV = V;
}

void PaintStampEngine::StampPaintBrush(const StampHit &Hit, double TimeSeconds)
{
    const bool HadLast = HaveLast;
    const float Moved = HadLast ? Dist3(Hit.Point, LastPoint) : 0.f;
    const double DtRaw = HadLast ? (TimeSeconds - LastTime) : 0.016;
    const float Dt = Clamp(float(DtRaw), 0.008f, 0.05f);
    Frame F = MakeFrame(Hit); // base frame for the yaw chase (not velocity-rotated)
    const float PressRaw = std::min(1.f, F.Pressure * P.Press * 1.15f);
    BrushZ += (1.f - BrushZ) * std::min(1.f, Dt * (5.f + P.Stiff * 11.f));
    const float Press = PressRaw * (0.12f + 0.88f * BrushZ);

    float Speed = 0;
    if (HadLast)
    {
        Speed = Moved / std::max(Dt, 0.008f);
        const float Move[3]{Hit.Point[0] - LastPoint[0], Hit.Point[1] - LastPoint[1], Hit.Point[2] - LastPoint[2]};
        const float Along = Dot3(Move, F.AxisX), Across = Dot3(Move, F.AxisY);
        if (Along * Along + Across * Across > 1e-8f)
        {
            const float Target = std::atan2(Across, Along);
            if (!HaveYaw) { BrushYaw = Target; HaveYaw = true; }
            else
            {
                float D = Target - BrushYaw;
                while (D > Pi) D -= 2 * Pi;
                while (D < -Pi) D += 2 * Pi;
                BrushYaw += D * std::min(1.f, Dt * (2.2f + P.Stiff * 8.f));
            }
        }
    }
    if (!HaveYaw) { BrushYaw = 0; HaveYaw = true; }
    {   // rotate the frame by yaw: AxisX = brush forward, AxisY = across the tuft
        const float Cy = std::cos(BrushYaw), Sy = std::sin(BrushYaw);
        float Nx[3], Ny[3];
        for (int i = 0; i < 3; i++) { Nx[i] = F.AxisX[i] * Cy + F.AxisY[i] * Sy; Ny[i] = -F.AxisX[i] * Sy + F.AxisY[i] * Cy; }
        const float Dx[2]{F.Dx[0] * Cy + F.Dy[0] * Sy, F.Dx[1] * Cy + F.Dy[1] * Sy};
        const float Dy[2]{-F.Dx[0] * Sy + F.Dy[0] * Cy, -F.Dx[1] * Sy + F.Dy[1] * Cy};
        for (int i = 0; i < 3; i++) { F.AxisX[i] = Nx[i]; F.AxisY[i] = Ny[i]; }
        F.Dx[0] = Dx[0]; F.Dx[1] = Dx[1]; F.Dy[0] = Dy[0]; F.Dy[1] = Dy[1];
    }

    // footprint from real tuft proportions (HTML Stage C7): radius = half-width at the ferrule
    float SplayW, SplayL;
    if (P.Shape == BrushShape::Flat) { SplayW = P.Radius * (0.92f + Press * 0.22f); SplayL = P.Radius * (1.05f + Press * 1.5f); }
    else if (P.Shape == BrushShape::Filbert) { SplayW = P.Radius * (0.85f + Press * 0.3f); SplayL = P.Radius * (0.55f + Press * 0.85f); }
    else if (P.Shape == BrushShape::Fan) { SplayW = P.Radius * (0.8f + Press * 0.45f); SplayL = P.Radius * (0.35f + Press * 0.5f); }
    else { SplayW = P.Radius * (0.3f + Press * 0.85f); SplayL = SplayW; }
    SplayW *= P.Splay;
    SplayL *= P.Splay;
    const float FollowBase = 0.16f + P.Stiff * 0.72f;
    const float Reach = 1.05f - Press * 0.95f;

    if (Hairs.empty()) RebuildTuft();
    float MinU = 1, MinV = 1, MaxU = 0, MaxV = 0;
    for (auto &Hair : Hairs)
    {
        const auto Target = UvAt(F, Hair.Oy * SplayL, Hair.Ox * SplayW);
        if (!Hair.HasTip || FreshTouch) { Hair.TipU = Target[0]; Hair.TipV = Target[1]; Hair.HasTip = true; }
        else
        {
            const float Follow = FollowBase * (1.25f - Hair.Len * 0.55f);
            float Du = Target[0] - Hair.TipU;
            if (WrapU) { if (Du > 0.5f) Du -= 1; if (Du < -0.5f) Du += 1; }
            Hair.TipU += Du * Follow;
            Hair.TipV += (Target[1] - Hair.TipV) * Follow;
        }
        MinU = std::min(MinU, Hair.TipU); MaxU = std::max(MaxU, Hair.TipU);
        MinV = std::min(MinV, Hair.TipV); MaxV = std::max(MaxV, Hair.TipV);
        if (Hair.HasPrev)
        {
            MinU = std::min(MinU, Hair.PrevU); MaxU = std::max(MaxU, Hair.PrevU);
            MinV = std::min(MinV, Hair.PrevV); MaxV = std::max(MaxV, Hair.PrevV);
        }
    }
    FreshTouch = false;
    if (MaxU - MinU > 0.5f) { MinU = F.U - 0.09f; MaxU = F.U + 0.09f; }
    const float Pad = 10.f / W;
    const Sampler S = MakeSampler(MinU - Pad, MinV - Pad, MaxU + Pad, MaxV + Pad);
    for (auto &Hair : Hairs) DepositHair(Hair, Hair.TipU, Hair.TipV, S, Press, Speed, Reach);

    // earned drips only: sampled film over yield, on a slope, at most one per event
    const float NyS = F.Normal[1];
    const float Slope = std::sqrt(std::max(0.f, 1.f - NyS * NyS));
    if (Slope > 0.55f && Drips.size() < 90 && P.Drip > 0.02f && !Hairs.empty())
    {
        const float YieldT = 0.55f - (1.f - P.Visc) * 0.3f;
        for (size_t T = 0; T < Hairs.size(); T++)
        {
            const Hair &Hr = Hairs[size_t(Rand() * Hairs.size()) % Hairs.size()];
            if (Hr.Ink < 0.15f) continue;
            const float UnderT = S.Sample(*this, Hr.TipU, Hr.TipV)[3];
            const float Excess = UnderT - YieldT + Hr.Ink * 0.1f;
            if (Excess <= 0 || Rand() > Excess * P.Drip * 0.5f) continue;
            const int Delay = P.Medium == PaintMedium::Water ? int(Rand() * 6)
                : P.Medium == PaintMedium::Oil ? 4 + int(Rand() * 16) : 10 + int(Rand() * 28);
            SpawnDrip(F, Hr.Oy * SplayL, Hr.Ox * SplayW, Hr.Colour, 0.3f + Excess * 1.4f,
                0.45f + Rand() * 1.7f, 0.4f + Rand() * 2.0f, Delay, false);
            break;
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SPRAY (aerosol)
//------------------------------------------------------------------------------------------------------------------------

void PaintStampEngine::StampSpray(const StampHit &Hit, double TimeSeconds)
{
    Frame F = MakeFrame(Hit);
    const float Cps = 0.5f * (W + H) / 1024.f;
    const float Upw = 0.5f * (std::hypot(F.Dx[0], F.Dx[1]) + std::hypot(F.Dy[0], F.Dy[1]));
    const double Idle = HaveSprayClock ? (TimeSeconds - SprayClock) : 1.0;
    if (Idle > 0.09) { SprayEnv = 0; CanHeat = std::min(1.f, CanHeat + float(Idle) * 0.3f); }
    SprayClock = TimeSeconds;
    HaveSprayClock = true;
    const float Dt = Clamp(float(Idle), 0.004f, 0.05f);
    SprayEnv = std::min(1.f, SprayEnv + Dt * 11.f);
    CanHeat = std::max(0.55f, CanHeat - Dt * 0.085f);

    const bool Skinny = P.Nozzle == SprayNozzle::Skinny;
    const float Cone = (0.018f + P.Cone * 0.07f) * (0.45f + P.Standoff * 1.35f) * (Skinny ? 0.55f : 1.25f);
    const float Dist = P.Standoff;
    const float CosAng = std::max(0.15f, Dot3(Hit.ViewDir, F.Normal));
    float Vt[3]{Hit.ViewDir[0] - F.Normal[0] * CosAng, Hit.ViewDir[1] - F.Normal[1] * CosAng, Hit.ViewDir[2] - F.Normal[2] * CosAng};
    const float VtL = std::max(1e-6f, std::sqrt(Dot3(Vt, Vt)));
    const float VtDotT = Dot3(F.AxisX, Vt) / VtL, VtDotB = Dot3(F.AxisY, Vt) / VtL;
    const float Elong = std::min(2.3f, 1.f / std::pow(CosAng, 0.7f));
    const float Stick = 0.35f + 0.65f * CosAng;
    const float DryFraction = Clamp(Dist * 0.8f - (CanHeat - 0.55f) * 0.25f, 0.f, 0.75f);
    const float Transfer = (1.f / (1.f + Dist * 1.15f)) * Stick;
    const float Press = std::min(1.f, F.Pressure + 0.15f);
    const float Rate = 2400.f * P.SprayPressure * Press * SprayEnv * (Skinny ? 0.8f : 1.3f) * (0.55f + CanHeat * 0.55f);
    SprayAcc += Rate * Dt;
    int NDrops = int(std::floor(SprayAcc));
    SprayAcc -= NDrops;
    if (NDrops > 420) NDrops = 420;
    const float PSpit = (1.f - SprayEnv) * 0.07f + (1.f - CanHeat) * 0.06f + (Skinny ? 0.004f : 0.018f);
    const float SizeBase = Skinny ? 0.75f : 1.25f;
    const float ADrop = 0.17f * Transfer * (1.f - DryFraction * 0.45f);

    for (int I = 0; I < NDrops; I++)
    {
        const bool Spit = Rand() < PSpit;
        const bool Drift = !Spit && Rand() < 0.06f + Dist * 0.1f;
        const float U1 = std::max(1e-6f, Rand());
        const float U2 = Rand() * 2.f * Pi;
        const float GaussMag = std::sqrt(-2.f * std::log(U1));
        float Rad = Cone * GaussMag * (Skinny ? 0.34f : 0.42f);
        if (Spit) Rad *= 1.3f + Rand() * 1.7f;
        if (Drift) Rad *= 1.7f + Rand() * 1.4f;
        float Ox = std::cos(U2) * Rad, Oy = std::sin(U2) * Rad;
        const float AlongV = Ox * VtDotT + Oy * VtDotB;
        Ox += VtDotT * AlongV * (Elong - 1.f);
        Oy += VtDotB * AlongV * (Elong - 1.f);
        const auto Uv = UvAt(F, Ox, Oy);
        float Sz = SizeBase * std::exp((Rand() + Rand() - 1.f) * 0.7f) * (1.f - DryFraction * 0.3f);
        if (Spit) Sz *= 2.6f;
        if (Drift) Sz *= 0.8f;
        const float A = std::min(0.8f, ADrop * (Spit ? 2.2f : Drift ? 0.22f : 1.f) * (0.6f + Rand() * 0.7f));
        FillCirclePx(Uv[0] * W, Uv[1] * H, std::max(0.3f, Sz * Cps), P.SprayColour, A, Blend::Over);
        if ((I & 3) == 0 && !Drift)
            HeightCirclePx(Uv[0] * W, Uv[1] * H, std::max(0.3f, Sz * 0.9f * Cps), 255.f, 0.05f + DryFraction * 0.1f);
    }

    WetDisc(F.U, F.V, Cone * 0.3f * Upw * 3.f, std::min(1.f, (0.5f + P.SprayPressure * 0.5f) * (1.f - DryFraction * 0.8f)) * SprayEnv);

    const float Ny = F.Normal[1];
    const float Slope = std::sqrt(std::max(0.f, 1.f - Ny * Ny));
    if (Slope > 0.5f && Drips.size() < 90 && P.SprayRuns > 0.02f)
    {
        const float Pad = Cone * 0.7f * Upw + 0.002f;
        const Sampler S = MakeSampler(F.U - Pad, F.V - Pad * 1.7f, F.U + Pad, F.V + Pad * 1.7f);
        const float YieldS = 0.34f + (1.f - P.SprayRuns) * 0.3f;
        for (int T = 0; T < 5; T++)
        {
            const float Ang = Rand() * 2.f * Pi;
            const float Rad = Cone * std::sqrt(Rand()) * 0.55f;
            const float Ox = std::cos(Ang) * Rad, Oy = std::sin(Ang) * Rad;
            const auto Uv = UvAt(F, Ox, Oy);
            const float Film = S.Sample(*this, Uv[0], Uv[1])[3];
            if (Film > YieldS && Rand() < (Film - YieldS) * P.SprayRuns * 1.6f)
            {
                SpawnDrip(F, Ox, Oy, P.SprayColour, 0.35f + (Film - YieldS) * 1.6f,
                    0.4f + Rand() * 1.1f, 0.7f + Rand() * 1.4f, int(Rand() * 8), true);
                break;
            }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     DRY MEDIA + PENCIL
//------------------------------------------------------------------------------------------------------------------------

namespace
{
struct DryKindParams { float Grain, Shed, Bind, Dust, Smudge; };
DryKindParams DryParamsFor(DryKind Kind) noexcept
{
    switch (Kind)
    {
    case DryKind::Chalk: return {0.85f, 0.55f, 0.35f, 0.55f, 0.50f};
    case DryKind::Conte: return {0.65f, 0.60f, 0.70f, 0.18f, 0.40f};
    case DryKind::Charcoal: return {1.55f, 1.00f, 0.15f, 0.85f, 0.90f};
    default: return {1.05f, 0.90f, 0.55f, 0.30f, 0.85f}; // pastel
    }
}
} // namespace

void PaintStampEngine::StampDry(const StampHit &Hit, double TimeSeconds)
{
    STAMP_PROLOGUE();
    const float Press = std::min(1.f, F.Pressure * P.DryPress);
    const DryKindParams K = DryParamsFor(P.Dry);
    const bool Square = P.Dry == DryKind::Pastel || P.Dry == DryKind::Conte;
    const float Hard = P.DryHardness;
    const float Rx = P.DrySize * (0.5f + Press * 0.55f);
    const float Ry = Square ? Rx * (0.85f + P.DrySide * 0.4f) : P.DrySize * (0.16f + P.DrySide * 2.4f);
    const int Steps = std::max(1, int(std::ceil(std::max(Moved, 0.002f) / 0.003f)));
    const float Pad = (Rx + Ry) * Upw * 1.5f + 0.004f;
    const Sampler S = MakeSampler(std::min(FromU, F.U) - Pad, std::min(FromV, F.V) - Pad * 1.6f,
        std::max(FromU, F.U) + Pad, std::max(FromV, F.V) + Pad * 1.6f);
    float Dhx = 0, Dhy = 0, Slope = 0;
    Downhill(F, Dhx, Dhy, Slope);
    (void)Slope;
    const int NG = Square ? 40 : 34;
    const float BaseA = (P.Dry == DryKind::Charcoal ? 0.5f + Press * 0.3f : P.Dry == DryKind::Chalk ? 0.42f + Press * 0.28f : 0.52f + Press * 0.25f) *
        (0.45f + K.Shed * 0.6f) * (1.1f - Hard * 0.45f);
    auto Lay = [&](float U0, float V0) {
        Frame G = F; G.U = U0; G.V = V0;
        const float CapHere = FieldAt(DryField, U0, V0);
        if (CapHere > 0.22f && Moved > 0.001f)
        {
            const auto Here = S.Sample(*this, U0, V0);
            const float SmA = std::min(0.5f, K.Smudge * CapHere * Press * 0.45f);
            const std::array<float, 3> Col{Here[0], Here[1], Here[2]};
            for (int Q = 1; Q <= 3; Q++)
            {
                const auto Uvq = UvAt(G, Rx * 0.45f * Q, 0.f);
                FillCirclePx(Uvq[0] * W, Uvq[1] * H, std::max(0.8f, Ry * 1024.f * Cps * 0.16f * (1.f - Q * 0.18f)), Col, SmA, Blend::Over);
            }
            DryDisc(U0, V0, Ry * 0.30f * Upw * 3.f, -0.05f * K.Smudge);
        }
        float Deposited = 0;
        for (int I = 0; I < NG; I++)
        {
            float Ox, Oy;
            if (Square) { Ox = (Rand() * 2.f - 1.f) * Rx; Oy = (Rand() * 2.f - 1.f) * Ry; }
            else
            {
                const float A2 = Rand() * 2.f * Pi, R2 = std::sqrt(Rand());
                Ox = std::cos(A2) * R2 * Rx; Oy = std::sin(A2) * R2 * Ry;
            }
            const auto Uv = UvAt(G, Ox, Oy);
            const float Film = S.Sample(*this, Uv[0], Uv[1])[3];
            const float Ridge = std::min(1.f, ToothRidge(Uv[0], Uv[1]) * P.DryTooth * 1.25f + Film * 0.5f);
            const float CatchP = Clamp((Ridge - (0.78f - Press * 0.66f - (1.f - Hard) * 0.12f)) * 3.4f, 0.03f, 1.f);
            if (Rand() > CatchP) continue;
            const float Cap = FieldAt(DryField, Uv[0], Uv[1]);
            const float Eff = std::max(0.07f, 1.12f - Cap * (1.35f - K.Bind * 0.6f));
            std::array<float, 3> Col = P.DryColour;
            if (P.Dry == DryKind::Charcoal) { const float Kv = 16.f + (1.f - Ridge) * 30.f + Rand() * 8.f; Col = {Kv, Kv, Kv}; }
            else { const float J = 0.9f + Ridge * 0.18f; Col = {Col[0] * J, Col[1] * J, Col[2] * J}; }
            const float A = std::min(0.82f, BaseA * Eff * (0.5f + Ridge * 0.6f));
            const float Gsz = K.Grain * (0.55f + Press * 0.7f) * (0.6f + Rand() * 0.7f) * Cps;
            FillCirclePx(Uv[0] * W, Uv[1] * H, std::max(0.25f, Gsz), Col, A, Blend::Over);
            Deposited += A;
        }
        DryDisc(U0, V0, (Rx + Ry) * 0.16f * Upw * 3.f, std::min(0.2f, Deposited / NG * 0.55f));
        HeightCirclePx(U0 * W, V0 * H, std::max(1.f, Ry * 1024.f * Cps * 0.13f), 255.f, 0.018f * Press * K.Shed);
        const int NDust = int(K.Dust * P.DryDust * 9.f * (0.4f + Press));
        for (int I = 0; I < NDust; I++)
        {
            const float Drift = Ry * (0.6f + Rand() * 2.2f);
            const float Ox = (Rand() * 2.f - 1.f) * Rx + Dhx * Drift;
            const float Oy = (Rand() * 2.f - 1.f) * Ry + Dhy * Drift;
            const auto Uv = UvAt(G, Ox, Oy);
            const std::array<float, 3> Col = P.Dry == DryKind::Charcoal ? std::array<float, 3>{30.f, 30.f, 30.f} : P.DryColour;
            FillCirclePx(Uv[0] * W, Uv[1] * H, (0.4f + Rand() * 0.6f) * Cps, Col, 0.10f + Rand() * 0.14f, Blend::Over);
        }
    };
    for (int S = 1; S <= Steps; S++)
    {
        const float T = float(S) / Steps;
        Lay(FromU + (F.U - FromU) * T, FromV + (F.V - FromV) * T);
    }
}

void PaintStampEngine::StampPencil(const StampHit &Hit, double TimeSeconds)
{
    STAMP_PROLOGUE();
    if (TimeSeconds - PenClock > 1.2) PenWear = 0; // re-pointed during the pause
    PenClock = TimeSeconds;
    const float Press = std::min(1.f, F.Pressure * P.PencilPress);
    const float Soft = P.PencilGrade;
    PenWear = std::min(1.6f, PenWear + Moved * (1.1f + Press * 1.6f) * (1.25f - Soft * 0.45f));
    const float Blunt = 1.f + PenWear * 0.85f;
    const float Rx = P.PencilSize * (0.45f + Press * 0.4f) * (0.7f + Soft * 0.7f) * Blunt;
    const float Ry = P.PencilSize * (0.1f + P.PencilSide * 2.8f) * (0.8f + Soft * 0.5f) * (1.f + PenWear * 0.4f);
    const float MovedEff = Moved < 0.0008f ? 0.003f : Moved;
    const int Steps = std::max(1, int(std::ceil(std::max(MovedEff, 0.002f) / 0.0025f)));
    const float Pad = (Rx + Ry) * Upw * 1.6f + 0.004f;
    const Sampler S = MakeSampler(std::min(FromU, F.U) - Pad, std::min(FromV, F.V) - Pad * 1.6f,
        std::max(FromU, F.U) + Pad, std::max(FromV, F.V) + Pad * 1.6f);
    const float Dark = std::round(26.f + std::pow(1.f - Soft, 1.35f) * 118.f);
    const int NG = int(16.f + Soft * 26.f);
    const float AMul = (0.2f + Press * 0.42f * (0.35f + Soft)) / Blunt;
    const float Burnish = std::max(0.f, (Press - 0.68f) / 0.32f) * (0.25f + Soft * 0.75f);
    auto Lay = [&](float U0, float V0) {
        Frame G = F; G.U = U0; G.V = V0;
        float Dep = 0;
        for (int I = 0; I < NG; I++)
        {
            const float A2 = Rand() * 2.f * Pi, R2 = std::sqrt(Rand());
            const float Ox = std::cos(A2) * R2 * Rx, Oy = std::sin(A2) * R2 * Ry;
            const auto Uv = UvAt(G, Ox, Oy);
            const float Film = S.Sample(*this, Uv[0], Uv[1])[3];
            const float Ridge = std::min(1.f, ToothRidge(Uv[0], Uv[1]) * 1.2f + Film * 0.45f);
            const float CatchP = Clamp((Ridge - (0.82f - Press * 0.62f - Soft * 0.22f)) * 3.6f, 0.04f, 1.f);
            if (Rand() > CatchP) continue;
            const float Cap = FieldAt(DryField, Uv[0], Uv[1]);
            const float Eff = std::max(0.1f, 1.1f - Cap * 0.9f);
            const float Centre = 1.f - std::fabs(Oy) / (Ry + 1e-6f);
            const float Shine = P.PencilShine * Burnish * Centre;
            const float Kv = std::round(Dark * (1.f - Shine * 0.5f) + Shine * 130.f);
            const float A = std::min(0.72f, AMul * Eff * (0.5f + Ridge * 0.65f));
            FillCirclePx(Uv[0] * W, Uv[1] * H, std::max(0.25f, (0.35f + Press * 0.5f + Soft * 0.4f) * (0.8f + PenWear * 0.5f) * Cps),
                {Kv, Kv + 1.f, Kv + 5.f}, A, Blend::Over);
            Dep += A;
        }
        DryDisc(U0, V0, (Rx + Ry) * 0.18f * Upw * 3.f, std::min(0.16f, Dep / NG * (0.25f + Soft * 0.5f)));
        if (Burnish > 0.05f)
            HeightCirclePx(U0 * W, V0 * H, std::max(0.8f, Ry * 1024.f * Cps * 0.10f), 104.f, Burnish * 0.10f);
    };
    for (int S = 1; S <= Steps; S++)
    {
        const float T = float(S) / Steps;
        Lay(FromU + (F.U - FromU) * T, FromV + (F.V - FromV) * T);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     MARKER (alcohol dye)
//------------------------------------------------------------------------------------------------------------------------

void PaintStampEngine::StampMarker(const StampHit &Hit, double TimeSeconds)
{
    STAMP_PROLOGUE();
    const float Press = std::min(1.f, F.Pressure * P.MarkerPress);
    const bool Fresh = !HadLast;
    if (Fresh) { MarkSeed = Rand() * 40.f; P.MarkerInk = P.MarkerLoad; } // HTML pointerdown re-wicks the nib
    float Rx, Ry;
    bool Square;
    if (P.Nib == MarkerNib::Chisel) { Rx = P.MarkerSize * (0.22f + Press * 0.12f); Ry = P.MarkerSize * (1.05f + Press * 0.35f); Square = true; }
    else if (P.Nib == MarkerNib::Brush) { Rx = P.MarkerSize * (0.28f + Press * 0.45f); Ry = P.MarkerSize * (0.85f + Press * 0.55f); Square = false; }
    else { Rx = P.MarkerSize * (0.62f + Press * 0.22f); Ry = Rx * 0.95f; Square = false; }
    Frame M = F; // nib frame: F axes rotated by the nib angle
    {
        const float Ang = P.MarkerAngle * Pi * 0.5f;
        const float Ca = std::cos(Ang), Sa = std::sin(Ang);
        float T2[3], B2[3];
        for (int i = 0; i < 3; i++) T2[i] = F.AxisX[i] * Ca + F.AxisY[i] * Sa;
        Cross3(F.Normal, T2, B2);
        Normalize3(B2);
        const float Dx2[2]{F.Dx[0] * Ca + F.Dy[0] * Sa * (Dot3(B2, F.AxisY) >= 0 ? 1.f : 1.f), F.Dx[1] * Ca + F.Dy[1] * Sa};
        const float G1 = Dot3(B2, F.AxisX), G2 = Dot3(B2, F.AxisY);
        M.Dx[0] = Dx2[0]; M.Dx[1] = Dx2[1];
        M.Dy[0] = F.Dx[0] * G1 + F.Dy[0] * G2;
        M.Dy[1] = F.Dx[1] * G1 + F.Dy[1] * G2;
        for (int i = 0; i < 3; i++) { M.AxisX[i] = T2[i]; M.AxisY[i] = B2[i]; }
    }
    const double DtMs = MarkClock > 0 ? Clamp((TimeSeconds - MarkClock) * 1000.0, 4.0, 80.0) : 16.0;
    MarkClock = TimeSeconds;
    const float Speed = Moved / (float(DtMs) / 1000.f);
    const float Ink = std::max(0.06f, P.MarkerInk);
    const float Demand = 0.30f + Speed * 0.85f * (P.Nib == MarkerNib::Chisel ? 1.15f : P.Nib == MarkerNib::Brush ? 1.0f : 0.7f);
    const float Feed = Clamp((0.25f + Ink * 0.9f) / Demand, 0.12f, 1.f);
    const float StepLen = std::min(Rx, Ry) * 0.55f;
    const int Steps = std::max(1, int(std::ceil(std::max(Moved, StepLen * 0.4f) / StepLen)));
    const float ACore = std::min(0.6f, 0.24f + Press * 0.3f) * (0.35f + Ink * 0.65f) * Feed;
    const float Bleed = P.MarkerBleed;
    const float Pad = (Rx + Ry) * Upw * 1.5f + 0.005f;
    const Sampler S = MakeSampler(std::min(FromU, F.U) - Pad, std::min(FromV, F.V) - Pad * 1.6f,
        std::max(FromU, F.U) + Pad, std::max(FromV, F.V) + Pad * 1.6f);
    const int Nx = std::max(6, int(std::ceil(Rx * 1024.f * 0.055f)));
    const int Ny = std::max(8, int(std::ceil(Ry * 1024.f * 0.055f)));
    auto Inside = [&](float Ox, float Oy) noexcept {
        if (Square) return std::fabs(Ox) <= Rx && std::fabs(Oy) <= Ry;
        return (Ox * Ox) / (Rx * Rx + 1e-8f) + (Oy * Oy) / (Ry * Ry + 1e-8f) <= 1.f;
    };
    auto StampAt = [&](float U0, float V0, float Boost) {
        Frame G = M; G.U = U0; G.V = V0;
        for (int J = 0; J <= Ny; J++)
            for (int I = 0; I <= Nx; I++)
            {
                const float Ox = (float(I) / Nx) * 2.f * Rx - Rx;
                const float Oy = (float(J) / Ny) * 2.f * Ry - Ry;
                if (!Inside(Ox, Oy)) continue;
                const auto Uv = UvAt(G, Ox, Oy);
                const float Th = ToothAt(Uv[0], Uv[1]);
                const float Edge = Square ? std::max(std::fabs(Ox) / Rx, std::fabs(Oy) / Ry)
                                          : std::sqrt((Ox * Ox) / (Rx * Rx) + (Oy * Oy) / (Ry * Ry));
                float Streak = 1.f;
                if (Feed < 0.85f) Streak = 0.45f + 0.55f * ValueNoise(Oy * 620.f + MarkSeed * 13.f, MarkSeed);
                const float Sv = FieldAt(SolventField, Uv[0], Uv[1]);
                std::array<float, 3> Col = P.MarkerColour;
                float RDot = 0.85f + Press * 0.35f;
                if (Sv > 0.22f)
                {
                    const auto Un = S.Sample(*this, Uv[0], Uv[1]);
                    Col = PigmentMix({Un[0], Un[1], Un[2]}, Sv * 0.8f, P.MarkerColour, 2.2f);
                    RDot *= 1.f + Sv * 0.5f;
                }
                float A = ACore * Boost * Streak * (0.82f + Th * 0.28f);
                if (Edge > 0.82f) A *= 1.f - (Edge - 0.82f) / 0.22f;
                if (A < 0.02f) continue;
                FillCirclePx(Uv[0] * W, Uv[1] * H, std::max(0.3f, RDot * Cps), Col, A, Blend::Multiply);
            }
        if (Bleed > 0.05f)
        {
            const int NT = int(Bleed * 7.f * Feed);
            for (int T = 0; T < NT; T++)
            {
                const float Side = Rand() < 0.5f ? -1.f : 1.f;
                float Ox = (Rand() * 2.f - 1.f) * Rx;
                float Oy = Side * Ry * (0.8f + Rand() * 0.2f);
                if (Rand() < 0.4f) { Ox = Side * Rx * 0.9f; Oy = (Rand() * 2.f - 1.f) * Ry; }
                const int Walk = 2 + int(Rand() * 4.f * Bleed);
                for (int Q = 0; Q < Walk; Q++)
                {
                    const auto Uv = UvAt(G, Ox, Oy);
                    FillCirclePx(Uv[0] * W, Uv[1] * H, std::max(0.3f, (0.7f + Press * 0.3f) * Cps), P.MarkerColour,
                        ACore * 0.32f * (1.f - float(Q) / Walk), Blend::Multiply);
                    Ox += (Ox / (Rx + 1e-6f)) * Rx * 0.17f + (Rand() - 0.5f) * Rx * 0.22f;
                    Oy += (Oy / (Ry + 1e-6f)) * Ry * 0.17f + (Rand() - 0.5f) * Ry * 0.22f;
                }
            }
        }
        SolventDisc(U0, V0, (Rx + Ry) * 0.14f * Upw * 3.f, std::min(1.f, 0.45f + Ink * 0.5f));
        WetDisc(U0, V0, (Rx + Ry) * 0.10f * Upw * 3.f, 0.35f);
    };
    if (Fresh) StampAt(F.U, F.V, 1.5f);
    for (int S = 1; S <= Steps; S++)
    {
        const float T = float(S) / Steps;
        StampAt(FromU + (F.U - FromU) * T, FromV + (F.V - FromV) * T, 1.f);
    }
    P.MarkerInk = std::max(0.04f, P.MarkerInk - std::max(Moved, StepLen) * (0.35f + (1.f - P.MarkerLoad) * 1.1f) * (0.5f + Press * 0.7f) * (0.7f + (Rx + Ry) * 6.f));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SAND
//------------------------------------------------------------------------------------------------------------------------

void PaintStampEngine::StampSand(const StampHit &Hit, double TimeSeconds)
{
    STAMP_PROLOGUE();
    const float Press = std::min(1.f, F.Pressure * P.SandPress);
    const float Rad = P.SandSize * (0.7f + Press * 0.5f);
    const float Grit = P.SandGrit;
    const bool Circ = P.SandMotion == SandMode::Circular;
    const int NGrains = Clamp(int(std::lround(24000.f / Grit)), 36, 380);
    if (int(SandGrains.size()) != NGrains)
    {
        SandGrains.clear();
        for (int I = 0; I < NGrains; I++)
        {
            const float A = Rand() * 2.f * Pi, R = std::sqrt(Rand());
            SandGrains.push_back({std::cos(A) * R, std::sin(A) * R,
                (0.55f + Rand() * 0.7f) * (Rand() < 0.03f ? 2.4f : 1.f), Rand() * 2.f * Pi, 0, 0, false});
        }
    }
    const int Steps = std::max(1, int(std::ceil(std::max(Moved, 0.002f) / 0.003f)));
    const float Pad = Rad * Upw * 1.6f + 0.005f;
    const Sampler S = MakeSampler(std::min(FromU, F.U) - Pad, std::min(FromV, F.V) - Pad * 1.7f,
        std::max(FromU, F.U) + Pad, std::max(FromV, F.V) + Pad * 1.7f);
    const float PartPx = std::max(0.4f, 26.f / Grit) * Cps;
    const float CutBase = Press * std::pow(150.f / Grit, 0.65f);
    const float OrbDelta = Circ ? Moved * 55.f : 0.f;
    auto CutAt = [&](float U0, float V0, float Phase) {
        Frame G = F; G.U = U0; G.V = V0;
        for (auto &Gr : SandGrains)
        {
            float Ox = Gr.Ox * Rad, Oy = Gr.Oy * Rad;
            if (Circ)
            {
                const float Oa = Phase + Gr.Phase;
                Ox += std::cos(Oa) * Rad * 0.30f;
                Oy += std::sin(Oa) * Rad * 0.30f;
            }
            const auto Uv = UvAt(G, Ox, Oy);
            if (Gr.HasPrev)
            {
                const auto Sm = S.Sample(*this, Gr.PrevU, Gr.PrevV);
                const float Film = Sm[3];
                const float Bite = std::min(1.3f, 0.22f + Film * 1.5f) * Gr.Depth;
                const std::array<float, 3> Powder{
                    std::round(Sm[0] + (233.f - Sm[0]) * 0.25f),
                    std::round(Sm[1] + (228.f - Sm[1]) * 0.25f),
                    std::round(Sm[2] + (218.f - Sm[2]) * 0.25f)};
                const float AC = std::min(0.85f, CutBase * 0.5f * Bite);
                const float LW = PartPx * (0.7f + Gr.Depth * 0.3f);
                UvLine(Gr.PrevU, Gr.PrevV, Uv[0], Uv[1], [&](float X0, float Y0, float X1, float Y1) noexcept {
                    StrokeLinePx(X0, Y0, X1, Y1, LW, Powder, AC, Blend::Over);
                });
                const float Grey = Gr.Depth > 1.4f ? 96.f : 128.f;
                const float HA = std::min(0.8f, CutBase * 0.42f * Bite);
                UvLine(Gr.PrevU, Gr.PrevV, Uv[0], Uv[1], [&](float X0, float Y0, float X1, float Y1) noexcept {
                    HeightLinePx(X0, Y0, X1, Y1, PartPx, Grey, HA);
                });
                if (Film > 0.1f) DryDisc(Uv[0], Uv[1], 0.006f * Upw * 3.f, CutBase * 0.02f * Bite);
            }
            Gr.PrevU = Uv[0]; Gr.PrevV = Uv[1]; Gr.HasPrev = true;
        }
    };
    for (int S = 1; S <= Steps; S++)
    {
        const float T = float(S) / Steps;
        CutAt(FromU + (F.U - FromU) * T, FromV + (F.V - FromV) * T, SandOrbit + OrbDelta * T);
    }
    SandOrbit += OrbDelta;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SPLATTER & SPLASH
//------------------------------------------------------------------------------------------------------------------------

namespace
{
struct LiquidParams { float R, G, B, Visc, Tension, Spread, Opacity, Chunk, Wet, Drip, Mu; bool Multiply; };
LiquidParams LiquidFor(SplatLiquid Kind) noexcept
{
    switch (Kind)
    {
    case SplatLiquid::Blood: return {94, 10, 16, 0.42f, 0.85f, 0.88f, 0.97f, 0.00f, 1.00f, 0.85f, 0.85f, false};
    case SplatLiquid::Mud: return {74, 53, 32, 0.92f, 0.30f, 0.55f, 1.00f, 0.90f, 0.45f, 0.12f, 1.60f, false};
    case SplatLiquid::Water: return {185, 201, 212, 0.04f, 0.70f, 1.80f, 0.85f, 0.00f, 1.00f, 1.20f, 0.70f, true};
    default: return {122, 16, 24, 0.50f, 0.60f, 1.00f, 0.95f, 0.00f, 0.85f, 0.60f, 1.00f, false};
    }
}
} // namespace

void PaintStampEngine::SplatMark(const Frame &F, float Px, float Py, float Ang, float Len, float Wid, float Alpha, bool Ragged, float Film, bool Multiply, const std::array<float, 3> &Rgb) noexcept
{
    const auto C = UvAt(F, Px, Py);
    const float Ca = std::cos(Ang), Sa = std::sin(Ang);
    const auto P1 = UvAt(F, Px + Ca * Len, Py + Sa * Len);
    const auto P2 = UvAt(F, Px - Sa * Wid, Py + Ca * Wid);
    float Du1 = P1[0] - C[0]; if (WrapU) { if (Du1 > 0.5f) Du1 -= 1; else if (Du1 < -0.5f) Du1 += 1; }
    float Du2 = P2[0] - C[0]; if (WrapU) { if (Du2 > 0.5f) Du2 -= 1; else if (Du2 < -0.5f) Du2 += 1; }
    const float RL = std::max(0.35f, std::hypot(Du1 * W, (P1[1] - C[1]) * H));
    const float RW = std::max(0.3f, std::hypot(Du2 * W, (P2[1] - C[1]) * H));
    const float Rot = std::atan2((P1[1] - C[1]) * H, Du1 * W);
    const float Cx = C[0] * W, Cy = C[1] * H;
    const Blend Mode = Multiply ? Blend::Multiply : Blend::Over;
    if (Ragged)
    {
        std::vector<std::array<float, 2>> Pts;
        const int N = 7;
        for (int K = 0; K < N; K++)
        {
            const float T = float(K) / N * 2.f * Pi;
            const float Jr = 0.62f + Rand() * 0.6f;
            const float Ex = std::cos(T) * RL * Jr, Ey = std::sin(T) * RW * Jr;
            Pts.push_back({Cx + Ex * std::cos(Rot) - Ey * std::sin(Rot), Cy + Ex * std::sin(Rot) + Ey * std::cos(Rot)});
        }
        FillPolygonPx(Pts, Rgb, Alpha, Mode);
        if (Film > 0) HeightEllipsePx(Cx, Cy, RL, RW, Rot, 255.f, Film);
    }
    else
    {
        FillEllipsePx(Cx, Cy, RL, RW, Rot, Rgb, Alpha, Mode);
        if (Film > 0) HeightEllipsePx(Cx, Cy, RL, RW, Rot, 255.f, Film);
    }
}

void PaintStampEngine::SplatBurst(const StampHit &A, const StampHit &B)
{
    Frame F = MakeFrame(A);
    const LiquidParams L = LiquidFor(P.Liquid);
    const std::array<float, 3> Rgb = P.SplatColour;
    const float Pxw = 0.5f * (W + H) / 2048.f;
    const float Upw = 0.5f * (std::hypot(F.Dx[0], F.Dx[1]) + std::hypot(F.Dy[0], F.Dy[1]));
    const float Throw[3]{B.Point[0] - A.Point[0], B.Point[1] - A.Point[1], B.Point[2] - A.Point[2]};
    float OxT = Dot3(Throw, F.AxisX), OyT = Dot3(Throw, F.AxisY);
    const float Mag = std::hypot(OxT, OyT);
    const float Force = P.SplatForce * (0.35f + std::min(1.4f, Mag / 0.22f));
    if (Mag > 1e-4f)
    {   // aim: AxisX becomes the throw direction (HTML tSplat), AxisY = nrm x AxisX (bSplat)
        const float Fx = OxT / Mag, Fy = OyT / Mag;
        float Nx[3], Ny[3];
        for (int i = 0; i < 3; i++) Nx[i] = F.AxisX[i] * Fx + F.AxisY[i] * Fy;
        Normalize3(Nx);
        Cross3(F.Normal, Nx, Ny);
        Normalize3(Ny);
        const float Dx[2]{F.Dx[0] * Fx + F.Dy[0] * Fy, F.Dx[1] * Fx + F.Dy[1] * Fy};
        const float G1 = Dot3(Ny, F.AxisX), G2 = Dot3(Ny, F.AxisY);
        const float Dy[2]{F.Dx[0] * G1 + F.Dy[0] * G2, F.Dx[1] * G1 + F.Dy[1] * G2};
        for (int i = 0; i < 3; i++) { F.AxisX[i] = Nx[i]; F.AxisY[i] = Ny[i]; }
        F.Dx[0] = Dx[0]; F.Dx[1] = Dx[1]; F.Dy[0] = Dy[0]; F.Dy[1] = Dy[1];
    }
    const float Vol = P.SplatVolume;
    const float Visc = Clamp(L.Visc * 0.55f + P.SplatBody * 0.55f, 0.02f, 0.98f);
    const float Thin = 1.f - Visc;
    const float Wet0 = FieldAt(WetField, F.U, F.V);
    const float Spread = L.Spread * (1.f + Wet0 * 0.5f) * (1.f + Thin * 0.3f);
    const float CoreR = (0.02f + Vol * 0.062f) * Spread * (0.8f + Force * 0.22f);
    const float Elong = 1.f + Force * (0.4f + Thin * 0.6f);
    float Dhx = 0, Dhy = 0, Slope = 0;
    Downhill(F, Dhx, Dhy, Slope);
    const float Sag = Slope * Thin * CoreR * 0.5f;
    const bool Ragged = L.Chunk > 0.4f;
    const float FilmDab = 0.16f * (0.35f + Visc);
    auto LobeAt = [&](float Ang) noexcept {
        float T = Ang * 3.f;
        T -= std::floor(T);
        return 0.68f + ToothAt(T, Vol) * 0.6f;
    };
    SplatEvidence = {};
    float SumSmall = 0, SumLarge = 0;
    int NSmall = 0, NLarge = 0;

    const int NCore = int(60.f + Vol * 85.f);
    for (int I = 0; I < NCore; I++)
    {
        const float Ang = Rand() * 2.f * Pi;
        const float T = std::pow(Rand(), 0.6f);
        const float Rr = T * CoreR * LobeAt(Ang);
        const float Along = std::cos(Ang);
        const float Px = std::cos(Ang) * Rr * (Along > 0 ? Elong : 0.75f) + Dhx * Sag * T;
        const float Py = std::sin(Ang) * Rr + Dhy * Sag * T;
        const float R0 = CoreR * (0.13f + Rand() * 0.1f) * (1.f - T * 0.35f);
        const float Al = L.Opacity * (0.16f + 0.3f * (1.f - T)) * (0.75f + Rand() * 0.35f);
        SplatMark(F, Px, Py, Ang, R0 * (1.f + Force * 0.35f * std::max(0.f, Along)), R0,
            std::min(0.92f, Al), Ragged, (I % 3 == 0) ? FilmDab : 0.f, L.Multiply, Rgb);
    }
    if (!Ragged && L.Tension > 0.35f)
    {
        const float RimA = L.Opacity * (0.3f + L.Tension * 0.4f);
        const float RimW = (0.8f + L.Tension * 1.6f) * Pxw;
        bool HavePrev = false;
        float PrevU = 0, PrevV = 0;
        const int M = 26;
        for (int K = 0; K <= M; K++)
        {
            const float Ang = float(K % M) / M * 2.f * Pi;
            const float Rr = CoreR * LobeAt(Ang) * 0.98f;
            const float Along = std::cos(Ang);
            const float Px = std::cos(Ang) * Rr * (Along > 0 ? Elong : 0.75f) + Dhx * Sag;
            const float Py = std::sin(Ang) * Rr + Dhy * Sag;
            const auto Uv = UvAt(F, Px, Py);
            if (HavePrev)
                UvLine(PrevU, PrevV, Uv[0], Uv[1], [&](float X0, float Y0, float X1, float Y1) noexcept {
                    StrokeLinePx(X0, Y0, X1, Y1, std::max(0.4f, RimW), Rgb, RimA, L.Multiply ? Blend::Multiply : Blend::Over);
                });
            PrevU = Uv[0]; PrevV = Uv[1]; HavePrev = true;
        }
    }
    if (L.Chunk > 0.05f)
    {
        const int NLump = int(L.Chunk * (7.f + Vol * 12.f));
        for (int I = 0; I < NLump; I++)
        {
            const float Ang = Rand() * 2.f * Pi;
            const float Rr = std::pow(Rand(), 0.7f) * CoreR * 1.15f;
            const float Px = std::cos(Ang) * Rr + Dhx * Sag * 0.5f;
            const float Py = std::sin(Ang) * Rr + Dhy * Sag * 0.5f;
            const float R0 = 0.004f + std::pow(Rand(), 1.6f) * 0.016f * (0.6f + Vol);
            SplatMark(F, Px, Py, Ang, R0, R0 * (0.7f + Rand() * 0.4f), 0.95f, true, 0.4f, false, Rgb);
        }
        const std::array<float, 3> Grit{Rgb[0] * 0.62f, Rgb[1] * 0.62f, Rgb[2] * 0.62f};
        for (int I = 0; I < 26; I++)
        {
            const float Ang = Rand() * 2.f * Pi;
            const float Rr = std::pow(Rand(), 0.7f) * CoreR * 1.3f;
            const auto Uv = UvAt(F, std::cos(Ang) * Rr, std::sin(Ang) * Rr);
            FillCirclePx(Uv[0] * W, Uv[1] * H, (0.4f + Rand() * 0.7f) * Pxw, Grit, 0.8f, Blend::Over);
        }
    }
    const float Weber = Force * Force * (0.35f + Vol) / ((0.35f + L.Tension) * (0.45f + Visc));
    const int NSpine = Weber > 0.7f ? std::min(26, int(Weber * 9.f)) : 0;
    for (int S2 = 0; S2 < NSpine; S2++)
    {
        float Ang = Rand() * 2.f * Pi;
        if (Mag > 0.01f && Rand() < 0.62f) Ang = Gauss() * 0.5f;
        const float Reach = CoreR * (1.05f + Rand() * (0.7f + std::sqrt(Weber) * 1.4f));
        const float W0 = (1.1f + Vol * 2.2f) * (0.7f + Visc * 0.5f) * Pxw;
        const int Steps = 6 + int(Reach * 90.f);
        bool HavePrev = false;
        float PrevU = 0, PrevV = 0;
        for (int K = 0; K <= Steps; K++)
        {
            const float T = float(K) / Steps;
            const float Taper = 1.f - T;
            const float Jitter = (Rand() - 0.5f) * 0.004f * (1.f + Thin);
            const float Px = std::cos(Ang) * Reach * T * (std::fabs(Ang) < 0.9f ? Elong : 1.f);
            const float Py = std::sin(Ang) * Reach * T + Jitter;
            const auto Uv = UvAt(F, Px, Py);
            const float Ww = std::max(0.35f, W0 * Taper * Taper);
            const float Aa = L.Opacity * (0.4f + Visc * 0.2f) * (0.35f + Taper);
            FillCirclePx(Uv[0] * W, Uv[1] * H, Ww, Rgb, Aa, L.Multiply ? Blend::Multiply : Blend::Over);
            if (HavePrev)
                UvLine(PrevU, PrevV, Uv[0], Uv[1], [&](float X0, float Y0, float X1, float Y1) noexcept {
                    StrokeLinePx(X0, Y0, X1, Y1, Ww * 1.3f, Rgb, Aa, L.Multiply ? Blend::Multiply : Blend::Over);
                });
            PrevU = Uv[0]; PrevV = Uv[1]; HavePrev = true;
        }
    }
    const int NSat = int(6.f + P.SplatSatellites * 115.f * Force * (1.15f - Visc * 0.55f) * (L.Chunk > 0.4f ? 0.45f : 1.f));
    for (int I = 0; I < NSat; I++)
    {
        float Dmm = L.Mu * std::exp(Gauss() * 0.55f) * (0.65f + Vol * 0.75f);
        Dmm = Clamp(Dmm, 0.15f, 5.f);
        const float Dd = Dmm / 100.f;
        float Ang;
        if (Mag > 0.01f) Ang = Gauss() * (0.6f + Thin * 0.45f) + (Rand() < 0.22f ? Pi : 0.f);
        else Ang = Rand() * 2.f * Pi;
        const float V0 = Force * (0.5f + Rand() * 0.95f);
        float Range = 0.015f + V0 * V0 * 0.13f * std::pow(Dmm, 1.15f) / (0.7f + L.Tension * 0.6f);
        Range = std::min(0.6f, Range) * (0.75f + Rand() * 0.4f) + CoreR * 0.55f;
        const float Px = std::cos(Ang) * Range, Py = std::sin(Ang) * Range;
        const float SinT = Clamp(1.12f - Range * 2.4f * (0.6f + Force * 0.5f), 0.16f, 1.f);
        const float Wid = Dd * 0.5f * (0.85f + Spread * 0.4f);
        const float Len = Wid / SinT;
        const float Al = std::min(0.95f, L.Opacity * (0.5f + 0.45f * std::min(1.f, Dmm)) * (0.7f + Rand() * 0.35f));
        SplatMark(F, Px, Py, Ang, Len, Wid, Al, Ragged, Dmm > 1.6f ? FilmDab * 1.4f : 0.f, L.Multiply, Rgb);
        SplatEvidence.Droplets++;
        if (Dmm < 0.8f) { SumSmall += Range; NSmall++; }
        else if (Dmm > 1.8f) { SumLarge += Range; NLarge++; }
        if (SinT < 0.55f && Rand() < 0.6f)
        {
            const auto Uv0 = UvAt(F, Px, Py);
            const float Tl = Len * (0.9f + Rand() * 1.3f);
            const auto Uv1 = UvAt(F, Px + std::cos(Ang) * Tl, Py + std::sin(Ang) * Tl);
            const float LW = Clamp(Wid * W * Upw * 0.35f, 0.4f, 3.f);
            UvLine(Uv0[0], Uv0[1], Uv1[0], Uv1[1], [&](float X0, float Y0, float X1, float Y1) noexcept {
                StrokeLinePx(X0, Y0, X1, Y1, LW, Rgb, Al * 0.55f, L.Multiply ? Blend::Multiply : Blend::Over);
            });
        }
        if (Visc < 0.55f && Dmm > 1.3f && Rand() < 0.5f)
        {
            const int Nb = int(2.f + Rand() * 2.f);
            for (int Q = 0; Q < Nb; Q++)
            {
                const float Ba = Ang + Gauss() * 0.7f;
                const float Br = Len * (0.8f + Rand() * 0.9f);
                const auto Uvb = UvAt(F, Px + std::cos(Ba) * Br, Py + std::sin(Ba) * Br);
                FillCirclePx(Uvb[0] * W, Uvb[1] * H, Clamp(Dd * W * Upw * 0.16f, 0.3f, 1.6f), Rgb, Al * 0.7f, L.Multiply ? Blend::Multiply : Blend::Over);
            }
        }
    }
    const int NMist = L.Chunk > 0.4f ? 0 : std::max(0, int((Force - 0.65f) * 110.f * Thin * (P.Liquid == SplatLiquid::Blood ? 1.6f : 0.85f)));
    for (int I = 0; I < NMist; I++)
    {
        const float Ang = Rand() * 2.f * Pi;
        const float Rr = CoreR * (0.8f + std::pow(Rand(), 0.5f) * 2.6f);
        const auto Uv = UvAt(F, std::cos(Ang) * Rr * Elong, std::sin(Ang) * Rr);
        FillCirclePx(Uv[0] * W, Uv[1] * H, (0.3f + Rand() * 0.45f) * Pxw, Rgb, L.Opacity * 0.55f, L.Multiply ? Blend::Multiply : Blend::Over);
    }
    if (NSmall) SplatEvidence.MeanFlightSmall = SumSmall / NSmall;
    if (NLarge) SplatEvidence.MeanFlightLarge = SumLarge / NLarge;

    WetDisc(F.U, F.V, CoreR * 0.24f * Upw * 3.f, std::min(1.f, L.Wet * (0.55f + Vol * 0.55f)));
    if (Slope > 0.45f && Drips.size() < 100 && L.Drip > 0.04f)
    {
        const float Pad = CoreR * 0.8f * Upw + 0.002f;
        const Sampler S = MakeSampler(F.U - Pad, F.V - Pad * 1.7f, F.U + Pad, F.V + Pad * 1.7f);
        const float YieldL = 0.26f + Visc * 0.5f;
        int SpawnedRuns = 0;
        for (int T = 0; T < 7 && SpawnedRuns < 2; T++)
        {
            const float Ang = Rand() * 2.f * Pi;
            const float Rr = std::pow(Rand(), 0.6f) * CoreR;
            const float Px = std::cos(Ang) * Rr + Dhx * Sag, Py = std::sin(Ang) * Rr + Dhy * Sag;
            const auto Uv = UvAt(F, Px, Py);
            const float Film = S.Sample(*this, Uv[0], Uv[1])[3];
            if (Film > YieldL && Rand() < (Film - YieldL) * L.Drip * 1.5f)
            {
                SpawnDrip(F, Px, Py, Rgb, 0.3f + (Film - YieldL) * 1.5f, 0.5f + Thin * 1.2f,
                    0.7f + Vol, int(Rand() * 5), true);
                SpawnedRuns++;
            }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     DUST / GLITTER / RUST
//------------------------------------------------------------------------------------------------------------------------

bool PaintStampEngine::StickParticle(float U, float V, const float Normal[3], const Frame &F) noexcept
{
    const float Cps = 0.5f * (W + H) / 1024.f;
    float Cu = U, Cv = V;
    float Nrm[3]{Normal[0], Normal[1], Normal[2]};
    const float Slope = std::sqrt(std::max(0.f, 1.f - Nrm[1] * Nrm[1]));
    if (Slope > 0.25f && P.Particle != DustKind::Glitter && Rand() < Slope)
    {
        const float Ny = Nrm[1];
        const float Proj[3]{Nrm[0] * Ny, -1.f + Ny * Ny, Nrm[2] * Ny};
        const float Dhx = Dot3(F.AxisX, Proj) / Slope, Dhy = Dot3(F.AxisY, Proj) / Slope;
        const float Slide = Slope * (1.1f - P.DustTack) * 0.02f * Rand();
        Cu += (F.Dx[0] * Dhx + F.Dy[0] * Dhy) * Slide;
        Cv += (F.Dx[1] * Dhx + F.Dy[1] * Dhy) * Slide;
        float Found[3];
        if (NormalLookup && NormalLookup(Cu * RangeU, Cv * RangeV, Found)) { Nrm[0] = Found[0]; Nrm[1] = Found[1]; Nrm[2] = Found[2]; }
    }
    const float Tk = TackAt(Cu, Cv);
    if (Tk < 0.12f) return false;
    const float Upness = std::max(0.15f, Nrm[1] * 0.5f + 0.5f);
    const float Valley = 1.f - ToothRidge(Cu, Cv);
    const float Lodge = P.Particle == DustKind::Glitter ? 1.f : (0.62f + Valley * (P.Particle == DustKind::Rust ? 0.85f : 0.6f));
    if (Rand() > Tk * Upness * Lodge) return false;
    const float X = Cu * W, Y = Cv * H;
    const float G = P.DustGrain;
    if (P.Particle == DustKind::Glitter)
    {
        const float Lx = 0.48f, Ly = 0.72f, Lz = 0.50f;
        const float Ndl = std::max(0.f, Nrm[0] * Lx + Nrm[1] * Ly + Nrm[2] * Lz);
        const bool FaceUp = Rand() < 0.88f;
        const float Tilt = Rand() * 0.35f;
        const float Spark = FaceUp ? std::pow(std::max(0.f, Ndl - Tilt), 6.f) : 0.f;
        const float Shade = FaceUp ? 0.22f + Ndl * 0.55f : 0.06f;
        const std::array<float, 3> Col{
            std::min(255.f, P.DustColour[0] * Shade + Spark * 255.f),
            std::min(255.f, P.DustColour[1] * Shade + Spark * 245.f),
            std::min(255.f, P.DustColour[2] * Shade + Spark * 210.f)};
        const float S = (1.6f + Rand() * 1.8f) * G * Cps;
        const float Ang = Rand() * Pi;
        const float Squash = 0.62f + Rand() * 0.2f;
        std::vector<std::array<float, 2>> Pts;
        for (int I = 0; I < 6; I++)
        {
            const float A = I * Pi / 3.f;
            const float Ex = std::cos(A) * S, Ey = std::sin(A) * S * Squash;
            Pts.push_back({X + Ex * std::cos(Ang) - Ey * std::sin(Ang), Y + Ex * std::sin(Ang) + Ey * std::cos(Ang)});
        }
        FillPolygonPx(Pts, Col, 0.92f, Blend::Over);
        if (Spark > 0.18f) FillCirclePx(X, Y, S * 0.18f, {255.f, 253.f, 245.f}, std::min(0.95f, Spark), Blend::Over);
        HeightCirclePx(X, Y, 1.2f * Cps, 210.f, 0.2f);
    }
    else if (P.Particle == DustKind::Rust)
    {
        const float Th = ToothAt(Cu * 2.1f, Cv * 1.7f);
        FillEllipsePx(X, Y, (3.2f + Th * 3.4f) * G * Cps, (2.2f + Th * 2.2f) * G * Cps, Th * 4.f, {150.f, 74.f, 30.f}, 0.16f + Th * 0.12f, Blend::Over);
        const std::array<float, 3> Col{90.f + Th * 80.f, 28.f + Th * 36.f, 8.f + Th * 14.f};
        FillEllipsePx(X, Y, (1.6f + Th * 2.4f) * G * Cps, (1.1f + Th * 1.6f) * G * Cps, Th * 4.f, Col, 0.5f + Th * 0.35f, Blend::Over);
        FillEllipsePx(X + 0.4f, Y + 0.3f, 0.7f * G * Cps, 0.5f * G * Cps, 0.f, {42.f, 16.f, 6.f}, 0.25f + Th * 0.25f, Blend::Over);
        HeightEllipsePx(X, Y, (1.5f + Th * 2.2f) * G * Cps, (1.0f + Th * 1.5f) * G * Cps, Th * 4.f, 255.f, 0.30f);
        HeightEllipsePx(X + 0.4f, Y + 0.3f, 0.55f * G * Cps, 0.4f * G * Cps, 0.f, 70.f, 0.35f);
    }
    else
    {
        const int N = 2 + int(P.DustRate * 3.f);
        for (int K = 0; K < N; K++)
        {
            const float Gu = Cu + (Rand() - 0.5f) * 0.0015f * (1024.f / W);
            const float Gv = Cv + (Rand() - 0.5f) * 0.0015f * (1024.f / H);
            const float Th = ToothAt(Gu, Gv);
            const float Kk = 0.55f + Th * 0.7f;
            const std::array<float, 3> Col{P.DustColour[0] * Kk, P.DustColour[1] * Kk * 0.96f, P.DustColour[2] * Kk * 0.88f};
            const float Wd = (0.45f + Rand() * 1.15f) * G * Cps, Ht = (0.35f + Rand() * 0.9f) * G * Cps;
            const float Rot = Th * 6.2f;
            std::vector<std::array<float, 2>> Pts;
            const float Corners[4][2]{{-Wd * 0.5f, -Ht * 0.5f}, {Wd * 0.5f, -Ht * 0.5f}, {Wd * 0.5f, Ht * 0.5f}, {-Wd * 0.5f, Ht * 0.5f}};
            for (const auto &C : Corners)
                Pts.push_back({Gu * W + C[0] * std::cos(Rot) - C[1] * std::sin(Rot), Gv * H + C[0] * std::sin(Rot) + C[1] * std::cos(Rot)});
            FillPolygonPx(Pts, Col, 0.45f + Th * 0.35f, Blend::Over);
        }
    }
    EatTack(Cu, Cv, P.Particle == DustKind::Glitter ? 0.55f : 0.28f);
    return true;
}

void PaintStampEngine::StampDust(const StampHit &Hit, double TimeSeconds)
{
    STAMP_PROLOGUE();
    const float Press = std::min(1.f, F.Pressure);
    const float Rad = P.DustSize * (0.75f + Press * 0.4f);
    const int Steps = std::max(1, int(std::ceil(std::max(Moved, Rad * 0.45f) / (Rad * 0.45f))));
    const int NPack = 80 + int(P.DustRate * 220.f);
    const bool Rusting = P.Particle == DustKind::Rust;
    auto NormalAtUv = [&](float Cu, float Cv, float Out[3]) {
        float Found[3];
        if (NormalLookup && NormalLookup(Cu * RangeU, Cv * RangeV, Found)) { Out[0] = Found[0]; Out[1] = Found[1]; Out[2] = Found[2]; }
        else { Out[0] = F.Normal[0]; Out[1] = F.Normal[1]; Out[2] = F.Normal[2]; }
    };
    for (int S = 1; S <= Steps; S++)
    {
        const float T = float(S) / Steps;
        Frame G = F; G.U = FromU + (F.U - FromU) * T; G.V = FromV + (F.V - FromV) * T;
        // glue film
        const int Nx = std::max(10, int(Rad * 1024.f * 0.09f));
        const float AT = 0.28f + P.DustTack * 0.55f * Press;
        for (int J = 0; J <= Nx; J++)
            for (int I = 0; I <= Nx; I++)
            {
                const float Ox = (float(I) / Nx) * 2.f * Rad - Rad;
                const float Oy = (float(J) / Nx) * 2.f * Rad - Rad;
                if (Ox * Ox + Oy * Oy > Rad * Rad) continue;
                const auto Uv = UvAt(G, Ox, Oy);
                const int Tx = int(Uv[0] * W), Ty = int(Uv[1] * H);
                const int Tr = std::max(1, int(1.2f * Cps));
                for (int Dy = -Tr; Dy <= Tr; Dy++)
                    for (int Dx = -Tr; Dx <= Tr; Dx++)
                        if (Dx * Dx + Dy * Dy <= Tr * Tr) TackPixel(Tx + Dx, Ty + Dy, 255.f, AT);
                FillCirclePx(Uv[0] * W, Uv[1] * H, std::max(0.5f, 1.0f * Cps), {236.f, 232.f, 224.f}, 0.07f + P.DustTack * 0.1f, Blend::Over);
            }
        // particle pack (rust creeps from its last scab)
        for (int I = 0; I < NPack; I++)
        {
            float Cu, Cv;
            if (Rusting && HaveRustAnchor && Rand() < 0.5f)
            {
                const float Jx = (Rand() - 0.5f) * Rad * 0.35f, Jy = (Rand() - 0.5f) * Rad * 0.35f;
                Cu = RustAnchorU + (G.Dx[0] * Jx + G.Dy[0] * Jy);
                Cv = RustAnchorV + (G.Dx[1] * Jx + G.Dy[1] * Jy);
            }
            else
            {
                const float A = Rand() * 2.f * Pi, R = std::sqrt(Rand()) * Rad;
                const auto Uv = UvAt(G, std::cos(A) * R, std::sin(A) * R);
                Cu = Uv[0]; Cv = Uv[1];
            }
            float Nrm[3];
            NormalAtUv(Cu, Cv, Nrm);
            if (StickParticle(Cu, Cv, Nrm, G) && Rusting) { RustAnchorU = Cu; RustAnchorV = Cv; HaveRustAnchor = true; }
        }
    }
}

void PaintStampEngine::AutoSettle() noexcept
{
    const int N = 280 + int(P.DustRate * 900.f);
    for (int I = 0; I < N; I++)
    {
        const float Cu = Rand(), Cv = Rand();
        float Nrm[3]{0, 1, 0};
        float Found[3];
        if (NormalLookup && NormalLookup(Cu * RangeU, Cv * RangeV, Found)) { Nrm[0] = Found[0]; Nrm[1] = Found[1]; Nrm[2] = Found[2]; }
        Frame G;
        G.U = Cu; G.V = Cv;
        G.Normal[0] = Nrm[0]; G.Normal[1] = Nrm[1]; G.Normal[2] = Nrm[2];
        const float Up[3]{0, 1, 0};
        Cross3(Up, Nrm, G.AxisX);
        if (std::sqrt(Dot3(G.AxisX, G.AxisX)) < 1e-6f) { G.AxisX[0] = 1; G.AxisX[1] = 0; G.AxisX[2] = 0; }
        Normalize3(G.AxisX);
        Cross3(Nrm, G.AxisX, G.AxisY);
        Normalize3(G.AxisY);
        G.Dx[0] = LastUvPerWorld; G.Dx[1] = 0;
        G.Dy[0] = 0; G.Dy[1] = LastUvPerWorld;
        StickParticle(Cu, Cv, Nrm, G);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SOOT
//------------------------------------------------------------------------------------------------------------------------

void PaintStampEngine::StampSoot(const StampHit &Hit, double TimeSeconds)
{
    Frame F = MakeFrame(Hit);
    const float Cps = 0.5f * (W + H) / 1024.f;
    const float Upw = 0.5f * (std::hypot(F.Dx[0], F.Dx[1]) + std::hypot(F.Dy[0], F.Dy[1]));
    const float Heat = P.SootHeat, Dens = P.SootDensity;
    const float W0 = 0.012f + P.SootSpread * 0.032f;
    const int Steps = 20 + int(Heat * 32.f);
    const float Ds = 0.011f + Heat * 0.01f;
    const float Time = float(TimeSeconds) * 0.35f;
    float Dhx = 0, Dhy = 0, Slope = 0;
    Downhill(F, Dhx, Dhy, Slope);
    const float Uhx = -Dhx, Uhy = -Dhy; // hot gas climbs
    struct Hist { float Ox, Oy, W, T; int S; };
    std::vector<Hist> Path;
    float PosOx = 0, PosOy = 0;
    for (int S = 0; S < Steps; S++)
    {
        PosOx += Uhx * Ds;
        PosOy += Uhy * Ds;
        const float Tt = Steps > 0 ? float(S) / Steps : 0.f;
        Path.push_back({PosOx, PosOy, W0 * (1.f + Tt * (1.6f + P.SootSpread * 2.4f)), Tt, S});
        if (Slope < 1e-4f) break; // at the crown the plume has nowhere to climb
    }
    for (const auto &Hst : Path)
    {
        const float Reside = std::min(1.f, Hst.T / 0.30f) * (1.f - Hst.T * 0.42f);
        const float Flick = 0.72f + 0.5f * ValueNoise(Time * 6.f + Hst.S * 0.35f, Hst.S * 0.2f);
        const float Fade = Reside * Flick * Dens;
        const float Sway = std::sin(Hst.T * 9.f - Time * 7.f) * P.SootWaves * Hst.W * 0.55f;
        const int NDrop = 10 + int(Dens * 22.f * (1.f - Hst.T * 0.45f));
        for (int I = 0; I < NDrop; I++)
        {
            const float U1 = std::max(1e-6f, Rand());
            const float Ang = Rand() * 2.f * Pi;
            const float Rad = Hst.W * std::sqrt(-2.f * std::log(U1)) * 0.38f;
            const float Ox = Hst.Ox + std::cos(Ang) * Rad + Sway * (0.4f + Rad / Hst.W);
            const float Oy = Hst.Oy + std::sin(Ang) * Rad;
            const auto Uv = UvAt(F, Ox, Oy);
            const float Core = std::exp(-(Rad * Rad) / (Hst.W * Hst.W * 0.22f));
            const float A = std::min(0.22f, 0.035f * Fade * (0.35f + Core));
            const float Brown = 1.f - Core * 0.25f;
            const std::array<float, 3> Col{
                P.SootColour[0] * Brown + 18.f * (1.f - Brown),
                P.SootColour[1] * Brown + 14.f * (1.f - Brown),
                P.SootColour[2] * Brown + 10.f * (1.f - Brown)};
            FillCirclePx(Uv[0] * W, Uv[1] * H, (0.55f + (1.f - Core) * 1.8f + Hst.T * 1.2f) * Cps, Col, A, Blend::Over);
        }
        const auto Huv = UvAt(F, Hst.Ox, Hst.Oy);
        DryDisc(Huv[0], Huv[1], Hst.W * 0.14f * Upw * 3.f, 0.05f * Fade);
        if (Rand() < P.SootFlakes * 0.18f * Fade)
        {
            const float Ang = Rand() * 2.f * Pi;
            const auto Uv = UvAt(F, Hst.Ox + std::cos(Ang) * Hst.W * Rand(), Hst.Oy + std::sin(Ang) * Hst.W * Rand());
            const float Rot = Rand() * 6.f, Wd = (1.8f + Rand()) * Cps, Ht = 0.5f * Cps;
            std::vector<std::array<float, 2>> Pts;
            const float Corners[4][2]{{-Wd * 0.5f, -Ht * 0.5f}, {Wd * 0.5f, -Ht * 0.5f}, {Wd * 0.5f, Ht * 0.5f}, {-Wd * 0.5f, Ht * 0.5f}};
            for (const auto &C : Corners)
                Pts.push_back({Uv[0] * W + C[0] * std::cos(Rot) - C[1] * std::sin(Rot), Uv[1] * H + C[0] * std::sin(Rot) + C[1] * std::cos(Rot)});
            FillPolygonPx(Pts, P.SootColour, 0.28f + Dens * 0.25f, Blend::Over);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     ERASER + DIGITAL
//------------------------------------------------------------------------------------------------------------------------

void PaintStampEngine::StampEraser(const StampHit &Hit, double TimeSeconds)
{
    STAMP_PROLOGUE();
    EraseCoverage = true;
    const float Press = std::min(1.f, F.Pressure);
    const float R = P.EraserSize * (0.7f + Press * 0.5f);
    const float Dist = HadLast ? Moved : R * 0.3f;
    const int Steps = std::max(1, int(std::ceil(std::max(Dist, R * 0.3f) / (R * 0.35f))));
    const float Strength = std::min(1.f, P.EraserStrength * (0.35f + Press * 0.65f));
    const float RxPx = std::max(1.2f, R * std::hypot(F.Dx[0] * W, F.Dx[1] * H));
    const float RyPx = std::max(1.2f, R * std::hypot(F.Dy[0] * W, F.Dy[1] * H));
    const float Inner = 1.f - P.EraserSoftness;
    const float Pad = R * Upw * 1.6f + 0.005f;
    const Sampler S = MakeSampler(std::min(FromU, F.U) - Pad, std::min(FromV, F.V) - Pad * 1.7f,
        std::max(FromU, F.U) + Pad, std::max(FromV, F.V) + Pad * 1.7f);
    float Lifted = 0, CrumbR = 0, CrumbG = 0, CrumbB = 0;
    int CrumbN = 0;
    auto Wipe = [&](float U0, float V0) {
        Frame G = F; G.U = U0; G.V = V0;
        const float X = U0 * W, Y = V0 * H;
        const float Dry = FieldAt(DryField, U0, V0);
        const float Wet = FieldAt(WetField, U0, V0);
        const auto Sm = S.Sample(*this, U0, V0);
        const float Film = Sm[3];
        const float LiftDry = std::min(1.f, Dry * (0.35f + Strength * 1.15f));
        float AShow = LiftDry * 0.9f;
        if (Wet > 0.28f)
        {
            const float Smear = std::min(0.55f, Strength * Wet * 0.8f);
            const auto Uvs = UvAt(G, R * 0.55f, 0.f);
            SoftDabPx(false, Uvs[0] * W, Uvs[1] * H, RxPx * 0.8f, RyPx * 0.8f, {Sm[0], Sm[1], Sm[2]}, Smear, Inner);
            AShow = std::max(AShow, Strength * 0.10f);
            SoftDabPx(true, X, Y, RxPx, RyPx, {128.f, 128.f, 128.f}, Strength * 0.12f * Wet, Inner);
        }
        else if (Film > 0.06f)
        {
            AShow = std::max(AShow, Strength * Press * 0.16f);
            SoftDabPx(true, X, Y, RxPx, RyPx, {128.f, 128.f, 128.f}, Strength * 0.10f, Inner);
        }
        else if (Dry <= 0.02f)
        {
            AShow = std::max(AShow, Strength * 0.05f);
            if (Press > 0.8f) SoftDabPx(true, X, Y, RxPx, RyPx, {116.f, 116.f, 116.f}, 0.05f, Inner);
        }
        if (AShow > 0.01f) SoftDabPx(false, X, Y, RxPx, RyPx, Base, std::min(1.f, AShow), Inner);
        if (LiftDry > 0.01f)
        {
            DryDisc(U0, V0, R * 0.30f * Upw * 3.f, -LiftDry * 0.55f);
            Lifted += LiftDry;
            CrumbR += Sm[0]; CrumbG += Sm[1]; CrumbB += Sm[2]; CrumbN++;
        }
        EatTack(U0, V0, Strength * 0.6f);
    };
    for (int S = 1; S <= Steps; S++)
    {
        const float T = float(S) / Steps;
        Wipe(FromU + (F.U - FromU) * T, FromV + (F.V - FromV) * T);
    }
    if (Lifted > 0.35f && CrumbN > 0)
    {
        const int NC = std::min(9, 2 + int(Lifted * 4.f));
        const std::array<float, 3> Col{
            std::round((CrumbR / CrumbN) * 0.45f + 150.f * 0.55f),
            std::round((CrumbG / CrumbN) * 0.45f + 144.f * 0.55f),
            std::round((CrumbB / CrumbN) * 0.45f + 138.f * 0.55f)};
        for (int I = 0; I < NC; I++)
        {
            const float Back = R * (0.5f + Rand() * 1.3f);
            const float Side = (Rand() - 0.5f) * R * 1.6f;
            const auto Uv = UvAt(F, -Back, Side);
            const float Wpx = 0.7f + Rand() * 1.3f;
            const float Rot = Rand() * 6.3f;
            std::vector<std::array<float, 2>> Pts;
            const float Corners[4][2]{{-Wpx, -Wpx * 0.32f}, {Wpx, -Wpx * 0.32f}, {Wpx, Wpx * 0.32f}, {-Wpx, Wpx * 0.32f}};
            for (const auto &C : Corners)
                Pts.push_back({Uv[0] * W + (C[0] * std::cos(Rot) - C[1] * std::sin(Rot)) * Cps,
                    Uv[1] * H + (C[0] * std::sin(Rot) + C[1] * std::cos(Rot)) * Cps});
            FillPolygonPx(Pts, Col, 0.75f, Blend::Over);
        }
    }
    EraseCoverage = false;
}

void PaintStampEngine::StampDigital(const StampHit &Hit, double TimeSeconds)
{
    STAMP_PROLOGUE();
    const float Press = std::min(1.f, F.Pressure);
    const float R = P.DigitalSize * (0.55f + Press * 0.65f);
    const float Dist = HadLast ? Moved : R * 0.3f;
    const int Steps = std::max(1, int(std::ceil(std::max(Dist, R * 0.3f) / (R * 0.32f))));
    const float A = std::min(1.f, P.DigitalOpacity * (0.25f + Press * 0.75f));
    const float RxPx = std::max(1.2f, R * std::hypot(F.Dx[0] * W, F.Dx[1] * H));
    const float RyPx = std::max(1.2f, R * std::hypot(F.Dy[0] * W, F.Dy[1] * H));
    for (int S = 1; S <= Steps; S++)
    {
        const float T = float(S) / Steps;
        const float U0 = FromU + (F.U - FromU) * T, V0 = FromV + (F.V - FromV) * T;
        SoftDabPx(false, U0 * W, V0 * H, RxPx, RyPx, P.DigitalColour, A, P.DigitalHardness);
    }
}

#undef STAMP_PROLOGUE

} // namespace Frontier
