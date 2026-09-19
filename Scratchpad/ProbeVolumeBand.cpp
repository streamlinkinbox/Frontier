//============================================================================================================================================
// 📦 Scratchpad/ProbeVolumeBand.cpp — measures the tessellated-volume band against the analytic closed form
//============================================================================================================================================
// Diagnostics only (no assertions): sweeps the span cap and chord of BrepBody::SignedVolume on the bounded asymmetric
//    frustum body, and prints absolute and relative error against the exact conical frustum volume, plus the cost.
//    Used once to choose ScalarCriteria::VerificationChord / VerificationCap from measurement instead of guesswork.
#include "Kernel/BlendSolver.h"
#include <chrono>
#include <cstdio>

using namespace Frontier;

int main()
{
    const EndpointSupport Low{ { 0, 0, 0 }, { 0, 0, 1 }, 2.0, ScalarCriteria::HalfPi };
    const EndpointSupport High{ { 0, 0, 8 }, { 0, 0, -1 }, 1.25, ScalarCriteria::HalfPi };
    const AsymmetricBlendSpecification Spec{ Low, High, AsymmetricSupportKind::UnequalRadialCaps, 0.1, 0.0 };
    const double Exact = ScalarCriteria::Pi * 8.0 * (4.0 + 2.5 + 1.5625) / 3.0;

    Deliver<BrepBody> Body = BlendSolver::ReconstructAsymmetricSupport(Spec);
    if (!Body) { std::printf("reconstruction refused: %s\n", Body.Denial.Detail); return 1; }

    std::printf("\n  measured tessellated volume of a tapered frustum (r 2.0 → 1.25, h 8 m)\n");
    std::printf("  analytic volume %.12f\n\n", Exact);
    std::printf("  %-10s %-7s %18s %12s %12s %10s\n", "chord", "cap", "measured", "abs err", "rel err", "ms");
    const double Chords[] = { ScalarCriteria::ChordTolerance, 1e-5, 1e-6, 1e-7 };
    const int Caps[] = { 32, 64, 128, 256, 512, 1024, 2048 };
    for (double Chord : Chords)
        for (int Cap : Caps)
        {
            const auto Start = std::chrono::steady_clock::now();
            const BodyReport Report = Body.Payload.Validate(Chord, Cap);
            const double Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
            const double Error = std::fabs(Report.Volume - Exact);
            std::printf("  %-10.0e %-7d %18.10f %12.3e %12.3e %10.2f\n", Chord, Cap, Report.Volume, Error, Error / Exact, Ms);
        }
    struct Family { const char* Name; Deliver<BrepBody> Body; double Exact; };
    std::vector<Family> Families;
    Families.push_back({ "box 2x3x4 (all planar)", BrepBody::Box({ 0, 0, 0 }, { 2, 3, 4 }), 24.0 });
    Families.push_back({ "cylinder r=2 h=8 (trimmed caps)", BrepBody::Cylinder({ 0, 0, 0 }, { 0, 0, 1 }, 2.0, 8.0),
                         ScalarCriteria::Pi * 4.0 * 8.0 });
    Families.push_back({ "sphere r=2 (single curved face)", BrepBody::Sphere({ 0, 0, 0 }, 2.0),
                         4.0 / 3.0 * ScalarCriteria::Pi * 8.0 });
    std::printf("\n  %-34s %16s %12s %12s %12s\n", "family", "analytic", "cap32", "cap512", "cap2048");
    for (const Family& F : Families)
    {
        if (!F.Body) { std::printf("  %-34s refused: %s\n", F.Name, F.Body.Denial.Detail); continue; }
        const double A = F.Body.Payload.Validate().Volume;
        const double B = F.Body.Payload.Validate(1e-7, 512).Volume;
        const double C = F.Body.Payload.Validate(1e-7, 2048).Volume;
        std::printf("  %-34s %16.9f %12.3e %12.3e %12.3e\n", F.Name, F.Exact,
                    std::fabs(A - F.Exact) / F.Exact, std::fabs(B - F.Exact) / F.Exact, std::fabs(C - F.Exact) / F.Exact);
    }
    return 0;
}
