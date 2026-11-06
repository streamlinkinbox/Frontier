/*==========================================================================================================================================
                                                        FLUIDPLUMEEDITOR.H
==========================================================================================================================================*/
// 🧩 The Smoke & fire editor window opened from a FluidPlume record's inspector: an Outliner of the record's parts (the domain, the tyre sources,
//    the test emitter, the gas's simulation, appearance and fire), the viewport on the plume, a Log Stream, an Inspector page for the selected part
//    and the libraries (the six looks and the four grades) — the Particle editor's window, on the HMI prototype's "Smoke & fire" page: the record's
//    33 gas readings (GasParameterCatalogue).
//
// 📝 The viewport is a GPU image from the host that owns the Vulkan device (RegisterPlumeViewportPresenter); without one the window still edits,
//    and says why the viewport is empty.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Interface/FluidPlumeEditor/PlumeViewportPresentation.h"
#include "Engine/Scene/EngineConfigurations.h"
#include "Engine/Scene/RecordToken.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{

#if FRONTIER_EDITOR

class FluidPlumeEditorWorkspace;

class FluidPlumeEditor
{
  public:
    // 📝 Lays the window (a modal popup, as the Particle editor's is) on the record's readings, for as long as the record's editor is the active one.
    static void ConstructInterface(RecordToken Token, FluidPlumeConfiguration& Parameters);

    // 📝 A host that runs the plume without the viewport's presenter says what it is doing; RevokeReadout says it stopped. The presenter's own
    //    readout, when the window has one, is the one QueryReadout returns.
    static void AssignReadout(const FluidPlumeReadout& Readout);
    static void RevokeReadout() noexcept;
    [[nodiscard]] static const FluidPlumeReadout* QueryReadout() noexcept;

    // 📝 The verbs the window raised that only a host without the presenter can carry out ("clear-smoke": empty the lattices). The call takes them.
    [[nodiscard]] static std::vector<std::string> TakeRequests();

    // 📝 The editor behind the window (created when it first opens); captures and the validation drive it through its verbs.
    [[nodiscard]] static FluidPlumeEditorWorkspace* AccessWorkspace();
};

#endif // FRONTIER_EDITOR

} // namespace Frontier
