/*====================================================================================================================================
                                                        SOLIDARCMEASUREMENT.H
====================================================================================================================================*/
// 🧩 Inspect: exact measurement of the selection (mass properties, areas, lengths, minimum distance, angles) and the exact
//    plane section of the shown solid bodies, both over the GeometryProcessing Measurement division
#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "UI/SolidArc/Document/SolidArcDocument.h"

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace Frontier::SolidArc
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr double SolidArcUnmeasured = std::numeric_limits<double>::quiet_NaN();      // [-] - A figure the subject lacks

//------------------------------------------------------------------------------------------------------------------------
//                                                    TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 One Inspector row: a caption and its formatted value (Number keeps the raw figure for scripts and gates).
struct SolidArcMeasureRow
{
    std::string Caption = {};                                                                // [-] - "Area"
    std::string Value   = {};                                                                // [-] - "6.0000 m²"
    double      Number  = 0.0;                                                               // [-] - Raw figure (NaN when textual)
};

// 💡 What Measure (I) reports for the current selection (Fusion Inspect → Measure; Plasticity has none):
//    nothing / mode-4 body → mass properties · 1 face → area, perimeter, carrier · 1 edge → length, carrier ·
//    2 entities → minimum distance (+ angle when both carry a direction) · more → total area / length.
//    2 solid bodies (mode 4) → their totals plus interference (Assembly/AssemblyAnalysis.h): clear with the clearance
//    and its closest points, contact, overlap with the shared volume and its edges, or one contained in the other.
//    Figures the subject lacks stay NaN; the overlay reads the point fields.
struct SolidArcMeasurement
{
    uint64_t                        Revision          = 0;                                   // [-]    - Document revision measured
    std::string                     SelectionKey      = {};                                  // [-]    - Selection it describes (cache key)
    std::string                     Subject           = {};                                  // [-]    - "Body Block1", "Face F3", "Distance"
    std::vector<SolidArcMeasureRow> Rows              = {};                                  // [-]    - Inspector rows, in order
    double                          Distance          = SolidArcUnmeasured;                  // [len]  - Minimum distance (2 entities)
    double                          Angle             = SolidArcUnmeasured;                  // [deg]  - Angle between the directions
    double                          Area              = SolidArcUnmeasured;                  // [len²] - Face area / total / body surface
    double                          Length            = SolidArcUnmeasured;                  // [len]  - Edge length / total
    double                          Volume            = SolidArcUnmeasured;                  // [len³] - Body volume
    bool                            DistanceCondition = false;                               // [-]    - First / Second hold closest points
    GeometryProcessing::Vector3               First             = {};                                  // [len]  - Closest point on the first entity
    GeometryProcessing::Vector3               Second            = {};                                  // [len]  - Closest point on the second entity
    bool                            CentroidCondition = false;                               // [-]    - Centroid is meaningful
    GeometryProcessing::Vector3               Centroid          = {};                                  // [len]  - Body / face centroid
    std::string                     Interference      = {};                                  // [-]    - 2 bodies: Clear / Contact / Overlap / Contained
    double                          OverlapVolume     = SolidArcUnmeasured;                  // [len³] - 2 bodies: shared volume
    std::vector<std::vector<GeometryProcessing::Vector3>> Overlap = {};                                // [len]  - 2 bodies: edges of the shared volume (overlay)
    double                          Milliseconds      = 0.0;                                 // [ms]   - Measurement time
    std::string                     Refusal           = {};                                  // [-]    - Empty on success
};

// 💡 Exact section of every shown solid body (sheets skipped): the kernel SectionBody per body, figures combined —
//    areas, perimeters, regions and loops add; the centroid is area-weighted; Iuu Ivv Iuv are moved to the common
//    centroid by the parallel-axis theorem (u = Plane.XAxis, v = Plane.YAxis).
struct SolidArcSection
{
    uint64_t                                    Revision     = 0;                            // [-]    - Document revision cut
    std::string                                 RequestKey   = {};                           // [-]    - Request it describes (cache key)
    GeometryProcessing::OrthonormalFrame                  Plane        = {};                           // [-]    - Cut plane (ZAxis = kept side)
    bool                                        PlaneCondition = false;                      // [-]    - Plane resolved (face found, planar)
    double                                      Area         = 0.0;                          // [len²] - Σ region areas
    double                                      Perimeter    = 0.0;                          // [len]  - Σ boundary lengths
    GeometryProcessing::Vector3                           Centroid     = {};                           // [len]  - Area centroid (world)
    std::array<double, 3>                       SecondMoments = {};                          // [len⁴] - Iuu Ivv Iuv about Centroid
    int                                         Regions      = 0;                            // [-]    - Disjoint pieces
    int                                         Loops        = 0;                            // [-]    - Outer loops + holes
    int                                         Bodies       = 0;                            // [-]    - Bodies the plane crosses
    int                                         Sheets       = 0;                            // [-]    - Sheet bodies skipped
    std::vector<std::vector<GeometryProcessing::Vector3>> Outline      = {};                           // [len]  - Boundary polylines (all bodies)
    double                                      Milliseconds = 0.0;                          // [ms]   - Cuts + integrals
    std::string                                 Refusal      = {};                           // [-]    - Empty on success
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Exact measurement of the current selection; with nothing selected, the active body (the only body when there is one).
[[nodiscard]] SolidArcMeasurement MeasureSolidArcSelection(const SolidArcDocument& Document);

// Selection fingerprint (faces, edges, bodies, in order) — Measure recomputes only when it or the revision changes.
[[nodiscard]] std::string SolidArcSelectionKey(const SolidArcSelection& Selection);

// Plane of a request (cheap: no cut). PlaneCondition is false when a Face request names no planar face.
[[nodiscard]] SolidArcSection ResolveSolidArcSectionPlane(const SolidArcDocument& Document, const SolidArcSectionRequest& Request);

// Exact section of the shown solid bodies by the request plane (OutlineStep = polyline spacing, 0 = automatic).
[[nodiscard]] SolidArcSection SectionSolidArcDocument(const SolidArcDocument& Document, const SolidArcSectionRequest& Request, double OutlineStep = 0.0);

// Request fingerprint (cache key and script readout).
[[nodiscard]] std::string SolidArcSectionKey(const SolidArcSectionRequest& Request);

} // namespace Frontier::SolidArc
