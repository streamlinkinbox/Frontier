/*==========================================================================================================================================
                                                     ROUGHREFLECTIONVALIDATION.H
==========================================================================================================================================*/
// 🧩 Compare actual rough-lobe screen/BVH integration against identical seeded BVH rays, source refresh and white illumination.

//------------------------------------------------------------------------------------------------------------------------
//                                                      REFLECTION CAPTURES
//------------------------------------------------------------------------------------------------------------------------

void ValidateRoughReflections(const char* Driver, const std::string& Shaders, const std::string& Output, bool SharpCapture = false)
{
    using namespace TransportReference;
    std::filesystem::create_directories(Output);
    LightComputeArchive Scene;
    constexpr uint32_t Samples = 16;
    Prepare(Scene, Samples, 8, false);
    auto Materials = AppendSceneMaterials(Scene, Shaders);
    std::array<uint32_t, 4> Addresses{};
    for (uint32_t I = 0; I < 4; ++I)
    {
        constexpr float Roughness[]{.045f, .2f, .45f, .8f};
        Addresses[I] = DuplicateSceneMaterial(Scene, Materials[3], 3, {.8f, .8f, .8f, Roughness[I]}, 1);
        MaterialRectangle(Scene, {-1.5 + I, 0, 0}, {.48, 0, 0}, {0, 2, 0}, Addresses[I], 800 + I);
    }
    // 📝 Bright/dark geometry is real radiance, not a post-process reflection texture.
    for (uint32_t Y = 0; Y < 8; ++Y)
        for (uint32_t X = 0; X < 16; ++X)
        {
            MaterialRectangle(Scene, {-1.875 + X * .25, 2.4, .175 + Y * .25}, {.125, 0, 0}, {0, 0, .125}, 0, 900);
            for (size_t I = Scene.Surfaces.size() - 2; I < Scene.Surfaces.size(); ++I)
                Scene.Surfaces[I].Emissive = (X + Y) % 2 ? std::array<float, 4>{8, 6, 3, 0} : std::array<float, 4>{.025f, .035f, .06f, 0};
        }
    // 📝 This red rectangle is outside the primary view, but rough reflection rays can still reach it.
    MaterialRectangle(Scene, {-4.5, -.5, 1}, {0, .65, 0}, {0, 0, .9}, 0, 901);
    for (size_t I = Scene.Surfaces.size() - 2; I < Scene.Surfaces.size(); ++I)
        Scene.Surfaces[I].Emissive = {12, .05f, .02f, 0};
    RebuildMaterialVisibility(Scene);
    MaterialCamera(Scene, {0, -4, 3.2}, {0, .4, .35}, 192, 120);
    LightComputeSubmission Compute(Driver);
    std::cout << "ROUGH_DEVICE name=" << Compute.Properties.deviceName << " type=" << Compute.Properties.deviceType
              << " software_vulkan=" << (Compute.Properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) << std::endl;
    Compute.Prepare(Scene, Shaders);
    auto Render = [&](const LightComputeArchive& S, const char* Name, uint32_t Seed = 71) {
        auto Pixels = Compute.Execute(S, 0, 16, Seed, 64, 0, true, true, Samples, false);
        for (const auto& P : Pixels)
            for (uint32_t C = 0; C < 3; ++C)
                Require(std::isfinite(P[C]) && P[C] >= 0, "Invalid rough reflection radiance");
        WriteImage(Output + "/" + Name + ".ppm", Pixels, S.Width, S.Height);
        return Pixels;
    };
    auto Hybrid = Render(Scene, SharpCapture ? "RoughSharp" : "RoughHybrid");
    if (SharpCapture)
    {
        std::cout << "ROUGH_SHARP_CAPTURE original_shader=1 width=" << Scene.Width << " height=" << Scene.Height << std::endl;
        return;
    }
    auto Receivers = Compute.PrimaryReceivers();
    uint64_t Screen = 0, Fallback = 0;
    uint32_t Floor = 0, HiddenSourcePrimary = 0;
    std::array<double, 4> FloorSum{};
    std::array<uint32_t, 4> FloorCount{};
    for (size_t I = 0; I < Receivers.size(); ++I)
    {
        const auto& R = Receivers[I];
        HiddenSourcePrimary += R.MotionPositionToken[3] == 901;
        if (R.ExpectedDirect[0] != 3 && R.ExpectedDirect[0] != 4) continue;
        ++Floor; Fallback += uint32_t(R.ExpectedDirect[1]); Screen += uint32_t(R.ExpectedDirect[2]);
        uint32_t Column = uint32_t(R.MotionPositionToken[3]) - 800;
        Require(Column < 4, "Reflection column token changed");
        FloorSum[Column] += Hybrid[I][0]; ++FloorCount[Column];
    }
    Require(Screen > 0 && Fallback > 0 && Floor > 100, "Missing rough reflection route coverage");
    Require(HiddenSourcePrimary == 0, "Off-screen source unexpectedly visible in primary view");
    auto Repeated = Render(Scene, "RoughRepeat");
    Require(Hybrid == Repeated, "Seeded rough reflection render is not deterministic");
    auto Bvh = Scene;
    for (uint32_t Address : Addresses) Bvh.Angles[Address] = 0;
    auto Reference = Render(Bvh, "RoughBvh");
    double Maximum = 0, Mean = 0;
    for (size_t I = 0; I < Hybrid.size(); ++I)
        for (uint32_t C = 0; C < 3; ++C)
        {
            double Difference = std::abs(Hybrid[I][C] - Reference[I][C]);
            Maximum = std::max(Maximum, Difference); Mean += Difference;
        }
    Mean /= Hybrid.size() * 3;
    Require(Maximum < .002 && Mean < .00001, "Screen-candidate path differs from identical BVH rays");
    auto NoHiddenSource = Scene;
    for (auto& F : NoHiddenSource.Surfaces)
        if (F.OpticalToken[3] == 901) F.Emissive[0] = F.Emissive[1] = F.Emissive[2] = 0;
    auto WithoutHidden = Render(NoHiddenSource, "RoughNoHiddenSource");
    double HiddenArrival = 0;
    for (size_t I = 0; I < Hybrid.size(); ++I)
        if (Receivers[I].ExpectedDirect[0] == 3 || Receivers[I].ExpectedDirect[0] == 4)
            HiddenArrival += std::max(0.f, Hybrid[I][0] - WithoutHidden[I][0]);
    Require(HiddenArrival > .01, "Off-screen reflection source contributed nothing");
    auto Dark = Scene;
    for (auto& F : Dark.Surfaces) F.Emissive[0] = F.Emissive[1] = F.Emissive[2] = 0;
    auto Black = Render(Dark, "RoughSourcesOff");
    for (const auto& P : Black)
        Require(P[0] == 0 && P[1] == 0 && P[2] == 0, "Source-off reflection retained stale radiance");
    auto Moved = Scene;
    for (auto& F : Moved.Surfaces)
        if (F.OpticalToken[3] == 900)
            for (auto* P : {&F.A, &F.B, &F.C}) (*P)[1] -= .5f;
    RebuildMaterialVisibility(Moved);
    auto Motion = Render(Moved, "RoughMoved");
    LightComputeSubmission Fresh(Driver);
    Fresh.Prepare(Moved, Shaders);
    auto MotionReference = Fresh.Execute(Moved, 0, 16, 71, 64, 0, true, true, Samples, false);
    Require(Motion == MotionReference, "Moved reflection differs from freshly prepared scene");
    Require(Motion != Hybrid, "Moving emissive geometry did not change reflection image");
    std::cout << "ROUGH_REFLECTION samples_per_proposal=" << Samples << " proposals=2 floor_pixels=" << Floor
              << " screen_rays=" << Screen << " bvh_fallback_rays=" << Fallback
              << " hybrid_bvh_max_rgb_error=" << Maximum << " hybrid_bvh_mean_rgb_error=" << Mean
              << " hidden_source_primary_pixels=" << HiddenSourcePrimary << " hidden_source_red_arrival=" << HiddenArrival
              << " deterministic_repeat=1 source_off_black=1 moving_source_fresh_equal=1" << std::endl;
    for (uint32_t I = 0; I < 4; ++I)
        std::cout << "ROUGH_COLUMN index=" << I << " pixels=" << FloorCount[I]
                  << " mean_red=" << FloorSum[I] / std::max(1u, FloorCount[I]) << std::endl;

    //--------------------------------------------------------------------------------------------------------------------
    //                                                    UNIFORM ILLUMINATION
    //--------------------------------------------------------------------------------------------------------------------

    auto White = Scene;
    White.Surfaces.erase(std::remove_if(White.Surfaces.begin(), White.Surfaces.end(), [](const auto& F) {
        return F.OpticalToken[3] >= 900;
    }), White.Surfaces.end());
    for (uint32_t Address : Addresses) White.Angles[Address] = 0;
    uint32_t Cube = uint32_t(White.Camera[10][0]), Size = uint32_t(White.Camera[10][1]);
    std::fill_n(White.Angles.begin() + Cube, 6 * Size * Size * 3, 1.f);
    White.Camera[10][3] = 1;
    RebuildMaterialVisibility(White);
    MaterialCamera(White, {0, -4, 3.2}, {0, .4, .35}, 64, 40);
    LightComputeSubmission Furnace(Driver);
    Furnace.Prepare(White, Shaders);
    auto Uniform = Furnace.Execute(White, 0, 16, 71, 64, 0, true, true, Samples, true);
    auto WhiteReceivers = Furnace.PrimaryReceivers();
    std::array<double, 4> Energy{};
    std::array<uint32_t, 4> Count{};
    for (size_t I = 0; I < Uniform.size(); ++I)
    {
        const auto& R = WhiteReceivers[I];
        if (R.PositionHit[3] == 0) continue;
        uint32_t Column = uint32_t(R.MotionPositionToken[3]) - 800;
        Require(Column < 4 && std::isfinite(Uniform[I][0]), "Invalid uniform reflection fixture");
        Energy[Column] += Uniform[I][0]; ++Count[Column];
    }
    for (uint32_t I = 0; I < 4; ++I)
    {
        double Average = Energy[I] / std::max(1u, Count[I]);
        Require(Count[I] > 10 && Average > 0 && Average <= 1.02, "Uniform illumination mean exceeds energy budget");
        std::cout << "ROUGH_UNIFORM index=" << I << " pixels=" << Count[I] << " mean_red=" << Average
                  << " limit=1.02" << std::endl;
    }
}
