/*============================================================================================================================================
                                                      PARTICLEWORKSPACESESSION.CPP
============================================================================================================================================*/
// 🧩 Implementation of ParticleWorkspaceSession coordinating Vulkan 1.3 / CPU particle execution, RevisionTracker, Codecs & Interchange

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Particle/ParticleWorkspaceSession.h"

#include "Engine/Codecs/ParticleCodec.h"
#include "Engine/ColourSpace/ColourPresetRepository.h"
#include "Engine/Interchange/ParticleInterchange.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace Frontier::Workspaces
{

namespace
{

void AppendSoftwarePixelRgba(
    std::vector<uint8_t>& Rgba,
    uint32_t              Width,
    uint32_t              Height,
    int32_t               Px,
    int32_t               Py,
    uint8_t               R,
    uint8_t               G,
    uint8_t               B,
    uint8_t               A) noexcept
{
    if (Px < 0 || Py < 0 || Px >= static_cast<int32_t>(Width) || Py >= static_cast<int32_t>(Height))
    {
        return;
    }
    const std::size_t Off = (static_cast<std::size_t>(Py) * Width + static_cast<std::size_t>(Px)) * 4u;
    const float SrcA = static_cast<float>(A) / 255.0f;
    Rgba[Off + 0u] = static_cast<uint8_t>(std::min(255.0f, static_cast<float>(Rgba[Off + 0u]) + static_cast<float>(R) * SrcA));
    Rgba[Off + 1u] = static_cast<uint8_t>(std::min(255.0f, static_cast<float>(Rgba[Off + 1u]) + static_cast<float>(G) * SrcA));
    Rgba[Off + 2u] = static_cast<uint8_t>(std::min(255.0f, static_cast<float>(Rgba[Off + 2u]) + static_cast<float>(B) * SrcA));
    Rgba[Off + 3u] = 255u;
}

} // namespace

ParticleWorkspaceSession::ParticleWorkspaceSession()
    : ContainerDoc(WorkspaceToken{ 0u, 1u }, "Studio Default", std::filesystem::path("StudioDefault.ParticleSpace"))
{
    (void)ColourCoord.Initialize();
    (void)Extension.Initialize(&ColourCoord, &ExchangeBus, ContainerDoc.QueryToken());
    EnsureSampleDocumentInitialized();
    SyncCameraToDocument();
    RecordRevisionStep("Baseline Studio Default");
    ContainerDoc.AccessRevisionTracker().MarkSavedAtCurrentCursor();
    AppendLog(LogStreamSeverity::Success, "Rig", "Initialized Studio Default particle rig & force field lattice");
    AppendLog(LogStreamSeverity::Notice, "Force", "Coupled 4 bounded vector field generators + ambient wind");
}

void ParticleWorkspaceSession::EnsureSampleDocumentInitialized()
{
    ParticleSpace::ParticleDocument& Doc = Extension.AccessActiveDocument();
    Doc.DocumentTitle  = "Object Surface Pour Ribbons";
    Doc.ActivePresetId = "bokeh-stream-ribbons";
    Doc.AmbientWindMps = { 2.2f, 0.8f, 0.0f };
    Doc.Collider.BallRadiusMeters   = 0.42f;
    Doc.Collider.BallPositionMeters = { 0.0f, 0.0f, 1.15f };
    Doc.Collider.ImpactImpulseScale = 1.0f;

    Doc.Rigs.clear();
    {
        ParticleSpace::ParticleRig Rig{};
        Rig.RigId                   = 1u;
        Rig.RigTitle                = "Object Surface Pour Ribbons";
        Rig.PresetOriginId          = "bokeh-stream-ribbons";
        Rig.Enabled                 = true;
        Rig.Sizing.CapacitySlots    = 3400u;
        Rig.Sizing.ReleaseRatePerSec = 620.0f;
        Rig.Sizing.LifetimeMinSec   = 1.6f;
        Rig.Sizing.LifetimeMaxSec   = 2.8f;
        Rig.Sizing.SizeStartMeters  = 0.055f;
        Rig.Sizing.SizeEndMeters    = 0.014f;
        Rig.Sizing.TurbulenceAmp    = 0.65f;
        Rig.Origin                  = ParticleSpace::SphereVolumeOriginSpec{ { 0.0f, 0.0f, 1.45f }, 1.25f, true };

        ParticleSpace::RibbonStrandCarrier Ribbon{};
        Ribbon.Profile       = ParticleSpace::RibbonProfileCategory::SurfaceStreamPour;
        Ribbon.BlendEquation = ColourSpace::OpticalBlendEquation::Additive;
        Ribbon.WidthMeters   = 0.032f;
        Ribbon.SegmentCount  = 16u;
        Rig.Carrier          = Ribbon;

        if (const auto* Aurora = ColourSpace::FindBuiltInColourRampPreset("aurora"))
        {
            Rig.TintBinding.SourceProfile = ColourSpace::ConstructProfileFromRampPreset(*Aurora);
        }
        Rig.TintBinding.ScalarChannel              = ParticleSpace::ParticleScalarChannel::NormalizedLifetime;
        Rig.Dynamics.Curl.Mode                     = ParticleSpace::CurlManifoldMode::BokehStreamRibbons;
        Rig.Dynamics.Curl.DriveSpeed               = 1.6f;
        Rig.Dynamics.Ballistics.DragPerSecond      = 0.25f;
        Rig.Dynamics.Ballistics.GravityFactor      = 0.05f;
        Rig.Dynamics.ForceLink.WindCouplingRate    = 0.25f;
        Rig.RebuildBakedColourLut();
        Rig.RebuildOriginCaches(Doc.RandomSeed);
        Doc.AddRig(std::move(Rig));
    }

    {
        ParticleSpace::ParticleRig EmberRig{};
        EmberRig.RigId                   = 2u;
        EmberRig.RigTitle                = "Silk Sparks Accent";
        EmberRig.PresetOriginId          = "sparks";
        EmberRig.Enabled                 = true;
        EmberRig.Sizing.CapacitySlots    = 1200u;
        EmberRig.Sizing.ReleaseRatePerSec = 180.0f;
        EmberRig.Sizing.LifetimeMinSec   = 1.1f;
        EmberRig.Sizing.LifetimeMaxSec   = 2.2f;
        EmberRig.Sizing.SizeStartMeters  = 0.042f;
        EmberRig.Sizing.SizeEndMeters    = 0.008f;
        EmberRig.Origin                  = ParticleSpace::SphereVolumeOriginSpec{ { 0.0f, 0.0f, 1.40f }, 0.85f, false };

        ParticleSpace::SpriteBillboardCarrier Sprite{};
        Sprite.CutoutShape   = ParticleSpace::SpriteCutoutCategory::BokehDisc;
        Sprite.BokehAperture = ParticleSpace::BokehApertureCategory::Mixed;
        Sprite.BlendEquation = ColourSpace::OpticalBlendEquation::Additive;
        EmberRig.Carrier     = Sprite;

        if (const auto* Thermal = ColourSpace::FindBuiltInColourRampPreset("thermal"))
        {
            EmberRig.TintBinding.SourceProfile = ColourSpace::ConstructProfileFromRampPreset(*Thermal);
        }
        EmberRig.TintBinding.ScalarChannel           = ParticleSpace::ParticleScalarChannel::VelocitySpeed;
        EmberRig.Dynamics.Ballistics.GravityFactor   = 0.25f;
        EmberRig.Dynamics.Ballistics.BounceRestitution = 0.55f;
        EmberRig.RebuildBakedColourLut();
        EmberRig.RebuildOriginCaches(Doc.RandomSeed + 17u);
        Doc.AddRig(std::move(EmberRig));
    }

    Doc.ForceSources.clear();
    {
        SimulationSpace::ForceFields::ForceSourceRecordEntry Wind{};
        Wind.Label        = "Wind Generator East";
        Wind.Model        = SimulationSpace::ForceFields::ForceModel::LinearWind;
        Wind.Enabled      = true;
        Wind.Strength     = 8.5f;
        Wind.CentreMeters = { -2.2f, 0.0f, 1.5f };
        Wind.EulerDegrees = { 0.0f, 5.0f, 14.0f };
        Wind.RadiusMeters = 3.5f;
        Doc.ForceSources.push_back(Wind);

        SimulationSpace::ForceFields::ForceSourceRecordEntry Vortex{};
        Vortex.Label        = "Vortex Column";
        Vortex.Model        = SimulationSpace::ForceFields::ForceModel::RotationalColumn;
        Vortex.Enabled      = true;
        Vortex.Strength     = 14.5f;
        Vortex.CentreMeters = { 0.0f, 0.0f, 1.4f };
        Vortex.RadiusMeters = 3.0f;
        Doc.ForceSources.push_back(Vortex);

        SimulationSpace::ForceFields::ForceSourceRecordEntry Attractor{};
        Attractor.Label        = "Attractor Core";
        Attractor.Model        = SimulationSpace::ForceFields::ForceModel::Attractor;
        Attractor.Enabled      = true;
        Attractor.Strength     = 9.5f;
        Attractor.CentreMeters = { 0.35f, 0.2f, 1.6f };
        Attractor.RadiusMeters = 2.8f;
        Doc.ForceSources.push_back(Attractor);

        SimulationSpace::ForceFields::ForceSourceRecordEntry Repulsor{};
        Repulsor.Label        = "Repulsor Pulse";
        Repulsor.Model        = SimulationSpace::ForceFields::ForceModel::Repulsor;
        Repulsor.Enabled      = true;
        Repulsor.Strength     = 11.5f;
        Repulsor.CentreMeters = { -0.65f, -0.3f, 1.1f };
        Repulsor.RadiusMeters = 2.2f;
        Doc.ForceSources.push_back(Repulsor);
    }

    SelectedRigId = 1u;
    SoftwareFallback.Initialize(Doc);
}

bool ParticleWorkspaceSession::AttachVulkanInterchange(
    GeometricRaster::VulkanInterchange* BoundDevice,
    const std::filesystem::path&        ShaderDirectory)
{
    if (BoundDevice == nullptr || !BoundDevice->Ready())
    {
        GpuExecutor.TeardownExecutor();
        GpuExecutorFailed = true;
        AppendLog(LogStreamSeverity::Warning, "Compute", "Vulkan device unavailable; active software parity fallback");
        return false;
    }

    if (!GpuExecutor.InitializeExecutor(BoundDevice, Extension.QueryActiveDocument(), ShaderDirectory))
    {
        GpuExecutorFailed = true;
        AppendLog(LogStreamSeverity::Warning, "Compute", "ParticleVulkanExecutor init failed; active software parity fallback");
        return false;
    }

    GpuExecutorFailed = false;
    AppendLog(LogStreamSeverity::Success, "Compute", "Attached Vulkan 1.3 compute & raster executor");
    return true;
}

void ParticleWorkspaceSession::SyncCameraToDocument() noexcept
{
    ParticleSpace::ParticleDocument& Doc = Extension.AccessActiveDocument();
    const float CosP = std::cos(CameraPitchRad);
    Doc.ViewportCamera.TargetMeters = { 0.0f, 0.0f, 1.25f };
    Doc.ViewportCamera.EyePositionMeters = {
        CameraDistanceMeters * CosP * std::sin(CameraYawRad),
        -CameraDistanceMeters * CosP * std::cos(CameraYawRad),
        1.25f + CameraDistanceMeters * std::sin(CameraPitchRad),
    };
    Doc.ViewportCamera.ShowFloorGrid     = true;
    Doc.ViewportCamera.ShowVectorOverlay = (OverlayMode == ParticleOverlayMode::ForceVectors);
}

void ParticleWorkspaceSession::StepSimulation(float DeltaSeconds, uint32_t ViewportWidthPx, uint32_t ViewportHeightPx)
{
    SyncCameraToDocument();
    ParticleSpace::ParticleDocument& Doc = Extension.AccessActiveDocument();
    Doc.PlaybackPaused = !PlaybackTuning.Playing;
    Doc.LoopPlayback   = PlaybackTuning.Looping;
    Doc.TimeScale      = PlaybackTuning.TimeScale;

    const float EffectiveDt = PlaybackTuning.Playing ? std::clamp(DeltaSeconds, 0.001f, 0.05f) * PlaybackTuning.TimeScale : 0.0f;
    if (EffectiveDt > 0.0f)
    {
        Extension.Tick(EffectiveDt);
        PlaybackTuning.ElapsedSeconds = Extension.InspectMetrics().ElapsedClockSeconds;
        if (PlaybackTuning.Looping && RulerSpec.DurationSeconds > 0.1 && PlaybackTuning.ElapsedSeconds >= RulerSpec.DurationSeconds)
        {
            Extension.ResetClock();
            PlaybackTuning.ElapsedSeconds = 0.0;
        }
    }

    RulerSpec.ElapsedSeconds = PlaybackTuning.ElapsedSeconds;
    RulerSpec.StepCounter    = Extension.InspectMetrics().TickCounter;

    const uint32_t TargetW = std::clamp(ViewportWidthPx, 128u, 1600u);
    const uint32_t TargetH = std::clamp(ViewportHeightPx, 128u, 1200u);

    const auto StartClk = std::chrono::steady_clock::now();
    if (GpuExecutor.Ready())
    {
        const ParticleSpace::Execution::ParticleRenderViewportSpec VpSpec{ TargetW, TargetH, true, true, true };
        (void)GpuExecutor.AdvanceRenderStep(Doc, EffectiveDt > 0.0f ? EffectiveDt : 0.0f, VpSpec);
    }
    else
    {
        if (EffectiveDt > 0.0f)
        {
            SoftwareFallback.Step(Doc, EffectiveDt);
        }
        SoftwareWidth  = TargetW;
        SoftwareHeight = TargetH;
        SoftwareRgba.assign(static_cast<std::size_t>(TargetW) * TargetH * 4u, 14u);
        for (std::size_t Off = 0u; Off + 3u < SoftwareRgba.size(); Off += 4u)
        {
            SoftwareRgba[Off + 0u] = 12u;
            SoftwareRgba[Off + 1u] = 14u;
            SoftwareRgba[Off + 2u] = 21u;
            SoftwareRgba[Off + 3u] = 255u;
        }
        for (const auto& Work : SoftwareFallback.QueryWorkspaces())
        {
            for (const ParticleSpace::ParticleSlot& Slot : Work.Slots)
            {
                if (Slot.PositionAge[3] >= Slot.VelocityLifetime[3])
                {
                    continue;
                }
                const float PxF = static_cast<float>(TargetW) * 0.5f + Slot.PositionAge[0] * 68.0f;
                const float PyF = static_cast<float>(TargetH) * 0.62f - Slot.PositionAge[2] * 68.0f;
                const uint8_t R = static_cast<uint8_t>(std::clamp(Slot.ColourRgba[0] * 255.0f, 0.0f, 255.0f));
                const uint8_t G = static_cast<uint8_t>(std::clamp(Slot.ColourRgba[1] * 255.0f, 0.0f, 255.0f));
                const uint8_t B = static_cast<uint8_t>(std::clamp(Slot.ColourRgba[2] * 255.0f, 0.0f, 255.0f));
                AppendSoftwarePixelRgba(SoftwareRgba, TargetW, TargetH, static_cast<int32_t>(PxF), static_cast<int32_t>(PyF), R, G, B, 220u);
                AppendSoftwarePixelRgba(SoftwareRgba, TargetW, TargetH, static_cast<int32_t>(PxF) + 1, static_cast<int32_t>(PyF), R, G, B, 140u);
            }
        }
    }
    const auto EndClk = std::chrono::steady_clock::now();
    StepDurationMs = std::chrono::duration<float, std::milli>(EndClk - StartClk).count();
}

void ParticleWorkspaceSession::TriggerSingleStep(uint32_t ViewportWidthPx, uint32_t ViewportHeightPx)
{
    PlaybackTuning.Playing = false;
    Extension.AccessActiveDocument().PlaybackPaused = false;
    Extension.Tick(1.0 / 60.0);
    PlaybackTuning.ElapsedSeconds = Extension.InspectMetrics().ElapsedClockSeconds;
    RulerSpec.ElapsedSeconds      = PlaybackTuning.ElapsedSeconds;
    RulerSpec.StepCounter         = Extension.InspectMetrics().TickCounter;

    const uint32_t TargetW = std::clamp(ViewportWidthPx, 128u, 1600u);
    const uint32_t TargetH = std::clamp(ViewportHeightPx, 128u, 1200u);
    if (GpuExecutor.Ready())
    {
        const ParticleSpace::Execution::ParticleRenderViewportSpec VpSpec{ TargetW, TargetH, true, true, true };
        (void)GpuExecutor.AdvanceRenderStep(Extension.QueryActiveDocument(), 1.0f / 60.0f, VpSpec);
    }
    else
    {
        SoftwareFallback.Step(Extension.AccessActiveDocument(), 1.0f / 60.0f);
    }
}

void ParticleWorkspaceSession::RestartSimulation()
{
    Extension.ResetClock();
    PlaybackTuning.ElapsedSeconds = 0.0;
    RulerSpec.ElapsedSeconds      = 0.0;
    RulerSpec.StepCounter         = 0u;
    if (GpuExecutor.Ready())
    {
        (void)GpuExecutor.ResetDocument(Extension.QueryActiveDocument());
    }
    else
    {
        SoftwareFallback.Reset(Extension.QueryActiveDocument());
    }
    AppendLog(LogStreamSeverity::Notice, "Rig", "Restarted simulation clock & slot ring");
}

void ParticleWorkspaceSession::TriggerBurst()
{
    if (ParticleSpace::ParticleRig* Rig = AccessSelectedRig())
    {
        Rig->Sizing.ReleaseRatePerSec = std::min(3000.0f, Rig->Sizing.ReleaseRatePerSec + 250.0f);
        Extension.NotifyDocumentEdited("Burst Impulse");
        AppendLog(LogStreamSeverity::Success, "Rig", "Triggered radial burst emission pulse");
    }
}

void ParticleWorkspaceSession::ThrowInteractiveBall()
{
    auto& Collider = Extension.AccessActiveDocument().Collider;
    Collider.BallPositionMeters = { -0.4f, 0.0f, 2.2f };
    Collider.BallVelocityMps    = { 1.8f, 0.3f, -1.2f };
    Collider.BallRadiusMeters   = std::max(0.40f, Collider.BallRadiusMeters);
    AppendLog(LogStreamSeverity::Notice, "Force", "Launched interactive chrome collider sphere");
}

void ParticleWorkspaceSession::RecordRevisionStep(std::string_view OperationTag)
{
    (void)ParticleAuthoringOperations::RecordDocumentSnapshot(
        ContainerDoc.AccessRevisionTracker(),
        Extension.QueryActiveDocument(),
        OperationTag,
        SelectedRigId,
        RevisionStamp++);
}

bool ParticleWorkspaceSession::Undo()
{
    if (!ContainerDoc.AccessRevisionTracker().Undo())
    {
        return false;
    }
    ParticleSpace::ParticleDocument Restored{};
    if (!ParticleAuthoringOperations::ReconstructDocumentAtCursor(ContainerDoc.InspectRevisionTracker(), Restored))
    {
        return false;
    }
    Extension.AccessActiveDocument() = std::move(Restored);
    if (GpuExecutor.Ready())
    {
        (void)GpuExecutor.ResetDocument(Extension.QueryActiveDocument());
    }
    AppendLog(LogStreamSeverity::Notice, "Codec", "Undid authoring step via RevisionTracker");
    return true;
}

bool ParticleWorkspaceSession::Redo()
{
    if (!ContainerDoc.AccessRevisionTracker().Redo())
    {
        return false;
    }
    ParticleSpace::ParticleDocument Restored{};
    if (!ParticleAuthoringOperations::ReconstructDocumentAtCursor(ContainerDoc.InspectRevisionTracker(), Restored))
    {
        return false;
    }
    Extension.AccessActiveDocument() = std::move(Restored);
    if (GpuExecutor.Ready())
    {
        (void)GpuExecutor.ResetDocument(Extension.QueryActiveDocument());
    }
    AppendLog(LogStreamSeverity::Notice, "Codec", "Redid authoring step via RevisionTracker");
    return true;
}

bool ParticleWorkspaceSession::LoadParticleSpaceDocument(const std::filesystem::path& SourcePath, std::string& OutDiagnostic)
{
    ParticleSpace::ParticleDocument Loaded{};
    if (!Codecs::ParticleDecoder::DecodeFromFile(SourcePath, Loaded, OutDiagnostic))
    {
        AppendLog(LogStreamSeverity::Error, "Codec", OutDiagnostic);
        return false;
    }
    Extension.AccessActiveDocument() = std::move(Loaded);
    ContainerDoc.AssignCanonicalPath(SourcePath);
    if (!Extension.QueryActiveDocument().Rigs.empty())
    {
        SelectedRigId = Extension.QueryActiveDocument().Rigs.front().RigId;
    }
    RestartSimulation();
    RecordRevisionStep("Load .ParticleSpace");
    AppendLog(LogStreamSeverity::Success, "Codec", "Loaded " + SourcePath.filename().string());
    return true;
}

bool ParticleWorkspaceSession::SaveParticleSpaceDocument(const std::filesystem::path& DestPath, std::string& OutDiagnostic)
{
    if (!Codecs::ParticleEncoder::EncodeToFile(DestPath, Extension.QueryActiveDocument(), OutDiagnostic, &ContainerDoc.InspectRevisionTracker()))
    {
        AppendLog(LogStreamSeverity::Error, "Codec", OutDiagnostic);
        return false;
    }
    ContainerDoc.AssignCanonicalPath(DestPath);
    ContainerDoc.AccessRevisionTracker().MarkSavedAtCurrentCursor();
    AppendLog(LogStreamSeverity::Success, "Codec", "Saved " + DestPath.filename().string());
    return true;
}

bool ParticleWorkspaceSession::ImportExternalSnapshot(const std::filesystem::path& SourcePath, std::string& OutDiagnostic)
{
    std::ifstream InFile(SourcePath);
    if (!InFile.is_open())
    {
        OutDiagnostic = "Failed to open snapshot: " + SourcePath.string();
        return false;
    }
    std::ostringstream Ss;
    Ss << InFile.rdbuf();

    ParticleSpace::ParticleDocument Imported{};
    if (!Interchange::ParticleInterchange::ImportHtmlStudioSceneJson(Ss.str(), Imported, OutDiagnostic))
    {
        AppendLog(LogStreamSeverity::Error, "Codec", OutDiagnostic);
        return false;
    }
    Extension.AccessActiveDocument() = std::move(Imported);
    if (!Extension.QueryActiveDocument().Rigs.empty())
    {
        SelectedRigId = Extension.QueryActiveDocument().Rigs.front().RigId;
    }
    RestartSimulation();
    RecordRevisionStep("Import Snapshot");
    AppendLog(LogStreamSeverity::Success, "Codec", "Imported snapshot " + SourcePath.filename().string());
    return true;
}

bool ParticleWorkspaceSession::ExportHtmlTomlSnapshot(const std::filesystem::path& DestPath, std::string& OutDiagnostic)
{
    const std::string JsonText = Interchange::ParticleInterchange::ExportHtmlStudioSceneJson(Extension.QueryActiveDocument());
    std::ofstream OutFile(DestPath);
    if (!OutFile.is_open())
    {
        OutDiagnostic = "Failed to write snapshot: " + DestPath.string();
        AppendLog(LogStreamSeverity::Error, "Codec", OutDiagnostic);
        return false;
    }
    OutFile << JsonText;
    AppendLog(LogStreamSeverity::Success, "Codec", "Exported snapshot " + DestPath.filename().string());
    return true;
}

void ParticleWorkspaceSession::SelectRig(uint32_t RigId) noexcept
{
    SelectedRigId   = RigId;
    SelectionTarget = ParticleSelectionTarget::Rig;
}

void ParticleWorkspaceSession::SelectForceSource(std::size_t ForceIndex) noexcept
{
    SelectedForceIndex = ForceIndex;
    SelectionTarget    = ParticleSelectionTarget::ForceSource;
}

void ParticleWorkspaceSession::AssignActiveRigColourProfile(const ColourSpace::ColourSourceProfile& Profile, std::string_view Label)
{
    if (ParticleSpace::ParticleRig* Rig = AccessSelectedRig())
    {
        Rig->TintBinding.SourceProfile = Profile;
        Rig->RebuildBakedColourLut();
        if (GpuExecutor.Ready())
        {
            (void)GpuExecutor.ResetDocument(Extension.QueryActiveDocument());
        }
        RecordRevisionStep("Assign Colour Ramp");
        AppendLog(LogStreamSeverity::Success, "Rig", std::string("Assigned colour ramp: ") + std::string(Label));
    }
}

void ParticleWorkspaceSession::AppendLog(LogStreamSeverity Severity, std::string_view CategoryTag, std::string_view Message)
{
    char TimeBuf[20]{};
    const double Sec = Extension.InspectMetrics().ElapsedClockSeconds;
    const int Min    = static_cast<int>(Sec) / 60;
    const double Rem = Sec - static_cast<double>(Min * 60);
    std::snprintf(TimeBuf, sizeof(TimeBuf), "%02d:%05.2f", Min, Rem);

    LogEntries.push_back(LogStreamEntry{
        TimeBuf,
        std::string(CategoryTag),
        std::string(Message),
        Severity,
    });
    if (LogEntries.size() > 128u)
    {
        LogEntries.erase(LogEntries.begin());
    }
}

ParticleSpace::ParticleRig* ParticleWorkspaceSession::AccessSelectedRig() noexcept
{
    return Extension.AccessActiveDocument().FindRigMutable(SelectedRigId);
}

const ParticleSpace::ParticleRig* ParticleWorkspaceSession::InspectSelectedRig() const noexcept
{
    return Extension.QueryActiveDocument().FindRig(SelectedRigId);
}

SimulationSpace::ForceFields::ForceSourceRecordEntry* ParticleWorkspaceSession::AccessSelectedForce() noexcept
{
    auto& Forces = Extension.AccessActiveDocument().ForceSources;
    if (SelectedForceIndex >= Forces.size())
    {
        return nullptr;
    }
    return &Forces[SelectedForceIndex];
}

std::span<const uint8_t> ParticleWorkspaceSession::InspectViewportRgba8() const noexcept
{
    return GpuExecutor.Ready() ? GpuExecutor.InspectRenderedRgba8() : std::span<const uint8_t>(SoftwareRgba);
}

uint32_t ParticleWorkspaceSession::QueryViewportWidthPx() const noexcept
{
    return GpuExecutor.Ready() ? GpuExecutor.QueryWidthPx() : SoftwareWidth;
}

uint32_t ParticleWorkspaceSession::QueryViewportHeightPx() const noexcept
{
    return GpuExecutor.Ready() ? GpuExecutor.QueryHeightPx() : SoftwareHeight;
}

ParticleSpace::Execution::ParticleMetricsSnapshot ParticleWorkspaceSession::QueryMetrics() const noexcept
{
    if (GpuExecutor.Ready())
    {
        return GpuExecutor.QueryMetrics();
    }
    ParticleSpace::Execution::ParticleMetricsSnapshot Snap{};
    Snap.AliveParticles   = SoftwareFallback.QueryTelemetry().AliveCount;
    Snap.LastStepMs       = static_cast<double>(StepDurationMs);
    Snap.GpuComputeActive = false;
    return Snap;
}

} // namespace Frontier::Workspaces
