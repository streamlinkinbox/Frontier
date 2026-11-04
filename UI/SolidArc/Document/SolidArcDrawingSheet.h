/*====================================================================================================================================
                                                        SOLIDARCDRAWINGSHEET.H
====================================================================================================================================*/
// 🧩 Drawing sheet (Alt+D): the shown solid bodies of the document as an ISO drawing — the kernel's Drawing/DrawingSheet
//    (views, hidden lines, automatic dimensions, title block) and its SVG / DXF R12 export; the settings are saved with
//    the document

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "UI/SolidArc/Document/SolidArcMeasurement.h"

#include "Engine/GeometryProcessing/Drawing/DrawingSheet.h"

#include <cstdint>
#include <string>

namespace Frontier::SolidArc
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr double SolidArcModelUnitMillimetres = 1000.0;                               // [mm] - One model unit (metre) on paper

//------------------------------------------------------------------------------------------------------------------------
//                                                    TYPES
//------------------------------------------------------------------------------------------------------------------------

// 💡 One composition: the kernel sheet of the shown solid bodies (hidden bodies and sheet bodies left out) in
//    millimetres, cached by the document revision and the settings key.
struct SolidArcDrawingSheet
{
    uint64_t                Revision     = 0;                                                // [-]  - Document revision drawn
    std::string             SettingsKey  = {};                                               // [-]  - SolidArcDrawingSheetCacheKey drawn
    GeometryProcessing::DrawingSheet  Sheet        = {};                                               // [-]  - Kernel sheet (millimetres)
    int                     Bodies       = 0;                                                // [-]  - Solid bodies drawn
    int                     Sheets       = 0;                                                // [-]  - Sheet bodies left out
    double                  Milliseconds = 0.0;                                              // [ms] - Composition time
    std::string             Refusal      = {};                                               // [-]  - Empty on success
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// The drawing sheet of the document's shown solid bodies.
[[nodiscard]] SolidArcDrawingSheet ComposeSolidArcDrawingSheet(const SolidArcDocument& Document, const SolidArcDrawingSheetSettings& Settings);

// Settings fingerprint (cache key and script readout).
[[nodiscard]] std::string SolidArcDrawingSheetKey(const SolidArcDrawingSheetSettings& Settings);

// Cache key of a composition: the settings key plus the hidden bodies (hiding is an appearance edit, not a revision).
[[nodiscard]] std::string SolidArcDrawingSheetCacheKey(const SolidArcDocument& Document, const SolidArcDrawingSheetSettings& Settings);

// True when the settings equal the defaults (the codec then writes no [DrawingSheet] table).
[[nodiscard]] bool DefaultSolidArcDrawingSheetCondition(const SolidArcDrawingSheetSettings& Settings);

// Kernel section frame of a Section-card plane: the card keeps its +normal side, the sheet keeps the −ZAxis side.
[[nodiscard]] GeometryProcessing::OrthonormalFrame SolidArcSheetSectionFrame(const GeometryProcessing::OrthonormalFrame& Plane);

// Where the sheet is exported: "Bracket.solidarc" → "Bracket.svg" / "Bracket.dxf" beside it; no document path →
// "Untitled.svg" in the working folder. Extension includes the dot.
[[nodiscard]] std::string SolidArcDrawingSheetExportPath(const std::string& DocumentPath, const std::string& Extension);

// Writes the sheet as SVG or DXF R12 (by the path's extension); returns the refusal (empty on success).
[[nodiscard]] std::string ExportSolidArcDrawingSheet(const SolidArcDrawingSheet& Sheet, const std::string& Path);

// "A3", "First", "Y", … ↔ enums for the codec and scripts (false / empty for an unknown label).
[[nodiscard]] const char* SolidArcProjectionAngleLabel(GeometryProcessing::DrawingProjectionAngle Angle) noexcept;
[[nodiscard]] bool        ParseSolidArcPaperSize(const std::string& Label, GeometryProcessing::DrawingPaperSize& Paper) noexcept;
[[nodiscard]] bool        ParseSolidArcProjectionAngle(const std::string& Label, GeometryProcessing::DrawingProjectionAngle& Angle) noexcept;

} // namespace Frontier::SolidArc
