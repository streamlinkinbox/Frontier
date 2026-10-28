/*==========================================================================================================================================
                                                     GLAZINGMOTIONVALIDATION.H
==========================================================================================================================================*/
// 🧩 Six actual vehicle poses with closed panes, native rigid reprojection checks and fresh directional-glass references.

void ValidateGlazingMotion(const char*        Driver,
                          const std::string& Shaders,
                          const std::string& Directory,
                          const std::string& Output,
                          const std::string& Saved,
                          bool               NativeOnly = false)
{
    using namespace TransportReference;
    constexpr uint32_t GlassToken = 100700, MirrorToken = 100301;
    std::filesystem::create_directories(Output);
    const auto Coefficients = ReadGlazingSpectra(Saved);
    std::vector<LightComputeArchive> Frames;
    double Volume = 0;
    uint32_t NativeChecks = 0;
    for (uint32_t Pose = 0; Pose < 6; ++Pose)
    {
        auto Raw = LightComputeArchive::Load(Directory+"/VehicleMotion"+std::to_string(Pose)+".bin");
        const double Angle = Raw.Camera[8][0];
        Require(std::abs(Angle-.08*Pose) < 1e-6,"Unexpected archived body transform");
        Require(Raw.Camera[8][2] > 0 && Raw.Camera[8][2] < MirrorToken,"Moving pane token is not above vehicle threshold");
        VehicleGlazingView View;
        View.Width = 192; View.Height = 120;
        View.Angle = Angle; View.GlassToken = GlassToken; View.MirrorToken = MirrorToken;
        View.Eye = Pose < 3 ? SpatialVector{6,-7,3.5} : SpatialVector{3.2,-5.5,2.4};
        View.Focus = {.8*Angle,0,1};
        auto Frame = AssembleVehicleGlazing(std::move(Raw),Shaders,View);
        if (Pose == 0) Volume = Frame.Volume;
        Require(std::abs(Frame.Volume-Volume) < 1e-7,"Moving panes changed enclosed volume");
        auto Scene = std::move(Frame.Closed);
        BindGlazingSpectrum(Scene,Frame.Address,Coefficients[3]);
        // 📝 Preserve the archive's rigid body angle/translation metadata; only the chosen test camera changes.
        if (Pose)
            for (uint32_t I = 0; I < 4; ++I) Scene.Camera[4+I] = Frames.back().Camera[I];
        EmissiveQuadrature::AppendDistribution(Scene);
        uint32_t Glass = 0, Mirrors = 0;
        for (const auto &F : Scene.Surfaces)
        {
            Glass += F.OpticalToken[3] == GlassToken;
            Mirrors += F.OpticalToken[3] == MirrorToken;
        }
        Require(Glass == 284 && Mirrors == 4,"Moving derived geometry/tokens changed");
        // 📝 Independently unpose each mirror vertex; every frame must retain the four original reflector facets.
        uint32_t MirrorCorners = 0;
        for (const auto &F : Scene.Surfaces)
            if (F.OpticalToken[3] == MirrorToken)
                for (const auto &P : {F.A,F.B,F.C})
                {
                    const double X = P[0]-.8*Angle, Y = P[1];
                    const double LocalX = std::cos(Angle)*X+std::sin(Angle)*Y;
                    const double LocalY = -std::sin(Angle)*X+std::cos(Angle)*Y;
                    Require(std::abs(LocalX-.85) <= .035001 && std::abs(std::abs(LocalY)-1.24) <= .170001 &&
                            P[2] >= 1.20999 && P[2] <= 1.39001,"Mirror detached from moving body");
                    ++MirrorCorners;
                }
        Require(MirrorCorners == 12,"Missing posed mirror corners");
        NativeChecks += 4;
        std::cout << "GLAZING_MOTION_SCENE pose=" << Pose << " angle=" << Angle << " glass_triangles=" << Glass
                  << " mirror_triangles=" << Mirrors << " volume_m3=" << Frame.Volume << std::endl;
        Frames.push_back(std::move(Scene));
    }
    std::cout << "GLAZING_MOTION_NATIVE pose_groups=" << NativeChecks
              << " poses=6 closed_triangles=284 moving_mirror_corners=12 source_ior=" << Coefficients[3][0] << std::endl;
    if (NativeOnly) return;
    auto Capacity = Frames.front();
    for (const auto &F : Frames)
    {
        Capacity.Visibility.Enclosures.resize(std::max(Capacity.Visibility.Enclosures.size(),F.Visibility.Enclosures.size()));
        Capacity.Angles.resize(std::max(Capacity.Angles.size(),F.Angles.size()));
    }
    const auto DebugPose = std::getenv("GLAZING_DEBUG_POSE");
    LightComputeSubmission History(Driver), Fresh(Driver);
    History.Prepare(Capacity,Shaders); if (!DebugPose) Fresh.Prepare(Capacity,Shaders);
    std::cout << "GLAZING_MOTION_DEVICE name=" << History.Properties.deviceName << " type=" << History.Properties.deviceType
              << " triangle_bvh=1 hardware_ray_query=0 width=192 height=120 samples=16 interaction_cap=2048" << std::endl;
    uint32_t TotalGlass = 0, TotalMirror = 0, TotalWarm = 0;
    double MotionError = 0, DirectError = 0;
    std::vector<std::array<float,4>> Last;
    auto Compare = [&](LightComputeArchive& Scene,uint32_t Seed,bool ExpectReset,const std::string& Name)
    {
        // 📝 Both submissions have identical direct histories. Only one enables indirect reconstruction.
        auto Raw = DebugPose ? std::vector<std::array<float,4>>{} : Fresh.Execute(Scene,3,16,Seed,64,7,false,true,16,true,true,false);
        auto RawReceivers = DebugPose ? std::vector<LightComputeReceiver>{} : Fresh.PrimaryReceivers();
        const auto Image = History.Execute(Scene,3,16,Seed,64,7,false,true,16,true,true,true);
        const auto Receivers = History.PrimaryReceivers();
        if (DebugPose) { Raw = Image; RawReceivers = Receivers; }
        const auto Diagnostic = History.TransportSlab(20), Incoming = History.TransportSlab(16), Resolved = History.TransportSlab(19);
        uint32_t Glass = 0, Mirrors = 0, Reused = 0, Capped = 0, Invalid = 0, Probes = 0;
        for (uint32_t I = 0; I < Image.size(); ++I)
        {
            const auto &R = Receivers[I];
            Reused += Diagnostic[I][0] > 0;
            for (uint32_t C = 0; C < 3; ++C)
            {
                Require(std::isfinite(Image[I][C]) && Image[I][C] >= 0,"Invalid moving-vehicle radiance");
                const double Delta = std::abs(double(Image[I][C]-R.ResidualSpecular[C])-
                                              double(Raw[I][C]-RawReceivers[I].ResidualSpecular[C]));
                DirectError = std::max(DirectError,Delta);
            }
            const bool Pane = R.MotionPositionToken[3] == GlassToken;
            const bool Mirror = R.MotionPositionToken[3] == MirrorToken;
            if (!Pane && !Mirror) continue;
            Glass += Pane; Mirrors += Mirror;
            Require(Diagnostic[I][0] == 0 && Diagnostic[I][1] == 0 && Diagnostic[I][2] == 1,
                    "Directional glass/mirror signal entered opaque history/filter");
            for (uint32_t C = 0; C < 3; ++C)
            {
                Require(Incoming[I][C] == Resolved[I][C],"Reconstruction blurred a directional boundary signal");
                Require(R.ResidualSpecular[C] == RawReceivers[I].ResidualSpecular[C],"Directional boundary differs from fresh reference");
            }
            if (Mirror) Require(R.ExpectedDirect[2] == 0,"Moving mirror reported capped/unsupported transport");
            if (Pane)
            {
                Require(R.ExpectedDirect[0] == 5,"Visible pane did not execute volume transport");
                const auto Cap = uint32_t(R.ExpectedDirect[1])/65536, Bad = uint32_t(R.ExpectedDirect[2]);
                Capped += Cap; Invalid += Bad;
                if (Cap || Bad) std::cerr << "GLAZING_MOTION_BAD name=" << Name << " pixel=" << I
                    << " cap=" << Cap << " unsupported=" << Bad << " position=" << R.PositionHit[0] << ','
                    << R.PositionHit[1] << ',' << R.PositionHit[2] << " diagnostic=" << R.ExpectedDirect[0] << ','
                    << R.ExpectedDirect[1] << ',' << R.ExpectedDirect[2] << std::endl;
            }
            if (I%3 == 0)
            {
                const double Current = Scene.Camera[8][0], Previous = Scene.Camera[9][0];
                const double X = R.PositionHit[0]-Scene.Camera[8][1], Y = R.PositionHit[1];
                const double LocalX = std::cos(Current)*X+std::sin(Current)*Y;
                const double LocalY = -std::sin(Current)*X+std::cos(Current)*Y;
                const double PriorX = std::cos(Previous)*LocalX-std::sin(Previous)*LocalY+Scene.Camera[9][1];
                const double PriorY = std::sin(Previous)*LocalX+std::cos(Previous)*LocalY;
                MotionError = std::max({MotionError,std::abs(PriorX-R.MotionPositionToken[0]),std::abs(PriorY-R.MotionPositionToken[1])});
                ++Probes;
            }
        }
        std::cout << "GLAZING_MOTION_COUNTS name=" << Name << " glass=" << Glass << " mirrors=" << Mirrors
                  << " probes=" << Probes << " capped=" << Capped << " unsupported=" << Invalid << std::endl;
        Require(Glass > 100 && Probes > 0 && !Capped && !Invalid,"Moving glass geometry/transport acceptance failed");
        Require(MotionError < 2e-5 && DirectError < 2e-5,"Moving boundary/direct histories differ from native reference");
        if (ExpectReset) Require(Reused == 0,"Moved authored geometry retained obsolete indirect history");
        else { Require(Reused > 0,"Static opaque history reuse was not exercised"); TotalWarm += Reused; }
        TotalGlass += Glass; TotalMirror += Mirrors;
        std::cout << "GLAZING_MOTION_FRAME name=" << Name << " glass_pixels=" << Glass << " mirror_pixels=" << Mirrors
                  << " opaque_reused=" << Reused << " capped=" << Capped << " unsupported=" << Invalid
                  << " native_motion_probes=" << Probes << " max_motion_error=" << MotionError
                  << " direct_max_error=" << DirectError << " directional_fresh_equal=" << int(!DebugPose) << std::endl;
        WriteImage(Output+"/"+Name+".ppm",Image,Scene.Width,Scene.Height);
        Last = Image;
    };
    if (DebugPose)
    {
        const auto Pose = uint32_t(std::stoul(DebugPose));
        Require(Pose < 6,"Invalid diagnostic pose");
        std::cout << "GLAZING_DIAGNOSTIC_ONLY fresh_reference=0 acceptance=0" << std::endl;
        Compare(Frames[Pose],71,true,"GlazingDebug"+std::to_string(Pose));
        return; // 📝 Diagnostic-only run never emits the six-pose acceptance marker.
    }
    Compare(Frames[0],71,true,"GlazingMotion0");
    Compare(Frames[0],72,false,"GlazingMotionWarm");
    for (uint32_t Pose = 1; Pose < 6; ++Pose) Compare(Frames[Pose],71,true,"GlazingMotion"+std::to_string(Pose));
    // 📝 Static source revision is isolated from body movement: change emission without moving a single facet/token.
    auto Changed = Frames.back();
    for (uint32_t I = 0; I < 4; ++I) Changed.Camera[4+I] = Changed.Camera[I];
    for (uint32_t I = 0; I < 3; ++I) Changed.Camera[9][I] = Changed.Camera[8][I];
    Changed.Camera[11][2] = Changed.Camera[11][0]; Changed.Camera[11][3] = Changed.Camera[11][1];
    for (auto &F : Changed.Surfaces)
        for (uint32_t C = 0; C < 3; ++C) F.Emissive[C] *= C == 0 ? .1f : C == 1 ? .3f : .6f;
    Changed.Angles.resize(size_t(Changed.Camera[10][2]));
    EmissiveQuadrature::AppendDistribution(Changed);
    const auto Before = Last;
    Compare(Changed,71,true,"GlazingMotionEmission");
    Require(Last != Before,"Static emission revision had no rendered effect");
    auto Dark = Changed;
    for (auto &F : Dark.Surfaces) F.Emissive[0] = F.Emissive[1] = F.Emissive[2] = 0;
    for (auto &L : Dark.Lights) L.PeakActive[1] = 0;
    Dark.Camera[10][3] = 0;
    Dark.Angles.resize(size_t(Dark.Camera[10][2]));
    EmissiveQuadrature::AppendDistribution(Dark);
    const auto Black = History.Execute(Dark,3,16,71,64,7,false,true,16,true,true,true);
    for (auto P : Black) Require(P[0] == 0 && P[1] == 0 && P[2] == 0,"Moving glass retained removed lighting");
    std::cout << "GLAZING_MOTION_PASS poses=6 warm_opaque_reused=" << TotalWarm << " tested_glass_pixels=" << TotalGlass
              << " tested_mirror_pixels=" << TotalMirror << " emission_revision=1 all_sources_off_black=" << Black.size()
              << " conservative_geometry_reset=1 glass_denoising=0" << std::endl;
}
