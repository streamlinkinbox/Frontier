/*==========================================================================================================================================
                                                     REPLAYBOUNDEDTEMPORALMEASURE.CPP
==========================================================================================================================================*/
// 🧩 Enumerate temporal weighting when three retained seeds overlap the current four-candidate group.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <stdexcept>

int main()
{
    try
    {
        constexpr std::array<long double,4> Bright{0,1,4,9}, Changed{9,0,1,4}, Dark{0,0,0,0};
        unsigned long long Visited = 0;
        long double Maximum = 0;
        for (unsigned Sequence=0; Sequence<4; ++Sequence)
            for (long double Floor : {.0001L,.01L,1.L})
                for (unsigned Retained : {1u,4u})
                {
                    const auto& Previous = Sequence==3 ? Dark : Bright;
                    const auto& Current = Sequence==1 ? Changed : Sequence==2 ? Dark : Bright;
                    std::array<long double,4> Marginals{};
                    long double Probability = 0;
                    for (unsigned Draws=0; Draws<1024; ++Draws)
                    {
                        std::array<unsigned,4> Old{}, Fresh{};
                        unsigned Digits = Draws;
                        long double OldSum = 0, FreshSum = 0;
                        for (unsigned Index=0; Index<4; ++Index)
                        {
                            Old[Index] = Digits & 3u;
                            Digits >>= 2;
                            OldSum += Previous[Old[Index]] + Floor;
                        }
                        Fresh = {Digits & 3u,Old[0],Old[1],Old[2]};
                        for (unsigned Seed : Fresh) FreshSum += Current[Seed] + Floor;
                        for (unsigned Winner=0; Winner<4; ++Winner)
                        {
                            const unsigned History = Old[Winner];
                            const long double OldTarget = Previous[History] + Floor;
                            const long double OldProbability = OldTarget / OldSum;
                            const long double Normalization = OldSum / (4 * OldTarget);
                            const long double HistoryWeight = (Current[History] + Floor)*Normalization*Retained;
                            const long double Sum = FreshSum + HistoryWeight;
                            for (unsigned Choice=0; Choice<5; ++Choice)
                            {
                                const unsigned Seed = Choice==4 ? History : Fresh[Choice];
                                const long double Target = Current[Seed] + Floor;
                                const long double Weight = Choice==4 ? HistoryWeight : Target;
                                const long double Chance = OldProbability*Weight/(1024*Sum);
                                Marginals[Seed] += Chance*Sum/((4+Retained)*Target);
                                Probability += Chance;
                                ++Visited;
                            }
                        }
                    }
                    if (std::abs(Probability-1)>1e-12L) throw std::runtime_error("Overlapping proposal mass differs from one");
                    for (long double Marginal : Marginals) Maximum = std::max(Maximum,std::abs(Marginal-.25L));
                }
        if (Maximum>1e-12L) throw std::runtime_error("Overlapping weighted proposals lost the seed measure");
        std::printf("BOUNDED_MEASURE configurations=24 overlapping_seeds=3 current_candidates=4 visited=%llu "
                    "max_basis_error=%.12Lg\n",Visited,Maximum);
        std::puts("PASS bounded overlapping temporal measure; no independent-sample variance claim");
        return 0;
    }
    catch (const std::exception& Error)
    {
        std::fprintf(stderr,"FAIL %s\n",Error.what());
        return 1;
    }
}
