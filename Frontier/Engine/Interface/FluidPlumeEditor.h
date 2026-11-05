/*==========================================================================================================================================
                                                        FLUIDPLUMEEDITOR.H
==========================================================================================================================================*/
// 🧩 The Smoke & fire editor window opened from a FluidPlume record's inspector: the HMI prototype's "Smoke & fire" page in C++ — Domain, Source,
//    Simulation, Appearance, Fire and Presets — editing the record's 33 gas readings (GasParameterCatalogue).
//
// 📝 The window has no preview viewport yet. The prototype shows the plume in its own scene through the illumination host's march, a composite the
//    editor application does not host; what the window shows of the running plume is the host's readout (AssignReadout).

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Scene/EngineConfigurations.h"
#include "Engine/Scene/RecordToken.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{

#if FRONTIER_EDITOR

// 📝 What the running plume tells the Domain and Presets cards (the host fills it from its SmokeSequence each refresh). Without one the cards read
//    "no solver": the editor never shows counters nothing is counting.
struct FluidPlumeReadout
{
    uint32_t Grade       = 2;       // [-] the grade the plume is built at: the Render settings' quality tier (0 low, 1 medium, 2 high)
    bool     Resident    = false;   // [-] the lattices hold state
    uint32_t Sources     = 0;       // [-] emitters this refresh
    uint32_t Ticks       = 0;       // [-] transport steps taken
    uint32_t Relocations = 0;       // [-] moves of the toroidal window
};

class FluidPlumeEditor
{
  public:
    // 📝 Lays the window (a modal popup, as the Particle editor's is) on the record's readings, for as long as the record's editor is the active one.
    static void ConstructInterface(RecordToken Token, FluidPlumeConfiguration& Parameters);

    // 📝 The host that runs the plume says what it is doing; RevokeReadout says it stopped.
    static void AssignReadout(const FluidPlumeReadout& Readout);
    static void RevokeReadout() noexcept;
    [[nodiscard]] static const FluidPlumeReadout* QueryReadout() noexcept;

    // 📝 The verbs the cards raised that only the host can carry out ("clear-smoke": empty the lattices). The call takes them.
    [[nodiscard]] static std::vector<std::string> TakeRequests();
};

#endif // FRONTIER_EDITOR

} // namespace Frontier
