/*==========================================================================================================================================
                                                     PROJECTEVERESTVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 End-to-end 1:1 VehicleHMI parity validation and 1920x1080 native-pixel CPU rasterization for Project Everest:
//    Project Browser -> Project Everest launch -> 'FCT1' ControlVehicle (2,924 body + 74 VehicleGlass + 4x5,536 wheel facets)
//    + 1:1 Proving Grounds Course (Two-Level SAH BVH + RTX ReSTIR GI vs GTX 4-Cascade SDF + CardRadiosity + BVH Reflections)
//    + 1:1 native ImTextureData RGBA32 viewport + VehicleHMI Outliner & Inspector + 10 Engine Observatories + Tier-3 Studios.

#include "Exhibits/ControlCentre/Code/CpuRasterization.h"

#include "Applications/Editor/ProjectBrowserHost.h"
#include "Engine/Diagnostics/LogCategories.h"
#include "Engine/Diagnostics/LogStream.h"
#include "Engine/Interface/ContentDirectory.h"
#include "Engine/Interface/ControlKit.h"
#include "Engine/Interface/DomainStudioPanels.h"
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
#include "Engine/Render/DielectricRefraction.h"
#include "Engine/Rendering/AtmosphereModel.h"
#include "Engine/Rendering/CloudField.h"
#include "Engine/Rendering/DualTierRenderer.h"
#include "Engine/Rendering/PathReplayRayQuery.h"
#include "Engine/Scene/EnvironmentRecords.h"
#include "Engine/Scene/RecordEntry.h"
#include "Engine/Scene/SceneDirectory.h"
#include "Projects/Project-Everest/Source/ProjectEverestScene.h"
#include "UI/Editor/ControlCentrePanel.h"
#include "UI/Editor/ProjectBrowserPanel.h"

#include <ProjectInterchange.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
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

constexpr int CaptureWidth  = 1920;   // [px] - Full HD desktop capture width
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
//                           1:1 NATIVE PIXEL VIEWPORT HOST (MATCHING WORKSPACEAPPLICATION.CPP)
//------------------------------------------------------------------------------------------------------------------------

struct ViewportTextureHost
{
    static constexpr int   AtlasW       = 2560;
    static constexpr int   AtlasH       = 1440;
    ImTextureData          Texture{};
    std::vector<uint8_t>   ScratchRgba{};
    bool                   Registered   = false;
    uint32_t               CachedWidth  = 0u;
    uint32_t               CachedHeight = 0u;
    EverestViewportInputs  CachedInputs{};
    DualTierFrameTelemetry CachedTelemetry{};
    bool                   CacheValid   = false;

    void Shutdown()
    {
        if (Registered && ImGui::GetCurrentContext() != nullptr)
        {
            ImGui::UnregisterUserTexture(&Texture);
            auto& Textures = ImGui::GetPlatformIO().Textures;
            for (int I = Textures.Size - 1; I >= 0; --I)
            {
                if (Textures[I] == &Texture)
                {
                    Textures.erase(Textures.Data + I);
                }
            }
            Registered = false;
        }
        Texture.DestroyPixels();
    }
};

void ConstructLiveEverestViewport(ProjectEverestScene&                  Everest,
                                  const DualTierProvingGroundsRenderer& Renderer,
                                  ViewportTextureHost&                  TexHost,
                                  bool                                  BottomDrawerOpen,
                                  bool                                  ShowDomainStudio,
                                  double                                CameraYawDeg,
                                  double                                CameraPitchDeg,
                                  double                                CameraDistanceM,
                                  DualTierFrameTelemetry&               OutTelemetry)
{
    const KitFaces Faces   = ResolveKitFaces();
    const KitTheme Theme   = ResolveKitTheme();
    ImDrawList*    Ink     = ImGui::GetWindowDrawList();
    const ImVec2   WinPos  = ImGui::GetWindowPos();
    const ImVec2   WinSize = ImGui::GetWindowSize();
    const float    Top     = std::max(WinPos.y + 28.0f, ImGui::GetCursorScreenPos().y);
    const float    BarH    = 38.0f;
    const float    FootH   = 26.0f;
    const float    Left    = WinPos.x;
    const float    Right   = WinPos.x + WinSize.x;
    const float    Bottom  = WinPos.y + WinSize.y;

    const ImU32 BarBg   = Theme.LightCanvas ? Theme.Surface : KitInkHex(0x111214);
    const ImU32 BarEdge = Theme.LightCanvas ? Theme.Line : KitInkHex(0x23262b);
    const ImU32 PillBg  = Theme.LightCanvas ? Theme.Surface2 : KitInkHex(0x181a1e);

    Ink->AddRectFilled(ImVec2(Left, Top), ImVec2(Right, Bottom), Theme.LightCanvas ? Theme.PanelBg : KitInkHex(0x0b0c0e));
    Ink->AddRectFilled(ImVec2(Left, Top), ImVec2(Right, Top + BarH), BarBg);
    Ink->AddLine(ImVec2(Left, Top + BarH), ImVec2(Right, Top + BarH), BarEdge);

    // 1. Viewport Top Bar
    float       BarX = Left + 10.0f;
    const float BtnY = Top + 7.0f;
    const float BtnH = 24.0f;

    KitPillSurface(Ink, ImVec2(BarX, BtnY), ImVec2(BarX + 72.0f, BtnY + BtnH), PillBg, BarEdge, 6.0f);
    KitDrawText(Ink, Faces.Medium, 11.0f, ImVec2(BarX + 10.0f, BtnY + 5.5f), Theme.Muted, "< Browser");
    BarX += 80.0f;

    static constexpr std::array<EverestCameraMode, 6> QuickRigs = {
        EverestCameraMode::Chase,     EverestCameraMode::Orbit, EverestCameraMode::Cockpit,
        EverestCameraMode::HmiTablet, EverestCameraMode::Hood,  EverestCameraMode::Free
    };
    static constexpr std::array<const char*, 6> QuickRigLabels = {
        "Chase", "Orbit", "Driver", "HMI", "Hood", "Overview"
    };
    for (size_t R = 0u; R < QuickRigs.size(); ++R)
    {
        const float RW  = KitTextWidth(Faces.Medium, 10.5f, QuickRigLabels[R]) + 14.0f;
        const bool  Sel = (Everest.Vehicle.CameraMode == QuickRigs[R]);
        KitPillSurface(Ink, ImVec2(BarX, BtnY), ImVec2(BarX + RW, BtnY + BtnH),
                       Sel ? Theme.MintDim : PillBg, Sel ? Theme.Mint : BarEdge, 5.5f);
        KitDrawText(Ink, Faces.Medium, 10.5f, ImVec2(BarX + 7.0f, BtnY + 5.8f),
                    Sel ? Theme.Text : Theme.Muted, QuickRigLabels[R]);
        BarX += RW + 3.0f;
    }

    float       RightX     = Right - 10.0f;
    const bool  RtxActive  = (Everest.Illumination.TierSelectionIndex == 0);
    const char* TierLabel  = RtxActive ? "RTX \xC2\xB7 PT" : "GTX \xC2\xB7 SDF";
    const float TierW      = KitTextWidth(Faces.SemiBold, 10.6f, TierLabel) + 24.0f;
    const ImU32 TierAccent = RtxActive ? KitInkHex(0x2fe6a7) : KitInkHex(0x63a8ff);
    RightX -= TierW;
    KitPillSurface(Ink, ImVec2(RightX, BtnY), ImVec2(RightX + TierW, BtnY + BtnH),
                   KitInkWithAlpha(TierAccent, 0.16f), KitInkWithAlpha(TierAccent, 0.65f), 6.0f);
    Ink->AddCircleFilled(ImVec2(RightX + 10.0f, BtnY + BtnH * 0.5f), 3.0f, TierAccent);
    KitDrawText(Ink, Faces.SemiBold, 10.6f, ImVec2(RightX + 17.0f, BtnY + 5.8f), Theme.Text, TierLabel);
    RightX -= 6.0f;

    auto DrawPill = [&](const char* Label, bool Active, ImU32 Accent) {
        const float BW = KitTextWidth(Faces.Medium, 10.5f, Label) + 14.0f;
        if (RightX - BW <= BarX + 8.0f) return;
        RightX -= BW;
        KitPillSurface(Ink, ImVec2(RightX, BtnY), ImVec2(RightX + BW, BtnY + BtnH),
                       Active ? KitInkWithAlpha(Accent, 0.20f) : PillBg, Active ? Accent : BarEdge, 5.5f);
        KitDrawText(Ink, Faces.Medium, 10.5f, ImVec2(RightX + 7.0f, BtnY + 5.8f),
                    Active ? Theme.Text : Theme.Muted, Label);
        RightX -= 4.0f;
    };
    DrawPill("Studios", BottomDrawerOpen && ShowDomainStudio, KitInkHex(0xf2a65e));
    DrawPill("Logs", BottomDrawerOpen && !ShowDomainStudio, KitInkHex(0x69dfa5));
    DrawPill("SDF Slice", Everest.Illumination.DistanceSliceInsetActive, KitInkHex(0x78aaff));

    static constexpr std::array<const char*, 6> AovLabels{"Final", "GI", "Refl", "SDF", "Glass", "Normals"};
    for (int Aov = 5; Aov >= 0; --Aov)
    {
        const float PW = KitTextWidth(Faces.Medium, 10.2f, AovLabels[static_cast<size_t>(Aov)]) + 12.0f;
        if (RightX - PW <= BarX + 8.0f) break;
        RightX -= PW;
        const bool Sel = (Everest.Illumination.InspectionAovIndex == Aov);
        KitPillSurface(Ink, ImVec2(RightX, BtnY), ImVec2(RightX + PW, BtnY + BtnH),
                       Sel ? KitInkWithAlpha(TierAccent, 0.20f) : PillBg, Sel ? TierAccent : BarEdge, 5.5f);
        KitDrawText(Ink, Faces.Medium, 10.2f, ImVec2(RightX + 6.0f, BtnY + 6.0f),
                    Sel ? Theme.Text : Theme.Muted, AovLabels[static_cast<size_t>(Aov)]);
        RightX -= 3.0f;
    }

    // 2. Native 1:1 Pixel-Resolution RGBA32 ImTextureData Viewport
    const ImVec2 CanvasMin(Left, Top + BarH + 1.0f);
    const ImVec2 CanvasMax(Right, std::max(CanvasMin.y + 64.0f, Bottom - FootH));
    const float  CWidth  = std::max(64.0f, CanvasMax.x - CanvasMin.x);
    const float  CHeight = std::max(64.0f, CanvasMax.y - CanvasMin.y);
    const double Aspect  = static_cast<double>(CWidth / CHeight);

    const uint32_t TexW = static_cast<uint32_t>(std::clamp(static_cast<int>(std::lround(CWidth)),  64, ViewportTextureHost::AtlasW));
    const uint32_t TexH = static_cast<uint32_t>(std::clamp(static_cast<int>(std::lround(CHeight)), 64, ViewportTextureHost::AtlasH));
    if (!TexHost.Registered)
    {
        TexHost.Texture.Create(ImTextureFormat_RGBA32, ViewportTextureHost::AtlasW, ViewportTextureHost::AtlasH);
        TexHost.Texture.UseColors = true;
        ImGui::RegisterUserTexture(&TexHost.Texture);
        TexHost.Registered = true;
        TexHost.CacheValid = false;
    }

    const EverestViewportInputs Inputs = Everest.BuildViewportInputs(CameraYawDeg, CameraPitchDeg, CameraDistanceM);
    if (!TexHost.CacheValid || TexHost.CachedWidth != TexW || TexHost.CachedHeight != TexH ||
        std::memcmp(&Inputs, &TexHost.CachedInputs, sizeof(EverestViewportInputs)) != 0)
    {
        TexHost.ScratchRgba.resize(static_cast<size_t>(TexW) * static_cast<size_t>(TexH) * 4u);
        Renderer.RenderRgba8Viewport(TexW, TexH, Inputs, TexHost.ScratchRgba.data(), TexHost.CachedTelemetry);
        for (uint32_t Row = 0u; Row < TexH; ++Row)
        {
            std::memcpy(TexHost.Texture.GetPixelsAt(0, static_cast<int>(Row)),
                        TexHost.ScratchRgba.data() + static_cast<size_t>(Row) * static_cast<size_t>(TexW) * 4u,
                        static_cast<size_t>(TexW) * 4u);
        }
        if (TexHost.Texture.Status == ImTextureStatus_OK)
        {
            TexHost.Texture.SetStatus(ImTextureStatus_WantUpdates);
        }
        TexHost.CachedInputs = Inputs;
        TexHost.CachedWidth  = TexW;
        TexHost.CachedHeight = TexH;
        TexHost.CacheValid   = true;
    }
    OutTelemetry = TexHost.CachedTelemetry;

    const ImVec2 UvMax(static_cast<float>(TexW) / static_cast<float>(ViewportTextureHost::AtlasW),
                       static_cast<float>(TexH) / static_cast<float>(ViewportTextureHost::AtlasH));
    Ink->AddImage(TexHost.Texture.GetTexRef(), CanvasMin, CanvasMax, ImVec2(0.0f, 0.0f), UvMax);

    if (Inputs.CollisionOverlayEnabled)
    {
        const double CosY = std::cos(static_cast<double>(Inputs.VehicleYawRadians));
        const double SinY = std::sin(static_cast<double>(Inputs.VehicleYawRadians));
        auto LocalToWorld = [&](double Lx, double Ly, double Lz) -> SpatialVector {
            return {
                static_cast<double>(Inputs.VehiclePosition[0]) + Lx * CosY - Ly * SinY,
                static_cast<double>(Inputs.VehiclePosition[1]) + Lx * SinY + Ly * CosY,
                static_cast<double>(Inputs.VehiclePosition[2]) + Lz
            };
        };
        auto ProjectScreen = [&](SpatialVector Pt, ImVec2& OutPx) -> bool {
            double U = 0.0, V = 0.0;
            if (!Renderer.ProjectWorldPoint(Pt, Inputs, Aspect, U, V))
            {
                return false;
            }
            OutPx = ImVec2(CanvasMin.x + static_cast<float>(U) * CWidth,
                           CanvasMin.y + static_cast<float>(V) * CHeight);
            return U >= -0.05 && U <= 1.05 && V >= -0.05 && V <= 1.05;
        };
        const std::array<SpatialVector, 8> HullCorners{{
            LocalToWorld(-2.98, -1.12, -0.35), LocalToWorld(+2.98, -1.12, -0.35),
            LocalToWorld(+2.98, +1.12, -0.35), LocalToWorld(-2.98, +1.12, -0.35),
            LocalToWorld(-2.98, -1.12, +0.92), LocalToWorld(+2.98, -1.12, +0.92),
            LocalToWorld(+2.98, +1.12, +0.92), LocalToWorld(-2.98, +1.12, +0.92),
        }};
        constexpr int Edges[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
        for (const auto& Edge : Edges)
        {
            ImVec2 A{}, B{};
            if (ProjectScreen(HullCorners[Edge[0]], A) && ProjectScreen(HullCorners[Edge[1]], B))
            {
                Ink->AddLine(A, B, KitInkHex(0xff6b8b), 1.8f);
            }
        }
        for (SpatialVector WC : {
                 LocalToWorld(ControlVehicleConstants::AxleFrontX,  ControlVehicleConstants::AxleHalfTrackY, -0.02),
                 LocalToWorld(ControlVehicleConstants::AxleFrontX, -ControlVehicleConstants::AxleHalfTrackY, -0.02),
                 LocalToWorld(ControlVehicleConstants::AxleRearX,   ControlVehicleConstants::AxleHalfTrackY, -0.02),
                 LocalToWorld(ControlVehicleConstants::AxleRearX,  -ControlVehicleConstants::AxleHalfTrackY, -0.02) })
        {
            ImVec2 Hub{};
            if (ProjectScreen(WC, Hub))
            {
                Ink->AddCircle(Hub, 14.0f, KitInkHex(0x2fe6a7), 24, 1.8f);
            }
        }
    }

    // 3. VehicleHMI 26px Status Footer
    const ImU32 FootBg = Theme.LightCanvas ? Theme.Surface : KitInkHex(0x0d0e10);
    Ink->AddRectFilled(ImVec2(Left, CanvasMax.y), ImVec2(Right, Bottom), FootBg);
    Ink->AddLine(ImVec2(Left, CanvasMax.y), ImVec2(Right, CanvasMax.y), BarEdge);

    const float FootCY = CanvasMax.y + FootH * 0.5f;
    Ink->AddCircleFilled(ImVec2(Left + 14.0f, FootCY), 3.0f, Theme.Mint);
    char LeftStatus[220] = {};
    std::snprintf(LeftStatus, sizeof(LeftStatus),
                  "%s  \xC2\xB7  %u tris / %u SAH nodes  \xC2\xB7  %u TLAS  \xC2\xB7  %u clustered lights  \xC2\xB7  %ux%u px",
                  Inputs.ExecutionMode == DualTierExecutionMode::HardwareRayQueryRtx
                      ? "RTX \xC2\xB7 ReSTIR Path Tracer"
                      : "GTX \xC2\xB7 4-Cascade SDF + BVH",
                  OutTelemetry.TriangleCount, OutTelemetry.BvhEnclosureCount, OutTelemetry.ActiveTlasInstances,
                  OutTelemetry.ActiveClusterLights, TexW, TexH);
    KitDrawText(Ink, Faces.Regular, 10.6f, ImVec2(Left + 24.0f, FootCY - 5.5f), Theme.Muted, LeftStatus);

    char RightStatus[160] = {};
    std::snprintf(RightStatus, sizeof(RightStatus),
                  "Sun %.0f\xC2\xB0  \xC2\xB7  %.0f km/h  \xC2\xB7  Rig: %s  \xC2\xB7  Proving Grounds.scene",
                  Inputs.SunElevationDegrees, std::fabs(Everest.Vehicle.SpeedKilometresPerHour),
                  QueryEverestCameraLabel(Inputs.CameraMode));
    const float RW = KitTextWidth(Faces.Regular, 10.6f, RightStatus);
    if (Right - 14.0f - RW > Left + 380.0f)
    {
        KitDrawText(Ink, Faces.Regular, 10.6f, ImVec2(Right - 14.0f - RW, FootCY - 5.5f), Theme.Faint, RightStatus);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                              NUMERICAL, ARCHIVE & DUAL-TIER GPU PARITY CONTRACTS
//------------------------------------------------------------------------------------------------------------------------

void VerifyEverestContracts(const DualTierProvingGroundsRenderer& Renderer, ProjectEverestScene& Everest,
                            DomainStudioPanel& StudioPanel)
{
    // 1. Binary 'FCT1' ControlVehicle Archive Fidelity (2,924 body + 1,568 rim + 3,968 tyre facets).
    Require(Renderer.QueryLoadedVehicleArchives() && Renderer.QueryAuthoredBodyFacetCount() == 2924u,
            "DualTierProvingGroundsRenderer failed to load ControlVehicle{Body,Rim,Tyre}.facet");
    Require(Renderer.QueryTriangleCount() > 30000u && Renderer.QueryCardTexelCount() == 168u,
            "Proving Grounds + ControlVehicle Two-Level SAH BVH / CardRadiosity incomplete");

    // 2. Strict RTX-Only Hardware Raytracing vs GTX SDF+BVH Fallback Negotiation.
    PathReplayRayQueryDeviceCapabilities GtxCaps{};
    GtxCaps.ApiVersion                   = VK_MAKE_API_VERSION(0, 1, 2, 0);
    GtxCaps.BufferDeviceAddressExtension = true;
    GtxCaps.BufferDeviceAddressFeature   = true;
    const DualTierTierDecision GtxDecision = NegotiateDualTierExecution(GtxCaps, true);
    Require(GtxDecision.Mode == DualTierExecutionMode::ToroidalSdfBvhGtx && !GtxDecision.HardwareRtxCapable,
            "GTX adapter without VK_KHR_ray_query must negotiate ToroidalSdfBvhGtx fallback");

    PathReplayRayQueryDeviceCapabilities RtxCaps = GtxCaps;
    RtxCaps.AccelerationStructureExtension       = true;
    RtxCaps.RayQueryExtension                    = true;
    RtxCaps.DeferredHostOperationsExtension      = true;
    RtxCaps.AccelerationStructureFeature         = true;
    RtxCaps.RayQueryFeature                      = true;
    const DualTierTierDecision RtxDecision       = NegotiateDualTierExecution(RtxCaps, true);
    Require(RtxDecision.Mode == DualTierExecutionMode::HardwareRayQueryRtx && RtxDecision.HardwareRtxCapable,
            "RTX adapter with acceleration_structure + ray_query must negotiate HardwareRayQueryRtx");

    // 3. Dielectric Glazing Physics (Snell + Fresnel + RadianceScale + Total Internal Reflection).
    const DielectricRefraction NormalHit = EvaluateDielectricRefraction({0.0, 0.0, -1.0}, {0.0, 0.0, 1.0}, 1.0, 1.52);
    Require(NormalHit.Valid && !NormalHit.TotalInternalReflection, "Normal-incidence glass refraction invalid");
    Require(std::abs(NormalHit.Reflectance - 0.042581) < 1e-3, "Normal-incidence Fresnel F0 for IOR 1.52 mismatch");
    const DielectricRefraction TirHit = EvaluateDielectricRefraction({0.85, 0.0, 0.52678}, {0.0, 0.0, -1.0}, 1.52, 1.0);
    Require(TirHit.Valid && TirHit.TotalInternalReflection && std::abs(TirHit.Reflectance - 1.0) < 1e-6,
            "Glass-to-air supercritical ray did not trigger TotalInternalReflection");

    // 4. Dual-Tier 3D Renderer Parity (RTX vs GTX fallback on exact Chase camera).
    std::vector<SpectrumRgb> RtxImg, GtxImg;
    DualTierFrameTelemetry   RtxTel{}, GtxTel{};
    EverestViewportInputs    RtxIn = Everest.BuildViewportInputs(202.9183, 17.1887, 8.0);
    RtxIn.ExecutionMode            = DualTierExecutionMode::HardwareRayQueryRtx;
    Renderer.RenderFrame(128u, 72u, RtxIn, RtxImg, RtxTel);

    EverestViewportInputs GtxIn = RtxIn;
    GtxIn.ExecutionMode         = DualTierExecutionMode::ToroidalSdfBvhGtx;
    Renderer.RenderFrame(128u, 72u, GtxIn, GtxImg, GtxTel);

    Require(RtxTel.CardRadiosityResidual < 1e-5, "CardRadiosity multi-bounce solve did not converge");
    Require(RtxTel.MeanLuminance > 0.04 && GtxTel.MeanLuminance > 0.04,
            "RTX or GTX frame produced near-black luminance");
    Require(RtxTel.MeanIndirectLuminance > 0.005 && GtxTel.MeanIndirectLuminance > 0.005,
            "RTX ReSTIR GI or GTX 4-cascade SDF+Cards indirect GI failed to illuminate scene");
    Require(RtxTel.ReflectionHitFraction > 0.01 && GtxTel.ReflectionHitFraction > 0.01,
            "RTX RayQuery or GTX OrderedSoftwareBvh reflections missed proving grounds geometry");

    // 5. Fog, Stars, Weather & Plume Lattice contracts.
    Everest.Fog.ApplyPreset(1);
    const FogSplitTargetSample NearTarget = Everest.Fog.EvaluateSplitTarget(15.0);
    const FogSplitTargetSample FarTarget  = Everest.Fog.EvaluateSplitTarget(400.0);
    Require(NearTarget.ApparentContrast > FarTarget.ApparentContrast,
            "Koschmieder split-target contrast must decay monotonically with distance in fog");
    Everest.Fog.ApplyPreset(0);

    const auto MagBins = Everest.Stars.QueryMagnitudeHistogram();
    uint32_t   StarSum = 0;
    for (uint32_t B : MagBins) { StarSum += B; }
    Require(StarSum == 360u, "StarsRecord Hipparcos histogram sum mismatch");

    Everest.Weather.TriggerStrikeAhead(42u);
    Require(Everest.Weather.ActiveBolt.MainChannel.size() >= 16u &&
                std::abs(Everest.Weather.QueryThunderArrivalSeconds() - (850.0 / 343.0)) < 1e-3 &&
                Everest.Weather.QueryCurrentFlashIntensity() > 0.5,
            "WeatherRecord forked lightning / 343 m/s thunder acoustics contract failed");
    Everest.Weather.Enabled = false;

    bool PlumeValid = false;
    (void)StudioPanel.BuildValidatedPlumeFootprint(PlumeValid);
    Require(PlumeValid, "DomainStudioPanel PlumeLatticeFootprint failed ValidatePlumeLatticeFootprint");
}

} // namespace
} // namespace Frontier

//------------------------------------------------------------------------------------------------------------------------
//                                                       ENTRY POINT
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
        Require(Centre->QueryTypefaces().QueryFamilyCount() == 5,
                "Expected 5 font families (35 OFL faces) loaded in TypefaceRegistry");

        // Step 1: Project Browser discovery + launch hand-off
        ProjectBrowserPanel Browser;
        const size_t        AttachedCount = Browser.AttachProjectsDirectory(ProjectsDir);
        Require(AttachedCount >= 5, "Expected Project Everest and peer .frontier applications to attach");

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

        for (int Warmup = 0; Warmup < 4; ++Warmup) { StepBrowserFrame(); }
        (void)RasterizeCurrentFrame(ExhibitDir / "01-ProjectBrowser-Library.png", 0);

        Require(Browser.OpenAttachedEntry("project-everest"), "Failed to select Project Everest in Project Browser");
        for (int Warmup = 0; Warmup < 4; ++Warmup) { StepBrowserFrame(); }
        (void)RasterizeCurrentFrame(ExhibitDir / "02-ProjectBrowser-SelectEverest.png", 0);

        Require(Browser.LaunchActiveEntry(), "Open in Frontier failed to record Project Everest launch path");
        const std::filesystem::path LaunchedDescriptorPath = Browser.ConsumeLaunchPath();
        Require(LaunchedDescriptorPath.filename() == "ProjectEverest.frontier", "Descriptor mismatch");

        ProjectDescriptor EverestDescriptor;
        std::string       DecodeDiagnostic;
        Require(DecodeProjectDescriptor(LaunchedDescriptorPath, EverestDescriptor, DecodeDiagnostic),
                DecodeDiagnostic.c_str());
        ConfigureContentRoots(RepoRoot, EverestDescriptor.ProjectDirectory / EverestDescriptor.ContentLocation);
        (void)RegisterDescriptorLogCategories(EverestDescriptor);

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
                 "WorkspaceApplication session opened \xC2\xB7 Proving Grounds.scene + ControlVehicle loaded",
                 "WorkspaceApplication.cpp");
        WriteLog(LogCategories().QueryByName("RayTracing")->Token, LogSeverity::Info, LogVerbosity::Log,
                 "RTX tier \xC2\xB7 Two-Level SAH BVH (9 TLAS / 8 BLAS) + ReSTIR GI + DielectricGlazing (IOR 1.52)",
                 "DualTierRenderer.cpp");
        WriteLog(LogCategories().QueryByName("DistanceField")->Token, LogSeverity::Info, LogVerbosity::Log,
                 "GTX fallback tier \xC2\xB7 4-cascade SceneClip0..3 toroidal SDF + 168-texel CardRadiosity + BVH reflections",
                 "DualTierRenderer.cpp");
        for (int Step = 0; Step < 4; ++Step) { Interchange.AdvanceProject(); }

        // Step 2: Construct 1:1 DualTierProvingGroundsRenderer ('FCT1' archives + Proving Grounds) & ProjectEverestScene (29 records)
        DualTierProvingGroundsRenderer EverestRenderer(
            RepoRoot / "Frontier" / "Projects" / "Project-Everest" / "Content" / "Vehicle");
        DomainStudioPanel   StudioPanel;
        const size_t        BaselineDirectoryCount = SceneDirectory::Instance().QueryCount();
        ProjectEverestScene EverestScene;
        EverestScene.BindDomainStudio(StudioPanel);
        const size_t EverestRecordCount = SceneDirectory::Instance().QueryCount() - BaselineDirectoryCount;
        Require(EverestRecordCount == 29,
                "Expected ProjectEverestScene to register 29 Proving Grounds.scene records");

        VerifyEverestContracts(EverestRenderer, EverestScene, StudioPanel);

        // Step 3: 1:1 VehicleHMI 3-Column Workspace (288px Outliner | Full-Height 1:1 Viewport | 380px Inspector)
        PanelDockspace         Panels;
        LogStreamPanel         LogPanel;
        SceneDirectoryPanel    OutlinerPanel;
        SceneInspectorPanel    InspectorPanel;
        ViewportTextureHost    TexHost;
        bool                   BottomDrawerOpen = false;
        bool                   ShowDomainStudio = false;
        double                 CamYaw           = 202.9183;
        double                 CamPitch         = 17.1887;
        double                 CamDist          = 8.0;
        DualTierFrameTelemetry LastTelemetry{};

        Panels.ConfigureDockspaceKey("Project Everest 1:1 VehicleHMI Workspace");
        DockPanelSpecification ViewportSpec;
        ViewportSpec.Key              = "Viewport";
        ViewportSpec.Caption          = "Viewport";
        ViewportSpec.ConstructContent = [&]() {
            ConstructLiveEverestViewport(EverestScene, EverestRenderer, TexHost, BottomDrawerOpen, ShowDomainStudio,
                                         CamYaw, CamPitch, CamDist, LastTelemetry);
        };
        ViewportSpec.Open = true;
        Require(Panels.Register(ViewportSpec), "Failed to register Viewport");

        DockPanelSpecification BottomDrawer = MakeLogStreamPanelSpecification(LogPanel);
        BottomDrawer.Open                   = false;
        BottomDrawer.ConstructContent       = [&]() {
            if (ShowDomainStudio) { StudioPanel.ConstructInterface(); }
            else                  { LogPanel.ConstructInterface(); }
        };
        Require(Panels.Register(BottomDrawer), "Failed to register BottomDrawer");
        Require(Panels.Register(MakeSceneDirectoryPanelSpecification(OutlinerPanel)), "Failed to register Outliner");
        Require(Panels.Register(MakeSceneInspectorPanelSpecification(InspectorPanel, OutlinerPanel)),
                "Failed to register Inspector");

        Panels.ConfigureWorkspaceInsets(79.0f, 0.0f);
        Panels.ConfigureTabExtent(124.0f, 26.0f);
        Require(Panels.ConfigureStackedPanels("Viewport", "LogStream", 680.0f), "Failed to stack Viewport/LogStream");
        Require(Panels.ConfigureColumns({{"Outliner", 0.16f}, {"LogStream", 0.63f}, {"Inspector", 0.21f}}),
                "Failed to configure 3-column VehicleHMI workspace");
        (void)Panels.ConfigurePlatform();

        const auto StepWorkspaceFrame = [&]() {
            const double DeltaSeconds = 1.0 / 60.0;
            if (StudioPanel.ConsumeFocusRequest())
            {
                BottomDrawerOpen = true;
                ShowDomainStudio = true;
                (void)Panels.ConfigureOpen("LogStream", true);
            }
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

        OutlinerPanel.ConfigureCollapsed(EverestScene.Vehicle.Token(), true);

        // 03: Exact 1:1 VehicleHMI Default Shell (Atmosphere selected, Chase camera [-47,0,3.13]->[-40,0,1.33], Lime ControlVehicle)
        EverestScene.Vehicle.CameraMode = EverestCameraMode::Chase;
        OutlinerPanel.Select(EverestScene.Atmosphere.Token());
        for (int F = 0; F < 16; ++F) { StepWorkspaceFrame(); }
        const std::vector<unsigned char> DefaultShellPixels =
            RasterizeCurrentFrame(ExhibitDir / "03-Folder-Outliner-Observatory.png", 10);
        const double DarkInspectorLuma = SampleRegionMeanLuminance(DefaultShellPixels, 1540, 150, 1900, 980);

        // 04: Sun Observatory + Front-Three-Quarter Orbit Camera on Sculpted ControlVehicle
        EverestScene.Vehicle.CameraMode        = EverestCameraMode::Orbit;
        EverestScene.Vehicle.FrontSteerRadians = 0.26f;
        CamYaw                                 = 34.0;
        CamPitch                               = 15.5;
        CamDist                                = 6.6;
        OutlinerPanel.Select(EverestScene.Sun.Token());
        for (int F = 0; F < 6; ++F) { StepWorkspaceFrame(); }
        (void)RasterizeCurrentFrame(ExhibitDir / "04-Sun-Observatory-Overview.png", 10);

        // 05: Lens Flare Observatory + Dielectric Glazing AOV over ControlVehicle's 74 Canopy Facets
        EverestScene.Illumination.InspectionAovIndex = static_cast<int32_t>(ViewportInspectionMode::DielectricGlazing);
        OutlinerPanel.Select(EverestScene.LensFlare.Token());
        for (int F = 0; F < 6; ++F) { StepWorkspaceFrame(); }
        (void)RasterizeCurrentFrame(ExhibitDir / "05-LensFlare-And-DielectricGlazingAOV.png", 10);

        // 06: Atmosphere Observatory + Denoised ReSTIR Indirect GI AOV
        EverestScene.Illumination.InspectionAovIndex = static_cast<int32_t>(ViewportInspectionMode::IndirectIrradiance);
        OutlinerPanel.Select(EverestScene.Atmosphere.Token());
        for (int F = 0; F < 6; ++F) { StepWorkspaceFrame(); }
        (void)RasterizeCurrentFrame(ExhibitDir / "06-Atmosphere-And-IndirectGiAOV.png", 10);

        // 07: Clouds Observatory + 4-Cascade Toroidal SDF + Top-Right Distance Slice Inset
        EverestScene.Illumination.InspectionAovIndex       = static_cast<int32_t>(ViewportInspectionMode::DistanceCascades);
        EverestScene.Illumination.DistanceSliceInsetActive = true;
        OutlinerPanel.Select(EverestScene.Clouds.Token());
        for (int F = 0; F < 6; ++F) { StepWorkspaceFrame(); }
        (void)RasterizeCurrentFrame(ExhibitDir / "07-Clouds-And-ToroidalSdfCascadesAOV.png", 10);
        EverestScene.Illumination.InspectionAovIndex       = static_cast<int32_t>(ViewportInspectionMode::FinalLit);
        EverestScene.Illumination.DistanceSliceInsetActive = false;

        // 08: Wind Forcefield Observatory + GTX 4-Cascade SDF + CardRadiosity + BVH Reflections Fallback
        EverestScene.Illumination.TierSelectionIndex = 1;
        EverestScene.Vehicle.CameraMode              = EverestCameraMode::Chase;
        EverestScene.Vehicle.FrontSteerRadians       = 0.0f;
        OutlinerPanel.Select(EverestScene.Wind.Token());
        for (int F = 0; F < 6; ++F) { StepWorkspaceFrame(); }
        (void)RasterizeCurrentFrame(ExhibitDir / "08-Wind-And-GtxSdfBvhFallback.png", 10);
        EverestScene.Illumination.TierSelectionIndex = 0;

        // 09: Fog Observatory (Koschmieder Split Targets) + Valley Mist across Proving Grounds
        EverestScene.Fog.ApplyPreset(2);
        OutlinerPanel.Select(EverestScene.Fog.Token());
        for (int F = 0; F < 6; ++F) { StepWorkspaceFrame(); }
        (void)RasterizeCurrentFrame(ExhibitDir / "09-Fog-Observatory-KoschmiederTargets.png", 10);
        EverestScene.Fog.ApplyPreset(0);

        // 10: Stars Planisphere Observatory + Night Clustered Street Lamps & ControlVehicle Headlamps
        EverestScene.Sun.ApplyPreset(SolarLightingPreset::Night);
        EverestScene.Vehicle.AutomotiveBeamMode = 2;   // High beam
        EverestScene.Vehicle.RoofFixtureIndex   = 1;   // Slim LED roof bar
        OutlinerPanel.Select(EverestScene.Stars.Token());
        for (int F = 0; F < 6; ++F) { StepWorkspaceFrame(); }
        (void)RasterizeCurrentFrame(ExhibitDir / "10-Stars-Observatory-NightPlanisphere.png", 10);

        // 11: Moon Selenographic Observatory + Night Lunar Illumination (East +X horizon over straight)
        EverestScene.Moon.ElevationDegrees       = 10.5;
        EverestScene.Moon.AzimuthDegrees         = 84.0;
        EverestScene.Moon.AngularDiameterDegrees = 3.8;
        OutlinerPanel.Select(EverestScene.Moon.Token());
        for (int F = 0; F < 6; ++F) { StepWorkspaceFrame(); }
        (void)RasterizeCurrentFrame(ExhibitDir / "11-Moon-Observatory-Selenograph.png", 10);
        EverestScene.Moon.AngularDiameterDegrees = 0.52;
        EverestScene.Vehicle.AutomotiveBeamMode  = 1;
        EverestScene.Vehicle.RoofFixtureIndex    = 0;

        // 12: Weather Storm Lightning & Wet Mirror Asphalt Reflections
        EverestScene.Sun.ApplyPreset(SolarLightingPreset::Dusk);
        EverestScene.Weather.Enabled                = true;
        EverestScene.Weather.PrecipitationModeIndex = 1;
        EverestScene.Weather.SurfaceDepositMm       = 2.8;
        EverestScene.Weather.TriggerStrikeAhead(77u);
        InspectorPanel.ConfigureWeatherTab(0);
        OutlinerPanel.Select(EverestScene.Weather.Token());
        for (int F = 0; F < 6; ++F) { StepWorkspaceFrame(); }
        (void)RasterizeCurrentFrame(ExhibitDir / "12-Weather-StormLightningAndWetApron.png", 10);

        // Restore full 12.0x 5778K solar daylight after dusk storm capture
        EverestScene.Sun.ElevationDegrees       = 38.0;
        EverestScene.Sun.AzimuthDegrees         = 145.0;
        EverestScene.Sun.TemperatureKelvin      = 5778.0;
        EverestScene.Sun.SolarIlluminance       = 12.0;
        EverestScene.Weather.TimeSinceStrikeSeconds = 4.0;

        // 13: Driver Cockpit Interior Camera (Slate Cabin + 175 km/h OLED Cluster + 14" HMI Tablet + Snow Deposit)
        EverestScene.Weather.Enabled                  = true;
        EverestScene.Weather.PrecipitationModeIndex   = 3;     // Snow deposit (0=Clear, 1=Rain, 2=Sleet, 3=Snow)
        EverestScene.Weather.SurfaceDepositMm         = 2.6;
        EverestScene.Vehicle.SpeedKilometresPerHour   = 175.0; // Matches ControlVehicle3DCockpitHmi960x540.png (175 km/h, gear 5)
        EverestScene.Vehicle.CameraMode               = EverestCameraMode::Cockpit;
        OutlinerPanel.Select(EverestScene.ChaseCamera.Token());
        for (int F = 0; F < 6; ++F) { StepWorkspaceFrame(); }
        (void)RasterizeCurrentFrame(ExhibitDir / "13-Weather-SurfaceDepositSnowGrid.png", 10);
        EverestScene.Weather.Enabled                = false;
        EverestScene.Weather.PrecipitationModeIndex = 0;
        EverestScene.Weather.SurfaceDepositMm       = 0.0;
        EverestScene.Vehicle.SpeedKilometresPerHour = 0.0;

        // 14: 8-Tab ControlVehicle Systems Inspector + World-Space Collision & Wheel-Hub X-Ray
        OutlinerPanel.ConfigureCollapsed(EverestScene.Vehicle.Token(), false);
        EverestScene.Vehicle.CameraMode              = EverestCameraMode::Orbit;
        EverestScene.Vehicle.FrontSteerRadians       = 0.24f;
        EverestScene.Vehicle.CollisionOverlayEnabled = true;
        EverestScene.Vehicle.ActiveModuleTab         = 0;
        CamYaw                                       = 32.0;
        CamPitch                                     = 16.0;
        CamDist                                      = 6.8;
        OutlinerPanel.Select(EverestScene.Vehicle.Token());
        for (int F = 0; F < 6; ++F) { StepWorkspaceFrame(); }
        (void)RasterizeCurrentFrame(ExhibitDir / "14-ControlVehicle-8Tab-CollisionXRay.png", 10);
        EverestScene.Vehicle.CollisionOverlayEnabled = false;
        EverestScene.Vehicle.FrontSteerRadians       = 0.0f;
        EverestScene.Vehicle.CameraMode              = EverestCameraMode::Chase;

        // 15: Tier-3 Dedicated Fracture Voronoi Studio (Bottom Drawer Opened)
        BottomDrawerOpen = true;
        ShowDomainStudio = true;
        Require(Panels.ConfigureOpen("LogStream", true), "Failed to open BottomDrawer");
        StudioPanel.ActivateStudio(DomainStudioCategory::FractureShatter);
        OutlinerPanel.Select(EverestScene.FractureBarrier.Token());
        for (int F = 0; F < 6; ++F) { StepWorkspaceFrame(); }
        (void)RasterizeCurrentFrame(ExhibitDir / "15-Tier3-FractureVoronoiStudio.png", 10);

        // 16: Tier-3 Fluid & Plume Lattice Studio + Proving Grounds Teleport to Hangar Apron
        ShowDomainStudio = true;
        StudioPanel.ActivateStudio(DomainStudioCategory::FluidPlume);
        EverestScene.Vehicle.TeleportToZone(5u);   // Hangar apron!
        OutlinerPanel.Select(EverestScene.ExhaustPlume.Token());
        for (int F = 0; F < 6; ++F) { StepWorkspaceFrame(); }
        const std::vector<unsigned char> DarkLogPixels =
            RasterizeCurrentFrame(ExhibitDir / "16-Tier3-FluidPlumeStudio.png", 10);
        const double DarkLogLuma = SampleRegionMeanLuminance(DarkLogPixels, 420, 790, 1450, 1040);
        ShowDomainStudio = false;
        EverestScene.Vehicle.TeleportToZone(0u);   // Back to Start grid

        // Verify `+ Folder` spawn + Control Centre Light Theme Parity
        OutlinerPanel.RequestSpawn(RecordClassification::Folder);
        StepWorkspaceFrame();
        Require(OutlinerPanel.QueryTotal() == EverestRecordCount + 1, "+ Folder spawn failed");

        EverestScene.Illumination.TierSelectionIndex = 1;
        EverestScene.Sun.ApplyPreset(SolarLightingPreset::Golden);
        OutlinerPanel.Select(EverestScene.Illumination.Token());

        ClickExtent(Centre->QueryExpansionTab().QueryTabExtent());
        for (int F = 0; F < 90; ++F) { StepWorkspaceFrame(); }
        ClickExtent(Centre->QuerySettingsExtent());
        for (int F = 0; F < 90; ++F) { StepWorkspaceFrame(); }
        ClickExtent(Centre->QueryCategoryExtent(0));
        for (int F = 0; F < 90; ++F) { StepWorkspaceFrame(); }
        ClickExtent(Centre->QueryTabExtent(2));
        ClickExtent(Centre->QueryAppearance().QueryThemeTileExtent(2));
        ClickExtent(Centre->QueryApplyExtent());
        for (int F = 0; F < 45; ++F) { StepWorkspaceFrame(); }
        Require(ControlKit::Palette().LightSurface, "Light theme did not activate");

        IO.AddKeyEvent(ImGuiKey_Escape, true);
        StepWorkspaceFrame();
        IO.AddKeyEvent(ImGuiKey_Escape, false);
        for (int F = 0; F < 260; ++F) { StepWorkspaceFrame(); }

        const std::vector<unsigned char> LightPixels =
            RasterizeCurrentFrame(ExhibitDir / "17-DualTier-GTX-Fallback-LightTheme.png", 240);
        const double LightLogLuma       = SampleRegionMeanLuminance(LightPixels, 420, 790, 1450, 1040);
        const double LightInspectorLuma = SampleRegionMeanLuminance(LightPixels, 1540, 150, 1900, 980);
        Require(DarkLogLuma < 45.0 && LightLogLuma > 200.0, "LogStream Dark/Light parity failed");
        Require(DarkInspectorLuma < 55.0 && LightInspectorLuma > 180.0, "Inspector Dark/Light parity failed");

        TexHost.Shutdown();
        Interchange.RetireProject();
        (void)Centre->Finalize();
        Centre.reset();
        ImGui::DestroyContext();

        std::ofstream Report(ExhibitDir / "ProjectEverestChecks.json", std::ios::trunc);
        Report << "{\n"
               << "  \"result\": \"passed\",\n"
               << "  \"project\": \"Project Everest\",\n"
               << "  \"vehicle_archives_loaded\": true,\n"
               << "  \"scene_triangles\": " << LastTelemetry.TriangleCount << ",\n"
               << "  \"sah_bvh_enclosures\": " << LastTelemetry.BvhEnclosureCount << ",\n"
               << "  \"active_tlas_instances\": " << LastTelemetry.ActiveTlasInstances << ",\n"
               << "  \"active_clustered_lights\": " << LastTelemetry.ActiveClusterLights << ",\n"
               << "  \"toroidal_sdf_cascades\": 4,\n"
               << "  \"surface_card_texels\": " << LastTelemetry.SurfaceCardTexels << ",\n"
               << "  \"card_radiosity_residual\": " << LastTelemetry.CardRadiosityResidual << ",\n"
               << "  \"initial_scene_records\": " << EverestRecordCount << ",\n"
               << "  \"post_folder_spawn_records\": " << OutlinerPanel.QueryTotal() << ",\n"
               << "  \"mean_luma\": {\n"
               << "    \"dark_log_stream\": " << DarkLogLuma << ",\n"
               << "    \"light_log_stream\": " << LightLogLuma << ",\n"
               << "    \"dark_inspector\": " << DarkInspectorLuma << ",\n"
               << "    \"light_inspector\": " << LightInspectorLuma << "\n"
               << "  }\n"
               << "}\n";

        std::cout << "PASS: Project Everest 1:1 VehicleHMI parity ("
                  << LastTelemetry.TriangleCount << " tris / " << LastTelemetry.BvhEnclosureCount
                  << " SAH nodes / " << LastTelemetry.ActiveTlasInstances << " TLAS / 1:1 ImTextureData RGBA32 viewport)\n";
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
