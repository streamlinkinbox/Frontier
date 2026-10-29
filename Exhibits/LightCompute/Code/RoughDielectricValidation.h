/*==========================================================================================================================================
                                                   ROUGHDIELECTRICVALIDATION.H
==========================================================================================================================================*/
// 🧩 Exercise cosine/GGX reflected rays through behind-camera closed glass, distance absorption and live emitter removal.
void ValidateRoughDielectric(const char* Driver, const std::string& Shaders, const std::string& Output, bool Original = false)
{
    using namespace TransportReference;
    std::filesystem::create_directories(Output);
    LightComputeArchive Scene;
    constexpr uint32_t Samples = 32;
    Prepare(Scene,Samples,4,false);
    auto Materials = AppendSceneMaterials(Scene,Shaders);
    uint32_t Glass = DuplicateSceneMaterial(Scene,Materials[3],4,{1,1,1,0},0), Offset = Glass-1;
    std::array<uint32_t,3> Rough{};
    for (uint32_t I = 0; I < 3; ++I)
    {
        constexpr float Widths[]{.06f,.3f,.65f};
        Rough[I] = DuplicateSceneMaterial(Scene,Materials[3],0,{.8f,.8f,.8f,Widths[I]},1);
        MaterialRectangle(Scene,{-1.6+I*1.6,0,1},{.8,0,0},{0,0,3},Rough[I],1300+I);
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
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Scene,Shaders);
    std::cout << "ROUGH_DIELECTRIC_DEVICE name=" << Compute.Properties.deviceName << " type=" << Compute.Properties.deviceType << std::endl;
    auto Render = [&](uint32_t Seed = 71) {
        auto P = Compute.Execute(Scene,0,16,Seed,64,0,true,true,Samples,false);
        auto R = Compute.PrimaryReceivers();
        uint32_t Count = 0;
        for (size_t I = 0; I < R.size(); ++I)
        {
            for (uint32_t C = 0; C < 3; ++C) Require(std::isfinite(P[I][C]) && P[I][C] >= 0,"Invalid rough glass radiance");
            Require(R[I].MotionPositionToken[3] < 1400,"Behind-camera emitter became directly visible");
            if (R[I].MotionPositionToken[3] < 1300 || R[I].MotionPositionToken[3] > 1302) continue;
            if (!Original && R[I].ExpectedDirect[0] == 6) Require(R[I].ExpectedDirect[1] == 0 && R[I].ExpectedDirect[2] == 0,"Rough volume path capped or unsupported");
            ++Count;
        }
        Require(Count > 2000,"Insufficient rough-glass receiver coverage");
        return P;
    };
    Scene.Angles[Offset+13] = 1;
    auto Clear = Render();
    Require(Clear == Render(),"Rough volume seeded repeat changed");
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
        Require(Changed == 0 && Clear == Absorbed,"Pinned shader no longer reproduces ignored rough volume absorption");
        std::cout << "ROUGH_DIELECTRIC_ORIGINAL changed_channels=" << Changed << " absorption_ignored=1 repeat_exact=1" << std::endl;
        return;
    }
    Require(Changed > 1000 && Tint[0] > Tint[1] && Tint[1] > Tint[2] && Tint[2] > 0,"Missing distance absorption in rough rays");
    std::cout << "ROUGH_BEER changed_channels=" << Changed << " channel_ratios=" << Tint[0]/Base[0] << ',' << Tint[1]/Base[1] << ',' << Tint[2]/Base[2] << " repeat_exact=1" << std::endl;
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
        for (uint32_t Address : Rough) Scene.Angles[Address] = 3;
        auto Hybrid = Render();
        Require(Pixels == Hybrid,"Off-screen glass screen/BVH fallback differs from full BVH");
        for (uint32_t Address : Rough) Scene.Angles[Address] = 0;
        std::cout << "ROUGH_GLASS_SCREEN case=" << Case << " bvh_exact=1" << std::endl;
        WriteImage(Output+"/RoughDielectric"+std::to_string(Case)+".ppm",Pixels,Scene.Width,Scene.Height);
    }
    for (auto& F : Scene.Surfaces) for (uint32_t C = 0; C < 3; ++C) F.Emissive[C] = 0;
    auto Black = Compute.Execute(Scene,0,16,71,64,0,false,true,Samples,false);
    for (auto P : Black) for (uint32_t C = 0; C < 3; ++C) Require(P[C] == 0,"Rough solid transport retained removed emission");
    std::cout << "ROUGH_DIELECTRIC_PASS source_off_black=1 pixels=" << Black.size() << " samples=" << Samples << " proposals_per_sample=2" << std::endl;
}
