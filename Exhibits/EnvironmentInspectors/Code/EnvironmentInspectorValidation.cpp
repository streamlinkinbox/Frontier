/*==========================================================================================================================================
                                                    ENVIRONMENTINSPECTORVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Production inspector captures, numerical reference comparisons, configuration round trips and native input/texture lifetime checks.

#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/Controls/RasterPlate.h"
#include "Engine/Interface/IconPresentation.h"
#include "Engine/Interface/InspectorControls.h"
#include "Engine/Interface/Inspectors/CloudDensityReference.h"
#include "Engine/Interface/Inspectors/EnvironmentReadings.h"
#include "Engine/Interface/Inspectors/LunarSurfaceReference.h"
#include "Engine/Interface/Inspectors/OpticalReference.h"
#include "Engine/Interface/Inspectors/StarCatalogueReference.h"
#include "Engine/Scene/SceneWorld.h"
#include "Exhibits/ControlCentre/Code/CpuRasterization.h"
#include "UI/Editor/ControlCentrePanel.h"

#include <imgui_internal.h>
#include <stb_image_write.h>
#include <toml++/toml.hpp>
#include <algorithm>
#include <bit>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <thread>

using namespace Frontier;

namespace
{

std::ofstream Report;
int           Assertions = 0;
int           Failures   = 0;

void Verify(bool Condition, const std::string &Caption)
{
    ++Assertions;
    Report << (Condition ? "PASS" : "FAIL") << '\t' << Caption << '\n';
    if (!Condition)
    {
        ++Failures;
        std::cerr << "FAIL: " << Caption << '\n';
    }
}

void Near(double Observed, double Expected, double Tolerance, const std::string &Caption)
{
    const bool Equal = std::isfinite(Observed) && std::abs(Observed - Expected) <= Tolerance;
    Verify(Equal, Caption);
    if (!Equal) std::cerr << "  observed=" << Observed << " expected=" << Expected << '\n';
}

std::vector<uint8_t> ReadBytes(const std::filesystem::path &Path)
{
    std::ifstream Input(Path, std::ios::binary);
    return {std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>()};
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    NUMERICAL REFERENCES
//------------------------------------------------------------------------------------------------------------------------

void ValidateEquations(const std::filesystem::path &Reference)
{
    std::ifstream Input(Reference);
    if (!Input) throw std::runtime_error("Missing generated reference expectations");
    CloudConfiguration Cloud;
    Cloud.BaseAltitudeMetres   = 1024;
    Cloud.LayerThicknessMetres = 2048;
    Cloud.CeilingMetres        = 4096;
    Cloud.Coverage             = 0.75f;
    Cloud.ExtinctionDensity    = 1.5f;
    Cloud.FeatureScale         = 1;
    Cloud.AnvilShaping         = 0.5f;
    Cloud.EdgeErosion          = 0.25f;
    Cloud.LocalFeatureMetres   = 32;
    Cloud.EdgeSoftness         = 0.5f;
    const float Centre[] = {100, 200, 300}, Extents[] = {80, 160, 96};
    std::copy_n(Centre, 3, Cloud.CentreMetres);
    std::copy_n(Extents, 3, Cloud.HalfExtentsMetres);
    std::string Line;
    int         Row = 0;
    while (std::getline(Input, Line))
    {
        if (Line.empty() || Line.front() == '#') continue;
        std::istringstream Fields(Line);
        std::string        Category;
        Fields >> Category;
        const std::string Caption = Category + " reference row " + std::to_string(++Row);
        if (Category == "Optical")
        {
            uint32_t              Preset;
            double                U, V;
            std::array<double, 3> Expected;
            Fields >> Preset >> U >> V >> Expected[0] >> Expected[1] >> Expected[2];
            OpticalFlareConfiguration Optics;
            ConfigureOpticalPreset(Optics, Preset);
            Optics.Intensity           = 1;
            Optics.Spread              = 1;
            Optics.ChromaticDispersion = 0.5f;
            Optics.ApertureBladeCount  = 8;
            Optics.HaloRadius          = 0.3f;
            Optics.HaloWidth           = 0.04f;
            const auto Sample          = EvaluateOpticalReference(Optics, U, V, 640.0 / 280.0);
            for (size_t Band = 0; Band < 3; ++Band)
                Near(Sample[Band], Expected[Band], 0.00002, Caption);
        }
        else if (Category == "Profile")
        {
            uint32_t Formation;
            double   Height, Expected;
            Fields >> Formation >> Height >> Expected;
            Near(EnvironmentReadings::CloudProfile(Height, Formation, 0.5), Expected, 1e-12, Caption);
        }
        else
        {
            std::array<double, 3> Position;
            double                Expected;
            Cloud.LocalVolume = Category == "LocalDensity";
            if (Cloud.LocalVolume)
                Fields >> Cloud.LocalFormation >> Cloud.BoundsShape;
            else
                Fields >> Cloud.Formation;
            Fields >> Position[0] >> Position[1] >> Position[2] >> Expected;
            Near(CloudDensityReference::Evaluate(Cloud, Position), Expected, 1e-9, Caption);
        }
        if (!Fields) throw std::runtime_error("Malformed reference row");
    }
    Verify(Row == 114, "All pinned-source numerical rows evaluated");
    WindConfiguration Wind;
    Wind.DirectionDegrees        = 0;
    Wind.SpeedMetresPerSecond    = 7;
    Wind.GustStrength            = 0;
    Wind.CurlNoiseTurbulence     = 0;
    Wind.ShearPerKilometre       = 0;
    Wind.VeerDegreesPerKilometre = 0;
    const auto Velocity          = EnvironmentReadings::WindVelocity(Wind, 0);
    Near(Velocity[0], 0, 1e-12, "Wind travel east component");
    Near(Velocity[1], 7, 1e-12, "Wind travel north component");
    Wind.Enabled = false;
    Near(EnvironmentReadings::WindVelocity(Wind, 0, 0, 0, true)[1], 0, 1e-12, "Wind disabled reference");
    FogConfiguration Fog;
    Fog.BaseDensity         = 0.005f;
    Fog.HeightFalloff       = 0.05f;
    const auto Transmission = EnvironmentReadings::FogTransmission(Fog, {0, 0, 0}, {200, 0, 0}, false);
    Near(Transmission[0], std::exp(-1.0), 1e-7, "Height fog squared extinction at one optical depth");
    Fog.AerialDensity     = 0.001f;
    Fog.AerialStartMetres = 100;
    Fog.AerialMieBlend    = 0;
    Near(EnvironmentReadings::FogTransmission(Fog, {0, 0, 0}, {50, 0, 0}, true)[2], 1, 1e-12, "Aerial clear-start gate");
    const auto Aerial = EnvironmentReadings::FogTransmission(Fog, {0, 0, 0}, {1100, 0, 0}, true);
    Near(Aerial[0], std::exp(-0.45), 1e-7, "Aerial red extinction");
    Near(Aerial[2], std::exp(-1.0), 1e-7, "Aerial blue extinction");
    Near(EnvironmentReadings::LunarIllumination(0), 0, 1e-12, "New moon fraction");
    Near(EnvironmentReadings::LunarIllumination(29.53 / 2), 1, 1e-12, "Full moon fraction");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONTENT AND ENCODING
//------------------------------------------------------------------------------------------------------------------------

void ValidateContent(const std::filesystem::path &Content, const std::filesystem::path &Scratch)
{
    const auto Maps      = Content / "EnvironmentArchives/CelestialMaps";
    auto       Bytes     = ReadBytes(Maps / "BrightStars.bin");
    const auto Catalogue = DecodeStarCatalogue(Bytes);
    Verify(Catalogue.Entries.size() == 8920 && Catalogue.Diagnostic.empty(), "Actual SLAT catalogue: 8920 finite entries");
    const auto Census = EvaluateStarCensus(Catalogue.Entries, 6);
    Verify(std::accumulate(Census.TotalBins.begin(), Census.TotalBins.end(), 0u) == 8920, "Magnitude census accounts for catalogue");
    Verify(std::accumulate(Census.EligibleBins.begin(), Census.EligibleBins.end(), 0u) == Census.Eligible,
           "Magnitude eligibility agrees with bins");
    auto Damaged = Bytes;
    Damaged.pop_back();
    Verify(DecodeStarCatalogue(Damaged).Entries.empty(), "Truncated SLAT rejected");
    Damaged    = Bytes;
    Damaged[0] = 0;
    Verify(DecodeStarCatalogue(Damaged).Entries.empty(), "Wrong SLAT magic rejected");
    Damaged    = Bytes;
    Damaged[4] = 200;
    Verify(DecodeStarCatalogue(Damaged).Entries.empty(), "Unknown SLAT version rejected");
    Damaged     = Bytes;
    Damaged[16] = 0;
    Damaged[17] = 0;
    Damaged[18] = 128;
    Damaged[19] = 127;
    Verify(DecodeStarCatalogue(Damaged).Entries.empty(), "Non-finite SLAT direction rejected");
    Verify(ReadStarCatalogue(Scratch / "missing.bin").Entries.empty(), "Missing SLAT has no fabricated fallback");
    for (const auto &Preset : LunarSurfacePresets)
    {
        const auto Surface = ReadLunarSurface(Maps / Preset.Filename);
        Verify(Surface.Width == 2048 && !Surface.Rgb.empty(), std::string("Registered lunar map: ") + Preset.Name);
        MoonInstanceConfiguration Moon;
        Moon.PhaseDays    = 14.765f;
        const auto Pixels = GenerateLunarReference(Surface, Moon);
        Verify(Pixels.size() == 256u * 256u * 4u && Pixels[3] == 0 && Pixels[(128 * 256 + 128) * 4 + 3] == 255,
               std::string("Transparent textured sphere: ") + Preset.Name);
        Moon.PhaseDays = std::numeric_limits<float>::quiet_NaN();
        Verify(GenerateLunarReference(Surface, Moon).empty(), "Non-finite lunar settings rejected");
    }
    Verify(ReadLunarSurface(Scratch / "absent.jpg").Rgb.empty(), "Missing lunar map reported");
    OpticalFlareConfiguration Flare;
    Verify(GenerateOpticalReference(Flare, 0, 10).empty(), "Empty optical extent rejected");
    Verify(GenerateOpticalReference(Flare, 4096, 4096).empty(), "Oversized optical allocation rejected");
    Flare.Intensity = std::numeric_limits<float>::infinity();
    Near(EvaluateOpticalReference(Flare, .5, .5, 2)[0], 0, 0, "Non-finite optical input rejected");
    std::vector<float> Radiance    = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    const auto         Destination = Scratch / "Orientation.pfm";
    std::filesystem::remove(Destination);
    std::string Diagnostic;
    Verify(EncodeLinearPfm(Destination, Radiance, 2, 2, Diagnostic), "PFM export succeeds");
    const auto        Pfm    = ReadBytes(Destination);
    const std::string Header = "PF\n2 2\n-1.0\n";
    Verify(Pfm.size() == Header.size() + 48 && std::equal(Header.begin(), Header.end(), Pfm.begin()),
           "PFM RGB Float32 little-endian header");
    if (Pfm.size() == Header.size() + 48)
    {
        const auto     At   = Header.size();
        const uint32_t Word = uint32_t(Pfm[At]) | uint32_t(Pfm[At + 1]) << 8 | uint32_t(Pfm[At + 2]) << 16 | uint32_t(Pfm[At + 3]) << 24;
        Near(std::bit_cast<float>(Word), 7, 0, "PFM scanlines are bottom-up and retain HDR");
    }
    Verify(!EncodeLinearPfm(Destination, Radiance, 2, 2, Diagnostic) && ReadBytes(Destination) == Pfm, "PFM refuses overwrite");
    Radiance[0] = std::numeric_limits<float>::quiet_NaN();
    Verify(!EncodeLinearPfm(Scratch / "Invalid.pfm", Radiance, 2, 2, Diagnostic), "PFM rejects NaN");
    Verify(!EncodeLinearPfm(Scratch / "NoDirectory/Invalid.pfm", {}, 2, 2, Diagnostic), "PFM rejects invalid dimensions");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TOML ROUND TRIPS
//------------------------------------------------------------------------------------------------------------------------

void PerturbConfiguration(toml::node &Entry)
{
    if (auto *Table = Entry.as_table())
        for (auto &&[Key, Field] : *Table)
            PerturbConfiguration(Field);
    else if (auto *Array = Entry.as_array())
        for (auto &Field : *Array)
            PerturbConfiguration(Field);
    else if (auto *Boolean = Entry.as_boolean())
        *Boolean = !Boolean->get();
    else if (auto *Integer = Entry.as_integer())
        *Integer = Integer->get() + 1;
    else if (auto *Quantity = Entry.as_floating_point())
        *Quantity = static_cast<double>(static_cast<float>(Quantity->get() + 0.125));
}

void ValidatePersistence(const std::filesystem::path &Project, const std::filesystem::path &Scratch)
{
    SceneWorld  World;
    std::string Diagnostic;
    if (!World.LoadLevelSpace(Project.parent_path().parent_path().parent_path() / "Exhibits/EverestInterface/Content/Simulation.levelspace", &Diagnostic)) throw std::runtime_error(Diagnostic);
    for (const char *Category : {"Clouds", "Wind", "ForceField", "Atmosphere", "Planet", "Fog", "AerialFog", "Lightning", "Stars", "OpticalFlare", "Moons"})
    {
        auto *Entry = World.FindFirstByType(Category);
        if (!Entry) throw std::runtime_error(std::string("Missing record ") + Category);
        toml::table Original;
        Entry->SerializeConfiguration(Original);
        toml::table Edited(Original);
        PerturbConfiguration(Edited);
        Entry->DeserializeConfiguration(Edited);
        toml::table RoundTrip;
        Entry->SerializeConfiguration(RoundTrip);
        Verify(Edited == RoundTrip, std::string(Category) + " all serialized authoring fields round-trip");
        Entry->DeserializeConfiguration(Original);
    }
    auto       &Cloud   = dynamic_cast<CloudRecord *>(World.FindFirstByType("Clouds"))->AccessConfiguration();
    const float Density = Cloud.ExtinctionDensity;
    toml::table Malformed{{"density", std::numeric_limits<double>::infinity()}, {"coverage", std::numeric_limits<double>::quiet_NaN()}};
    const float Coverage = Cloud.Coverage;
    World.FindFirstByType("Clouds")->DeserializeConfiguration(Malformed);
    Verify(Cloud.ExtinctionDensity == Density && Cloud.Coverage == Coverage, "TOML non-finite float rejection preserves prior settings");
    auto &Moons = dynamic_cast<MoonRecord *>(World.FindFirstByType("Moons"))->AccessConfiguration();
    World.FindFirstByType("Moons")->DeserializeConfiguration(toml::table{{"primary_phase", 0.25}, {"enabled", false}});
    Near(Moons.Instances[0].PhaseDays, 29.53 / 4, 1e-5, "Legacy Moon phase migrates to days");
    Verify(!Moons.Instances[0].Visible, "Legacy Moon visibility migrates");
    Verify(World.SaveLevelSpace(Scratch / "Environment.levelspace", &Diagnostic), "Complete environment level saved");
    SceneWorld Reloaded;
    Verify(Reloaded.LoadLevelSpace(Scratch / "Environment.levelspace", &Diagnostic) &&
               Reloaded.QueryRecordCount() == World.QueryRecordCount(),
           "Complete environment level reloaded");
    for (const char *Category : {"Clouds", "Wind", "ForceField", "Atmosphere", "Planet", "Fog", "AerialFog", "Lightning", "Stars", "OpticalFlare", "Moons"})
    {
        toml::table Before, After;
        World.FindFirstByType(Category)->SerializeConfiguration(Before);
        Reloaded.FindFirstByType(Category)->SerializeConfiguration(After);
        Verify(Before == After, std::string(Category) + " survives disk save/reload");
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    NATIVE UI INPUT AND CAPTURES
//------------------------------------------------------------------------------------------------------------------------

class InterfaceCapture
{
  public:
    explicit InterfaceCapture(ControlCentrePanel &InCentre) : Centre(InCentre) {}
    int     Width         = 420;
    int     Height        = 6000;
    float   ContentHeight = 0;
    ImGuiID WindowId      = 0;

    ImGuiID ResolveId(std::initializer_list<const char *> Parts) const
    {
        ImGuiID Id = WindowId;
        for (const char *Part : Parts)
            Id = ImHashStr(Part, 0, Id);
        return Id;
    }

    void Construct(const std::function<void()> &ConstructPage, ImGuiID Focus = 0, bool Activate = false)
    {
        auto &Io       = ImGui::GetIO();
        Io.DisplaySize = {static_cast<float>(Width), static_cast<float>(Height)};
        Centre.PrepareInterface();
        ImGui::NewFrame();
        (void)Centre.Integrate(1.0 / 60.0);
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(Io.DisplaySize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
        ImGui::Begin("EnvironmentCapture", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoScrollbar);
        auto *Window = ImGui::GetCurrentWindow();
        WindowId     = Window->ID;
        if (Focus)
        {
            ImGui::FocusWindow(Window);
            ImGui::SetFocusID(Focus, Window);
            if (Activate)
            {
                GImGui->NavActivateId = GImGui->NavActivateDownId = GImGui->NavActivatePressedId = Focus;
                GImGui->NavInputSource                                                           = ImGuiInputSource_Keyboard;
            }
        }
        ConstructPage();
        ContentHeight = ImGui::GetCursorScreenPos().y;
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::Render();
        FrontierExhibit::AcknowledgeTextures();
    }

    void Press(const std::function<void()> &ConstructPage, ImGuiID Focus, ImGuiKey Key, bool Shift = false)
    {
        ImGui::GetIO().AddKeyEvent(ImGuiMod_Shift, Shift);
        ImGui::GetIO().AddKeyEvent(Key, true);
        Construct(ConstructPage, Focus);
        ImGui::GetIO().AddKeyEvent(Key, false);
        ImGui::GetIO().AddKeyEvent(ImGuiMod_Shift, false);
        Construct(ConstructPage, Focus);
    }

    void Capture(RecordInspector &Inspector, const std::filesystem::path &Filename)
    {
        Height    = 6000;
        auto Page = [&] { Inspector.ConstructInspector(); };
        for (int Tick = 0; Tick < 3; ++Tick)
            Construct(Page);
        if (auto *Optical = dynamic_cast<OpticalFlareInspector *>(&Inspector))
        {
            const auto Limit = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            while (!Optical->QueryReferenceReady() && std::chrono::steady_clock::now() < Limit)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
                Construct(Page);
            }
            Verify(Optical->QueryReferenceReady(), "Asynchronous optical preview publishes actual radiance");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(350));
        Construct(Page);
        Height = std::clamp(static_cast<int>(std::ceil(ContentHeight)) + 8, 380, 6000);
        Construct(Page);
        Construct(Page);
        const auto          *Commands = ImGui::GetDrawData();
        std::vector<uint8_t> Pixels(static_cast<size_t>(Width) * Height * 3, 14);
        for (const auto *List : Commands->CmdLists)
        {
            for (const auto &Vertex : List->VtxBuffer)
                if (!std::isfinite(Vertex.pos.x) || !std::isfinite(Vertex.pos.y)) throw std::runtime_error("Non-finite UI vertex");
            FrontierExhibit::RasterizeCommands(List, Pixels.data(), Width, Height, Commands->DisplayPos, Commands->FramebufferScale);
        }
        Verify(stbi_write_png(Filename.string().c_str(), Width, Height, 3, Pixels.data(), Width * 3) != 0,
               "Native rendered capture " + Filename.filename().string());
    }

  private:
    ControlCentrePanel &Centre;
};

void ValidateControls(InterfaceCapture &Capture)
{
    float  Quantity = .5f;
    bool   Enabled  = true;
    bool   Changed  = false;
    ImVec2 Track;
    auto   SliderPage = [&]
    {
        const auto    Theme = ControlTheme::ResolveWind();
        const auto    Faces = EditorTypefaces::Resolve();
        ControlColumn Page(Theme, Faces, 1);
        Changed = SliderRow::Construct(Page, "Quantity", "Coverage", Quantity, 0, 1, .01f, "%", 100, Enabled);
        Track   = ImGui::GetItemRectMin();
        Track.x += 290;
        Track.y += 7;
        Page.Finalize();
    };
    Capture.Construct(SliderPage);
    const auto Focus = Capture.ResolveId({"Quantity", "##Track"});
    Capture.Press(SliderPage, Focus, ImGuiKey_RightArrow);
    Near(Quantity, .51, 1e-6, "Slider arrow changes one raw step");
    Capture.Press(SliderPage, Focus, ImGuiKey_RightArrow, true);
    Near(Quantity, .61, 1e-6, "Slider Shift-arrow changes ten raw steps");
    Enabled = false;
    Capture.Press(SliderPage, Focus, ImGuiKey_RightArrow);
    Near(Quantity, .61, 1e-6, "Disabled slider ignores keyboard input");
    Enabled = true;
    ImGui::GetIO().AddMousePosEvent(Track.x, Track.y);
    Capture.Construct(SliderPage);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    Capture.Construct(SliderPage);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    Capture.Construct(SliderPage);
    Verify(Quantity > .7f && Quantity < .9f, "Slider pointer hit region edits the setting");
    ImGui::GetIO().AddMousePosEvent(-1000, -1000);
}

void ValidateInspectors(InterfaceCapture &Capture, const std::filesystem::path &Output)
{
    CloudConfiguration Clouds, LocalCloud;
    LocalCloud.LocalVolume = true;
    WindConfiguration Wind;
    FogConfiguration  Fog, AerialFog, LocalFog;
    AerialFog.AerialOnly = true;
    LocalFog.LocalVolume = true;
    LightningConfiguration    Lightning;
    StarConfiguration         Stars;
    MoonConfiguration         Moons;
    SolarConfiguration        Sun;
    OpticalFlareConfiguration Flare;
    AtmosphereConfiguration   Atmosphere;
    ForceFieldConfiguration Forces;
    EnvironmentEditAccess     Access;
    Access.Clouds      = &Clouds;
    Access.LocalCloud  = &LocalCloud;
    Access.Wind        = &Wind;
    Access.Fog         = &Fog;
    Access.LocalFog    = &LocalFog;
    Access.Lightning   = &Lightning;
    Access.Solar       = &Sun;
    Access.CameraReady = true;
    Access.ViewExtent  = {1920, 1080};
    Access.Atmosphere = &Atmosphere;
    Access.Forces = &Forces;
    InspectorRuntime::Instance().ConfigureEnvironmentAccess(Access);
    CloudInspector                                   CloudPage(Clouds), LocalCloudPage(LocalCloud);
    WindInspector                                    WindPage(Wind);
    FogInspector                                     FogPage(Fog), AerialFogPage(AerialFog), LocalFogPage(LocalFog);
    LightningInspector                               LightningPage(Lightning);
    StarInspector                                    StarPage(Stars);
    MoonInspector                                    MoonPage(Moons);
    OpticalFlareInspector                            FlarePage(Flare);
    SolarInspector                                   SunPage(Sun);
    AtmosphereInspector                              AtmospherePage(Atmosphere);
    ForceFieldInspector ForcePage(Forces);
    const std::pair<const char *, RecordInspector *> Pages[] = {{"Cloud", &CloudPage},
                                                                {"LocalCloud", &LocalCloudPage},
                                                                {"Wind", &WindPage},
                                                                {"ForceField", &ForcePage},
                                                                {"Fog", &FogPage},
                                                                {"AerialFog", &AerialFogPage},
                                                                {"LocalFog", &LocalFogPage},
                                                                {"Lightning", &LightningPage},
                                                                {"Stars", &StarPage},
                                                                {"Moon", &MoonPage},
                                                                {"Flare", &FlarePage},
                                                                {"Sun", &SunPage},
                                                                {"Atmosphere", &AtmospherePage}};
    for (const auto &[Name, Inspector] : Pages)
    {
        for (int Section = 0; Section < std::max(1, Inspector->QuerySectionCount()); ++Section)
        {
            Inspector->ConfigureSection(Section);
            Capture.Capture(*Inspector, Output / (std::string(Name) + "-" + std::to_string(Section) + ".png"));
        }
    }
    auto Planets = ResolveDefaultPlanets();
    for (auto& Planet : Planets)
    {
        Planet.ApparentDiameterDegrees = 8;
        PlanetInspector Page(Planet);
        for (int Section = 0; Section < Page.QuerySectionCount(); ++Section)
        {
            Page.ConfigureSection(Section);
            Capture.Capture(Page,Output / (std::string(PlanetBodyNames[Planet.Body]) + "-" + std::to_string(Section) + ".png"));
        }
    }
    Capture.Height = 6000;
    CloudPage.ConfigureSection(0);
    auto CloudDraw = [&] { CloudPage.ConstructInspector(); };
    Capture.Construct(CloudDraw);
    const auto  CloudRevision = Clouds.Revision;
    const float Coverage      = Clouds.Coverage;
    Capture.Press(CloudDraw, Capture.ResolveId({"CloudMix"}), ImGuiKey_RightArrow);
    Near(Clouds.Coverage, Coverage + .01, 1e-6, "Cloud mix diagram keyboard changes coverage");
    Verify(Clouds.Revision == CloudRevision + 1, "Cloud mix edit advances revision once");
    FogPage.ConfigureSection(0);
    const auto FogRevision = Fog.Revision;
    Capture.Press([&] { FogPage.ConstructInspector(); }, Capture.ResolveId({"FogTransect"}), ImGuiKey_RightArrow);
    Verify(Fog.Revision == FogRevision, "Fog diagnostic probe does not dirty scene");
    WindPage.ConfigureSection(0);
    Capture.Press([&] { WindPage.ConstructInspector(); }, Capture.ResolveId({"WindCompass"}), ImGuiKey_Home);
    Near(Wind.SpeedMetresPerSecond, 7, 1e-6, "Wind compass Home reference speed");
    Near(EnvironmentReadings::WrapDegrees(Wind.DirectionDegrees + 180), 0, 1e-6, "Wind compass FROM convention");
    StarPage.ConfigureSection(0);
    auto StarsDraw = [&] { StarPage.ConstructInspector(); };
    Capture.Construct(StarsDraw, Capture.ResolveId({"PreviewClearNight"}), true);
    Near(Sun.ElevationDegrees, -18, 1e-6, "Clear-night action edits Sun");
    Verify(!Clouds.Enabled && !LocalCloud.Enabled && !LocalFog.Enabled && Stars.Enabled,
           "Clear-night action edits cross-record visibility");
    FlarePage.ConfigureSection(0);
    const float Source = Flare.PreviewPosition[0];
    Capture.Press([&] { FlarePage.ConstructInspector(); }, Capture.ResolveId({"FlareSource"}), ImGuiKey_RightArrow);
    Near(Flare.PreviewPosition[0], Source + .02, 1e-6, "Flare source keyboard interaction");
    // 📝 Destruction waits for an in-progress optical calculation without retaining a scene pointer.
    {
        OpticalFlareConfiguration Temporary;
        OpticalFlareInspector     Pending(Temporary);
        Capture.Construct([&] { Pending.ConstructInspector(); });
    }
    Verify(true, "Optical calculation survives immediate inspector destruction");
    MoonPage.ConfigureSection(0);
    Moons.Instances[0].FollowSky = true;
    const float Days             = Moons.Instances[0].PhaseDays;
    Capture.Press([&] { MoonPage.ConstructInspector(); }, Capture.ResolveId({"MoonPhase", "##Track"}), ImGuiKey_RightArrow);
    Near(Moons.Instances[0].PhaseDays, std::round((Days + .01) * 100) / 100, 1e-5, "Moon phase slider changes days");
    Verify(!Moons.Instances[0].FollowSky, "Manual Moon phase disables sky following");
    Capture.Press([&] { LightningPage.ConstructInspector(); }, Capture.ResolveId({"LightningIntervalMin", "##Track"}), ImGuiKey_End);
    Verify(Lightning.MinimumIntervalSeconds == 60 && Lightning.MaximumIntervalSeconds == 60,
           "Lightning intervals remain ordered after input");
    const auto PlacementRevision = LocalFog.Revision;
    Access.CameraReady           = false;
    InspectorRuntime::Instance().ConfigureEnvironmentAccess(Access);
    Capture.Construct([&] { LocalFogPage.ConstructInspector(); }, Capture.ResolveId({"PlaceLocalFog"}), true);
    Verify(LocalFog.Revision == PlacementRevision, "Camera-dependent placement is disabled without camera access");
    AtmospherePage.ConfigureSection(3);
    const auto BakeSerial = Atmosphere.BakeRequested;
    Capture.Construct([&] { AtmospherePage.ConstructInspector(); },Capture.ResolveId({"AtmosphereInspectorPage", "##BakeAtmosphere"}),true);
    Verify(Atmosphere.BakeRequested == BakeSerial,"Bake button is disabled without a live viewport");
    auto Available = std::make_shared<EnvironmentRenderReadout>();
    Available->Ready = true;
    Access.Rendering = Available;
    Access.Atmosphere = &Atmosphere;
    InspectorRuntime::Instance().ConfigureEnvironmentAccess(Access);
    Capture.Construct([&] { AtmospherePage.ConstructInspector(); });
    Capture.Construct([&] { AtmospherePage.ConstructInspector(); },Capture.ResolveId({"AtmosphereInspectorPage", "##BakeAtmosphere"}),true);
    Verify(Atmosphere.BakeRequested == BakeSerial + 1,"Bake button queues an actual command serial");
    Verify(!Atmosphere.BakedAvailable,"Bake button does not fabricate completion before GPU acknowledgement");
    InspectorRuntime::Instance().ConfigureEnvironmentAccess({});
    Capture.Width = 320;
    Capture.Capture(MoonPage, Output / "Moon-Narrow.png");
    Capture.Width = 420;
}

void ValidateTextureLifetime(InterfaceCapture &Capture)
{
    auto                 Plate = std::make_unique<RasterPlate>();
    std::vector<uint8_t> Pixels(32 * 32 * 4, 255);
    for (int Tick = 0; Tick < 24; ++Tick)
    {
        Capture.Construct(
            [&]
            {
                Verify(Plate->Configure(32, 32, Pixels), "Raster replacement accepted");
                auto* Ink = ImGui::GetWindowDrawList();
            const int Before = Ink->VtxBuffer.Size;
            Plate->Construct(Ink, {20, 20}, {100, 100}, IM_COL32_WHITE, 12);
            if (Tick == 0) Verify(Ink->VtxBuffer.Size > Before + 4, "Raster corner radius produces rounded geometry");
            });
    }
    const auto Retained = ImGui::GetCurrentContext()->UserTextures.Size;
    Verify(Retained < 24, "Acknowledged reference textures retire instead of accumulating");
    Plate.reset();
    for (int Tick = 0; Tick < 6; ++Tick)
        Capture.Construct([] {});
    Verify(ImGui::GetCurrentContext()->UserTextures.Size < Retained, "Inspector deletion retires reference texture");
    RasterPlate SurvivesContext;
    Verify(SurvivesContext.Configure(32, 32, Pixels), "Reference before context shutdown");
    ImGuiContext *First  = ImGui::GetCurrentContext();
    ImGuiContext *Second = ImGui::CreateContext();
    ImGui::SetCurrentContext(Second);
    ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    Verify(!SurvivesContext.QueryReady(), "Reference not reused across ImGui contexts");
    Verify(SurvivesContext.Configure(32, 32, Pixels), "Reference registers in another context");
    ImGui::DestroyContext();
    ImGui::SetCurrentContext(First);
    Verify(!SurvivesContext.QueryReady(), "Destroyed context invalidates reference");
    Verify(SurvivesContext.Configure(32, 32, Pixels), "Reference recreated after context shutdown");
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    VALIDATION ENTRY
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char **Arguments)
{
    try
    {
        if (ArgumentCount != 5) throw std::runtime_error("Expected EngineContent, project, output and reference TSV");
        const auto Content = std::filesystem::absolute(Arguments[1]);
        const auto Project = std::filesystem::absolute(Arguments[2]) / "Everest.projectspace";
        const auto Output  = std::filesystem::absolute(Arguments[3]);
        const auto Scratch = std::filesystem::current_path() / "_ClaudeScratch/tmp/EnvironmentValidation";
        std::filesystem::create_directories(Output);
        std::filesystem::create_directories(Scratch);
        Report.open(Output / "Validation.tsv");
        ValidateEquations(Arguments[4]);
        ValidateContent(Content, Scratch);
        ValidatePersistence(Project, Scratch);
        ImGui::CreateContext();
        auto &Io       = ImGui::GetIO();
        Io.IniFilename = nullptr;
        Io.DeltaTime   = 1.0f / 60.0f;
        Io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        Io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
        ImGui::GetPlatformIO().Renderer_TextureMaxWidth  = 4096;
        ImGui::GetPlatformIO().Renderer_TextureMaxHeight = 4096;
        IconPresentation::Attach(Content / "Icons");
        {
            ActivationOwnership Ownership(new ControlCentrePanel(Content / "FontArchives", Scratch / "Personal.projectspace", Project));
            auto               &Centre = *static_cast<ControlCentrePanel *>(Ownership.get());
            if (!Centre.Initialize() || !Centre.Activate()) throw std::runtime_error("Control Centre initialization failed");
            InterfaceCapture Capture(Centre);
            for (int Tick = 0; Tick < 3; ++Tick)
                Capture.Construct([] {});
            ValidateControls(Capture);
            ValidateInspectors(Capture, Output);
            ValidateTextureLifetime(Capture);
        }
        ImGui::DestroyContext();
        Report << "SUMMARY\t" << Assertions << " assertions\t" << Failures << " failures\n";
        std::cout << Assertions << " assertions; " << Failures << " failures\n";
        return Failures ? 1 : 0;
    }
    catch (const std::exception &Error)
    {
        std::cerr << Error.what() << '\n';
        Report << "ERROR\t" << Error.what() << '\n';
        return 2;
    }
}
