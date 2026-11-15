/*============================================================================================================================================
                                                       PARTICLEWORKSPACESESSION.H
============================================================================================================================================*/
// 🧩 Authoritative ParticleWorkspace session binding WorkspaceDocument (.ParticleSpace), RevisionTracker, ParticleExtension, GPU/CPU executors & UI controls

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ColourSpace/ColourExtension.h"
#include "Engine/Components/ContentBrowser/ContentBrowserPanel.h"
#include "Engine/Components/Controls/ColourRampEditor.h"
#include "Engine/Components/Controls/ScalarCurveEditor.h"
#include "Engine/Components/Controls/SimulationPlaybackControls.h"
#include "Engine/Components/Controls/SimulationTimeRuler.h"
#include "Engine/Components/Diagnostics/LogStreamDrawer.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/ParticleSpace/Execution/ParticleVulkanExecutor.h"
#include "Engine/ParticleSpace/ParticleExtension.h"
#include "Engine/ParticleSpace/ParticleSoftwareExecutor.h"
#include "Engine/Workspaces/Particle/ParticleAuthoringOperations.h"
#include "Engine/Workspaces/WorkspaceDocument.h"
#include "Engine/Workspaces/WorkspaceExchange.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ParticleInspectorTab : uint8_t
{
    Overview       = 0u,
    Source         = 1u,
    MotionEffects  = 2u,
    UnitsRecord    = 3u,
};

enum class ParticleSelectionTarget : uint8_t
{
    Rig         = 0u,
    ForceSource = 1u,
};

enum class ParticleViewportTool : uint8_t
{
    SelectHandles = 0u,
    ThrowBall     = 1u,
    Magnet        = 2u,
    Anemometer    = 3u,
    ResetView     = 4u,
};

enum class ParticleOverlayMode : uint8_t
{
    CleanViewport  = 0u,
    VelocityArrows = 1u,
    ForceVectors   = 2u,
    AnchorPoints   = 3u,
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        CLASSES
//------------------------------------------------------------------------------------------------------------------------

class ParticleWorkspaceSession
{
public:
    ParticleWorkspaceSession();
    ~ParticleWorkspaceSession() = default;

    [[nodiscard]] bool AttachVulkanInterchange(
        GeometricRaster::VulkanInterchange* BoundDevice,
        const std::filesystem::path&        ShaderDirectory = {});

    void AttachContentBrowserPanel(ContentBrowserPanel* TargetPanel) noexcept { BoundBrowserPanel = TargetPanel; }

    void StepSimulation(float DeltaSeconds, uint32_t ViewportWidthPx, uint32_t ViewportHeightPx);
    void TriggerSingleStep(uint32_t ViewportWidthPx, uint32_t ViewportHeightPx);
    void RestartSimulation();
    void TriggerBurst();
    void ThrowInteractiveBall();

    void RecordRevisionStep(std::string_view OperationTag);
    [[nodiscard]] bool Undo();
    [[nodiscard]] bool Redo();

    [[nodiscard]] bool LoadParticleSpaceDocument(const std::filesystem::path& SourcePath, std::string& OutDiagnostic);
    [[nodiscard]] bool SaveParticleSpaceDocument(const std::filesystem::path& DestPath, std::string& OutDiagnostic);
    [[nodiscard]] bool ImportExternalSnapshot(const std::filesystem::path& SourcePath, std::string& OutDiagnostic);
    [[nodiscard]] bool ExportHtmlTomlSnapshot(const std::filesystem::path& DestPath, std::string& OutDiagnostic);

    void SelectRig(uint32_t RigId) noexcept;
    void SelectForceSource(std::size_t ForceIndex) noexcept;
    void AssignActiveRigColourProfile(const ColourSpace::ColourSourceProfile& Profile, std::string_view Label);
    void AppendLog(LogStreamSeverity Severity, std::string_view CategoryTag, std::string_view Message);

    [[nodiscard]] ParticleSpace::ParticleDocument&       AccessDocument() noexcept { return Extension.AccessActiveDocument(); }
    [[nodiscard]] const ParticleSpace::ParticleDocument& InspectDocument() const noexcept { return Extension.QueryActiveDocument(); }
    [[nodiscard]] ParticleSpace::ParticleRig*            AccessSelectedRig() noexcept;
    [[nodiscard]] const ParticleSpace::ParticleRig*      InspectSelectedRig() const noexcept;
    [[nodiscard]] SimulationSpace::ForceFields::ForceSourceRecordEntry* AccessSelectedForce() noexcept;

    [[nodiscard]] WorkspaceDocument&                     AccessWorkspaceDocument() noexcept { return ContainerDoc; }
    [[nodiscard]] const WorkspaceDocument&               InspectWorkspaceDocument() const noexcept { return ContainerDoc; }
    [[nodiscard]] RevisionTracker&                       AccessRevision() noexcept { return ContainerDoc.AccessRevisionTracker(); }
    [[nodiscard]] const RevisionTracker&                 InspectRevision() const noexcept { return ContainerDoc.InspectRevisionTracker(); }

    [[nodiscard]] std::span<const uint8_t>               InspectViewportRgba8() const noexcept;
    [[nodiscard]] uint32_t                               QueryViewportWidthPx() const noexcept;
    [[nodiscard]] uint32_t                               QueryViewportHeightPx() const noexcept;
    [[nodiscard]] ParticleSpace::Execution::ParticleMetricsSnapshot QueryMetrics() const noexcept;
    [[nodiscard]] bool                                   UsingGpuExecutor() const noexcept { return GpuExecutor.Ready(); }

    // Public session UI controls
    SimulationPlaybackTuning   PlaybackTuning       {};
    SimulationRulerSpec        RulerSpec            {};
    ColourRampEditorSession    RampEditorSession    {};
    ScalarCurveProfile         SizeOverLifeCurve    {};
    ScalarCurveEditorSession   SizeCurveSession     {};
    ScalarCurveProfile         OpacityOverLifeCurve {};
    ScalarCurveEditorSession   OpacityCurveSession  {};
    LogStreamDrawerSpec        LogDrawerSpec        {};

    ParticleInspectorTab       ActiveInspectorTab   = ParticleInspectorTab::Overview;
    ParticleSelectionTarget    SelectionTarget      = ParticleSelectionTarget::Rig;
    ParticleViewportTool       ActiveTool           = ParticleViewportTool::SelectHandles;
    ParticleOverlayMode        OverlayMode          = ParticleOverlayMode::CleanViewport;
    uint32_t                   SelectedRigId        = 1u;
    std::size_t                SelectedForceIndex   = 0u;
    bool                       HandlesVisible       = true;
    bool                       ShowAssetsBrowser    = false;
    bool                       GpuExecutorFailed    = false;
    float                      StepDurationMs       = 0.0f;
    float                      CameraYawRad         = 0.45f;
    float                      CameraPitchRad       = 0.30f;
    float                      CameraDistanceMeters = 6.8f;
    std::string                StatusBanner         { "Studio Default" };
    std::vector<LogStreamEntry> LogEntries          {};

private:
    void SyncCameraToDocument() noexcept;
    void EnsureSampleDocumentInitialized();

    WorkspaceDocument                          ContainerDoc;
    WorkspaceExchange                          ExchangeBus     {};
    ColourSpace::ColourExtension               ColourCoord     {};
    ParticleSpace::ParticleExtension           Extension       {};
    ParticleSpace::Execution::ParticleVulkanExecutor GpuExecutor {};
    ParticleSpace::ParticleSoftwareExecutor    SoftwareFallback{};
    std::vector<uint8_t>                       SoftwareRgba    {};
    uint32_t                                   SoftwareWidth   = 0u;
    uint32_t                                   SoftwareHeight  = 0u;
    ContentBrowserPanel*                       BoundBrowserPanel = nullptr;
    uint64_t                                   RevisionStamp   = 1ull;
};

} // namespace Frontier::Workspaces
