/*============================================================================================================================================
                                                          WORKSPACECONFIGURATION.H
============================================================================================================================================*/
// 🧩 Session layout, default UDIM tile resolution, default-off seam overlay policy, and 3D/2D TransformGizmo snap configuration

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <cstdint>

namespace Frontier::Configuration
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class UVViewportLayoutMode : uint8_t
{
    Split2DAnd3D = 0u, // [-] - Side-by-side 2D UVTileCanvas and 3D ProjectionViewport
    UVTile2DOnly = 1u, // [-] - Full-width 2D UDIM parameter canvas
    Spatial3DOnly = 2u // [-] - Full-width 3D lit/MatCap viewport
};

enum class UVBudgetEnforcementMode : uint8_t
{
    StrictRatio          = 0u, // [-] - Enforces exact PriorityAllocationLayer target share ratios
    FillRemainderPockets = 1u, // [-] - Packs unassigned islands into leftover concave pockets
};

enum class UVPackingScopeMode : uint8_t
{
    ActiveUDIMTile         = 0u, // [-] - Packs into the active UDIM tile
    PerAssignedUDIMTile    = 1u, // [-] - Packs each PriorityAllocationLayer into its assigned UDIM
    AutoSpillOverflowUDIMs = 2u, // [-] - Spills overflow islands into consecutive UDIM tiles (1001, 1002, ...)
    SelectedIslandsOnly    = 3u, // [-] - Re-packs only currently selected UVTile islands
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct GizmoSnapSettings
{
    float SpatialMoveStepMeters = 0.25f; // [m]   - 3D Ctrl-drag translation snap increment
    float UVMoveStepUnits       = 0.05f; // [uv]  - 2D Ctrl-drag UV translation snap increment
    float ScaleFactorStep       = 0.10f; // [-]   - 3D/2D Ctrl-drag scale snap increment
    float RotationStepDegrees   = 5.0f;  // [°]   - 3D/2D Ctrl-drag rotation snap increment
};

struct UVWorkspacePreferences
{
    UVViewportLayoutMode    InitialViewportLayout  = UVViewportLayoutMode::Spatial3DOnly;       // [-]    - Default viewport split
    UVBudgetEnforcementMode BudgetMode             = UVBudgetEnforcementMode::StrictRatio;      // [-]    - Strict vs pocket fill
    UVPackingScopeMode      PackingScope           = UVPackingScopeMode::ActiveUDIMTile;        // [-]    - UDIM target scope
    uint32_t                DefaultTileResolutionPx = 4096u;                                    // [px]   - Default UDIM resolution
    uint32_t                IslandMarginPixels     = 12u;                                       // [px]   - Bleed gutter padding
    uint32_t                RotationStepDegrees    = 90u;                                       // [°]    - Candidate pack orientations
    float                   TargetTexelDensity     = 850.0f;                                    // [px/m] - Physical texel density
    float                   SplitRatioNormalized   = 0.50f;                                     // [0-1]  - 2D/3D divider ratio
    float                   LeftOutlinerWidthPx    = 304.0f;                                    // [px]   - UV Space pane width
    float                   RightInspectorWidthPx  = 336.0f;                                    // [px]   - Inspector pane width
    bool                    WireframeOverlayEnabled = false;                                    // [-]    - 3D wireframe visibility (default off)
    bool                    SeamOverlayEnabled     = false;                                     // [-]    - Optional seam debug (OFF by default)
    bool                    BoundaryOverlayEnabled = false;                                     // [-]    - Optional border debug (OFF by default)
    bool                    CheckerOverlayEnabled  = true;                                      // [-]    - 2D UV checker visibility
    bool                    EqualizeIntraLayerScale = true;                                     // [-]    - Normalize texel scale per layer
};

struct ParticleWorkspacePreferences
{
    float    LeftOutlinerWidthPx      = 304.0f; // [px]    - Particle Outliner pane width
    float    RightInspectorWidthPx    = 360.0f; // [px]    - Particle Inspector pane width
    uint32_t DefaultEffectCapacity    = 2048u;  // [count] - Default particle allocation budget
    uint32_t LatticeCascadeResolution = 16u;    // [idx]   - Default coupled ForceField clipmap resolution
    bool     FloorGridEnabled         = true;   // [-]     - 3D Z-Up floor grid overlay visibility
    bool     ForceVectorsEnabled      = true;   // [-]     - Coupled ForceField vector & gizmo overlay
    bool     BoundsOverlayEnabled     = false;  // [-]     - Simulation bounding sphere telemetry overlay
    bool     PlaybackScrubberVisible  = true;   // [-]     - Bottom viewport timeline transport bar visibility
};

class WorkspaceConfiguration
{
public:
    WorkspaceConfiguration() noexcept = default;

    void ConfigureUVPreferences(const UVWorkspacePreferences& Preferences) noexcept;
    void ConfigureParticlePreferences(const ParticleWorkspacePreferences& Preferences) noexcept;
    void ConfigureGizmoSnap(const GizmoSnapSettings& SnapSettings) noexcept;
    void ResetDefaults() noexcept;

    [[nodiscard]] const UVWorkspacePreferences&       InspectUVPreferences() const noexcept;
    [[nodiscard]] UVWorkspacePreferences&             AccessUVPreferences() noexcept;
    [[nodiscard]] const ParticleWorkspacePreferences& InspectParticlePreferences() const noexcept;
    [[nodiscard]] ParticleWorkspacePreferences&       AccessParticlePreferences() noexcept;
    [[nodiscard]] const GizmoSnapSettings&            InspectGizmoSnap() const noexcept;
    [[nodiscard]] GizmoSnapSettings&                  AccessGizmoSnap() noexcept;

private:
    UVWorkspacePreferences       UVPreferences       {}; // [-] - UV workspace layout, solver & overlay defaults
    ParticleWorkspacePreferences ParticlePreferences {}; // [-] - Particle workspace layout, budget & viewport defaults
    GizmoSnapSettings            GizmoSnap           {}; // [-] - Shared 3D/2D TransformGizmo snap steps
};

} // namespace Frontier::Configuration
