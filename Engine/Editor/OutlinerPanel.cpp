//============================================================================================================================================
//                                                     OUTLINERPANEL.CPP
//============================================================================================================================================
// 🧩 Development editor, left column — the record register: query line plus grouped rows. Static records this step.

#include "OutlinerPanel.h"

#include <imgui.h>
#include <imgui_internal.h>   // the two button silencers live in the vendor's private flags

#include <cctype>
#include <cstring>

namespace Frontier {

namespace {

//------------------------------------------------------------------------------------------------------------------------
//                                                       STATIC REGISTER
//------------------------------------------------------------------------------------------------------------------------

struct OutlinerGroup
{
    const char* Label;
    uint32_t    First;
    uint32_t    Count;
};

constexpr const char* kLabels[] = {
    "Sky Dome", "Sun Light", "Moon Light",          // Environment 0 … 2
    "Sea Surface", "Pool Surface",                   // Water 3 … 4
    "Chrome Sphere", "Clay Vase", "Marble Floor",    // Objects 5 … 7
    "Key Spot", "Rim Point", "Fill Rect",            // Lighting 8 … 10
    "Main Camera", "Top Camera",                     // Cameras 11 … 12
    "Bloom", "Vignette",                             // Effects 13 … 14
};

constexpr OutlinerGroup kGroups[] = {
    { "Environment",  0u, 3u },
    { "Water",        3u, 2u },
    { "Objects",      5u, 3u },
    { "Lighting",     8u, 3u },
    { "Cameras",     11u, 2u },
    { "Effects",     13u, 2u },
};

constexpr uint32_t kLabelCount = sizeof(kLabels) / sizeof(kLabels[0]);
constexpr uint32_t kGroupCount = sizeof(kGroups) / sizeof(kGroups[0]);

bool MatchesQuery(const char* Label, const char* Query) noexcept
{
    if (Query[0] == '\0')
        return true;
    const size_t LabelLength = std::strlen(Label);
    const size_t QueryLength = std::strlen(Query);
    if (QueryLength > LabelLength)
        return false;
    for (size_t Start = 0u; Start + QueryLength <= LabelLength; ++Start)
    {
        bool Hit = true;
        for (size_t I = 0u; I < QueryLength; ++I)
        {
            const int A = std::tolower(static_cast<unsigned char>(Label[Start + I]));
            const int B = std::tolower(static_cast<unsigned char>(Query[I]));
            if (A != B) { Hit = false; break; }
        }
        if (Hit)
            return true;
    }
    return false;
}

} // namespace

//============================================================================================================================================
//                                                          RECORD
//============================================================================================================================================

void OutlinerPanel::Record() noexcept
{
#ifdef FRONTIER_DEVELOPMENT
    // 🔴 NoTitleBar is deliberately NOT set: the vendor refuses a tab strip to any window carrying it, so
    //    hiding the caption that way would also deny a torn-off panel the trapezoid. Docked, the column's
    //    strip replaces the caption; floating, the panel keeps a strip of its own. The two silencers remove
    //    the caret and close mark the sheet has none of.
    ImGuiWindowClass Declared;
    Declared.DockingAlwaysTabBar      = true;
    Declared.DockNodeFlagsOverrideSet = static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_NoWindowMenuButton)
                                      | static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_NoCloseButton);
    ImGui::SetNextWindowClass(&Declared);

    if (!ImGui::Begin("Outliner", nullptr, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##OutlinerQuery", "Search records...", QueryText, sizeof(QueryText));
    ImGui::Separator();

    const bool Narrowing = QueryText[0] != '\0';
    for (uint32_t Group = 0u; Group < kGroupCount; ++Group)
    {
        uint32_t Hits = 0u;
        for (uint32_t I = 0u; I < kGroups[Group].Count; ++I)
            if (MatchesQuery(kLabels[kGroups[Group].First + I], QueryText))
                ++Hits;
        if (Hits == 0u)
            continue;

        // While narrowing every group with a hit stands open, so a match is never hidden inside a closed one.
        if (Narrowing)
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        else
            ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);

        if (ImGui::CollapsingHeader(kGroups[Group].Label))
        {
            ImGui::Indent();
            for (uint32_t I = 0u; I < kGroups[Group].Count; ++I)
            {
                const uint32_t Index = kGroups[Group].First + I;
                if (!MatchesQuery(kLabels[Index], QueryText))
                    continue;
                if (ImGui::Selectable(kLabels[Index], Picked == Index))
                    Picked = Index;
            }
            ImGui::Unindent();
        }
    }

    ImGui::End();
#endif
}

} // namespace Frontier
