//=============================================================================================================================================
// SolidArc · bounded general face editing
//
// The public face-edit surface is deliberately conservative. It accepts canonical axis-aligned
// rectangular prisms, bounded pentagonal-prism, concave-prism, genus-one holed-prism, elliptical-prism,
// and oblique triangular-prism routes, bounded concave-prism shell and triangular-prism draft routes,
// a bounded twin-holed-prism route,
// and natural NURBS replacement faces whose rim is identical to the selected rim. Operations are copy-in/copy-out: a refusal never mutates the
// source. This is the safe foundation for extending the routes to arbitrary trimmed and curved B-reps later.
#pragma once

#include "TopologySpecification.h"

namespace Frontier
{

class FaceEditSolver
{
public:
    // Exact normal offset of one supported planar face. Canonical boxes use the legacy route;
    // the bounded pentagonal-prism upper-cap route is selected when the box route declines.
    [[nodiscard]] static Deliver<BrepBody> OffsetFace(const BrepBody& Source, int Face, double Distance) noexcept;

    // Hollow an axis-aligned rectangular prism through one selected face. The result is a closed solid
    // with an open cavity (a real shell, not a visual offset); the selected face is the opening.
    [[nodiscard]] static Deliver<BrepBody> Shell(const BrepBody& Source, int Face, double Thickness) noexcept;

    // Bounded non-box shell route for a six-sided straight convex prism selected through its upper cap.
    // The existing rectangular-box route remains separate and is not widened by this API.
    [[nodiscard]] static Deliver<BrepBody> ShellExtrudedConvexPrism(const BrepBody& Source, int Face, double Thickness) noexcept;

    // Bounded non-box shell route for an axis-aligned orthogonal L-shaped prism.
    // Only its upper planar cap is supported; arbitrary concave shell sources remain refused.
    [[nodiscard]] static Deliver<BrepBody> ShellExtrudedConcavePrism(const BrepBody& Source, int Face, double Thickness) noexcept;

    // Bounded non-box offset route for a five-sided straight convex prism selected through its upper cap.
    // Positive distance extends the upper cap along +Z; arbitrary polygonal/freeform offsets remain refused.
    [[nodiscard]] static Deliver<BrepBody> OffsetExtrudedConvexPrism(const BrepBody& Source, int Face, double Distance) noexcept;

    // Bounded non-convex offset route for an axis-aligned orthogonal L-shaped prism.
    // Only its upper planar cap is supported; arbitrary concave profiles remain refused.
    [[nodiscard]] static Deliver<BrepBody> OffsetExtrudedConcavePrism(const BrepBody& Source, int Face, double Distance) noexcept;

    // Bounded genus-one offset route for a straight rectangular prism with one exact circular through-hole.
    // Only its upper annular cap is supported; arbitrary multi-loop/freeform offsets remain refused.
    [[nodiscard]] static Deliver<BrepBody> OffsetExtrudedHoledPrism(const BrepBody& Source, int Face, double Distance) noexcept;

    // Bounded curved-profile offset route for a straight prism with one axis-aligned exact ellipse.
    // Only its upper planar cap is supported; circles, tilted profiles, and freeform curves remain refused.
    [[nodiscard]] static Deliver<BrepBody> OffsetExtrudedEllipticalPrism(const BrepBody& Source, int Face, double Distance) noexcept;

    // Bounded non-axis-aligned offset route for an oblique triangular straight prism.
    // Only the planar cap normal to its common non-vertical generator axis is supported.
    [[nodiscard]] static Deliver<BrepBody> OffsetObliqueTriangularPrism(const BrepBody& Source, int Face, double Distance) noexcept;

    // Bounded draft route for an axis-aligned triangular prism side face. Angle is in radians.
    // The existing canonical-box draft route remains separate.
    [[nodiscard]] static Deliver<BrepBody> DraftExtrudedTriangularPrism(const BrepBody& Source, int Face, double AngleRadians) noexcept;

    // Bounded genus-two offset route for a rectangular prism with exactly two circular through-holes.
    // Only its upper three-loop planar cap is supported; arbitrary multi-loop profiles remain refused.
    [[nodiscard]] static Deliver<BrepBody> OffsetExtrudedTwinHoledPrism(const BrepBody& Source, int Face, double Distance) noexcept;

    // Draft one vertical (+/-X or +/-Y) face about the source Z direction. Angle is in radians and
    // positive moves the selected wall outward at the high-Z end.
    [[nodiscard]] static Deliver<BrepBody> Draft(const BrepBody& Source, int Face, double AngleRadians) noexcept;

    // Remove one face and retain the five exact source faces as an open, healed sheet.
    [[nodiscard]] static Deliver<BrepBody> DeleteFace(const BrepBody& Source, int Face) noexcept;

    // Replace one box face with a natural replacement surface with the exact same four-edge rim.
    // Freeform surfaces, mismatched rims, and non-box sources refuse instead of being approximated.
    [[nodiscard]] static Deliver<BrepBody> ReplaceFace(const BrepBody& Source, int Face, const NurbsSurface& Replacement) noexcept;

    // Exact bounded box-face extension: grow the selected face in both in-plane directions and
    // rebuild its neighbouring planar faces. Distance is measured on each side of the face.
    [[nodiscard]] static Deliver<BrepBody> ExtendFace(const BrepBody& Source, int Face, double Distance) noexcept;

    // Exact bounded box-face trim: shrink the selected face in both in-plane directions and rebuild
    // its neighbouring planar faces. Distance is measured on each side of the face.
    [[nodiscard]] static Deliver<BrepBody> TrimFace(const BrepBody& Source, int Face, double Distance) noexcept;

    // Re-sew natural faces, validate existing trimmed faces, orient them, and reject degenerate/sliver
    // topology. This is intentionally a conservative healer: it never deletes geometry that cannot be proven redundant.
    [[nodiscard]] static Deliver<BrepBody> Heal(const BrepBody& Source, double Tolerance = ScalarCriteria::MergeTolerance) noexcept;
    [[nodiscard]] static Deliver<BrepBody> RemoveSlivers(const BrepBody& Source, double Tolerance = ScalarCriteria::MergeTolerance) noexcept;

    // Public diagnostic used by focused verification and callers that want to preflight a route.
    [[nodiscard]] static bool IsCanonicalBox(const BrepBody& Source) noexcept;
};

} // namespace Frontier
