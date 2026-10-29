/*==========================================================================================================================================
                                                   MIRRORDIELECTRICVALIDATION.H
==========================================================================================================================================*/
// 🧩 Render a closed volume behind the camera through a planar mirror; verify slab energy, absorption and source removal.
void ValidateMirrorDielectric(const char* Driver, const std::string& Shaders, const std::string& Output, bool Original = false)
{
    using namespace TransportReference;
    std::filesystem::create_directories(Output);
    LightComputeArchive Scene;
    Prepare(Scene,32,4,false);
    auto Materials = AppendSceneMaterials(Scene,Shaders);
    uint32_t Glass = DuplicateSceneMaterial(Scene,Materials[3],4,{1,1,1,0},0), Offset = Glass-1;
    uint32_t Mirror = DuplicateSceneMaterial(Scene,Materials[3],1,{1,1,1,0},1);
    MaterialRectangle(Scene,{0,0,1},{4,0,0},{0,0,4},Mirror,1300);
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
    std::cout << "MIRROR_DIELECTRIC_DEVICE name=" << Compute.Properties.deviceName << " type=" << Compute.Properties.deviceType << std::endl;
    for (uint32_t Case = 0; Case < 4; ++Case)
    {
        double Ior = Case < 2 ? 1 : 1.5;
        Scene.Angles[Offset+13] = float(Ior);
        std::array<double,3> Sigma = Case%2 ? std::array<double,3>{.2,.6,1.2} : std::array<double,3>{0,0,0};
        for (uint32_t C = 0; C < 3; ++C) Scene.Angles[Offset+16+C] = float(Sigma[C]);
        auto Pixels = Compute.Execute(Scene,0,16,71,64,0,true,true,32,false);
        auto R = Compute.PrimaryReceivers();
        uint32_t Count = 0;
        std::array<double,3> Sum{}, Expected{};
        for (size_t P = 0; P < R.size(); ++P)
        {
            Require(R[P].ExpectedDirect[2] == 0,"Mirror volume path capped or unsupported");
            Require(R[P].ExpectedDirect[0] == 1,"Fixture did not see the volume exclusively through the mirror");
            for (uint32_t C = 0; C < 3; ++C) Require(std::isfinite(Pixels[P][C]) && Pixels[P][C] >= 0,"Invalid secondary volume radiance");
            if (std::abs(R[P].PositionHit[0]) > .4 || std::abs(R[P].PositionHit[2]-1) > .4) continue;
            double Cosine = std::abs(R[P].ViewMetallic[1]);
            auto Boundary = EvaluateDielectricRefraction({std::sqrt(1-Cosine*Cosine),0,-Cosine},{0,0,1},1,Ior);
            double Ct = std::sqrt(1-(1-Cosine*Cosine)/(Ior*Ior)), F = Boundary.Reflectance;
            for (uint32_t C = 0; C < 3; ++C)
            {
                double A = std::exp(-Sigma[C]/Ct);
                Expected[C] += .94*(1-F)*(1-F)*A/(1-F*F*A*A);
                Sum[C] += Pixels[P][C];
            }
            ++Count;
        }
        double Error = 0;
        for (uint32_t C = 0; C < 3; ++C) Error = std::max(Error,std::abs(Sum[C]-Expected[C])/std::max(1u,Count));
        if (Original)
        {
            Require(Count > 100,"Negative-control mirror coverage is insufficient");
            for (double C : Sum) Require(C == 0,"Negative control did not reproduce blocked solid mirror rays");
            std::cout << "MIRROR_NEGATIVE_CONTROL central_pixels=" << Count << " blocked_black=1 routing_disabled=1" << std::endl;
            return;
        }
        std::cout << "MIRROR_SLAB case=" << Case << " analytic_pixels=" << Count << " mean_error=" << Error << std::endl;
        Require(Count > 100 && Error < (Ior == 1 ? .0001 : .08),"Mirror dielectric analytic slab failed");
    }
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
        auto Pixels = Compute.Execute(Scene,0,16,71,64,0,true,true,32,false);
        WriteImage(Output+"/MirrorDielectric"+std::to_string(Case)+".ppm",Pixels,Scene.Width,Scene.Height);
    }
    for (auto& F : Scene.Surfaces) for (uint32_t C = 0; C < 3; ++C) F.Emissive[C] = 0;
    auto Black = Compute.Execute(Scene,0,16,71,64,0,false,true,32,false);
    for (auto P : Black) for (uint32_t C = 0; C < 3; ++C) Require(P[C] == 0,"Secondary volume retained removed emission");
    std::cout << "MIRROR_SOURCE_OFF black=1 pixels=" << Black.size() << std::endl;
}
