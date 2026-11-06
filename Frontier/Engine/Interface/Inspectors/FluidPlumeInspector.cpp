/*==========================================================================================================================================
                                                          FLUIDPLUMEINSPECTOR.CPP
==========================================================================================================================================*/
// 🧩 FluidPlumeConfiguration: the plume's figures at a glance, and the Smoke & fire editor that edits them.

#include "Engine/Interface/Inspectors/FluidPlumeInspector.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/FluidPlumeEditor.h"
#include "Engine/Interface/FluidPlumeEditor/FluidPlumePages.h"
#include "Engine/Interface/InspectorControls.h"

#include <cmath>
#include <cstdio>
#include <iterator>
#include <string>

#endif

namespace Frontier
{

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

const char* FluidPlumeInspector::QuerySectionNote() const noexcept
{
    // 📝 "128×128×32 Eulerian": the fine lattice of the grade the host builds the plume at (High without a host).
    static std::string Note;
    const FluidPlumeReadout* Readout = FluidPlumeEditor::QueryReadout();
    Note = FluidPlumePages::FormatLatticeExtent(Readout != nullptr ? Readout->Grade : 2u) + " Eulerian";
    return Note.c_str();
}

void FluidPlumeInspector::ConstructEditor(RecordToken Token)
{
    FluidPlumeEditor::ConstructInterface(Token, Parameters);
}

void FluidPlumeInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>&) {}

void FluidPlumeInspector::ConstructInspector()
{
    const InspectorPalette Palette = InspectorPalette::Resolve();
    const EditorTypefaces  Faces   = EditorTypefaces::Resolve();
    const FluidDynamics::GasParameters& Gas = Parameters.Gas;

    // 📝 The record's figures: the lattice it is solved on, the wind, the flame's core and what the host's solver is doing.
    const FluidPlumeReadout* Readout = FluidPlumeEditor::QueryReadout();
    const std::string        Lattice = FluidPlumePages::FormatLatticeExtent(Readout != nullptr ? Readout->Grade : 2u);
    char                     Wind[48], Flame[48];
    std::snprintf(Wind, sizeof(Wind), "%.1f m/s \xC2\xB7 %.0f\xC2\xB0", Gas.WindSpeed, Gas.WindHeading);
    std::snprintf(Flame, sizeof(Flame), "%.0f K", std::floor(Gas.FlameCore * Gas.FlameTemperature + 0.5));
    const InspectorReadoutCell Cells[] = {
        { "Lattice", Lattice.c_str() },
        { "Wind", Wind },
        { "Flame core", Flame },
        { "Solver", Readout == nullptr ? "none" : Readout->Resident ? "resident" : "idle" },
    };
    InspectorControls::ConstructReadoutStrip(Palette, Faces, Cells, std::size(Cells));
    InspectorControls::ConstructMutedNote(Palette, Faces,
                                          "The tyres' smoke and fire, solved on a lattice that follows the car. The Plume Editor opens a window with an Outliner of "
                                          "its parts, the viewport, an Inspector for each part and the libraries of looks and grades.");
}

#elif FRONTIER_EDITOR

const char* FluidPlumeInspector::QuerySectionNote() const noexcept { return ""; }
void FluidPlumeInspector::ConstructEditor(RecordToken) {}
void FluidPlumeInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>&) {}
void FluidPlumeInspector::ConstructInspector() {}

#endif

} // namespace Frontier
