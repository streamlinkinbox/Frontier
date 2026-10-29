/*==========================================================================================================================================
                                                     VEHICLEGLAZINGVALIDATION.H
==========================================================================================================================================*/
// 🧩 Render retained vehicle window surfaces as validated closed panes using native saved material coefficients.

void ValidateVehicleGlazing(const char*        Driver,
                           const std::string& Shaders,
                           LightComputeArchive Scene,
                           const std::string& Output,
                           const std::string& Saved,
                           bool               Probe = false)
{
    using namespace TransportReference;
    std::filesystem::create_directories(Output);
    const auto Coefficients = ReadGlazingSpectra(Saved);
    auto Frame = AssembleVehicleGlazing(std::move(Scene),Shaders);
    const auto Thin = std::move(Frame.Thin);
    Scene = std::move(Frame.Closed);
    const auto Address = Frame.Address;
    LightComputeSubmission Compute(Driver);
    auto Capacity = Scene;
    Capacity.Visibility.Enclosures.resize(std::max(Capacity.Visibility.Enclosures.size(),Thin.Visibility.Enclosures.size()));
    Compute.Prepare(Capacity,Shaders);
    std::cout << "GLAZING_DEVICE name=" << Compute.Properties.deviceName << " type=" << Compute.Properties.deviceType
              << " triangle_bvh=1 hardware_ray_query=0 interaction_cap=2048" << std::endl;
    auto Render = [&](const char* Name)
    {
        const auto Pixels = Compute.Execute(Scene,0,16,71,64,0,true,true,16,true);
        WriteImage(Output+"/"+Name+".ppm",Pixels,Scene.Width,Scene.Height);
        std::cout << "GLAZING_CAPTURE name=" << Name << " width=320 height=200" << std::endl;
        return Pixels;
    };
    const auto Closed = Scene;
    if (!Probe) { Scene = Thin; Render("VehicleGlazingSheet"); Scene = Closed; }
    auto Apply = [&](uint32_t Case) { BindGlazingSpectrum(Scene,Address,Coefficients[Case]); };
    if (Probe)
    {
        Apply(3); Render("VehicleGlazingProbe");
        const auto Receivers = Compute.PrimaryReceivers();
        uint32_t Capped = 0, Unsupported = 0;
        for (uint32_t I = 0; I < Receivers.size(); ++I)
            if (Receivers[I].MotionPositionToken[3] == 700)
            {
                const auto &R = Receivers[I];
                Capped += uint32_t(R.ExpectedDirect[1])/65536; Unsupported += uint32_t(R.ExpectedDirect[2]);
                if (uint32_t(R.ExpectedDirect[1])/65536 || R.ExpectedDirect[2])
                    std::cout << "GLAZING_PROBE pixel=" << I << " capped=" << uint32_t(R.ExpectedDirect[1])/65536
                              << " tir=" << uint32_t(R.ExpectedDirect[1])%65536 << " unsupported=" << R.ExpectedDirect[2] << std::endl;
            }
        std::cout << "GLAZING_PROBE_TOTAL capped=" << Capped << " unsupported=" << Unsupported << std::endl;
        return;
    }
    Apply(2); const auto Clear = Render("VehicleGlazingClear");
    Apply(3); const auto Absorbed = Render("VehicleGlazingAbsorbed");
    const auto Repeat = Compute.Execute(Scene,0,16,71,64,0,true,true,16,true);
    Require(Repeat == Absorbed,"Vehicle glazing seeded repeat changed");
    uint32_t GlassPixels = 0, Capped = 0, Unsupported = 0, Changed = 0;
    double Maximum = 0, Decrease = 0;
    const auto Receivers = Compute.PrimaryReceivers();
    for (uint32_t I = 0; I < Receivers.size(); ++I)
    {
        for (uint32_t C = 0; C < 3; ++C)
            Require(std::isfinite(Absorbed[I][C]) && Absorbed[I][C] >= 0,"Invalid vehicle radiance");
        if (Receivers[I].MotionPositionToken[3] != 700) continue;
        ++GlassPixels;
        Capped += uint32_t(Receivers[I].ExpectedDirect[1])/65536;
        Unsupported += uint32_t(Receivers[I].ExpectedDirect[2]);
        for (uint32_t C = 0; C < 3; ++C)
        {
            Require(Absorbed[I][C] <= Clear[I][C]+1e-5,"Absorption increased primary glass radiance");
            const double Delta = Clear[I][C]-Absorbed[I][C];
            Changed += Delta > 1e-5; Maximum = std::max(Maximum,Delta); Decrease += Delta;
        }
    }
    std::cout << "GLAZING_RENDER glass_pixels=" << GlassPixels << " changed_channels=" << Changed
              << " max_decrease=" << Maximum << " total_decrease=" << Decrease << " capped=" << Capped
              << " unsupported=" << Unsupported << " repeat_exact=1" << std::endl;
    Require(GlassPixels > 100 && Changed > 100 && !Capped && !Unsupported,"Vehicle glass acceptance failed");
    MaterialCamera(Scene,{5.8,-6.8,3.4},{0,0,1},320,200);
    const auto Moved = Compute.Execute(Scene,0,16,72,64,0,false,true,16,true);
    const auto Cold = Compute.Execute(Scene,0,16,72,64,0,true,true,16,true);
    Require(Moved == Cold,"Moved glazing camera differs from cold transport");
    WriteImage(Output+"/VehicleGlazingMoved.ppm",Moved,Scene.Width,Scene.Height);
    std::cout << "GLAZING_MOTION camera_moved=1 cold_equal=1 fresh_transport=1" << std::endl;
    for (auto &F : Scene.Surfaces) F.Emissive[0] = F.Emissive[1] = F.Emissive[2] = 0;
    for (auto &L : Scene.Lights) L.PeakActive[1] = 0;
    Scene.Camera[10][3] = 0;
    const auto Black = Compute.Execute(Scene,0,16,71,64,0,false,true,16,true);
    for (const auto &P : Black) Require(P[0] == 0 && P[1] == 0 && P[2] == 0,"Vehicle glass retained a removed source");
    std::cout << "GLAZING_RESET all_sources_off_black=1 pixels=" << Black.size() << std::endl;
}
