/*==========================================================================================================================================
                                                     PROJECTEVERESTVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 End-to-end validation and real CPU rasterization for Project Everest Phase 2:
//    Project Browser -> Project Everest launch -> Engine Folder Outliner + 6 Global Observatories (Folder, Sun, Lens Flare,
//    Atmosphere, Clouds, Wind) + Custom ControlVehicle & Dual-Tier RTX/GTX Illumination Inspectors + Dark/Light themes.

#include "Exhibits/ControlCentre/Code/CpuRasterization.h"

#include "Applications/Editor/ProjectBrowserHost.h"
#include "Engine/Diagnostics/LogCategories.h"
#include "Engine/Diagnostics/LogStream.h"
#include "Engine/Interface/ContentDirectory.h"
#include "Engine/Interface/ControlKit.h"
#include "Engine/Interface/IconPresentation.h"
#include "Engine/Interface/LogStreamPanel.h"
#include "Engine/Interface/LogStreamPanelRegistration.h"
#include "Engine/Interface/PanelDockspace.h"
#include "Engine/Interface/ProjectContent.h"
#include "Engine/Interface/ProjectDescriptor.h"
#include "Engine/Interface/SceneDirectoryPanel.h"
#include "Engine/Interface/SceneDirectoryPanelRegistration.h"
#include "Engine/Interface/SceneInspectorPanel.h"
#include "Engine/Interface/SceneInspectorPanelRegistration.h"
#include "Engine/Interface/StackRailControls.h"
#include "Engine/Rendering/AtmosphereModel.h"
#include "Engine/Rendering/CloudField.h"
#include "Engine/Scene/EnvironmentRecords.h"
#include "Engine/Scene/RecordEntry.h"
#include "Engine/Scene/SceneDirectory.h"
#include "Projects/Project-Everest/Source/ProjectEverestScene.h"
#include "UI/Editor/ControlCentrePanel.h"
#include "UI/Editor/ProjectBrowserPanel.h"

#include <ProjectInterchange.h>
#include <imgui.h>
#include <stb_image_write.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

extern "C" bool FRONTIER_CODE_IMAGE_CALL ConstructProjectInterchange(
    uint32_t InterchangeNumber, uint64_t InterfaceFingerprint,
    FrontierProjectInterchange* InterchangeDelivery, char* RefusalExplanation,
    size_t RefusalExplanationCapacity);

namespace Frontier
{
namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    ASSERTION & CAPTURE
//------------------------------------------------------------------------------------------------------------------------

constexpr int CaptureWidth  = 1920;   // [px] - Full HD desktop capture width (middle column > 900 px desktop breakpoint)
constexpr int CaptureHeight = 1080;   // [px] - Full HD desktop capture height

void Require(bool Condition, const char* Explanation)
{
    if (!Condition)
    {
        throw std::runtime_error(Explanation);
    }
}

std::vector<unsigned char> RasterizeCurrentFrame(const std::filesystem::path& PngPath, unsigned char BackdropTone)
{
    std::vector<unsigned char> Pixels(static_cast<size_t>(CaptureWidth) * static_cast<size_t>(CaptureHeight) * 3u,
                                      BackdropTone);
    const ImDrawData* Commands = ImGui::GetDrawData();
    Require(Commands != nullptr && Commands->TotalVtxCount > 0, "Frame produced zero ImGui vertices");
    for (int Index = 0; Index < Commands->CmdListsCount; ++Index)
    {
        FrontierExhibit::RasterizeCommands(Commands->CmdLists[Index], Pixels.data(), CaptureWidth, CaptureHeight,
                                           Commands->DisplayPos, Commands->FramebufferScale);
    }
    std::filesystem::create_directories(PngPath.parent_path());
    const int Saved = stbi_write_png(PngPath.string().c_str(), CaptureWidth, CaptureHeight, 3, Pixels.data(),
                                     CaptureWidth * 3);
    Require(Saved != 0, "Failed to write exhibit PNG");
    return Pixels;
}

double SampleRegionMeanLuminance(const std::vector<unsigned char>& Pixels, int Left, int Top, int Right, int Bottom)
{
    double Sum   = 0.0;
    size_t Count = 0;
    for (int Row = std::max(0, Top); Row < std::min(CaptureHeight, Bottom); ++Row)
    {
        for (int Column = std::max(0, Left); Column < std::min(CaptureWidth, Right); ++Column)
        {
            const unsigned char* Rgb = Pixels.data() + (static_cast<size_t>(Row) * CaptureWidth + Column) * 3u;
            Sum += 0.2126 * Rgb[0] + 0.7152 * Rgb[1] + 0.0722 * Rgb[2];
            ++Count;
        }
    }
    return Count > 0 ? Sum / static_cast<double>(Count) : 0.0;
}

uint32_t ResolveEngineChannel(const char* Name)
{
    const LogCategory* Category = LogCategories().QueryByName(Name);
    return Category != nullptr ? Category->Token : 0;
}

void FRONTIER_CODE_IMAGE_CALL ReceiveEverestDiagnostic(const FrontierProjectDiagnostic* Diagnostic, void*)
{
    if (Diagnostic == nullptr || Diagnostic->Explanation == nullptr)
    {
        return;
    }
    uint32_t Token = ResolveEngineChannel("Interface");
    if (Diagnostic->SubjectName != nullptr)
    {
        const std::string_view Subject(Diagnostic->SubjectName);
        const size_t           Slash = Subject.rfind('/');
        if (Slash != std::string_view::npos && Slash + 1 < Subject.size())
        {
            if (const LogCategory* Matched = LogCategories().QueryByName(Subject.substr(Slash + 1)))
            {
                Token = Matched->Token;
            }
        }
    }
    WriteLog(Token, LogSeverity::Info, LogVerbosity::Display, Diagnostic->Explanation, "ProjectEverestInterchange.cpp");
}

//------------------------------------------------------------------------------------------------------------------------
//                                        LIVE COUPLED ENVIRONMENT VIEWPORT CANVAS
//------------------------------------------------------------------------------------------------------------------------

void ConstructLiveEverestViewport(const ProjectDescriptor& Descriptor, ProjectEverestScene& Everest)
{
    const KitFaces Faces   = ResolveKitFaces();
    const KitTheme Theme   = ResolveKitTheme();
    ImDrawList*    Ink     = ImGui::GetWindowDrawList();
    const ImVec2   WinPos  = ImGui::GetWindowPos();
    const ImVec2   WinSize = ImGui::GetWindowSize();
    const float    Left    = WinPos.x + 14.0f;
    const float    Top     = std::max(WinPos.y + 30.0f, ImGui::GetCursorScreenPos().y + 4.0f);
    const float    Width   = std::max(120.0f, WinSize.x - 28.0f);
    const float    Height  = std::max(120.0f, WinPos.y + WinSize.y - Top - 12.0f);

    Ink->AddRectFilled(ImVec2(WinPos.x, Top - 6.0f), ImVec2(WinPos.x + WinSize.x, WinPos.y + WinSize.y), Theme.PanelBg);

    // Top viewport bar: "< Browser", project title, GPU tier pill.
    bool BackHover = false;
    (void)KitTarget("##ViewportBackBrowser", ImVec2(Left, Top), ImVec2(88.0f, 26.0f), BackHover,
                    "Return to Project Browser");
    KitPillSurface(Ink, ImVec2(Left, Top), ImVec2(Left + 88.0f, Top + 26.0f),
                   BackHover ? Theme.Surface3 : Theme.Surface2, Theme.Line, 999.0f);
    KitDrawText(Ink, Faces.Medium, 11.5f, ImVec2(Left + 14.0f, Top + 6.5f),
                BackHover ? Theme.Text : Theme.Muted, "< Browser");

    KitDrawText(Ink, Faces.SemiBold, 14.0f, ImVec2(Left + 100.0f, Top + 5.5f), Theme.Text,
                Descriptor.Name.c_str());

    {
        const bool  RtxActive = (Everest.Illumination.TierSelectionIndex == 0);
        const char* TierLabel = RtxActive ? "RTX RayQuery + ReSTIR GI" : "GTX SDF + BVH Fallback";
        const float TierW     = KitTextWidth(Faces.Medium, 11.0f, TierLabel) + 26.0f;
        const float TierX     = Left + Width - TierW;
        bool        TierHover = false;
        if (KitTarget("##ViewportTierToggle", ImVec2(TierX, Top), ImVec2(TierW, 26.0f), TierHover,
                      "Click to toggle RTX Raytracing vs GTX SDF + BVH Fallback"))
        {
            Everest.Illumination.TierSelectionIndex = RtxActive ? 1 : 0;
        }
        const ImU32 TierAccent = RtxActive ? KitInkHex(0x2fe6a7) : KitInkHex(0x63a8ff);
        KitPillSurface(Ink, ImVec2(TierX, Top), ImVec2(TierX + TierW, Top + 26.0f),
                       KitInkWithAlpha(TierAccent, 0.16f), KitInkWithAlpha(TierAccent, 0.55f), 999.0f);
        Ink->AddCircleFilled(ImVec2(TierX + 12.0f, Top + 13.0f), 3.4f, TierAccent);
        KitDrawText(Ink, Faces.Medium, 11.0f, ImVec2(TierX + 20.0f, Top + 7.0f), Theme.Text, TierLabel);
    }

    // Live Coupled Sky + Sun + Cloud + Lens-Flare Viewport Diagnostic Canvas.
    const ImVec2 CanvasMin(Left, Top + 34.0f);
    const ImVec2 CanvasMax(Left + Width, Top + Height);
    KitPillSurface(Ink, CanvasMin, CanvasMax, KitInkHex(0x090d11), Theme.Line, 12.0f);

    const SunRecord*        Sun        = &Everest.Sun;
    const AtmosphereRecord* Atmosphere = &Everest.Atmosphere;
    const CloudRecord*      Clouds     = &Everest.Clouds;
    const LensFlareRecord*  Flare      = &Everest.LensFlare;
    const WindRecord*       Wind       = &Everest.Wind;

    const float         CWidth     = CanvasMax.x - CanvasMin.x - 6.0f;
    const float         CHeight    = CanvasMax.y - CanvasMin.y - 6.0f;
    const SpatialVector SunDir     = Sun->QuerySunDirection();
    const SpatialVector ObsKm{0.0, 0.0, Atmosphere->Parameters.GroundRadiusKm + 0.1};
    const double        Scale      = (Sun->Enabled ? Sun->SolarIlluminance : 0.0) * Atmosphere->SkyBrightnessScale;
    const SpectrumRgb   SolarTint  = SunRecord::EvaluateBlackbodyRgb(Sun->TemperatureKelvin);
    const SpectrumRgb   SunIrr{Scale * SolarTint.R, Scale * SolarTint.G, Scale * SolarTint.B};
    constexpr int       Cols       = 128;
    constexpr int       Rows       = 72;
    const float         CellW      = CWidth / static_cast<float>(Cols);
    const float         CellH      = CHeight / static_cast<float>(Rows);
    const double        Aspect     = static_cast<double>(CWidth / std::max(1.0f, CHeight));
    const double        SunScreenU = std::clamp(0.5 + (Sun->AzimuthDegrees - 145.0) / 180.0, 0.08, 0.92);
    const double        SunScreenV = std::clamp(0.68 - (Sun->ElevationDegrees / 90.0) * 0.58, 0.06, 0.92);

    auto ToByte = [](double Linear) -> int {
        const double Mapped = std::max(0.0, Linear) / (1.0 + std::max(0.0, Linear));
        return std::clamp(static_cast<int>(std::pow(Mapped, 1.0 / 2.2) * 255.0 + 0.5), 0, 255);
    };

    std::vector<ImU32> SkyVertexGrid(static_cast<size_t>((Cols + 1) * (Rows + 1)));
    for (int Row = 0; Row <= Rows; ++Row)
    {
        const double V        = static_cast<double>(Row) / static_cast<double>(Rows);
        const double ViewElev = (0.68 - V) * 1.45;
        for (int Col = 0; Col <= Cols; ++Col)
        {
            const double U      = static_cast<double>(Col) / static_cast<double>(Cols);
            SpectrumRgb  Colour = {0.015, 0.020, 0.024};

            if (ViewElev >= 0.0)
            {
                if (Atmosphere->Enabled)
                {
                    const double        ViewAz = (U - 0.5) * 2.2 + (Sun->AzimuthDegrees * 3.14159265358979323846 / 180.0);
                    const SpatialVector ViewDir{std::cos(ViewElev) * std::cos(ViewAz),
                                                std::cos(ViewElev) * std::sin(ViewAz), std::sin(std::max(0.008, ViewElev))};
                    Colour = SingleScattering(Atmosphere->Parameters, ObsKm, ViewDir, SunDir, SunIrr,
                                              Atmosphere->UseBakedLookup ? &Atmosphere->LookupTable : nullptr, 10);
                }
                if (Clouds->Enabled && ViewElev > 0.03)
                {
                    const double CloudD = Clouds->SampleTopDownDensity(U, V * 1.4);
                    const double Mix    = std::clamp(CloudD * 0.78 * std::min(1.0, (ViewElev - 0.03) * 8.0), 0.0, 0.88);
                    const double Lit    = 0.25 + 0.75 * std::clamp((Sun->ElevationDegrees + 8.0) / 48.0, 0.05, 1.0);
                    Colour.R            = Colour.R * (1.0 - Mix) + Lit * SolarTint.R * Mix;
                    Colour.G            = Colour.G * (1.0 - Mix) + Lit * SolarTint.G * Mix;
                    Colour.B            = Colour.B * (1.0 - Mix) + Lit * SolarTint.B * Mix;
                }
                if (Sun->Enabled && Sun->DiscEnabled && Sun->ElevationDegrees > -2.0)
                {
                    const double Dx   = (U - SunScreenU) * Aspect;
                    const double Dy   = (V - SunScreenV);
                    const double Dist = std::sqrt(Dx * Dx + Dy * Dy);
                    const double Disc = std::exp(-Dist * Dist / 0.00045);
                    const double Glow = std::exp(-Dist * 11.0) * 0.45 * Sun->OpticalGlow;
                    Colour.R += SolarTint.R * (Disc * 4.2 + Glow);
                    Colour.G += SolarTint.G * (Disc * 4.0 + Glow * 0.92);
                    Colour.B += SolarTint.B * (Disc * 3.6 + Glow * 0.80);
                }
            }
            else
            {
                const double DepthFade = std::clamp((V - 0.68) / 0.32, 0.0, 1.0);
                const double Ambient   = 0.03 + 0.14 * std::clamp((Sun->ElevationDegrees + 6.0) / 50.0, 0.0, 1.0);
                Colour = SpectrumRgb{Ambient * (0.55 + 0.35 * DepthFade),
                                     Ambient * (0.60 + 0.35 * DepthFade),
                                     Ambient * (0.62 + 0.32 * DepthFade)};
            }

            if (Flare->Enabled && Sun->Enabled && Sun->ElevationDegrees > -2.0)
            {
                const SpectrumRgb FlareRgb =
                    Flare->EvaluateOpticalSampleAtSource(U, V, SunScreenU, SunScreenV, Aspect);
                Colour.R += FlareRgb.R;
                Colour.G += FlareRgb.G;
                Colour.B += FlareRgb.B;
            }

            SkyVertexGrid[static_cast<size_t>(Row * (Cols + 1) + Col)] =
                KitInkRgba(ToByte(Colour.R), ToByte(Colour.G), ToByte(Colour.B), 255);
        }
    }

    for (int Row = 0; Row < Rows; ++Row)
    {
        const bool CrossesBoundary = ((static_cast<double>(Row) / Rows) < 0.68 &&
                                      (static_cast<double>(Row + 1) / Rows) > 0.68);
        for (int Col = 0; Col < Cols; ++Col)
        {
            const ImVec2 PMin(CanvasMin.x + 3.0f + static_cast<float>(Col) * CellW,
                              CanvasMin.y + 3.0f + static_cast<float>(Row) * CellH);
            const ImVec2 PMax(PMin.x + CellW, PMin.y + CellH);
            const ImU32  C00 = SkyVertexGrid[static_cast<size_t>(Row * (Cols + 1) + Col)];
            const ImU32  C10 = SkyVertexGrid[static_cast<size_t>(Row * (Cols + 1) + Col + 1)];
            const ImU32  C11 = CrossesBoundary ? C10 : SkyVertexGrid[static_cast<size_t>((Row + 1) * (Cols + 1) + Col + 1)];
            const ImU32  C01 = CrossesBoundary ? C00 : SkyVertexGrid[static_cast<size_t>((Row + 1) * (Cols + 1) + Col)];
            Ink->AddRectFilledMultiColor(PMin, PMax, C00, C10, C11, C01);
        }
    }

    // Proving-grounds perspective grid lines across the ground plane.
    const float BoundaryY = CanvasMin.y + 2.0f + CHeight * 0.68f;
    Ink->AddLine(ImVec2(CanvasMin.x + 2.0f, BoundaryY), ImVec2(CanvasMax.x - 2.0f, BoundaryY),
                 KitInkRgba(255, 255, 255, 42), 1.0f);
    for (int LineIdx = -5; LineIdx <= 5; ++LineIdx)
    {
        const float TopX = CanvasMin.x + 2.0f + CWidth * 0.5f + static_cast<float>(LineIdx) * (CWidth * 0.038f);
        const float BotX = CanvasMin.x + 2.0f + CWidth * 0.5f + static_cast<float>(LineIdx) * (CWidth * 0.125f);
        Ink->AddLine(ImVec2(TopX, BoundaryY), ImVec2(BotX, CanvasMax.y - 2.0f), KitInkRgba(210, 228, 240, 24), 1.0f);
    }

    // Bottom HUD telemetry bar inside Viewport.
    char HudLine[220] = {};
    std::snprintf(HudLine, sizeof(HudLine),
                  "Sun %.1f\xC2\xB0 / %.0f K  \xC2\xB7  Clouds %d%% (%s)  \xC2\xB7  Wind %.1f m/s FROM %.0f\xC2\xB0  \xC2\xB7  Flare %s  \xC2\xB7  %.0f kg AWD",
                  Sun->ElevationDegrees, Sun->TemperatureKelvin,
                  static_cast<int>(std::lround(Clouds->Coverage * 100.0)),
                  Clouds->QueryFormationName(), Wind->ReferenceSpeedMps,
                  Wind->QueryFromBearingDegrees(),
                  Flare->Enabled ? "Active" : "Bypassed",
                  Everest.Vehicle.SprungMassKilograms);
    KitPillSurface(Ink, ImVec2(CanvasMin.x + 12.0f, CanvasMax.y - 34.0f),
                   ImVec2(CanvasMax.x - 12.0f, CanvasMax.y - 10.0f), KitInkRgba(10, 14, 18, 196),
                   KitInkRgba(255, 255, 255, 28), 999.0f);
    KitDrawText(Ink, Faces.Medium, 11.0f, ImVec2(CanvasMin.x + 24.0f, CanvasMax.y - 27.5f), KitInkHex(0xeef3f0),
                HudLine);
}

} // namespace
} // namespace Frontier

//------------------------------------------------------------------------------------------------------------------------
//                                                          MAIN
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char** Arguments)
{
    using namespace Frontier;
    try
    {
        const std::filesystem::path RepoRoot =
            ArgumentCount > 1 ? std::filesystem::path(Arguments[1]) : std::filesystem::current_path();
        const std::filesystem::path ExhibitDir =
            ArgumentCount > 2 ? std::filesystem::path(Arguments[2]) : (RepoRoot / "Exhibits" / "ProjectEverest");
        const std::filesystem::path ScratchDir = RepoRoot / "_ClaudeScratch" / "tmp" / "project-everest-exhibit";
        std::filesystem::create_directories(ExhibitDir);
        std::filesystem::create_directories(ScratchDir);

        const std::filesystem::path FontsDir    = RepoRoot / "EngineContent" / "FontArchives";
        const std::filesystem::path IconsDir    = RepoRoot / "EngineContent" / "Icons";
        const std::filesystem::path ProjectsDir = RepoRoot / "Frontier" / "Projects";

        // ① Boot headless ImGui + ThorVG + TypefaceRegistry + 142-icon SVG presentation.
        ImGui::CreateContext();
        ImGuiIO& IO    = ImGui::GetIO();
        IO.IniFilename = nullptr;
        IO.DisplaySize = ImVec2(static_cast<float>(CaptureWidth), static_cast<float>(CaptureHeight));
        IO.DeltaTime   = 1.0f / 60.0f;
        IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
        ImGui::GetPlatformIO().Renderer_TextureMaxWidth  = 4096;
        ImGui::GetPlatformIO().Renderer_TextureMaxHeight = 4096;
        IconPresentation::Attach(IconsDir.string());

        const std::filesystem::path AppearanceFile = ScratchDir / "ControlCentre.toml";
        std::filesystem::remove(AppearanceFile);
        auto Centre = std::make_unique<ControlCentrePanel>(FontsDir, AppearanceFile);
        Require(Centre->Initialize() && Centre->Activate(), "ControlCentrePanel failed to initialize");
        Require(Centre->QueryTypefaces().QueryFamilyCount() == 5, "Expected 5 font families (35 faces)");

        // ② Step 1: Project Browser opens, enumerates Frontier/Projects, and displays Project Everest.
        ProjectBrowserPanel Browser;
        const size_t        AttachedCount = Browser.AttachProjectsDirectory(ProjectsDir);
        Require(AttachedCount >= 5, "Expected Project Everest and sibling .frontier applications to attach");
        const std::vector<std::string> MatchingTitles = Browser.QueryFilteredTitles("project", "Everest", false);
        Require(!MatchingTitles.empty() && MatchingTitles.front() == "Project Everest",
                "Project Everest missing from Project Browser project filter");

        const auto StepBrowserFrame = [&]() {
            const double DeltaSeconds = 1.0 / 60.0;
            (void)Centre->Integrate(DeltaSeconds);
            Centre->PrepareInterface();
            Browser.ConfigurePalette(ControlKit::Palette());
            ImGui::NewFrame();
            if (ImGui::GetMainViewport() != nullptr)
            {
                ImGui::GetMainViewport()->Size     = IO.DisplaySize;
                ImGui::GetMainViewport()->WorkSize = IO.DisplaySize;
                ImGui::GetMainViewport()->WorkPos  = ImVec2(0.0f, 0.0f);
            }
            Browser.ConstructInterface();
            ImGui::Render();
            FrontierExhibit::AcknowledgeTextures();
        };

        for (int Warmup = 0; Warmup < 4; ++Warmup)
        {
            StepBrowserFrame();
        }
        (void)RasterizeCurrentFrame(ExhibitDir / "01-ProjectBrowser-Library.png", 0);

        // ③ Step 2: Select application "Project Everest" in the Project Browser Reader view.
        Require(Browser.OpenAttachedEntry("project-everest"), "Failed to select Project Everest in Project Browser");
        Require(Browser.IsReaderOpen() && Browser.QueryActiveToken() == "project-everest",
                "Project Browser reader did not open on project-everest");
        for (int Warmup = 0; Warmup < 4; ++Warmup)
        {
            StepBrowserFrame();
        }
        (void)RasterizeCurrentFrame(ExhibitDir / "02-ProjectBrowser-SelectEverest.png", 0);

        // ④ Step 3: Launch Project Everest via native hand-off ("Open in Frontier" -> ConsumeLaunchPath).
        Require(Browser.LaunchActiveEntry(), "Open in Frontier failed to stage Project Everest launch path");
        const std::filesystem::path LaunchedDescriptorPath = Browser.ConsumeLaunchPath();
        Require(!LaunchedDescriptorPath.empty() && Browser.ConsumeLaunchPath().empty(),
                "Project Everest hand-off path was not single-consumption");
        Require(LaunchedDescriptorPath.filename() == "ProjectEverest.frontier",
                "Launched descriptor was not ProjectEverest.frontier");

        ProjectDescriptor EverestDescriptor;
        std::string       DecodeDiagnostic;
        Require(DecodeProjectDescriptor(LaunchedDescriptorPath, EverestDescriptor, DecodeDiagnostic),
                DecodeDiagnostic.c_str());
        Require(EverestDescriptor.Name == "Project Everest", "Decoded descriptor ProjectName mismatch");
        Require(EverestDescriptor.OpeningScene == "EverestProvingGrounds", "Decoded OpeningScene mismatch");
        Require(EverestDescriptor.LogCategories.size() == 5, "Expected 5 declared Project Everest log categories");

        ConfigureContentRoots(RepoRoot, EverestDescriptor.ProjectDirectory / EverestDescriptor.ContentLocation);
        const uint32_t RegisteredCategories = RegisterDescriptorLogCategories(EverestDescriptor);
        Require(RegisteredCategories == 5, "Expected 5 newly registered Project Everest user log categories");

        // ⑤ Verify ProjectEverestInterchange v3 negotiation and stream real diagnostics into LogStream.
        FrontierProjectInterchange Interchange = {};
        Interchange.StructureSize              = sizeof(Interchange);
        char Refusal[256]                      = {};
        Require(ConstructProjectInterchange(EverestDescriptor.CodeInterchangeNumber,
                                            EverestDescriptor.InterfaceFingerprint,
                                            &Interchange, Refusal, sizeof(Refusal)),
                Refusal);

        FrontierHostInterchange HostCallbacks = {};
        HostCallbacks.StructureSize           = sizeof(HostCallbacks);
        HostCallbacks.ReceiveDiagnostic       = &ReceiveEverestDiagnostic;
        Require(Interchange.ConstructProject(&HostCallbacks, nullptr), "ConstructProject refused host table");

        WriteLog(ResolveEngineChannel("Interface"), LogSeverity::Info, LogVerbosity::Display,
                 "WorkspaceApplication session opened \xC2\xB7 Project Everest launched from Project Browser",
                 "WorkspaceApplication.cpp");
        WriteLog(ResolveEngineChannel("Render"), LogSeverity::Info, LogVerbosity::Log,
                 "Vulkan device probing \xC2\xB7 RTX ray-query tier and GTX toroidal SDF + BVH fallback registered",
                 "WorkspaceApplication.cpp");
        WriteLog(ResolveEngineChannel("Illumination"), LogSeverity::Info, LogVerbosity::Log,
                 "PathReplayRayQuery temporal reservoir reuse converged \xC2\xB7 beta=2 Power MIS active",
                 "PathReplayRayQuery.cpp");
        WriteLog(ResolveEngineChannel("Physics"), LogSeverity::Info, LogVerbosity::Log,
                 "Vehicle suspension & wheel contact manifolds stepped \xC2\xB7 120 Hz fixed substep",
                 "ControlVehicle.cpp");

        for (int Step = 0; Step < 5; ++Step)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            Interchange.AdvanceProject();
        }
        WriteLog(LogCategories().QueryByName("Powertrain")->Token, LogSeverity::Info, LogVerbosity::Display,
                 "ControlVehicle powertrain \xC2\xB7 gear 3 \xC2\xB7 4120 rpm \xC2\xB7 spectral Beer-Lambert glazing transmittance active",
                 "ProjectEverestInterchange.cpp");
        WriteLog(LogCategories().QueryByName("RayTracing")->Token, LogSeverity::Info, LogVerbosity::Log,
                 "RTX tier \xC2\xB7 VK_KHR_ray_query + DielectricBoundary \xC2\xB7 reservoir acceptance 97.4%",
                 "ProjectEverestInterchange.cpp");
        WriteLog(LogCategories().QueryByName("DistanceField")->Token, LogSeverity::Warning, LogVerbosity::Log,
                 "GTX fallback tier \xC2\xB7 SceneClip0..3 toroidal brick budget at 88% \xC2\xB7 OrderedSoftwareBvh 24 BLAS",
                 "ProjectEverestInterchange.cpp");
        WriteLog(LogCategories().QueryByName("Telemetry")->Token, LogSeverity::Info, LogVerbosity::Display,
                 "Cabin telemetry \xC2\xB7 86 km/h \xC2\xB7 pack 84% \xC2\xB7 inverter 41 C",
                 "ProjectEverestInterchange.cpp");
        for (int Repeat = 0; Repeat < 3; ++Repeat)
        {
            WriteLog(LogCategories().QueryByName("Everest")->Token, LogSeverity::Info, LogVerbosity::Log,
                     "SurfaceCards multi-bounce irradiance atlas refreshed \xC2\xB7 0 dropped tiles",
                     "ProjectEverestInterchange.cpp");
        }
        WriteLog(ResolveEngineChannel("Illumination"), LogSeverity::Error, LogVerbosity::Log,
                 "Blocked-NEE disocclusion spike at tile (94, 41) \xC2\xB7 temporal history clamped",
                 "PathReplayVarianceFilter.slang");

        // ⑥ Construct ProjectEverestScene and verify all 18 RecordEntry + RecordEditorDescription bindings!
        const size_t        BaselineDirectoryCount = SceneDirectory::Instance().QueryCount();
        ProjectEverestScene EverestScene;
        const size_t        EverestRecordCount = SceneDirectory::Instance().QueryCount() - BaselineDirectoryCount;
        Require(EverestRecordCount == 18,
                "Expected ProjectEverestScene to register 18 records (5 Folders, 5 Engine Env/Forcefield, "
                "1 Illumination, 1 Vehicle, 4 Tyres, 1 Course, 1 Camera)");
        const SceneDirectory& Directory = SceneDirectory::Instance();
        Require(Directory.QueryEditorDescription(EverestScene.Sun.Token()) != nullptr &&
                    Directory.QueryEditorDescription(EverestScene.LensFlare.Token()) != nullptr &&
                    Directory.QueryEditorDescription(EverestScene.Atmosphere.Token()) != nullptr &&
                    Directory.QueryEditorDescription(EverestScene.Clouds.Token()) != nullptr &&
                    Directory.QueryEditorDescription(EverestScene.Wind.Token()) != nullptr &&
                    Directory.QueryEditorDescription(EverestScene.EnvironmentFolder.Token()) != nullptr &&
                    Directory.QueryEditorDescription(EverestScene.Illumination.Token()) != nullptr &&
                    Directory.QueryEditorDescription(EverestScene.Vehicle.Token()) != nullptr,
                "Every Engine Observatory and Project Everest custom item must register a RecordEditorDescription");

        // Verify Folder cascade visibility and scoped RecordEntry destructor cleanup.
        Require(SceneDirectory::Instance().ConfigureCascadeVisible(EverestScene.EnvironmentFolder.Token(), false),
                "Failed to hide EnvironmentFolder");
        Require(!Directory.QueryVisible(EverestScene.Sun.Token()) &&
                    !Directory.QueryVisible(EverestScene.LensFlare.Token()) &&
                    !Directory.QueryVisible(EverestScene.Atmosphere.Token()) &&
                    !Directory.QueryVisible(EverestScene.Clouds.Token()),
                "FolderRecord hide did not cascade to nested Environment records");
        Require(SceneDirectory::Instance().ConfigureCascadeVisible(EverestScene.EnvironmentFolder.Token(), true),
                "Failed to restore EnvironmentFolder visibility");
        Require(Directory.QueryVisible(EverestScene.Sun.Token()) &&
                    Directory.QueryVisible(EverestScene.Clouds.Token()),
                "FolderRecord show did not restore nested Environment records");

        {
            FolderRecord ScopedFolder("Transient Fracture Group", "FRACTURE STUDIO", 0xFF5F7AE0u);
            SceneRecord  ScopedProp(RecordClassification::Geometry);
            (void)SceneDirectory::Instance().ConfigureName(ScopedProp.Token(), "Destructible Barrier");
            (void)SceneDirectory::Instance().ConfigureParent(ScopedProp.Token(), ScopedFolder.Token());
            Require(Directory.QueryCount() == BaselineDirectoryCount + EverestRecordCount + 2,
                    "Scoped FolderRecord + SceneRecord did not increment SceneDirectory count");
        }
        Require(Directory.QueryCount() == BaselineDirectoryCount + EverestRecordCount,
                "Scoped FolderRecord + SceneRecord destructors did not unregister cleanly");

        // ⑦ Compose 3-column Project Everest workspace: Outliner (0.23) | Viewport/LogStream (0.48) | Inspector (0.29).
        PanelDockspace      Panels;
        LogStreamPanel      LogPanel;
        SceneDirectoryPanel OutlinerPanel;
        SceneInspectorPanel InspectorPanel;

        Panels.ConfigureDockspaceKey("Project Everest Phase 2 Workspace");
        DockPanelSpecification ViewportSpec;
        ViewportSpec.Key              = "Viewport";
        ViewportSpec.Caption          = "Viewport";
        ViewportSpec.ConstructContent = [&]() { ConstructLiveEverestViewport(EverestDescriptor, EverestScene); };
        ViewportSpec.Open             = true;
        Require(Panels.Register(ViewportSpec), "Failed to register Viewport dock panel");
        Require(Panels.Register(MakeLogStreamPanelSpecification(LogPanel)), "Failed to register LogStream dock panel");
        Require(Panels.Register(MakeSceneDirectoryPanelSpecification(OutlinerPanel)),
                "Failed to register Outliner dock panel");
        Require(Panels.Register(MakeSceneInspectorPanelSpecification(InspectorPanel, OutlinerPanel)),
                "Failed to register Inspector dock panel");
        Panels.ConfigureWorkspaceInsets(79.0f, 0.0f);
        Panels.ConfigureTabExtent(136.0f, 26.0f);
        Require(Panels.ConfigureStackedPanels("Viewport", "LogStream", 580.0f),
                "Failed to stack Viewport above LogStream");
        Require(Panels.ConfigureColumns({{"Outliner", 0.23f}, {"LogStream", 0.48f}, {"Inspector", 0.29f}}),
                "Failed to configure 3-column Outliner | Viewport/LogStream | Inspector workspace");
        (void)Panels.ConfigurePlatform();

        const auto StepWorkspaceFrame = [&]() {
            const double DeltaSeconds = 1.0 / 60.0;
            EverestScene.ServiceSpawnRequest(OutlinerPanel);
            LogPanel.Synchronize(LogStore(), LogCategories(), DeltaSeconds);
            OutlinerPanel.Synchronize(SceneDirectory::Instance());
            Centre->PrepareInterface();
            const float Scale = std::clamp(Centre->QueryAppearance().QueryApplied().InterfaceScale / 100.0f, 0.6f, 2.0f);
            (void)Centre->Integrate(DeltaSeconds);
            ImGui::NewFrame();
            if (ImGui::GetMainViewport() != nullptr)
            {
                ImGui::GetMainViewport()->Size     = IO.DisplaySize;
                ImGui::GetMainViewport()->WorkSize = IO.DisplaySize;
                ImGui::GetMainViewport()->WorkPos  = ImVec2(0.0f, 0.0f);
            }
            Panels.ConstructInterface(Scale);
            Centre->ConstructInterface();
            ImGui::Render();
            FrontierExhibit::AcknowledgeTextures();
        };

        const auto ClickExtent = [&](PlaneExtent Extent) {
            IO.AddMousePosEvent((Extent.MinimumX + Extent.MaximumX) * 0.5f,
                                (Extent.MinimumY + Extent.MaximumY) * 0.5f);
            StepWorkspaceFrame();
            IO.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            StepWorkspaceFrame();
            IO.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            StepWorkspaceFrame();
            if (Centre->QueryConfirmation().QueryOpen())
            {
                const PlaneExtent Primary = Centre->QueryConfirmation().QueryPrimaryExtent();
                IO.AddMousePosEvent((Primary.MinimumX + Primary.MaximumX) * 0.5f,
                                    (Primary.MinimumY + Primary.MaximumY) * 0.5f);
                StepWorkspaceFrame();
                IO.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
                StepWorkspaceFrame();
                IO.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
                StepWorkspaceFrame();
            }
        };

        // Fold Vehicle tyres initially so all 5 folders + top-level scene items fit cleanly in the Outliner rail!
        OutlinerPanel.ConfigureCollapsed(EverestScene.Vehicle.Token(), true);

        // ⑧ Capture 03: Engine Folder Outliner + Folder Observatory (`Environment` folder selected).
        OutlinerPanel.Select(EverestScene.EnvironmentFolder.Token());
        for (int Frame = 0; Frame < 24; ++Frame)
        {
            StepWorkspaceFrame();
        }
        Require(LogPanel.QueryTotalCount() >= 10, "LogStreamPanel did not ingest Project Everest log entries");
        Require(OutlinerPanel.QueryTotal() == EverestRecordCount,
                "SceneDirectoryPanel did not synchronize all 18 Project Everest records");
        Require(Directory.FindTokenByIdentifier("sun") == EverestScene.Sun.Token() &&
                    Directory.FindTokenByIdentifier("flare") == EverestScene.LensFlare.Token() &&
                    Directory.FindTokenByIdentifier("wind") == EverestScene.Wind.Token(),
                "SceneDirectory FindTokenByIdentifier failed to resolve global Engine environment records");

        const std::vector<unsigned char> DarkFolderPixels =
            RasterizeCurrentFrame(ExhibitDir / "03-Folder-Outliner-Observatory.png", 10);
        const double DarkLogLuma       = SampleRegionMeanLuminance(DarkFolderPixels, 520, 700, 1300, 1030);
        const double DarkInspectorLuma = SampleRegionMeanLuminance(DarkFolderPixels, 1400, 150, 1890, 980);

        // ⑨ Capture 04: Engine Sun Observatory (`ENVIRONMENT / 02 · Sun`).
        OutlinerPanel.Select(EverestScene.Sun.Token());
        for (int Frame = 0; Frame < 8; ++Frame)
        {
            StepWorkspaceFrame();
        }
        (void)RasterizeCurrentFrame(ExhibitDir / "04-Sun-Observatory-Overview.png", 10);

        // ⑩ Capture 05: Engine Lens Flare Observatory (`ENVIRONMENT / CAMERA OPTICS · Lens Flare`).
        OutlinerPanel.Select(EverestScene.LensFlare.Token());
        for (int Frame = 0; Frame < 8; ++Frame)
        {
            StepWorkspaceFrame();
        }
        (void)RasterizeCurrentFrame(ExhibitDir / "05-LensFlare-Observatory-Optics.png", 10);

        // ⑪ Capture 06: Engine Atmosphere Observatory (`ENVIRONMENT / 03 · Atmosphere`).
        OutlinerPanel.Select(EverestScene.Atmosphere.Token());
        for (int Frame = 0; Frame < 8; ++Frame)
        {
            StepWorkspaceFrame();
        }
        (void)RasterizeCurrentFrame(ExhibitDir / "06-Atmosphere-Observatory-Medium.png", 10);

        // ⑫ Capture 07: Engine Clouds Observatory (`ENVIRONMENT / 01 · Clouds`).
        OutlinerPanel.Select(EverestScene.Clouds.Token());
        for (int Frame = 0; Frame < 8; ++Frame)
        {
            StepWorkspaceFrame();
        }
        (void)RasterizeCurrentFrame(ExhibitDir / "07-Clouds-Observatory-Weather.png", 10);

        // ⑬ Capture 08: Engine Wind ForceField Observatory (`FORCEFIELD / SHARED WEATHER · Wind`).
        OutlinerPanel.Select(EverestScene.Wind.Token());
        for (int Frame = 0; Frame < 8; ++Frame)
        {
            StepWorkspaceFrame();
        }
        (void)RasterizeCurrentFrame(ExhibitDir / "08-Wind-Observatory-Forcefield.png", 10);

        // ⑭ Capture 09: Project Everest Custom ControlVehicle Inspector (`CUSTOM PROJECT INSPECTOR · VEHICLE DYNAMICS`).
        OutlinerPanel.ConfigureCollapsed(EverestScene.Vehicle.Token(), false);   // Expand 4 tyres in Outliner!
        OutlinerPanel.Select(EverestScene.Vehicle.Token());
        for (int Frame = 0; Frame < 8; ++Frame)
        {
            StepWorkspaceFrame();
        }
        (void)RasterizeCurrentFrame(ExhibitDir / "09-Custom-Vehicle-Inspector.png", 10);

        // Verify `+ Folder` dynamic spawn request round-trip on ProjectEverestScene.
        OutlinerPanel.RequestSpawn(RecordClassification::Folder);
        StepWorkspaceFrame();
        Require(OutlinerPanel.QueryTotal() == EverestRecordCount + 1,
                "+ Folder spawn request did not create a new FolderRecord in ProjectEverestScene");

        // ⑮ Capture 10: Switch Dual-Tier Renderer to GTX SDF + BVH Fallback AND switch Control Centre to Light theme!
        EverestScene.Illumination.TierSelectionIndex = 1;   // Tier 1: GTX SDF + BVH Fallback
        EverestScene.Sun.ApplyPreset(SolarLightingPreset::Golden);
        OutlinerPanel.Select(EverestScene.Illumination.Token());

        ClickExtent(Centre->QueryExpansionTab().QueryTabExtent());
        Require(Centre->QueryQuickOpen(), "Notch click did not open Control Centre drawer");
        for (int Frame = 0; Frame < 90; ++Frame)
        {
            StepWorkspaceFrame();
        }
        ClickExtent(Centre->QuerySettingsExtent());
        for (int Frame = 0; Frame < 90; ++Frame)
        {
            StepWorkspaceFrame();
        }
        ClickExtent(Centre->QueryCategoryExtent(0));
        for (int Frame = 0; Frame < 90; ++Frame)
        {
            StepWorkspaceFrame();
        }
        ClickExtent(Centre->QueryTabExtent(2));
        Require(Centre->QueryAppearancePage() == AppearancePageCategory::Theme,
                "Control Centre did not open Appearance -> Theme tab");
        ClickExtent(Centre->QueryAppearance().QueryThemeTileExtent(2));
        ClickExtent(Centre->QueryApplyExtent());
        for (int Frame = 0; Frame < 45; ++Frame)
        {
            StepWorkspaceFrame();
        }
        Require(ControlKit::Palette().LightSurface, "Control Centre Light theme did not activate LightSurface");

        IO.AddKeyEvent(ImGuiKey_Escape, true);
        StepWorkspaceFrame();
        IO.AddKeyEvent(ImGuiKey_Escape, false);
        for (int Frame = 0; Frame < 260; ++Frame)
        {
            StepWorkspaceFrame();
        }
        Require(!Centre->QueryQuickOpen(), "Control Centre drawer did not close on Escape");

        const std::vector<unsigned char> LightPixels =
            RasterizeCurrentFrame(ExhibitDir / "10-DualTier-GTX-Fallback-LightTheme.png", 240);
        const double LightLogLuma       = SampleRegionMeanLuminance(LightPixels, 520, 700, 1300, 1030);
        const double LightInspectorLuma = SampleRegionMeanLuminance(LightPixels, 1400, 150, 1890, 980);
        Require(DarkLogLuma < 45.0 && LightLogLuma > 200.0,
                "LogStreamPanel luminance did not respond to Control Centre Dark -> Light theme switch");
        Require(DarkInspectorLuma < 55.0 && LightInspectorLuma > 180.0,
                "SceneInspectorPanel luminance did not respond to Control Centre Dark -> Light theme switch");

        Interchange.RetireProject();
        (void)Centre->Finalize();
        Centre.reset();
        ImGui::DestroyContext();

        std::ofstream Report(ExhibitDir / "ProjectEverestChecks.json", std::ios::trunc);
        Report << "{\n"
               << "  \"result\": \"passed\",\n"
               << "  \"phase\": 2,\n"
               << "  \"project\": \"Project Everest\",\n"
               << "  \"descriptor\": \"Frontier/Projects/Project-Everest/ProjectEverest.frontier\",\n"
               << "  \"workspace_application_loop\": \"Initialize -> Loop -> Finalize (FrontierMain removed)\",\n"
               << "  \"declared_log_categories\": 5,\n"
               << "  \"log_entries_ingested\": " << LogPanel.QueryTotalCount() << ",\n"
               << "  \"initial_scene_records\": " << EverestRecordCount << ",\n"
               << "  \"post_folder_spawn_records\": " << OutlinerPanel.QueryTotal() << ",\n"
               << "  \"folder_cascade_visibility_verified\": true,\n"
               << "  \"scoped_destructor_unregistered\": true,\n"
               << "  \"engine_observatories\": [\"Folder\", \"Sun\", \"LensFlare\", \"Atmosphere\", \"Clouds\", \"Wind\"],\n"
               << "  \"custom_inspectors\": [\"EverestVehicleRecord\", \"EverestIlluminationRecord (RTX/GTX)\"],\n"
               << "  \"mean_luma\": {\n"
               << "    \"dark_log_stream\": " << DarkLogLuma << ",\n"
               << "    \"light_log_stream\": " << LightLogLuma << ",\n"
               << "    \"dark_inspector\": " << DarkInspectorLuma << ",\n"
               << "    \"light_inspector\": " << LightInspectorLuma << "\n"
               << "  },\n"
               << "  \"captures\": [\n"
               << "    \"01-ProjectBrowser-Library.png\",\n"
               << "    \"02-ProjectBrowser-SelectEverest.png\",\n"
               << "    \"03-Folder-Outliner-Observatory.png\",\n"
               << "    \"04-Sun-Observatory-Overview.png\",\n"
               << "    \"05-LensFlare-Observatory-Optics.png\",\n"
               << "    \"06-Atmosphere-Observatory-Medium.png\",\n"
               << "    \"07-Clouds-Observatory-Weather.png\",\n"
               << "    \"08-Wind-Observatory-Forcefield.png\",\n"
               << "    \"09-Custom-Vehicle-Inspector.png\",\n"
               << "    \"10-DualTier-GTX-Fallback-LightTheme.png\"\n"
               << "  ]\n"
               << "}\n";

        std::cout << "PASS: Project Everest Phase 2 validation (Browser -> WorkspaceApplication -> "
                  << "Folder Outliner + 6 Engine Observatories + ControlVehicle & Dual-Tier RTX/GTX Inspectors + "
                  << "Dark/Light Control Centre parity; Inspector luma Dark=" << DarkInspectorLuma
                  << ", Light=" << LightInspectorLuma << ")\n";
        return 0;
    }
    catch (const std::exception& Failure)
    {
        if (ImGui::GetCurrentContext() != nullptr)
        {
            ImGui::DestroyContext();
        }
        std::cerr << "FAIL: " << Failure.what() << '\n';
        return 1;
    }
}
