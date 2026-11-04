/*==========================================================================================================================================
                                                           FORCEFIELDPIECES.H
==========================================================================================================================================*/
// 🧩 What the Force Field, Wind and force pages draw alike: the hero card around a live figure, its series chart (a ring of
//    readings under a dashed target), the curve plot of the gust and altitude sections, and the colour of each contribution.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Scene/EngineConfigurations.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include <imgui.h>

namespace Frontier
{

class ControlColumn;

namespace ForceFieldPieces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    COLOURS AND FIGURES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] ImU32 ResolveContributionColour(ForceContribution Contribution, float Alpha = 1.0f) noexcept;
// 📝 "16.0 MB", "820 KB".
void FormatBytes(uint64_t Bytes, char* Text, size_t Capacity) noexcept;
// 📝 The figure with the digits its size calls for: one decimal below 100, none above.
void FormatFigure(double Reading, char* Text, size_t Capacity) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                    HERO CARD
//------------------------------------------------------------------------------------------------------------------------

enum class HeroTone : uint8_t
{
    Calm    = 0,
    Warm    = 1,
    Severe  = 2,
    Waiting = 3,
};

// 📝 The card's chip (tone glyph and text), its corner tag, the title over the figure and the line beside the unit.
struct HeroFigures
{
    const char* Chip   = "";
    HeroTone    Tone   = HeroTone::Calm;
    const char* Tag    = "";
    const char* Title  = "";
    const char* Figure = "";
    const char* Unit   = "";
};

void BeginHero(ControlColumn& Page, const HeroFigures& Figures);
void EndHero(ControlColumn& Page, const char* Note);

//------------------------------------------------------------------------------------------------------------------------
//                                                    SERIES
//------------------------------------------------------------------------------------------------------------------------

// 📝 A ring of readings (Cursor is where the next one goes) charted oldest at the left under the dashed target line; the
//    chart's ceiling is the highest of the readings and the target, times Headroom.
struct SeriesLook
{
    double      Target      = 0.0;
    const char* TargetLabel = "";
    const char* Unit        = "";
    double      Headroom    = 1.25;
    float       Height      = 150.0f;   // [logical px] of a 640-wide canvas
    const char* Early       = "\xE2\x88\x92" "12 s";
    const char* Middle      = "\xE2\x88\x92" "6 s";
    const char* Late        = "now";
    const char* Empty       = "The chart fills as the view refreshes";
};

void ConstructSeries(ControlColumn& Page, const char* Id, const float* Ring, uint32_t Capacity, uint32_t Count, uint32_t Cursor,
                     const SeriesLook& Look);

//------------------------------------------------------------------------------------------------------------------------
//                                                    CURVE PLOT
//------------------------------------------------------------------------------------------------------------------------

struct PlotCurve
{
    std::vector<std::array<double, 2>> Points;
    ImU32                              Stroke = IM_COL32(222, 222, 222, 255);
    float                              Width  = 1.8f;
    bool                               Dashed = false;
    ImU32                              Fill   = 0;   // [-] 0: no fill under the curve
};

struct PlotGuide
{
    double      Y = 0.0;
    std::string Text;
};

struct PlotMarker
{
    double      X = 0.0;
    double      Y = 0.0;
    std::string Label;
};

struct PlotTick
{
    double      At = 0.0;
    std::string Text;
};

// 📝 Data ranges map onto a 640-wide canvas of the given height, with tick labels below and left and the caption under it.
struct CurvePlot
{
    std::array<double, 2>  XRange{0.0, 1.0};
    std::array<double, 2>  YRange{0.0, 1.0};
    std::vector<PlotCurve> Curves;
    std::vector<PlotGuide> Guides;
    std::vector<PlotMarker> Markers;
    std::vector<PlotTick>  XTicks;
    std::vector<PlotTick>  YTicks;
    std::string            Caption;
    float                  Height = 300.0f;   // [logical px]
};

void ConstructCurvePlot(ControlColumn& Page, const char* Id, const CurvePlot& Plot);

} // namespace ForceFieldPieces

} // namespace Frontier

#endif
