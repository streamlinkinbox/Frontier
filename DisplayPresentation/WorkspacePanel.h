//============================================================================================================================================
// 📦 Frontier/DisplayPresentation/WorkspacePanel.h — Multi-Band Immediate Mode Trapezoidal Workspace Overlay and Multi-Viewport Docking
//============================================================================================================================================

#pragma once

#if defined(_MSC_VER)
    #pragma warning(disable: 4324)                              // Disable structure padding alignment warning under /WX
#endif

#include "../DeviceExchange/RenderTargetExchange.h"
#include "../DeviceExchange/InputExchange.h"
#include "ThemeStructure.h"
#include "VectorCodec.h"
#include "ControlCentrePanel.h"
#include <cstdint>
#include <string_view>
#include <array>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                WORKSPACE EDIT MODE
//------------------------------------------------------------------------------------------------------------------------

enum class WorkspaceEditMode : uint32_t
{
    Editing                             = 0,                    // CAD and geometry editing mode
    SimulatingInEditor                  = 1,                    // Physics active within editor
    Paused                              = 2                     // Simulation frozen for inspection
};

//------------------------------------------------------------------------------------------------------------------------
//                                               WORKSPACE CATEGORY
//------------------------------------------------------------------------------------------------------------------------

enum class WorkspaceCategory : uint32_t
{
    Modeling                            = 0,                    // Mesh topology, vertices and cluster layout
    Shading                             = 1,                    // Material BRDF, textures and ReSTIR photometric lighting
    Simulation                          = 2,                    // Physical dynamics, vehicles and fluid level sets
    Diagnostics                         = 3,                    // Profiler graphs, telemetry tables and console
    Count                               = 4
};

//------------------------------------------------------------------------------------------------------------------------
//                                             TRAPEZOIDAL TAB RECORD
//------------------------------------------------------------------------------------------------------------------------

struct TrapezoidTabRecord
{
    Vector3                 BottomLeftPoint;                    // [px] P0 corner
    Vector3                 TopLeftPoint;                       // [px] P1 corner
    Vector3                 TopRightPoint;                      // [px] P2 corner
    Vector3                 BottomRightPoint;                   // [px] P3 corner
    const char*             LabelText;                          // [text] workspace title
    WorkspaceCategory       Category;                           // [category] target workspace
    bool                    ActiveCondition;                    // [bool] true if selected
    bool                    HoveredCondition;                   // [bool] true if mouse is over trapezoid
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   WORKSPACE PANEL
//------------------------------------------------------------------------------------------------------------------------

class WorkspacePanel
{
public:
    WorkspacePanel() noexcept;
    ~WorkspacePanel() noexcept = default;

    WorkspacePanel(const WorkspacePanel&) = delete;
    WorkspacePanel& operator=(const WorkspacePanel&) = delete;

    [[nodiscard]] bool      Initialize(uint32_t DesiredWidth, uint32_t DesiredHeight) noexcept;
    void                    Terminate() noexcept;

    void                    RenderWorkspaceOverlay(WorkspaceEditMode DesiredMode, float DeltaSeconds) noexcept;
    void                    AdvanceInteraction(const InputExchange& Input, float CursorX, float CursorY) noexcept;

    [[nodiscard]] WorkspaceEditMode QueryEditMode() const noexcept { return CurrentMode; }
    void                    AssignEditMode(WorkspaceEditMode NewMode) noexcept { CurrentMode = NewMode; }

    [[nodiscard]] WorkspaceCategory QueryActiveWorkspace() const noexcept { return ActiveWorkspace; }
    void                    AssignActiveWorkspace(WorkspaceCategory Category) noexcept;

    [[nodiscard]] bool      IsViewportFocused() const noexcept { return ViewportFocusedCondition; }
    [[nodiscard]] bool      IsInputGated() const noexcept { return InputGatedCondition; }
    [[nodiscard]] const RenderTargetExchange& QueryActiveRenderTarget() const noexcept;

#ifdef FRONTIER_DEVELOPMENT
    [[nodiscard]] const ControlCentrePanel& QueryControlCentrePanel() const noexcept { return NotchControlCentrePanel; }
    ControlCentrePanel&     AccessControlCentrePanel() noexcept { return NotchControlCentrePanel; }
    [[nodiscard]] const ControlCentrePanel& QueryControlPanel() const noexcept { return NotchControlCentrePanel; }
    ControlCentrePanel&     AccessControlPanel() noexcept { return NotchControlCentrePanel; }
#endif

    // Single unified conversion operator for edit mode
    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

private:
#ifdef FRONTIER_DEVELOPMENT
    void                    RenderBand0WorkspaceHeader() noexcept;
    void                    RenderBand1CentralViewport() noexcept;
    void                    RenderBand2DockedDrawers() noexcept;
    void                    CalculateTrapezoidTabs() noexcept;
#endif

    uint32_t                ViewportWidth;                      // [px] viewport width
    uint32_t                ViewportHeight;                     // [px] viewport height
    WorkspaceEditMode       CurrentMode;                        // [mode] active editor operation mode
    WorkspaceCategory       ActiveWorkspace;                    // [workspace] active master workspace category
    std::array<TrapezoidTabRecord, 4> TrapezoidTabs;            // [tabs] trapezoid tab geometry
    std::array<RenderTargetExchange, 4> WorkspaceRenderTargets; // [targets] independent offscreen Vulkan scene render targets
    bool                    ViewportFocusedCondition;           // [bool] true if 3D viewport has exclusive input focus
    bool                    InputGatedCondition;                // [bool] true if docked tool panels trap input
    bool                    InitializedCondition;               // [bool] overlay status
#ifdef FRONTIER_DEVELOPMENT
    ControlCentrePanel      NotchControlCentrePanel;            // [panel] organic top notch control centre
#endif
};

template<>
inline WorkspaceEditMode WorkspacePanel::Convert<WorkspaceEditMode>() const noexcept
{
    return CurrentMode;
}

template<>
inline WorkspaceCategory WorkspacePanel::Convert<WorkspaceCategory>() const noexcept
{
    return ActiveWorkspace;
}

} // namespace Frontier
