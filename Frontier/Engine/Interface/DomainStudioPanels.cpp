/*==========================================================================================================================================
                                                        DOMAIN STUDIO PANELS
                                     ------------------------------------------------------------------
             Tier-3 Dedicated Domain Studios: Fracture Voronoi Shatter, Fluid/Plume Eulerian Lattice, and Particle Emitter.
==========================================================================================================================================*/

// 🧩 Tier-3 Dedicated Domain Studios: Fracture Voronoi Shatter, Fluid/Plume Eulerian Lattice, and Particle Emitter.

#include "DomainStudioPanels.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>

#if defined(FRONTIER_WITH_INTERFACE_SDK)
#include "Engine/Interface/PanelDockspace.h"
#include "Engine/Interface/StackRailControls.h"
#include "imgui.h"
#endif

namespace Frontier
{

PlumeLatticeFootprint DomainStudioPanel::BuildValidatedPlumeFootprint(bool& OutValid) const noexcept
{
    PlumeLatticeFootprint Footprint{};
    const uint32_t        CoarseX = static_cast<uint32_t>(std::clamp(FluidPlume.LatticeExtentX / 2, 8, 128));
    const uint32_t        CoarseY = static_cast<uint32_t>(std::clamp(FluidPlume.LatticeExtentY / 2, 8, 128));
    const uint32_t        CoarseZ = static_cast<uint32_t>(std::clamp(FluidPlume.LatticeExtentZ / 2, 8, 128));
    const float           FineDx  = static_cast<float>(std::clamp(FluidPlume.VoxelSizeMetres, 0.01, 2.0));

    Footprint.VelocityExtent      = {CoarseX, CoarseY, CoarseZ, 0u};
    Footprint.ThermoExtent        = {CoarseX * 2u, CoarseY * 2u, CoarseZ * 2u, 1u};
    Footprint.PriorVelocityOrigin = {0, 0, 0, 1};
    Footprint.Lattice             = {FineDx, FineDx * 2.0f, static_cast<float>(FluidPlume.JacobiPressureSweeps),
                                     1.0f / 60.0f};
    Footprint.Forces              = {static_cast<float>(FluidPlume.BuoyancyLift),
                                     static_cast<float>(FluidPlume.VorticityConfinement), 9.81f, 0.0f};
    Footprint.Retention           = {0.985f, 0.990f, 0.975f, 0.960f};
    Footprint.SourceMinimum       = {-0.5f, -0.5f, 0.0f, 0.25f};
    Footprint.SourceMaximum       = {+0.5f, +0.5f, 1.0f, 1.0f};
    Footprint.Combustion          = {static_cast<float>(FluidPlume.CombustionKelvin), 1.0f, 0.5f, 0.1f};
    Footprint.Optical             = {static_cast<float>(FluidPlume.SmokeExtinction), 0.85f, 0.35f, 0.0f};
    Footprint.Sun                 = {0.48f, -0.62f, 0.6203225f, 0.5f};

    try
    {
        ValidatePlumeLatticeFootprint(Footprint, 8u);
        OutValid = true;
    }
    catch (const std::exception&)
    {
        OutValid = false;
    }
    return Footprint;
}

#if defined(FRONTIER_WITH_INTERFACE_SDK)

namespace
{

uint32_t StudioHash(uint32_t Seed) noexcept
{
    Seed ^= Seed >> 16;
    Seed *= 0x7feb352du;
    Seed ^= Seed >> 15;
    Seed *= 0x846ca68bu;
    Seed ^= Seed >> 16;
    return Seed;
}

bool DrawStudioMiniSlider(ImDrawList* Ink, const KitFaces& Faces, const KitTheme& Theme,
                          float Left, float Width, const char* Id, const char* Label, double& Target,
                          double MinVal, double MaxVal, const char* Unit, int Digits, ImU32 Accent)
{
    const ImVec2 Pos    = ImGui::GetCursorScreenPos();
    const float  TrackY = Pos.y + 23.0f;
    KitDrawText(Ink, Faces.Regular, 11.2f, ImVec2(Left, Pos.y + 1.0f), Theme.Muted, Label);
    char Buf[48] = {};
    std::snprintf(Buf, sizeof(Buf), "%.*f %s", Digits, Target, Unit);
    const float TxtW = KitTextWidth(Faces.Medium, 11.2f, Buf);
    KitDrawText(Ink, Faces.Medium, 11.2f, ImVec2(Left + Width - TxtW, Pos.y + 1.0f), Theme.Text, Buf);

    ImGui::SetCursorScreenPos(ImVec2(Left, TrackY - 7.0f));
    ImGui::InvisibleButton(Id, ImVec2(Width, 14.0f));
    bool Changed = false;
    if (ImGui::IsItemActive() && MaxVal > MinVal)
    {
        const double T = std::clamp(static_cast<double>((ImGui::GetIO().MousePos.x - Left) / Width), 0.0, 1.0);
        Target         = MinVal + T * (MaxVal - MinVal);
        Changed        = true;
    }
    const float Frac = static_cast<float>(std::clamp((Target - MinVal) / std::max(1e-6, MaxVal - MinVal), 0.0, 1.0));
    Ink->AddRectFilled(ImVec2(Left, TrackY - 2.0f), ImVec2(Left + Width, TrackY + 2.0f), Theme.Surface2, 2.0f);
    Ink->AddRectFilled(ImVec2(Left, TrackY - 2.0f), ImVec2(Left + Width * Frac, TrackY + 2.0f), Accent, 2.0f);
    Ink->AddCircleFilled(ImVec2(Left + Width * Frac, TrackY), 4.8f, Accent);
    ImGui::SetCursorScreenPos(ImVec2(Left, Pos.y + 34.0f));
    ImGui::Dummy(ImVec2(Width, 0.0f));
    return Changed;
}

} // namespace

void DomainStudioPanel::RegisterWithDockspace(PanelDockspace& Dockspace)
{
    DockPanelSpecification Specification;
    Specification.Key              = "DomainStudio";
    Specification.Caption          = "Domain Studio";
    Specification.ConstructContent = [this]() { ConstructInterface(); };
    Specification.Open             = true;
    Dockspace.Register(Specification);
}

void DomainStudioPanel::ConstructInterface()
{
    if (ImGui::GetCurrentContext() == nullptr)
    {
        return;
    }
    const KitTheme Theme   = ResolveKitTheme();
    const KitFaces Faces   = ResolveKitFaces();
    const ImVec2   WinPos  = ImGui::GetWindowPos();
    const ImVec2   WinSize = ImGui::GetWindowSize();
    const float    PL      = WinPos.x;
    const float    PT      = std::max(WinPos.y, ImGui::GetCursorScreenPos().y - ImGui::GetStyle().WindowPadding.y);
    const float    PR      = PL + WinSize.x;
    const float    PB      = WinPos.y + WinSize.y;
    ImDrawList*    Ink     = ImGui::GetWindowDrawList();

    Ink->AddRectFilled(ImVec2(PL, PT), ImVec2(PR, PB), Theme.PanelBg);
    Ink->AddRect(ImVec2(PL, PT), ImVec2(PR, PB), Theme.PanelEdge);

    // Studio Mode Ribbon (38 px)
    const float BarH = 38.0f;
    Ink->AddRectFilled(ImVec2(PL + 1.0f, PT + 1.0f), ImVec2(PR - 1.0f, PT + BarH), Theme.Surface);
    Ink->AddLine(ImVec2(PL, PT + BarH), ImVec2(PR, PT + BarH), Theme.LineSoft);
    KitDrawText(Ink, Faces.SemiBold, 11.5f, ImVec2(PL + 14.0f, PT + 12.0f), Theme.Muted, "TIER-3 DOMAIN STUDIO");

    static constexpr std::array<const char*, 3> TabNames{
        "Fracture Voronoi", "Fluid & Plume Lattice", "Particle Emitter"};
    static constexpr std::array<ImU32, 3> TabAccents{
        IM_COL32(242, 166, 94, 255), IM_COL32(94, 206, 178, 255), IM_COL32(138, 176, 250, 255)};

    float TabX = PL + 178.0f;
    for (size_t I = 0; I < 3; ++I)
    {
        const float PillW  = KitTextWidth(Faces.Medium, 11.2f, TabNames[I]) + 22.0f;
        const bool  Active = (static_cast<size_t>(ActiveCategory) == I);
        ImGui::SetCursorScreenPos(ImVec2(TabX, PT + 6.5f));
        char BtnId[32] = {};
        std::snprintf(BtnId, sizeof(BtnId), "##StudioTab%zu", I);
        if (ImGui::InvisibleButton(BtnId, ImVec2(PillW, 25.0f)))
        {
            ActiveCategory = static_cast<DomainStudioCategory>(I);
        }
        Ink->AddRectFilled(ImVec2(TabX, PT + 6.5f), ImVec2(TabX + PillW, PT + 31.5f),
                           Active ? IM_COL32(42, 54, 68, 235) : Theme.Surface2, 999.0f);
        Ink->AddRect(ImVec2(TabX, PT + 6.5f), ImVec2(TabX + PillW, PT + 31.5f),
                     Active ? TabAccents[I] : Theme.LineSoft, 999.0f);
        KitDrawText(Ink, Faces.Medium, 11.2f, ImVec2(TabX + 11.0f, PT + 12.5f),
                    Active ? Theme.Text : Theme.Muted, TabNames[I]);
        TabX += PillW + 8.0f;
    }

    // Two-column studio body: Left = live interactive canvas (52%), Right = domain solver controls (48%)!
    const float BodyTop  = PT + BarH + 10.0f;
    const float BodyH    = std::max(60.0f, PB - BodyTop - 10.0f);
    const float CanvasL  = PL + 14.0f;
    const float CanvasW  = std::max(140.0f, (WinSize.x - 40.0f) * 0.52f);
    const float CtrlL    = CanvasL + CanvasW + 14.0f;
    const float CtrlW    = std::max(120.0f, PR - CtrlL - 14.0f);
    const ImU32 Accent   = TabAccents[static_cast<size_t>(ActiveCategory)];

    const ImVec2 CMin(CanvasL, BodyTop);
    const ImVec2 CMax(CanvasL + CanvasW, BodyTop + BodyH);
    Ink->AddRectFilled(CMin, CMax, IM_COL32(11, 15, 22, 255), 8.0f);
    Ink->AddRect(CMin, CMax, Theme.Line, 8.0f);

    if (ActiveCategory == DomainStudioCategory::FractureShatter)
    {
        // Interactive Voronoi Cell Decomposition Preview (click/drag epicentre!)
        ImGui::SetCursorScreenPos(CMin);
        ImGui::InvisibleButton("##VoronoiCanvas", ImVec2(CanvasW, BodyH));
        if (ImGui::IsItemActive())
        {
            Fracture.ImpactEpicentreU = std::clamp(static_cast<double>((ImGui::GetIO().MousePos.x - CMin.x) / CanvasW), 0.08, 0.92);
            Fracture.ImpactEpicentreV = std::clamp(static_cast<double>((ImGui::GetIO().MousePos.y - CMin.y) / BodyH), 0.08, 0.92);
        }

        const int32_t Sites = std::clamp(Fracture.VoronoiSiteCount, 4, 64);
        std::array<ImVec2, 64> SitePts{};
        for (int32_t S = 0; S < Sites; ++S)
        {
            const uint32_t Hx = StudioHash(Fracture.PatternSeed + static_cast<uint32_t>(S) * 977u);
            const uint32_t Hy = StudioHash(Fracture.PatternSeed + static_cast<uint32_t>(S) * 2903u + 19u);
            double         U  = static_cast<double>(Hx & 0xFFFFu) / 65535.0;
            double         V  = static_cast<double>(Hy & 0xFFFFu) / 65535.0;
            U                 = U * (1.0 - Fracture.ClusterRadialBias) + Fracture.ImpactEpicentreU * Fracture.ClusterRadialBias +
                (U - 0.5) * 0.28 * Fracture.ClusterRadialBias;
            V                 = V * (1.0 - Fracture.ClusterRadialBias) + Fracture.ImpactEpicentreV * Fracture.ClusterRadialBias +
                (V - 0.5) * 0.28 * Fracture.ClusterRadialBias;
            SitePts[static_cast<size_t>(S)] = ImVec2(
                CMin.x + 8.0f + static_cast<float>(std::clamp(U, 0.03, 0.97)) * (CanvasW - 16.0f),
                CMin.y + 8.0f + static_cast<float>(std::clamp(V, 0.05, 0.95)) * (BodyH - 16.0f));
        }

        // High-density nearest-site cell mosaic + thin crack boundaries
        const int GridX = 96, GridY = 52;
        const float StepX = (CanvasW - 12.0f) / GridX;
        const float StepY = (BodyH - 26.0f) / GridY;
        for (int GY = 0; GY < GridY; ++GY)
        {
            for (int GX = 0; GX < GridX; ++GX)
            {
                const ImVec2 Sample(CMin.x + 6.0f + (GX + 0.5f) * StepX, CMin.y + 20.0f + (GY + 0.5f) * StepY);
                float Best1 = 1e12f, Best2 = 1e12f;
                int   Owner = 0;
                for (int32_t S = 0; S < Sites; ++S)
                {
                    const float Dx = Sample.x - SitePts[static_cast<size_t>(S)].x;
                    const float Dy = Sample.y - SitePts[static_cast<size_t>(S)].y;
                    const float D2 = Dx * Dx + Dy * Dy;
                    if (D2 < Best1) { Best2 = Best1; Best1 = D2; Owner = S; }
                    else if (D2 < Best2) { Best2 = D2; }
                }
                const bool Crack = (std::sqrt(Best2) - std::sqrt(Best1)) < 2.2f;
                const ImU32 Fill = Crack ? IM_COL32(242, 166, 94, 225)
                                         : IM_COL32(24 + (Owner * 13) % 28, 32 + (Owner * 7) % 24, 44 + (Owner * 17) % 28, 235);
                Ink->AddRectFilled(ImVec2(Sample.x - StepX * 0.5f, Sample.y - StepY * 0.5f),
                                   ImVec2(Sample.x + StepX * 0.5f + 0.4f, Sample.y + StepY * 0.5f + 0.4f), Fill);
            }
        }
        const ImVec2 Epicentre(CMin.x + static_cast<float>(Fracture.ImpactEpicentreU) * CanvasW,
                               CMin.y + static_cast<float>(Fracture.ImpactEpicentreV) * BodyH);
        Ink->AddCircle(Epicentre, 9.0f, Accent, 24, 1.8f);
        KitDrawText(Ink, Faces.Medium, 9.8f, ImVec2(CMin.x + 10.0f, CMin.y + 6.0f), Accent,
                    "VORONOI SHATTER DECOMPOSITION (DRAG IMPACT EPICENTRE)");

        ImGui::SetCursorScreenPos(ImVec2(CtrlL, BodyTop + 2.0f));
        double SitesD = static_cast<double>(Fracture.VoronoiSiteCount);
        if (DrawStudioMiniSlider(Ink, Faces, Theme, CtrlL, CtrlW, "##FracSites", "Voronoi convex cells", SitesD, 4.0, 64.0, "cells", 0, Accent))
        {
            Fracture.VoronoiSiteCount = static_cast<int32_t>(std::lround(SitesD));
        }
        DrawStudioMiniSlider(Ink, Faces, Theme, CtrlL, CtrlW, "##FracBias", "Impact radial clustering", Fracture.ClusterRadialBias, 0.0, 1.0, "", 2, Accent);
        DrawStudioMiniSlider(Ink, Faces, Theme, CtrlL, CtrlW, "##FracImp", "Fracture impulse threshold", Fracture.ImpulseThresholdKn, 1.0, 120.0, "kN", 1, Accent);
        DrawStudioMiniSlider(Ink, Faces, Theme, CtrlL, CtrlW, "##FracGlue", "Internal constraint glue", Fracture.InternalGlueStrength, 0.0, 1.0, "", 2, Accent);
    }
    else if (ActiveCategory == DomainStudioCategory::FluidPlume)
    {
        bool ValidFootprint = false;
        const PlumeLatticeFootprint Footprint = BuildValidatedPlumeFootprint(ValidFootprint);
        const double TotalVoxels = static_cast<double>(Footprint.ThermoExtent[0]) * Footprint.ThermoExtent[1] * Footprint.ThermoExtent[2];
        const double VramMiB     = (TotalVoxels * 28.0) / (1024.0 * 1024.0);

        // Rising buoyant plume slice visualization
        const int GX = 36, GZ = 22;
        const float SX = (CanvasW - 16.0f) / GX;
        const float SZ = (BodyH - 28.0f) / GZ;
        for (int Z = 0; Z < GZ; ++Z)
        {
            const double HFrac = 1.0 - static_cast<double>(Z) / GZ;
            for (int X = 0; X < GX; ++X)
            {
                const double U    = (static_cast<double>(X) + 0.5) / GX - 0.5 - (FluidPlume.CoupleSharedWind ? HFrac * 0.18 : 0.0);
                const double Swirl = 0.06 * FluidPlume.VorticityConfinement * std::sin(HFrac * 15.0 + X * 0.4);
                const double Rad  = 0.06 + 0.24 * HFrac;
                const double Dens = std::exp(-((U + Swirl) * (U + Swirl)) / (Rad * Rad)) * (1.0 - 0.35 * HFrac);
                if (Dens > 0.04)
                {
                    const bool Core = (HFrac < 0.28 && Dens > 0.55);
                    const ImU32 Col = Core ? IM_COL32(255, 176, 82, static_cast<int>(220 * Dens))
                                           : IM_COL32(108, 196, 178, static_cast<int>(185 * Dens));
                    Ink->AddRectFilled(ImVec2(CMin.x + 8.0f + X * SX, CMin.y + 22.0f + Z * SZ),
                                       ImVec2(CMin.x + 8.0f + (X + 1) * SX, CMin.y + 22.0f + (Z + 1) * SZ), Col);
                }
            }
        }
        char HeaderBuf[128] = {};
        std::snprintf(HeaderBuf, sizeof(HeaderBuf), "PLUME LATTICE %ux%ux%u  \xC2\xB7  %.1f MiB VRAM  \xC2\xB7  %s",
                      Footprint.ThermoExtent[0], Footprint.ThermoExtent[1], Footprint.ThermoExtent[2], VramMiB,
                      ValidFootprint ? "VALIDATED" : "INVALID");
        KitDrawText(Ink, Faces.Medium, 9.8f, ImVec2(CMin.x + 10.0f, CMin.y + 6.0f), Accent, HeaderBuf);

        ImGui::SetCursorScreenPos(ImVec2(CtrlL, BodyTop + 2.0f));
        DrawStudioMiniSlider(Ink, Faces, Theme, CtrlL, CtrlW, "##PlumeBuoy", "Buoyant Kelvin lift", FluidPlume.BuoyancyLift, 0.2, 8.0, "m/s2", 2, Accent);
        DrawStudioMiniSlider(Ink, Faces, Theme, CtrlL, CtrlW, "##PlumeVort", "Vorticity confinement", FluidPlume.VorticityConfinement, 0.0, 1.5, "", 2, Accent);
        DrawStudioMiniSlider(Ink, Faces, Theme, CtrlL, CtrlW, "##PlumeJac", "GasPressureProjection sweeps", FluidPlume.JacobiPressureSweeps, 4.0, 64.0, "iter", 0, Accent);
        DrawStudioMiniSlider(Ink, Faces, Theme, CtrlL, CtrlW, "##PlumeExt", "Beer-Lambert extinction", FluidPlume.SmokeExtinction, 0.1, 4.0, "1/m", 2, Accent);
    }
    else
    {
        // Ballistic + SDF-bouncing GPU particle emitter trajectories!
        KitDrawText(Ink, Faces.Medium, 9.8f, ImVec2(CMin.x + 10.0f, CMin.y + 6.0f), Accent,
                    "GPU PARTICLE FOUNTAIN + SDF FLOOR RESTITUTION");
        const float FloorY = CMax.y - 16.0f;
        Ink->AddLine(ImVec2(CMin.x + 10.0f, FloorY), ImVec2(CMax.x - 10.0f, FloorY), Theme.Line, 1.4f);
        const int ArcCount = 26;
        for (int A = 0; A < ArcCount; ++A)
        {
            const double SpreadRad = ((static_cast<double>(A) / (ArcCount - 1) - 0.5) * 2.0) *
                                     (Particle.ConeSpreadDegrees * 3.14159265 / 180.0);
            double PX = 0.0, PZ = 0.05;
            double VX = std::sin(SpreadRad) * Particle.InitialSpeedMps;
            double VZ = std::cos(SpreadRad) * Particle.InitialSpeedMps;
            ImVec2 Prev(CMin.x + CanvasW * 0.5f, FloorY - 4.0f);
            for (int Step = 1; Step <= 28; ++Step)
            {
                const double Dt = Particle.ParticleLifetimeSeconds / 28.0;
                VX *= std::max(0.0, 1.0 - Particle.AerodynamicDrag * Dt);
                VZ = VZ * std::max(0.0, 1.0 - Particle.AerodynamicDrag * Dt) - 9.81 * Dt;
                PX += VX * Dt;
                PZ += VZ * Dt;
                if (PZ < 0.0)
                {
                    PZ = 0.0;
                    VZ = -VZ * Particle.SurfaceRestitution;
                }
                const ImVec2 Cur(std::clamp(CMin.x + CanvasW * 0.5f + static_cast<float>(PX * 14.0), CMin.x + 8.0f, CMax.x - 8.0f),
                                 std::clamp(FloorY - static_cast<float>(PZ * 16.0), CMin.y + 22.0f, FloorY));
                Ink->AddLine(Prev, Cur, IM_COL32(138, 176, 250, 155), 1.2f);
                Prev = Cur;
            }
        }

        ImGui::SetCursorScreenPos(ImVec2(CtrlL, BodyTop + 2.0f));
        DrawStudioMiniSlider(Ink, Faces, Theme, CtrlL, CtrlW, "##PartRate", "Spawn flux", Particle.SpawnRateThousandsPerSec, 0.5, 100.0, "k/s", 1, Accent);
        DrawStudioMiniSlider(Ink, Faces, Theme, CtrlL, CtrlW, "##PartSpd", "Nozzle launch speed", Particle.InitialSpeedMps, 1.0, 30.0, "m/s", 1, Accent);
        DrawStudioMiniSlider(Ink, Faces, Theme, CtrlL, CtrlW, "##PartCone", "Cone half-angle", Particle.ConeSpreadDegrees, 2.0, 85.0, "deg", 0, Accent);
        DrawStudioMiniSlider(Ink, Faces, Theme, CtrlL, CtrlW, "##PartRest", "Toroidal SDF restitution", Particle.SurfaceRestitution, 0.0, 0.95, "", 2, Accent);
    }
}

#else // FRONTIER_WITH_INTERFACE_SDK

void DomainStudioPanel::RegisterWithDockspace(PanelDockspace&) {}
void DomainStudioPanel::ConstructInterface() {}

#endif // FRONTIER_WITH_INTERFACE_SDK

} // namespace Frontier
