/*==========================================================================================================================================
                                                   DIFFUSEDIELECTRICVALIDATION.H
==========================================================================================================================================*/
// 🧩 Exercise compact cosine diffuse rays through closed glass and preserve emitter sampling and ray-count semantics.
void ValidateDiffuseDielectric(const char* Driver, const std::string& Shaders, const std::string& Output, bool Original = false)
{
    using namespace TransportReference;
    std::filesystem::create_directories(Output);
    LightComputeArchive Scene;
    constexpr uint32_t Samples = 64;
    Prepare(Scene,Samples,4,false);
    auto Materials = AppendSceneMaterials(Scene,Shaders);
    uint32_t Glass = DuplicateSceneMaterial(Scene,Materials[3],4,{1,1,1,0},0), Offset = Glass-1;
    for (uint32_t I = 0; I < 3; ++I)
    {
        MaterialRectangle(Scene,{-1.6+I*1.6,0,1},{.8,0,0},{0,0,3},0,1300+I);
        float Albedo = .3f + .3f*I;
        for (size_t F = Scene.Surfaces.size()-2; F < Scene.Surfaces.size(); ++F)
        {
            Scene.Surfaces[F].AlbedoRoughness = {Albedo,Albedo,Albedo,.5f};
            Scene.Surfaces[F].OpticalToken[0] = 0;
        }
    }
    MaterialRectangle(Scene,{0,-6.5,1},{3,0,0},{0,0,3},Glass,1100);
    MaterialRectangle(Scene,{0,-5.5,1},{-3,0,0},{0,0,3},Glass,1100);
    MaterialRectangle(Scene,{3,-6,1},{0,.5,0},{0,0,3},Glass,1100);
    MaterialRectangle(Scene,{-3,-6,1},{0,-.5,0},{0,0,3},Glass,1100);
    MaterialRectangle(Scene,{0,-6,4},{3,0,0},{0,.5,0},Glass,1100);
    MaterialRectangle(Scene,{0,-6,-2},{3,0,0},{0,-.5,0},Glass,1100);
    for (uint32_t Z = 0; Z < 20; ++Z)
        for (uint32_t X = 0; X < 32; ++X)
        {
            MaterialRectangle(Scene,{-9.3+X*.6,-9,-4.7+Z*.6},{.3,0,0},{0,0,.3},0,1400+X+Z*32);
            for (size_t I = Scene.Surfaces.size()-2; I < Scene.Surfaces.size(); ++I) Scene.Surfaces[I].Emissive = {1,1,1,0};
        }
    RebuildMaterialVisibility(Scene);
    MaterialCamera(Scene,{0,-4,1},{0,0,1},96,64);
    Scene.Camera[8][2] = 1e9f;
    EmissiveQuadrature::AppendDistribution(Scene);
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Scene,Shaders);
    std::cout << "DIFFUSE_DIELECTRIC_DEVICE name=" << Compute.Properties.deviceName << " type=" << Compute.Properties.deviceType << std::endl;
    auto Render = [&](uint32_t Seed = 71) {
        auto P = Compute.Execute(Scene,0,16,Seed,64,0,true,true,Samples,false);
        auto R = Compute.PrimaryReceivers();
        uint32_t Count = 0;
        for (size_t I = 0; I < R.size(); ++I)
        {
            for (uint32_t C = 0; C < 3; ++C) Require(std::isfinite(P[I][C]) && P[I][C] >= 0,"Invalid compact diffuse glass radiance");
            Require(R[I].MotionPositionToken[3] < 1400,"Behind-camera emitter became directly visible");
            if (R[I].MotionPositionToken[3] < 1300 || R[I].MotionPositionToken[3] > 1302) continue;
            Require(!(R[I].ExpectedDirect[3] > 0),"Compact receiver acquired an authored material address");
            if (!Original) Require(R[I].ExpectedDirect[1] == 0 && R[I].ExpectedDirect[2] == 0,"Compact volume path capped or unsupported");
            ++Count;
        }
        auto Counters = Compute.HistoryStatistics();
        Require(Counters[13] == Count*Samples && Counters[14]+Counters[15] == Counters[13],"Compact primary-secondary counters changed");
        Require(Count > 2000,"Insufficient compact glass receiver coverage");
        return P;
    };
    Scene.Angles[Offset+13] = 1;
    auto Clear = Render();
    auto Counts = Compute.HistoryStatistics();
    std::cout << "DIFFUSE_RAY_COUNTS rays=" << Counts[13] << " hits=" << Counts[14] << " misses=" << Counts[15] << std::endl;
    Require(Clear == Render(),"Compact volume seeded repeat changed");
    for (uint32_t C = 0; C < 3; ++C) Scene.Angles[Offset+16+C] = std::array<float,3>{.2f,.6f,1.2f}[C];
    auto Absorbed = Render();
    std::array<double,3> Base{}, Tint{};
    uint32_t Changed = 0;
    for (size_t I = 0; I < Clear.size(); ++I)
        for (uint32_t C = 0; C < 3; ++C)
        {
            Require(Absorbed[I][C] <= Clear[I][C]+1e-6f,"Index-matched Beer absorption increased radiance");
            Base[C] += Clear[I][C]; Tint[C] += Absorbed[I][C];
            Changed += Clear[I][C]-Absorbed[I][C] > .01f;
        }
    if (Original)
    {
        Require(Changed == 0 && Clear == Absorbed,"Pinned shader no longer reproduces ignored compact volume absorption");
        std::cout << "DIFFUSE_DIELECTRIC_ORIGINAL changed_channels=" << Changed << " absorption_ignored=1 repeat_exact=1" << std::endl;
        return;
    }
    Require(Changed > 1000 && Tint[0] > Tint[1] && Tint[1] > Tint[2] && Tint[2] > 0,"Missing distance absorption in compact diffuse rays");
    std::cout << "DIFFUSE_BEER changed_channels=" << Changed << " channel_ratios=" << Tint[0]/Base[0] << ',' << Tint[1]/Base[1] << ',' << Tint[2]/Base[2] << " repeat_exact=1" << std::endl;
    // All active emitter segments in this copy cross the opaque-shadow volume, isolating refracted emission.
    auto Shielded = Scene;
    for (auto& F : Shielded.Surfaces)
        if (F.OpticalToken[3] >= 1400)
        {
            uint32_t Tile = uint32_t(F.OpticalToken[3])-1400, X = Tile%32, Z = Tile/32;
            F.Emissive = X >= 14 && X <= 17 && Z >= 9 && Z <= 11 ? std::array<float,4>{1,1,1,0} : std::array<float,4>{0,0,0,0};
        }
    size_t Capacity = Shielded.Angles.size();
    Shielded.Angles.resize(size_t(Shielded.Camera[10][2]));
    EmissiveQuadrature::AppendDistribution(Shielded);
    Require(Shielded.Angles.size() <= Capacity,"Shielded emitter distribution exceeded prepared atlas");
    Shielded.Angles.resize(Capacity);
    auto Bsdf = Compute.Execute(Shielded,0,16,71,64,0,true,true,Samples,false,false);
    auto Nee = Compute.Execute(Shielded,0,16,71,64,0,true,true,Samples,false,true);
    double NeeTotal = 0;
    for (auto P : Nee) for (uint32_t C = 0; C < 3; ++C) NeeTotal += P[C];
    Require(Nee == Bsdf && NeeTotal > 0,"Emitter sampling suppressed or doubled isolated refracted emitter paths");
    std::cout << "DIFFUSE_EMITTER_ROUTING nee_equal=1 refracted_total=" << NeeTotal << std::endl;
    for (auto& F : Scene.Surfaces)
        if (F.OpticalToken[3] >= 1400)
        {
            uint32_t Tile = uint32_t(F.OpticalToken[3])-1400;
            F.Emissive = ((Tile%32)+(Tile/32))%2 ? std::array<float,4>{3,.7f,.12f,0} : std::array<float,4>{.08f,.35f,1.4f,0};
        }
    for (uint32_t Case = 0; Case < 3; ++Case)
    {
        Scene.Angles[Offset+13] = Case ? 1.5f : 1.f;
        for (uint32_t C = 0; C < 3; ++C) Scene.Angles[Offset+16+C] = Case == 2 ? std::array<float,3>{.2f,.6f,1.2f}[C] : 0;
        auto Pixels = Render();
        WriteImage(Output+"/DiffuseDielectric"+std::to_string(Case)+".ppm",Pixels,Scene.Width,Scene.Height);
    }
    for (auto& F : Scene.Surfaces) for (uint32_t C = 0; C < 3; ++C) F.Emissive[C] = 0;
    auto Black = Compute.Execute(Scene,0,16,71,64,0,false,true,Samples,false);
    for (auto P : Black) for (uint32_t C = 0; C < 3; ++C) Require(P[C] == 0,"Rough solid transport retained removed emission");
    std::cout << "DIFFUSE_DIELECTRIC_PASS source_off_black=1 pixels=" << Black.size() << " samples=" << Samples << " proposals_per_sample=1" << std::endl;
}
