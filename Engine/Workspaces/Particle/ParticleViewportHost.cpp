/*============================================================================================================================================
                                                        PARTICLEVIEWPORTHOST.CPP
============================================================================================================================================*/
// 🧩 Implementation of ParticleViewportHost rendering Z-Up studio floor, coupled ForceFields gizmos, live particles & PlaybackScrubber

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Particle/ParticleViewportHost.h"

#include "Engine/SimulationSpace/Particles/ParticleChannelLayouts.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <imgui.h>

namespace Frontier::Workspaces
{

namespace
{

namespace ViewportSurfaceColors
{
    inline constexpr ColorRgba CanvasBackdrop { 11u,  11u,  13u,  255u };
    inline constexpr ColorRgba GridMinor      { 255u, 255u, 255u, 14u  };
    inline constexpr ColorRgba GridMajor      { 255u, 255u, 255u, 28u  };
    inline constexpr ColorRgba AxisX          { 230u, 108u, 108u, 175u };
    inline constexpr ColorRgba AxisY          { 108u, 210u, 134u, 175u };
    inline constexpr ColorRgba AxisZ          { 112u, 166u, 240u, 175u };
    inline constexpr ColorRgba HudBg          { 18u,  18u,  21u,  228u };
    inline constexpr ColorRgba HudBorder      { 255u, 255u, 255u, 24u  };
    inline constexpr ColorRgba AccentGold     { 232u, 194u, 122u, 255u };
    inline constexpr ColorRgba AccentGreen    { 62u,  196u, 109u, 255u };
    inline constexpr ColorRgba InkPrimary     { 242u, 242u, 244u, 255u };
    inline constexpr ColorRgba InkMuted       { 148u, 148u, 156u, 255u };
} // namespace ViewportSurfaceColors

[[nodiscard]] constexpr ColorRgba ConvertRgba(ParticleRgba8 Color) noexcept
{
    return ColorRgba{ Color.Red, Color.Green, Color.Blue, Color.Alpha };
}

void UploadViewportTexturePixels(
    ImTextureData& TargetTex,
    const uint8_t* RgbaPixels,
    uint32_t       WidthPx,
    uint32_t       HeightPx) noexcept
{
    if (RgbaPixels == nullptr || WidthPx == 0u || HeightPx == 0u)
    {
        return;
    }
    const bool NeedsCreate =
        TargetTex.Width != static_cast<int>(WidthPx) ||
        TargetTex.Height != static_cast<int>(HeightPx) ||
        TargetTex.Pixels == nullptr;
    if (NeedsCreate)
    {
        TargetTex.DestroyPixels();
        TargetTex.SetStatus(ImTextureStatus_Destroyed);
        TargetTex.Create(ImTextureFormat_RGBA32, static_cast<int>(WidthPx), static_cast<int>(HeightPx));
    }
    std::memcpy(TargetTex.Pixels, RgbaPixels, static_cast<std::size_t>(WidthPx) * HeightPx * 4u);
    if (!NeedsCreate && TargetTex.Status == ImTextureStatus_OK)
    {
        TargetTex.UpdateRect = { 0u, 0u, static_cast<unsigned short>(WidthPx), static_cast<unsigned short>(HeightPx) };
        TargetTex.SetStatus(ImTextureStatus_WantUpdates);
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE & RENDERING
//------------------------------------------------------------------------------------------------------------------------

ParticleViewportHost::~ParticleViewportHost() noexcept
{
    GpuViewportTexture.DestroyPixels();
}

void ParticleViewportHost::AttachSession(ParticleWorkspaceSession* TargetSession) noexcept
{
    Session = TargetSession;
}

ParticleViewportRenderRequest ParticleViewportHost::BuildGpuRenderRequestForEffect(
    const ParticleEffectDescriptor&                       Effect,
    const SimulationSpace::Particles::ParticleSimulation& Simulation,
    const ParticleCameraRig&                              Camera,
    uint32_t                                              WidthPx,
    uint32_t                                              HeightPx,
    float                                                 TimeSeconds,
    bool                                                  ShowFloorGrid) noexcept
{
    ParticleViewportRenderRequest Req{};
    Req.WidthPx       = std::clamp(WidthPx, 128u, 1920u);
    Req.HeightPx      = std::clamp(HeightPx, 128u, 1080u);
    Req.Camera        = Camera;
    Req.TimeSeconds   = TimeSeconds;
    Req.ActiveCount   = Simulation.QueryActiveCount();
    Req.ShowFloorGrid = ShowFloorGrid;
    Req.SizeScale     = 1.15f;
    Req.EmissiveBoost = 1.25f;

    const bool Additive = (Effect.BlendCategory == ParticleBlendCategory::AdditiveGlow);
    Req.BlendMode       = Additive ? ParticleGpuBlendMode::Additive : ParticleGpuBlendMode::Alpha;

    // Select 3D Oriented Solid profile, Strand Ribbons, or Billboard Silhouette by authoring domain
    switch (Effect.AuthoringDomain)
    {
        case ParticleAuthoringDomain::StrandBotanical:
            Req.RenderStrands        = true;
            Req.RenderBillboards     = true;
            Req.RenderOrientedSolids = false;
            Req.Silhouette           = ParticleGpuSilhouette::SoftDisc;
            Req.RibbonWidthMeters    = std::max(0.025f, Effect.SizeStartMeters * 0.45f);
            break;

        case ParticleAuthoringDomain::FerrofluidFlux:
            Req.RenderOrientedSolids = true;
            Req.SolidProfile         = ParticleSolidProfile::Spike;
            Req.RenderBillboards     = false;
            Req.SizeScale            = 1.35f;
            break;

        case ParticleAuthoringDomain::BoidsFlock:
            Req.RenderOrientedSolids = true;
            Req.SolidProfile         = ParticleSolidProfile::WingedBoid;
            Req.RenderBillboards     = false;
            Req.SizeScale            = 1.45f;
            break;

        case ParticleAuthoringDomain::FractureSeismic:
            Req.RenderOrientedSolids = true;
            Req.SolidProfile         = ParticleSolidProfile::Shard;
            Req.RenderBillboards     = false;
            Req.SizeScale            = 1.25f;
            break;

        case ParticleAuthoringDomain::MoGraphEffector:
            Req.RenderOrientedSolids = true;
            Req.SolidProfile         = ParticleSolidProfile::Cube;
            Req.RenderBillboards     = false;
            Req.SizeScale            = 1.15f;
            break;

        case ParticleAuthoringDomain::EnergyShield:
            Req.RenderOrientedSolids = true;
            Req.SolidProfile         = ParticleSolidProfile::CoinDisc;
            Req.RenderBillboards     = true;
            Req.Silhouette           = ParticleGpuSilhouette::GlowOrb;
            Req.BlendMode            = ParticleGpuBlendMode::Additive;
            Req.EmissiveBoost        = 1.40f;
            break;

        case ParticleAuthoringDomain::FireworksSpectacle:
        case ParticleAuthoringDomain::SpaceDistortion:
            Req.RenderBillboards     = true;
            Req.Silhouette           = ParticleGpuSilhouette::GlowOrb;
            Req.BlendMode            = ParticleGpuBlendMode::Additive;
            Req.EmissiveBoost        = 1.45f;
            break;

        case ParticleAuthoringDomain::TransitionDerez:
            Req.RenderOrientedSolids = true;
            Req.SolidProfile         = ParticleSolidProfile::Shard;
            Req.RenderBillboards     = false;
            Req.BlendMode            = ParticleGpuBlendMode::Alpha;
            Req.SizeScale            = 1.30f;
            break;

        case ParticleAuthoringDomain::GeneralParticle:
        default:
            if (Effect.Silhouette == ParticleVisualSilhouette::CoinDisc || Effect.Silhouette == ParticleVisualSilhouette::LeafOrPaper)
            {
                Req.RenderOrientedSolids = true;
                Req.SolidProfile         = ParticleSolidProfile::CoinDisc;
                Req.RenderBillboards     = false;
                Req.SizeScale            = 1.35f;
            }
            else if (Effect.Silhouette == ParticleVisualSilhouette::CubeShard || Effect.Silhouette == ParticleVisualSilhouette::HailstonePellet)
            {
                Req.RenderOrientedSolids = true;
                Req.SolidProfile         = (Effect.Silhouette == ParticleVisualSilhouette::CubeShard)
                    ? ParticleSolidProfile::Cube
                    : ParticleSolidProfile::Shard;
                Req.RenderBillboards     = false;
                Req.SizeScale            = 1.25f;
            }
            else if (Effect.Silhouette == ParticleVisualSilhouette::ShockwaveRing ||
                     Effect.Silhouette == ParticleVisualSilhouette::EmpPulseDome ||
                     Effect.Silhouette == ParticleVisualSilhouette::RadarOrSonarSweep)
            {
                Req.RenderBillboards = true;
                Req.Silhouette       = ParticleGpuSilhouette::ShockRing;
                Req.BlendMode        = ParticleGpuBlendMode::Additive;
                Req.EmissiveBoost    = 1.15f;
            }
            else if (Effect.Silhouette == ParticleVisualSilhouette::AuroraCurtain)
            {
                Req.RenderBillboards = true;
                Req.Silhouette       = ParticleGpuSilhouette::SparkStreak;
                Req.BlendMode        = ParticleGpuBlendMode::Additive;
                Req.EmissiveBoost    = 0.95f;
            }
            else if (Effect.Silhouette == ParticleVisualSilhouette::VolumetricWaterMist)
            {
                Req.RenderBillboards = true;
                Req.Silhouette       = ParticleGpuSilhouette::SoftDisc;
                Req.BlendMode        = ParticleGpuBlendMode::Alpha;
                Req.EmissiveBoost    = 0.85f;
            }
            else if (Effect.Silhouette == ParticleVisualSilhouette::Streak || Effect.Silhouette == ParticleVisualSilhouette::HologramScanline)
            {
                Req.RenderBillboards     = true;
                Req.Silhouette           = ParticleGpuSilhouette::SparkStreak;
            }
            else
            {
                Req.RenderBillboards     = true;
                Req.Silhouette           = Additive ? ParticleGpuSilhouette::GlowOrb : ParticleGpuSilhouette::SoftDisc;
            }
            break;
    }

    return Req;
}

bool ParticleViewportHost::RenderLiveVulkanViewport(
    PixelSpace& Surface,
    Rectangle2D Bounds,
    bool        ShowFloorGrid) noexcept
{
    if (Session == nullptr || GpuRasterizer == nullptr || !GpuRasterizer->QueryReady())
    {
        return false;
    }
    auto* Executor = Session->AccessVulkanExecutor();
    if (Executor == nullptr || !Executor->QueryDeviceReady())
    {
        return false;
    }

    // Prefer selected visible effect; fallback to first visible effect
    const ParticleEffectDescriptor* TargetEffect = Session->QuerySelectedEffect();
    if (TargetEffect == nullptr || !TargetEffect->Visible)
    {
        TargetEffect = nullptr;
        for (const ParticleEffectDescriptor& Entry : Session->QueryEffects())
        {
            if (Entry.Visible)
            {
                TargetEffect = &Entry;
                break;
            }
        }
    }
    if (TargetEffect == nullptr)
    {
        return false;
    }

    const auto* Sim = Session->QueryEffectSimulation(TargetEffect->UniqueToken);
    if (Sim == nullptr)
    {
        return false;
    }

    // Ensure target effect channels reside on GPU before rasterizing
    if (!Executor->UploadChannelBank(Sim->QueryChannelBank(), LastGpuDiagnostic))
    {
        return false;
    }

    const uint32_t RenderW = std::clamp(static_cast<uint32_t>(std::round(Bounds.Width)), 160u, 1280u);
    const uint32_t RenderH = std::clamp(static_cast<uint32_t>(std::round(Bounds.Height)), 120u, 960u);
    const ParticleViewportRenderRequest Req = BuildGpuRenderRequestForEffect(
        *TargetEffect,
        *Sim,
        Interactor.InspectCameraRig(),
        RenderW,
        RenderH,
        static_cast<float>(Session->QuerySimulationTimeSec()),
        ShowFloorGrid);

    if (!GpuRasterizer->RenderOffscreen(Req, LastGpuDiagnostic))
    {
        return false;
    }

    const auto Pixels = GpuRasterizer->QueryColorRgbaPixels();
    if (Pixels.empty())
    {
        return false;
    }

    UploadViewportTexturePixels(GpuViewportTexture, Pixels.data(), GpuRasterizer->QueryWidthPx(), GpuRasterizer->QueryHeightPx());
    Surface.FillTexturedRectangle(Bounds, &GpuViewportTexture, { 0.0f, 0.0f }, { 1.0f, 1.0f }, 0.0f);
    return true;
}

void ParticleViewportHost::Render(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const PaneInputReading& Input,
    double                  Δt)
{
    Surface.PushClip(Bounds);
    Surface.FillRectangle(Bounds, ViewportSurfaceColors::CanvasBackdrop, 0.0f);

    if (Session == nullptr)
    {
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Medium,
            { Bounds.X + 20.0f, Bounds.Y + 20.0f },
            12.0f,
            ViewportSurfaceColors::InkMuted,
            "No Particle Workspace Session attached.");
        Surface.PopClip();
        return;
    }

    const auto& Prefs = Session->InspectWorkspaceConfiguration().InspectParticlePreferences();
    const float TopHudReserve    = 52.0f;
    const float BottomHudReserve = Prefs.PlaybackScrubberVisible ? (PlaybackScrubber::FullTransportHeight + 16.0f) : 12.0f;
    const Rectangle2D InteractiveCanvas{
        Bounds.X,
        Bounds.Y + TopHudReserve,
        Bounds.Width,
        std::max(40.0f, Bounds.Height - TopHudReserve - BottomHudReserve),
    };

    (void)Interactor.ApplyPointerInput(InteractiveCanvas, Input);

    LastFrameUsedGpuRaster = RenderLiveVulkanViewport(Surface, Bounds, Prefs.FloorGridEnabled);
    if (!LastFrameUsedGpuRaster)
    {
        if (Prefs.FloorGridEnabled)
        {
            RenderFloorGrid(Surface, Bounds);
        }
        RenderParticleSimulations(Surface, Bounds);
    }

    if (Prefs.ForceVectorsEnabled)
    {
        RenderForceFieldGizmos(Surface, Bounds);
    }
    RenderTopHudBar(Surface, Bounds, Input);

    if (Prefs.PlaybackScrubberVisible)
    {
        RenderBottomScrubber(Surface, Bounds, Input, Δt);
    }

    Surface.PopClip();
}

void ParticleViewportHost::RenderFloorGrid(PixelSpace& Surface, Rectangle2D CanvasBounds) const noexcept
{
    constexpr int32_t HalfSpan = 8;
    for (int32_t Coord = -HalfSpan; Coord <= HalfSpan; ++Coord)
    {
        const float     Pos     = static_cast<float>(Coord);
        const bool      MajorAxisLine = (Coord % 4 == 0);
        const ColorRgba Stroke  = MajorAxisLine ? ViewportSurfaceColors::GridMajor : ViewportSurfaceColors::GridMinor;

        Point2D A{}, B{};
        float   Da = 0.0f, Db = 0.0f;
        if (Interactor.ProjectWorldPoint({ Pos, -static_cast<float>(HalfSpan), 0.0f }, CanvasBounds, A, Da) &&
            Interactor.ProjectWorldPoint({ Pos, static_cast<float>(HalfSpan), 0.0f }, CanvasBounds, B, Db))
        {
            Surface.StrokeLine(A, B, (Coord == 0) ? ViewportSurfaceColors::AxisY : Stroke, (Coord == 0) ? 1.4f : 1.0f);
        }
        if (Interactor.ProjectWorldPoint({ -static_cast<float>(HalfSpan), Pos, 0.0f }, CanvasBounds, A, Da) &&
            Interactor.ProjectWorldPoint({ static_cast<float>(HalfSpan), Pos, 0.0f }, CanvasBounds, B, Db))
        {
            Surface.StrokeLine(A, B, (Coord == 0) ? ViewportSurfaceColors::AxisX : Stroke, (Coord == 0) ? 1.4f : 1.0f);
        }
    }

    Point2D Origin{}, TopZ{};
    float   Do = 0.0f, Dz = 0.0f;
    if (Interactor.ProjectWorldPoint({ 0.0f, 0.0f, 0.0f }, CanvasBounds, Origin, Do) &&
        Interactor.ProjectWorldPoint({ 0.0f, 0.0f, 3.0f }, CanvasBounds, TopZ, Dz))
    {
        Surface.StrokeLine(Origin, TopZ, ViewportSurfaceColors::AxisZ, 1.3f);
    }
}

void ParticleViewportHost::RenderForceFieldGizmos(PixelSpace& Surface, Rectangle2D CanvasBounds) const noexcept
{
    for (const auto& Force : Session->QueryForceSources())
    {
        if (!Force.Enabled || !Force.Visible)
        {
            continue;
        }
        Point2D ScreenCenter{};
        float   Depth = 0.0f;
        const Mathematics::Axis3D WorldPos{ Force.CentreMeters.X, Force.CentreMeters.Y, Force.CentreMeters.Z };
        if (!Interactor.ProjectWorldPoint(WorldPos, CanvasBounds, ScreenCenter, Depth))
        {
            continue;
        }

        const ColorRgba Tint       = WorkspaceAppearance::QueryForceModelTint(static_cast<uint32_t>(Force.Model));
        const float     RingRadius = std::clamp(88.0f / std::max(1.0f, Depth), 7.0f, 22.0f);
        Surface.StrokeCircle(ScreenCenter, RingRadius, Tint.ScaledAlpha(0.62f), 1.3f);
        Surface.FillCircle(ScreenCenter, 3.2f, Tint);

        const auto Sample = Session->SampleCoupledForce(Force.CentreMeters);
        const Mathematics::Axis3D ForceVec{
            Sample.AdvectionVelocity.X + Sample.LinearAcceleration.X * 0.25f,
            Sample.AdvectionVelocity.Y + Sample.LinearAcceleration.Y * 0.25f,
            Sample.AdvectionVelocity.Z + Sample.LinearAcceleration.Z * 0.25f,
        };
        const float Mag = std::sqrt(ForceVec.X * ForceVec.X + ForceVec.Y * ForceVec.Y + ForceVec.Z * ForceVec.Z);
        if (Mag > 0.05f)
        {
            const float ArrowScale = std::min(1.6f, 0.25f + Mag * 0.08f) / Mag;
            const Mathematics::Axis3D TipWorld{
                WorldPos.X + ForceVec.X * ArrowScale,
                WorldPos.Y + ForceVec.Y * ArrowScale,
                WorldPos.Z + ForceVec.Z * ArrowScale,
            };
            Point2D TipScreen{};
            float   TipDepth = 0.0f;
            if (Interactor.ProjectWorldPoint(TipWorld, CanvasBounds, TipScreen, TipDepth))
            {
                Surface.StrokeLine(ScreenCenter, TipScreen, Tint.ScaledAlpha(0.85f), 1.5f);
            }
        }
    }
}

void ParticleViewportHost::RenderParticleSimulations(PixelSpace& Surface, Rectangle2D CanvasBounds) const noexcept
{
    using namespace SimulationSpace::Particles;
    const bool ShowBounds = Session->InspectWorkspaceConfiguration().InspectParticlePreferences().BoundsOverlayEnabled;

    for (const ParticleEffectDescriptor& Effect : Session->QueryEffects())
    {
        if (!Effect.Visible)
        {
            continue;
        }
        const ParticleSimulation* Sim = Session->QueryEffectSimulation(Effect.UniqueToken);
        if (Sim == nullptr)
        {
            continue;
        }

        const uint32_t Alive = Sim->QueryActiveCount();
        if (Alive == 0u)
        {
            continue;
        }

        const auto Kinematics = Sim->QueryChannel(ChannelLayouts::Kinematics);
        const auto Appearance = Sim->QueryChannel(ChannelLayouts::Appearance);
        if (Kinematics.empty())
        {
            continue;
        }

        const ColorRgba FallbackColor = ConvertRgba(Effect.ColorA);
        const bool      StreakSilhouette      = (Effect.Silhouette == ParticleVisualSilhouette::Streak);

        for (uint32_t Idx = 0u; Idx < Alive; ++Idx)
        {
            const auto& K = Kinematics[Idx];
            Point2D ScreenPt{};
            float   Depth = 0.0f;
            if (!Interactor.ProjectWorldPoint({ K.PosX, K.PosY, K.PosZ }, CanvasBounds, ScreenPt, Depth))
            {
                continue;
            }
            if (!CanvasBounds.Contains(ScreenPt))
            {
                continue;
            }

            ColorRgba LinearTint = FallbackColor;
            float     WorldSize = Effect.SizeStartMeters;
            if (Idx < Appearance.size())
            {
                const auto& A = Appearance[Idx];
                LinearTint = ColorRgba{
                    static_cast<uint8_t>(std::clamp(A.ColorR * 255.0f, 20.0f, 255.0f)),
                    static_cast<uint8_t>(std::clamp(A.ColorG * 255.0f, 20.0f, 255.0f)),
                    static_cast<uint8_t>(std::clamp(A.ColorB * 255.0f, 20.0f, 255.0f)),
                    static_cast<uint8_t>(std::clamp(A.ColorA * 255.0f, 35.0f, 255.0f)),
                };
                if (A.RadiusMeters > 1.0e-4f)
                {
                    WorldSize = A.RadiusMeters;
                }
            }

            const float ScreenRadius = std::clamp((WorldSize * 210.0f) / std::max(0.5f, Depth), 1.4f, 10.5f);

            if (StreakSilhouette)
            {
                const Mathematics::Axis3D TailWorld{
                    K.PosX - K.VelX * 0.04f,
                    K.PosY - K.VelY * 0.04f,
                    K.PosZ - K.VelZ * 0.04f,
                };
                Point2D TailPt{};
                float   TailDepth = 0.0f;
                if (Interactor.ProjectWorldPoint(TailWorld, CanvasBounds, TailPt, TailDepth))
                {
                    Surface.StrokeLine(TailPt, ScreenPt, LinearTint.ScaledAlpha(0.75f), std::max(1.2f, ScreenRadius * 0.65f));
                }
            }

            if (Effect.BlendCategory == ParticleBlendCategory::AdditiveGlow)
            {
                Surface.FillCircle(ScreenPt, ScreenRadius * 1.9f, LinearTint.ScaledAlpha(0.16f));
            }
            Surface.FillCircle(ScreenPt, ScreenRadius, LinearTint.ScaledAlpha(0.88f));
        }

        if (ShowBounds)
        {
            const ParticleReadbackSummary Summary = Sim->QueryReadbackSummary();
            Point2D BoundsCenter{};
            float   BoundsDepth = 0.0f;
            if (Interactor.ProjectWorldPoint(
                    { Summary.BoundsCenterX, Summary.BoundsCenterY, Summary.BoundsCenterZ },
                    CanvasBounds,
                    BoundsCenter,
                    BoundsDepth))
            {
                const float RadiusPx = std::clamp(
                    (Summary.BoundsRadiusMeters * 460.0f) / std::max(0.5f, BoundsDepth),
                    12.0f,
                    CanvasBounds.Height * 0.45f);
                Surface.StrokeCircle(BoundsCenter, RadiusPx, ViewportSurfaceColors::AccentGold.ScaledAlpha(0.55f), 1.1f);
            }
        }
    }
}

void ParticleViewportHost::RenderTopHudBar(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const PaneInputReading& Input) noexcept
{
    const Rectangle2D Bar{ Bounds.X + 12.0f, Bounds.Y + 10.0f, std::max(180.0f, Bounds.Width - 24.0f), 34.0f };
    Surface.FillRectangle(Bar, ViewportSurfaceColors::HudBg, 17.0f);
    Surface.StrokeRectangle(Bar, ViewportSurfaceColors::HudBorder, 17.0f, 1.0f);

    Surface.FillCircle({ Bar.X + 16.0f, Bar.Center().Y }, 4.0f, ViewportSurfaceColors::AccentGreen);

    char Telemetry[160]{};
    std::snprintf(
        Telemetry,
        sizeof(Telemetry),
        "%s  ·  %u alive  ·  %.1f m/s peak  ·  %zu forces",
        QueryGpuRasterizerReady() ? "Vulkan 1.3 .slang Viewport" : "3D Particle Viewport",
        Session->QueryTotalAliveParticles(),
        Session->QueryPeakParticleSpeedMs(),
        Session->QueryForceSources().size());
    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Medium,
        { Bar.X + 28.0f, Bar.Y + 10.0f },
        std::max(80.0f, Bar.Width - 260.0f),
        11.2f,
        ViewportSurfaceColors::InkPrimary,
        Telemetry);

    auto& Prefs = Session->AccessWorkspaceConfiguration().AccessParticlePreferences();
    const Point2D Cursor{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool Click = Input.PrimaryPointer.Enabled && Input.PrimaryPointer.Pressed;

    auto ConstructToggleCapsule = [&](float RightX, float Width, const char* Label, bool Active) -> bool
    {
        const Rectangle2D Capsule{ RightX - Width, Bar.Y + 5.0f, Width, 24.0f };
        const bool Hover = Input.PrimaryPointer.Enabled && Capsule.Contains(Cursor);
        const ColorRgba Bg = Active
            ? ColorRgba{ 232u, 194u, 122u, 36u }
            : (Hover ? ColorRgba{ 255u, 255u, 255u, 18u } : ColorRgba{ 255u, 255u, 255u, 8u });
        const ColorRgba Stroke = Active ? ViewportSurfaceColors::AccentGold.ScaledAlpha(0.65f) : ViewportSurfaceColors::HudBorder;
        Surface.FillRectangle(Capsule, Bg, 12.0f);
        Surface.StrokeRectangle(Capsule, Stroke, 12.0f, 1.0f);
        const float TextW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 10.2f, Label);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Medium,
            { Capsule.X + (Capsule.Width - TextW) * 0.5f, Capsule.Y + 5.5f },
            10.2f,
            Active ? ViewportSurfaceColors::AccentGold : ViewportSurfaceColors::InkMuted,
            Label);
        return Click && Hover;
    };

    float CursorRight = Bar.Right() - 8.0f;
    if (ConstructToggleCapsule(CursorRight, 56.0f, "Reset", false))
    {
        Interactor.ResetCamera();
    }
    CursorRight -= 62.0f;
    if (ConstructToggleCapsule(CursorRight, 60.0f, "Bounds", Prefs.BoundsOverlayEnabled))
    {
        Prefs.BoundsOverlayEnabled = !Prefs.BoundsOverlayEnabled;
    }
    CursorRight -= 66.0f;
    if (ConstructToggleCapsule(CursorRight, 62.0f, "Forces", Prefs.ForceVectorsEnabled))
    {
        Prefs.ForceVectorsEnabled = !Prefs.ForceVectorsEnabled;
    }
    CursorRight -= 68.0f;
    if (ConstructToggleCapsule(CursorRight, 48.0f, "Grid", Prefs.FloorGridEnabled))
    {
        Prefs.FloorGridEnabled = !Prefs.FloorGridEnabled;
    }
}

void ParticleViewportHost::RenderBottomScrubber(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const PaneInputReading& Input,
    double                  Δt) noexcept
{
    const ParticlePlaybackClock& SessionClock = Session->QueryPlaybackClock();
    ScrubberClock.ElapsedSeconds      = SessionClock.CurrentSeconds;
    ScrubberClock.LoopDurationSeconds = SessionClock.DurationSeconds;
    ScrubberClock.FrameRateHz         = SessionClock.FixedStepHz;
    ScrubberClock.SpeedMultiplier     = SessionClock.PlaybackRate;
    ScrubberClock.Playing             = SessionClock.Playing;
    ScrubberClock.LiveUncached        = true;
    ScrubberClock.ActionLabel         = "Burst";
    ScrubberClock.ActionShortcut      = "B";
    if (const ParticleEffectDescriptor* Active = Session->QuerySelectedEffect())
    {
        ScrubberClock.TrackCaption = Active->Title.c_str();
    }
    else
    {
        ScrubberClock.TrackCaption = "Coupled ForceFields & Particle Studio";
    }

    const Rectangle2D ScrubberBounds{
        Bounds.X + 12.0f,
        Bounds.Bottom() - PlaybackScrubber::FullTransportHeight - 10.0f,
        std::max(260.0f, Bounds.Width - 24.0f),
        PlaybackScrubber::FullTransportHeight,
    };

    const auto Interaction = PlaybackScrubber::ConstructFullTransport(
        Surface,
        ScrubberBounds,
        ScrubberClock,
        Session->QueryScrubbingPlayhead(),
        Input.PrimaryPointer,
        1.0f);

    ParticlePlaybackClock& MutClock = Session->AccessPlaybackClock();
    Session->ConfigureScrubbingPlayhead(Interaction.DraggingPlayhead);

    if (Interaction.PlayPauseToggled)
    {
        MutClock.Playing = !MutClock.Playing;
    }
    if (Interaction.SpeedCycled)
    {
        MutClock.PlaybackRate = Interaction.UpdatedSpeedMultiplier;
    }
    if (Interaction.ActionTriggered)
    {
        if (const ParticleEffectDescriptor* Active = Session->QuerySelectedEffect())
        {
            Session->TriggerEffectBurst(Active->UniqueToken);
        }
    }
    if (Interaction.ResetTriggered)
    {
        Session->ResetPlaybackClock();
    }
    else if (Interaction.Scrubbed)
    {
        Session->SeekPlaybackSeconds(Interaction.UpdatedSeconds);
    }
    else if (Interaction.StepForwardTriggered)
    {
        const float Step = 1.0f / std::max(1.0f, MutClock.FixedStepHz);
        Session->AdvanceSimulationClock(static_cast<double>(Step));
    }
    else if (MutClock.Playing && !Interaction.DraggingPlayhead && Δt > 0.0)
    {
        Session->AdvanceSimulationClock(Δt * static_cast<double>(MutClock.PlaybackRate));
    }
}

void ParticleViewportHost::PresentInImGuiPane(double Δt, float ScaleFactor)
{
    const float  SafeScale = std::max(0.25f, ScaleFactor);
    const ImVec2 WinPos    = ImGui::GetWindowPos();
    const ImVec2 WinSize   = ImGui::GetWindowSize();
    const ImVec2 CursorPos = ImGui::GetCursorScreenPos();
    const float  TopY      = CursorPos.y - ImGui::GetStyle().WindowPadding.y;
    const float  PaneH     = std::max(1.0f, (WinPos.y + WinSize.y) - TopY);

    ImGui::InvisibleButton(
        "##ParticleViewportPaneCanvas",
        ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), std::max(1.0f, ImGui::GetContentRegionAvail().y)),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);

    const Rectangle2D ScreenBounds{ WinPos.x, TopY, WinSize.x, PaneH };
    const Rectangle2D LogicalBounds{ 0.0f, 0.0f, WinSize.x / SafeScale, PaneH / SafeScale };

    PixelSpace       Surface(ImGui::GetWindowDrawList(), { ScreenBounds.X, ScreenBounds.Y }, SafeScale);
    PaneInputReading Input = WorkspaceAppearance::CaptureImGuiPaneInput(ScreenBounds, SafeScale);
    WorkspaceAppearance::EnforcePanePointerOwnership(this, Input);

    Render(Surface, LogicalBounds, Input, Δt);

    WorkspaceAppearance::UpdatePanePointerOwnership(this, ScreenBounds, SafeScale, Input, false);
}

} // namespace Frontier::Workspaces
