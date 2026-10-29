/*==========================================================================================================================================
                                                      MATERIALENERGYVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Enforce hemispherical reflectance and reciprocity over retained isotropic materials with cosine/GGX/Charlie mixture quadrature.

#include "Engine/Render/ScatteringSequence.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace Frontier;

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRATIFIED MIXTURE QUADRATURE
//------------------------------------------------------------------------------------------------------------------------

ScatteringSpectrum IntegrateMaterialEnergy(const PreviewScattering& S, double NormalView, uint32_t Resolution)
{
    const double π = std::numbers::pi, ViewX = std::sqrt(1-NormalView*NormalView);
    ScatteringSpectrum Sum{};
    std::vector<uint32_t> Fuzz;
    for (uint32_t P = 0; P < S.Configuration[0]; ++P)
        if (S.Strata[P].Classification[0] == 0 && S.Strata[P].Sheen[3] > 0) Fuzz.push_back(P);
    for (uint32_t Proposal = 0; Proposal <= S.Configuration[0]+Fuzz.size(); ++Proposal)
        for (uint32_t I = 0; I < Resolution; ++I)
            for (uint32_t J = 0; J < Resolution; ++J)
            {
                double U = (I+.5)/Resolution, Angle = 2*π*(J+.5)/Resolution;
                double X, Y, Z;
                if (Proposal == 0)
                {
                    X = std::sqrt(U)*std::cos(Angle); Y = std::sqrt(U)*std::sin(Angle); Z = std::sqrt(1-U);
                }
                else
                {
                    double HZ;
                    if (Proposal <= S.Configuration[0])
                    {
                        double Roughness = S.Strata[Proposal-1].AlbedoRoughness[3], α = Roughness*Roughness;
                        HZ = std::sqrt((1-U)/(1+(α*α-1)*U));
                    }
                    else
                    {
                        double R = S.Strata[Fuzz[Proposal-S.Configuration[0]-1]].SheenFinish[0];
                        double InverseAlpha = 1/std::max(R*R,1e-3);
                        HZ = std::sqrt(1-std::pow(U,2/(InverseAlpha+2)));
                    }
                    double Radius = std::sqrt(std::max(0.,1-HZ*HZ));
                    double HX = Radius*std::cos(Angle), HY = Radius*std::sin(Angle);
                    double VH = ViewX*HX+NormalView*HZ;
                    X = 2*VH*HX-ViewX; Y = 2*VH*HY; Z = 2*VH*HZ-NormalView;
                }
                if (Z <= 0) continue;
                double HX = ViewX+X, HZ = NormalView+Z, Length = std::sqrt(HX*HX+Y*Y+HZ*HZ);
                double NH = HZ/Length, VH = (ViewX*HX+NormalView*HZ)/Length;
                double Density = Z/π;
                for (uint32_t P = 0; P < S.Configuration[0]; ++P)
                    Density += EvaluateGgxDistribution(NH,S.Strata[P].AlbedoRoughness[3])*NH/(4*VH);
                for (uint32_t P : Fuzz)
                {
                    double R = S.Strata[P].SheenFinish[0], InverseAlpha = 1/std::max(R*R,1e-3);
                    double D = (InverseAlpha+2)*std::pow(std::max(0.,1-NH*NH),InverseAlpha*.5)/(2*π);
                    Density += D*NH/(4*VH);
                }
                auto Forward = EvaluateScatteringSequence(S,NormalView,Z,NH,VH);
                auto Reverse = EvaluateScatteringSequence(S,Z,NormalView,NH,VH);
                for (uint32_t C = 0; C < 3; ++C)
                {
                    if (!std::isfinite(Forward[C]) || Forward[C] < 0 || std::abs(Forward[C]-Reverse[C]) > 1e-10)
                    {
                        std::cerr << std::setprecision(17) << "ENERGY_RECIPROCITY forward=" << Forward[C]
                                  << " reverse=" << Reverse[C] << " difference=" << Forward[C]-Reverse[C]
                                  << " normal_view=" << NormalView << " normal_light=" << Z << '\n';
                        throw std::runtime_error("Invalid or nonreciprocal material scattering");
                    }
                    Sum[C] += Forward[C]*Z/(Density*Resolution*Resolution);
                }
            }
    return Sum;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       ENERGY ACCEPTANCE
//------------------------------------------------------------------------------------------------------------------------

int main(int Count, char** Arguments)
{
    try
    {
        std::ofstream File;
        if (Count == 2) File.open(Arguments[1]);
        std::ostream& Output = Count == 2 ? File : std::cout;
        if (!Output) throw std::runtime_error("Cannot write energy report");
        uint32_t Cases = 0, Excess = 0, Unsettled = 0;
        double Maximum = 0, DeltaMaximum = 0;
        auto Evaluate = [&](const PreviewScattering& S, double View, const char* Profile) {
            if (!ValidatePreviewScattering(S)) throw std::runtime_error("Invalid material fixture");
            auto Coarse = IntegrateMaterialEnergy(S,View,64), Fine = IntegrateMaterialEnergy(S,View,128);
            double Delta = 0;
            for (uint32_t C = 0; C < 3; ++C) Delta = std::max(Delta,std::abs(Coarse[C]-Fine[C]));
            if (Delta > .005)
            {
                auto Refined = IntegrateMaterialEnergy(S,View,256);
                Delta = 0;
                for (uint32_t C = 0; C < 3; ++C) Delta = std::max(Delta,std::abs(Fine[C]-Refined[C]));
                Fine = Refined;
            }
            double Energy = *std::max_element(Fine.begin(),Fine.end());
            Maximum = std::max(Maximum,Energy); DeltaMaximum = std::max(DeltaMaximum,Delta);
            Excess += Energy > 1.02; Unsettled += Delta > .01; ++Cases;
            Output << std::setprecision(9) << "MATERIAL_ENERGY_ACCEPTANCE profile=" << Profile
                   << " roughness=" << S.Strata[0].AlbedoRoughness[3] << " normal_view=" << View
                   << " ior=" << S.Strata[0].Optics[1] << " metallic=" << S.Strata[0].Optics[0]
                   << " reflectance=" << Energy << " quadrature_delta=" << Delta << '\n';
        };
        for (float Roughness : {.045f,.1f,.2f,.35f,.65f,1.f})
            for (double View : {.01,.05,.25,.6,1.})
                for (float Ior : {1.f,1.5f,3.f})
                    for (float Metal : {0.f,.5f,1.f})
                    {
                        PreviewScattering S;
                        S.Strata[0].AlbedoRoughness = {1,1,1,Roughness};
                        S.Strata[0].Optics = {Metal,Ior,1,0};
                        Evaluate(S,View,"white_isotropic");
                    }
        for (float SheenRoughness : {0.f,.1f,.3f,.5f,.65f,1.f})
            for (double View : {.005,.025,.1,.25,.6,1.})
                for (float Strength : {.5f,1.f})
                {
                    PreviewScattering S;
                    S.Strata[0].AlbedoRoughness = {.8f,.8f,.8f,.35f};
                    S.Strata[0].Sheen = {1,2,.5f,Strength};
                    S.Strata[0].SheenFinish[0] = SheenRoughness;
                    Evaluate(S,View,"bounded_fuzz");
                }
        for (float Coverage : {.25f,1.f})
            for (double View : {.01,.25,1.})
                for (float Roughness : {.045f,.35f,.8f})
                {
                    PreviewScattering S;
                    S.Configuration[0] = 2;
                    S.Strata[0].AlbedoRoughness = {1,1,1,.35f};
                    S.Strata[1].Classification[0] = 1;
                    S.Strata[1].Optics[2] = Coverage;
                    S.Strata[1].AlbedoRoughness[3] = Roughness;
                    Evaluate(S,View,"coated_white");
                }
        for (double View : {.01,.05,.25,1.})
            for (float Roughness : {.045f,.35f,.8f})
                for (float Absorption : {0.f,2.f})
                {
                    PreviewScattering S;
                    S.Configuration[0] = 8;
                    S.Strata[0].AlbedoRoughness = {1,1,1,.35f};
                    for (uint32_t I = 1; I < 8; ++I)
                    {
                        S.Strata[I].Classification[0] = 1;
                        S.Strata[I].AlbedoRoughness[3] = Roughness;
                        S.Strata[I].Optics[2] = I % 2 ? 1.f : .5f;
                        S.Strata[I].AbsorptionThickness = {Absorption,Absorption,Absorption,.01f};
                    }
                    Evaluate(S,View,"eight_strata");
                }
        Output << "MATERIAL_ENERGY_SUMMARY cases=" << Cases << " reciprocal_finite_nonnegative=1 excess_cases=" << Excess
               << " unsettled_cases=" << Unsettled << " maximum_reflectance=" << Maximum
               << " maximum_quadrature_delta=" << DeltaMaximum << " full_library_acceptance=0\n";
        return Excess || Unsettled ? 1 : 0;
    }
    catch (const std::exception& Error) { std::cerr << Error.what() << '\n'; return 1; }
}
