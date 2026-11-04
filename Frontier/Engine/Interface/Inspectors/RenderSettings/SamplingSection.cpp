/*==========================================================================================================================================
                                                            SAMPLINGSECTION.CPP
==========================================================================================================================================*/
// 🧩 Resolution & sampling: the viewport scale, the trace resolution of ReSTIR GI and the reflections, paths per pixel and the
//    quality grade, with the render and trace extents drawn to scale and the grade's figures.

#include "Engine/Interface/Inspectors/RenderSettings/RenderSettingsSections.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/ChoiceRow.h"
#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/InspectorControls.h"

#include <algorithm>
#include <cstdio>

namespace Frontier
{

namespace
{

// 📝 The render target and, inside it from the same corner, the trace target, both to scale.
void ConstructExtentDiagram(ControlColumn& Page, const RenderSettingsReading& Reading, int TraceScale)
{
    const ControlTheme& Theme = Page.QueryTheme();
    DiagramPen          Pen   = DiagramCanvas::Begin(Page, "RenderExtents", 640, 300, 2, 14);
    Pen.Rect({0, 0}, {640, 300}, ResolveCssColour(0x0E0F11FF), 18);
    const float Fit    = std::min(560.0f / std::max(1u, Reading.Render[0]), 220.0f / std::max(1u, Reading.Render[1]));
    const float Width  = Reading.Render[0] * Fit;
    const float Height = Reading.Render[1] * Fit;
    const ImVec2 Corner(40, 34);
    Pen.Rect(Corner, {Width, Height}, ResolveCssColour(0x22262BFF), 6);
    Pen.Line(Corner, {Corner.x + Width, Corner.y}, Theme.Accent, 1.5f);
    Pen.Line({Corner.x + Width, Corner.y}, {Corner.x + Width, Corner.y + Height}, Theme.Accent, 1.5f);
    Pen.Line({Corner.x + Width, Corner.y + Height}, {Corner.x, Corner.y + Height}, Theme.Accent, 1.5f);
    Pen.Line({Corner.x, Corner.y + Height}, Corner, Theme.Accent, 1.5f);
    const float TraceWidth  = Width / static_cast<float>(TraceScale);
    const float TraceHeight = Height / static_cast<float>(TraceScale);
    Pen.Rect(Corner, {TraceWidth, TraceHeight}, ResolveCssColour(0x9FB4D633), 4);
    // 📝 One dot per traced pixel at this scale, a sample of the lattice (every 12th along each axis).
    for (float Y = 6; Y < TraceHeight; Y += 12)
        for (float X = 6; X < TraceWidth; X += 12) Pen.Circle({Corner.x + X, Corner.y + Y}, 1.2f, ResolveCssColour(0xC9D6EA80));
    char Text[64];
    std::snprintf(Text, sizeof(Text), "Render %u\xC3\x97%u", Reading.Render[0], Reading.Render[1]);
    Pen.Text({Corner.x + Width - 10, Corner.y + 24}, Text, Theme.Ink, 18, 1);
    std::snprintf(Text, sizeof(Text), "Trace %u\xC3\x97%u", Reading.Trace[0], Reading.Trace[1]);
    Pen.Text({Corner.x + 10, Corner.y + std::max(24.0f, TraceHeight - 12)}, Text, Theme.Ink, 18, -1);
    const char* Share = TraceScale == 1 ? "Full trace \xC2\xB7 every render pixel" : TraceScale == 2 ? "Half trace \xC2\xB7 1 render pixel in 4" : "Quarter trace \xC2\xB7 1 in 16";
    Pen.Text({40, 288}, Share, Theme.Muted, 16, -1);
    Pen.Text({600, 288}, Reading.Measured ? "MEASURED" : "FOR A 1600\xC3\x97" "900 PANEL", Theme.Muted, 15, 1);
}

} // namespace

bool SamplingSection::Construct(ControlColumn& Page, RenderPipelineConfiguration& P, const RenderSettingsReading& Reading)
{
    using namespace SectionRows;
    char Status[64];
    std::snprintf(Status, sizeof(Status), "%u\xC3\x97%u \xC2\xB7 %s trace", Reading.Render[0], Reading.Render[1],
                  P.TraceScale == 1 ? "Full" : P.TraceScale == 2 ? "Half" : "Quarter");
    BeginSection(Page, "Resolution & sampling", "Monitor", Status,
                 "The render target is the viewport times the viewport scale, under the grade's pixel ceiling. ReSTIR GI and the reflections trace "
                 "at the trace resolution: Half (the demo) traces one pixel in four, Full traces every pixel, four times the rays, for sharper "
                 "bounce light and reflections.");
    ConstructExtentDiagram(Page, Reading, std::clamp(P.TraceScale, 1, 4));
    bool Changed = Amount(Page, "RenderScale", "Viewport scale", P.RenderScale, 0.25f, 2.0f, 0.05f, "\xC3\x97", nullptr,
                          "Multiplies the grade's render scale; the grade's pixel ceiling still caps it. Rebuilds the screen targets.");
    static const char* const TraceOptions[3] = {"Full", "Half", "Quarter"};
    int TraceIndex = P.TraceScale <= 1 ? 0 : (P.TraceScale >= 4 ? 2 : 1);
    if (ChoiceRow::Construct(Page, "TraceScale", "Trace resolution \xC2\xB7 GI and reflections", TraceOptions, 3, TraceIndex,
                             RenderAvailability::Lighting(P) == nullptr, "rebuilds targets", RenderAvailability::Lighting(P)))
    {
        P.TraceScale = TraceIndex == 0 ? 1 : (TraceIndex == 1 ? 2 : 4);
        Changed      = true;
    }
    Changed |= Count(Page, "InitialCandidates", "Paths per pixel (M)", P.InitialCandidates, 1, 4, "M", RenderAvailability::RayTracedOnly(P),
                     "Initial ReSTIR candidates per trace pixel: more paths, lower variance");
    static const char* const GradeOptions[3] = {"Low", "Medium", "High"};
    Changed |= ChoiceRow::Construct(Page, "QualityTier", "Quality grade", GradeOptions, 3, P.QualityTier, true, "reloads the scene",
                                    "IlluminationConstants grades: cascades, voxel size, march steps, render scale, tiles and pixel ceiling");
    const RenderQualityGrade& Grade = *Reading.Grade;
    char Cascades[24], Voxel[24], Steps[24], Shadow[24], Atlas[24], Ceiling[32];
    std::snprintf(Cascades, sizeof(Cascades), "%u\xC2\xB3", Grade.ClipResolution);
    std::snprintf(Voxel, sizeof(Voxel), "%.3g", Grade.FinestVoxel);
    std::snprintf(Steps, sizeof(Steps), "%u", Grade.Steps);
    std::snprintf(Shadow, sizeof(Shadow), "%u", Grade.ShadowExtent);
    std::snprintf(Atlas, sizeof(Atlas), "%u", Grade.CardExtent);
    std::snprintf(Ceiling, sizeof(Ceiling), "%.1f", Grade.PixelLimit / 1.0e6);
    const MetricTile Upper[3] = {{"Cascades", Cascades, ""}, {"Finest voxel", Voxel, "m"}, {"March steps", Steps, ""}};
    const MetricTile Lower[3] = {{"Shadow map", Shadow, "px"}, {"Card atlas", Atlas, "px"}, {"Pixel ceiling", Ceiling, "Mpx"}};
    MetricTiles::Construct(Page, Upper, 3, MetricTiles::NoLead);
    MetricTiles::Construct(Page, Lower, 3, MetricTiles::NoLead);
    EndSection(Page);
    return Changed;
}

} // namespace Frontier

#endif
