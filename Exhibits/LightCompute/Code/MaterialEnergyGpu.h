/*==========================================================================================================================================
                                                       MATERIALENERGYGPU.H
==========================================================================================================================================*/
// 🧩 Exercise the shared Slang evaluator through independent Vulkan hemisphere quadrature and compare native results.

//------------------------------------------------------------------------------------------------------------------------
//                                                     SCATTERING FIXTURES
//------------------------------------------------------------------------------------------------------------------------

void ValidateMaterialEnergyGpu(const char* Driver, const std::string& Shaders, bool Legacy)
{
    LightComputeArchive Scene;
    std::vector<PreviewScattering> Materials;
    std::vector<double> Views;
    auto Append = [&](const PreviewScattering& S, double View) {
        Require(ValidatePreviewScattering(S), "Invalid GPU energy fixture");
        Materials.push_back(S); Views.push_back(View);
        Scene.Angles.insert(Scene.Angles.end(), {float(View),float(S.Configuration[0]),0,0,0,0,0,0});
        for (uint32_t I = 0; I < 8; ++I)
        {
            const auto& T = S.Strata[I];
            for (const auto* V : {&T.AlbedoRoughness,&T.Optics,&T.AbsorptionThickness})
                Scene.Angles.insert(Scene.Angles.end(),V->begin(),V->end());
            for (uint32_t V : T.Classification) Scene.Angles.push_back(float(V));
            for (const auto* V : {&T.Sheen,&T.SheenFinish})
                Scene.Angles.insert(Scene.Angles.end(),V->begin(),V->end());
        }
    };
    for (uint32_t Profile = 0; Profile < 4; ++Profile)
        for (float Roughness : {.35f,.65f})
            for (double View : {.25,.6,1.})
            {
                PreviewScattering S;
                S.Strata[0].AlbedoRoughness = {1,1,1,Roughness};
                if (Profile == 1) { S.Strata[0].AlbedoRoughness = {.9f,.65f,.2f,Roughness}; S.Strata[0].Optics[0] = 1; }
                if (Profile == 2)
                {
                    S.Configuration[0] = 2; S.Strata[1].Classification[0] = 1;
                    S.Strata[1].AlbedoRoughness[3] = .5f;
                }
                if (Profile == 3)
                {
                    S.Strata[0].AlbedoRoughness = {.8f,.8f,.8f,Roughness};
                    S.Strata[0].Sheen = {1,1,1,1}; S.Strata[0].SheenFinish[0] = .5f;
                }
                Append(S,View);
            }
    for (float Roughness : {0.f,.1f,.3f})
        for (double View : {.005,.025})
        {
            PreviewScattering S;
            S.Strata[0].AlbedoRoughness = {.8f,.8f,.8f,.35f};
            S.Strata[0].Sheen = {1,2,.5f,1}; S.Strata[0].SheenFinish[0] = Roughness;
            Append(S,View);
        }
    for (double View : {.05,.25})
        for (float Roughness : {.35f,.8f})
        {
            PreviewScattering S;
            S.Configuration[0] = 8;
            S.Strata[0].AlbedoRoughness = {1,1,1,.35f};
            for (uint32_t I = 1; I < 8; ++I)
            {
                S.Strata[I].Classification[0] = 1;
                S.Strata[I].AlbedoRoughness[3] = Roughness;
                S.Strata[I].Optics[2] = I % 2 ? 1.f : .5f;
            }
            Append(S,View);
        }
    Scene.Width = uint32_t(Materials.size()); Scene.Height = 1;
    Scene.Receivers.resize(Materials.size());
    LightComputeSubmission Compute(Driver);
    std::cout << "ENERGY_DEVICE name=" << Compute.Properties.deviceName << " type=" << Compute.Properties.deviceType
              << " software_vulkan=" << (Compute.Properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) << std::endl;
    Compute.Prepare(Scene,Shaders);
    auto Actual = Compute.Execute(Scene,0);
    double Error = 0, Maximum = 0;
    uint32_t Excess = 0, OriginalExcess = 0;
    constexpr uint32_t Resolution = 256;
    for (uint32_t I = 0; I < Materials.size(); ++I)
    {
        const auto& S = Materials[I];
        double NV = Views[I], VX = std::sqrt(1-NV*NV);
        ScatteringSpectrum Expected{};
        if (!Legacy)
            for (uint32_t Y = 0; Y < Resolution; ++Y)
            {
                double NL = (Y+.5)/Resolution, Radius = std::sqrt(1-NL*NL);
                for (uint32_t X = 0; X < Resolution; ++X)
                {
                    double Angle = 2*std::numbers::pi*(X+.5)/Resolution;
                    double HX = VX+Radius*std::cos(Angle), HY = Radius*std::sin(Angle), HZ = NV+NL;
                    double Length = std::sqrt(HX*HX+HY*HY+HZ*HZ);
                    auto R = EvaluateScatteringSequence(S,NV,NL,HZ/Length,(VX*HX+NV*HZ)/Length);
                    for (uint32_t C = 0; C < 3; ++C) Expected[C] += R[C]*NL*(2*std::numbers::pi/(Resolution*Resolution));
                }
            }
        double Energy = 0;
        for (uint32_t C = 0; C < 3; ++C)
        {
            Require(std::isfinite(Actual[I][C]) && Actual[I][C] >= 0, "Invalid GPU energy output");
            Energy = std::max(Energy,double(Actual[I][C]));
            if (!Legacy) Error = std::max(Error,std::abs(Actual[I][C]-Expected[C]));
        }
        Require(Actual[I][3] == 1, "GPU energy invocation was not written");
        Excess += Energy > 1.02; OriginalExcess += I < 24 && Energy > 1.02;
        Maximum = std::max(Maximum,Energy);
        std::cout << "ENERGY_GPU_CASE index=" << I << " legacy=" << Legacy << " reflectance=" << Energy << '\n';
    }
    std::cout << "ENERGY_GPU_SUMMARY legacy=" << Legacy << " cases=" << Materials.size()
              << " excess_cases=" << Excess << " original24_excess=" << OriginalExcess
              << " maximum_reflectance=" << Maximum << " native_max_rgb_error=" << Error << std::endl;
    if (Legacy) Require(OriginalExcess == 3 && Maximum > 1.3, "Original shader energy defect was not reproduced");
    else Require(Excess == 0 && Error < .001, "GPU white-furnace energy or scalar parity failed");
}
