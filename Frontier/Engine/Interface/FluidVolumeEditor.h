/*==========================================================================================================================================
                                                          FLUIDVOLUMEEDITOR.H
==========================================================================================================================================*/
// 🧩 The Fluid editor window opened from a FluidVolume record's inspector: an Outliner of the volume's parts (the domain, the emitter, the collider and
//    the light), the viewport on the solver's picture with the prototype's tools and transport, a Log Stream, an Inspector page for the selected part
//    and the preset library — the Particle editor's window, on the Fluid prototype's page (Experimental/Fluid): the record's 85 settings and the 18
//    presets (GasPresetLibrary).
//
// 📝 The viewport is a GPU image from the host that owns the Vulkan device (RegisterFluidViewportPresenter); without one the window still edits, and
//    says why the viewport is empty.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Interface/FluidEditor/FluidViewportPresentation.h"
#include "Engine/Scene/EngineConfigurations.h"
#include "Engine/Scene/RecordToken.h"

#include <string>
#include <vector>

namespace Frontier
{

#if FRONTIER_EDITOR

class FluidEditorWorkspace;

class FluidVolumeEditor
{
  public:
    // 📝 Lays the window (a modal popup, as the Particle editor's is) on the record's settings, for as long as the record's editor is the active one.
    static void ConstructInterface(RecordToken Token, FluidVolumeConfiguration& Parameters);

    // 📝 A host that runs the volume without the viewport's presenter says what it is doing; RevokeReadout says it stopped. The presenter's own readout,
    //    when the window has one, is the one QueryReadout returns.
    static void AssignReadout(const FluidVolumeReadout& Readout);
    static void RevokeReadout() noexcept;
    [[nodiscard]] static const FluidVolumeReadout* QueryReadout() noexcept;

    // 📝 The verbs the window raised that only a host without the presenter can carry out ("restart", "step", "burst:<kind>"). The call takes them.
    [[nodiscard]] static std::vector<std::string> TakeRequests();

    // 📝 The editor behind the window (created when it first opens); captures and the validation drive it through its verbs.
    [[nodiscard]] static FluidEditorWorkspace* AccessWorkspace();
};

#endif // FRONTIER_EDITOR

} // namespace Frontier
