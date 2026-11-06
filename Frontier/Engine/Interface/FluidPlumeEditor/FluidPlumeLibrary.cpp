/*==========================================================================================================================================
                                                          FLUIDPLUMELIBRARY.CPP
==========================================================================================================================================*/
// 🧩 See FluidPlumeLibrary.h.

#include "Engine/Interface/FluidPlumeEditor/FluidPlumeLibrary.h"

#if FRONTIER_EDITOR

#include <cctype>
#include <cstdio>
#include <cstring>

namespace Frontier
{

namespace
{

using namespace FluidDynamics;

// 📝 What each look is, in the window's words. The group is where the look's changes lie (the source and the appearance are Smoke, the flame is Fire,
//    the wind is Wind). Legacy billows carries the prototype's own comment on the preset.
struct LookWords
{
    std::string_view Name;
    std::string_view Group;
    std::string_view Lead;
};

constexpr LookWords Words[] = {
    { "Tyre smoke", "Smoke", "The catalogue's own gas, what a new record reads. Applying it returns every reading to its default." },
    { "Thick burnout", "Smoke", "More and denser smoke from a hard burnout." },
    { "Light haze", "Smoke", "Thin smoke that fades quickly." },
    { "Inferno", "Fire", "Hotter, brighter flames with more soot and a stronger lift." },
    { "Crosswind", "Wind", "A strong wind that bends the plume and thins it faster." },
    { "Legacy billows", "Smoke", "The look before the reference pass: big warped billows, softer and cloudier." },
};

std::string MakeIdentifier(std::string_view Name)
{
    std::string Identifier;
    for (const char Letter : Name) Identifier += Letter == ' ' ? '-' : static_cast<char>(std::tolower(static_cast<unsigned char>(Letter)));
    return Identifier;
}

std::vector<FluidPlumeLook> ConstructLooks()
{
    const GasParameters         Defaults = ConstructGasParameters();
    std::vector<FluidPlumeLook> Looks;
    for (const GasPreset& Preset : QueryGasPresets())
    {
        FluidPlumeLook Look;
        Look.Name       = std::string(Preset.Name);
        Look.Identifier = MakeIdentifier(Preset.Name);
        Look.Preset     = &Preset;
        Look.Values     = ApplyGasPreset(Preset);
        Look.Group      = "Looks";
        Look.Lead       = "A look from the prototype's library.";
        for (const LookWords& Entry : Words)
        {
            if (Entry.Name != Preset.Name) continue;
            Look.Group = std::string(Entry.Group);
            Look.Lead  = std::string(Entry.Lead);
        }
        Look.Differences = FluidPlumeLibrary::CountDifferences(Look.Values, Defaults);
        for (const GasParameterEntry& Entry : QueryGasParameterCatalogue())
        {
            const double Value = Look.Values.*(Entry.Field);
            if (Value == Entry.Default) continue;
            if (!Look.Changes.empty()) Look.Changes += " \xC2\xB7 ";
            Look.Changes += std::string(Entry.Label) + " " + FluidPlumeLibrary::FormatReading(Entry, Value);
        }
        Looks.push_back(std::move(Look));
    }
    return Looks;
}

} // namespace

const std::vector<FluidPlumeLook>& FluidPlumeLibrary::QueryLooks()
{
    static const std::vector<FluidPlumeLook> Looks = ConstructLooks();
    return Looks;
}

const std::vector<std::string>& FluidPlumeLibrary::QueryGroups()
{
    static const std::vector<std::string> Groups = [] {
        std::vector<std::string> Found;
        for (const FluidPlumeLook& Look : QueryLooks())
        {
            bool Known = false;
            for (const std::string& Group : Found) Known = Known || Group == Look.Group;
            if (!Known) Found.push_back(Look.Group);
        }
        return Found;
    }();
    return Groups;
}

const FluidPlumeLook* FluidPlumeLibrary::FindLook(std::string_view Identifier) noexcept
{
    for (const FluidPlumeLook& Look : QueryLooks())
    {
        if (Look.Identifier == Identifier) return &Look;
    }
    return nullptr;
}

uint32_t FluidPlumeLibrary::CountDifferences(const GasParameters& Left, const GasParameters& Right) noexcept
{
    uint32_t Count = 0;
    for (const GasParameterEntry& Entry : QueryGasParameterCatalogue())
    {
        if (Left.*(Entry.Field) != Right.*(Entry.Field)) ++Count;
    }
    return Count;
}

const FluidPlumeLook* FluidPlumeLibrary::MatchLook(const GasParameters& Gas) noexcept
{
    for (const FluidPlumeLook& Look : QueryLooks())
    {
        if (CountDifferences(Gas, Look.Values) == 0) return &Look;
    }
    return nullptr;
}

std::string FluidPlumeLibrary::FormatReading(const GasParameterEntry& Entry, double Value)
{
    const int Digits = Entry.Step >= 1.0 ? 0 : Entry.Step >= 0.1 ? 1 : Entry.Step >= 0.01 ? 2 : 3;
    char      Figure[48];
    std::snprintf(Figure, sizeof(Figure), "%.*f", Digits, Value);
    std::string Text = Figure;
    if (!Entry.Unit.empty())
    {
        // 📝 The slider sets its unit a few pixels after the figure; in a line of words a degree sign sits against it.
        if (Entry.Unit != "\xC2\xB0") Text += ' ';
        Text += std::string(Entry.Unit);
    }
    return Text;
}

} // namespace Frontier

#endif
