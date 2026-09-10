//============================================================================================================================================
// 📦 Scratchpad/CelestialTierProof.cpp — one ladder, one translation, and an Auto loop that does not oscillate
//============================================================================================================================================
// Celestial port, step 9.
//
// Two things are proved. The mapping from a tier to celestial settings must be complete, monotonic and unique;
//    and the Auto ladder must be a control law that settles rather than a switch that flaps.
//
// The Auto tests are driven by SUPPLIED frame rates rather than a clock. That is deliberate: the thing at risk is
//    the control law, and a test that depended on real timing would be both unreproducible and untestable here,
//    where there is no GPU to produce a frame rate at all.

#include "DisplayPresentation/CelestialTier.h"

#include <cmath>
#include <cstdio>
#include <cstdint>

using namespace Frontier;

namespace {
int Failures = 0;
void Expect(bool Condition, const char* What)
{
    if (!Condition) ++Failures;
    std::printf("  %-68s %s\n", What, Condition ? "PASS" : "FAIL");
}
const char* Name(FidelityCategory C)
{
    switch (C)
    {
        case FidelityCategory::MinimalFidelity:   return "Minimal";
        case FidelityCategory::EconomyFidelity:   return "Economy";
        case FidelityCategory::StandardFidelity:  return "Standard";
        case FidelityCategory::UltraFidelity:     return "Ultra";
        case FidelityCategory::ReferenceFidelity: return "Reference";
        default:                                  return "?";
    }
}
} // namespace

int main()
{
    std::printf("\nCelestialTier — the ladder reaches the simulation, and Auto holds a target\n");
    for (int I = 0; I < 108; ++I) std::putchar('=');
    std::printf("\n\n");

    FidelityClassifier Classifier;

    // ── ① the translation is complete and monotonic ────────────────────────────────────────────────────────────
    std::printf("1. every tier maps to a complete celestial budget\n");
    std::printf("     %-11s %6s %6s %5s %7s %6s %6s %7s %10s\n",
                "tier", "cloud", "local", "taps", "godray", "atmN", "atmL", "stars", "cost");
    CelestialBudget Budgets[AutoTierLadder::kTierCount];
    float Costs[AutoTierLadder::kTierCount];
    for (uint32_t T = 0; T < AutoTierLadder::kTierCount; ++T)
    {
        const FidelityCriteria Criteria = Classifier.ConstructCriteria(AutoTierLadder::kOrder[T]);
        Budgets[T] = CelestialTier::BudgetFor(Criteria);
        Costs[T]   = CelestialTier::RelativeCost(Budgets[T]);
        std::printf("     %-11s %6u %6u %5u %6u %6u %7u %10.1f\n",
                    Name(AutoTierLadder::kOrder[T]),
                    Budgets[T].Volumetrics.CloudSteps, Budgets[T].Volumetrics.LocalSteps,
                    Budgets[T].Volumetrics.LightTaps,
                    Budgets[T].AtmosphereSamples, Budgets[T].AtmosphereLightSamples,
                    Budgets[T].StarLayers, Costs[T]);
    }

    bool Complete = true;
    for (uint32_t T = 0; T < AutoTierLadder::kTierCount; ++T)
    {
        // A field left at its struct default is the translation forgetting one, which is exactly how a budget
        //    silently stops following the tier.
        if (Budgets[T].Volumetrics.CloudSteps == 0u || Budgets[T].AtmosphereSamples == 0u ||
            Budgets[T].StarLayers == 0u || Budgets[T].ParticleCapacity == 0u) Complete = false;
    }
    Expect(Complete, "no budget field is left unset by the translation");

    bool CostRises = true;
    for (uint32_t T = 1; T < AutoTierLadder::kTierCount; ++T)
        if (Costs[T] <= Costs[T - 1]) CostRises = false;
    Expect(CostRises, "relative cost rises strictly with the tier, so Auto can order them");

    Expect(Budgets[4].Volumetrics.CloudSteps == 64u && Budgets[4].AtmosphereSamples == 32u,
           "Reference carries the Cinematic-collapsed figures from the plan's mapping");

    // The translation must be a pure function of the tier: same input, same output, every time.
    {
        const FidelityCriteria C = Classifier.ConstructCriteria(FidelityCategory::StandardFidelity);
        const CelestialBudget A = CelestialTier::BudgetFor(C);
        const CelestialBudget B = CelestialTier::BudgetFor(C);
        Expect(A.Volumetrics.CloudSteps == B.Volumetrics.CloudSteps &&
               std::fabs(CelestialTier::RelativeCost(A) - CelestialTier::RelativeCost(B)) < 1e-6f,
               "the translation is pure — the same tier always yields the same budget");
    }

    // ── ② Auto is opt-in ───────────────────────────────────────────────────────────────────────────────────────
    std::printf("\n2. Auto does nothing until it is asked\n");
    {
        AutoTierLadder Ladder;
        Ladder.Reset(FidelityCategory::StandardFidelity);
        AutoTierSettings Settings{};        // Enabled defaults to false
        for (int I = 0; I < 600; ++I) Ladder.Observe(Settings, 5.0f, 1.0f / 60.0f);
        std::printf("     10 s at a catastrophic 5 fps with Auto off: %u changes\n", Ladder.Changes());
        Expect(Ladder.Changes() == 0u && Ladder.Tier() == FidelityCategory::StandardFidelity,
               "a disabled ladder never moves, however bad the frame rate");
    }

    // ── ③ it falls when it must, and climbs when it may ────────────────────────────────────────────────────────
    std::printf("\n3. the ladder moves in the right direction\n");
    {
        AutoTierSettings Settings{};
        Settings.Enabled = true; Settings.TargetFps = 45.0f;

        AutoTierLadder Falling;
        Falling.Reset(FidelityCategory::ReferenceFidelity);
        for (int I = 0; I < 1200; ++I) Falling.Observe(Settings, 12.0f, 1.0f / 60.0f);
        std::printf("     20 s at 12 fps from Reference: settles at %s\n", Name(Falling.Tier()));
        Expect(Falling.Tier() == FidelityCategory::MinimalFidelity,
               "a machine far below target falls all the way to Minimal");

        AutoTierLadder Rising;
        Rising.Reset(FidelityCategory::MinimalFidelity);
        for (int I = 0; I < 3000; ++I) Rising.Observe(Settings, 200.0f, 1.0f / 60.0f);
        std::printf("     50 s at 200 fps from Minimal: settles at %s\n", Name(Rising.Tier()));
        Expect(Rising.Tier() == FidelityCategory::ReferenceFidelity,
               "a machine with headroom climbs all the way to Reference");

        // And it must STOP at the ends rather than walking off them.
        for (int I = 0; I < 600; ++I) Rising.Observe(Settings, 200.0f, 1.0f / 60.0f);
        Expect(Rising.Tier() == FidelityCategory::ReferenceFidelity, "and stops at the top");
        for (int I = 0; I < 600; ++I) Falling.Observe(Settings, 12.0f, 1.0f / 60.0f);
        Expect(Falling.Tier() == FidelityCategory::MinimalFidelity, "and at the bottom");
    }

    // ── ④ it settles ───────────────────────────────────────────────────────────────────────────────────────────
    // A steady frame rate inside the band must stop the ladder dead. This is the difference between a control law
    // and a switch: a switch keeps acting on the same input.
    std::printf("\n4. a steady frame rate settles the ladder\n");
    {
        AutoTierSettings Settings{};
        Settings.Enabled = true; Settings.TargetFps = 45.0f;
        AutoTierLadder Ladder;
        Ladder.Reset(FidelityCategory::StandardFidelity);
        for (int I = 0; I < 3600; ++I) Ladder.Observe(Settings, 46.0f, 1.0f / 60.0f);
        std::printf("     60 s at a steady 46 fps: %u changes\n", Ladder.Changes());
        Expect(Ladder.Changes() == 0u, "a frame rate on target never moves the tier");
    }

    // ── ⑤ it does not oscillate ────────────────────────────────────────────────────────────────────────────────
    // The failure mode that matters. A machine that runs a little fast at tier N and a little slow at tier N+1
    // will make a naive ladder flip between them forever, and the flicker is far worse than either tier. The
    // hysteresis is what prevents it: climbing costs two consecutive intervals of headroom, falling costs one.
    std::printf("\n5. a borderline machine does not flap between two tiers\n");
    {
        AutoTierSettings Settings{};
        Settings.Enabled = true; Settings.TargetFps = 45.0f;
        AutoTierLadder Ladder;
        Ladder.Reset(FidelityCategory::StandardFidelity);

        // The pathological input: alternating just-fast and just-slow intervals straddling both thresholds.
        for (int I = 0; I < 4000; ++I)
        {
            const bool Fast = ((I / 90) % 2) == 0;
            Ladder.Observe(Settings, Fast ? 65.0f : 36.0f, 1.0f / 60.0f);
        }
        std::printf("     66 s alternating 65 / 36 fps: %u changes, ended at %s\n",
                    Ladder.Changes(), Name(Ladder.Tier()));
        // Without hysteresis this alternation produces a change on nearly every interval — about 44 of them.
        Expect(Ladder.Changes() <= 8u, "the ladder does not chase an alternating frame rate");

        // ⚠️ The run above settles at Minimal, and a ladder resting on the floor cannot flap even if it wants
        //    to — so on its own that result would prove nothing. Repeat the alternation with the frame rates
        //    centred on the band, from a tier with room to move in BOTH directions, so the floor cannot mask it.
        AutoTierLadder Middle;
        Middle.Reset(FidelityCategory::StandardFidelity);
        for (int I = 0; I < 4000; ++I)
        {
            const bool Fast = ((I / 90) % 2) == 0;
            Middle.Observe(Settings, Fast ? 62.0f : 40.0f, 1.0f / 60.0f);   // 40 is INSIDE the band
        }
        std::printf("     66 s alternating 62 / 40 fps (both near target): %u changes, ended at %s\n",
                    Middle.Changes(), Name(Middle.Tier()));
        Expect(Middle.Changes() <= 4u && Middle.Tier() != FidelityCategory::MinimalFidelity,
               "and does not drift when only one side of the alternation is out of band");
    }

    // ── ⑥ falling is prompter than climbing ────────────────────────────────────────────────────────────────────
    // Dropped frames are a fault to fix now; headroom is not. The ladder must be asymmetric, and by construction
    // rather than by tuning.
    std::printf("\n6. it falls promptly and climbs cautiously\n");
    {
        AutoTierSettings Settings{};
        Settings.Enabled = true; Settings.TargetFps = 45.0f;

        AutoTierLadder Down;
        Down.Reset(FidelityCategory::StandardFidelity);
        int IntervalsToFall = 0;
        for (int I = 0; I < 6000 && Down.Changes() == 0u; ++I)
        {
            Down.Observe(Settings, 20.0f, 1.0f / 60.0f);
            ++IntervalsToFall;
        }
        AutoTierLadder Up;
        Up.Reset(FidelityCategory::StandardFidelity);
        int IntervalsToRise = 0;
        for (int I = 0; I < 6000 && Up.Changes() == 0u; ++I)
        {
            Up.Observe(Settings, 200.0f, 1.0f / 60.0f);
            ++IntervalsToRise;
        }
        std::printf("     frames before the first step: down %d, up %d\n", IntervalsToFall, IntervalsToRise);
        Expect(IntervalsToRise > IntervalsToFall,
               "climbing takes longer than falling, so a fault is fixed before headroom is spent");
    }

    std::printf("\n");
    for (int I = 0; I < 108; ++I) std::putchar('=');
    std::printf("\n%s\n\n", Failures == 0 ? "  the ladder behaves" : "  THE LADDER DOES NOT BEHAVE");
    return Failures == 0 ? 0 : 1;
}
