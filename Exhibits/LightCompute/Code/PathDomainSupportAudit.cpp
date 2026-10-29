/*==========================================================================================================================================
                                                 PATHDOMAINSUPPORTAUDIT.CPP
==========================================================================================================================================*/
// 🧩 Exhaustively audit temporal reuse on a finite common random-number domain.
#include <array>
#include <cmath>
#include <cstdio>

namespace
{
template <size_t Extent> double EnumerateReusedMean(const std::array<double, Extent>& Previous,
                                                     const std::array<double, Extent>& Current, double Floor,
                                                     double Retained = 2)
{
    // Two independent previous draws, one current draw, and both random selections are enumerated exactly.
    double Result = 0;
    const double DrawProbability = 1.0 / double(Extent * Extent * Extent);
    for (size_t A = 0; A < Extent; ++A)
        for (size_t B = 0; B < Extent; ++B)
            for (size_t Fresh = 0; Fresh < Extent; ++Fresh)
            {
                const size_t PreviousDraws[] = {A, B};
                const double PreviousSum = Previous[A] + Previous[B] + 2 * Floor;
                if (PreviousSum == 0)
                {
                    // Negative control: count the two discarded zero-target draws as the old algorithm did.
                    Result += DrawProbability * Current[Fresh] / (1 + Retained);
                    continue;
                }
                for (size_t Choice = 0; Choice < 2; ++Choice)
                {
                    const size_t Prior = PreviousDraws[Choice];
                    const double PriorTarget = Previous[Prior] + Floor;
                    if (PriorTarget == 0) continue;
                    const double PriorSelection = PriorTarget / PreviousSum;
                    const double PriorWeight = PreviousSum / (2 * PriorTarget);
                    const double FreshTarget = Current[Fresh] + Floor;
                    const double ReusedTarget = Current[Prior] + Floor;
                    const double FreshWeight = FreshTarget;
                    const double ReusedWeight = ReusedTarget * PriorWeight * Retained;
                    const double Sum = FreshWeight + ReusedWeight;
                    if (Sum == 0) continue;
                    // Explicitly enumerate the final reservoir selection rather than cancelling it algebraically.
                    if (FreshWeight > 0)
                        Result += DrawProbability * PriorSelection * (FreshWeight / Sum) *
                                  Current[Fresh] * Sum / ((1 + Retained) * FreshTarget);
                    if (ReusedWeight > 0)
                        Result += DrawProbability * PriorSelection * (ReusedWeight / Sum) *
                                  Current[Prior] * Sum / ((1 + Retained) * ReusedTarget);
                }
            }
    return Result;
}
template <size_t Extent> double ExactMean(const std::array<double, Extent>& Current)
{
    double Sum = 0;
    for (double V : Current) Sum += V;
    return Sum / Extent;
}
template <size_t Extent> bool Validate(const char* Name, const std::array<double, Extent>& Previous,
                                       const std::array<double, Extent>& Current, bool RequireBias)
{
    const double Reference = ExactMean(Current);
    const double WithSupport = EnumerateReusedMean(Previous, Current, .125);
    const double WithoutSupport = EnumerateReusedMean(Previous, Current, 0);
    const double WithCap = EnumerateReusedMean(Previous, Current, .125, 1);
    const bool Recovered = std::abs(WithSupport - Reference) < 1e-12 && std::abs(WithCap - Reference) < 1e-12;
    const bool Detected = !RequireBias || std::abs(WithoutSupport - Reference) > .05;
    std::printf("%s exact=%.12g positive_floor=%.12g capped_count=%.12g zero_floor=%.12g\n", Name, Reference,
                WithSupport, WithCap, WithoutSupport);
    return Recovered && Detected;
}
} // namespace

int main()
{
    bool Valid = true;
    Valid &= Validate("INVERTED_VISIBILITY", std::array<double,2>{1,0}, std::array<double,2>{0,1}, true);
    Valid &= Validate("DARK_TO_BRIGHT", std::array<double,2>{0,0}, std::array<double,2>{1,1}, true);
    Valid &= Validate("RADIANCE_CHANGE", std::array<double,4>{0,1,0,3}, std::array<double,4>{0,0,2,5}, true);
    Valid &= Validate("NO_CHANGE", std::array<double,4>{0,1,2,3}, std::array<double,4>{0,1,2,3}, false);
    std::printf("%s finite-domain selection enumeration; not a GPU or full-scene validation\n", Valid ? "PASS" : "FAIL");
    return Valid ? 0 : 1;
}
