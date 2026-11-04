/*==========================================================================================================================================
                                                              VIEWSECTION.CPP
==========================================================================================================================================*/
// 🧩 Debug views: the demo's fifteen inspection views and the analytic illumination as a view of its own, and what view 6 reads.

#include "Engine/Interface/Inspectors/RenderSettings/RenderSettingsSections.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/ChoiceList.h"
#include "Engine/Interface/Controls/ChoiceRow.h"
#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/InspectorControls.h"
#include "Engine/Interface/Inspectors/RenderInspectionModes.h"

#include <algorithm>
#include <array>

namespace Frontier
{

bool ViewSection::Construct(ControlColumn& Page, RenderPipelineConfiguration& P, const RenderSettingsReading& Reading)
{
    using namespace SectionRows;
    const int Chosen = P.AnalyticIllumination ? RenderInspectionModes::AnalyticView : std::clamp(P.InspectionModeIndex, 0, 14);
    BeginSection(Page, "Debug views", "Bug", RenderInspectionModes::Views[Chosen].Name,
                 "What the viewport shows. Views 00 to 14 are the demo's buffers; the fast preview swaps the global illumination for the "
                 "analytic illumination, which needs no rays or history (also on the viewport bar).");
    std::array<ChoiceEntry, RenderInspectionModes::ViewCount> Entries{};
    for (size_t Index = 0; Index < Entries.size(); ++Index)
    {
        const RenderInspectionView& View = RenderInspectionModes::Views[Index];
        Entries[Index].Group   = View.Group;
        Entries[Index].Chip    = View.Chip;
        Entries[Index].Caption = View.Name;
        Entries[Index].Note    = View.Note;
        Entries[Index].Tag     = View.NeedsVirtualized && !P.VirtualizedGeometry ? "needs virtualized geometry" : nullptr;
    }
    int  Selection = Chosen;
    bool Changed   = false;
    if (ChoiceList::Construct(Page, "DebugViews", Entries, Selection))
    {
        P.AnalyticIllumination = Selection == RenderInspectionModes::AnalyticView;
        if (!P.AnalyticIllumination) P.InspectionModeIndex = Selection;
        Changed = true;
    }
    SubHeading(Page, "BVH / distance field view");
    int Reads = std::clamp(P.DistanceInspection, 1, 3) - 1;
    const char* Unavailable = Chosen == 6 ? nullptr : "Shown in view 06 only";
    if (ChoiceRow::Construct(Page, "DistanceInspection", Reading.RayTraced ? "View 06 reads the BVH as" : "View 06 reads the distance field as",
                             RenderInspectionModes::DistanceNames, 3, Reads, Unavailable == nullptr, nullptr, Unavailable))
    {
        P.DistanceInspection = Reads + 1;
        Changed              = true;
    }
    EndSection(Page);
    return Changed;
}

} // namespace Frontier

#endif
