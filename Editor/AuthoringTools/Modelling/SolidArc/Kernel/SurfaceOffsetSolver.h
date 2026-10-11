//============================================================================================================================================
//                                                         SURFACEOFFSETSOLVER.H
//============================================================================================================================================
// 📦 Normal-geodesic curve offsets on untrimmed sewn NURBS skins, with bounded numerical refinement.

#pragma once
#include "TopologySpecification.h"

namespace Frontier
{
struct SurfaceOffsetOptions
{
    double Distance  = 0.02;          // [m] - signed left-of-tangent surface distance
    double Tolerance = 0.00005;       // [m] - numerical march and interpolation tolerance
    int    Samples   = 256;           // [-] - initial longitudinal intervals
};

struct SurfaceOffsetReport
{
    double              MarchRefinement    = 0;  // [m] - maximum coarse/fine endpoint difference
    double              CornerRefinement   = 0;  // [m] - coarse/fine local corner path discrepancy
    double              CornerResidual     = 0;  // [m] - final corner distance-field residual
    double              PathLengthError    = 0;  // [m] - refined midpoint-chord path-length discrepancy
    double              InterpolationError = 0;  // [m] - held-out sample discrepancy
    double              SourceDistance     = 0;  // [m] - maximum source-to-support discrepancy
    int                 CornerIntervals    = 0;  // [-] - local distance-field corner intervals
    int                 Intervals          = 0;  // [-] - accepted longitudinal intervals
    std::vector<double> Parameters;             // [-] - output parameters of adaptive stations
    std::vector<Vec3>   Sources;                 // [m] - source positions corresponding to offset stations
    std::vector<Vec3>   Stations;                // [m] - surface offset stations
};

class SurfaceOffsetSolver
{
public:
    [[nodiscard]] static Deliver<NurbsCurve> Construct(
        const NurbsCurve&           Source,
        const BrepBody&             Support,
        const SurfaceOffsetOptions& Options,
        SurfaceOffsetReport*        Report = nullptr) noexcept;
};
}
