/*==========================================================================================================================================
                                                  SOLARPAYLOADVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Validate solar payload weight, symmetry, domain checks and synthetic sensor conversion.
#include "Engine/Rendering/SolarOpticsPayload.h"
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

int main()
{
    using namespace Frontier;
    SolarOpticsParameters P;
    const auto Samples = CompileSolarOpticsPayload(P);
    double Weight = 0, X = 0, Y = 0;
    for (uint32_t Index = 0; Index < SolarSourceCount; ++Index)
    {
        const size_t Start = SolarOpticsHeaderFloats + 4 * Index;
        Weight += Samples[Start + 2]; X += Samples[Start] * Samples[Start + 2]; Y += Samples[Start + 1] * Samples[Start + 2];
    }
    const double Sigma = CalibrateSolarBlur({50, 4, 2});
    const double Expected = 2 * 4 * .001 / (50 * 2 * std::sqrt(2 * std::log(2.0)));
    bool Valid = std::abs(Weight - 1) < 1e-7 && std::abs(X) < 1e-10 && std::abs(Y) < 1e-10 &&
                 std::abs(Sigma - Expected) < 1e-15;
    int Rejected = 0;
    const auto Reject = [&](const SolarOpticsParameters& Value)
    {
        try { (void)CompileSolarOpticsPayload(Value); }
        catch (const std::invalid_argument&) { ++Rejected; }
    };
    for (int Case = 0; Case < 9; ++Case)
    {
        auto Q = P;
        switch (Case)
        {
            case 0: Q.SunDirection = {0, 0, 0}; break;
            case 1: Q.Disc.AngularRadiusRad = 0; break;
            case 2: Q.Disc.AngularRadiusRad = std::numeric_limits<double>::quiet_NaN(); break;
            case 3: Q.Disc.EdgeBlurRad = -1; break;
            case 4: Q.Disc.LimbLinear = 2; break;
            case 5: Q.DiscIrradiance.R = -1; break;
            case 6: Q.CloudTransmission = 1.1; break;
            case 7: Q.PlanetHorizonZ = 2; break;
            case 8: Q.Flare.ApertureBlades = 2; break;
        }
        Reject(Q);
    }
    try { (void)CalibrateSolarBlur({0, 4, 2}); } catch (const std::invalid_argument&) { ++Rejected; }
    try { (void)CalibrateSolarBlur({50, 4, -1}); } catch (const std::invalid_argument&) { ++Rejected; }
    Valid &= Rejected == 11;
    std::printf("%s %u source samples weight=%.12g centroid=(%.3g,%.3g), synthetic sigma=%.12g rad, %d invalid domains rejected\n",
                Valid ? "PASS" : "FAIL", SolarSourceCount, Weight, X, Y, Sigma, Rejected);
    return Valid ? 0 : 1;
}
