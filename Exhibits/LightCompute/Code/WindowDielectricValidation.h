/*==========================================================================================================================================
                                                   WINDOWDIELECTRICVALIDATION.H
==========================================================================================================================================*/
// 🧩 Compare a primary parallel sheet followed by closed glass with independent sheet/slab arithmetic and live source changes.
void ValidateWindowDielectric(const char* Driver, const std::string& Shaders, const std::string& Output, bool Original = false)
{
    using namespace TransportReference;
    std::filesystem::create_directories(Output);
    LightComputeArchive Scene;
    constexpr uint32_t Samples = 64;
    Prepare(Scene,Samples,4,false);
    auto Materials = AppendSceneMaterials(Scene,Shaders);
    uint32_t Glass = DuplicateSceneMaterial(Scene,Materials[3],4,{1,1,1,0},0), Offset = Glass-1;
    uint32_t Sheet = DuplicateSceneMaterial(Scene,Materials[3],2,{1,1,1,0},0);
    MaterialRectangle(Scene,{0,-1.5,1},{4,0,0},{0,0,4},Sheet,1500);
    MaterialRectangle(Scene,{0,-.5,1},{1,0,0},{0,0,1},Glass,1100);
    MaterialRectangle(Scene,{0,.5,1},{-1,0,0},{0,0,1},Glass,1100);
    MaterialRectangle(Scene,{1,0,1},{0,.5,0},{0,0,1},Glass,1100);
    MaterialRectangle(Scene,{-1,0,1},{0,-.5,0},{0,0,1},Glass,1100);
    MaterialRectangle(Scene,{0,0,2},{1,0,0},{0,.5,0},Glass,1100);
    MaterialRectangle(Scene,{0,0,0},{1,0,0},{0,-.5,0},Glass,1100);
    MaterialRectangle(Scene,{0,-6,1},{4,0,0},{0,0,4},0,1600);
    for (uint32_t Z = 0; Z < 12; ++Z)
        for (uint32_t X = 0; X < 20; ++X)
        {
            MaterialRectangle(Scene,{-3.8+X*.4,2,-1.2+Z*.4},{.2,0,0},{0,0,.2},0,1200+X+Z*20);
            for (size_t I = Scene.Surfaces.size()-2; I < Scene.Surfaces.size(); ++I) Scene.Surfaces[I].Emissive = {1,1,1,0};
        }
    RebuildMaterialVisibility(Scene);
    MaterialCamera(Scene,{0,-4,1},{0,0,1},96,64);
    Scene.Camera[8][2] = 1e9f;
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Scene,Shaders);
    std::cout << "WINDOW_DEVICE name=" << Compute.Properties.deviceName << " type=" << Compute.Properties.deviceType << std::endl;
    auto Render = [&](bool Reset = true) {
        auto P = Compute.Execute(Scene,0,16,71,64,0,Reset,true,Samples,false);
        auto R = Compute.PrimaryReceivers();
        for (size_t I = 0; I < R.size(); ++I)
        {
            Require(R[I].ExpectedDirect[0] == 2,"Window fixture lost primary sheet coverage");
            Require(R[I].ExpectedDirect[2] == 0,"Window volume path capped or unsupported");
            for (uint32_t C = 0; C < 3; ++C) Require(std::isfinite(P[I][C]) && P[I][C] >= 0,"Invalid window transport");
        }
        return P;
    };
    for (uint32_t Case = 0; Case < 4; ++Case)
    {
        double Ior = Case < 2 ? 1 : 1.5;
        std::array<double,3> Sigma = Case%2 ? std::array<double,3>{.2,.6,1.2} : std::array<double,3>{0,0,0};
        Scene.Angles[Offset+13] = float(Ior);
        for (uint32_t C = 0; C < 3; ++C) Scene.Angles[Offset+16+C] = float(Sigma[C]);
        auto P = Render();
        auto R = Compute.PrimaryReceivers();
        uint32_t Count = 0;
        std::array<double,3> Sum{}, Expected{};
        for (size_t I = 0; I < R.size(); ++I)
        {
            if (std::abs(R[I].PositionHit[0]) > .2 || std::abs(R[I].PositionHit[2]-1) > .2) continue;
            double Cosine = std::abs(R[I].ViewMetallic[1]), Fw = .04+.96*std::pow(1-Cosine,5);
            auto B = EvaluateDielectricRefraction({std::sqrt(1-Cosine*Cosine),0,-Cosine},{0,0,1},1,Ior);
            double Ct = std::sqrt(1-(1-Cosine*Cosine)/(Ior*Ior)), F = B.Reflectance;
            for (uint32_t C = 0; C < 3; ++C)
            {
                constexpr double SheetSigma[]{.8,.15,.4};
                double Tw = (1-Fw)/(1+Fw)*std::exp(-SheetSigma[C]*.006/std::max(.05,Cosine));
                double A = std::exp(-Sigma[C]/Ct);
                Expected[C] += Tw*(1-F)*(1-F)*A/(1-F*F*A*A);
                Sum[C] += P[I][C];
            }
            ++Count;
        }
        Require(Count > 50,"Insufficient window analytic coverage");
        if (Original)
        {
            for (double C : Sum) Require(C == 0,"Pinned window route did not reproduce blocked solid transmission");
            std::cout << "WINDOW_ORIGINAL analytic_pixels=" << Count << " blocked_black=1" << std::endl;
            return;
        }
        double Error = 0;
        for (uint32_t C = 0; C < 3; ++C) Error = std::max(Error,std::abs(Sum[C]-Expected[C])/Count);
        std::cout << "WINDOW_SLAB case=" << Case << " analytic_pixels=" << Count << " mean_error=" << Error << std::endl;
        Require(Error < (Ior == 1 ? .0001 : .03),"Window-to-volume analytic slab failed");
    }
    for (auto& F : Scene.Surfaces)
        if (F.OpticalToken[3] >= 1200 && F.OpticalToken[3] < 1440)
        {
            uint32_t Tile = uint32_t(F.OpticalToken[3])-1200;
            F.Emissive = ((Tile%20)+(Tile/20))%2 ? std::array<float,4>{3,.7f,.12f,0} : std::array<float,4>{.08f,.35f,1.4f,0};
        }
    for (uint32_t Case = 0; Case < 3; ++Case)
    {
        Scene.Angles[Offset+13] = Case ? 1.5f : 1.f;
        for (uint32_t C = 0; C < 3; ++C) Scene.Angles[Offset+16+C] = Case == 2 ? std::array<float,3>{.2f,.6f,1.2f}[C] : 0;
        auto P = Render();
        WriteImage(Output+"/WindowDielectric"+std::to_string(Case)+".ppm",P,Scene.Width,Scene.Height);
    }
    Scene.Angles[Offset+13] = 1;
    for (uint32_t C = 0; C < 3; ++C) Scene.Angles[Offset+16+C] = 0;
    for (uint32_t Case = 0; Case < 2; ++Case)
    {
        for (auto& F : Scene.Surfaces)
            if ((F.OpticalToken[3] >= 1200 && F.OpticalToken[3] < 1440) || F.OpticalToken[3] == 1600)
                F.Emissive = F.OpticalToken[3] == 1600 || Case == 1 ? std::array<float,4>{1,1,1,0} : std::array<float,4>{0,0,0,0};
        auto P = Render(); auto R = Compute.PrimaryReceivers();
        double Error = 0; uint32_t Count = 0;
        for (size_t I = 0; I < R.size(); ++I)
        {
            if (std::abs(R[I].PositionHit[0]) > .2 || std::abs(R[I].PositionHit[2]-1) > .2) continue;
            double Cosine = std::abs(R[I].ViewMetallic[1]), F = .04+.96*std::pow(1-Cosine,5);
            for (uint32_t C = 0; C < 3; ++C)
            {
                constexpr double Sigma[]{.8,.15,.4};
                double Expected = 2*F/(1+F);
                if (Case) Expected += (1-F)/(1+F)*std::exp(-Sigma[C]*.006/Cosine);
                Error = std::max(Error,std::abs(P[I][C]-Expected));
            }
            ++Count;
        }
        Require(Count > 50 && Error < 1e-5,"Thin-sheet reflection/transmission preservation failed");
        std::cout << "WINDOW_ENERGY case=" << Case << " analytic_pixels=" << Count << " max_error=" << Error << std::endl;
    }
    for (auto& F : Scene.Surfaces) for (uint32_t C = 0; C < 3; ++C) F.Emissive[C] = 0;
    auto Black = Render(false);
    for (auto P : Black) for (uint32_t C = 0; C < 3; ++C) Require(P[C] == 0,"Window transport retained removed emission");
    std::cout << "WINDOW_SOURCE_OFF black=1 pixels=" << Black.size() << " samples=" << Samples << " directions_per_sample=2" << std::endl;
}
