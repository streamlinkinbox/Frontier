//============================================================================================================================================
// 📦 Scratchpad/ReSTIRSelectionIdentity.cpp — proves the carried selection p̂ equals the re-derived one, bit for bit
//============================================================================================================================================
// #5 replaces four PHatFull evaluations with a carried value, on the argument that p̂ of the current selection is
//    already known: the selection only changes at a resample, and the winner's p̂ was computed to decide it won.
//
// That argument is exactly the kind that reads as obviously true and is not. Earlier this round a comparable
//    "provably exact" shadow shortcut turned out to shift 425 pixels, because the reasoning quietly assumed
//    something the code did not guarantee. So this file does not argue — it RUNS both algebras over randomised
//    reservoir states and compares.
//
// Both models below are transcriptions of the kernel's merge sequence. OLD re-derives p̂ with a fresh evaluation at
//    each stage, as ReSTIRViewport.slang did before #5; NEW carries it, as the kernel does now. The target function
//    is a deterministic stand-in — its job is to be a pure function of (light, point), the same property the real
//    PHatFull has — so if the two models ever disagree, the carry is wrong for a reason independent of the BSDF.
//
// The RNG sequence is asserted too, because the merge test consumes RandFloat: a carry that changed how many draws
//    were taken would decorrelate the reservoir and never show up as a p̂ mismatch.

#include <cmath>
#include <cstdint>
#include <cstdio>

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                              DETERMINISTIC SCAFFOLD
//------------------------------------------------------------------------------------------------------------------------

uint32_t PcgHash(uint32_t Input) noexcept
{
    uint32_t State = Input * 747796405u + 2891336453u;
    uint32_t Word  = ((State >> ((State >> 28u) + 4u)) ^ State) * 277803737u;
    return (Word >> 22u) ^ Word;
}

float RandFloat(uint32_t& Seed) noexcept
{
    Seed = PcgHash(Seed);
    return static_cast<float>(Seed) * (1.0f / 4294967296.0f);
}

// A pure function of the sample identity, standing in for PHatFull. Deterministic in (light, point) exactly as the
//    real target function is deterministic in (emission, direction) for a fixed surface.
uint64_t gEvaluations = 0u;
float TargetFunction(uint32_t Light, float PointX) noexcept
{
    ++gEvaluations;
    const float A = static_cast<float>(PcgHash(Light * 2654435761u) & 0xFFFFu) * (1.0f / 65535.0f);
    return 0.05f + A + 0.5f * std::fabs(std::sin(PointX * 3.7f + static_cast<float>(Light)));
}

struct Reservoir
{
    uint32_t SelectedLight;
    float    SelectedPoint;
    float    WeightSum;
    uint32_t SampleCount;
    float    UnbiasedWeight;
};

struct Neighbour        // stands in for both the temporal `prev` and a spatial `neigh`
{
    bool     Valid;
    uint32_t Light;
    float    Point;
    float    W;
    uint32_t M;
};

struct Outcome
{
    float    UnbiasedWeight;
    uint32_t SelectedLight;
    float    SelectedPoint;
    uint32_t SampleCount;
    float    WeightSum;
    uint32_t FinalSeed;      // the RNG must land in the same place, or the reservoir decorrelates downstream
};

constexpr uint32_t kMClamp = 20u;

//------------------------------------------------------------------------------------------------------------------------
//        OLD — p̂ of the selection re-derived at every stage (ReSTIRViewport.slang before #5)
//------------------------------------------------------------------------------------------------------------------------

Outcome RunOld(Reservoir Res, const Neighbour* Merges, uint32_t MergeCount, uint32_t Seed) noexcept
{
    // :853 pHatSel — sets UnbiasedWeight, which :876 then overwrites unconditionally.
    const float PHatSel = TargetFunction(Res.SelectedLight, Res.SelectedPoint);
    if (PHatSel > 0.0f)
        Res.UnbiasedWeight = Res.WeightSum / (static_cast<float>(Res.SampleCount) * PHatSel);

    // :876 W for the final selection.
    const float PHat2 = TargetFunction(Res.SelectedLight, Res.SelectedPoint);
    Res.UnbiasedWeight = PHat2 > 0.0f ? Res.WeightSum / (static_cast<float>(Res.SampleCount) * PHat2) : 0.0f;

    for (uint32_t I = 0u; I < MergeCount; ++I)
    {
        const Neighbour& N = Merges[I];
        if (!N.Valid) continue;

        const uint32_t MCapped = N.M < kMClamp * Res.SampleCount ? N.M : kMClamp * Res.SampleCount;
        const float PNeigh = TargetFunction(N.Light, N.Point);
        const float PSelf  = TargetFunction(Res.SelectedLight, Res.SelectedPoint);      // re-derived
        const float WSelf  = PSelf  * Res.UnbiasedWeight * static_cast<float>(Res.SampleCount);
        const float WNeigh = PNeigh * N.W * static_cast<float>(MCapped);
        const float Total  = WSelf + WNeigh;

        if (Total > 0.0f && RandFloat(Seed) * Total <= WNeigh)
        {
            Res.SelectedLight = N.Light;
            Res.SelectedPoint = N.Point;
        }
        Res.SampleCount += MCapped;
        Res.WeightSum    = Total;

        const float PFinal = TargetFunction(Res.SelectedLight, Res.SelectedPoint);      // re-derived
        Res.UnbiasedWeight = PFinal > 0.0f ? Total / (static_cast<float>(Res.SampleCount) * PFinal) : 0.0f;
    }

    return { Res.UnbiasedWeight, Res.SelectedLight, Res.SelectedPoint, Res.SampleCount, Res.WeightSum, Seed };
}

//------------------------------------------------------------------------------------------------------------------------
//        NEW — p̂ of the selection carried (ReSTIRViewport.slang after #5)
//------------------------------------------------------------------------------------------------------------------------

Outcome RunNew(Reservoir Res, const Neighbour* Merges, uint32_t MergeCount, uint32_t Seed) noexcept
{
    // The dead pHatSel is gone entirely.
    float PSelected = TargetFunction(Res.SelectedLight, Res.SelectedPoint);
    Res.UnbiasedWeight = PSelected > 0.0f ? Res.WeightSum / (static_cast<float>(Res.SampleCount) * PSelected) : 0.0f;

    for (uint32_t I = 0u; I < MergeCount; ++I)
    {
        const Neighbour& N = Merges[I];
        if (!N.Valid) continue;

        const uint32_t MCapped = N.M < kMClamp * Res.SampleCount ? N.M : kMClamp * Res.SampleCount;
        const float PNeigh = TargetFunction(N.Light, N.Point);
        const float PSelf  = PSelected;                                                  // carried
        const float WSelf  = PSelf  * Res.UnbiasedWeight * static_cast<float>(Res.SampleCount);
        const float WNeigh = PNeigh * N.W * static_cast<float>(MCapped);
        const float Total  = WSelf + WNeigh;

        if (Total > 0.0f && RandFloat(Seed) * Total <= WNeigh)
        {
            Res.SelectedLight = N.Light;
            Res.SelectedPoint = N.Point;
            PSelected         = PNeigh;                                                  // the winner's p̂
        }
        Res.SampleCount += MCapped;
        Res.WeightSum    = Total;
        Res.UnbiasedWeight = PSelected > 0.0f ? Total / (static_cast<float>(Res.SampleCount) * PSelected) : 0.0f;
    }

    return { Res.UnbiasedWeight, Res.SelectedLight, Res.SelectedPoint, Res.SampleCount, Res.WeightSum, Seed };
}

}   // namespace

int main()
{
    constexpr uint32_t kTrials = 400000u;
    uint32_t Mismatches = 0u, SeedMismatches = 0u, WinnerChanges = 0u;
    double   WorstDelta = 0.0;
    uint64_t OldEvals = 0u, NewEvals = 0u;

    std::printf("[SelectionIdentity] %u randomised reservoir states, up to 5 merges each\n", kTrials);
    std::printf("[SelectionIdentity] comparing re-derived p-hat against carried p-hat\n\n");

    for (uint32_t T = 0u; T < kTrials; ++T)
    {
        uint32_t S = PcgHash(T * 0x9E3779B9u + 17u);

        Reservoir Res{};
        Res.SelectedLight  = static_cast<uint32_t>(RandFloat(S) * 64.0f);
        Res.SelectedPoint  = RandFloat(S) * 10.0f - 5.0f;
        Res.WeightSum      = RandFloat(S) * 4.0f;
        Res.SampleCount    = 1u + static_cast<uint32_t>(RandFloat(S) * 16.0f);
        Res.UnbiasedWeight = 0.0f;

        // Exercise the degenerate states too: a zero weight sum, and merges that fail validation.
        if ((T & 63u) == 0u) Res.WeightSum = 0.0f;

        Neighbour Merges[5]{};
        const uint32_t MergeCount = static_cast<uint32_t>(RandFloat(S) * 6.0f);
        for (uint32_t I = 0u; I < MergeCount && I < 5u; ++I)
        {
            Merges[I].Valid = RandFloat(S) > 0.25f;
            Merges[I].Light = static_cast<uint32_t>(RandFloat(S) * 64.0f);
            Merges[I].Point = RandFloat(S) * 10.0f - 5.0f;
            Merges[I].W     = RandFloat(S) * 2.0f;
            Merges[I].M     = static_cast<uint32_t>(RandFloat(S) * 40.0f);
            if ((T & 127u) == 0u) Merges[I].W = 0.0f;       // force Total == 0 paths
        }

        const uint32_t Count = MergeCount < 5u ? MergeCount : 5u;

        gEvaluations = 0u;
        const Outcome Old = RunOld(Res, Merges, Count, S);
        OldEvals += gEvaluations;

        gEvaluations = 0u;
        const Outcome New = RunNew(Res, Merges, Count, S);
        NewEvals += gEvaluations;

        if (Old.SelectedLight != New.SelectedLight) ++WinnerChanges;
        if (Old.FinalSeed != New.FinalSeed) ++SeedMismatches;

        const double D = std::fabs(static_cast<double>(Old.UnbiasedWeight) - static_cast<double>(New.UnbiasedWeight));
        if (D > WorstDelta) WorstDelta = D;

        const bool Same = Old.UnbiasedWeight == New.UnbiasedWeight
                       && Old.SelectedLight  == New.SelectedLight
                       && Old.SelectedPoint  == New.SelectedPoint
                       && Old.SampleCount    == New.SampleCount
                       && Old.WeightSum      == New.WeightSum;
        if (!Same) ++Mismatches;
    }

    std::printf("  selection differs in           %u trials\n", WinnerChanges);
    std::printf("  RNG sequence differs in        %u trials\n", SeedMismatches);
    std::printf("  worst |dW|                     %.17g\n", WorstDelta);
    std::printf("  full-state mismatches          %u\n\n", Mismatches);
    std::printf("  target-function evaluations    old %llu, new %llu  (%.1f%% fewer)\n",
                static_cast<unsigned long long>(OldEvals), static_cast<unsigned long long>(NewEvals),
                100.0 * static_cast<double>(OldEvals - NewEvals) / static_cast<double>(OldEvals));

    const bool Pass = Mismatches == 0u && SeedMismatches == 0u && WorstDelta == 0.0;
    std::printf("\n[SelectionIdentity] %s\n", Pass ? "IDENTICAL — the carry is exact" : "*** DIVERGENT — the carry is NOT exact ***");
    return Pass ? 0 : 1;
}
