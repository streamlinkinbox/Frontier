/*============================================================================================================================================
                                                          PARTICLEOUTLINERPANEL.H
============================================================================================================================================*/
// 🧩 Outliner panel matching HTML Particle Studio (Visible/Hidden census tiles, category filter chips, Environment & Particle Rigs)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/TextInputField.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Workspaces/Particle/ParticleWorkspaceSession.h"

#include <cstdint>
#include <string>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class OutlinerFilterCategory : uint8_t
{
    All        = 0u,
    Effects    = 1u,
    MoGraph    = 2u,
    Cloth      = 3u,
    Flora      = 4u,
    Ferro      = 5u,
    Fracture   = 6u,
    Curl       = 7u,
    Sparks     = 8u,
    Pyro       = 9u,
    Derez      = 10u,
    Lens       = 11u,
    Shield     = 12u,
    Sky        = 13u,
    Wind       = 14u,
    Weather    = 15u,
    Quarantine = 16u,
    Forces     = 17u,
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        CLASSES
//------------------------------------------------------------------------------------------------------------------------

class ParticleOutlinerPanel
{
public:
    ParticleOutlinerPanel() = default;
    ~ParticleOutlinerPanel() noexcept;

    void AttachSession(ParticleWorkspaceSession* TargetSession) noexcept;

    void Render(
        PixelSpace&             Surface,
        Rectangle2D             Bounds,
        const PaneInputReading& Input,
        double                  DeltaSeconds);

    void PresentInImGuiPane(double DeltaSeconds, float ScaleFactor = 1.0f);

private:
    ParticleWorkspaceSession* Session          = nullptr;
    OutlinerFilterCategory    ActiveFilter     = OutlinerFilterCategory::All;
    TextEditBuffer            SearchBuffer     {};
    std::string               SearchText       {};
    bool                      EnvExpanded      = true;
    bool                      RigsExpanded     = true;
    bool                      AddMenuOpen      = false;
};

} // namespace Frontier::Workspaces
