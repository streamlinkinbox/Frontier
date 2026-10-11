//============================================================================================================================================
// 📦 Exhibits/Workbench/Sky/SunLimbProof.cpp — the sun-coin limb-darkening question, measured and closed (roadmap #6b)
//============================================================================================================================================
// The kernel draws a limb-darkened disc (SkyRecords.slang: SunLimb = mix(1.0, 0.55, smoothstep(0, R, ang)))
//    but the NEE estimator shades every cone sample with CONSTANT radiance (SunEmission() = Q/Ω, uniform cone
//    pdf 1/Ω). §14.3 kept "sun-coin variance" open on the suspicion that this flatness costs something. Two
//    questions, each a number:
//
//    1. VARIANCE — does the flat estimator inject noise, and would weighting samples by the true limb profile
//       remove it? The answer is the reverse of the suspicion: with constant L every cone sample returns the
//       SAME value, so the estimator's variance from the disc's radiance is EXACTLY ZERO — it is the optimal
//       (zero-variance) form for the disc's total flux. Weighting samples by the physical profile at the same
//       uniform pdf would ADD the profile's own relative variance. Measured below.
//
//    2. PENUMBRA SHAPE — the one place flatness is visible in principle. In a penumbra the occluder eats the
//       disc edge-first: with a flat disc the irradiance ramp follows visible AREA; with a limb-darkened disc
//       the centre carries more flux, so the true ramp is steeper mid-penumbra and softer at its ends. The
//       error is a SHAPE difference inside the penumbra band only (total energy is Q either way — the host
//       packs Q from the atmosphere march, not from the profile). Measured below against both the kernel's
//       own drawn profile and the physical (linear limb, u = 0.6) one.
//
//    The verdict the gates pin: the shipped estimator is the zero-variance form, the penumbra shape error
//    peaks at a few percent of the sun's direct term confined to the penumbra band, and the "fix" (radiance-
//    weighted samples) would trade that few-percent SHAPE bias for per-sample noise on EVERY sun-lit pixel.
//    #6b closes as a measured negative: the flat coin stays.
//============================================================================================================================================

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

uint32_t Failures = 0u;

void Expect(bool Condition, const char* Caption)
{
    std::fprintf(stderr, "  %s %s\n", Condition ? "PASS" : "FAIL", Caption);
    if (!Condition) ++Failures;
}

constexpr double kPi = 3.14159265358979323846;

// The shared body: SkyRecords.slang's kSunAngularRadius — the disc, the NEE cone and the CPU raster's disc
//    all read this one figure (0.53° diameter).
constexpr double kSunAngularRadius = 0.53 * (kPi / 180.0) * 0.5;

double SmoothStep(double Edge0, double Edge1, double X)
{
    const double T = std::clamp((X - Edge0) / (Edge1 - Edge0), 0.0, 1.0);
    return T * T * (3.0 - 2.0 * T);
}

// The kernel's DRAWN profile (SkyRecords.slang line 351): 1.0 at centre easing to 0.55 at the rim.
double KernelLimb(double AngleFromCentre)
{
    return 1.0 + (0.55 - 1.0) * SmoothStep(0.0, kSunAngularRadius, AngleFromCentre);
}

// The physical profile at visible wavelengths, the standard linear limb law I(mu)/I(0) = 1 - u(1 - mu) with
//    u = 0.6 (the textbook broadband figure); mu = cos of the heliocentric angle = sqrt(1 - (ang/R)^2).
double PhysicalLimb(double AngleFromCentre)
{
    const double Ratio = std::min(1.0, AngleFromCentre / kSunAngularRadius);
    const double Mu = std::sqrt(std::max(0.0, 1.0 - Ratio * Ratio));
    return 1.0 - 0.6 * (1.0 - Mu);
}

// Mean of a profile over the disc (area-weighted), so a weighted estimator can be normalised to the same Q.
template <typename Profile>
double DiscMean(Profile&& L)
{
    constexpr uint32_t kRings = 4096u;
    double Sum = 0.0, Area = 0.0;
    for (uint32_t I = 0u; I < kRings; ++I)
    {
        const double Ang = (static_cast<double>(I) + 0.5) / kRings * kSunAngularRadius;
        const double Ring = Ang;   // ~flat-disc area element for a 0.27° cone (sin ang ≈ ang)
        Sum  += L(Ang) * Ring;
        Area += Ring;
    }
    return Sum / Area;
}

// Relative standard deviation of a profile over the disc under UNIFORM sampling — exactly the per-sample
//    noise a radiance-weighted estimator (true L per sample, uniform pdf, normalised to Q) would add where
//    the shipped flat estimator adds none.
template <typename Profile>
double DiscRelativeDeviation(Profile&& L)
{
    const double Mean = DiscMean(L);
    constexpr uint32_t kRings = 4096u;
    double Sum2 = 0.0, Area = 0.0;
    for (uint32_t I = 0u; I < kRings; ++I)
    {
        const double Ang = (static_cast<double>(I) + 0.5) / kRings * kSunAngularRadius;
        const double Ring = Ang;
        const double D = L(Ang) / Mean - 1.0;
        Sum2 += D * D * Ring;
        Area += Ring;
    }
    return std::sqrt(Sum2 / Area);
}

// The penumbra ramp: a straight occluder edge at signed offset D from the disc centre (in disc radii,
//    -1 = fully lit onward, +1 = fully dark onward), irradiance = the profile integrated over the VISIBLE
//    part of the disc, normalised so the fully lit disc reads 1. Flat profile = visible-area fraction.
template <typename Profile>
double PenumbraRamp(Profile&& L, double EdgeOffsetRadii)
{
    constexpr uint32_t kGrid = 1024u;
    double Lit = 0.0, Total = 0.0;
    for (uint32_t Y = 0u; Y < kGrid; ++Y)
        for (uint32_t X = 0u; X < kGrid; ++X)
        {
            const double Px = (static_cast<double>(X) + 0.5) / kGrid * 2.0 - 1.0;   // [-1, 1] disc plane
            const double Py = (static_cast<double>(Y) + 0.5) / kGrid * 2.0 - 1.0;
            const double R2 = Px * Px + Py * Py;
            if (R2 > 1.0) continue;
            const double Value = L(std::sqrt(R2) * kSunAngularRadius);
            Total += Value;
            if (Px > EdgeOffsetRadii) Lit += Value;   // the occluder covers everything at or left of the edge
        }
    return Total > 0.0 ? Lit / Total : 0.0;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                       MAIN
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    std::fprintf(stderr, "[SunLimbProof] the sun coin's limb-darkening question - variance, then penumbra shape\n");

    // 1. VARIANCE. The shipped estimator: constant L = Q/Ω, uniform cone — every sample identical, so its
    //    radiance variance is zero by construction. The alternative under suspicion: the true profile per
    //    sample at the same pdf. Its per-sample relative deviation IS the noise it would add.
    {
        const double KernelDeviation   = DiscRelativeDeviation(KernelLimb);
        const double PhysicalDeviation = DiscRelativeDeviation(PhysicalLimb);
        std::fprintf(stderr,
            "  per-sample relative deviation a radiance-weighted coin would ADD (flat form adds 0):\n"
            "    kernel's drawn profile (1.0 -> 0.55): %.4f (%.1f %% of the sun term, EVERY sun-lit pixel)\n"
            "    physical linear limb (u = 0.6):       %.4f (%.1f %%)\n",
            KernelDeviation, KernelDeviation * 100.0, PhysicalDeviation, PhysicalDeviation * 100.0);
        Expect(KernelDeviation > 0.05 && PhysicalDeviation > 0.05,
               "the weighted coin would add measurable per-sample noise where the flat coin adds none");
    }

    // 2. PENUMBRA SHAPE. Sweep the occluder edge across the disc; the worst ramp difference against the
    //    flat disc is the whole visible cost of the flat estimator, and it lives ONLY inside the penumbra
    //    band (one solar diameter of shadow travel — geometry-scaled, millimetres to centimetres indoors).
    {
        double WorstKernel = 0.0, WorstPhysical = 0.0, AtKernel = 0.0, AtPhysical = 0.0;
        for (int Step = -100; Step <= 100; ++Step)
        {
            const double D = static_cast<double>(Step) / 100.0;
            const double Flat     = PenumbraRamp([](double) { return 1.0; }, D);
            const double Kern     = PenumbraRamp(KernelLimb, D);
            const double Phys     = PenumbraRamp(PhysicalLimb, D);
            if (std::fabs(Kern - Flat) > WorstKernel)   { WorstKernel = std::fabs(Kern - Flat);   AtKernel = D; }
            if (std::fabs(Phys - Flat) > WorstPhysical) { WorstPhysical = std::fabs(Phys - Flat); AtPhysical = D; }
        }
        std::fprintf(stderr,
            "  penumbra ramp error of the flat disc (worst over the sweep, fraction of the sun term):\n"
            "    vs the kernel's drawn profile: %.4f (%.2f %%) at edge offset %+.2f radii\n"
            "    vs the physical limb (u=0.6):  %.4f (%.2f %%) at edge offset %+.2f radii\n"
            "    (an 8-bit display step is 0.39 %%; the error is confined to the penumbra band and is zero\n"
            "     outside it - total energy is the host's Q either way)\n",
            WorstKernel, WorstKernel * 100.0, AtKernel, WorstPhysical, WorstPhysical * 100.0, AtPhysical);
        Expect(WorstKernel < 0.05 && WorstPhysical < 0.05,
               "the flat disc's penumbra shape error stays under 5 % of the sun term at its worst point");
        Expect(PenumbraRamp(PhysicalLimb, -1.0) > 0.999 && PenumbraRamp(PhysicalLimb, 1.0) < 0.001,
               "the ramp's ends agree exactly - the error is shape inside the band, never energy");
    }

    // The verdict, as arithmetic: the fix under suspicion trades a <5 % SHAPE bias confined to penumbra
    //    bands for a >5 % PER-SAMPLE noise on every sun-lit pixel in the frame. The flat coin stays.
    std::fprintf(stderr, "[SunLimbProof] %s\n", Failures == 0u ? "every figure agrees" : "FAILURES above");
    return Failures == 0u ? 0 : 1;
}
