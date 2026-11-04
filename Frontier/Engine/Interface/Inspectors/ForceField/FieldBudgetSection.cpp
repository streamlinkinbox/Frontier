/*==========================================================================================================================================
                                                          FIELDBUDGETSECTION.CPP
==========================================================================================================================================*/
// 🧩 Budget: which forces bake into the lattice and which are evaluated per receiver, and what the Field view's sampling costs.

#include "Engine/Interface/Inspectors/ForceField/ForceFieldSections.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/Inspectors/ForceField/ForceFieldPieces.h"

#include <cstdio>
#include <string>

namespace Frontier
{

bool FieldBudgetSection::Construct(ControlColumn& Page, ForceFieldConfiguration&, const ForceFieldPageReading& Reading)
{
    using namespace SectionRows;
    const ForceFieldBudget& Budget = Reading.Budget;
    char Status[48];
    std::snprintf(Status, sizeof(Status), "%u / %u slots", Budget.Baked, Budget.GpuSlots);
    BeginSection(Page, "Budget", "Gauge", Status,
                 "Flow forces sum into the lattice and then cost one sample per receiver however many there are; accelerate forces, "
                 "and any flow force narrowed to some receivers, are evaluated per receiver per step.");
    char Slots[32], Memory[32];
    std::snprintf(Slots, sizeof(Slots), "%u / %u", Budget.Baked, Budget.GpuSlots);
    ForceFieldPieces::FormatBytes(Budget.TotalBytes, Memory, sizeof(Memory));
    const MetricTile Forces[3] = {{"Lattice slots", Slots, ""}, {"Per receiver", std::to_string(Budget.Live), "fields"},
                                  {"Damp volumes", std::to_string(Budget.Damping), ""}};
    MetricTiles::Construct(Page, Forces, 3, MetricTiles::NoLead);
    const ForceFieldReadings* Readings = Reading.Access != nullptr ? &Reading.Access->FieldReadings : nullptr;
    if (Readings != nullptr && Readings->Measured)
    {
        char Samples[32], Milliseconds[32];
        std::snprintf(Samples, sizeof(Samples), "%u", Readings->Samples);
        std::snprintf(Milliseconds, sizeof(Milliseconds), "%.1f", Readings->Milliseconds);
        const MetricTile View[3] = {{"View samples", Samples, ""}, {"View refresh", Milliseconds, "ms"},
                                    {"Disabled", std::to_string(Budget.Disabled), "forces"}};
        MetricTiles::Construct(Page, View, 3, MetricTiles::NoLead);
    }
    else
    {
        CaptionText::Construct(Page, "The Field view's sampling cost shows once the view has refreshed.", 2.0f, 8.0f);
    }
    char Note[160];
    std::snprintf(Note, sizeof(Note), "The proposed cascades hold %s of lattice memory; the GPU's force array has %u slots.", Memory,
                  Budget.GpuSlots);
    CaptionText::Construct(Page, Note, 4.0f, 8.0f);
    EndSection(Page);
    return false;
}

} // namespace Frontier

#endif
