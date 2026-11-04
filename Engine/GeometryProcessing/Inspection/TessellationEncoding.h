/*====================================================================================================================================
                                                    TESSELLATIONENCODING.H
====================================================================================================================================*/
// 🧩 One-line JSON description of a PolygonComplex for the viewer's shaded mode (triangles, per-face records, N-gates)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometryProcessing/Audit/TessellationAudit.h"
#include "Engine/GeometryProcessing/Tessellation/BodyTessellation.h"

#include <string>

namespace Frontier::GeometryProcessing
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Fields: Kind "Tessellation", Specimen, SagRatio, Sag, EdgeLimit, Diagonal, Positions [x, y, z …], Triangles [a, b, c …]
//    (outward CCW), TriangleFaces, Edges [[vertex …] …], Faces [{Token, Surface, Triangles, …}], Gates [{Name, Measured,
//    Allowance, Passed}], Milliseconds, AuditMilliseconds.
[[nodiscard]] std::string EncodeTessellation(const ModelPartition& Partition, const std::string& SpecimenName, double SagRatio,
                                             const TessellationOutcome& Outcome, const TessellationAuditReport& Report, double AuditMilliseconds);

} // namespace Frontier::GeometryProcessing
