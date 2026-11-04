/*==========================================================================================================================================
                                                        FORCEFIELDPAGEREADING.CPP
==========================================================================================================================================*/
// 🧩 The Force Field page's reading of the level: the forces the workspace listed, their budget and the count of each
//    contribution, once per page refresh.

#include "Engine/Interface/Inspectors/ForceField/ForceFieldSections.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/InspectorRuntime.h"

namespace Frontier
{

ForceFieldPageReading ResolveForceFieldPageReading(const ForceFieldConfiguration& Field)
{
    ForceFieldPageReading Reading;
    Reading.Access          = &InspectorRuntime::Instance().QueryEnvironmentAccess();
    Reading.TimelineSeconds = Field.TimelineSeconds;
    for (const ForceSourceAccess& Entry : Reading.Access->ForceSources)
    {
        if (Entry.Configuration == nullptr) continue;
        Reading.Sources.push_back(*Entry.Configuration);
        Reading.Names.push_back(Entry.Name);
        Reading.Tokens.push_back(Entry.Token);
        const ForceContribution Contribution = ResolveForceSourceDescriptor(Entry.Configuration->Category).Contribution;
        ++Reading.Counts[static_cast<size_t>(Contribution) < Reading.Counts.size() ? static_cast<size_t>(Contribution) : 0];
    }
    Reading.Budget = ResolveForceFieldBudget(Field, Reading.Sources);
    return Reading;
}

} // namespace Frontier

#endif
