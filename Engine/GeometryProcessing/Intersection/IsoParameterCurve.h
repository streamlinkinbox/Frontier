/*====================================================================================================================================
                                                      ISOPARAMETERCURVE.H
====================================================================================================================================*/
// 🧩 Exact iso-parameter curves of the analytic surfaces (every one is a line or a circle)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometryProcessing/Geometry/CurveGeometry.h"
#include "Engine/GeometryProcessing/Geometry/SurfaceGeometry.h"

namespace Frontier::GeometryProcessing
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

enum class IsoDirection
{
    FixedU = 0,                                                                              // 𝑢 = constant, 𝑣 runs
    FixedV = 1,                                                                              // 𝑣 = constant, 𝑢 runs
};

// 📝 Curve parameter τ = Offset + Scale·s, s the running surface parameter — so curve hits map straight back to (𝑢, 𝑣).
struct IsoParameterCurve
{
    CurveGeometry Curve                = StraightLine{};                                     // [-] - Exact carrier
    double        Offset               = 0.0;                                                // [-] - τ at s = 0
    double        Scale                = 1.0;                                                // [-] - dτ/ds
    bool          DegenerateCondition  = false;                                              // [-] - Collapses to a point (pole, apex)
    bool          ApproximateCondition = false;                                              // [-] - Polyline stand-in (parallel surface): seeds only
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 Plane: lines. Cylinder: generator lines / circles. Cone: generator lines / circles. Sphere: meridian / latitude
//    circles. Torus: tube / latitude circles. Lines and circles are exactly what curve–surface intersection solves, so
//    iso-lattice start points for surface–surface intersection are exact.
[[nodiscard]] IsoParameterCurve ExtractIsoParameterCurve(const SurfaceGeometry& Surface, IsoDirection Direction, double Value) noexcept;

} // namespace Frontier::GeometryProcessing
