#pragma once
#include "UI/SolidArc/Viewport/SolidArcSnapping.h"
#include "Engine/GeometryProcessing/Surfacing/CurveFitting.h"

namespace Frontier::SolidArc
{
// Surface hits are exact carrier evaluations, seeded by the visible tessellation.
[[nodiscard]] SolidArcSnap SnapSolidArcTrace(SolidArcSnapCatalogue& Catalogue, const SolidArcDocument& Document,
    const SolidArcScene& Scene, const OrbitCameraFrame& Frame, double X, double Y, bool CurveSnap, bool AutoCurves = false);

// Point-only authoring: exactly the supplied controls, never a surface fit.
struct SolidArcControlTrace
{
    GeometryProcessing::NurbsCurve Curve;
    std::optional<SolidArcDrawingRequest> Arc;
    std::vector<GeometryProcessing::Vector3> Samples; // Display tessellation only, never saved as controls
    std::string Refusal;
};
[[nodiscard]] SolidArcControlTrace MakeSolidArcControlTrace(const std::vector<GeometryProcessing::Vector3>& Controls,
    SolidArcDrawingKind Kind, GeometryProcessing::SketchSplineForm Form, bool Closed);

struct SolidArcSurfaceTrace
{
    std::vector<GeometryProcessing::Vector3> Samples;
    GeometryProcessing::CurveFitOutcome Fit;
    std::string Refusal;
    double WorstSurfaceDeviation = 0.0;
};
// Project the ENTIRE seed curve, not just its control points. Misses / gaps refuse.
// Arc is a three-point circular seed; a curved-surface projection becomes a NURBS.
// The fit is checked at 257 samples against the visible source, to Tolerance metres.
[[nodiscard]] SolidArcSurfaceTrace ProjectSolidArcTrace(SolidArcSnapCatalogue& Catalogue, const SolidArcDocument& Document,
    const SolidArcScene& Scene, const OrbitCameraFrame& Frame, const std::vector<GeometryProcessing::Vector3>& Controls,
    SolidArcDrawingKind Kind, GeometryProcessing::SketchSplineForm Form, bool Closed, bool Fit,
    double Tolerance = 0.0002);
}
