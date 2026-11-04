/*====================================================================================================================================
                                                         SOLIDARCCAPTURE.CPP
====================================================================================================================================*/
// 🧩 Headless SolidArc: the real ImGui shell on Vulkan (offscreen), driven by a script, written as PNG + JSON

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometryProcessing/Diagnostics/OperationTrace.h"
#include "UI/SolidArc/Document/SolidArcDocumentCodec.h"
#include "UI/SolidArc/Document/SolidArcSpecimen.h"
#include "UI/SolidArc/Interface/SolidArcFrame.h"
#include "UI/SolidArc/Interface/SolidArcInterface.h"
#include "UI/SolidArc/Interface/SolidArcOptions.h"
#include "UI/SolidArc/Interface/SolidArcScript.h"
#include "UI/SolidArc/Viewport/SolidArcDevice.h"
#include "UI/SolidArc/Viewport/SolidArcPortableImage.h"

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace Frontier::SolidArc;

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct CaptureSettings
{
    std::string DriverPath   = {};                                                           // [-]  - Vulkan ICD (else SOLIDARC_VULKAN_DRIVER / loader)
    std::string OptionsPath  = {};                                                           // [-]  - SolidArcWorkspace.toml
    std::string ScriptPath   = {};                                                           // [-]  - Input script
    std::string Exhibits     = "Exhibits/SolidArc/Results/Walkthrough";                       // [-]  - Output folder
    std::string RecoveryPath = {};                                                           // [-]  - Override of the autosave path
    std::string OpenPath     = {};                                                           // [-]  - Document opened at start
    std::string DocumentPath = {};                                                           // [-]  - Override of the default Save path
    std::string CurvesPath   = {};                                                           // [-]  - Override of the Import curves path
    std::string InsertFolder = {};                                                           // [-]  - Override of the knife insert library folder
    bool        Specimen     = false;                                                        // [-]  - Start with the Bracket specimen
    bool        ResetFiles   = false;                                                        // [-]  - Delete document / recovery files first
    int         Width        = 0;                                                            // [px] - Override of the frame width
    int         Height       = 0;                                                            // [px] - Override of the frame height
    std::string TracePath    = {};                                                           // [-]  - Operation trace file (empty: memory only)
    double      SlowFrame    = 0.0;                                                          // [ms] - Slow-frame line threshold (0: the default)
};

struct CaptureRecord
{
    std::string Name   = {};                                                                 // [-]  - Capture name
    uint64_t    Frame  = 0;                                                                  // [-]  - Frame number
    std::string Digest = {};                                                                 // [-]  - FNV-1a of the pixels
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    HELPERS
//------------------------------------------------------------------------------------------------------------------------

std::string ReadWholeFile(const std::string& Path)
{
    std::ifstream      Stream(Path, std::ios::binary);
    std::ostringstream Text;
    Text << Stream.rdbuf();
    return Text.str();
}

CaptureSettings ReadArguments(int Count, char** Arguments)
{
    CaptureSettings Settings;
    if (const char* Driver = std::getenv("SOLIDARC_VULKAN_DRIVER")) Settings.DriverPath = Driver;
    for (int Index = 1; Index < Count; ++Index)
    {
        const std::string Flag  = Arguments[Index];
        auto              Value = [&]() { return Index + 1 < Count ? std::string(Arguments[++Index]) : std::string(); };
        if (Flag == "--vulkan-driver") Settings.DriverPath = Value();
        else if (Flag == "--options") Settings.OptionsPath = Value();
        else if (Flag == "--script") Settings.ScriptPath = Value();
        else if (Flag == "--exhibits") Settings.Exhibits = Value();
        else if (Flag == "--recovery") Settings.RecoveryPath = Value();
        else if (Flag == "--open") Settings.OpenPath = Value();
        else if (Flag == "--document") Settings.DocumentPath = Value();
        else if (Flag == "--curves") Settings.CurvesPath = Value();
        else if (Flag == "--inserts") Settings.InsertFolder = Value();
        else if (Flag == "--specimen") Settings.Specimen = true;
        else if (Flag == "--reset-files") Settings.ResetFiles = true;
        else if (Flag == "--width") Settings.Width = std::atoi(Value().c_str());
        else if (Flag == "--height") Settings.Height = std::atoi(Value().c_str());
        else if (Flag == "--trace") Settings.TracePath = Value();
        else if (Flag == "--slow-frame") Settings.SlowFrame = std::atof(Value().c_str());
    }
    return Settings;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

int main(int Count, char** Arguments)
{
    const CaptureSettings Settings = ReadArguments(Count, Arguments);
    SolidArcOptions       Options  = LoadSolidArcOptions(Settings.OptionsPath);
    if (Settings.Width > 0) Options.WindowWidth = Settings.Width;
    // The app header (68 px) and document bar (34 px) left the layout (12 px margin instead): the walkthrough window
    // gives up the same 90 px so the viewport keeps its size, scale and pixel drags (an explicit --height wins).
    if (Settings.Height > 0) Options.WindowHeight = Settings.Height;
    else Options.WindowHeight -= 68 + 34 - 12;
    if (!Settings.RecoveryPath.empty()) Options.RecoveryPath = Settings.RecoveryPath;
    if (!Settings.DriverPath.empty()) Options.DriverPath = Settings.DriverPath;
    if (!Settings.DocumentPath.empty()) Options.DocumentPath = Settings.DocumentPath;
    if (!Settings.CurvesPath.empty()) Options.CurvesPath = Settings.CurvesPath;
    if (!Settings.InsertFolder.empty()) Options.InsertFolder = Settings.InsertFolder;
    if (!Settings.TracePath.empty()) Frontier::GeometryProcessing::OpenOperationTrace(Settings.TracePath);

    if (Settings.ResetFiles)
    {
        // A walkthrough must never pass on files left by an earlier run.
        std::error_code Failure;
        for (const std::string& Path : { Options.RecoveryPath, SolidArcPreviousRecoveryPath(Options), Options.DocumentPath })
            std::filesystem::remove(Path, Failure);
    }

    SolidArcScript Script = ParseSolidArcScript(Settings.ScriptPath.empty() ? std::string("frames 3\ncapture Workspace\n") : ReadWholeFile(Settings.ScriptPath));
    if (!Script.Refusal.empty())
    {
        std::fprintf(stderr, "script refused: %s\n", Script.Refusal.c_str());
        return 2;
    }

    // ① device (no presentation) + offscreen composed target
    SolidArcDevice Device;
    std::string    Refusal;
    if (!Device.Open(SolidArcDeviceSettings{ .DriverPath = Options.DriverPath, .ValidationEnabled = Options.ValidationEnabled }, Refusal))
    {
        std::fprintf(stderr, "no Vulkan device: %s\n", Refusal.c_str());
        return 77;
    }
    const uint32_t Width  = uint32_t(Options.WindowWidth);
    const uint32_t Height = uint32_t(Options.WindowHeight);
    SolidArcImage  Target = Device.CreateImage(Width, Height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                               VK_IMAGE_ASPECT_COLOR_BIT);
    SolidArcBuffer Readback = Device.CreateBuffer(VkDeviceSize(Width) * Height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);

    // ② ImGui + backend + workspace state
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& Io     = ImGui::GetIO();
    Io.DisplaySize  = ImVec2(float(Width), float(Height));
    Io.DeltaTime    = 1.0f / 60.0f;
    Io.ConfigInputTrickleEventQueue = true;
    ConfigureSolidArcInterface(Options);
    SolidArcWorkspaceFrame Frame;
    if (!Frame.Create(Device, VK_FORMAT_R8G8B8A8_UNORM, 2, Refusal))
    {
        std::fprintf(stderr, "workspace frame refused: %s\n", Refusal.c_str());
        return 1;
    }
    SolidArcWorkspaceState State;
    InitialiseSolidArcWorkspace(State, Options);
    State.DeviceLabel = Device.Describe() + (Frame.Raster.SampleCount() > 1 ? " · " + std::to_string(Frame.Raster.SampleCount()) + "× MSAA" : std::string(" · no MSAA"));
    State.ViewportRect = { 0.0f, 0.0f, float(Width) - 680.0f, float(Height) - 200.0f };
    if (Settings.Specimen)
    {
        State.WelcomeOpen = false;
        State.Document    = BuildSolidArcSpecimen();
        RefreshSolidArcScene(State, false);
        FitSolidArcCamera(State.Camera, State.Scene);
    }
    if (!Settings.OpenPath.empty())
    {
        State.WelcomeOpen = false;
        SolidArcDocumentFile File = LoadSolidArcDocumentFile(Settings.OpenPath);
        if (File.Refusal.empty())
        {
            State.Document          = CreateSolidArcDocument(File.Title);
            State.Document.Tree     = File.Tree;
            State.Document.Captions = File.Captions;
            State.Document.ImagePlanes = File.ImagePlanes;
            State.Document.Appearance  = File.Appearance;
            State.Document.DrawingSheet = File.DrawingSheet;
            State.Document.Dimensions   = File.Dimensions;
            RegenerateSolidArcDocument(State.Document, true);
            RefreshSolidArcScene(State, false);
            FitSolidArcCamera(State.Camera, State.Scene);
            State.DocumentPath = Settings.OpenPath;
        }
    }
    ConfigureSolidArcStartupViews(State);
    std::filesystem::create_directories(Settings.Exhibits);

    // ③ one script step per frame
    std::vector<CaptureRecord> Captures;
    double                     SlowestMs = 0.0;
    const auto                 Started   = std::chrono::steady_clock::now();
    while (!FinishedSolidArcScript(Script))
    {
        const auto        FrameStart = std::chrono::steady_clock::now();
        const std::string Capture    = AdvanceSolidArcScript(Script, State);
        const auto        Uploading  = std::chrono::steady_clock::now();
        Frame.PrepareViewport(State);
        State.Profile.UploadMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Uploading).count();
        ImGui_ImplVulkan_NewFrame();
        ImGui::NewFrame();
        DrawSolidArcInterface(State, Frame.ViewportTexture(), 1.0 / 60.0);
        ImGui::Render();
        const SolidArcFrameTarget Composed{ Target.Image, Target.View, Width, Height, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL };
        Device.SubmitImmediately([&](VkCommandBuffer Commands)
        {
            Frame.Record(Commands, State, ImGui::GetDrawData(), Composed, false);
            if (!Capture.empty())
            {
                VkBufferImageCopy Whole{};
                Whole.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
                Whole.imageExtent      = { Width, Height, 1 };
                vkCmdCopyImageToBuffer(Commands, Target.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, Readback.Buffer, 1, &Whole);
            }
        });
        Frame.Conclude(State);
        State.FrameMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - FrameStart).count();
        SlowestMs     = std::max(SlowestMs, State.FrameMs);
        if (Settings.SlowFrame > 0.0) State.Profile.SlowThreshold = Settings.SlowFrame, State.Profile.LineGap = 0.0;
        ConcludeSolidArcFrame(State);
        if (!Capture.empty())
        {
            const uint32_t*   Pixels = static_cast<const uint32_t*>(Readback.Mapped);
            const std::string Stem   = Settings.Exhibits + "/" + Capture;
            WriteSolidArcPng(Stem + ".png", Width, Height, Pixels);
            std::ofstream(Stem + ".json") << DescribeSolidArcWorkspace(State);
            Captures.push_back({ Capture, State.FrameNumber, DigestSolidArcPixels(Pixels, size_t(Width) * Height) });
            std::printf("capture %-28s frame %4llu  %s\n", Capture.c_str(), static_cast<unsigned long long>(State.FrameNumber), Captures.back().Digest.c_str());
        }
    }
    // Trailing expectations after the last frame.
    AdvanceSolidArcScript(Script, State);
    const double TotalSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - Started).count();

    // ④ report
    int Failed = 0;
    for (const SolidArcExpectation& Expectation : Script.Expectations) Failed += Expectation.Passed ? 0 : 1;
    std::ostringstream Report;
    Report << "# SolidArcCapture\n\n";
    Report << "- Device: " << State.DeviceLabel << "\n- Frame: " << Width << " x " << Height << " · " << State.FrameNumber << " frames · "
           << TotalSeconds << " s · slowest frame " << SlowestMs << " ms\n";
    Report << "- Script: " << (Settings.ScriptPath.empty() ? "(default)" : Settings.ScriptPath) << "\n";
    Report << "- Expectations: " << (Script.Expectations.size() - size_t(Failed)) << " / " << Script.Expectations.size() << " passed\n\n";
    Report << "| Line | Expectation | Observed | Result |\n|-----:|-------------|----------|--------|\n";
    for (const SolidArcExpectation& Expectation : Script.Expectations)
    {
        // Cut on a code-point boundary: back off over UTF-8 continuation bytes (10xxxxxx) so "→" or "²" is never split.
        size_t Cut = std::min<size_t>(87, Expectation.Observed.size());
        while (Cut > 0 && Cut < Expectation.Observed.size() && (static_cast<unsigned char>(Expectation.Observed[Cut]) & 0xC0u) == 0x80u) --Cut;
        std::string Observed = Expectation.Observed.size() > 90 ? Expectation.Observed.substr(0, Cut) + "..." : Expectation.Observed;
        for (char& Character : Observed) if (Character == '|') Character = '/';
        Report << "| " << Expectation.Line << " | `" << Expectation.Text << "` | " << Observed << " | " << (Expectation.Passed ? "🟢" : "🔴") << " |\n";
    }
    Report << "\n| Capture | Frame | Digest |\n|---------|------:|--------|\n";
    for (const CaptureRecord& Record : Captures) Report << "| " << Record.Name << " | " << Record.Frame << " | `" << Record.Digest << "` |\n";
    Report << "\n## Journal\n\n";
    for (const std::string& Entry : State.Journal) Report << "- " << Entry << "\n";
    const std::string ReportName = Settings.ScriptPath.empty() ? std::string("Capture") : std::filesystem::path(Settings.ScriptPath).stem().string();
    std::ofstream(Settings.Exhibits + "/" + ReportName + ".md") << Report.str();
    std::printf("%s", Report.str().c_str());

    vkDeviceWaitIdle(Device.Device);
    Frame.Destroy();
    ImGui::DestroyContext();
    Device.DestroyBuffer(Readback);
    Device.DestroyImage(Target);
    return Failed == 0 ? 0 : 1;
}
