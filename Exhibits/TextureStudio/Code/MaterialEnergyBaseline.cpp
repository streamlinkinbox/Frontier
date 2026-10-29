/*==========================================================================================================================================
                                                     MATERIALENERGYBASELINE.CPP
==========================================================================================================================================*/
// 🧩 Resume reused-material evaluation with native reciprocity and white-furnace diagnostics; no replacement BSDF.

#include "Engine/Render/ScatteringSequence.h"
#include <iomanip>
#include <iostream>

//------------------------------------------------------------------------------------------------------------------------
//                                                 HEMISPHERICAL QUADRATURE
//------------------------------------------------------------------------------------------------------------------------

Frontier::ScatteringSpectrum IntegrateReflectance(
    const Frontier::PreviewScattering& Material,
    double                            NormalView,
    uint32_t                          Resolution)
{
    Frontier::ScatteringSpectrum Sum{};
    double ViewX = std::sqrt(1 - NormalView * NormalView);
    for (uint32_t I = 0; I < Resolution; ++I)
    {
        double NormalLight = (I + .5) / Resolution;
        double Radius = std::sqrt(1 - NormalLight * NormalLight);
        for (uint32_t J = 0; J < Resolution; ++J)
        {
            double Angle = 2 * std::numbers::pi * (J + .5) / Resolution;
            double LightX = Radius * std::cos(Angle), LightY = Radius * std::sin(Angle);
            double HalfX = ViewX + LightX, HalfZ = NormalView + NormalLight;
            double Length = std::sqrt(HalfX * HalfX + LightY * LightY + HalfZ * HalfZ);
            double NormalHalf = HalfZ / Length;
            double ViewHalf = (ViewX * HalfX + NormalView * HalfZ) / Length;
            auto Forward = Frontier::EvaluateScatteringSequence(Material, NormalView, NormalLight, NormalHalf, ViewHalf);
            auto Reverse = Frontier::EvaluateScatteringSequence(Material, NormalLight, NormalView, NormalHalf, ViewHalf);
            for (uint32_t C = 0; C < 3; ++C)
            {
                if (!std::isfinite(Forward[C]) || Forward[C] < 0 || std::abs(Forward[C] - Reverse[C]) > 1e-10)
                    throw std::runtime_error("Nonfinite/negative or nonreciprocal reused material evaluation");
                Sum[C] += Forward[C] * NormalLight * (2 * std::numbers::pi / (Resolution * Resolution));
            }
        }
    }
    return Sum;
}

int main()
{
    try
    {
        uint32_t Cases = 0, Excess = 0;
        double Maximum = 0, Sensitivity = 0;
        std::cout << std::setprecision(9);
        for (uint32_t Profile = 0; Profile < 4; ++Profile)
            for (float Roughness : {.35f, .65f})
                for (double NormalView : {.25, .6, 1.})
                {
                    Frontier::PreviewScattering Material;
                    auto& Stratum = Material.Strata[0];
                    Stratum.AlbedoRoughness = {1, 1, 1, Roughness};
                    if (Profile == 1)
                    {
                        Stratum.AlbedoRoughness = {.9f, .65f, .2f, Roughness};
                        Stratum.Optics[0] = 1;
                    }
                    if (Profile == 2)
                    {
                        Material.Configuration[0] = 2;
                        Material.Strata[1].Classification[0] = 1;
                        Material.Strata[1].AlbedoRoughness[3] = .5f;
                    }
                    if (Profile == 3)
                    {
                        Stratum.AlbedoRoughness = {.8f, .8f, .8f, Roughness};
                        Stratum.Sheen = {1, 1, 1, 1};
                        Stratum.SheenFinish[0] = .5f;
                    }
                    if (!Frontier::ValidatePreviewScattering(Material))
                        throw std::runtime_error("Energy fixture violates existing material validation");
                    auto Coarse = IntegrateReflectance(Material, NormalView, 256);
                    auto Fine = IntegrateReflectance(Material, NormalView, 512);
                    double Delta = 0;
                    for (uint32_t C = 0; C < 3; ++C)
                    {
                        Delta = std::max(Delta, std::abs(Fine[C] - Coarse[C]));
                        Maximum = std::max(Maximum, Fine[C]);
                    }
                    Sensitivity = std::max(Sensitivity, Delta);
                    bool Over = *std::max_element(Fine.begin(), Fine.end()) > 1.02;
                    Excess += Over;
                    ++Cases;
                    std::cout << "MATERIAL_ENERGY profile=" << Profile << " roughness=" << Roughness
                              << " normal_view=" << NormalView << " red=" << Fine[0] << " green=" << Fine[1]
                              << " blue=" << Fine[2] << " quadrature_delta=" << Delta
                              << " exceeds_1p02=" << Over << '\n';
                }
        std::cout << "MATERIAL_ENERGY_DIAGNOSTIC cases=" << Cases << " reciprocal_finite_nonnegative=1"
                  << " maximum_reflectance=" << Maximum << " coarse_fine_max_delta=" << Sensitivity
                  << " energy_excess_cases=" << Excess << " full_library_acceptance=0\n";
    }
    catch (const std::exception& Error)
    {
        std::cerr << Error.what() << '\n';
        return 1;
    }
}
