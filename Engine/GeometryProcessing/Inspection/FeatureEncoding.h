/*====================================================================================================================================
                                                       FEATUREENCODING.H
====================================================================================================================================*/
// 🧩 Sketch and feature-tree results → JSON: solved sketches, diagnosis, step reports, persistent names, body geometry

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometryProcessing/Audit/ModelAudit.h"
#include "Engine/GeometryProcessing/Feature/FeatureCatalogue.h"
#include "Engine/GeometryProcessing/Inspection/JsonWriter.h"
#include "Engine/GeometryProcessing/Sketch/SketchCatalogue.h"
#include "Engine/GeometryProcessing/Sketch/SketchProfile.h"

#include <string>

namespace Frontier::GeometryProcessing
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct FeatureInspection
{
    int    ShownSteps        = 0;                                                            // [-]    - Steps replayed (Upto)
    int    TotalSteps        = 0;                                                            // [-]    - Steps in the tree
    double Volume            = 0.0;                                                          // [len³] - V of the shown body
    double AuditMilliseconds = 0.0;                                                          // [ms]   - Audit of the shown body
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 The regenerated body with every face / edge / vertex token mapped to its persistent name, the step reports
//    (status, refusal, how each reference resolved), and every solved sketch drawn on its plane.
[[nodiscard]] std::string EncodeFeature(const ModelPartition& Partition, const FeatureSpecimen& Specimen, const FeatureTree& Tree,
                                        const RegenerationOutcome& Outcome, const AuditReport& Audit, const FeatureInspection& Inspection);

// 💡 A solved catalogue sketch: points, curves (plane z = 0), diagnosis, per-constraint status, profile.
//    EditedCondition: the viewer changed a dimension, so the catalogue's closed-form area no longer applies.
[[nodiscard]] std::string EncodeSketch(const SketchSpecimen& Specimen, const SketchModel& Solved, const SketchSolveOutcome& Solve, const SketchProfile& Profile,
                                       bool EditedCondition);

// Catalogue rows for the viewer (kinds "Sketch" and "Feature").
void EncodeSolidArcCatalogue(JsonWriter& Writer);

// Dimension constraints a viewer may edit: label → value (Distance, Horizontal/VerticalDistance, Length, Radius, Angle).
[[nodiscard]] bool EditableDimensionCondition(const SketchConstraint& Constraint);

} // namespace Frontier::GeometryProcessing
