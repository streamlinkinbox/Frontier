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
#include <stb_image_write.h>

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

const ImTextureData* BoundTexture = nullptr;
bool SampleWhiteTexel = false; // Commands bound to external GPU textures render as flat colour

float SampleBoundTexture(float U, float V, int Channel)
{
    if (BoundTexture == nullptr || BoundTexture->Pixels == nullptr || BoundTexture->Width <= 0 ||
        BoundTexture->Height <= 0 || BoundTexture->BytesPerPixel <= 0)
    {
        return 1.0f;
    }
    if (BoundTexture->Format == ImTextureFormat_Alpha8 && Channel != 3)
    {
        return 1.0f;
    }
    const int Components = BoundTexture->BytesPerPixel;
    const float X = std::clamp(U * static_cast<float>(BoundTexture->Width) - 0.5f, 0.0f,
                               static_cast<float>(BoundTexture->Width - 1));
    const float Y = std::clamp(V * static_cast<float>(BoundTexture->Height) - 0.5f, 0.0f,
                               static_cast<float>(BoundTexture->Height - 1));
    const int   X0 = static_cast<int>(X);
    const int   Y0 = static_cast<int>(Y);
    const int   X1 = std::min(X0 + 1, BoundTexture->Width - 1);
    const int   Y1 = std::min(Y0 + 1, BoundTexture->Height - 1);
    const float Fx = X - static_cast<float>(X0);
    const float Fy = Y - static_cast<float>(Y0);
    const auto  At = [&](int Px, int Py) {
        const size_t Offset = (static_cast<size_t>(Py) * BoundTexture->Width + Px) * Components;
        const int    Sample = BoundTexture->Format == ImTextureFormat_Alpha8 ? 0 : Channel;
        return static_cast<float>(BoundTexture->Pixels[Offset + Sample]) / 255.0f;
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

            const float Sr = (SampleWhiteTexel ? 1.0f : SampleBoundTexture(U, V, 0)) * Vr;
            const float Sg = (SampleWhiteTexel ? 1.0f : SampleBoundTexture(U, V, 1)) * Vg;
            const float Sb = (SampleWhiteTexel ? 1.0f : SampleBoundTexture(U, V, 2)) * Vb;
            const float Sa = (SampleWhiteTexel ? 1.0f : SampleBoundTexture(U, V, 3)) * Va;

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
            BoundTexture = Cmd.TexRef._TexData;
            SampleWhiteTexel = BoundTexture == nullptr || BoundTexture->Pixels == nullptr;
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


bool WritePng(const char* Path)
{
    static_assert(sizeof(Pixel) == 4);
    return stbi_write_png(Path, CaptureWidth, CaptureHeight, 4,
                          reinterpret_cast<const unsigned char*>(Framebuffer.data()),
                          CaptureWidth * static_cast<int>(sizeof(Pixel))) != 0;
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
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(nullptr, nullptr, nullptr);
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

void WriteCaptureLog(uint32_t Token, LogSeverity Severity, const char* Message, const char* SourcePath)
{
    WriteLog(Token, Severity, LogVerbosity::Display, Message, SourcePath);
}

void SeedStream(const std::function<void(double)>& PumpSecond)
{
    const LogCategory* Interface = LogCategories().QueryByName("Interface");
    const LogCategory* Content   = LogCategories().QueryByName("Content");
    const LogCategory* Render    = LogCategories().QueryByName("Render");
    const LogCategory* Environment = LogCategories().QueryByName("Environment");

    WriteCaptureLog(Interface != nullptr ? Interface->Token : 0, LogSeverity::Info,
                    "Project session opened · Project Everest", "FrontierMain.cpp");
    PumpSecond(1.0);
    WriteCaptureLog(Content != nullptr ? Content->Token : 0, LogSeverity::Info,
                    "Project content root configured · Content", "ProjectContent.cpp");
    WriteCaptureLog(Environment != nullptr ? Environment->Token : 0, LogSeverity::Info,
                    "Reusable environment records registered · Sun, Atmosphere, Clouds, Fog, Wind",
                    "EnvironmentRecords.cpp");
    PumpSecond(1.0);
    WriteCaptureLog(Interface != nullptr ? Interface->Token : 0, LogSeverity::Info,
                    "Outliner selection synchronized · Sun", "SceneDirectoryPanel.cpp");
    WriteCaptureLog(Interface != nullptr ? Interface->Token : 0, LogSeverity::Info,
                    "Sun Inspector exposes Overview, Daylight, Light & colour, and Disc & optics", "SunInspectorDashboard.cpp");
    PumpSecond(1.0);
    WriteCaptureLog(Render != nullptr ? Render->Token : 0, LogSeverity::Info,
                    "Scene Canvas idle · no opening scene or renderer connected", "FrontierMain.cpp");
    WriteCaptureLog(Interface != nullptr ? Interface->Token : 0, LogSeverity::Info,
                    "Log Stream synchronized · following the current session", "LogStreamPanel.cpp");
    PumpSecond(1.0);
}
} // namespace


//------------------------------------------------------------------------------------------------------------------------
//                                                    WORKSPACE MODE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Renders the native four-panel workspace.
//    It uses the same ControlCentrePanel, PanelDockspace, Outliner, Inspector, and Log Stream
//    production units as FrontierMain. The exhibit substitutes a CPU rasterizer for GLFW/Vulkan presentation.
#if defined(FRONTIER_WITH_WORKSPACE_CAPTURE)

#include "Engine/Diagnostics/DiagnosticJournal.h"
#include "Engine/Interface/LogStreamPanelRegistration.h"
#include "Engine/Interface/IconPresentation.h"
#include "Engine/Interface/SceneDirectoryPanel.h"
#include "Engine/Interface/SceneDirectoryPanelRegistration.h"
#include "Engine/Interface/SceneInspectorPanel.h"
#include "Engine/Scene/EnvironmentRecords.h"
#include "Engine/Scene/SceneDirectory.h"
#include "Engine/Scene/SceneRecord.h"
#include "UI/Editor/ControlCentrePanel.h"
#include "Engine/Interface/PanelDockspace.h"

#include <filesystem>

namespace
{

int RunWorkspaceCapture(const std::string& OutputDirectory, const std::filesystem::path& RepositoryRoot)
{
    CaptureWidth  = 1920;
    CaptureHeight = 1080;
    std::filesystem::create_directories(OutputDirectory);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& Io    = ImGui::GetIO();
    Io.IniFilename = nullptr;
    Io.DisplaySize = ImVec2(static_cast<float>(CaptureWidth), static_cast<float>(CaptureHeight));
    Io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    // The software exhibit consumes ImTextureData directly, exercising the same dynamic-icon atlas as the Vulkan host.
    Io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    Io.AddMousePosEvent(-1.0f, -1.0f);
    Framebuffer.resize(static_cast<size_t>(CaptureWidth) * static_cast<size_t>(CaptureHeight));

    const std::filesystem::path ProjectDirectory =
        RepositoryRoot / "Frontier" / "Projects" / "Project-Everest";
    Frontier::DiagnosticJournal::Initialize("Frontier");
    Frontier::ControlCentrePanel Centre(RepositoryRoot / "EngineContent" / "FontArchives",
                                        ProjectDirectory / "Saved" / "ControlCentre.toml");
    if (!Centre.Initialize() || !Centre.Activate())
    {
        std::printf("workspace capture refused: %s\n", Centre.QueryDiagnostic().c_str());
        ImGui::DestroyContext();
        return 3;
    }
    Frontier::IconPresentation::AttachVehicleHmiOutliner(RepositoryRoot / "EngineContent" / "Icons");

    Frontier::PanelDockspace Panels;
    (void)Panels.ConfigureDockspaceKey("Frontier project panels");
    Frontier::DockPanelSpecification ViewportPanel;
    ViewportPanel.Key              = "Viewport";
    ViewportPanel.Caption          = "Viewport";
    ViewportPanel.Open             = true;
    ViewportPanel.ConstructContent = []() {
        const ImGuiStyle& Style = ImGui::GetStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28.0f, 24.0f));
        ImGui::TextColored(Style.Colors[ImGuiCol_TextDisabled], "SCENE CANVAS");
        ImGui::Spacing();
        ImGui::TextWrapped("Project Everest has no opening scene. The VehicleHMI viewport and ReSTIR renderer "
                           "are not connected in this UI port; this canvas remains an empty workspace.");
        ImGui::Separator();
        ImGui::TextDisabled("Project");
        ImGui::SameLine(150.0f);
        ImGui::TextUnformatted("Project Everest");
        ImGui::TextDisabled("Content");
        ImGui::SameLine(150.0f);
        ImGui::TextUnformatted("Frontier/Projects/Project-Everest/Content");
        ImGui::PopStyleVar();
    };

    Frontier::SceneDirectoryPanel OutlinerPanel;
    Frontier::DockPanelSpecification InspectorPanel;
    InspectorPanel.Key              = "Inspector";
    InspectorPanel.Caption          = "Inspector";
    InspectorPanel.Open             = true;
    InspectorPanel.ConstructContent = [&OutlinerPanel]() {
        Frontier::ConstructSceneInspectorInterface(OutlinerPanel);
    };

    if (!Panels.Register(ViewportPanel) ||
        !Panels.Register(Frontier::MakeSceneDirectoryPanelSpecification(OutlinerPanel)) ||
        !Panels.Register(Frontier::MakeLogStreamPanelSpecification(Panel)) ||
        !Panels.Register(InspectorPanel))
    {
        std::printf("workspace capture refused: a native panel registration was rejected\n");
        ImGui::DestroyContext();
        return 4;
    }
    Panels.ConfigureWorkspaceInsets(79.0f, 0.0f);
    Panels.ConfigureTabExtent(140.0f, 26.0f);
    const bool StackConfigured = Panels.ConfigureStackedPanels("Viewport", "LogStream", 560.0f);
    const bool ColumnsConfigured = Panels.ConfigureColumns(
        {{"Outliner", 0.2f}, {"LogStream", 0.56f}, {"Inspector", 0.24f}});
    if (!StackConfigured || !ColumnsConfigured || !Panels.ConfigurePlatform())
    {
        std::printf("workspace capture refused: the four-panel layout was rejected\n");
        ImGui::DestroyContext();
        return 5;
    }

    Frontier::SunRecord        Sun;
    Frontier::AtmosphereRecord Atmosphere;
    Frontier::CloudRecord      Clouds;
    Frontier::FogRecord        Fog;
    Frontier::WindRecord       Wind;
    Frontier::SceneDirectory&  Directory = Frontier::SceneDirectory::Instance();
    (void)Directory.ConfigureName(Sun.Token(), "Sun");
    (void)Directory.ConfigureName(Atmosphere.Token(), "Atmosphere");
    (void)Directory.ConfigureName(Clouds.Token(), "Clouds");
    (void)Directory.ConfigureName(Fog.Token(), "Fog");
    (void)Directory.ConfigureName(Wind.Token(), "Wind");
    OutlinerPanel.ConfigureWorkspaceContext("Project Everest", "No opening scene · canvas idle");
    OutlinerPanel.Synchronize(Directory);
    if (!OutlinerPanel.SelectByStableId("sun"))
    {
        std::printf("workspace capture refused: reusable Sun record has no Inspector description\n");
        ImGui::DestroyContext();
        return 6;
    }
    if (!OutlinerPanel.QueryInspectorModel().ConstructInspector)
    {
        std::printf("workspace capture refused: Sun did not install its native four-section Inspector page\n");
        ImGui::DestroyContext();
        return 6;
    }

    const LogCategory* Interface = Frontier::LogCategories().QueryByName("Interface");
    const LogCategory* Render    = Frontier::LogCategories().QueryByName("Render");
    const auto LogToken = [](const LogCategory* Category) { return Category != nullptr ? Category->Token : 0; };
    WriteCaptureLog(LogToken(Interface), Frontier::LogSeverity::Info,
                    "Project session opened · Project Everest", "FrontierMain.cpp");
    WriteCaptureLog(LogToken(Interface), Frontier::LogSeverity::Info,
                    "Outliner selected Sun · native four-section VehicleHMI Inspector is active",
                    "SunInspectorDashboard.cpp");
    WriteCaptureLog(LogToken(Render), Frontier::LogSeverity::Info,
                    "Scene Canvas idle · no opening scene or renderer connected", "FrontierMain.cpp");

    const auto Frame = [&](double Seconds, bool Rasterize) {
        Panel.Synchronize(Frontier::LogStore(), Frontier::LogCategories(), Seconds);
        OutlinerPanel.Synchronize(Directory);
        Centre.PrepareInterface();
        if (!Io.Fonts->IsBuilt())
        {
            Io.Fonts->Build();
        }
        Io.DeltaTime = static_cast<float>(std::max(Seconds, 1.0 / 240.0));
        const float InterfaceScale =
            std::clamp(Centre.QueryAppearance().QueryApplied().InterfaceScale / 100.0f, 0.6f, 2.0f);
        ImGui::NewFrame();
        (void)Centre.Integrate(Seconds);
        Panels.ConstructInterface(InterfaceScale);
        Centre.ConstructInterface();
        ImGui::Render();
        if (Rasterize)
        {
            Io.Fonts->GetTexDataAsRGBA32(nullptr, nullptr, nullptr);
            std::fill(Framebuffer.begin(), Framebuffer.end(), Pixel{11, 13, 12, 255});
            RasterizeDrawData(*ImGui::GetDrawData());
        }
    };

    for (int Warmup = 0; Warmup < 4; ++Warmup)
    {
        Frame(1.0 / 60.0, false);
    }
    for (int Settle = 0; Settle < 30; ++Settle)
    {
        Frame(1.0 / 60.0, false);
    }
    Frame(1.0 / 60.0, true);
    const Frontier::IconSymbol OutlinerIcons[] = {
        Frontier::IconSymbol::FolderEnvironment, Frontier::IconSymbol::Sun, Frontier::IconSymbol::SkyScattering,
        Frontier::IconSymbol::Clouds, Frontier::IconSymbol::Fog, Frontier::IconSymbol::LocalCloud,
        Frontier::IconSymbol::LocalFog, Frontier::IconSymbol::OutlinerStars, Frontier::IconSymbol::Moon,
        Frontier::IconSymbol::LensFlare};
    bool IconAssetsReady = true;
    for (const Frontier::IconSymbol Symbol : OutlinerIcons)
    {
        if (Frontier::IconPresentation::Result(Symbol) != Frontier::IconResult::Ready)
        {
            std::fprintf(stderr, "workspace capture refused: %s icon unavailable: %s\n",
                         Frontier::IconArt::Name(Symbol), Frontier::IconPresentation::Diagnostic(Symbol));
            IconAssetsReady = false;
        }
    }
    const std::string CapturePath = OutputDirectory + "/native-workspace.png";
    const bool CaptureWritten = WritePng(CapturePath.c_str());
    std::printf("Project Everest native ImGui workspace · Outliner + Inspector + Log Stream · icons %s · capture %s\n",
                IconAssetsReady ? "ready" : "failed", CaptureWritten ? CapturePath.c_str() : "failed");

    ImGui::DestroyContext();
    return CaptureWritten && IconAssetsReady ? 0 : 7;
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
        std::fprintf(stderr, "usage: PanelCapture <font.ttf> <output-directory> [--workspace] [repository-root]\n");
        return 2;
    }
    const std::string OutputDirectory = Arguments[2];
#if defined(FRONTIER_WITH_WORKSPACE_CAPTURE)
    const std::filesystem::path RepositoryRoot = ArgumentCount > 4 ? Arguments[4] : std::filesystem::current_path();
    if (ArgumentCount > 3 && std::string(Arguments[3]) == "--workspace")
    {
        return RunWorkspaceCapture(OutputDirectory, RepositoryRoot);
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
    Io.Fonts->GetTexDataAsRGBA32(nullptr, nullptr, nullptr);

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
        WriteCaptureLog(Token, Frontier::LogSeverity::Info, "Notch calibration pass complete \xC2\xB7 grip extent published",
             "PanelExpansionTab.cpp");
        WriteCaptureLog(Token, Frontier::LogSeverity::Info, "Dock layout persisted \xC2\xB7 5 panels \xC2\xB7 2 splits",
             "PanelDockspace.cpp");
        WriteCaptureLog(Token, Frontier::LogSeverity::Info, "Preview texture resident \xC2\xB7 1024 px tile \xC2\xB7 4 mips",
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
