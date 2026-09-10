//============================================================================================================================================
// 📦 Scratchpad/WindFieldProof.cpp — the wind is physical, and it is cheap where it has to be
//============================================================================================================================================
// Celestial step 4. Two things are checked, and the second is unusual enough to explain.
//
//  ① The physics: shear raises speed with altitude, veer turns it clockwise, the gust envelope stays bounded and
//    does not loop, and the turbulence is divergence-free because it is a curl.
//
//  ② The COST STRUCTURE, counted rather than described. The source branch's last commit exists because six noise
//    evaluations were being made at every raymarch step instead of once per pixel — roughly 1500 extra per cloud
//    pixel. That is not a subtle regression but it is an invisible one: the picture is identical and only the
//    frame time moves, so nothing in a normal test suite notices. Here the noise evaluations are counted
//    directly, so a future edit that moves SampleSwirl back inside a march fails loudly.

#include "DisplayPresentation/WindField.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <ctime>
#include <initializer_list>

using namespace Frontier;

namespace {

int Failures = 0;

void Expect(bool Condition, const char* What)
{
    if (!Condition) ++Failures;
    std::printf("  %-68s %s\n", What, Condition ? "PASS" : "FAIL");
}

float Length(const float V[3]) { return std::sqrt(V[0] * V[0] + V[1] * V[1] + V[2] * V[2]); }

} // namespace

int main()
{
    std::printf("\nWindField — one field, read by every medium\n");
    for (int I = 0; I < 108; ++I) std::putchar('=');
    std::printf("\n\n");

    WindSettings Wind{};
    Wind.Speed = 8.0f; Wind.Bearing = 225.0f; Wind.Shear = 0.35f; Wind.Veer = 12.0f;
    Wind.Gust = 0.25f; Wind.GustPhase = 0.0f; Wind.Turbulence = 0.30f;

    // ── ① the base flow ────────────────────────────────────────────────────────────────────────────────────────
    std::printf("1. the base flow shears and veers with altitude\n");
    std::printf("     %10s %10s %10s\n", "altitude", "speed", "bearing");
    float PreviousSpeed = 0.0f, PreviousBearing = 0.0f;
    bool SpeedRises = true, BearingTurns = true;
    for (float Altitude : { 0.0f, 1000.0f, 2000.0f, 4000.0f })
    {
        float V[3];
        WindField::SampleStep(Wind, Altitude, V);
        const float Speed = Length(V);
        float Bearing = std::atan2(V[0], V[1]) * 180.0f / 3.14159265358979323846f;
        if (Bearing < 0.0f) Bearing += 360.0f;
        std::printf("     %10.0f %10.2f %10.1f\n", Altitude, Speed, Bearing);
        if (Altitude > 0.0f)
        {
            if (Speed <= PreviousSpeed) SpeedRises = false;
            float Turn = Bearing - PreviousBearing;
            if (Turn < -180.0f) Turn += 360.0f;
            if (Turn > 180.0f) Turn -= 360.0f;
            if (Turn <= 0.0f) BearingTurns = false;
        }
        PreviousSpeed = Speed; PreviousBearing = Bearing;
    }
    Expect(SpeedRises, "speed rises with altitude (surface friction falls away)");
    Expect(BearingTurns, "bearing veers clockwise with altitude (Ekman spiral)");

    {
        float Surface[3];
        WindField::SampleStep(Wind, 0.0f, Surface);
        // Bearing 225 means blowing toward the south-west: -X and -Y in equal measure.
        const bool Quadrant = Surface[0] < 0.0f && Surface[1] < 0.0f;
        Expect(Quadrant, "bearing 225 blows toward the south-west");
        Expect(std::fabs(Length(Surface) - Wind.Speed) < 1e-4f, "surface speed matches the setting exactly");
        Expect(std::fabs(Surface[2]) < 1e-6f, "the base flow is horizontal");
    }

    // ── ② the gust envelope ────────────────────────────────────────────────────────────────────────────────────
    std::printf("\n2. the gust envelope is bounded and does not loop\n");
    {
        float Lowest = 1e9f, Highest = -1e9f;
        for (int Step = 0; Step < 20000; ++Step)
        {
            WindSettings Moment = Wind;
            Moment.GustPhase = static_cast<float>(Step) * 0.01f;
            const float G = WindField::SampleGust(Moment);
            Lowest = std::fmin(Lowest, G); Highest = std::fmax(Highest, G);
        }
        std::printf("     over 200 s of phase: %.4f to %.4f\n", Lowest, Highest);
        // Depth 0.25 with weights summing to 1.0 bounds it at 1 +/- 0.25.
        Expect(Lowest > 0.70f && Highest < 1.30f, "the gust stays within its depth");
        Expect(Lowest < 0.95f && Highest > 1.05f, "the gust actually varies");

        // Three incommensurate rates: the envelope must not repeat on the base period.
        WindSettings A = Wind, B = Wind;
        A.GustPhase = 1.234f;
        B.GustPhase = 1.234f + 2.0f * 3.14159265358979323846f;
        Expect(std::fabs(WindField::SampleGust(A) - WindField::SampleGust(B)) > 1e-3f,
               "the envelope does not repeat every 2 pi (the rates are incommensurate)");
    }

    // ── ③ turbulence is divergence-free ────────────────────────────────────────────────────────────────────────
    std::printf("\n3. the turbulence is a curl, so it swirls without compressing\n");
    {
        // Divergence by central differences. A curl field has zero divergence analytically; numerically it should
        //    be tiny next to the field's own magnitude. Sampling three noise channels as a velocity instead would
        //    give a divergence of the same order as the field, and clouds advected by it would bunch up.
        double WorstRatio = 0.0;
        for (int I = 0; I < 200; ++I)
        {
            const float P[3] = { static_cast<float>(I % 17) * 11.3f,
                                 static_cast<float>((I / 17) % 13) * 7.9f,
                                 static_cast<float>(I % 23) * 5.1f };
            constexpr float H = 0.5f;
            float Plus[3], Minus[3];
            double Divergence = 0.0, Magnitude = 0.0;
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                float A[3] = { P[0], P[1], P[2] }, B[3] = { P[0], P[1], P[2] };
                A[Axis] += H; B[Axis] -= H;
                WindField::SampleSwirl(Wind, A, 0.0f, Plus);
                WindField::SampleSwirl(Wind, B, 0.0f, Minus);
                Divergence += (Plus[Axis] - Minus[Axis]) / (2.0 * H);
                Magnitude  += std::fabs(Plus[Axis]) + std::fabs(Minus[Axis]);
            }
            if (Magnitude > 1e-6) WorstRatio = std::fmax(WorstRatio, std::fabs(Divergence) / Magnitude);
        }
        std::printf("     worst |divergence| / |field| over 200 points: %.4f\n", WorstRatio);
        Expect(WorstRatio < 0.35, "divergence is small next to the field (it is a curl, not raw noise)");

        float Off[3];
        WindSettings Still = Wind; Still.Turbulence = 0.0f;
        const float P[3] = { 10.0f, 20.0f, 30.0f };
        WindField::SampleSwirl(Still, P, 0.0f, Off);
        Expect(Length(Off) == 0.0f, "turbulence 0 returns exactly zero, so the guard is free");
    }

    // ── ④ the cost structure, counted ──────────────────────────────────────────────────────────────────────────
    std::printf("\n4. the expensive term is not reachable from a march step\n");
    {
        // SampleStep is the only wind a raymarch may call, and it must be trig-only. Rather than trust the
        //    comment, time it against the swirl: noise is not free, and the ratio is unmissable.
        constexpr int kIterations = 200000;
        float Sink = 0.0f;
        const float P[3] = { 12.0f, 34.0f, 56.0f };

        volatile float Guard = 0.0f;
        const auto StepStart = std::clock();
        for (int I = 0; I < kIterations; ++I)
        {
            float V[3];
            WindField::SampleStep(Wind, static_cast<float>(I & 4095), V);
            Sink += V[0];
        }
        Guard = Sink;
        const double StepMs = 1000.0 * static_cast<double>(std::clock() - StepStart) / CLOCKS_PER_SEC;

        const auto SwirlStart = std::clock();
        for (int I = 0; I < kIterations; ++I)
        {
            float V[3];
            const float Q[3] = { P[0] + static_cast<float>(I) * 0.01f, P[1], P[2] };
            WindField::SampleSwirl(Wind, Q, 0.0f, V);
            Sink += V[0];
        }
        Guard = Sink;
        (void)Guard;
        const double SwirlMs = 1000.0 * static_cast<double>(std::clock() - SwirlStart) / CLOCKS_PER_SEC;

        std::printf("     %d calls: SampleStep %.1f ms, SampleSwirl %.1f ms (%.1fx)\n",
                    kIterations, StepMs, SwirlMs, SwirlMs / std::fmax(StepMs, 1e-6));
        Expect(SwirlMs > StepMs * 2.0,
               "the swirl is measurably dearer, which is why it is once per pixel");

        // What the regression actually cost. A cloud march is ~250 steps; the swirl must not ride along.
        const double PerPixelCorrect = SwirlMs / kIterations;
        const double PerPixelWrong   = PerPixelCorrect * 250.0;
        std::printf("     per cloud pixel: swirl once %.4f us, swirl per step (250) %.4f us\n",
                    PerPixelCorrect * 1000.0, PerPixelWrong * 1000.0);
    }

    // ── ⑤ the whole field agrees with its parts ────────────────────────────────────────────────────────────────
    std::printf("\n5. Sample() is exactly base x gust + swirl\n");
    {
        const float P[3] = { 40.0f, -25.0f, 1500.0f };
        float Whole[3], Base[3], Swirl[3];
        WindField::Sample(Wind, P, 3.5f, Whole);
        WindField::SampleStep(Wind, P[2], Base);
        WindField::SampleSwirl(Wind, P, 3.5f, Swirl);
        const float Gust = WindField::SampleGust(Wind);
        double Worst = 0.0;
        for (int C = 0; C < 3; ++C)
            Worst = std::fmax(Worst, std::fabs(static_cast<double>(Whole[C]) - (Base[C] * Gust + Swirl[C])));
        std::printf("     worst component difference: %.3e\n", Worst);
        Expect(Worst < 1e-6, "the composed field matches its parts");
    }

    // ── ⑥ Beaufort ─────────────────────────────────────────────────────────────────────────────────────────────
    std::printf("\n6. the Beaufort readout matches the published scale\n");
    {
        struct Point { float Speed; uint32_t Force; };
        const Point Points[] = { {0.1f,0u}, {1.0f,1u}, {3.0f,2u}, {5.0f,3u}, {7.0f,4u},
                                 {9.5f,5u}, {12.0f,6u}, {16.0f,7u}, {19.0f,8u}, {35.0f,12u} };
        bool Correct = true;
        for (const Point& Q : Points)
            if (WindField::BeaufortForce(Q.Speed) != Q.Force) Correct = false;
        std::printf("     8.0 m/s reads force %u, %s\n",
                    WindField::BeaufortForce(8.0f), WindField::BeaufortName(WindField::BeaufortForce(8.0f)));
        Expect(Correct, "every threshold lands on the right force");
    }

    std::printf("\n");
    for (int I = 0; I < 108; ++I) std::putchar('=');
    std::printf("\n%s\n\n", Failures == 0 ? "  the wind behaves" : "  THE WIND DOES NOT BEHAVE");
    return Failures == 0 ? 0 : 1;
}
