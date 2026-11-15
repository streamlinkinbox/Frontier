/*============================================================================================================================================
                                                         PARTICLEINSPECTORPANEL.H
============================================================================================================================================*/
// 🧩 Context-aware 4-tab Inspector panel matching HTML Particle Studio (Overview, Source + ColourRampEditor, Motion & FX + ScalarCurveEditor, Units & Record)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/ContentBrowser/ContentBrowserPanel.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Workspaces/Particle/ParticleWorkspaceSession.h"

#include <cstdint>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        CLASSES
//------------------------------------------------------------------------------------------------------------------------

class ParticleInspectorPanel
{
public:
    ParticleInspectorPanel() = default;
    ~ParticleInspectorPanel() noexcept;

    void AttachSession(ParticleWorkspaceSession* TargetSession) noexcept;
    void AttachContentBrowserPanel(ContentBrowserPanel* TargetPanel) noexcept { BoundBrowserPanel = TargetPanel; }

    void Render(
        PixelSpace&             Surface,
        Rectangle2D             Bounds,
        const PaneInputReading& Input,
        double                  DeltaSeconds);

    void PresentInImGuiPane(double DeltaSeconds, float ScaleFactor = 1.0f);

    [[nodiscard]] Rectangle2D QueryLastColourRampStripBounds() const noexcept { return LastRampStripBounds; }

private:
    ParticleWorkspaceSession* Session             = nullptr;
    ContentBrowserPanel*      BoundBrowserPanel   = nullptr;
    Rectangle2D               LastRampStripBounds {};
    bool                      ChannelMenuOpen     = false;
    bool                      DraggingRate        = false;
    bool                      DraggingSize        = false;
    bool                      DraggingStrength    = false;
    bool                      DraggingRadius      = false;
};

} // namespace Frontier::Workspaces
