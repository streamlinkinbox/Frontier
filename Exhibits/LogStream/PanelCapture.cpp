/*==========================================================================================================================================
                                                          LOG STREAM PANEL CAPTURE
                                     ------------------------------------------------------------------
                              Renders the production C++ panel with a CPU rasterizer so its real pixels can be
==========================================================================================================================================*/

// 🧩 Log Stream panel capture: renders the production C++ panel with a CPU rasterizer so its real pixels can be
//    inspected without a GPU or window. Every widget below is drawn by LogStreamPanelPresentation.cpp itself —
//    this harness only supplies what the Windows host supplies (an ImGui context, a font, a window frame standing
//    in for the dock node, and input events). The dockspace/notch chrome belongs to the host and is not faked here.

#include "Engine/Diagnostics/LogCategories.h"
#include "Engine/Diagnostics/LogStream.h"
#include "Engine/Interface/ControlKit.h"
#include "Engine/Interface/LogStreamPanel.h"
#include "Engine/Interface/TypefaceRegistry.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   CPU RASTERIZER
//------------------------------------------------------------------------------------------------------------------------

int CaptureWidth  = 1240; // [px] - Panel close-up at the prototype's own 1240x830; workspace widens to HD
int CaptureHeight = 830;  // [px]

struct Pixel
{
    uint8_t Red, Green, Blue, Alpha;
};

std::vector<Pixel> Framebuffer;

const unsigned char* AtlasPixels = nullptr;
int                  AtlasWidth  = 0;
int                  AtlasHeight = 0;

bool SampleWhiteTexel = false; // Commands bound to non-atlas textures (GPU thumbnails) render as flat colour

float SampleAtlas(float U, float V, int Channel)
{
    const float X = std::clamp(U * static_cast<float>(AtlasWidth) - 0.5f, 0.0f, static_cast<float>(AtlasWidth - 1));
    const float Y = std::clamp(V * static_cast<float>(AtlasHeight) - 0.5f, 0.0f, static_cast<float>(AtlasHeight - 1));
    const int   X0 = static_cast<int>(X);
    const int   Y0 = static_cast<int>(Y);
    const int   X1 = std::min(X0 + 1, AtlasWidth - 1);
    const int   Y1 = std::min(Y0 + 1, AtlasHeight - 1);
    const float Fx = X - static_cast<float>(X0);
    const float Fy = Y - static_cast<float>(Y0);
    const auto  At = [&](int Px, int Py) {
        return static_cast<float>(AtlasPixels[(static_cast<size_t>(Py) * AtlasWidth + Px) * 4 + Channel]) / 255.0f;
    };
    const float Top    = At(X0, Y0) + (At(X1, Y0) - At(X0, Y0)) * Fx;
    const float Bottom = At(X0, Y1) + (At(X1, Y1) - At(X0, Y1)) * Fx;
    return Top + (Bottom - Top) * Fy;
}

void RasterizeTriangle(const ImDrawVert& A, const ImDrawVert& B, const ImDrawVert& C, const ImVec4& Clip)
{
    const float Area = (B.pos.x - A.pos.x) * (C.pos.y - A.pos.y) - (B.pos.y - A.pos.y) * (C.pos.x - A.pos.x);
    if (Area == 0.0f)
    {
        return;
    }
    const int MinX = std::max({static_cast<int>(std::floor(std::min({A.pos.x, B.pos.x, C.pos.x}))),
                               static_cast<int>(Clip.x), 0});
    const int MinY = std::max({static_cast<int>(std::floor(std::min({A.pos.y, B.pos.y, C.pos.y}))),
                               static_cast<int>(Clip.y), 0});
    const int MaxX = std::min({static_cast<int>(std::ceil(std::max({A.pos.x, B.pos.x, C.pos.x}))),
                               static_cast<int>(Clip.z), CaptureWidth});
    const int MaxY = std::min({static_cast<int>(std::ceil(std::max({A.pos.y, B.pos.y, C.pos.y}))),
                               static_cast<int>(Clip.w), CaptureHeight});

    for (int Y = MinY; Y < MaxY; ++Y)
    {
        for (int X = MinX; X < MaxX; ++X)
        {
            const float Px = static_cast<float>(X) + 0.5f;
            const float Py = static_cast<float>(Y) + 0.5f;
            const float W0 = ((B.pos.x - Px) * (C.pos.y - Py) - (B.pos.y - Py) * (C.pos.x - Px)) / Area;
            const float W1 = ((C.pos.x - Px) * (A.pos.y - Py) - (C.pos.y - Py) * (A.pos.x - Px)) / Area;
            const float W2 = 1.0f - W0 - W1;
            if (W0 < 0.0f || W1 < 0.0f || W2 < 0.0f)
            {
                continue;
            }
            const float U = A.uv.x * W0 + B.uv.x * W1 + C.uv.x * W2;
            const float V = A.uv.y * W0 + B.uv.y * W1 + C.uv.y * W2;

            const auto Channel = [](ImU32 Colour, int Shift) {
                return static_cast<float>((Colour >> Shift) & 0xffu) / 255.0f;
            };
            const float Vr = Channel(A.col, IM_COL32_R_SHIFT) * W0 + Channel(B.col, IM_COL32_R_SHIFT) * W1 +
                             Channel(C.col, IM_COL32_R_SHIFT) * W2;
            const float Vg = Channel(A.col, IM_COL32_G_SHIFT) * W0 + Channel(B.col, IM_COL32_G_SHIFT) * W1 +
                             Channel(C.col, IM_COL32_G_SHIFT) * W2;
            const float Vb = Channel(A.col, IM_COL32_B_SHIFT) * W0 + Channel(B.col, IM_COL32_B_SHIFT) * W1 +
                             Channel(C.col, IM_COL32_B_SHIFT) * W2;
            const float Va = Channel(A.col, IM_COL32_A_SHIFT) * W0 + Channel(B.col, IM_COL32_A_SHIFT) * W1 +
                             Channel(C.col, IM_COL32_A_SHIFT) * W2;

            const float Sr = (SampleWhiteTexel ? 1.0f : SampleAtlas(U, V, 0)) * Vr;
            const float Sg = (SampleWhiteTexel ? 1.0f : SampleAtlas(U, V, 1)) * Vg;
            const float Sb = (SampleWhiteTexel ? 1.0f : SampleAtlas(U, V, 2)) * Vb;
            const float Sa = (SampleWhiteTexel ? 1.0f : SampleAtlas(U, V, 3)) * Va;

            Pixel&      Out = Framebuffer[static_cast<size_t>(Y) * CaptureWidth + X];
            const float Inv = 1.0f - Sa;
            Out.Red   = static_cast<uint8_t>(std::clamp(Sr * Sa + (Out.Red / 255.0f) * Inv, 0.0f, 1.0f) * 255.0f);
            Out.Green = static_cast<uint8_t>(std::clamp(Sg * Sa + (Out.Green / 255.0f) * Inv, 0.0f, 1.0f) * 255.0f);
            Out.Blue  = static_cast<uint8_t>(std::clamp(Sb * Sa + (Out.Blue / 255.0f) * Inv, 0.0f, 1.0f) * 255.0f);
            Out.Alpha = 255;
        }
    }
}

void RasterizeDrawData(const ImDrawData& Data)
{
    for (int ListIndex = 0; ListIndex < Data.CmdListsCount; ++ListIndex)
    {
        const ImDrawList* List = Data.CmdLists[ListIndex];
        for (int CmdIndex = 0; CmdIndex < List->CmdBuffer.Size; ++CmdIndex)
        {
            const ImDrawCmd& Cmd = List->CmdBuffer[CmdIndex];
            if (Cmd.UserCallback != nullptr)
            {
                continue;
            }
            SampleWhiteTexel = Cmd.TexRef._TexData == nullptr; // Atlas-owned commands carry texture data
            for (unsigned Element = 0; Element + 2 < Cmd.ElemCount; Element += 3)
            {
                const ImDrawIdx I0 = List->IdxBuffer[static_cast<int>(Cmd.IdxOffset + Element + 0)];
                const ImDrawIdx I1 = List->IdxBuffer[static_cast<int>(Cmd.IdxOffset + Element + 1)];
                const ImDrawIdx I2 = List->IdxBuffer[static_cast<int>(Cmd.IdxOffset + Element + 2)];
                RasterizeTriangle(List->VtxBuffer[static_cast<int>(Cmd.VtxOffset) + I0],
                                  List->VtxBuffer[static_cast<int>(Cmd.VtxOffset) + I1],
                                  List->VtxBuffer[static_cast<int>(Cmd.VtxOffset) + I2], Cmd.ClipRect);
            }
        }
    }
}

bool WritePpm(const char* Path)
{
    std::FILE* File = std::fopen(Path, "wb");
    if (File == nullptr)
    {
        return false;
    }
    std::fprintf(File, "P6\n%d %d\n255\n", CaptureWidth, CaptureHeight);
    for (const Pixel& Value : Framebuffer)
    {
        const uint8_t Rgb[3] = {Value.Red, Value.Green, Value.Blue};
        std::fwrite(Rgb, 1, 3, File);
    }
    std::fclose(File);
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     FRAME DRIVER
//------------------------------------------------------------------------------------------------------------------------

Frontier::LogStreamPanel Panel;
bool                     ChannelsPopupOpen = false; // Sampled inside the frame (IsPopupOpen needs a window)

void RunFrame(double DeltaSeconds, bool Rasterize)
{
    Frontier::LogStreamPanel& Host = Panel;
    Host.Synchronize(Frontier::LogStore(), Frontier::LogCategories(), DeltaSeconds);

    ImGuiIO& Io    = ImGui::GetIO();
    Io.DeltaTime   = static_cast<float>(std::max(DeltaSeconds, 1.0 / 240.0));
    if (!Io.Fonts->IsBuilt())
    {
        Io.Fonts->Build(); // CPU backend: dynamic font sizes rebuild the atlas before NewFrame
    }
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(CaptureWidth), static_cast<float>(CaptureHeight)));
    // The harness stands in for the dock node: a borderless panel surface (the host draws the real tab).
    ImGui::Begin("Log Stream", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
    Host.ConstructInterface();
    ChannelsPopupOpen = ImGui::IsPopupOpen("##LogChannelsPopup");
    ImGui::End();
    ImGui::Render();

    if (Rasterize)
    {
        unsigned char* Pixels = nullptr;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&Pixels, &AtlasWidth, &AtlasHeight);
        AtlasPixels = Pixels;
        std::fill(Framebuffer.begin(), Framebuffer.end(), Pixel{11, 13, 12, 255});
        RasterizeDrawData(*ImGui::GetDrawData());
    }
}

void ClickAt(float X, float Y)
{
    ImGuiIO& Io = ImGui::GetIO();
    Io.AddMousePosEvent(X, Y);
    Io.AddMouseButtonEvent(0, true);
    RunFrame(1.0 / 60.0, false);
    Io.AddMouseButtonEvent(0, false);
    RunFrame(1.0 / 60.0, false);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       SEED DATA
//------------------------------------------------------------------------------------------------------------------------

using namespace Frontier;

void Emit(uint32_t Token, LogSeverity Severity, const char* Message, const char* Source)
{
    WriteLog(Token, Severity, LogVerbosity::Display, Message, Source);
}

void SeedStream(const std::function<void(double)>& PumpSecond)
{
    LogCategoryRegistry& Categories = LogCategories();
    const LogCategory FoliageCategory  = RegisterLogCategory("Foliage", LogColour::Pink);
    const LogCategory GameplayCategory = RegisterLogCategory("Gameplay", LogColour::Purple);
    const LogCategory NetworkCategory  = RegisterLogCategory("Network", LogColour::Blue);

    const LogCategory* Render      = Categories.QueryByName("Render");
    const LogCategory* Illum       = Categories.QueryByName("Illumination");
    const LogCategory* Physics     = Categories.QueryByName("Physics");
    const LogCategory* Vehicle     = Categories.QueryByName("Vehicle");
    const LogCategory* Environment = Categories.QueryByName("Environment");
    const LogCategory* Interface   = Categories.QueryByName("Interface");
    const LogCategory* Foliage     = &FoliageCategory;
    const LogCategory* Gameplay    = &GameplayCategory;
    const LogCategory* Network     = &NetworkCategory;

    // Burst 1 — boot.
    Emit(Interface ? Interface->Token : 0, LogSeverity::Info, "Project window opened · Project-Zero (blank reference)", "FrontierMain.cpp");
    Emit(Render ? Render->Token : 0, LogSeverity::Info, "Submission order resolved · 2 units · 0 imported targets", "SubmissionOrder.cpp");
    Emit(Render ? Render->Token : 0, LogSeverity::Info, "Transient atlas resolved · 2 targets -> 2 bins · 24883200 bytes (unaliased 24883200, saved 0)", "TransientTargetAtlas.cpp");
    PumpSecond(1.0);
    // Burst 2 — barriers.
    Emit(Render ? Render->Token : 0, LogSeverity::Info, "Barrier · Surface layout Undefined -> ColourWrite before SurfaceRaster [#0]", "BarrierDerivation.cpp");
    Emit(Render ? Render->Token : 0, LogSeverity::Info, "Barrier · Depth layout Undefined -> DepthWrite before SurfaceRaster [#0]", "BarrierDerivation.cpp");
    Emit(Render ? Render->Token : 0, LogSeverity::Info, "Barrier · Surface layout ColourWrite -> Sampled before DisplayPresent [#1]", "BarrierDerivation.cpp");
    Emit(Render ? Render->Token : 0, LogSeverity::Info, "Barriers derived · 5 layout transitions · 0 execution dependencies", "BarrierDerivation.cpp");
    PumpSecond(1.0);
    // Burst 3 — scene + user categories.
    Emit(Environment ? Environment->Token : 0, LogSeverity::Info, "Proving Grounds scene exported · 28 vertices · 42 indices · spin 0.6 rad/s", "ProvingGrounds.cpp");
    Emit(Foliage ? Foliage->Token : 0, LogSeverity::Info, "Canopy card atlas repacked · 512 cards · wind phase seeded", "FoliageStreaming.cpp");
    Emit(Gameplay ? Gameplay->Token : 0, LogSeverity::Info, "Checkpoint 3 armed · split delta -0.42 s vs session best", "RaceDirector.cpp");
    PumpSecond(1.0);
    // Burst 4 — repeats (collapse badge) + a warning.
    for (int Index = 0; Index < 4; ++Index)
    {
        Emit(Network ? Network->Token : 0, LogSeverity::Info, "Peer heartbeat acknowledged · rtt 18 ms · jitter 2 ms", "SessionLink.cpp");
    }
    Emit(Vehicle ? Vehicle->Token : 0, LogSeverity::Warning, "Tyre load estimate stale · Pacejka reference not yet wired (Phase 7)", "VehicleDynamics.cpp");
    PumpSecond(1.0);
    // Burst 5 — more life.
    Emit(Illum ? Illum->Token : 0, LogSeverity::Info, "Reservoir reuse converged · temporal history valid · 1 spp initial candidates", "IlluminationResolve.cpp");
    Emit(Physics ? Physics->Token : 0, LogSeverity::Info, "Worker pool steady · 8 threads · 0 jobs starved over 10 s window", "WorkerPool.cpp");
    Emit(Foliage ? Foliage->Token : 0, LogSeverity::Warning, "Leaf normal variance above budget on 12 cards · clamping to canopy mean", "FoliageStreaming.cpp");
    PumpSecond(1.0);
    // Burst 6 — an error and a fatal worth expanding.
    Emit(Render ? Render->Token : 0, LogSeverity::Error, "Surface target sampled before its layout transition completed · unit DisplayPresent reordered after SurfaceRaster", "BarrierDerivation.cpp");
    Emit(Gameplay ? Gameplay->Token : 0, LogSeverity::Info, "Ghost replay buffer ring wrapped · oldest 2.1 s retired", "RaceDirector.cpp");
    PumpSecond(1.0);
    // Burst 7 — settle.
    Emit(Network ? Network->Token : 0, LogSeverity::Info, "Snapshot 1042 published · render thread consumed in order", "SnapshotExchange.cpp");
    Emit(Interface ? Interface->Token : 0, LogSeverity::Info, "Log Stream panel synchronised · 0 dropped · following tail", "LogStreamPanel.cpp");
    PumpSecond(1.0);
}

} // namespace


//------------------------------------------------------------------------------------------------------------------------
//                                                    WORKSPACE MODE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Mirrors the TextureWorkspace project-window recipe (WindowsTextureStudio.cpp + MaterialLibraryPanel),
//    exactly as FrontierMain::RunProjectWindow now composes it: a host-owned PanelDockspace under a
//    ControlCentrePanel — NO project-summary header band, the trapezoidal expansion-notch tab in the top
//    inset band, Viewport | Log Stream docked 50/50. One substitution only: this harness plays the OS
//    window/GLFW/Vulkan host. Everything on screen is drawn by the same production units the Windows
//    build links.
#if defined(FRONTIER_WITH_WORKSPACE_CAPTURE)

#include "Engine/Diagnostics/DiagnosticJournal.h"
#include "Engine/Interface/LogStreamPanelRegistration.h"
#include "Engine/Interface/SceneDirectoryPanel.h"
#include "Engine/Interface/SceneDirectoryPanelRegistration.h"
#include "Engine/Scene/SceneDirectory.h"
#include "Engine/Scene/SceneRecord.h"
#include "UI/Editor/ControlCentrePanel.h"
#include "Engine/Interface/PanelDockspace.h"

#include <imgui_internal.h>

namespace
{

int PopupStackDepth()
{
    ImGuiContext* Context = ImGui::GetCurrentContext();
    return Context != nullptr ? Context->OpenPopupStack.Size : 0;
}

} // namespace

namespace
{

int RunWorkspaceCapture(const std::string& OutputDirectory)
{
    CaptureWidth  = 1920; // [px]
    CaptureHeight = 1080; // [px]

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& Io    = ImGui::GetIO();
    Io.IniFilename = nullptr;
    Io.DisplaySize = ImVec2(static_cast<float>(CaptureWidth), static_cast<float>(CaptureHeight));
    Io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    Io.AddMousePosEvent(-1.0f, -1.0f);
    Framebuffer.resize(static_cast<size_t>(CaptureWidth) * static_cast<size_t>(CaptureHeight));

    Frontier::DiagnosticJournal::Initialize("Frontier");
    Frontier::ControlCentrePanel Centre("EngineContent/FontArchives", {}, {});
    if (!Centre.Initialize() || !Centre.Activate())
    {
        std::printf("workspace capture refused: %s\n", Centre.QueryDiagnostic().c_str());
        ImGui::DestroyContext();
        return 3;
    }

    Frontier::PanelDockspace Panels;
    Panels.ConfigureDockspaceKey("Frontier project panels");
    Frontier::DockPanelSpecification ViewportPanel;
    ViewportPanel.Key     = "Viewport";
    ViewportPanel.Caption = "Viewport";
    // The same honest Scene Canvas placeholder FrontierMain registers until the Phase 3 renderer
    // records into this panel (upstream's own viewport is the identical placeholder today).
    ViewportPanel.ConstructContent = []() {
        const ImGuiStyle& Style = ImGui::GetStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28, 24));
        ImGui::TextColored(Style.Colors[ImGuiCol_TextDisabled], "SCENE CANVAS");
        ImGui::Spacing();
        ImGui::TextWrapped("The VehicleHMI viewport connects here: the ReSTIR renderer port (Phase 3) records "
                           "into this panel through VulkanPresentation. Native scene rendering is not connected "
                           "yet — this surface will present the project document once the SDK build lands.");
        ImGui::Separator();
        const auto Row = [](const char* Label, const char* Value) {
            ImGui::TextDisabled("%s", Label);
            ImGui::SameLine(150);
            ImGui::TextWrapped("%s", Value);
        };
        Row("Project", "ProjectZero");
        Row("Location", "Frontier/Projects/Project-Zero");
        ImGui::PopStyleVar();
    };
    ViewportPanel.Open = true;
    Frontier::SceneDirectoryPanel OutlinerPanel;
    if (!Panels.Register(ViewportPanel) ||
        !Panels.Register(Frontier::MakeLogStreamPanelSpecification(Panel)) ||
        !Panels.Register(Frontier::MakeSceneDirectoryPanelSpecification(OutlinerPanel)))
    {
        std::printf("workspace capture refused: a panel registration was rejected by the dockspace\n");
        ImGui::DestroyContext();
        return 4;
    }
    Panels.ConfigureWorkspaceInsets(79.0f, 0.0f);   // [px] - Top band for the notch tab (TextureStudio)
    Panels.ConfigureTabExtent(140.0f, 26.0f);       // [px]
    (void)Panels.ConfigureStackedPanels("Outliner", "LogStream", 420.0f);        // [px] upper height
    (void)Panels.ConfigureColumns({{"Viewport", 0.5f}, {"LogStream", 0.5f}});   // 50/50 split
    (void)Panels.ConfigurePlatform();

    // Seed the scene directory: a vehicle with four tyres, the proving-dock environment with its two
    // geometry records, a light and a camera — one tyre hidden so the eye-off state renders.
    std::vector<std::unique_ptr<Frontier::SceneRecord>> SceneRecords;
    Frontier::SceneDirectory&                          Directory = Frontier::SceneDirectory::Instance();
    const auto Spawn = [&](Frontier::RecordClassification Classification, const char* Name,
                           Frontier::RecordToken Parent = {}, bool Visible = true) {
        auto Record = std::make_unique<Frontier::SceneRecord>(Classification);
        (void)Directory.ConfigureName(Record->Token(), Name);
        if (Parent.IsValid())
        {
            (void)Directory.ConfigureParent(Record->Token(), Parent);
        }
        (void)Directory.ConfigureVisible(Record->Token(), Visible);
        const Frontier::RecordToken Token = Record->Token();
        SceneRecords.push_back(std::move(Record));
        return Token;
    };
    const Frontier::RecordToken Vehicle =
        Spawn(Frontier::RecordClassification::Vehicle, "VehicleHMI Test Vehicle");
    Spawn(Frontier::RecordClassification::Tyre, "Front Left", Vehicle);
    Spawn(Frontier::RecordClassification::Tyre, "Front Right", Vehicle);
    Spawn(Frontier::RecordClassification::Tyre, "Rear Left", Vehicle);
    Spawn(Frontier::RecordClassification::Tyre, "Rear Right", Vehicle, false);
    const Frontier::RecordToken Grounds =
        Spawn(Frontier::RecordClassification::Environment, "Proving Grounds");
    Spawn(Frontier::RecordClassification::Geometry, "Test Cube", Grounds);
    Spawn(Frontier::RecordClassification::Geometry, "Ground Plane", Grounds);
    Spawn(Frontier::RecordClassification::Illumination, "Key Light");
    Spawn(Frontier::RecordClassification::Camera, "Chase Camera");
    std::printf("seeded %zu scene records\n", SceneRecords.size());

    const auto Frame = [&](double Seconds, bool Rasterize) {
        Panel.Synchronize(Frontier::LogStore(), Frontier::LogCategories(), Seconds);
        while (const std::optional<Frontier::RecordClassification> Request = OutlinerPanel.TakeSpawnRequest())
        {
            Spawn(*Request, "Spawned Record");
        }
        OutlinerPanel.Synchronize(Directory);
        Centre.PrepareInterface();
        if (!Io.Fonts->IsBuilt())
        {
            Io.Fonts->Build(); // CPU backend: the atlas must exist before NewFrame (host backends upload it)
        }
        Io.DeltaTime = static_cast<float>(std::max(Seconds, 1.0 / 240.0));
        const float InterfaceScale =
            std::clamp(Centre.QueryAppearance().QueryApplied().InterfaceScale / 100.0f, 0.6f, 2.0f);   // [%] -> [-]
        ImGui::NewFrame();
        (void)Centre.Integrate(Seconds);
        Panels.ConstructInterface(InterfaceScale);
        Centre.ConstructInterface();
        ChannelsPopupOpen = ImGui::IsPopupOpen("##LogChannelsPopup");
        ImGui::Render();
        if (Rasterize)
        {
            unsigned char* Pixels = nullptr;
            Io.Fonts->GetTexDataAsRGBA32(&Pixels, &AtlasWidth, &AtlasHeight);
            AtlasPixels = Pixels;
            std::fill(Framebuffer.begin(), Framebuffer.end(), Pixel{11, 13, 12, 255});
            RasterizeDrawData(*ImGui::GetDrawData());
        }
    };

    Frame(1.0 / 60.0, false);
    SeedStream([&](double Seconds) { Frame(Seconds, false); });
    for (int Second = 0; Second < 14; ++Second)
    {
        Frame(1.0, false);
    }
    for (int Settle = 0; Settle < 30; ++Settle)
    {
        Frame(1.0 / 60.0, false); // Dock layout, tab motion and scroll prime settle
    }

    Frame(1.0 / 60.0, true);
    (void)WritePpm((OutputDirectory + "/workspace-studio.ppm").c_str());
    std::printf("workspace capture (studio 50/50 + notch tab) -> workspace-studio.ppm\n");

    const auto WorkspaceClick = [&](float X, float Y) {
        Io.AddMousePosEvent(X, Y);
        Io.AddMouseButtonEvent(0, true);
        Frame(1.0 / 60.0, false);
        Io.AddMouseButtonEvent(0, false);
        Frame(1.0 / 60.0, false);
    };

    // Column seating puts both panels front immediately; confirm the Log Stream window is drawing
    // (its window emits heavy draw lists), and record where it seated for the dropdown sweep below.
    ImGuiWindow* LogWindow = ImGui::FindWindowByName("Log Stream###FrontierPanel-LogStream");
    const bool LogStreamFront =
        LogWindow != nullptr && LogWindow->DrawList != nullptr && LogWindow->DrawList->VtxBuffer.Size > 400;
    std::printf("Log Stream seated at (%.0f, %.0f) size (%.0f, %.0f), drawing: %s\n",
                LogWindow != nullptr ? static_cast<double>(LogWindow->Pos.x) : -1.0,
                LogWindow != nullptr ? static_cast<double>(LogWindow->Pos.y) : -1.0,
                LogWindow != nullptr ? static_cast<double>(LogWindow->Size.x) : -1.0,
                LogWindow != nullptr ? static_cast<double>(LogWindow->Size.y) : -1.0,
                LogStreamFront ? "yes" : "NO");
    Frame(1.0 / 60.0, true);
    (void)WritePpm((OutputDirectory + "/workspace-active.ppm").c_str());
    std::printf("workspace capture (studio, settled) -> workspace-active.ppm\n");

    // Outliner close-up: select the Front Left tyre through the view-model, then give the panel the
    // whole dockspace; then open the spawn menu with a synthesized click on the add button.
    for (const Frontier::SceneDirectoryRow& Row : OutlinerPanel.QueryRows())
    {
        if (Row.Name == "Front Left")
        {
            OutlinerPanel.Select(Row.Token);
            std::printf("selected scene record '%s' (#%llu)\n", Row.Name.c_str(),
                        static_cast<unsigned long long>(Row.Token.Value));
        }
    }
    (void)Panels.ConfigureOpen("Viewport", false);
    (void)Panels.ConfigureOpen("LogStream", false);
    for (int Settle = 0; Settle < 10; ++Settle)
    {
        Frame(1.0 / 60.0, false);
    }
    Frame(1.0 / 60.0, true);
    (void)WritePpm((OutputDirectory + "/workspace-outliner.ppm").c_str());
    std::printf("workspace capture (outliner) -> workspace-outliner.ppm\n");
    {
        const auto PopupOpen = []() {
            ImGuiContext* Context = ImGui::GetCurrentContext();
            return Context != nullptr && Context->OpenPopupStack.Size > 0;
        };
        bool SpawnOpened = false;
        for (float Y = 92.0f; Y <= 128.0f && !SpawnOpened; Y += 4.0f)
        {
            for (float X = 1872.0f; X <= 1904.0f && !SpawnOpened; X += 6.0f)
            {
                WorkspaceClick(X, Y);
                if (PopupOpen())
                {
                    SpawnOpened = true;
                    std::printf("spawn menu opened by click at (%.0f, %.0f)\n", static_cast<double>(X),
                                static_cast<double>(Y));
                }
            }
        }
        if (SpawnOpened)
        {
            Io.AddMousePosEvent(1500.0f, 700.0f);
            Frame(1.0 / 60.0, false);
            Frame(1.0 / 60.0, true);
            (void)WritePpm((OutputDirectory + "/workspace-outliner-spawn.ppm").c_str());
            std::printf("workspace capture (outliner spawn menu) -> workspace-outliner-spawn.ppm\n");
            Io.AddKeyEvent(ImGuiKey_Escape, true);
            Frame(1.0 / 60.0, false);
            Io.AddKeyEvent(ImGuiKey_Escape, false);
            Frame(1.0 / 60.0, false);
        }
        else
        {
            std::printf("spawn menu sweep missed the add button\n");
        }
    }
    (void)Panels.ConfigureOpen("Viewport", true);
    (void)Panels.ConfigureOpen("LogStream", true);
    (void)Panels.ConfigureOpen("Outliner", false);
    for (int Settle = 0; Settle < 10; ++Settle)
    {
        Frame(1.0 / 60.0, false);
    }

    // Expand the error row through the panel's own selection API.
    size_t WorkspaceErrorIndex = Panel.QueryVisible().size();
    for (size_t Index = 0; Index < Panel.QueryVisible().size(); ++Index)
    {
        if (Panel.QueryVisible()[Index].Entry.Severity == Frontier::LogSeverity::Error)
        {
            WorkspaceErrorIndex = Index;
        }
    }
    if (WorkspaceErrorIndex < Panel.QueryVisible().size())
    {
        Panel.SelectVisible(WorkspaceErrorIndex);
        Frame(1.0 / 60.0, false);
        Frame(1.0 / 60.0, true);
        (void)WritePpm((OutputDirectory + "/workspace-expanded.ppm").c_str());
        std::printf("workspace capture (expanded row) -> workspace-expanded.ppm\n");
        Panel.SelectVisible(WorkspaceErrorIndex); // Toggle back off
    }

    // Log Stream alone in the dockspace (Viewport and Outliner closed through the panels API).
    (void)Panels.ConfigureOpen("Viewport", false);
    (void)Panels.ConfigureOpen("Outliner", false);
    for (int Settle = 0; Settle < 10; ++Settle)
    {
        Frame(1.0 / 60.0, false);
    }
    Frame(1.0 / 60.0, true);
    (void)WritePpm((OutputDirectory + "/workspace-logstream.ppm").c_str());
    std::printf("workspace capture (Log Stream alone) -> workspace-logstream.ppm\n");

    // Pull the Frontier expansion notch down (grip click, then a pull gesture if a click is not enough).
    {
        const Frontier::PlaneExtent Grip = Centre.QueryExpansionTab().QueryGripExtent();
        const float GripX = (Grip.MinimumX + Grip.MaximumX) * 0.5f;
        const float GripY = (Grip.MinimumY + Grip.MaximumY) * 0.5f;
        WorkspaceClick(GripX, GripY);
        for (int Settle = 0; Settle < 40 && !Centre.QueryExpansionTab().QueryOpen(); ++Settle)
        {
            Frame(1.0 / 60.0, false);
        }
        if (!Centre.QueryExpansionTab().QueryOpen())
        {
            Io.AddMousePosEvent(GripX, GripY);
            Io.AddMouseButtonEvent(0, true);
            for (int Hold = 0; Hold < 3; ++Hold)
            {
                Frame(1.0 / 60.0, false); // Let contact resolve before travel starts
            }
            for (int Step = 1; Step <= 28; ++Step)
            {
                Io.AddMousePosEvent(GripX, GripY + 8.0f * static_cast<float>(Step));
                Frame(1.0 / 60.0, false);
            }
            for (int Hold = 0; Hold < 3; ++Hold)
            {
                Frame(1.0 / 60.0, false);
            }
            Io.AddMouseButtonEvent(0, false);
            for (int Settle = 0; Settle < 90; ++Settle)
            {
                Frame(1.0 / 60.0, false);
            }
        }
        std::printf("expansion notch open after gesture: %s (grip %.0f..%.0f x %.0f..%.0f)\n",
                    Centre.QueryExpansionTab().QueryOpen() ? "yes" : "no",
                    static_cast<double>(Grip.MinimumX), static_cast<double>(Grip.MaximumX),
                    static_cast<double>(Grip.MinimumY), static_cast<double>(Grip.MaximumY));
        if (!Centre.QueryExpansionTab().QueryOpen())
        {
            // The top-bar "Control centre" button's own action: open the drawer at Quick Controls.
            Centre.ActivateSettingsPage(Frontier::ControlCentrePageCategory::QuickControls);
            for (int Settle = 0; Settle < 240; ++Settle)
            {
                Frame(1.0 / 60.0, false);
            }
            std::printf("expansion notch open after Control centre action: %s\n",
                        Centre.QueryExpansionTab().QueryOpen() ? "yes" : "no");
        }
        Frame(1.0 / 60.0, true);
        (void)WritePpm((OutputDirectory + "/workspace-notch.ppm").c_str());
        std::printf("workspace capture (notch) -> workspace-notch.ppm\n");
    }

    // Open both dropdowns by sweeping the filter row left-to-right: the severity chips open nothing, so the
    // first popup met is Channels and the next distinct one is Verbosity. (Popup window names are ID hashes
    // and IsPopupOpen hashes against a different seed here, so the sweep identifies menus by position and
    // logs each popup's draw-list size as corroboration.)
    {
        bool ChannelsDone = false;
        bool VerbosityDone = false;
        const auto PopupWindow = []() -> ImGuiWindow* {
            ImGuiContext* Context = ImGui::GetCurrentContext();
            if (Context == nullptr || Context->OpenPopupStack.Size == 0)
            {
                return nullptr;
            }
            return Context->OpenPopupStack.back().Window;
        };
        const auto DismissPopup = [&]() {
            Io.AddKeyEvent(ImGuiKey_Escape, true);
            Frame(1.0 / 60.0, false);
            Io.AddKeyEvent(ImGuiKey_Escape, false);
            Frame(1.0 / 60.0, false);
        };
        // The Log Stream now occupies the full dockspace (Viewport closed above): its filter row
        // sits ~171 px below the panel content top (79 px inset + tab strip).
        for (float Y = 250.0f; Y <= 360.0f && !(ChannelsDone && VerbosityDone); Y += 5.0f)
        {
            for (float X = 300.0f; X <= 1500.0f && !(ChannelsDone && VerbosityDone); X += 7.0f)
            {
                WorkspaceClick(X, Y);
                ImGuiWindow* Popup = PopupWindow();
                if (Popup == nullptr)
                {
                    continue;
                }
                Frame(1.0 / 60.0, false);
                Frame(1.0 / 60.0, false);
                Popup = PopupWindow();
                if (Popup == nullptr)
                {
                    continue;
                }
                const int Verts = Popup->DrawList != nullptr ? Popup->DrawList->VtxBuffer.Size : 0;
                std::printf("popup '%s' at (%.0f, %.0f) from click (%.0f, %.0f), %d verts\n",
                            Popup->Name != nullptr ? Popup->Name : "", static_cast<double>(Popup->Pos.x),
                            static_cast<double>(Popup->Pos.y), static_cast<double>(X), static_cast<double>(Y), Verts);
                Io.AddMousePosEvent(960.0f, 1000.0f);
                Frame(1.0 / 60.0, false);
                Frame(1.0 / 60.0, true);
                if (!ChannelsDone)
                {
                    ChannelsDone = true;
                    (void)WritePpm((OutputDirectory + "/workspace-channels.ppm").c_str());
                    std::printf("workspace capture (channels menu) -> workspace-channels.ppm\n");
                }
                else
                {
                    VerbosityDone = true;
                    (void)WritePpm((OutputDirectory + "/workspace-verbosity.ppm").c_str());
                    std::printf("workspace capture (verbosity menu) -> workspace-verbosity.ppm\n");
                }
                DismissPopup();
                X += 150.0f;                                           // Step past this trigger before sweeping on
            }
        }
        std::printf("dropdown sweep: channels=%s verbosity=%s\n", ChannelsDone ? "captured" : "missed",
                    VerbosityDone ? "captured" : "missed");
    }

    ImGui::DestroyContext();
    return 0;
}

} // namespace

#endif // FRONTIER_WITH_WORKSPACE_CAPTURE

//------------------------------------------------------------------------------------------------------------------------
//                                                          MAIN
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char** Arguments)
{
    if (ArgumentCount < 3)
    {
        std::fprintf(stderr, "usage: PanelCapture <font.ttf> <output-directory>\n");
        return 2;
    }
    const std::string OutputDirectory = Arguments[2];
#if defined(FRONTIER_WITH_WORKSPACE_CAPTURE)
    if (ArgumentCount > 3 && std::string(Arguments[3]) == "--workspace")
    {
        return RunWorkspaceCapture(OutputDirectory);
    }
#endif

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& Io = ImGui::GetIO();
    Io.IniFilename = nullptr;
    Io.DisplaySize = ImVec2(static_cast<float>(CaptureWidth), static_cast<float>(CaptureHeight));
    Io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    Io.AddMousePosEvent(-1.0f, -1.0f);

    if (Io.Fonts->AddFontFromFileTTF(Arguments[1], 15.0f) == nullptr)
    {
        Io.Fonts->AddFontDefault();
    }
    static Frontier::TypefaceRegistry FaceRegistry;
    const uint32_t Families = FaceRegistry.Load("EngineContent/FontArchives");
    Frontier::TypefaceRegistry::Install(&FaceRegistry);
    std::printf("typeface registry: %u families, DM Sans index %d\n", Families,
                FaceRegistry.ResolveFamilyIndex("DM Sans"));
    unsigned char* OwnedAtlas = nullptr;
    Io.Fonts->GetTexDataAsRGBA32(&OwnedAtlas, &AtlasWidth, &AtlasHeight);
    AtlasPixels = OwnedAtlas;

    // Host-side style from the upstream palette, the way the Windows host applies its theme.
    ImGui::StyleColorsDark();
    ImGuiStyle& Style = ImGui::GetStyle();
    const auto& Palette = Frontier::ControlKit::Palette();
    const auto  Quad = [](Frontier::ColorQuad Colour) {
        return ImVec4(Colour.Red, Colour.Green, Colour.Blue, Colour.Alpha);
    };
    Style.Colors[ImGuiCol_WindowBg]         = Quad(Palette.Panel);
    Style.Colors[ImGuiCol_ChildBg]          = Quad(Palette.Panel);
    Style.Colors[ImGuiCol_PopupBg]          = Quad(Palette.Card);
    Style.Colors[ImGuiCol_Text]             = Quad(Palette.Text);
    Style.Colors[ImGuiCol_TextDisabled]     = Quad(Palette.TextDim);
    Style.Colors[ImGuiCol_Button]           = Quad(Palette.Raised);
    Style.Colors[ImGuiCol_ButtonHovered]    = Quad(Palette.Selected);
    Style.Colors[ImGuiCol_ButtonActive]     = Quad(Palette.Selected);
    Style.Colors[ImGuiCol_FrameBg]          = Quad(Palette.Inset);
    Style.Colors[ImGuiCol_FrameBgHovered]   = Quad(Palette.Raised);
    Style.Colors[ImGuiCol_FrameBgActive]    = Quad(Palette.Selected);
    Style.Colors[ImGuiCol_Header]           = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    Style.Colors[ImGuiCol_HeaderHovered]    = Quad(Palette.Raised);
    Style.Colors[ImGuiCol_HeaderActive]     = Quad(Palette.Selected);
    Style.Colors[ImGuiCol_TitleBg]          = Quad(Palette.Card);
    Style.Colors[ImGuiCol_TitleBgActive]    = Quad(Palette.Card);
    Style.Colors[ImGuiCol_Separator]        = ImVec4(1.0f, 1.0f, 1.0f, 0.08f);
    Style.Colors[ImGuiCol_Border]           = ImVec4(1.0f, 1.0f, 1.0f, 0.08f);
    Style.Colors[ImGuiCol_ScrollbarBg]      = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    Style.Colors[ImGuiCol_ScrollbarGrab]    = Quad(Palette.StrokeStrong);
    Style.WindowRounding                    = 6.0f;
    Style.FrameRounding                     = 5.0f;
    Style.PopupRounding                     = 6.0f;
    Style.GrabRounding                      = 4.0f;
    Style.WindowPadding                     = ImVec2(10.0f, 8.0f);
    Style.FramePadding                      = ImVec2(8.0f, 4.0f);
    Style.ItemSpacing                       = ImVec2(6.0f, 5.0f);

    Framebuffer.resize(static_cast<size_t>(CaptureWidth) * CaptureHeight);

    // Warm-up so the window, scroll prime and first Synchronize settle before seeding.
    RunFrame(1.0 / 60.0, false);
    SeedStream([](double Seconds) { RunFrame(Seconds, false); });
    for (int Second = 0; Second < 14; ++Second)
    {
        RunFrame(1.0, false); // Quiet seconds scroll the burst shape into the sparkline window
    }
    for (int Settle = 0; Settle < 6; ++Settle)
    {
        RunFrame(1.0 / 60.0, false);
    }

    // Capture A — the panel as hosted, following the tail.
    RunFrame(1.0 / 60.0, true);
    const std::string MainPpm = OutputDirectory + "/panel-main.ppm";
    (void)WritePpm(MainPpm.c_str());
    std::printf("capture A (visible rows: %zu): %s\n", Panel.QueryVisible().size(), MainPpm.c_str());

    // Capture B — expand the error row (detail block + Copy entry / Only this channel / Filter to text).
    size_t ErrorIndex = Panel.QueryVisible().size();
    for (size_t Index = 0; Index < Panel.QueryVisible().size(); ++Index)
    {
        if (Panel.QueryVisible()[Index].Entry.Severity == Frontier::LogSeverity::Error)
        {
            ErrorIndex = Index;
        }
    }
    if (ErrorIndex < Panel.QueryVisible().size())
    {
        Panel.SelectVisible(ErrorIndex);
    }
    RunFrame(1.0 / 60.0, false);
    RunFrame(1.0 / 60.0, true);
    const std::string ExpandedPpm = OutputDirectory + "/panel-expanded.ppm";
    (void)WritePpm(ExpandedPpm.c_str());
    std::printf("capture B (expanded index %zu): %s\n", ErrorIndex, ExpandedPpm.c_str());

    // Capture C — open the Channels dropdown by driving real mouse input across the chips row
    // (right-to-left: the "All channels" trigger is the first control past the severity chips).
    bool Opened = false;
    int  ClicksSinceEscape = 0;
    for (float Y = 176.0f; Y <= 206.0f && !Opened; Y += 3.0f)
    {
        for (float X = 1100.0f; X >= 200.0f && !Opened; X -= 5.0f)
        {
            ClickAt(X, Y);
            if (ChannelsPopupOpen)
            {
                Opened = true;
                std::printf("channels popup opened by click at (%.0f, %.0f)\n", static_cast<double>(X),
                            static_cast<double>(Y));
            }
            else if (++ClicksSinceEscape >= 48)
            {
                // Blind Escape: dismiss any stray Verbosity combo popup the sweep may have opened.
                ClicksSinceEscape = 0;
                ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
                RunFrame(1.0 / 60.0, false);
                ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
                RunFrame(1.0 / 60.0, false);
            }
        }
    }
    if (Opened)
    {
        ImGui::GetIO().AddMousePosEvent(700.0f, 860.0f);
        RunFrame(1.0 / 60.0, false);
        RunFrame(1.0 / 60.0, true);
        const std::string ChannelsPpm = OutputDirectory + "/panel-channels.ppm";
        (void)WritePpm(ChannelsPpm.c_str());
        std::printf("capture C: %s\n", ChannelsPpm.c_str());
    }
    else
    {
        std::printf("capture C skipped: channels popup never opened during input sweep\n");
    }

    // Capture D/F/E: dismiss any popup and collapse the detail row first.
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
    RunFrame(1.0 / 60.0, false);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
    if (ErrorIndex < Panel.QueryVisible().size())
    {
        Panel.SelectVisible(ErrorIndex);                               // Toggle the detail closed again
    }

    // Capture D - live search: match spans highlight, "N shown" + clear disc appear.
    Panel.ConfigureQuery("barrier");
    Panel.Synchronize(Frontier::LogStore(), Frontier::LogCategories(), 0.0);
    RunFrame(1.0 / 60.0, false);
    RunFrame(1.0 / 60.0, true);
    (void)WritePpm((OutputDirectory + "/panel-search.ppm").c_str());
    std::printf("capture D (search 'barrier', %zu shown) -> panel-search.ppm\n", Panel.QueryVisible().size());
    Panel.ConfigureQuery("");

    // Capture F - overflow menu (Stream/Display actions) via a real click on the "..." button.
    ClickAt(static_cast<float>(CaptureWidth) - 40.0f, 36.0f);
    RunFrame(1.0 / 60.0, false);
    RunFrame(1.0 / 60.0, true);
    (void)WritePpm((OutputDirectory + "/panel-overflow.ppm").c_str());
    std::printf("capture F (overflow menu) -> panel-overflow.ppm\n");
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
    RunFrame(1.0 / 60.0, false);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);

    // Capture E - scrolled up while entries keep arriving: the floating "N new" pill appears.
    ImGui::GetIO().AddMousePosEvent(620.0f, 420.0f);
    for (int Wheel = 0; Wheel < 5; ++Wheel)
    {
        ImGui::GetIO().AddMouseWheelEvent(0.0f, 1.0f);                 // Real wheel-up over the stream
        RunFrame(1.0 / 60.0, false);
    }
    {
        const LogCategory* InterfaceCat = Frontier::LogCategories().QueryByName("Interface");
        const uint32_t     Token        = InterfaceCat ? InterfaceCat->Token : 0;
        Emit(Token, Frontier::LogSeverity::Info, "Notch calibration pass complete \xC2\xB7 grip extent published",
             "PanelExpansionTab.cpp");
        Emit(Token, Frontier::LogSeverity::Info, "Dock layout persisted \xC2\xB7 5 panels \xC2\xB7 2 splits",
             "PanelDockspace.cpp");
        Emit(Token, Frontier::LogSeverity::Info, "Preview texture resident \xC2\xB7 1024 px tile \xC2\xB7 4 mips",
             "TextureWorkspace.cpp");
    }
    for (int Settle = 0; Settle < 4; ++Settle)
    {
        RunFrame(1.0 / 60.0, false);
    }
    RunFrame(1.0 / 60.0, true);
    (void)WritePpm((OutputDirectory + "/panel-newpill.ppm").c_str());
    std::printf("capture E (new-below pill, %u new) -> panel-newpill.ppm\n", Panel.QueryNewBelow());

    ImGui::DestroyContext();
    return 0;
}
