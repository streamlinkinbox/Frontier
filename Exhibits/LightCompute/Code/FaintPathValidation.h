/*==========================================================================================================================================
                                                        FAINTPATHVALIDATION.H
==========================================================================================================================================*/
// 🧩 Exhaust paired survival draws through an extracted production block; verify exact first-moment preservation and latch state.
void ValidateFaintPath(const char* Driver,const std::string& Shaders)
{
    LightComputeArchive Scene;
    for (float W : {0.f,5e-7f,1e-6f,2e-6f,1.f})
        for (uint32_t Depth : {31u,32u,39u,40u,47u,48u})
            for (uint32_t Latched = 0; Latched < 2; ++Latched)
                for (float Draw : {0.f,.25f,.5f,.75f})
                    Scene.Angles.insert(Scene.Angles.end(),{W,W*.5f,W*.25f,float(Depth),float(Latched),Draw,0,0});
    Scene.Width = uint32_t(Scene.Angles.size()/8); Scene.Height = 1; Scene.Receivers.resize(Scene.Width);
    LightComputeSubmission Compute(Driver); Compute.Prepare(Scene,Shaders);
    const auto Output = Compute.Execute(Scene,0), State = Compute.TransportSlab(16);
    uint32_t Trials = 0, Killed = 0;
    for (uint32_t I = 0; I < Scene.Width; ++I)
    {
        const auto P = size_t(I)*8;
        const bool Faint = Scene.Angles[P+4] != 0 || (Scene.Angles[P+3] >= 32 && Scene.Angles[P] < 1e-6f);
        const bool Trial = Faint && (uint32_t(Scene.Angles[P+3]) & 7u) == 7u;
        const bool Survived = !Trial || Scene.Angles[P+5] < .5f;
        Require(State[I][0] == Faint,"Faint-path activation/latch differs from native oracle");
        Trials += Trial; Killed += !Survived;
        for (uint32_t C = 0; C < 3; ++C)
            Require(Output[I][C] == Scene.Angles[P+C] * (Survived ? Trial ? 2 : 1 : 0),"Faint-path compensation differs");
        if (I%4 == 0)
            for (uint32_t C = 0; C < 3; ++C)
            {
                double Sum = 0;
                for (uint32_t J = 0; J < 4; ++J) Sum += Output[I+J][C];
                Require(Sum*.25 == double(Scene.Angles[P+C]),"Survival changed expected RGB transport weight");
            }
    }
    Require(Trials > 0 && Killed*2 == Trials,"Survival outcomes were not paired");
    std::cout << "FAINT_PATH_PASS device=" << Compute.Properties.deviceName << " type=" << Compute.Properties.deviceType
              << " cases=" << Scene.Width << " trials=" << Trials << " killed=" << Killed
              << " exact_rgb_first_moment=1 latched_compensation=1 draw_half_boundary=1 production_block=1" << std::endl;
}
