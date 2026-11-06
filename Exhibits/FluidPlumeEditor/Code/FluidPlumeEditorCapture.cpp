/*==========================================================================================================================================
                                                    FLUIDPLUMEEDITORCAPTURE.CPP
==========================================================================================================================================*/
// 🧩 The Smoke & fire editor with the plume's own picture in its stage. The window is the one FluidPlumeEditorValidation drives, opened from a
//    FluidPlume record the way the Inspector's button opens it; the presenter registered here is PlumeExtension (the editor application's), on an
//    own Vulkan device: the C++ plume (SmokeSequence for the sources, PlumeSimulationSequence for the lattices and the march, both bit for bit
//    the prototype's) runs the gas the Inspector holds, the emitter the editor asks for and the camera its stage orbits, and its radiance is
//    composited over the backdrop into the stage. The picture is read back only for the tick a capture rasterizes.
//
//        FluidPlumeEditorCapture <EngineContent> <Projects/Everest> <ShaderRoot> <output folder> [low|medium|high]
//
//    The grade is the level's Render settings quality tier (the lattice follows it): high, the tier of a level without such a record, unless one is named.
//
//    Every capture is checked against what it should show (the plume's pixels counted against the empty stage; the readout the plume reports) and the
//    claims are written to LivePlume.tsv beside the PNGs. The window is drawn by Dear ImGui on the CPU raster of Exhibits/ControlCentre/Code/CpuRasterization.h.

#include "Engine/Interface/FluidPlumeEditor.h"
#include "Engine/Interface/FluidPlumeEditor/FluidPlumeEditorWorkspace.h"
#include "Engine/Interface/IconPresentation.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Interface/SceneDirectoryPanel.h"
#include "Engine/Scene/SceneDirectory.h"
#include "Engine/Scene/WorldSpace.h"
#include "EverestWorkspace.h"
#include "Exhibits/ControlCentre/Code/CpuRasterization.h"
#include "Frontier/Extensions/Plume/PlumeExtension.h"
#include "UI/Editor/ControlCentrePanel.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <stb_image_write.h>

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
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

constexpr uint64_t PlumeTag = 0x9A27C2u;   // [-] the ImTextureID the capture resolves to the read-back plume picture
constexpr int      Develop  = 96;          // [refreshes] a plume's growth before it is captured: 3.2 s of the stage's clock at 30 refreshes a second

void Require(bool Condition, const std::string& Message)
{
    if (!Condition) throw std::runtime_error(Message);
}

std::ofstream Ledger;
int           Claims   = 0;
int           Failures = 0;

// 📝 One line of LivePlume.tsv: what a capture should show, what it showed, whether the two agree.
void Claim(const std::string& Subject, const std::string& Expected, const std::string& Observed, bool Held)
{
    ++Claims;
    if (!Held) ++Failures;
    Ledger << (Held ? "PASS" : "FAIL") << '\t' << Subject << '\t' << Expected << '\t' << Observed << '\n';
    std::printf("[FluidPlumeEditorCapture] %s  %s — expected %s, observed %s\n", Held ? "PASS" : "FAIL", Subject.c_str(), Expected.c_str(), Observed.c_str());
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

// 📝 Pixels warmer than any pixel of the backdrop (red above blue): the incandescence of flame and embers, or sun-warmed smoke.
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
        Require(ArgumentCount == 5 || ArgumentCount == 6, "usage: FluidPlumeEditorCapture <EngineContent> <Projects/Everest> <ShaderRoot> <output folder> [low|medium|high]");
        const std::string Grade = ArgumentCount == 6 ? Arguments[5] : "high";
        Require(Grade == "low" || Grade == "medium" || Grade == "high", "the grade is low, medium or high");
        const std::filesystem::path ContentDir  = std::filesystem::absolute(Arguments[1]);
        const std::filesystem::path EverestRoot = std::filesystem::absolute(Arguments[2]);
        const std::filesystem::path ShaderRoot  = std::filesystem::absolute(Arguments[3]);
        const std::filesystem::path OutputDir   = std::filesystem::absolute(Arguments[4]);
        std::filesystem::create_directories(OutputDir);
        Ledger.open(OutputDir / "LivePlume.tsv");
        Ledger << "verdict\tcapture\texpected\tobserved\n";

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
        // 📝 The level's Render settings record carries the quality tier the plume's lattice follows (0 low, 1 medium, 2 high): the Everest project's own
        //    record when it has one, a new one when it has none.
        {
            auto* Record = dynamic_cast<RenderPipelineRecord*>(World.FindFirstByType("RenderPipeline"));
            if (Record == nullptr)
            {
                const RecordToken Pipeline = World.ConstructRecord("RenderPipeline", "Render settings");
                Require(Pipeline.IsValid(), "the RenderPipeline record could not be constructed");
                Record = dynamic_cast<RenderPipelineRecord*>(World.FindRecord(Pipeline));
            }
            Require(Record != nullptr, "the RenderPipeline record has the wrong type");
            std::printf("[FluidPlumeEditorCapture] Render settings quality tier %d -> %s\n", Record->AccessConfiguration().QualityTier, Grade.c_str());
            Record->AccessConfiguration().QualityTier = Grade == "low" ? 0 : (Grade == "medium" ? 1 : 2);
        }
        Frontier::PlumeExtension  Bridge(ShaderRoot, nullptr);
        std::vector<uint8_t>      Pixels;         // [-] the plume picture of the last refresh that was read back
        std::array<uint32_t, 2>   Extent = { 0, 0 };
        bool                      ReadNext = false;
        std::vector<double>       RefreshTimes;
        FluidPlumeReadout         Readout{};
        InspectorRuntime::Instance().RegisterPlumeViewportPresenter([&](const PlumeViewportRequest& Request) {
            Frontier::PlumeViewportOutcome Outcome = Bridge.Render(Request, World);
            PlumeViewportImage             Image   = std::move(Outcome.Image);
            if (Outcome.View != VK_NULL_HANDLE)
            {
                RefreshTimes.push_back(Image.RefreshMilliseconds);
                Image.Texture = PlumeTag;
                Readout       = Image.Readout;
                if (ReadNext)
                {
                    std::string Diagnostic;
                    Require(Bridge.ReadPresentedImage(Pixels, &Diagnostic), "plume picture readback: " + Diagnostic);
                    Extent = Image.Extent;
                }
            }
            return Image;
        });

        // 📝 A FluidPlume record as the Construct dialog adds one, selected, and its editor opened as the Inspector's Plume Editor button opens it.
        const RecordToken Token = World.ConstructRecord("FluidPlume", "Smoke and fire");
        Require(Token.IsValid(), "the FluidPlume record could not be constructed");
        Workspace.QueryOutliner().Select(Token);
        Require(InspectorRuntime::Instance().RequestEditorOpen(Token), "the FluidPlume record has no editor");

        int                   Width  = 1366;
        int                   Height = 800;
        ImVec2                Pointer(-FLT_MAX, -FLT_MAX);
        std::function<void()> AfterNewFrame;   // [-] the part of a refresh between NewFrame and the workspace
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
                if (Count > 24 && (Index + 1) % 24 == 0) std::printf("[FluidPlumeEditorCapture]   %d of %d refreshes\n", Index + 1, Count);
            }
            ReadNext = false;
        };
        // 📝 A pointer press on the screen: it arrives and hovers, presses, releases and leaves.
        const auto Click = [&](ImVec2 At) {
            Pointer = At;
            Step();
            Io.AddMouseButtonEvent(0, true);
            Step();
            Io.AddMouseButtonEvent(0, false);
            Step();
            Pointer = ImVec2(-FLT_MAX, -FLT_MAX);
            Step();
        };
        const auto Press = [&](std::string_view Name) {
            const FluidPlumeControlRectangle* Control = FluidPlumeEditor::AccessWorkspace()->FindControl(Name);
            Require(Control != nullptr, "the editor has no control `" + std::string(Name) + "`");
            Click(ImVec2((Control->Minimum.x + Control->Maximum.x) * 0.5f, (Control->Minimum.y + Control->Maximum.y) * 0.5f));
        };
        const auto Drag = [&](ImVec2 From, ImVec2 Delta) {
            Pointer = From;
            Step();
            Io.AddMouseButtonEvent(0, true);
            Step();
            Pointer = ImVec2(From.x + Delta.x, From.y + Delta.y);
            Step();
            Io.AddMouseButtonEvent(0, false);
            Step();
            Pointer = ImVec2(-FLT_MAX, -FLT_MAX);
            Step();
        };

        Tick(1);
        FluidPlumeEditorWorkspace* Editor = FluidPlumeEditor::AccessWorkspace();
        Require(Editor != nullptr && InspectorRuntime::Instance().QueryActiveEditorToken() == Token, "the Smoke & fire editor did not open");
        Require(Editor->QueryImage().Failure.empty(), "the plume could not run: " + Editor->QueryImage().Failure);
        std::printf("[FluidPlumeEditorCapture] editor open on %s\n", Editor->QueryImage().Adapter.c_str());
        Ledger << "INFO\trun\tadapter, quality tier\t" << Editor->QueryImage().Adapter << ", " << Grade << " (the lattice follows the tier)\n";

        // 📝 The picture of the refresh that was read back, written into the window as the editor's own draw data places it. The window is captured at rest:
        //    the keyboard focus ring of the last control used is hidden, as a mouse click hides it (the two refreshes this takes are the plume's like any other).
        const auto WriteCapture = [&](const std::string& Name) {
            AfterNewFrame = [] { ImGui::SetNavCursorVisible(false); };
            Tick(2);
            AfterNewFrame = nullptr;
            const PlumeViewportImage& Image = Editor->QueryImage();
            Require(Image.Failure.empty(), Name + ": " + Image.Failure);
            Require(Image.Texture == PlumeTag && !Pixels.empty(), Name + ": the viewport has no plume picture");
            // 📝 The picture is a whole number of trace texels: it may exceed the stage by less than a texel (the extension's scale is at most 3 here) and never fall short of it.
            const std::array<ImVec2, 2> Stage = Editor->QueryStage();
            const double                StageW = Stage[1].x - Stage[0].x, StageH = Stage[1].y - Stage[0].y;
            Claim(Name + " · the picture follows the stage", "the picture covers the stage and exceeds it by under 3 px",
                  std::to_string(Extent[0]) + " × " + std::to_string(Extent[1]) + " picture, " + Describe(StageW, 0) + " × " + Describe(StageH, 0) + " stage",
                  Extent[0] >= StageW - 0.5 && Extent[0] < StageW + 3.0 && Extent[1] >= StageH - 0.5 && Extent[1] < StageH + 3.0);
            const ImDrawData*          Submitted = ImGui::GetDrawData();
            std::vector<unsigned char> Window(static_cast<size_t>(Width) * Height * 3u, 14u);
            const std::function<FrontierExhibit::Image(ImTextureID)> Resolve = [&](ImTextureID Texture) {
                return Texture == PlumeTag ? FrontierExhibit::Image{ Pixels.data(), static_cast<int>(Extent[0]), static_cast<int>(Extent[1]), 4 } : FrontierExhibit::Image{};
            };
            for (const ImDrawList* List : Submitted->CmdLists)
                FrontierExhibit::RasterizeCommands(List, Window.data(), Width, Height, Submitted->DisplayPos, Submitted->FramebufferScale, nullptr, &Resolve);
            const std::filesystem::path Target = OutputDir / (Name + ".png");
            Require(stbi_write_png(Target.string().c_str(), Width, Height, 3, Window.data(), Width * 3) != 0, "PNG write failed: " + Target.string());
            // 📝 What the refreshes cost here: SwiftShader on the CPU, a Debug build. Not a figure for a GPU.
            std::vector<double> Times = RefreshTimes;
            RefreshTimes.clear();
            std::sort(Times.begin(), Times.end());
            if (!Times.empty())
                Ledger << "INFO\t" << Name << "\trefresh ms (SwiftShader, Debug)\t" << Times.size() << " refreshes: median " << Describe(Times[Times.size() / 2]) << ", max "
                       << Describe(Times.back()) << '\n';
            std::printf("[FluidPlumeEditorCapture] wrote %s (%d x %d window, %u x %u plume picture)\n", Target.string().c_str(), Width, Height, Extent[0], Extent[1]);
        };
        const auto Share = [&](size_t Count) { return 100.0 * static_cast<double>(Count) / (static_cast<double>(Extent[0]) * Extent[1]); };
        const auto Resident = [&]() { return std::string(Readout.Resident ? "resident" : "not resident"); };

        // ① The empty stage: no emitter, nothing in the lattices. The picture is the backdrop alone and the plume records no march.
        Require(Editor->Perform("emitter:off") && Editor->Perform("clear-smoke"), "the emitter and Clear verbs were refused");
        Tick(2);
        const std::vector<uint8_t> Empty = Pixels;
        Claim("Empty stage", "the plume is not resident (no march recorded)", Resident() + ", " + std::to_string(Readout.Sources) + " sources", !Readout.Resident && Readout.Sources == 0);
        WriteCapture("SmokeAndFireEditor-Live-empty");

        // ② The default look, the smoke emitter, the stage's clock run on: the plume the Inspector's numbers describe.
        Require(Editor->Perform("emitter:smoke"), "the smoke emitter verb was refused");
        const uint64_t TicksBefore = Readout.Ticks;
        Tick(Develop);
        const size_t Smoke = CountDifferences(Pixels, Empty, 8);
        Claim("Tyre smoke · smoke emitter · " + std::to_string(Develop) + " refreshes", "resident, one transport step in nine refreshes of ten at least, over 0.5 % of the stage changed",
              Resident() + ", " + std::to_string(Readout.Ticks - TicksBefore) + " steps, " + Describe(Share(Smoke), 2) + " % of the stage", Readout.Resident && Readout.Ticks - TicksBefore >= static_cast<uint64_t>(Develop * 9 / 10) && Share(Smoke) > 0.5);
        WriteCapture("SmokeAndFireEditor-Live-smoke");
        const std::vector<uint8_t> SmokePicture = Pixels;

        // ③ Inferno from the libraries, pressed as a user presses it (the pointer on the Libraries button, then the card), the fire emitter, the lattices cleared.
        Press("library");
        Press("library-card:inferno");
        Require(!Editor->QueryLibraryShown(), "Inferno's card did not close the sheet");
        Require(Editor->Perform("emitter:fire") && Editor->Perform("clear-smoke"), "the fire emitter and Clear verbs were refused");
        Tick(Develop);
        const size_t Fire     = CountDifferences(Pixels, Empty, 8);
        const size_t FireWarm = CountWarm(Pixels, 10);
        Claim("Inferno · fire emitter · " + std::to_string(Develop) + " refreshes", "resident, over 0.5 % of the stage changed, flame and embers redder than any pixel of the empty stage",
              Resident() + ", " + Describe(Share(Fire), 2) + " % of the stage, " + std::to_string(FireWarm) + " px redder than blue (the empty stage: " + std::to_string(CountWarm(Empty, 10)) + ")",
              Readout.Resident && Share(Fire) > 0.5 && FireWarm > 0 && CountWarm(Empty, 10) == 0);
        WriteCapture("SmokeAndFireEditor-Live-inferno");

        // ④ Orbit: a drag over the stage turns the camera (yaw -0.006 rad a pixel across, pitch +0.006 down) and the fire is seen from another side.
        {
            const std::array<ImVec2, 2> Stage      = Editor->QueryStage();
            const ImVec2                Middle     = ImVec2((Stage[0].x + Stage[1].x) * 0.5f, (Stage[0].y + Stage[1].y) * 0.5f);
            const std::array<double, 3> Start      = Editor->QueryCamera();
            const std::vector<uint8_t>  Before     = Pixels;
            Drag(Middle, ImVec2(-120.0f, -20.0f));
            Tick(2);
            const std::array<double, 3> Turned = Editor->QueryCamera();
            const size_t                Moved  = CountDifferences(Pixels, Before, 8);
            Claim("Orbit · a drag of (-120, -20) px over the stage", "yaw +0.72 rad and pitch -0.12 rad (0.006 rad a pixel), the distance unchanged, over 5 % of the stage redrawn",
                  "yaw " + Describe(Turned[0] - Start[0], 3) + " rad, pitch " + Describe(Turned[1] - Start[1], 3) + " rad, distance " + Describe(Turned[2], 1) + " m, " + Describe(Share(Moved), 1) + " % of the stage",
                  std::fabs((Turned[0] - Start[0]) - 0.72) < 1.0e-6 && std::fabs((Turned[1] - Start[1]) + 0.12) < 1.0e-6 && Turned[2] == Start[2] && Share(Moved) > 5.0);
            WriteCapture("SmokeAndFireEditor-Live-orbit");
        }

        // ⑤ Crosswind, from the opening view again: the look's 5.5 m/s wind (against the Tyre smoke look's 0.8 m/s) must make a plume that is its own picture.
        Require(Editor->Perform("camera:0.3,0.22,11") && Editor->Perform("look:crosswind") && Editor->Perform("emitter:smoke") && Editor->Perform("clear-smoke"), "the Crosswind verbs were refused");
        Tick(Develop);
        const size_t Wind  = CountDifferences(Pixels, Empty, 8);
        const size_t Apart = CountDifferences(Pixels, SmokePicture, 8);
        Claim("Crosswind · smoke emitter · " + std::to_string(Develop) + " refreshes", "resident, over 0.5 % of the stage changed, and over 0.5 % of it unlike the Tyre smoke plume's picture",
              Resident() + ", " + Describe(Share(Wind), 2) + " % of the stage changed, " + Describe(Share(Apart), 2) + " % unlike the Tyre smoke picture", Readout.Resident && Share(Wind) > 0.5 && Share(Apart) > 0.5);
        WriteCapture("SmokeAndFireEditor-Live-crosswind");

        // ⑥ A 1920 × 1080 window: the stage grows, the picture follows it, the lattices stay (the steps go on counting).
        {
            const std::array<uint32_t, 2> Before = Extent;
            const uint64_t                Ticks  = Readout.Ticks;
            Width  = 1920;
            Height = 1080;
            Tick(6);
            Claim("Window of 1920 × 1080", "the picture grows with the stage and the plume goes on (its steps keep counting, still resident)",
                  std::to_string(Before[0]) + " × " + std::to_string(Before[1]) + " -> " + std::to_string(Extent[0]) + " × " + std::to_string(Extent[1]) + ", steps " + std::to_string(Ticks) + " -> " + std::to_string(Readout.Ticks) + ", " + Resident(),
                  Extent[0] > Before[0] && Extent[1] > Before[1] && Readout.Ticks > Ticks && Readout.Resident);
            WriteCapture("SmokeAndFireEditor-Live-1080p");
        }

        // ⑦ Clear smoke at the opening view: no emitter and an empty lattice give back the empty stage, pixel for pixel.
        Width  = 1366;
        Height = 800;
        Require(Editor->Perform("camera:0.3,0.22,11") && Editor->Perform("emitter:off") && Editor->Perform("clear-smoke"), "the camera, emitter and Clear verbs were refused");
        Tick(3);
        Claim("Clear smoke · emitter off · opening view", "the picture equals the empty stage's in every pixel", std::to_string(CountDifferences(Pixels, Empty, 0)) + " pixels differ, " + Resident(),
              CountDifferences(Pixels, Empty, 0) == 0 && !Readout.Resident);

        Ledger << "SUMMARY\t" << Claims << " claims\t" << Failures << " failed\n";
        std::printf("[FluidPlumeEditorCapture] %d claims, %d failed\n", Claims, Failures);
        return Failures != 0 ? 1 : 0;
    }
    catch (const std::exception& Failure)
    {
        std::fprintf(stderr, "[FluidPlumeEditorCapture] %s\n", Failure.what());
        return 2;
    }
}
