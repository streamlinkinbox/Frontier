/*==========================================================================================================================================
                                                       FLUIDEDITORCAPTURE.CPP
==========================================================================================================================================*/
// 🧩 The Fluid editor, driven the way a person drives it, with the Pyro solver's own picture in its viewport. The window is the one the Everest
//    workspace opens from a FluidVolume record (the Inspector's Fluid Editor button); the presenter registered here is FluidVolumeExtension (the editor
//    application's) on an own Vulkan device. Every press is a pointer press at a control's rectangle, every drag a pointer drag over the viewport;
//    what each should do is stated first (as the prototype, Experimental/Fluid, does it), what it did is measured after, and the pair is written to
//    FluidEditor.tsv beside the PNGs.
//
//        FluidEditorCapture <EngineContent> <Projects/Everest> <output folder>
//
//    The window is drawn by Dear ImGui on the CPU raster of Exhibits/ControlCentre/Code/CpuRasterization.h; the solver runs on whatever Vulkan device the
//    loader offers (SwiftShader here). Nothing is claimed of a GPU, of a browser or of the Windows application.

#include "Engine/Interface/FluidEditor/FluidEditorWorkspace.h"
#include "Engine/Interface/FluidVolumeEditor.h"
#include "Engine/Interface/IconPresentation.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Interface/SceneDirectoryPanel.h"
#include "Engine/Scene/SceneDirectory.h"
#include "Engine/Scene/WorldSpace.h"
#include "EverestWorkspace.h"
#include "Exhibits/ControlCentre/Code/CpuRasterization.h"
#include "Frontier/Extensions/Fluid/FluidVolumeExtension.h"
#include "UI/Editor/ControlCentrePanel.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <stb_image_write.h>
#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

constexpr uint64_t FluidTag = 0x9A27C3u;   // [-] the ImTextureID the capture resolves to the read-back Fluid picture

void Require(bool Condition, const std::string& Message)
{
    if (!Condition) throw std::runtime_error(Message);
}

std::ofstream Ledger;
int           Claims   = 0;
int           Failures = 0;

// 📝 One line of FluidEditor.tsv: what a step should do, what it did, whether the two agree.
void Claim(const std::string& Subject, const std::string& Expected, const std::string& Observed, bool Held)
{
    ++Claims;
    if (!Held) ++Failures;
    Ledger << (Held ? "PASS" : "FAIL") << '\t' << Subject << '\t' << Expected << '\t' << Observed << '\n';
    std::printf("[FluidEditorCapture] %s  %s — expected %s, observed %s\n", Held ? "PASS" : "FAIL", Subject.c_str(), Expected.c_str(), Observed.c_str());
}

std::string Describe(double Value, int Decimals = 1)
{
    char Text[64];
    std::snprintf(Text, sizeof(Text), "%.*f", Decimals, Value);
    return Text;
}

// 📝 Pixels of two pictures of one size whose colour differs by more than Threshold (of 255) in any channel.
size_t CountDifferences(const std::vector<uint8_t>& Picture, const std::vector<uint8_t>& Reference, int Threshold)
{
    if (Picture.size() != Reference.size()) return Picture.size() / 4u;
    size_t Count = 0;
    for (size_t Texel = 0; Texel + 3 < Picture.size(); Texel += 4)
    {
        for (int Channel = 0; Channel < 3; ++Channel)
        {
            if (std::abs(static_cast<int>(Picture[Texel + Channel]) - static_cast<int>(Reference[Texel + Channel])) > Threshold)
            {
                ++Count;
                break;
            }
        }
    }
    return Count;
}

// 📝 Pixels warmer than a grey one (red above blue by more than Margin): flame, embers, lit smoke.
size_t CountWarm(const std::vector<uint8_t>& Picture, int Margin)
{
    size_t Count = 0;
    for (size_t Texel = 0; Texel + 3 < Picture.size(); Texel += 4)
    {
        if (static_cast<int>(Picture[Texel]) > static_cast<int>(Picture[Texel + 2]) + Margin) ++Count;
    }
    return Count;
}

} // namespace

int main(int ArgumentCount, char** Arguments)
{
    using namespace Frontier;
    try
    {
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        Require(ArgumentCount == 4, "usage: FluidEditorCapture <EngineContent> <Projects/Everest> <output folder>");
        const std::filesystem::path ContentDir  = std::filesystem::absolute(Arguments[1]);
        const std::filesystem::path EverestRoot = std::filesystem::absolute(Arguments[2]);
        const std::filesystem::path OutputDir   = std::filesystem::absolute(Arguments[3]);
        std::filesystem::create_directories(OutputDir);
        Ledger.open(OutputDir / "FluidEditor.tsv");
        Ledger << "verdict\tstep\texpected\tobserved\n";

        ImGui::CreateContext();
        ImGuiIO& Io    = ImGui::GetIO();
        Io.IniFilename = nullptr;
        Io.DeltaTime   = 1.0f / 30.0f;
        Io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
        Io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
        ImGui::GetPlatformIO().Renderer_TextureMaxWidth  = 4096;
        ImGui::GetPlatformIO().Renderer_TextureMaxHeight = 4096;
        IconPresentation::Attach(ContentDir / "Icons");
        const std::filesystem::path Preferences = OutputDir / "Personal.projectspace";
        std::filesystem::remove(Preferences);
        ActivationOwnership Ownership(new ControlCentrePanel(ContentDir / "FontArchives", Preferences, EverestRoot / "Everest.projectspace"));
        auto*               Centre = static_cast<ControlCentrePanel*>(Ownership.get());
        Require(Centre->Initialize() && Centre->Activate(), "ControlCentrePanel initialization failed");

        Everest::EverestWorkspace Workspace(*Centre, EverestRoot);
        WorldSpace&               World = Workspace.QueryWorldSpace();
        Frontier::FluidVolumeExtension Bridge(nullptr);
        std::vector<uint8_t>      Pixels;         // [-] the Fluid picture of the last refresh that was read back
        std::array<uint32_t, 2>   Extent = { 0, 0 };
        bool                      ReadNext = false;
        bool                      Solver   = true;
        std::vector<double>       RefreshTimes;
        FluidVolumeReadout        Readout{};
        InspectorRuntime::Instance().RegisterFluidViewportPresenter([&](const FluidViewportRequest& Request) {
            FluidViewportImage Image;
            if (!Solver)
            {
                Image.Failure = "The Fluid viewport needs the editor's Vulkan device.";
                return Image;
            }
            Frontier::FluidViewportOutcome Outcome = Bridge.Render(Request);
            Image = std::move(Outcome.Image);
            if (Outcome.View != VK_NULL_HANDLE)
            {
                RefreshTimes.push_back(Image.RefreshMilliseconds);
                Image.Texture = FluidTag;
                Readout       = Image.Readout;
                if (ReadNext)
                {
                    std::string Diagnostic;
                    Require(Bridge.ReadPresentedImage(Pixels, &Diagnostic), "Fluid picture readback: " + Diagnostic);
                    Extent = Image.Extent;
                }
            }
            return Image;
        });

        // 📝 A FluidVolume record as the Construct dialog adds one, selected, and its editor opened as the Inspector's Fluid Editor button opens it.
        const RecordToken Token = World.ConstructRecord("FluidVolume", "Fire and smoke");
        Require(Token.IsValid(), "the FluidVolume record could not be constructed");
        Workspace.QueryOutliner().Select(Token);
        Require(InspectorRuntime::Instance().RequestEditorOpen(Token), "the FluidVolume record has no editor");
        auto* Record = dynamic_cast<FluidVolumeRecord*>(World.FindRecord(Token));
        Require(Record != nullptr, "the FluidVolume record has the wrong type");
        FluidVolumeConfiguration& Volume = Record->AccessConfiguration();

        int                   Width  = 1366;
        int                   Height = 800;
        ImVec2                Pointer(-FLT_MAX, -FLT_MAX);
        std::function<void()> AfterNewFrame;
        const auto            Step = [&]() {
            Io.DisplaySize = ImVec2(static_cast<float>(Width), static_cast<float>(Height));
            Io.MousePos    = Pointer;
            Centre->PrepareInterface();
            ImGui::NewFrame();
            if (AfterNewFrame) AfterNewFrame();
            (void)Centre->Integrate(1.0 / 30.0);
            (void)Workspace.ConfigurePlatform();
            Workspace.ConstructInterface(1.0 / 30.0);
            Centre->ConstructInterface();
            ImGui::Render();
            FrontierExhibit::AcknowledgeTextures();
        };
        const auto Tick = [&](int Count) {
            for (int Index = 0; Index < Count; ++Index)
            {
                ReadNext = Index + 1 == Count;
                Step();
            }
            ReadNext = false;
        };
        const auto Away = [&]() { Pointer = ImVec2(-FLT_MAX, -FLT_MAX); };
        const auto Click = [&](ImVec2 At) {
            Pointer = At;
            Step();
            Io.AddMouseButtonEvent(0, true);
            Step();
            Io.AddMouseButtonEvent(0, false);
            Step();
            Away();
            Step();
        };
        FluidEditorWorkspace* Editor = nullptr;
        const auto            Centred = [&](std::string_view Name) {
            const FluidControlRectangle* Control = Editor->FindControl(Name);
            Require(Control != nullptr, "the editor has no control `" + std::string(Name) + "`");
            return ImVec2((Control->Minimum.x + Control->Maximum.x) * 0.5f, (Control->Minimum.y + Control->Maximum.y) * 0.5f);
        };
        const auto Press = [&](std::string_view Name) { Click(Centred(Name)); };
        // 📝 A drag over the viewport with one mouse button (0 left, 1 right), the pointer arriving, pressing, moving, releasing and leaving.
        const auto Drag = [&](ImVec2 From, ImVec2 Delta, int Button, bool Shifted) {
            if (Shifted) Io.AddKeyEvent(ImGuiMod_Shift, true);
            Pointer = From;
            Step();
            Io.AddMouseButtonEvent(Button, true);
            Step();
            Pointer = ImVec2(From.x + Delta.x, From.y + Delta.y);
            Step();
            Io.AddMouseButtonEvent(Button, false);
            Step();
            if (Shifted) Io.AddKeyEvent(ImGuiMod_Shift, false);
            Away();
            Step();
        };
        const auto ViewportMiddle = [&]() {
            const std::array<ImVec2, 2> Box = Editor->QueryViewport();
            return ImVec2((Box[0].x + Box[1].x) * 0.5f, (Box[0].y + Box[1].y) * 0.5f);
        };

        Tick(1);
        Editor = FluidVolumeEditor::AccessWorkspace();
        Require(Editor != nullptr && InspectorRuntime::Instance().QueryActiveEditorToken() == Token, "the Fluid editor did not open");
        Require(Editor->QueryImage().Failure.empty(), "the solver could not run: " + Editor->QueryImage().Failure);
        std::printf("[FluidEditorCapture] editor open on %s\n", Editor->QueryImage().Adapter.c_str());
        Ledger << "INFO\trun\tadapter\t" << Editor->QueryImage().Adapter << "\n";

        // 📝 The picture of the refresh that was read back, written into the window as the editor's own draw data places it. The window is captured at
        //    rest: the keyboard focus ring of the last control used is hidden, as a mouse click hides it.
        const auto WriteCapture = [&](const std::string& Name) {
            AfterNewFrame = [] { ImGui::SetNavCursorVisible(false); };
            Tick(2);
            AfterNewFrame = nullptr;
            const FluidViewportImage& Image = Editor->QueryImage();
            Require(Image.Failure.empty(), Name + ": " + Image.Failure);
            Require(Image.Texture == FluidTag && !Pixels.empty(), Name + ": the viewport has no Fluid picture");
            const ImDrawData*          Submitted = ImGui::GetDrawData();
            std::vector<unsigned char> Window(static_cast<size_t>(Width) * Height * 3u, 14u);
            const std::function<FrontierExhibit::Image(ImTextureID)> Resolve = [&](ImTextureID Texture) {
                return Texture == FluidTag ? FrontierExhibit::Image{ Pixels.data(), static_cast<int>(Extent[0]), static_cast<int>(Extent[1]), 4 } : FrontierExhibit::Image{};
            };
            for (const ImDrawList* List : Submitted->CmdLists)
                FrontierExhibit::RasterizeCommands(List, Window.data(), Width, Height, Submitted->DisplayPos, Submitted->FramebufferScale, nullptr, &Resolve);
            const std::filesystem::path Target = OutputDir / (Name + ".png");
            Require(stbi_write_png(Target.string().c_str(), Width, Height, 3, Window.data(), Width * 3) != 0, "PNG write failed: " + Target.string());
            std::vector<double> Times = RefreshTimes;
            RefreshTimes.clear();
            std::sort(Times.begin(), Times.end());
            if (!Times.empty())
                Ledger << "INFO\t" << Name << "\trefresh ms (SwiftShader, Debug)\t" << Times.size() << " refreshes: median " << Describe(Times[Times.size() / 2]) << ", max " << Describe(Times.back()) << '\n';
            std::printf("[FluidEditorCapture] wrote %s (%d x %d window, %u x %u picture)\n", Target.string().c_str(), Width, Height, Extent[0], Extent[1]);
        };
        const auto SettingsEqual = [&](const GasSettings& Left, const GasSettings& Right) {
            int Differ = 0;
            const FluidEditor::ControlRow* Rail = FluidEditor::FluidControls();
            for (uint32_t Index = 0; Index < FluidEditor::FluidControlCount; ++Index)
            {
                float A = 0.0f, B = 0.0f;
                if (!FluidEditor::ReadSetting(Left, Rail[Index].Key, A) || !FluidEditor::ReadSetting(Right, Rail[Index].Key, B) || A != B) ++Differ;
            }
            return Differ;
        };

        // ① The window as the prototype opens: the emitter selected on its Source tab, the camera standing back at 4.1 looking at (0, 0.3, 0), the record at InitialParameters.
        {
            const PyroOrbitCamera& Camera = Editor->QueryCamera();
            Claim("Opening", "emitter selected on Source; camera aims at distance 4.1, centre (0, 0.3, 0); the record holds the prototype's InitialParameters in all 82 controls",
                  std::string(FluidEditorPages::ResolveToken(Editor->QuerySelected())) + " on " + FluidEditorPages::ResolveTabToken(Editor->QueryTab()) + "; aim " + Describe(Camera.TargetDistance, 2) +
                      " at (" + Describe(Camera.TargetCentre[0], 2) + ", " + Describe(Camera.TargetCentre[1], 2) + ", " + Describe(Camera.TargetCentre[2], 2) + "); " +
                      std::to_string(SettingsEqual(Volume.Gas, GasSettings{})) + " controls differ",
                  Editor->QuerySelected() == FluidPart::Emitter && Editor->QueryTab() == FluidTab::Source && Camera.TargetDistance == 4.1f && Camera.TargetCentre[1] == 0.3f &&
                      SettingsEqual(Volume.Gas, GasSettings{}) == 0);
        }

        // ② The solver runs while the window is open: the clock counts, flame and smoke appear, and the picture is the viewport at the render scale.
        Press("restart");
        Tick(2);
        const std::vector<uint8_t> AtRest = Pixels;
        Tick(48);
        {
            const std::array<ImVec2, 2> Box = Editor->QueryViewport();
            const double                Wide = Box[1].x - Box[0].x, High = Box[1].y - Box[0].y;
            const double                Scale = Volume.Gas.RenderScale;
            Claim("The solver runs", "after a restart and 48 refreshes the steps count, the clock is above 0.5 s and flame is redder than the first picture's",
                  std::to_string(Readout.Steps) + " steps, " + Describe(Readout.SimulationSeconds, 2) + " s, " + std::to_string(CountWarm(Pixels, 10)) + " warm px (at rest " + std::to_string(CountWarm(AtRest, 10)) + ")",
                  Readout.Steps >= 30 && Readout.SimulationSeconds > 0.5 && CountWarm(Pixels, 10) > CountWarm(AtRest, 10));
            Claim("The picture follows the viewport", "the picture is the viewport's pixels at the render scale (within 1 px)",
                  std::to_string(Extent[0]) + " x " + std::to_string(Extent[1]) + " for " + Describe(Wide, 0) + " x " + Describe(High, 0) + " at " + Describe(Scale, 2),
                  std::fabs(Extent[0] - Wide * Scale) <= 1.5 && std::fabs(Extent[1] - High * Scale) <= 1.5);
        }
        WriteCapture("FluidEditor-Boot");

        // ③ Pause, step, restart: the transport as the prototype's (Space pauses, S steps one 1/60 s, R restarts).
        Press("play");
        Tick(3);
        const uint32_t HeldSteps = Readout.Steps;
        Tick(6);
        Claim("Pause", "Gas.Paused is set and six more refreshes take no step", "paused " + std::string(Volume.Gas.Paused ? "yes" : "no") + ", steps " + std::to_string(HeldSteps) + " -> " + std::to_string(Readout.Steps),
              Volume.Gas.Paused && Readout.Steps == HeldSteps);
        Press("step");
        Tick(2);
        Claim("Step once", "one step of 1/60 s while paused", "steps " + std::to_string(HeldSteps) + " -> " + std::to_string(Readout.Steps), Volume.Gas.Paused && Readout.Steps == HeldSteps + 1);
        Press("play");
        Tick(6);
        Claim("Play", "the clock runs on again", "steps " + std::to_string(HeldSteps + 1) + " -> " + std::to_string(Readout.Steps), !Volume.Gas.Paused && Readout.Steps > HeldSteps + 1);
        const uint32_t BeforeRestart = Readout.Steps;
        Press("restart");
        Tick(2);
        Claim("Restart", "the clock and the step count start from zero again: only the refreshes since the press (four at most) have stepped", "steps " + std::to_string(BeforeRestart) + " -> " + std::to_string(Readout.Steps) + ", " + Describe(Readout.SimulationSeconds, 3) + " s",
              BeforeRestart > 8 && Readout.Steps <= 4 && Readout.SimulationSeconds < 0.15);

        // ④ The camera: a drag of 100 px orbits 0.0075 rad a pixel (camera.js), the wheel zooms, a right-drag pans.
        {
            PyroOrbitCamera Probe = Editor->QueryCamera();
            const PyroOrbitCamera Start = Probe;
            Probe.OrbitByPixels(-100.0f, 40.0f);
            Drag(ViewportMiddle(), ImVec2(-100.0f, 40.0f), 0, false);
            const PyroOrbitCamera& Turned = Editor->QueryCamera();
            Claim("Orbit", "a left-drag of (-100, 40) px over the viewport aims the camera as camera.js does", "theta " + Describe(Start.TargetTheta, 3) + " -> " + Describe(Turned.TargetTheta, 3) + " (expected " +
                  Describe(Probe.TargetTheta, 3) + "), phi " + Describe(Start.TargetPhi, 3) + " -> " + Describe(Turned.TargetPhi, 3) + " (expected " + Describe(Probe.TargetPhi, 3) + ")",
                  std::fabs(Turned.TargetTheta - Probe.TargetTheta) < 1.0e-5f && std::fabs(Turned.TargetPhi - Probe.TargetPhi) < 1.0e-5f);
            PyroOrbitCamera Zoom = Editor->QueryCamera();
            Zoom.ZoomByWheel(-100.0f);
            Pointer = ViewportMiddle();
            Step();
            Io.AddMouseWheelEvent(0.0f, 1.0f);
            Step();
            Away();
            Step();
            Claim("Zoom", "one wheel notch toward the screen (ImGui +1) is a browser delta of -100: the distance shrinks as camera.js says", "distance " + Describe(Turned.TargetDistance, 3) + ", expected " + Describe(Zoom.TargetDistance, 3),
                  std::fabs(Editor->QueryCamera().TargetDistance - Zoom.TargetDistance) < 1.0e-4f);
            Tick(40);   // 📝 the view settles first: a pan moves the aim along the camera's current right and up (camera.js pan), which the orbit's easing is still turning
            PyroOrbitCamera Pan = Editor->QueryCamera();
            Pan.PanByPixels(60.0f, -30.0f);
            Drag(ViewportMiddle(), ImVec2(60.0f, -30.0f), 1, false);
            Claim("Pan", "a right-drag of (60, -30) px moves the aim's centre as camera.js does", "centre (" + Describe(Editor->QueryCamera().TargetCentre[0], 3) + ", " + Describe(Editor->QueryCamera().TargetCentre[1], 3) + ") expected (" +
                  Describe(Pan.TargetCentre[0], 3) + ", " + Describe(Pan.TargetCentre[1], 3) + ")",
                  std::fabs(Editor->QueryCamera().TargetCentre[0] - Pan.TargetCentre[0]) < 1.0e-4f && std::fabs(Editor->QueryCamera().TargetCentre[1] - Pan.TargetCentre[1]) < 1.0e-4f);
        }
        Press("view:front");
        Claim("Camera views", "Front turns the aim to theta 0, phi pi/2", "theta " + Describe(Editor->QueryCamera().TargetTheta, 3) + ", phi " + Describe(Editor->QueryCamera().TargetPhi, 3),
              std::fabs(Editor->QueryCamera().TargetTheta) < 1.0e-6f && std::fabs(Editor->QueryCamera().TargetPhi - 1.5707964f) < 1.0e-5f);
        Press("focus");
        Tick(40);

        // ⑤ The tools: Place burst (3) fires a burst where the ray meets the domain; Source brush (2) lays a flame while the pointer is held.
        Press("restart");
        Tick(2);
        const std::vector<uint8_t> Quiet = Pixels;
        Press("tool:detonate");
        Claim("Tool: Place burst", "the tool is detonate", std::string(Volume.Gas.InteractionMode), std::string(Volume.Gas.InteractionMode) == "detonate");
        Click(ViewportMiddle());
        Tick(30);
        Claim("Place burst", "a click inside the domain sets a burst off: flame and smoke where the picture was quiet", std::to_string(CountDifferences(Pixels, Quiet, 12)) + " px changed, " + std::to_string(CountWarm(Pixels, 10)) + " warm",
              CountDifferences(Pixels, Quiet, 12) > 200 && CountWarm(Pixels, 10) > CountWarm(Quiet, 10));
        WriteCapture("FluidEditor-Burst");
        Press("tool:flamethrower");
        Press("restart");
        Tick(2);
        {
            const std::vector<uint8_t> Before = Pixels;
            const ImVec2               From   = ViewportMiddle();
            Io.AddKeyEvent(ImGuiMod_Shift, false);
            Pointer = From;
            Step();
            Io.AddMouseButtonEvent(0, true);
            Step();
            Pointer = ImVec2(From.x + 40.0f, From.y - 20.0f);
            Step();
            const bool Held = Editor->QueryLastRequest().Brush.Held;
            Tick(20);
            Io.AddMouseButtonEvent(0, false);
            Step();
            Claim("Source brush", "while the button is held over the domain the request carries the brush; the flame it lays shows",
                  std::string("brush ") + (Held ? "held" : "not held") + ", " + std::to_string(CountDifferences(Pixels, Before, 12)) + " px changed", Held && CountDifferences(Pixels, Before, 12) > 100);
            Away();
            Step();
            Step();
            Claim("Source brush released", "the brush goes out with the button", std::string("brush ") + (Editor->QueryLastRequest().Brush.Held ? "held" : "not held"), !Editor->QueryLastRequest().Brush.Held);
        }
        Press("tool:orbit");

        // ⑥ The preset library, by the pointer: the sheet lists the 18 effects, a filter narrows it, a card sets all the settings and selects as the prototype does.
        Press("library");
        Tick(2);
        Claim("Library opens", "the sheet shows the 18 effects", std::to_string(Editor->QueryLibraryCards()) + " cards", Editor->QueryLibraryShown() && Editor->QueryLibraryCards() == FluidEditor::FluidPresetCardCount);
        WriteCapture("FluidEditor-Library");
        Press("library-group:fire");
        Tick(2);
        size_t Fires = 0;
        for (uint32_t Index = 0; Index < FluidEditor::FluidPresetCardCount; ++Index)
            if (std::string(FluidEditor::FluidPresetCards()[Index].Category) == "fire") ++Fires;
        Claim("Library filter", "Fire narrows the sheet to the fire effects", std::to_string(Editor->QueryLibraryCards()) + " cards, " + std::to_string(Fires) + " fire presets", Editor->QueryLibraryCards() == Fires);
        Press("library-group:all");
        Tick(2);
        const char* Chosen = "tactical_ordnance";
        Press(std::string("library-card:") + Chosen);
        Tick(2);
        {
            const GasPreset*  Preset   = FindGasPreset(Chosen);
            const GasSettings Expected = ConstructPresetSettings(Chosen);
            Claim("Preset card", "the card sets all 82 controls to the prototype's preset, records its name, closes the sheet, selects the emitter (else the domain) on its tab",
                  std::string(Volume.Preset) + ", " + std::to_string(SettingsEqual(Volume.Gas, Expected)) + " controls differ, sheet " + (Editor->QueryLibraryShown() ? "open" : "closed") + ", " +
                      FluidEditorPages::ResolveToken(Editor->QuerySelected()) + " on " + FluidEditorPages::ResolveTabToken(Editor->QueryTab()),
                  Preset != nullptr && Volume.Preset == Chosen && SettingsEqual(Volume.Gas, Expected) == 0 && !Editor->QueryLibraryShown() &&
                      Editor->QuerySelected() == (Expected.EmitterEnabled ? FluidPart::Emitter : FluidPart::Domain));
            Tick(40);
            Claim("Preset starts", "the preset runs from a restart, with its burst when the prototype sets one off on load", std::to_string(Readout.Steps) + " steps, " + std::to_string(CountWarm(Pixels, 10)) + " warm px",
                  Readout.Steps > 10 && CountWarm(Pixels, 10) > 0);
        }
        WriteCapture("FluidEditor-Preset");

        // ⑦ The Inspector's tabs of a part: each tab is a page of the real controls; the captures show them.
        Press("row:Domain");
        Claim("Outliner: Domain", "the domain opens on Simulation", std::string(FluidEditorPages::ResolveToken(Editor->QuerySelected())) + " on " + FluidEditorPages::ResolveTabToken(Editor->QueryTab()),
              Editor->QuerySelected() == FluidPart::Domain && Editor->QueryTab() == FluidTab::Simulation);
        WriteCapture("FluidEditor-Domain");
        Press("row:Sun");
        Claim("Outliner: Sun", "the light opens on Rendering", std::string(FluidEditorPages::ResolveToken(Editor->QuerySelected())) + " on " + FluidEditorPages::ResolveTabToken(Editor->QueryTab()),
              Editor->QuerySelected() == FluidPart::Sun && Editor->QueryTab() == FluidTab::Rendering);
        const float LightWas = Volume.Gas.SunIntensity;
        Press("eye:Sun");
        Claim("Eye of the light", "switches the light off (intensity 0) and remembers its strength", "intensity " + Describe(LightWas, 2) + " -> " + Describe(Volume.Gas.SunIntensity, 2) + ", remembers " + Describe(Editor->QueryMemory().SavedSun, 2),
              Volume.Gas.SunIntensity == 0.0f && std::fabs(Editor->QueryMemory().SavedSun - LightWas) < 1.0e-6f);
        Press("eye:Sun");
        Claim("Eye of the light, again", "brings the strength back", "intensity " + Describe(Volume.Gas.SunIntensity, 2), std::fabs(Volume.Gas.SunIntensity - LightWas) < 1.0e-6f);
        Press("row:Emitter");
        WriteCapture("FluidEditor-Emitter");

        // ⑧ A volume that is disabled stands still (drawn, not stepped); one that is enabled runs on.
        Editor->Apply("enabled:off");
        Tick(2);
        const uint32_t Still = Readout.Steps;
        Tick(8);
        Claim("Disabled volume", "eight refreshes take no step, and the picture is still drawn", "steps " + std::to_string(Still) + " -> " + std::to_string(Readout.Steps) + ", picture " + std::to_string(Extent[0]) + " x " + std::to_string(Extent[1]),
              !Volume.Enabled && Readout.Steps == Still && Extent[0] > 0);
        Editor->Apply("enabled:on");
        Tick(6);
        Claim("Enabled again", "the clock runs", "steps " + std::to_string(Still) + " -> " + std::to_string(Readout.Steps), Volume.Enabled && Readout.Steps > Still);

        // ⑨ The record's TOML: what the window set is what a level saves and loads.
        {
            const RecordToken Copy = World.ConstructRecord("FluidVolume", "Copy");
            auto*             Other = dynamic_cast<FluidVolumeRecord*>(World.FindRecord(Copy));
            Require(Other != nullptr, "the second FluidVolume record could not be made");
            toml::table Table;
            Record->SerializeConfiguration(Table);
            Other->DeserializeConfiguration(Table);
            const FluidVolumeConfiguration& Back = Other->QueryConfiguration();
            Claim("Record round trip", "a record written to its TOML and read into another holds the same preset, enabled flag and all 82 controls",
                  std::string(Back.Preset) + ", enabled " + (Back.Enabled ? "yes" : "no") + ", " + std::to_string(SettingsEqual(Back.Gas, Volume.Gas)) + " controls differ",
                  Back.Preset == Volume.Preset && Back.Enabled == Volume.Enabled && SettingsEqual(Back.Gas, Volume.Gas) == 0);
        }

        // ⑩ A larger window: the viewport and the picture grow with it.
        {
            const std::array<uint32_t, 2> Before = Extent;
            Width  = 1920;
            Height = 1080;
            Tick(6);
            Claim("Window of 1920 x 1080", "the picture grows with the viewport and the solver goes on", std::to_string(Before[0]) + " x " + std::to_string(Before[1]) + " -> " + std::to_string(Extent[0]) + " x " + std::to_string(Extent[1]),
                  Extent[0] > Before[0] && Extent[1] > Before[1] && Readout.Steps > 0);
            WriteCapture("FluidEditor-1080p");
            Width  = 1366;
            Height = 800;
        }

        // ⑪ Without a solver the window still edits and says why the viewport is empty.
        Solver = false;
        Tick(3);
        Claim("No solver", "the viewport says why it is empty and the Inspector is still there", "failure: " + Editor->QueryImage().Failure, !Editor->QueryImage().Failure.empty() && Editor->QueryImage().Texture == 0);
        AfterNewFrame = [] { ImGui::SetNavCursorVisible(false); };
        Tick(2);
        AfterNewFrame = nullptr;
        {
            const ImDrawData*          Submitted = ImGui::GetDrawData();
            std::vector<unsigned char> Window(static_cast<size_t>(Width) * Height * 3u, 14u);
            for (const ImDrawList* List : Submitted->CmdLists)
                FrontierExhibit::RasterizeCommands(List, Window.data(), Width, Height, Submitted->DisplayPos, Submitted->FramebufferScale, nullptr, nullptr);
            const std::filesystem::path Target = OutputDir / "FluidEditor-NoSolver.png";
            Require(stbi_write_png(Target.string().c_str(), Width, Height, 3, Window.data(), Width * 3) != 0, "PNG write failed: " + Target.string());
        }

        Ledger << "SUMMARY\t" << Claims << " claims\t" << Failures << " failed\n";
        std::printf("[FluidEditorCapture] %d claims, %d failed\n", Claims, Failures);
        return Failures != 0 ? 1 : 0;
    }
    catch (const std::exception& Failure)
    {
        std::fprintf(stderr, "[FluidEditorCapture] %s\n", Failure.what());
        return 2;
    }
}
