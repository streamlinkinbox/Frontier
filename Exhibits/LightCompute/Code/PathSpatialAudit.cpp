/*==========================================================================================================================================
                                                PATH SPATIAL AUDIT
==========================================================================================================================================*/
// 🧩 Enumerate an exact two-sample counterexample to the retained demo's zero-weight spatial donor rejection.

#include <cmath>
#include <cstdio>
#include <initializer_list>

// Domain: one bright event and one dark event; proposal P(bright)=Probability. Both receivers have identical
// normals/positions, so the reconnection Jacobian and all compatibility tests are exactly one/true.
// A one-candidate reservoir has Tally=1, Weight=1/p if bright, and Weight=0 if dark.
static double Resolve(bool OwnBright, bool DonorBright, double Probability, bool RetainDarkDomain)
{
    const double OwnWeight = OwnBright ? 1.0/Probability : 0.0;
    const double DonorWeight = DonorBright ? 1.0/Probability : 0.0;
    const double WeightSum = OwnWeight + DonorWeight;
    if (WeightSum == 0.0) return 0.0;
    // The chosen sample is bright. Its target is positive at *both* identical receivers, even when a donor's
    // own earlier random draw was dark. Source skips the donor before recording its tally when Weight <= 0.
    const double Z = 1.0 + ((DonorBright || RetainDarkDomain) ? 1.0 : 0.0);
    return WeightSum/Z;
}

int main()
{
    bool Success = true;
    for (const double P : {0.5, 0.125, 0.0625})
    {
        double SourceMean = 0.0, RetainedMean = 0.0;
        for (int Own = 0; Own <= 1; ++Own) for (int Donor = 0; Donor <= 1; ++Donor)
        {
            const double Chance = (Own ? P : 1.0-P) * (Donor ? P : 1.0-P);
            SourceMean += Chance * Resolve(Own != 0, Donor != 0, P, false);
            RetainedMean += Chance * Resolve(Own != 0, Donor != 0, P, true);
        }
        const double AnalyticSource = 1.0 + (1.0-P)/2.0;
        Success &= std::abs(SourceMean-AnalyticSource) < 1e-12 && std::abs(RetainedMean-1.0) < 1e-12;
        std::printf("bright proposal p=%.4f, independent expectation 1; source-equation mean %.9g, retain-domain mean %.9g\n",
                    P, SourceMean, RetainedMean);
    }
    std::printf("%s exact counterexample: identical receiver domains; no WGSL/GPU execution\n", Success ? "PASS" : "FAIL");

    // Two discrete lights and two one-draw domains: the receiver sees both lights, but the
    // neighbour's target is zero for light 1. A total-M denominator loses half that light.
    double Naive = 0.0, Supported = 0.0;
    for (int Own = 0; Own < 2; ++Own)
        for (int Neighbour = 0; Neighbour < 2; ++Neighbour)
        {
            const bool NeighbourCanWin = Neighbour == 0;
            const double Sum = 2.0 + (NeighbourCanWin ? 2.0 : 0.0);
            const double OwnProbability = 2.0 / Sum;
            const double ZOwn = Own == 0 ? 2.0 : 1.0;
            Naive += 0.25 * Sum / 2.0;
            Supported += 0.25 * (OwnProbability * Sum / ZOwn +
                (NeighbourCanWin ? (1.0 - OwnProbability) * Sum / 2.0 : 0.0));
        }
    std::printf("different target supports: naive total-M mean %.9g; selected-support mean %.9g; exact sum 2\n",
                Naive, Supported);
    Success &= std::abs(Naive - 1.5) < 1e-12 && std::abs(Supported - 2.0) < 1e-12;
    std::printf("%s discrete direct-light support enumeration; not a general path-reconnection proof\n",
                Success ? "PASS" : "FAIL");
    return Success ? 0 : 1;
}
