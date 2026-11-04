/*==========================================================================================================================================
                                                         DIAGRAMCONTROLS.H
==========================================================================================================================================*/
// 🧩 Generic, reusable analytical plotting, polar dial, spectrum ramp, disc row, and modal editor frame controls.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Interface/InspectorControls.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include <cstddef>
#include <cstdint>
#include <functional>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    DIAGRAM CONTROLS
//------------------------------------------------------------------------------------------------------------------------

struct DiagramControls
{
    // Generic 180° trajectory/elevation arc plot with baseline and normalized marker [0..1].
    static void ConstructArcPlot(const InspectorPalette& Palette,
                                 const EditorTypefaces&  Faces,
                                 const char*             HeaderLabel,
                                 const char*             BaselineLabel,
                                 float                   HorizontalRatio,
                                 float                   ElevationRatio,
                                 ImU32                   MarkerTint);

    // Generic vertical multi-band strata diagram with custom top/bottom gradient and tier labels.
    static void ConstructStrataPlot(const InspectorPalette& Palette,
                                    const EditorTypefaces&  Faces,
                                    ImU32                   UpperTint,
                                    ImU32                   LowerTint,
                                    const char* const*      TierLabels,
                                    size_t                  TierCount);

    // Generic interactive 360° polar vector/angle dial. Returns true when angle is dragged.
    static bool ConstructPolarDial(const InspectorPalette& Palette,
                                   const EditorTypefaces&  Faces,
                                   const char*             Id,
                                   const char*             ReadoutLabel,
                                   float&                  InOutAngleDegrees,
                                   float                   MagnitudeRatio,
                                   ImU32                   VectorTint);

    // Generic 1D analytical function plot (1 or 2 normalized [0..1] sample series + optional X threshold < 0 to hide).
    static void ConstructCurvePlot(const InspectorPalette& Palette,
                                   const EditorTypefaces&  Faces,
                                   const char*             HeaderLabel,
                                   const float*            PrimarySamples,
                                   size_t                  PrimaryCount,
                                   ImU32                   PrimaryTint,
                                   const float*            SecondarySamples        = nullptr,
                                   size_t                  SecondaryCount          = 0,
                                   ImU32                   SecondaryTint           = 0,
                                   float                   ThresholdRatio          = -1.0f);

    // Generic multi-stop colour ramp with normalized [0..1] cursor indicator.
    static void ConstructSpectrumBar(const InspectorPalette& Palette,
                                     const EditorTypefaces&  Faces,
                                     const char*             HeaderLabel,
                                     const ImU32*            ColourStops,
                                     size_t                  StopCount,
                                     float                   CursorRatio);

    // Generic regular-polygon / iris geometry plot with optional concentric dispersion ring.
    static void ConstructPolygonDiagram(const InspectorPalette& Palette,
                                        const EditorTypefaces&  Faces,
                                        const char*             HeaderLabel,
                                        uint32_t                SideCount,
                                        float                   RadiusRatio,
                                        float                   DispersionRatio,
                                        ImU32                   StrokeTint);

    // Generic horizontal row of shaded/illuminated phase discs (each phase in [0..1]).
    static void ConstructDiscSequence(const InspectorPalette& Palette,
                                      const EditorTypefaces&  Faces,
                                      const char*             HeaderLabel,
                                      const float*            IlluminationRatios,
                                      size_t                  DiscCount,
                                      int                     HighlightedIndex,
                                      ImU32                   LitTint);

    // Generic time-series waveform / sparkline plot with optional target reference line.
    static void ConstructTimeSeriesPlot(const InspectorPalette& Palette,
                                        const EditorTypefaces&  Faces,
                                        const char*             HeaderLabel,
                                        const float*            Samples,
                                        size_t                  SampleCount,
                                        float                   MinimumDomain,
                                        float                   MaximumDomain,
                                        float                   ReferenceValue,
                                        ImU32                   SeriesTint);

    // Generic split-pane modal window frame used by standalone <Domain>Editor classes.
    static void ConstructEditorModalFrame(const char*                                             WindowTitle,
                                          const char*                                             GlyphName,
                                          RecordToken                                             Token,
                                          RecordInspector&                                        Inspector,
                                          const std::function<void(ImDrawList*, ImVec2, ImVec2)>& CanvasCallback);
};

} // namespace Frontier

#endif // FRONTIER_EDITOR && FRONTIER_WITH_INTERFACE_SDK
