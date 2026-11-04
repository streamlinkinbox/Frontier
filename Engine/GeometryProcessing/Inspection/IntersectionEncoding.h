/*====================================================================================================================================
                                                    INTERSECTIONENCODING.H
====================================================================================================================================*/
// 🧩 JSON encoding of an intersection scene for the HTML viewer (same schema family as model specimens, Kind = Intersection)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometryProcessing/Audit/IntersectionAudit.h"
#include "Engine/GeometryProcessing/Inspection/IntersectionCatalogue.h"

#include <string>

namespace Frontier::GeometryProcessing
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 Faces = the two surface patches as iso-hatching (display-clipped around the curve), Edges = branch polylines,
//    Vertices = special points, Summary.Metrics = measured vs expected with a pass flag per row.
[[nodiscard]] std::string EncodeIntersection(const std::string&                SceneName,
                                             const IntersectionScene&          Scene,
                                             const SurfaceIntersectionOutcome& Outcome,
                                             const IntersectionAuditReport&    Audit,
                                             double                            SolveMilliseconds,
                                             double                            AuditMilliseconds);

} // namespace Frontier::GeometryProcessing
