/*==========================================================================================================================================
                                                             TIMINGSECTION.CPP
==========================================================================================================================================*/
// 🧩 Refresh timing: the measured refresh times (latest, smoothed, median and slowest of the recent ones), live refresh and the
//    budget the chart draws.

#include "Engine/Interface/Inspectors/RenderSettings/RenderSettingsSections.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/ChoiceRow.h"
#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/InspectorControls.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace Frontier
{

bool TimingSection::Construct(ControlColumn& Page, RenderPipelineConfiguration& P, const RenderSettingsReading& Reading)
{
    using namespace SectionRows;
    const auto& M = P.Measured;
    char Status[48];
    if (M.Presenting) std::snprintf(Status, sizeof(Status), "%.1f ms", M.RefreshMs);
    else std::snprintf(Status, sizeof(Status), "No image");
    BeginSection(Page, "Refresh timing", "Activity", Status,
                 "Measured, not estimated: each refresh from its first submission to its fence (encoding and GPU work). The editor's frame "
                 "rate stops at the display's, so the refresh time shows the headroom first.");
    bool Changed = Switch(Page, "LiveRefresh", "Live refresh", P.LiveRefresh, nullptr, "Off holds the last image: compare a still against a change");

    const uint32_t Count = std::min<uint32_t>(M.HistoryCount, static_cast<uint32_t>(M.HistoryMs.size()));
    std::vector<float> Recent(M.HistoryMs.begin(), M.HistoryMs.begin() + Count);
    std::sort(Recent.begin(), Recent.end());
    char Latest[24], Smoothed[24], Median[24], Slowest[24];
    std::snprintf(Latest, sizeof(Latest), "%.1f", M.LatestMs);
    std::snprintf(Smoothed, sizeof(Smoothed), "%.1f", M.RefreshMs);
    std::snprintf(Median, sizeof(Median), "%.1f", Recent.empty() ? 0.0f : Recent[Recent.size() / 2]);
    std::snprintf(Slowest, sizeof(Slowest), "%.1f", Recent.empty() ? 0.0f : Recent.back());
    const MetricTile Upper[2] = {{"Latest", M.Presenting ? Latest : "\xE2\x80\x94", "ms"}, {"Smoothed", M.Presenting ? Smoothed : "\xE2\x80\x94", "ms"}};
    const MetricTile Lower[2] = {{"Median of recent", M.Presenting ? Median : "\xE2\x80\x94", "ms"},
                                 {"Slowest of recent", M.Presenting ? Slowest : "\xE2\x80\x94", "ms"}};
    MetricTiles::Construct(Page, Upper, 2, MetricTiles::NoLead);
    MetricTiles::Construct(Page, Lower, 2, MetricTiles::NoLead);

    static const char* const Budgets[3] = {"30 fps", "60 fps", "120 fps"};
    int Budget = P.TargetRate <= 30 ? 0 : (P.TargetRate >= 120 ? 2 : 1);
    if (ChoiceRow::Construct(Page, "TargetRate", "Budget per refresh", Budgets, 3, Budget, true, "the chart's line"))
    {
        P.TargetRate = Budget == 0 ? 30 : (Budget == 1 ? 60 : 120);
        Changed      = true;
    }
    char Line[200];
    std::snprintf(Line, sizeof(Line), "%llu refreshes on %s \xC2\xB7 render %u\xC3\x97%u, trace %u\xC3\x97%u.", static_cast<unsigned long long>(M.RefreshCount),
                  M.Adapter.empty() ? "no adapter yet" : M.Adapter.c_str(), Reading.Render[0], Reading.Render[1], Reading.Trace[0], Reading.Trace[1]);
    CaptionText::Construct(Page, Line, 2.0f, 12.0f);
    EndSection(Page);
    return Changed;
}

} // namespace Frontier

#endif
