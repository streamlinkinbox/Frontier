/*==========================================================================================================================================
                                                        FLUIDVOLUMEINSPECTOR.CPP
==========================================================================================================================================*/
// 🧩 FluidVolumeConfiguration: the volume's figures at a glance, and the Fluid editor that edits them.

#include "Engine/Interface/Inspectors/FluidVolumeInspector.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/FluidVolumeEditor.h"
#include "Engine/Interface/FluidEditor/FluidEditorPages.h"
#include "Engine/Interface/InspectorControls.h"

#include <cstdio>
#include <iterator>
#include <string>

#endif

namespace Frontier
{

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

const char* FluidVolumeInspector::QuerySectionNote() const noexcept
{
    // 📝 "48³ Eulerian": the lattice the volume is solved on.
    static std::string Note;
    Note = FluidEditorPages::FormatLattice(Parameters.Gas.LatticeResolution) + " Eulerian";
    return Note.c_str();
}

void FluidVolumeInspector::ConstructEditor(RecordToken Token)
{
    FluidVolumeEditor::ConstructInterface(Token, Parameters);
}

void FluidVolumeInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>&) {}

void FluidVolumeInspector::ConstructInspector()
{
    const InspectorPalette Palette = InspectorPalette::Resolve();
    const EditorTypefaces  Faces   = EditorTypefaces::Resolve();
    const GasSettings&     Gas     = Parameters.Gas;

    // 📝 The record's figures: the lattice, the domain, the preset it came from and what the host's solver is doing.
    const FluidVolumeReadout* Readout = FluidVolumeEditor::QueryReadout();
    const GasPreset*          Source  = FindGasPreset(Parameters.Preset.c_str());
    char                      Domain[48];
    std::snprintf(Domain, sizeof(Domain), "%.2f \xC3\x97 %.2f \xC3\x97 %.2f m", Gas.BoundsWidth, Gas.BoundsHeight, Gas.BoundsWidth);
    const std::string          Lattice = FluidEditorPages::FormatLattice(Gas.LatticeResolution);
    const InspectorReadoutCell Cells[] = {
        { "Lattice", Lattice.c_str() },
        { "Domain", Domain },
        { "Preset", Source != nullptr ? Source->Name : "custom" },
        { "Solver", Readout == nullptr ? "none" : "running" },
    };
    InspectorControls::ConstructReadoutStrip(Palette, Faces, Cells, std::size(Cells));
    InspectorControls::ConstructMutedNote(Palette, Faces,
                                          "A box of fire, smoke or a blast, solved on a lattice that stays where it is put. The Fluid Editor opens a window with an Outliner of its "
                                          "parts, the viewport, an Inspector for each part and the presets.");
}

#elif FRONTIER_EDITOR

const char* FluidVolumeInspector::QuerySectionNote() const noexcept { return ""; }
void FluidVolumeInspector::ConstructEditor(RecordToken) {}
void FluidVolumeInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>&) {}
void FluidVolumeInspector::ConstructInspector() {}

#endif

} // namespace Frontier
