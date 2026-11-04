/*==========================================================================================================================================
                                                          FIELDFORCESSECTION.CPP
==========================================================================================================================================*/
// 🧩 Localized forces: every ForceSource under the field, grouped by what it contributes (flow, acceleration, damping), with
//    its type, strength and status; a row selects its record, and the action opens the Construct dialog's Forces group.

#include "Engine/Interface/Inspectors/ForceField/ForceFieldSections.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/InspectorRuntime.h"

#include <cstdio>
#include <string>
#include <vector>

namespace Frontier
{

bool FieldForcesSection::Construct(ControlColumn& Page, ForceFieldConfiguration&, const ForceFieldPageReading& Reading)
{
    using namespace SectionRows;
    char Status[32];
    std::snprintf(Status, sizeof(Status), "%zu force%s", Reading.Sources.size(), Reading.Sources.size() == 1 ? "" : "s");
    BeginSection(Page, "Localized forces", "Target", Status,
                 "The forces under the field, from a gust puff to a mesocyclone. Each sums into the field where it reaches; select "
                 "one to edit its strength, reach and timing.");
    if (Reading.Sources.empty())
        CaptionText::Construct(Page, "No localized force yet: the field carries the Wind alone.", 0.0f, 10.0f);
    constexpr ForceContribution Contributions[] = {ForceContribution::Flow, ForceContribution::Accelerate, ForceContribution::Damp};
    for (const ForceContribution Contribution : Contributions)
    {
        std::vector<size_t> Members;
        for (size_t Index = 0; Index < Reading.Sources.size(); ++Index)
            if (ResolveForceSourceDescriptor(Reading.Sources[Index].Category).Contribution == Contribution) Members.push_back(Index);
        if (Members.empty()) continue;
        const ForceContributionDescriptor& Facts = ResolveForceContributionDescriptor(Contribution);
        char Heading[64];
        std::snprintf(Heading, sizeof(Heading), "%s \xC2\xB7 %s", Facts.Label, Facts.Unit);
        SubHeading(Page, Heading);
        CaptionText::Construct(Page, Facts.Blurb, 2.0f, 8.0f);
        std::vector<std::string> Values;
        std::vector<ChannelRow>  Rows;
        Values.reserve(Members.size());
        for (const size_t Index : Members)
        {
            const ForceSourceConfiguration& Source = Reading.Sources[Index];
            char Value[48];
            std::snprintf(Value, sizeof(Value), "%.*f %s", Source.Strength >= 100.0 ? 0 : 2, Source.Strength, Facts.Unit);
            Values.emplace_back(Value);
        }
        for (size_t Row = 0; Row < Members.size(); ++Row)
        {
            const ForceSourceConfiguration& Source = Reading.Sources[Members[Row]];
            ChannelRow Entry;
            Entry.Name   = Reading.Names[Members[Row]].c_str();
            Entry.Source = ResolveForceSourceDescriptor(Source.Category).Name;
            Entry.Value  = Values[Row].c_str();
            Entry.Mode   = Source.Enabled ? ResolveForceSourceStatusLabel(ResolveForceActivation(Source, Reading.TimelineSeconds).Status) : "Off";
            Entry.Swatch = (Facts.ColourRgb << 8u) | 0xFFu;
            Rows.push_back(Entry);
        }
        const std::string Id      = std::string("Forces") + Facts.Identifier;
        const int         Pressed = ChannelRows::Construct(Page, Id.c_str(), Rows.data(), static_cast<int>(Rows.size()), -1);
        if (Pressed >= 0) InspectorRuntime::Instance().RequestWorkspaceAction("select:" + std::to_string(Reading.Tokens[Members[Pressed]]));
    }
    if (NextButton::Construct(Page, "AddLocalizedForce", "Add a localized force \xE2\x86\x97", Reading.Access != nullptr && Reading.Access->Forces != nullptr,
                              "Add a Force Field record first"))
        InspectorRuntime::Instance().RequestWorkspaceAction("add-force");
    EndSection(Page);
    return false;
}

} // namespace Frontier

#endif
