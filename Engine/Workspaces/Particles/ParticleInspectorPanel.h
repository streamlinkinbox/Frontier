/*============================================================================================================================================
                                                       PARTICLEINSPECTORPANEL.H
============================================================================================================================================*/
// 🧩 Native Frontier Particle Inspector: 4-tab Particle Effect authoring (Overview/Source/Motion & FX/Spec & Export) + direct ForceInspectorView reuse

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/TextInputField.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Editor/Workspaces/Simulation/ForceInspectorView.h"
#include "Engine/Workspaces/Particles/ParticleWorkspaceSession.h"

#include <cstdint>
#include <string>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ParticleInspectorTab : uint8_t
{
    Overview      = 0u, // [-] - Effect summary, actions, color ramp, and domain-specific controls
    Source        = 1u, // [-] - Release volume, origin XYZ, capacity, birth rate, kinematics & bursts
    MotionAndFx   = 2u, // [-] - Silhouette, compositing blend, sizing, physics & coupled ForceFields gain
    SpecAndExport = 3u, // [-] - Memory budget, coupled C++ ForceSources census & .ParticleSpace TOML preview
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 PARTICLEINSPECTORPANEL
//------------------------------------------------------------------------------------------------------------------------

class ParticleInspectorPanel
{
public:
    ParticleInspectorPanel() noexcept = default;
    ~ParticleInspectorPanel() noexcept;

    void AttachSession(ParticleWorkspaceSession* TargetSession) noexcept;

    void ConfigureActiveTab(ParticleInspectorTab Tab) noexcept;
    void ConfigureForceInspectorTab(Editor::Simulation::ForceInspectorTab Tab) noexcept;
    void ConfigureScrollOffset(float OffsetY) noexcept;

    [[nodiscard]] ParticleInspectorTab                  QueryActiveTab() const noexcept;
    [[nodiscard]] Editor::Simulation::ForceInspectorTab QueryForceInspectorTab() const noexcept;
    [[nodiscard]] bool                                  QueryForceInspectorDelegated() const noexcept;
    [[nodiscard]] float                                 QueryScrollOffset() const noexcept;

    void Render(
        PixelSpace&             Surface,
        Rectangle2D             Bounds,
        const PaneInputReading& Input,
        double                  Δt);

    void PresentInImGuiPane(double Δt, float ScaleFactor = 1.0f);

private:
    enum class OpenDropdownCategory : uint8_t
    {
        None                = 0u,
        ReleaseVolume       = 1u,
        Silhouette          = 2u,
        ColorBy             = 3u,
        RampWrap            = 4u,
        FireworkPattern     = 5u,
        RiggingArchitecture = 6u,
        FerrofluidVessel    = 7u,
        FracturePattern     = 8u,
        ShieldGeometry      = 9u,
        BoidsCreature       = 10u,
    };

    struct DeferredDropdownSheet
    {
        OpenDropdownCategory Category      = OpenDropdownCategory::None;
        Rectangle2D          TriggerBounds {};
    };

    float RenderOverviewTab(
        PixelSpace&                Surface,
        Rectangle2D                ScrollViewport,
        float                      LeftX,
        float                      TopY,
        float                      InnerW,
        ParticleEffectRecordEntry& Effect,
        const PaneInputReading&    Input,
        double                     Δt,
        DeferredDropdownSheet&     OutOverlay);

    float RenderSourceTab(
        PixelSpace&                Surface,
        Rectangle2D                ScrollViewport,
        float                      LeftX,
        float                      TopY,
        float                      InnerW,
        ParticleEffectRecordEntry& Effect,
        const PaneInputReading&    Input,
        DeferredDropdownSheet&     OutOverlay);

    float RenderMotionAndFxTab(
        PixelSpace&                Surface,
        Rectangle2D                ScrollViewport,
        float                      LeftX,
        float                      TopY,
        float                      InnerW,
        ParticleEffectRecordEntry& Effect,
        const PaneInputReading&    Input,
        DeferredDropdownSheet&     OutOverlay);

    float RenderSpecAndExportTab(
        PixelSpace&                      Surface,
        float                            LeftX,
        float                            TopY,
        float                            InnerW,
        const ParticleEffectRecordEntry& Effect);

    void RenderDeferredDropdown(
        PixelSpace&                Surface,
        Rectangle2D                PaneBounds,
        const DeferredDropdownSheet& Overlay,
        ParticleEffectRecordEntry& Effect,
        const PaneInputReading&    Input);

    // Direct reuse of Frontier's existing C++ ForceFields Inspector when ForceField / Wind / Source selected
    Editor::Simulation::ForceInspectorView ForceView            {};

    ParticleWorkspaceSession*              Session              = nullptr;
    ParticleInspectorTab                   ActiveTab            = ParticleInspectorTab::Overview;
    float                                  ScrollOffsetY        = 0.0f;
    float                                  LastContentH         = 0.0f;
    int32_t                                ActiveSliderId       = -1;
    int32_t                                ActiveAxisDragId     = -1;
    float                                  AxisDragStartX       = 0.0f;
    float                                  AxisDragOriginScalar = 0.0f;
    OpenDropdownCategory                   OpenDropdown         = OpenDropdownCategory::None;
    uint32_t                               SyncedTitleToken     = 0u;
    TextEditBuffer                         TitleEditor          {};
    uint32_t                               SyncedSkyTextToken   = 0u;
    TextEditBuffer                         SkyTextEditor        {};
};

} // namespace Frontier::Workspaces
