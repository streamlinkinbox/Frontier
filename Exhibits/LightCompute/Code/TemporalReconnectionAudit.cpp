/*==========================================================================================================================================
                                            TEMPORALRECONNECTIONAUDIT.CPP
==========================================================================================================================================*/
// 🧩 Independent area integral exposing a missing temporal reconnection Jacobian in the retained demo equations.
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
struct IrradianceComparison { double Exact = 0, WithoutJacobian = 0, WithJacobian = 0; };
IrradianceComparison Integrate(double Shift, unsigned Grid)
{
    IrradianceComparison Result;
    const double Step = 1.0 / Grid;
    for (unsigned Y = 0; Y < Grid; ++Y)
        for (unsigned X = 0; X < Grid; ++X)
        {
            const double EmitterX = (X + .5) * Step - .5, EmitterY = (Y + .5) * Step - .5;
            const double PreviousDistance = std::sqrt(1 + EmitterX * EmitterX + EmitterY * EmitterY);
            const double CurrentDistance = std::sqrt(1 + (EmitterX - Shift) * (EmitterX - Shift) + EmitterY * EmitterY);
            // Receiver and emitter cosines are each 1/r. Exact area measure is 1/r^4.
            const double ExactArea = 1 / std::pow(CurrentDistance, 4);
            // Prior candidate is cosine-sampled; the retained temporal target omits dωcurrent/dωprevious.
            const double Uncorrected = 1 / (CurrentDistance * std::pow(PreviousDistance, 3));
            const double Jacobian = std::pow(PreviousDistance / CurrentDistance, 3);
            Result.Exact += ExactArea * Step * Step;
            Result.WithoutJacobian += Uncorrected * Step * Step;
            Result.WithJacobian += Uncorrected * Jacobian * Step * Step;
        }
    return Result;
}
} // namespace
int main()
{
    bool Valid = true;
    for (double Shift : {0.0, .25, .5, 1.0})
    {
        const auto Fine = Integrate(Shift, 1200), Coarse = Integrate(Shift, 600);
        const double Convergence = std::abs(Fine.Exact - Coarse.Exact);
        const double Relative = Fine.WithoutJacobian / Fine.Exact - 1;
        Valid &= Convergence < 1e-6 && std::abs(Fine.WithJacobian - Fine.Exact) < 1e-11;
        if (Shift == 0) Valid &= std::abs(Relative) < 1e-11;
        if (Shift == 1) Valid &= std::abs(Relative) > .1;
        std::printf("receiver_shift=%.2f exact=%.10g no_jacobian=%.10g relative_error=%+.6f "
                    "with_jacobian=%.10g midpoint_convergence=%.3g\n", Shift, Fine.Exact,
                    Fine.WithoutJacobian, Relative, Fine.WithJacobian, Convergence);
    }
    std::printf("%s area-measure counterexample; mathematical audit, not WGSL execution or a complete GI fix\n",
                Valid ? "PASS" : "FAIL");
    return Valid ? 0 : 1;
}
