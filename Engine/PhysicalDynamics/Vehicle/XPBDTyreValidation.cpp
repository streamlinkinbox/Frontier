//============================================================================================================================================
// 📦 Frontier/PhysicalDynamics/Vehicle/XPBDTyreValidation.cpp — cross-validation of the XPBD soft tyre vs the Pacejka baseline
//============================================================================================================================================
//
//    Phase-2 gate: the calibrated multi-ring XPBD soft tyre (XPBDSoftTyre) must reproduce the Phase-1 analytic Magic-Formula
//    tyre (PacejkaMagicFormula) before it can replace it in the vehicle. This headless harness rolls the soft carcass on a
//    moving flat-track ("belt") to steady state and compares the EMERGENT contact force to Pacejka at the same vertical load.
//
//    Build & run (in-sandbox, no Unreal / no Jolt):
//        g++ -std=c++17 -O2 XPBDSoftTyre.cpp PacejkaMagicFormula.cpp XPBDTyreValidation.cpp -o xpbdval && ./xpbdval
//
//    What "matches" means here — and its honest limits. The XPBD tyre is an emergent brush-on-elastic-carcass model, not a
//    curve fit, so we validate the PHYSICS, not bit-equality:
//      • shape        — Fx(κ), Fy(α) rise, peak, and saturate; the free-rolling point sits at ~zero Fx.
//      • slip/​corner  — the initial slopes (slip stiffness / cornering stiffness) match Pacejka within a stated band.
//      • peak & place — the peak force is within ~0.5–1.3× of Pacejka's and occurs at a plausible slip.
//      • signs        — drive slip → +Fx, slip angle → correct-sign Fy, and Mz opposes the slip angle (pneumatic trail).
//      • load sens.   — a heavier load produces a larger peak force.
//    Known limitation (documented, not hidden): the self-aligning-torque MAGNITUDE is under-resolved — the contact patch is
//    only a handful of segments long, so the fore-aft pneumatic-trail lever arm is coarse; Mz comes out with the right sign
//    but ~10× small. Finer circumferential meshing (or a tread-stiffness gradient) closes it, at proportional cost.

#include "XPBDSoftTyre.h"
#include "PacejkaMagicFormula.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace Frontier::Vehicle;

//------------------------------------------------------------------------------------------------------------------------
static int g_pass = 0, g_fail = 0;
static void Check(const char* name, bool ok, const std::string& detail = "")
{
    if (ok) { ++g_pass; std::printf("  [PASS] %s\n", name); }
    else    { ++g_fail; std::printf("  [FAIL] %s   %s\n", name, detail.c_str()); }
}

static XPBDSoftTyre::GroundQuery g_flat =
    [](const Vec3& p, Vec3& s, Vec3& n) { s = {p.x, p.y, 0.0f}; n = {0, 0, 1}; return true; };

struct Measured { double Fx = 0, Fy = 0, Fz = 0, Mz = 0; };

// Settle a static loaded tyre and return the steady vertical load.
static double SettleFz(XPBDSoftTyre& t, float hubZ)
{
    const float dt = 1.0f / 2000.0f;
    double fz = 0; int n = 0;
    for (int i = 0; i < 800; ++i) { t.Step(dt, 12, {0, 0, hubZ}, Quat{}, {0, 0, 0}, g_flat);
        if (i >= 500) { fz += t.Reaction().Force.z; ++n; } }
    return (n > 0) ? fz / n : 0.0;
}

// Roll a loaded tyre with a given belt velocity and spin rate; average the reaction over an INTEGER number of wheel
// revolutions at the tail (this removes the periodic ripple from discrete nodes entering/leaving the contact patch).
static Measured Roll(XPBDSoftTyre& t, float hubZ, float Vx, float Vlat, float Omega, int revsAvg = 3, int revsWarm = 3)
{
    const float dt = 1.0f / 2000.0f;
    const unsigned sub = 12u;
    const float revPeriod = 2.0f * 3.14159265f / std::max(std::fabs(Omega), 1e-3f);
    const int stepsPerRev = std::max(1, static_cast<int>(revPeriod / dt + 0.5f));
    const int warm = revsWarm * stepsPerRev;
    const int total = warm + revsAvg * stepsPerRev;

    float phi = 0.0f; const Vec3 hub{0, 0, hubZ}; const Vec3 belt{-Vx, -Vlat, 0};
    Measured m; int n = 0;
    for (int i = 0; i < total; ++i)
    {
        phi += Omega * dt;
        const Quat q = Quat::AxisAngle({0, 1, 0}, phi);
        t.Step(dt, sub, hub, q, belt, g_flat);
        if (i >= warm) { const TyreReaction& r = t.Reaction();
            m.Fx += r.Force.x; m.Fy += r.Force.y; m.Fz += r.Force.z; m.Mz += r.Mz; ++n; }
    }
    if (n > 0) { m.Fx /= n; m.Fy /= n; m.Fz /= n; m.Mz /= n; }
    return m;
}

// Find the free-rolling spin rate (Fx = 0) by scanning Omega and linearly interpolating the zero-crossing of Fx(Omega).
// (A discrete brush patch has a slightly noisy Fx, so we regress the sign change rather than trust a single grid point.)
static float FreeRollOmega(XPBDSoftTyre& t, float hubZ, float Vx, float R)
{
    const float base = Vx / R;
    float prevO = 0, prevFx = 0; bool have = false;
    float fallback = base, fbAbs = 1e18f;
    for (float mult = 0.94f; mult <= 1.061f; mult += 0.01f)
    {
        const float O = base * mult;
        t.Build(t.Params(), {0, 0, hubZ}, Quat{}); SettleFz(t, hubZ);
        const float fx = static_cast<float>(Roll(t, hubZ, Vx, 0, O, 3, 3).Fx);
        if (std::fabs(fx) < fbAbs) { fbAbs = std::fabs(fx); fallback = O; }
        if (have && prevFx < 0.0f && fx >= 0.0f)                 // bracketed the zero crossing (Fx rises with spin)
            return prevO + (O - prevO) * (0.0f - prevFx) / (fx - prevFx);
        prevO = O; prevFx = fx; have = true;
    }
    return fallback;
}

//------------------------------------------------------------------------------------------------------------------------
int main()
{
    std::printf("========================================================================\n");
    std::printf(" Project-Tractrix — Phase 2 XPBD soft-tyre vs Pacejka cross-validation\n");
    std::printf("========================================================================\n");

    SoftTyreParameters P;   // ships pre-calibrated (see XPBDSoftTyre.h defaults)
    PacejkaMagicFormula M; PacejkaParameters pp; M.AssignParameters(pp);

    // ── stability / build ────────────────────────────────────────────────────────────────────────────────────────
    std::printf("\n[1] Build & numerical stability\n");
    XPBDSoftTyre tyre; tyre.Build(P, {0, 0, P.Radius - 0.008f}, Quat{});
    Check("Tyre builds with the expected node count", tyre.Nodes().size() == P.RingCount * P.SegmentCount);
    const double Fz = SettleFz(tyre, P.Radius - 0.008f);
    bool finite = std::isfinite(Fz);
    for (const auto& nd : tyre.Nodes()) finite = finite && std::isfinite(nd.Position.x) && std::isfinite(nd.Position.z);
    Check("Loaded carcass settles to finite state (no blow-up)", finite);
    Check("Contact patch resolves multiple nodes", tyre.Reaction().ContactCount >= 9,
          "contacts=" + std::to_string(tyre.Reaction().ContactCount));

    // ── vertical stiffness ───────────────────────────────────────────────────────────────────────────────────────
    std::printf("\n[2] Vertical load capacity\n");
    XPBDSoftTyre vt;
    vt.Build(P, {0, 0, P.Radius - 0.004f}, Quat{}); const double fzLo = SettleFz(vt, P.Radius - 0.004f);
    vt.Build(P, {0, 0, P.Radius - 0.012f}, Quat{}); const double fzHi = SettleFz(vt, P.Radius - 0.012f);
    Check("Load positive and increases with penetration", fzLo > 0 && fzHi > fzLo,
          "Fz(4mm)=" + std::to_string(fzLo) + " Fz(12mm)=" + std::to_string(fzHi));

    // Operating point ≈ 5 kN (near Pacejka Fz0), and the free-rolling spin rate.
    const float hubZ = P.Radius - 0.008f;
    tyre.Build(P, {0, 0, hubZ}, Quat{}); const double Fz0 = SettleFz(tyre, hubZ);
    const float Vx = 15.0f, Rroll = P.Radius - 0.008f;
    const float Ostar = FreeRollOmega(tyre, hubZ, Vx, Rroll);
    tyre.Build(P, {0, 0, hubZ}, Quat{}); SettleFz(tyre, hubZ);
    const Measured freeRoll = Roll(tyre, hubZ, Vx, 0, Ostar);
    std::printf("    operating Fz=%.0f N, free-roll Omega*=%.2f rad/s, residual Fx=%.0f N\n", Fz0, Ostar, freeRoll.Fx);
    Check("Free-rolling residual |Fx| small vs load", std::fabs(freeRoll.Fx) < 0.15 * Fz0,
          "Fx=" + std::to_string(freeRoll.Fx) + " Fz=" + std::to_string(Fz0));

    // ── longitudinal Fx(κ) ───────────────────────────────────────────────────────────────────────────────────────
    std::printf("\n[3] Longitudinal Fx(kappa) vs Pacejka @ Fz=%.0f\n", Fz0);
    double xpbdPeakFx = 0; float xpbdPeakK = 0; double pacPeakFx = 0;
    double fxAtLowSlip = 0, pacAtLowSlip = 0;
    const float kList[] = {0.03f, 0.06f, 0.10f, 0.16f, 0.22f};
    for (float k : kList)
    {
        tyre.Build(P, {0, 0, hubZ}, Quat{}); SettleFz(tyre, hubZ);
        const Measured m = Roll(tyre, hubZ, Vx, 0, Ostar * (1.0f + k));
        const double pac = M.LongitudinalForce(k, 0, static_cast<float>(Fz0));
        std::printf("    k=%.2f  XPBD Fx=%+8.0f   Pacejka=%+8.0f\n", k, m.Fx, pac);
        if (m.Fx > xpbdPeakFx) { xpbdPeakFx = m.Fx; xpbdPeakK = k; }
        if (pac > pacPeakFx) pacPeakFx = pac;
        if (k == 0.03f) { fxAtLowSlip = m.Fx; pacAtLowSlip = pac; }
    }
    Check("Fx positive for drive slip (correct sign)", xpbdPeakFx > 0);
    Check("Fx peak within 0.5-1.3x Pacejka peak", xpbdPeakFx > 0.5 * pacPeakFx && xpbdPeakFx < 1.3 * pacPeakFx,
          "XPBD=" + std::to_string(xpbdPeakFx) + " Pac=" + std::to_string(pacPeakFx));
    Check("Fx peak at plausible slip (0.05-0.30)", xpbdPeakK >= 0.05f && xpbdPeakK <= 0.30f,
          "peakK=" + std::to_string(xpbdPeakK));
    Check("Longitudinal slip stiffness within 0.4-1.6x Pacejka",
          fxAtLowSlip > 0.4 * pacAtLowSlip && fxAtLowSlip < 1.6 * pacAtLowSlip,
          "XPBD=" + std::to_string(fxAtLowSlip) + " Pac=" + std::to_string(pacAtLowSlip));

    // ── lateral Fy(α) & Mz ───────────────────────────────────────────────────────────────────────────────────────
    std::printf("\n[4] Lateral Fy(alpha) & Mz vs Pacejka @ Fz=%.0f\n", Fz0);
    double xpbdPeakFy = 0; double pacPeakFy = 0; double fyLow = 0, pacFyLow = 0; double mzLow = 0, pacMzLow = 0;
    const float aDeg[] = {2.0f, 4.0f, 6.0f, 8.0f};
    for (float ad : aDeg)
    {
        const float ar = ad * 3.14159265f / 180.0f, Vlat = Vx * std::tan(ar);
        tyre.Build(P, {0, 0, hubZ}, Quat{}); SettleFz(tyre, hubZ);
        const Measured m = Roll(tyre, hubZ, Vx, Vlat, Ostar);
        const TyreForces cf = M.Combined(0, ar, 0, static_cast<float>(Fz0));
        const double fyX = -m.Fy, mzX = -m.Mz;   // sign-align to Pacejka convention
        std::printf("    a=%.0f deg  XPBD Fy=%+8.0f Mz=%+7.1f   Pacejka Fy=%+8.0f Mz=%+7.1f\n", ad, fyX, mzX, cf.Fy, cf.Mz);
        if (fyX > xpbdPeakFy) xpbdPeakFy = fyX;
        if (cf.Fy > pacPeakFy) pacPeakFy = cf.Fy;
        if (ad == 2.0f) { fyLow = fyX; pacFyLow = cf.Fy; mzLow = mzX; pacMzLow = cf.Mz; }
    }
    Check("Fy positive for positive slip angle (correct sign)", fyLow > 0);
    Check("Fy peak within 0.5-1.3x Pacejka peak", xpbdPeakFy > 0.5 * pacPeakFy && xpbdPeakFy < 1.3 * pacPeakFy,
          "XPBD=" + std::to_string(xpbdPeakFy) + " Pac=" + std::to_string(pacPeakFy));
    Check("Cornering stiffness within 0.4-1.6x Pacejka",
          fyLow > 0.4 * pacFyLow && fyLow < 1.6 * pacFyLow,
          "XPBD=" + std::to_string(fyLow) + " Pac=" + std::to_string(pacFyLow));
    Check("Self-aligning torque has Pacejka sign (opposes slip angle; magnitude under-resolved — see header note)",
          mzLow != 0 && (mzLow < 0) == (pacMzLow < 0),
          "XPBD Mz=" + std::to_string(mzLow) + " Pac Mz=" + std::to_string(pacMzLow));

    // ── load sensitivity ─────────────────────────────────────────────────────────────────────────────────────────
    std::printf("\n[5] Load sensitivity\n");
    auto peakFxAt = [&](float pen) {
        XPBDSoftTyre lt; lt.Build(P, {0, 0, P.Radius - pen}, Quat{});
        const float hz = P.Radius - pen; SettleFz(lt, hz);
        const float Os = (Vx / (P.Radius - pen));
        double pk = 0;
        for (float k : {0.10f, 0.16f}) { lt.Build(P, {0, 0, hz}, Quat{}); SettleFz(lt, hz);
            const Measured m = Roll(lt, hz, Vx, 0, Os * (1.0f + k)); pk = std::max(pk, m.Fx); }
        return pk;
    };
    const double pkLight = peakFxAt(0.005f), pkHeavy = peakFxAt(0.011f);
    Check("Peak Fx grows with vertical load", pkHeavy > pkLight,
          "light=" + std::to_string(pkLight) + " heavy=" + std::to_string(pkHeavy));

    std::printf("\n------------------------------------------------------------------------\n");
    std::printf(" RESULT: %d passed, %d failed\n", g_pass, g_fail);
    std::printf("------------------------------------------------------------------------\n");
    return g_fail == 0 ? 0 : 1;
}
