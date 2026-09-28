# Batch 61 plan — exact hexagonal elliptical-holed and concave twin-circular-holed prism face offsets

## Scope

Promote one consolidated batch that moves the holed-prism family off rectangular outer profiles
onto two new non-rectangular modelling domains:

1. An origin-centred, axis-aligned regular hexagonal prism with exactly one canonical elliptical
   through-hole (genus 1).
2. An origin-centred, axis-aligned orthogonal concave L-profile prism with exactly two canonical
   circular through-holes (genus 2).

Both sources support only a finite positive +Z extension of their upper planar cap.

## Canonical geometry

- Hexagonal route: circumradius `4`, height `6`, first vertex at `(+4,0)`, one axis-aligned
  elliptical hole centred at the origin with major radius `1.8` and minor radius `1.1`.
  Closed manifold topology `V14/E21/C42/L11/F9`, genus 1, two-loop upper cap.
- Concave route: the canonical six-edge orthogonal L profile
  `(-6,-4) (6,-4) (6,-1) (-1,-1) (-1,4) (-6,4)`, height `6`, profile area `61`, with circular
  holes of radius `1.0` centred at `(-3,-2.5)` and `(3,-2.5)`. Closed manifold topology
  `V16/E24/C48/L14/F10`, genus 2, three-loop upper cap with exactly one reflex turn.

## Recognition contract

The strict readers require the exact canonical bounds, vertex level counts, cap loop structure,
polygon vertex sets, turn signatures, hole supports, hole dimensions and placements, full edge
and face censuses, and reciprocal analytic edge incidence. Non-canonical polygon radii, ellipse
or circle dimensions, hole counts, hole supports, hole placements, translated profiles, hole-free
sources, malformed topology, lower/side selections, and invalid distances refuse transactionally
without healing and without mutating the source.

## Verification and proof

Use one verifier,
`HexagonalEllipticalHoledAndConcaveTwinCircularHoledFaceOffsetVerification.cpp`, with distances
`0.5`, `1.5`, and `3.0`; exact topology, cap, and support checks; exact volume identities
(`(3√3/2)R² − πab` and `61 − 2πr²` times the extended height); source immutability; dispatcher
coverage; cross-route, cross-support, wrong-hole-count, hole-free, translated, non-canonical,
malformed, lower/side, and invalid-distance refusals; and one durable four-body proof:

`Proofs/Batch61_HexagonalEllipticalHoledAndConcaveTwinCircularHoledFaceOffset.png`

Register the verifier and proof in the standalone CMake target, dependency-free full gate,
capability roadmap, and proof audit.

## Explicit non-goals

No arbitrary polygon side counts, non-regular or rotated polygons, arbitrary concave profiles,
arbitrary hole counts, dimensions, placements or supports, tilted/oblique/freeform profiles,
inward or lower/side offsets, healing, or general multi-loop face editing is promoted by this
batch.
