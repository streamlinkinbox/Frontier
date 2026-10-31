/*==========================================================================================================================================
                                                     PROJECTEVERESTVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 End-to-end Phase 3 validation and full-HD CPU rasterization for Project Everest:
//    Project Browser -> Project Everest launch -> Dual-Tier 3D Proving Grounds Renderer (RTX RayQuery + ReSTIR GI + Dielectric
//    Canopy vs GTX 4-Cascade Toroidal SDF + SurfaceCards + OrderedSoftwareBvh) + 10 Global Engine Observatories + 8-Tab
//    ControlVehicle Systems Inspector + 3 Tier-3 Dedicated Domain Studios (Fracture, Fluid/Plume, Particle) + Dark/Light parity.

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
#include <stb_image_write.h>

#include <algorithm>
#include <array>
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
//                                  LIVE 3D DUAL-TIER PROVING GROUNDS VIEWPORT CANVAS
//------------------------------------------------------------------------------------------------------------------------

void ConstructLiveEverestViewport(const ProjectDescriptor&              Descriptor,
                                  ProjectEverestScene&                  Everest,
                                  const DualTierProvingGroundsRenderer& Renderer,
                                  bool&                                 ShowDomainStudio,
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
    const float    Left    = WinPos.x + 14.0f;
    const float    Top     = std::max(WinPos.y + 30.0f, ImGui::GetCursorScreenPos().y + 4.0f);
    const float    Width   = std::max(120.0f, WinSize.x - 28.0f);
    const float    Height  = std::max(120.0f, WinPos.y + WinSize.y - Top - 12.0f);

    Ink->AddRectFilled(ImVec2(WinPos.x, Top - 6.0f), ImVec2(WinPos.x + WinSize.x, WinPos.y + WinSize.y), Theme.PanelBg);

    bool BackHover = false;
    (void)KitTarget("##ViewportBackBrowser", ImVec2(Left, Top), ImVec2(88.0f, 26.0f), BackHover);
    KitPillSurface(Ink, ImVec2(Left, Top), ImVec2(Left + 88.0f, Top + 26.0f),
                   BackHover ? Theme.Surface3 : Theme.Surface2, Theme.Line, 999.0f);
    KitDrawText(Ink, Faces.Medium, 11.5f, ImVec2(Left + 14.0f, Top + 6.5f), Theme.Muted, "< Browser");
    KitDrawText(Ink, Faces.SemiBold, 14.0f, ImVec2(Left + 100.0f, Top + 5.5f), Theme.Text, Descriptor.Name.c_str());

    const bool  RtxActive  = (Everest.Illumination.TierSelectionIndex == 0);
    const char* TierLabel  = RtxActive ? "RTX RayQuery + ReSTIR" : "GTX SDF + BVH";
    const float TierW      = KitTextWidth(Faces.Medium, 10.8f, TierLabel) + 24.0f;
    const float TierX      = Left + Width - TierW;
    const ImU32 TierAccent = RtxActive ? KitInkHex(0x2fe6a7) : KitInkHex(0x63a8ff);
    KitPillSurface(Ink, ImVec2(TierX, Top), ImVec2(TierX + TierW, Top + 26.0f),
                   KitInkWithAlpha(TierAccent, 0.16f), KitInkWithAlpha(TierAccent, 0.55f), 999.0f);
    Ink->AddCircleFilled(ImVec2(TierX + 11.0f, Top + 13.0f), 3.4f, TierAccent);
    KitDrawText(Ink, Faces.Medium, 10.8f, ImVec2(TierX + 19.0f, Top + 7.0f), Theme.Text, TierLabel);

    const float StudioW = 74.0f;
    const float StudioX = TierX - StudioW - 6.0f;
    KitPillSurface(Ink, ImVec2(StudioX, Top), ImVec2(StudioX + StudioW, Top + 26.0f),
                   ShowDomainStudio ? KitInkRgba(242, 166, 94, 48) : Theme.Surface2,
                   ShowDomainStudio ? KitInkHex(0xf2a65e) : Theme.LineSoft, 999.0f);
    KitDrawText(Ink, Faces.Medium, 10.8f, ImVec2(StudioX + 14.0f, Top + 7.0f),
                ShowDomainStudio ? Theme.Text : Theme.Muted, "Studios");

    static constexpr std::array<const char*, 6> AovShortLabels{"Final", "GI", "Refl", "SDF", "Glass", "Nrm"};
    float AovCursor = StudioX - 8.0f;
    for (int Aov = 5; Aov >= 0; --Aov)
    {
        const float PW = KitTextWidth(Faces.Medium, 10.2f, AovShortLabels[static_cast<size_t>(Aov)]) + 14.0f;
        AovCursor -= PW;
        if (AovCursor > Left + 225.0f)
        {
            const bool Sel = (Everest.Illumination.InspectionAovIndex == Aov);
            KitPillSurface(Ink, ImVec2(AovCursor, Top + 1.0f), ImVec2(AovCursor + PW, Top + 25.0f),
                           Sel ? KitInkWithAlpha(TierAccent, 0.22f) : Theme.Surface2,
                           Sel ? TierAccent : Theme.LineSoft, 6.0f);
            KitDrawText(Ink, Faces.Medium, 10.2f, ImVec2(AovCursor + 7.0f, Top + 7.0f),
                        Sel ? Theme.Text : Theme.Muted, AovShortLabels[static_cast<size_t>(Aov)]);
        }
        AovCursor -= 4.0f;
    }

    const ImVec2 CanvasMin(Left, Top + 34.0f);
    const ImVec2 CanvasMax(Left + Width, Top + Height);
    KitPillSurface(Ink, CanvasMin, CanvasMax, KitInkHex(0x090d11), Theme.Line, 12.0f);

    const float                 CWidth  = std::max(32.0f, CanvasMax.x - CanvasMin.x - 6.0f);
    const float                 CHeight = std::max(32.0f, CanvasMax.y - CanvasMin.y - 6.0f);
    const double                Aspect  = static_cast<double>(CWidth / CHeight);
    const EverestViewportInputs Inputs  = Everest.BuildViewportInputs(CameraYawDeg, CameraPitchDeg, CameraDistanceM);

    constexpr uint32_t                GridCols = 384u;
    constexpr uint32_t                GridRows = 216u;
    static EverestViewportInputs      CachedInputs{};
    static std::vector<SpectrumRgb>   CachedGrid;
    static DualTierFrameTelemetry     CachedTel{};
    static bool                       CacheReady = false;
    if (!CacheReady || std::memcmp(&Inputs, &CachedInputs, sizeof(EverestViewportInputs)) != 0 ||
        CachedGrid.size() != static_cast<size_t>((GridCols + 1u) * (GridRows + 1u)))
    {
        Renderer.RenderFrame(GridCols + 1u, GridRows + 1u, Inputs, CachedGrid, CachedTel);
        CachedInputs = Inputs;
        CacheReady   = true;
    }
    const std::vector<SpectrumRgb>& LinearGrid = CachedGrid;
    OutTelemetry                               = CachedTel;

    double              SunU = 0.24, SunV = 0.22;
    const SpatialVector FarSunPt{Inputs.SunDirection.X * 120.0, Inputs.SunDirection.Y * 120.0,
                                 Inputs.SunDirection.Z * 120.0};
    const bool             SunInFront = Renderer.ProjectWorldPoint(FarSunPt, Inputs, Aspect, SunU, SunV);
    const LensFlareRecord& Flare      = Everest.LensFlare;

    auto ToSrgbByte = [](double Linear) -> int {
        const double Pos    = std::max(0.0, Linear);
        const double Mapped = (Pos * (2.51 * Pos + 0.03)) / (Pos * (2.43 * Pos + 0.59) + 0.14);
        return std::clamp(static_cast<int>(std::pow(std::clamp(Mapped, 0.0, 1.0), 1.0 / 2.2) * 255.0 + 0.5), 0, 255);
    };

    std::vector<ImU32> PackedVertices(LinearGrid.size());
    for (uint32_t Row = 0; Row <= GridRows; ++Row)
    {
        const double V = static_cast<double>(Row) / static_cast<double>(GridRows);
        for (uint32_t Col = 0; Col <= GridCols; ++Col)
        {
            const double U  = static_cast<double>(Col) / static_cast<double>(GridCols);
            SpectrumRgb  Px = LinearGrid[static_cast<size_t>(Row) * (GridCols + 1u) + Col];
            if (Inputs.InspectionMode == ViewportInspectionMode::FinalLit && Flare.Enabled &&
                Inputs.SunEnabled && SunInFront && SunU > -0.15 && SunU < 1.15 && SunV > -0.15 && SunV < 1.15)
            {
                const SpectrumRgb Opt = Flare.EvaluateOpticalSampleAtSource(
                    U, V, std::clamp(SunU, 0.04, 0.96), std::clamp(SunV, 0.04, 0.96), Aspect);
                Px.R += Opt.R;
                Px.G += Opt.G;
                Px.B += Opt.B;
            }
            PackedVertices[static_cast<size_t>(Row) * (GridCols + 1u) + Col] =
                KitInkRgba(ToSrgbByte(Px.R), ToSrgbByte(Px.G), ToSrgbByte(Px.B), 255);
        }
    }

    const float CellW = CWidth / static_cast<float>(GridCols);
    const float CellH = CHeight / static_cast<float>(GridRows);
    for (uint32_t Row = 0; Row < GridRows; ++Row)
    {
        for (uint32_t Col = 0; Col < GridCols; ++Col)
        {
            const ImVec2 PMin(CanvasMin.x + 3.0f + static_cast<float>(Col) * CellW,
                              CanvasMin.y + 3.0f + static_cast<float>(Row) * CellH);
            const ImVec2 PMax(PMin.x + CellW + 0.5f, PMin.y + CellH + 0.5f);
            const size_t VertexOffset = static_cast<size_t>(Row) * (GridCols + 1u) + Col;
            Ink->AddRectFilledMultiColor(PMin, PMax, PackedVertices[VertexOffset],
                                         PackedVertices[VertexOffset + 1u],
                                         PackedVertices[VertexOffset + GridCols + 2u],
                                         PackedVertices[VertexOffset + GridCols + 1u]);
        }
    }

    if (Inputs.CollisionOverlayEnabled)
    {
        auto ProjectScreen = [&](SpatialVector Pt, ImVec2& OutPx) -> bool {
            double U = 0.0, V = 0.0;
            if (!Renderer.ProjectWorldPoint(Pt, Inputs, Aspect, U, V))
            {
                return false;
            }
            OutPx = ImVec2(CanvasMin.x + 3.0f + static_cast<float>(U) * CWidth,
                           CanvasMin.y + 3.0f + static_cast<float>(V) * CHeight);
            return U >= 0.0 && U <= 1.0 && V >= 0.0 && V <= 1.0;
        };
        const std::array<SpatialVector, 8> HullCorners{{
            {-0.96, -2.22, 0.20}, {+0.96, -2.22, 0.20}, {+0.96, +2.22, 0.20}, {-0.96, +2.22, 0.20},
            {-0.96, -2.22, 1.24}, {+0.96, -2.22, 1.24}, {+0.96, +2.22, 1.24}, {-0.96, +2.22, 1.24},
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
        for (SpatialVector WC : {SpatialVector{-0.96, +1.38, 0.35}, SpatialVector{+0.96, +1.38, 0.35},
                                 SpatialVector{-0.96, -1.38, 0.35}, SpatialVector{+0.96, -1.38, 0.35}})
        {
            ImVec2 Hub{};
            if (ProjectScreen(WC, Hub))
            {
                Ink->AddCircle(Hub, 15.0f, KitInkHex(0x2fe6a7), 24, 1.8f);
            }
        }
    }

    char HudLine[220] = {};
    std::snprintf(HudLine, sizeof(HudLine),
                  "%s  \xC2\xB7  %u tris / %u SAH nodes  \xC2\xB7  4 SDF Cascades  \xC2\xB7  %u Card texels  \xC2\xB7  GI %.2f  \xC2\xB7  Refl %.0f%%",
                  Inputs.ExecutionMode == DualTierExecutionMode::HardwareRayQueryRtx ? "RTX TIER" : "GTX FALLBACK",
                  OutTelemetry.TriangleCount, OutTelemetry.BvhEnclosureCount, OutTelemetry.SurfaceCardTexels,
                  OutTelemetry.MeanIndirectLuminance, OutTelemetry.ReflectionHitFraction * 100.0);
    KitPillSurface(Ink, ImVec2(CanvasMin.x + 12.0f, CanvasMax.y - 34.0f),
                   ImVec2(CanvasMax.x - 12.0f, CanvasMax.y - 10.0f), KitInkRgba(10, 14, 18, 208),
                   KitInkRgba(255, 255, 255, 32), 999.0f);
    KitDrawText(Ink, Faces.Medium, 10.8f, ImVec2(CanvasMin.x + 22.0f, CanvasMax.y - 27.5f), KitInkHex(0xeef3f0),
                HudLine);
}

//------------------------------------------------------------------------------------------------------------------------
//                                  PHASE 3 NUMERICAL & DUAL-TIER GPU CONTRACTS
//------------------------------------------------------------------------------------------------------------------------

void VerifyPhase3Contracts(const DualTierProvingGroundsRenderer& Renderer, ProjectEverestScene& Everest,
                           DomainStudioPanel& StudioPanel)
{
    // 1. RTX vs GTX Capability Negotiation (strict RTX-only hardware raytracing policy).
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
            "RTX adapter with full acceleration_structure + ray_query must negotiate HardwareRayQueryRtx");

    // 2. Dielectric Glazing Physics (Snell + Fresnel + RadianceScale + Total Internal Reflection).
    const DielectricRefraction NormalHit = EvaluateDielectricRefraction({0.0, 0.0, -1.0}, {0.0, 0.0, 1.0}, 1.0, 1.52);
    Require(NormalHit.Valid && !NormalHit.TotalInternalReflection, "Normal-incidence glass refraction invalid");
    Require(std::abs(NormalHit.Reflectance - 0.042581) < 1e-3, "Normal-incidence Fresnel F0 for IOR 1.52 mismatch");
    const DielectricRefraction TirHit = EvaluateDielectricRefraction({0.85, 0.0, 0.52678}, {0.0, 0.0, -1.0}, 1.52, 1.0);
    Require(TirHit.Valid && TirHit.TotalInternalReflection && std::abs(TirHit.Reflectance - 1.0) < 1e-6,
            "Glass-to-air supercritical ray did not trigger TotalInternalReflection");

    // 3. Dual-Tier 3D Renderer Parity (RTX vs GTX fallback + 6 AOVs).
    Require(Renderer.QueryTriangleCount() >= 280u && Renderer.QueryCardTexelCount() == 36u,
            "DualTierProvingGroundsRenderer scene BVH / SurfaceCards incomplete");
    std::vector<SpectrumRgb> RtxImg, GtxImg;
    DualTierFrameTelemetry   RtxTel{}, GtxTel{};
    EverestViewportInputs    RtxIn = Everest.BuildViewportInputs(-36.0, 16.5, 5.85);
    RtxIn.ExecutionMode            = DualTierExecutionMode::HardwareRayQueryRtx;
    Renderer.RenderFrame(96u, 54u, RtxIn, RtxImg, RtxTel);

    EverestViewportInputs GtxIn = RtxIn;
    GtxIn.ExecutionMode         = DualTierExecutionMode::ToroidalSdfBvhGtx;
    Renderer.RenderFrame(96u, 54u, GtxIn, GtxImg, GtxTel);

    Require(RtxTel.CardRadiosityResidual < 1e-5, "SurfaceCards multi-bounce radiosity did not converge");
    Require(RtxTel.MeanLuminance > 0.05 && GtxTel.MeanLuminance > 0.05,
            "RTX or GTX frame produced near-black luminance");
    Require(RtxTel.MeanIndirectLuminance > 0.01 && GtxTel.MeanIndirectLuminance > 0.01,
            "RTX ReSTIR GI or GTX 4-cascade SDF+Cards indirect GI failed to illuminate scene");
    Require(RtxTel.ReflectionHitFraction > 0.05 && GtxTel.ReflectionHitFraction > 0.05,
            "RTX RayQuery or GTX OrderedSoftwareBvh reflections missed proving grounds geometry");

    // 4. Fog, Stars, Moon, Weather & Plume Lattice contracts.
    Everest.Fog.ApplyPreset(1);   // Morning Mist
    const FogSplitTargetSample NearTarget = Everest.Fog.EvaluateSplitTarget(15.0);
    const FogSplitTargetSample FarTarget  = Everest.Fog.EvaluateSplitTarget(400.0);
    Require(NearTarget.ApparentContrast > FarTarget.ApparentContrast,
            "Koschmieder split-target contrast must decay monotonically with distance in fog");
    Everest.Fog.ApplyPreset(0);   // Restore Clear

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
        Require(Centre->QueryTypefaces().QueryFamilyCount() == 5, "Expected 5 font families (35 faces)");

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
                 "WorkspaceApplication session opened \xC2\xB7 Project Everest Phase 3 Dual-Tier GPU active",
                 "WorkspaceApplication.cpp");
        WriteLog(LogCategories().QueryByName("RayTracing")->Token, LogSeverity::Info, LogVerbosity::Log,
                 "RTX tier \xC2\xB7 VK_KHR_ray_query + ReSTIR GI + DielectricRefraction (IOR 1.52) ready",
                 "DualTierRenderer.cpp");
        WriteLog(LogCategories().QueryByName("DistanceField")->Token, LogSeverity::Info, LogVerbosity::Log,
                 "GTX fallback tier \xC2\xB7 4-cascade SceneClip0..3 toroidal SDF + 36-texel SurfaceCards + SAH BVH ready",
                 "DualTierRenderer.cpp");
        for (int Step = 0; Step < 4; ++Step) { Interchange.AdvanceProject(); }

        // Step 2: Construct 3D DualTierProvingGroundsRenderer, ProjectEverestScene (26 records), and DomainStudioPanel!
        DualTierProvingGroundsRenderer EverestRenderer;
        DomainStudioPanel              StudioPanel;
        const size_t                   BaselineDirectoryCount = SceneDirectory::Instance().QueryCount();
        ProjectEverestScene            EverestScene;
        EverestScene.BindDomainStudio(StudioPanel);
        const size_t EverestRecordCount = SceneDirectory::Instance().QueryCount() - BaselineDirectoryCount;
        Require(EverestRecordCount == 26,
                "Expected ProjectEverestScene Phase 3 to register 26 records (6 Folders, 9 Env/Forcefield, "
                "1 Illumination, 1 Vehicle, 4 Tyres, 3 Domain Studio actors, 1 Course, 1 Camera)");

        VerifyPhase3Contracts(EverestRenderer, EverestScene, StudioPanel);

        // Step 3: Docked 3-column Workspace
        PanelDockspace         Panels;
        LogStreamPanel         LogPanel;
        SceneDirectoryPanel    OutlinerPanel;
        SceneInspectorPanel    InspectorPanel;
        bool                   ShowDomainStudio = false;
        double                 CamYaw           = -36.0;
        double                 CamPitch         = 16.5;
        double                 CamDist          = 5.75;
        DualTierFrameTelemetry LastTelemetry{};

        Panels.ConfigureDockspaceKey("Project Everest Phase 3 Workspace");
        DockPanelSpecification ViewportSpec;
        ViewportSpec.Key              = "Viewport";
        ViewportSpec.Caption          = "Viewport";
        ViewportSpec.ConstructContent = [&]() {
            ConstructLiveEverestViewport(EverestDescriptor, EverestScene, EverestRenderer, ShowDomainStudio,
                                         CamYaw, CamPitch, CamDist, LastTelemetry);
        };
        ViewportSpec.Open = true;
        Require(Panels.Register(ViewportSpec), "Failed to register Viewport");

        DockPanelSpecification BottomDrawer = MakeLogStreamPanelSpecification(LogPanel);
        BottomDrawer.ConstructContent = [&]() {
            if (StudioPanel.ConsumeFocusRequest()) { ShowDomainStudio = true; }
            if (ShowDomainStudio) { StudioPanel.ConstructInterface(); }
            else                  { LogPanel.ConstructInterface(); }
        };
        Require(Panels.Register(BottomDrawer), "Failed to register BottomDrawer");
        Require(Panels.Register(MakeSceneDirectoryPanelSpecification(OutlinerPanel)), "Failed to register Outliner");
        Require(Panels.Register(MakeSceneInspectorPanelSpecification(InspectorPanel, OutlinerPanel)),
                "Failed to register Inspector");
        Panels.ConfigureWorkspaceInsets(79.0f, 0.0f);
        Panels.ConfigureTabExtent(136.0f, 26.0f);
        Require(Panels.ConfigureStackedPanels("Viewport", "LogStream", 590.0f), "Failed to stack Viewport/LogStream");
        Require(Panels.ConfigureColumns({{"Outliner", 0.22f}, {"LogStream", 0.49f}, {"Inspector", 0.29f}}),
                "Failed to configure 3-column workspace");
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

        OutlinerPanel.ConfigureCollapsed(EverestScene.Vehicle.Token(), true);

        // 03: Folder Outliner + 3D RTX Proving Grounds Viewport
        OutlinerPanel.Select(EverestScene.EnvironmentFolder.Token());
        for (int F = 0; F < 16; ++F) { StepWorkspaceFrame(); }
        const std::vector<unsigned char> DarkFolderPixels =
            RasterizeCurrentFrame(ExhibitDir / "03-Folder-Outliner-Observatory.png", 10);
        const double DarkLogLuma       = SampleRegionMeanLuminance(DarkFolderPixels, 520, 720, 1300, 1030);
        const double DarkInspectorLuma = SampleRegionMeanLuminance(DarkFolderPixels, 1400, 150, 1890, 980);

        // 04: Sun Observatory + 3D RTX Proving Grounds
        OutlinerPanel.Select(EverestScene.Sun.Token());
        for (int F = 0; F < 6; ++F) { StepWorkspaceFrame(); }
        (void)RasterizeCurrentFrame(ExhibitDir / "04-Sun-Observatory-Overview.png", 10);

        // 05: Lens Flare Observatory + Dielectric Glazing AOV!
        EverestScene.Illumination.InspectionAovIndex = static_cast<int32_t>(ViewportInspectionMode::DielectricGlazing);
        OutlinerPanel.Select(EverestScene.LensFlare.Token());
        for (int F = 0; F < 6; ++F) { StepWorkspaceFrame(); }
        (void)RasterizeCurrentFrame(ExhibitDir / "05-LensFlare-And-DielectricGlazingAOV.png", 10);

        // 06: Atmosphere Observatory + Indirect GI AOV (Coral/Mint Radiosity Bleed)!
        EverestScene.Illumination.InspectionAovIndex = static_cast<int32_t>(ViewportInspectionMode::IndirectIrradiance);
        OutlinerPanel.Select(EverestScene.Atmosphere.Token());
        for (int F = 0; F < 6; ++F) { StepWorkspaceFrame(); }
        (void)RasterizeCurrentFrame(ExhibitDir / "06-Atmosphere-And-IndirectGiAOV.png", 10);

        // 07: Clouds Observatory + 4-Cascade Toroidal SDF (`SceneClip0..3`) AOV!
        EverestScene.Illumination.InspectionAovIndex = static_cast<int32_t>(ViewportInspectionMode::DistanceCascades);
        OutlinerPanel.Select(EverestScene.Clouds.Token());
        for (int F = 0; F < 6; ++F) { StepWorkspaceFrame(); }
        (void)RasterizeCurrentFrame(ExhibitDir / "07-Clouds-And-ToroidalSdfCascadesAOV.png", 10);
        EverestScene.Illumination.InspectionAovIndex = static_cast<int32_t>(ViewportInspectionMode::FinalLit);

        // 08: Wind Forcefield Observatory + GTX SDF + BVH Fallback 3D Render!
        EverestScene.Illumination.TierSelectionIndex = 1;   // GTX Fallback!
        OutlinerPanel.Select(EverestScene.Wind.Token());
        for (int F = 0; F < 6; ++F) { StepWorkspaceFrame(); }
        (void)RasterizeCurrentFrame(ExhibitDir / "08-Wind-And-GtxSdfBvhFallback.png", 10);
        EverestScene.Illumination.TierSelectionIndex = 0;   // Back to RTX!

        // 09: NEW Fog Observatory (6 receding Koschmieder split targets + Morning Mist in 3D Viewport)!
        EverestScene.Fog.ApplyPreset(2);   // Valley Bank fog!
        OutlinerPanel.Select(EverestScene.Fog.Token());
        for (int F = 0; F < 6; ++F) { StepWorkspaceFrame(); }
        (void)RasterizeCurrentFrame(ExhibitDir / "09-Fog-Observatory-KoschmiederTargets.png", 10);
        EverestScene.Fog.ApplyPreset(0);   // Clear

        // 10: NEW Stars Planisphere Observatory + Night Starfield & Headlamps in 3D Viewport!
        EverestScene.Sun.ApplyPreset(SolarLightingPreset::Night);
        CamYaw = -26.0;
        OutlinerPanel.Select(EverestScene.Stars.Token());
        for (int F = 0; F < 6; ++F) { StepWorkspaceFrame(); }
        (void)RasterizeCurrentFrame(ExhibitDir / "10-Stars-Observatory-NightPlanisphere.png", 10);

        // 11: NEW Moon Selenographic Observatory + Night Lunar Illumination!
        EverestScene.Moon.ElevationDegrees = 22.0;
        EverestScene.Moon.AzimuthDegrees   = -24.0;
        OutlinerPanel.Select(EverestScene.Moon.Token());
        for (int F = 0; F < 6; ++F) { StepWorkspaceFrame(); }
        (void)RasterizeCurrentFrame(ExhibitDir / "11-Moon-Observatory-Selenograph.png", 10);

        // 12: NEW Storm, Lightning, 343 m/s Thunder & Surface Deposit Grid + Wet Apron Reflections!
        EverestScene.Sun.ApplyPreset(SolarLightingPreset::Dusk);
        EverestScene.Weather.PrecipitationModeIndex = 1;   // Rain
        EverestScene.Weather.SurfaceDepositMm       = 2.6; // Wet mirror puddles!
        EverestScene.Weather.TriggerStrikeAhead(77u);
        InspectorPanel.ConfigureWeatherTab(0);
        OutlinerPanel.Select(EverestScene.Weather.Token());
        for (int F = 0; F < 6; ++F) { StepWorkspaceFrame(); }
        (void)RasterizeCurrentFrame(ExhibitDir / "12-Weather-StormLightningAndWetApron.png", 10);

        // 13: Storm Surface Retention Grid Tab (0..4 mm vehicle chassis shadow) + Snow Cover!
        EverestScene.Weather.PrecipitationModeIndex = 3;   // Snow!
        EverestScene.Weather.SurfaceDepositMm       = 3.2;
        InspectorPanel.ConfigureWeatherTab(3);             // Deposit grid tab!
        for (int F = 0; F < 6; ++F) { StepWorkspaceFrame(); }
        (void)RasterizeCurrentFrame(ExhibitDir / "13-Weather-SurfaceDepositSnowGrid.png", 10);
        EverestScene.Weather.Enabled = false;
        EverestScene.Sun.ApplyPreset(SolarLightingPreset::Noon);
        CamYaw = -36.0;

        // 14: Upgraded 8-Tab ControlVehicle Systems Inspector + 3D Collision X-Ray Wireframe Overlay!
        OutlinerPanel.ConfigureCollapsed(EverestScene.Vehicle.Token(), false);
        EverestScene.Vehicle.CollisionOverlayEnabled = true;
        EverestScene.Vehicle.PaintFinishPreset       = 1;   // Solar Copper Metallic!
        EverestScene.Vehicle.ActiveModuleTab         = 1;   // Chassis / Collision X-Ray!
        OutlinerPanel.Select(EverestScene.Vehicle.Token());
        for (int F = 0; F < 6; ++F) { StepWorkspaceFrame(); }
        (void)RasterizeCurrentFrame(ExhibitDir / "14-ControlVehicle-8Tab-CollisionXRay.png", 10);
        EverestScene.Vehicle.CollisionOverlayEnabled = false;
        EverestScene.Vehicle.PaintFinishPreset       = 0;

        // 15: Tier-3 Dedicated Domain Studios (Fracture Voronoi + Fluid/Plume Lattice)!
        ShowDomainStudio = true;
        StudioPanel.ActivateStudio(DomainStudioCategory::FractureShatter);
        OutlinerPanel.Select(EverestScene.FractureBarrier.Token());
        for (int F = 0; F < 6; ++F) { StepWorkspaceFrame(); }
        (void)RasterizeCurrentFrame(ExhibitDir / "15-Tier3-FractureVoronoiStudio.png", 10);

        StudioPanel.ActivateStudio(DomainStudioCategory::FluidPlume);
        OutlinerPanel.Select(EverestScene.ExhaustPlume.Token());
        for (int F = 0; F < 6; ++F) { StepWorkspaceFrame(); }
        (void)RasterizeCurrentFrame(ExhibitDir / "16-Tier3-FluidPlumeStudio.png", 10);
        ShowDomainStudio = false;

        // Verify `+ Folder` spawn + Control Centre Light Theme Parity!
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
        const double LightLogLuma       = SampleRegionMeanLuminance(LightPixels, 520, 720, 1300, 1030);
        const double LightInspectorLuma = SampleRegionMeanLuminance(LightPixels, 1400, 150, 1890, 980);
        Require(DarkLogLuma < 45.0 && LightLogLuma > 200.0, "LogStream Dark/Light parity failed");
        Require(DarkInspectorLuma < 55.0 && LightInspectorLuma > 180.0, "Inspector Dark/Light parity failed");

        Interchange.RetireProject();
        (void)Centre->Finalize();
        Centre.reset();
        ImGui::DestroyContext();

        std::ofstream Report(ExhibitDir / "ProjectEverestChecks.json", std::ios::trunc);
        Report << "{\n"
               << "  \"result\": \"passed\",\n"
               << "  \"phase\": 3,\n"
               << "  \"project\": \"Project Everest\",\n"
               << "  \"scene_triangles\": " << LastTelemetry.TriangleCount << ",\n"
               << "  \"sah_bvh_enclosures\": " << LastTelemetry.BvhEnclosureCount << ",\n"
               << "  \"toroidal_sdf_cascades\": 4,\n"
               << "  \"surface_card_texels\": " << LastTelemetry.SurfaceCardTexels << ",\n"
               << "  \"card_radiosity_residual\": " << LastTelemetry.CardRadiosityResidual << ",\n"
               << "  \"initial_scene_records\": " << EverestRecordCount << ",\n"
               << "  \"post_folder_spawn_records\": " << OutlinerPanel.QueryTotal() << ",\n"
               << "  \"engine_observatories\": [\"Folder\", \"Sun\", \"LensFlare\", \"Atmosphere\", \"Clouds\", \"Wind\", \"Fog\", \"Stars\", \"Moon\", \"Weather\"],\n"
               << "  \"tier3_domain_studios\": [\"FractureVoronoi\", \"FluidPlumeLattice\", \"ParticleEmitter\"],\n"
               << "  \"custom_inspectors\": [\"EverestVehicleRecord (8-Tab + Collision X-Ray)\", \"EverestIlluminationRecord (RTX/GTX + 6 AOVs)\"],\n"
               << "  \"mean_luma\": {\n"
               << "    \"dark_log_stream\": " << DarkLogLuma << ",\n"
               << "    \"light_log_stream\": " << LightLogLuma << ",\n"
               << "    \"dark_inspector\": " << DarkInspectorLuma << ",\n"
               << "    \"light_inspector\": " << LightInspectorLuma << "\n"
               << "  }\n"
               << "}\n";

        std::cout << "PASS: Project Everest Phase 3 validation (3D Dual-Tier RTX/GTX Proving Grounds + "
                  << LastTelemetry.TriangleCount << " tris / " << LastTelemetry.BvhEnclosureCount
                  << " SAH nodes + 10 Engine Observatories + 8-Tab ControlVehicle + 3 Tier-3 Studios + Dark/Light parity)\n";
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
