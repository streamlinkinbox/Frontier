/*==========================================================================================================================================
                                                    REPLAYTEMPORALMEASUREVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Enumerate finite-seed temporal resampling, deterministic count caps and deliberately biased alternatives.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace
{
//------------------------------------------------------------------------------------------------------------------------
//                                                 FINITE PROBABILITY SPACE
//------------------------------------------------------------------------------------------------------------------------

struct TemporalOutcome
{
    uint32_t Seed       = 0;
    uint32_t Count      = 1;
    uint32_t Unselected = 0;
    long double Weight = 1;
    long double Probability = .25L;
};

struct TemporalMeasure
{
    std::array<long double,4> Marginals{};
    long double Probability = 0;
    long double Mean        = 0;
    long double Variance    = 0;
};

void Require(bool Condition, const char* Message)
{
    if (!Condition) throw std::runtime_error(Message);
}

std::array<long double,4> TemporalRadiance(uint32_t Sequence, uint32_t Step)
{
    static constexpr std::array<std::array<long double,4>,4> Spectra{{
        {{0,1,4,9}}, {{9,0,1,4}}, {{0,0,0,0}}, {{.01L,20,.1L,0}}
    }};
    if (Sequence == 0) return Spectra[0];
    if (Sequence == 1) return Spectra[Step % 2];
    if (Sequence == 2) return Spectra[Step < 3 ? 0 : 2];
    if (Sequence == 3) return Spectra[Step < 3 ? 2 : 0];
    return Spectra[Step % 4];
}

TemporalMeasure InspectTemporalMeasure(const std::vector<TemporalOutcome>& Outcomes,
                                      const std::array<long double,4>& Radiance)
{
    TemporalMeasure Result;
    long double Second = 0;
    for (const auto& Outcome : Outcomes)
    {
        Result.Probability += Outcome.Probability;
        Result.Marginals[Outcome.Seed] += Outcome.Probability * Outcome.Weight;
        const long double Estimate = Outcome.Weight * Radiance[Outcome.Seed];
        Result.Mean += Outcome.Probability * Estimate;
        Second += Outcome.Probability * Estimate * Estimate;
    }
    Result.Variance = std::max(0.L, Second - Result.Mean * Result.Mean);
    return Result;
}

std::vector<TemporalOutcome> AccumulateTemporalOutcomes(
    const std::vector<TemporalOutcome>& Previous,
    const std::array<long double,4>&    Radiance,
    long double                        Floor,
    uint32_t                           Cap,
    bool                               Correlated,
    uint32_t                           Mutation)
{
    std::vector<TemporalOutcome> Result;
    Result.reserve(Previous.size() * (Correlated ? 2 : 8));
    for (const auto& Source : Previous)
    {
        uint32_t Retained = std::min(Cap, Source.Count);
        // 📝 Mutations are negative controls, never proposed production estimators.
        if (Mutation == 1 && Radiance[Source.Seed] == 0) Retained = 0;
        const long double SourceWeight = Mutation == 2 ? std::min(1.L, Source.Weight) : Source.Weight;
        const uint32_t FreshCount = Correlated ? 1 : 4;
        for (uint32_t Index = 0; Index < FreshCount; ++Index)
        {
            const uint32_t Fresh = Correlated ? Source.Unselected : Index;
            const long double FreshTarget = Radiance[Fresh] + Floor;
            const long double SourceTarget = Radiance[Source.Seed] + Floor;
            const uint32_t WeightCount = Mutation == 3 && Retained > 0 ? Source.Count : Retained;
            const long double HistoryWeight = SourceTarget * SourceWeight * WeightCount;
            const long double Sum = FreshTarget + HistoryWeight;
            const uint32_t Count = Retained + 1;
            const long double Probability = Source.Probability / FreshCount;
            Result.push_back({Fresh, Count, Fresh, Sum / (Count * FreshTarget),
                              Probability * FreshTarget / Sum});
            if (HistoryWeight > 0)
                Result.push_back({Source.Seed, Count, Fresh, Sum / (Count * SourceTarget),
                                  Probability * HistoryWeight / Sum});
        }
    }
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                          RECURSIVE IDENTITY AND COUNTEREXAMPLES
//------------------------------------------------------------------------------------------------------------------------

void ValidateTemporalMeasures()
{
    uint64_t OutcomesVisited = 0;
    uint32_t Sequences = 0;
    long double Maximum = 0;
    for (uint32_t Sequence = 0; Sequence < 5; ++Sequence)
        for (long double Floor : {.0001L,.01L,1.L})
            for (uint32_t Cap : {0u,1u,3u,64u})
                for (bool Correlated : {false,true})
                {
                    std::vector<TemporalOutcome> Outcomes;
                    for (uint32_t Seed = 0; Seed < 4; ++Seed) Outcomes.push_back({Seed,1,Seed,1,.25L});
                    for (uint32_t Step = 0; Step < 6; ++Step)
                    {
                        const auto Radiance = TemporalRadiance(Sequence, Step);
                        if (Step > 0)
                            Outcomes = AccumulateTemporalOutcomes(Outcomes, Radiance, Floor, Cap,
                                                                 Correlated && Step >= 2, 0);
                        const auto Measure = InspectTemporalMeasure(Outcomes, Radiance);
                        Require(std::abs(Measure.Probability - 1) < 2e-12L, "Outcome probability is not normalized");
                        for (long double Marginal : Measure.Marginals)
                            Maximum = std::max(Maximum, std::abs(Marginal - .25L));
                        long double Reference = 0;
                        for (long double Red : Radiance) Reference += Red / 4;
                        Require(std::abs(Measure.Mean - Reference) < 2e-11L,
                                "Recursive temporal mean differs from finite-space integral");
                        for (const auto& Outcome : Outcomes)
                            Require(Outcome.Count <= Cap + 1 && std::isfinite(Outcome.Weight) && Outcome.Weight > 0,
                                    "Count cap or finite positive normalization failed");
                        OutcomesVisited += Outcomes.size();
                        if (Sequence == 0 && Floor == .01L && Cap == 64 && Step == 5)
                            std::printf("TEMPORAL_VARIANCE correlated=%u mean=%.12Lg variance=%.12Lg outcomes=%zu\n",
                                        unsigned(Correlated), Measure.Mean, Measure.Variance, Outcomes.size());
                    }
                    ++Sequences;
                }
    Require(Maximum < 2e-12L, "Weighted seed marginals are not the original uniform measure");
    std::printf("TEMPORAL_IDENTITY sequences=%u steps_each=6 seed_domain=4 visited=%llu max_basis_error=%.12Lg\n",
                Sequences, static_cast<unsigned long long>(OutcomesVisited), Maximum);
    const auto Radiance = TemporalRadiance(0,0);
    for (uint32_t Mutation = 1; Mutation <= 3; ++Mutation)
    {
        std::vector<TemporalOutcome> Outcomes;
        for (uint32_t Seed = 0; Seed < 4; ++Seed) Outcomes.push_back({Seed,1,Seed,1,.25L});
        Outcomes = AccumulateTemporalOutcomes(Outcomes, Radiance, .01L, 64, false, 0);
        const uint32_t Cap = Mutation == 3 ? 1 : 64;
        Outcomes = AccumulateTemporalOutcomes(Outcomes, Radiance, .01L, Cap, false, Mutation);
        const auto Measure = InspectTemporalMeasure(Outcomes, Radiance);
        const char* Name = Mutation == 1 ? "visibility_rejection" : Mutation == 2 ? "normalization_clipping" :
                           "count_only_cap";
        Require(std::abs(Measure.Mean - 3.5L) > .01L, "Biased negative control unexpectedly preserved the integral");
        std::printf("TEMPORAL_NEGATIVE method=%s mean=%.12Lg reference=3.5 bias=%.12Lg\n",
                    Name, Measure.Mean, Measure.Mean - 3.5L);
    }
    // 📝 Reusing old RGB after a light turns off leaves half the previous integral in this two-proposal example.
    long double Ghost = 0;
    for (uint32_t Fresh = 0; Fresh < 4; ++Fresh)
        for (uint32_t History = 0; History < 4; ++History)
            Ghost += Radiance[History] / (2 * 16);
    Require(Ghost == 1.75L, "Stale-radiance negative control changed");
    std::printf("TEMPORAL_NEGATIVE method=stale_radiance mean=%.12Lg reference=0 bias=%.12Lg\n", Ghost, Ghost);
    std::printf("PASS temporal finite-measure identities and four biased negative controls\n");
}
} // namespace

int main()
{
    try { ValidateTemporalMeasures(); return 0; }
    catch (const std::exception& Error)
    {
        std::fprintf(stderr, "FAIL %s\n", Error.what());
        return 1;
    }
}
