/*============================================================================================================================================
                                                        VEHICLELIGHTINGCAPTURE.CPP
============================================================================================================================================*/
// 🧩 Capture authored vehicle lighting through existing exhibit Vulkan passes,without production changes.

#define main ExecuteInheritedLightValidation
#include "LightComputeValidation.cpp"
#undef main

//------------------------------------------------------------------------------------------------------------------------
//                                                 LIGHTING SCENE CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

struct LightingCaptureProfile
{
    LightComputeArchive Scene;
    uint32_t CloudOffset, FogOffset;
};

LightingCaptureProfile ConstructLightingScene(const std::string& Archive, const std::string& Shaders, uint32_t View)
{
    using namespace TransportReference;
    VehicleGlazingView Camera;
    Camera.Width  = 384;
    Camera.Height = 240;
    Camera.Eye    = View == 2 ? SpatialVector{8, -11, 3.0} : SpatialVector{6.8, -8.2, 3.0};
    Camera.Focus  = {0, 0, 1.4};
    auto Vehicle = AssembleVehicleGlazing(LightComputeArchive::Load(Archive), Shaders, Camera);
    auto Scene   = std::move(Vehicle.Closed);
    BindGlazingSpectrum(Scene, Vehicle.Address, {1.52f, .035f, .012f, .006f});
    #include "../../../_ClaudeScratch/build/light-compute/MaterialSceneOffsets.h"
    uint32_t GreenAddress = 0;
    for (const auto& Facet : Scene.Surfaces)
        if (std::abs(Facet.AlbedoRoughness[0] - .5304039f) < 1e-5)
            GreenAddress = uint32_t(Facet.Emissive[3]);
    Require(GreenAddress > 0, "Missing retained green material");
    std::array<uint32_t, 5> Materials;
    for (uint32_t Index = 0; Index < 5; ++Index)
        Materials[Index] = GreenAddress - MaterialSceneOffsets[0] + MaterialSceneOffsets[Index];
    const auto Plastic   = Materials[3];
    const auto Structure = DuplicateSceneMaterial(Scene, Plastic, 0, {.10f, .12f, .15f, .4f}, .4f);
    const auto WetApron  = DuplicateSceneMaterial(Scene, Plastic, 3, {.12f, .14f, .18f, .24f}, .65f);
    MaterialRectangle(Scene, {0, -2.3, .015}, {4.0, 0, 0}, {0, 2.8, 0}, WetApron, 810);
    UnifiedSceneReference::AppendDrapedDenimBanner(Scene, {-3.5, 2.3, .1}, Materials[4], 811);
    for (double Side : {-1., 1.})
        MaterialRectangle(Scene, {Side * 3.4, 2.8, 1.8}, {.07, 0, 0}, {0, 0, 1.8}, Structure, 812);
    const size_t EmitterStart = Scene.Surfaces.size();
    MaterialRectangle(Scene, {0, 2.8, 3.55}, {3.4, 0, 0}, {0, .10, .10}, Structure, 813);
    for (size_t Facet = EmitterStart; Facet < Scene.Surfaces.size(); ++Facet)
    {
        Scene.Surfaces[Facet].Emissive[0] = 18;
        Scene.Surfaces[Facet].Emissive[1] = 8;
        Scene.Surfaces[Facet].Emissive[2] = 2.5;
    }
    RebuildMaterialVisibility(Scene);
    ReferenceScene Lighting;
    const auto SolarDirection = Unit(SpatialVector{-.48, .87, .15});
    ReferenceLight Sun;
    Sun.Kind     = ReferenceLight::Kind::Directional;
    Sun.Vector   = SolarDirection;
    Sun.Radiance = View == 1 ? SpectrumRgb{.12, .15, .22} : SpectrumRgb{4.5, 3.6, 2.5};
    Lighting.Lights.push_back(Sun);
    ReferenceLight Spot;
    Spot.Kind        = ReferenceLight::Kind::Spot;
    Spot.Vector      = {1.5, -1.5, 5};
    Spot.Direction   = Unit(SpatialVector{-.15, .15, -1});
    Spot.Up          = {0, 1, 0};
    Spot.Reach       = 18;
    Spot.InnerCosine = .96;
    Spot.OuterCosine = .62;
    Spot.Radiance    = {170, 125, 70};
    Lighting.Lights.push_back(Spot);
    ReferenceLight Photometric = Spot;
    Photometric.Vector    = {-2, -3.5, 3.7};
    Photometric.Direction = Unit(SpatialVector{.3, .45, -.85});
    Photometric.Radiance  = {65, 170, 260};
    std::istringstream Photometry("IESNA:LM-63-2002\nTILT=NONE\n1 -1 1 3 3 1 2 0 0 0\n1 1 10\n0 45 90\n0 180 360\n100 80 0 100 20 0 100 80 0");
    Photometric.Photometry = std::make_shared<CandelaDistribution>(CandelaDistribution::Read(Photometry));
    Lighting.Lights.push_back(Photometric);
    ReferenceImage Empty;
    const auto Packed = PackLightCompute(Lighting, Empty);
    Scene.Lights = Packed.Lights;
    const uint32_t AngularStart = uint32_t(Scene.Angles.size());
    for (auto& Light : Scene.Lights)
        if (Light.Profile[1])
        {
            Light.Profile[0] += AngularStart;
            Light.Profile[2] += AngularStart;
            Light.PeakActive[2] += float(AngularStart);
        }
    Scene.Angles.insert(Scene.Angles.end(), Packed.Angles.begin(), Packed.Angles.end());
    Prepare(Scene, 16, 32, true);
    CloudParameters Clouds;
    Clouds.Coverage        = View == 2 ? .63 : .42;
    Clouds.WindTimeS       = View == 2 ? 130 : 15;
    Clouds.AmbientRadiance = View == 1 ? SpectrumRgb{.025, .035, .07} : SpectrumRgb{.22, .28, .38};
    const auto CloudOffset = AppendCloudParameters(Scene, Clouds, SolarDirection, Sun.Radiance, 20);
    const auto FogOffset = UINT32_MAX;
    EmissiveQuadrature::AppendDistribution(Scene);
    return {std::move(Scene), CloudOffset, FogOffset};
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    GPU CAPTURE AND CONTROLS
//------------------------------------------------------------------------------------------------------------------------

int main(int Count, char** Arguments)
{
    try
    {
        Require(Count == 5, "Expected driver,shader directory,vehicle archive,output directory");
        const std::string Output = Arguments[4];
        std::filesystem::create_directories(Output);
        auto Capacity = ConstructLightingScene(Arguments[3], Arguments[2], 0);
        LightComputeSubmission Compute(Arguments[1]);
        Compute.Prepare(Capacity.Scene, Arguments[2]);
        std::cout << "VEHICLE_LIGHTING device=" << Compute.Properties.deviceName << " width=384 height=240\n" << std::flush;
        SolarOpticsParameters Optics;
        Optics.SunDirection = {-.48, .87, .15};
        Optics.DiscIrradiance = {.28, .18, .08};
        Optics.Flare.GhostStrength = .22;
        Optics.Flare.StarburstStrength = .4;
        Optics.Flare.GlowStrength = .3;
        Optics.Flare.AnamorphicStrength = .3;
        for (uint32_t View = 0; View < 3; ++View)
        {
            auto Profile = ConstructLightingScene(Arguments[3], Arguments[2], View);
            auto& Scene = Profile.Scene;
            const uint32_t CloudOffset = Profile.CloudOffset;
            const uint32_t FogOffset = Profile.FogOffset;
            auto Execute = [&](uint32_t Seed, bool Reset, const SolarOpticsParameters* Flare)
            {
                return Compute.Execute(Scene, 3, 16, Seed, 64, 7, Reset, true, 2, true, true, true,
                                       CloudOffset, FogOffset, true, false, UINT32_MAX, Flare);
            };
            std::vector<std::array<float, 4>> Image;
            for (uint32_t Sample = 0; Sample < 2; ++Sample)
            {
                Image = Execute(900 + Sample, Sample == 0, View == 1 ? nullptr : &Optics);
                std::cout << "VEHICLE_LIGHTING view=" << View << " sample=" << Sample << " pass=1\n" << std::flush;
            }
            WriteImage(Output + "/VehicleLighting" + std::to_string(View) + ".ppm", Image, 384, 240);
            std::ofstream Raw(Output + "/VehicleLighting" + std::to_string(View) + ".f32", std::ios::binary);
            Raw.write(reinterpret_cast<const char*>(Image.data()), std::streamsize(Image.size() * sizeof(Image[0])));
            if (View == 0)
            {
                const auto Full = Execute(1234, true, &Optics);
                const auto Repeat = Execute(1234, true, &Optics);
                Require(Full == Repeat, "Seeded cold capture is not deterministic");
                const auto NoFlare = Execute(1234, true, nullptr);
                Scene.Lights[2].PeakActive[1] = 0;
                const auto NoIes = Execute(1234, true, &Optics);
                uint32_t FlareChanged = 0, IesChanged = 0;
                for (size_t Pixel = 0; Pixel < Full.size(); ++Pixel)
                {
                    FlareChanged += Full[Pixel] != NoFlare[Pixel];
                    IesChanged   += Full[Pixel] != NoIes[Pixel];
                }
                Require(FlareChanged > 0 && IesChanged > 0, "Requested lighting controls had no effect");
                WriteImage(Output + "/VehicleLightingFlareOff.ppm", NoFlare, 384, 240);
                WriteImage(Output + "/VehicleLightingIesOff.ppm", NoIes, 384, 240);
                WriteImage(Output + "/VehicleLightingCold.ppm", Full, 384, 240);
                std::cout << "VEHICLE_LIGHTING_CONTROLS repeat_exact=1 flare_changed=" << FlareChanged
                          << " ies_changed=" << IesChanged << '\n' << std::flush;
            }
        }
        std::cout << "VEHICLE_LIGHTING_COMPLETE views=3 pass=1\n";
    }
    catch (const std::exception& Error)
    {
        std::cerr << Error.what() << '\n';
        return 1;
    }
}
