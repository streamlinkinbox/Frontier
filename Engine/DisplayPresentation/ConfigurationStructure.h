//========================================================================================================================
// 🧩 ConfigurationStructure — plain records for the Input and Notifications pages, shared by their inspectors, the
//    Control Centre host and ConfigurationRegistry (TOML). Kept free of widget code so projects can consume them directly.
//========================================================================================================================
#pragma once

#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                              RENDER BACKEND CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------
// Engine-level (not per-page) settings read at device bring-up. Lives under [render] next to the dashboard record.

// Mirrors RayTracingRequestCategory (DeviceExchange) without pulling Vulkan headers into the presentation layer.
enum class RayTracingTierRequestCategory : uint32_t { Auto = 0, Software = 1, RayQuery = 2, Pipeline = 3, Count = 4 };

// Mirrors DebugViewCategory (DeviceExchange/VisibilityExchange.h): the R2 debug popup (F3) cycles these.
enum class DebugViewSelection : uint32_t { Off = 0, Depth = 1, Visibility = 2, Motion = 3, Cluster = 4, HiZ = 5, Albedo = 6, Normal = 7, Roughness = 8, Metalness = 9, ShadingNormal = 10, ReservoirM = 11, ReservoirW = 12, ReservoirAge = 13, Count = 14 };

struct RenderBackendConfiguration
{
    RayTracingTierRequestCategory RayTracingTier = RayTracingTierRequestCategory::Auto;   // [render] ray_tracing_tier
    DebugViewSelection            DebugView      = DebugViewSelection::Off;               // [render] debug_view
    bool                          OcclusionCulling = true;                                // [render] occlusion_culling (HiZ)
    bool                          AliasPick = true;                                       // [render] alias_pick (R6 row 3 Walker-alias light pick; false = uniform R0 identity, F5)
    uint32_t                      SlabLimit        = 1u;                                  // [render] slab_limit (R4a: Tier A 1, Tier B/C 4, ceiling 8)
    uint32_t                      TextureEdgeLimit = 2048u;                               // [render] texture_edge_limit (largest texture edge kept resident; 0 = unlimited)
    [[nodiscard]] bool operator==(const RenderBackendConfiguration&) const noexcept = default;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  INPUT PREFERENCES
//------------------------------------------------------------------------------------------------------------------------

// Notch InputSettingsModal "Preset profile" select: Blender (Default) · Maya / Unity · Unreal Engine.
enum class InputProfileCategory : uint32_t { Blender = 0, MayaUnity = 1, Unreal = 2, Count = 3 };

// Fields follow Notch InputSettingsModal top-to-bottom; the shortcut strings are the four editable key fields.
struct InputPreferences
{
    InputProfileCategory Profile = InputProfileCategory::Blender;
    float MouseSensitivity   = 50.0f;   // [%] Notch range default 50 → project maps 0 … 100 onto 0.25 … 2.0 × base rad/px
    bool  CustomShortcuts    = true;    // "Custom Shortcuts" toggle; fields below are read-only while off
    char  SelectTool[24]     = "Q";
    char  TranslateTool[24]  = "W";
    char  RotateTool[24]     = "E";
    char  FrameSelected[24]  = "Ctrl + Shift + F";
    bool  AdvancedControls   = false;   // "Advanced Controls" toggle
    bool  InvertPitch        = false;   // "Invert Y-Axis"
    [[nodiscard]] bool operator==(const InputPreferences&) const noexcept = default;
};

//------------------------------------------------------------------------------------------------------------------------
//                                               NOTIFICATION PREFERENCES
//------------------------------------------------------------------------------------------------------------------------

// Fields follow Notch TelemetrySettingsModal rows top-to-bottom (overlay rows, then the "Alerts" group); the dwell
//    slider is an engine addition (flagged) so the toast hold time is user-tunable.
struct NotificationPreferences
{
    bool  ShowFrameRateOverlay = true;    // "Show FPS Overlay"  — same flag as the dashboard tile
    bool  ShowMemoryUsage      = true;    // "Show RAM Usage"
    bool  ShowSceneMetadata    = false;   // "Scene Metadata"
    bool  BakingComplete       = true;    // Alerts › "Baking Complete"
    bool  RenderFinished       = true;    // Alerts › "Render Finished"  (render / appearance applied toasts)
    bool  AutosaveErrors       = true;    // Alerts › "Autosave Errors"  (preference save failures)
    bool  FrameRateDrops       = false;   // Alerts › "Frame-rate Drops" (engine addition)
    float HoldSeconds          = 3.5f;    // [s] toast dwell 1 … 10 (engine addition)
    [[nodiscard]] bool operator==(const NotificationPreferences&) const noexcept = default;
};

} // namespace Frontier
