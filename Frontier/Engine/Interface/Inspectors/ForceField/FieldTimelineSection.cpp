/*==========================================================================================================================================
                                                         FIELDTIMELINESECTION.CPP
==========================================================================================================================================*/
// 🧩 Timeline: the clock windowed and triggered forces follow (its length, rate and play switch), what it holds now, and Fire
//    for the first three triggered forces.

#include "Engine/Interface/Inspectors/ForceField/ForceFieldSections.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/InspectorRuntime.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace Frontier
{

namespace
{

constexpr const char* RateOptions[] = {"\xC3\x97" "1 \xC2\xB7 real time", "\xC3\x97" "10", "\xC3\x97" "60 \xC2\xB7 a minute a second"};
constexpr float       Rates[]       = {1.0f, 10.0f, 60.0f};

} // namespace

bool FieldTimelineSection::Construct(ControlColumn& Page, ForceFieldConfiguration& Field, const ForceFieldPageReading& Reading)
{
    using namespace SectionRows;
    char Status[48];
    std::snprintf(Status, sizeof(Status), "%.0f s \xC2\xB7 \xC3\x97%.0f", Field.TimelineDurationSeconds, Field.TimelineRate);
    BeginSection(Page, "Timeline", "Timeline", Status,
                 "Forces with a time window or a trigger get a lane under the Field view. Begins, Lasts and Repeats follow the eos "
                 "taxonomy (Lasts 0 = forever, Repeats 0 = once); the ramps follow the native trigger envelope.");
    bool Changed = Amount(Page, "TimelineLength", "Timeline length", Field.TimelineDurationSeconds, 30.0f, 600.0f, 10.0f, "s");
    int  Rate    = Field.TimelineRate >= 30.0f ? 2 : (Field.TimelineRate >= 5.0f ? 1 : 0);
    if (ChoiceRow::Construct(Page, "TimelineRate", "Playback rate", RateOptions, 3, Rate))
    {
        Field.TimelineRate = Rates[Rate];
        Changed            = true;
    }
    Changed |= Switch(Page, "TimelinePlaying", "Playing", Field.TimelinePlaying, nullptr,
                      "At the end the clock returns to 0 s; fired triggers keep their firing time");

    // 📝 What the clock holds: the time, the windowed forces and the triggered ones.
    uint32_t Windows = 0, Triggers = 0;
    for (const ForceSourceConfiguration& Source : Reading.Sources)
    {
        Windows += Source.Activation == ForceActivation::Window ? 1u : 0u;
        Triggers += Source.Activation == ForceActivation::Trigger ? 1u : 0u;
    }
    char Clock[32];
    std::snprintf(Clock, sizeof(Clock), "%.1f", std::fmod(std::max(0.0, Field.TimelineSeconds), std::max(1.0f, Field.TimelineDurationSeconds)));
    const MetricTile Tiles[3] = {{"Clock", Clock, "s"}, {"Windowed", std::to_string(Windows), "forces"}, {"Triggered", std::to_string(Triggers), "forces"}};
    MetricTiles::Construct(Page, Tiles, 3, MetricTiles::NoLead);

    // 📝 Fire asks the workspace to stamp the trigger with the clock, as the lane's FIRE does (TriggeredAtSeconds is runtime only;
    //    below zero is armed), so both write the same Log Stream line.
    std::vector<const ForceSourceAccess*> Armed;
    std::vector<std::string>              Captions;
    if (Reading.Access != nullptr)
        for (const ForceSourceAccess& Entry : Reading.Access->ForceSources)
            if (Entry.Configuration != nullptr && Entry.Configuration->Activation == ForceActivation::Trigger && Armed.size() < 3)
            {
                Armed.push_back(&Entry);
                Captions.push_back("Fire " + Entry.Name);
            }
    if (!Armed.empty())
    {
        std::array<ActionEntry, 3> Actions{};
        constexpr const char*      Ids[3] = {"FireFirst", "FireSecond", "FireThird"};
        for (size_t Index = 0; Index < Armed.size(); ++Index)
            Actions[Index] = {Ids[Index], Captions[Index].c_str(), Armed[Index]->Configuration->Enabled, "Starts the force's envelope at the clock's time"};
        ActionRowLook Look;
        Look.MarginTop    = 4;
        Look.MarginBottom = 10;
        if (const int Pressed = ActionRow::Construct(Page, std::span<const ActionEntry>(Actions.data(), Armed.size()), Look); Pressed >= 0)
            InspectorRuntime::Instance().RequestWorkspaceAction("fire:" + std::to_string(Armed[Pressed]->Token));
    }
    else
    {
        CaptionText::Construct(Page, "No triggered force: give a force the Trigger activation to fire it from here.", 2.0f, 8.0f);
    }
    EndSection(Page);
    return Changed;
}

} // namespace Frontier

#endif
