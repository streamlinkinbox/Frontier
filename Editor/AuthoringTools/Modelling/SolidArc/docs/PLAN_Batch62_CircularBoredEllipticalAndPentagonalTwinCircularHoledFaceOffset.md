# Batch 62 plan — exact circular-bored elliptical and pentagonal twin-circular-holed prism face offsets

## Scope

Promote one consolidated batch covering two further non-rectangular holed domains:

1. An origin-centred, axis-aligned elliptical prism with exactly one canonical **eccentric
   circular bore** (genus 1). This is the first holed route whose outer wall is a single closed
   curved analytic profile rather than a polygonal chain.
2. An origin-centred, axis-aligned regular pentagonal prism with exactly two canonical circular
   through-holes (genus 2).

Both support only a finite positive +Z extension of their upper planar cap.

## Canonical geometry

- Bored elliptical route: outer ellipse major radius `6` (X) and minor radius `4` (Y) centred at
  the origin, height `6`, one circular bore of radius `1.2` centred at `(2, 0)` — deliberately
  eccentric so the route cannot be satisfied by the concentric annular reader. Closed manifold
  topology `V4/E6/C12/L6/F4`, genus 1, two-loop caps, two extrusion walls, two seam lines.
- Pentagonal route: regular pentagon of circumradius `4` with a vertex at `(+4,0)`, height `6`,
  circular holes of radius `0.9` centred at `(-1.5, 0)` and `(1.5, 0)`. Closed manifold topology
  `V14/E21/C42/L13/F9`, genus 2, three-loop caps, seven extrusion walls.

## Recognition contract

The strict readers require exact canonical bounds, vertex level counts, cap loop structure, exact
axis-aligned outer ellipse or canonical regular pentagon vertex set, convex turn signature, exact
bore/hole support type, radius, centre and plane, bore containment inside the outer profile, full
edge censuses (2 seam lines + 2 circles + 2 outer ellipse edges; 17 lines + 4 circles) and face
censuses, plus reciprocal analytic edge incidence.

Non-canonical ellipse or polygon dimensions, bore/hole radii or centres, hole counts, mismatched
hole supports (concentric elliptical annulus, elliptical pentagon holes), hole-free sources,
translated profiles, malformed incidence, lower/side selections, and invalid distances refuse
transactionally without healing and without mutating the source. The pre-existing concentric
elliptical-annulus and hole-free pentagonal routes remain separate and must still dispatch exactly.

## Verification and proof

Use one verifier,
`CircularBoredEllipticalAndPentagonalTwinCircularHoledFaceOffsetVerification.cpp`, with distances
`0.5`, `1.5`, and `3.0`; exact topology, cap, face-census, and support checks; exact volume
identities (`πab − πr²` and `(5/2)R²·sin(72°) − 2πr²`, times the extended height); source
immutability; dispatcher coverage; regression checks that the earlier concentric-annulus and
hole-free pentagon domains still route; explicit refusal boundaries; and one durable four-body
proof:

`Proofs/Batch62_CircularBoredEllipticalAndPentagonalTwinCircularHoledFaceOffset.png`

Register the verifier and proof in the standalone CMake target, dependency-free full gate,
capability roadmap, and proof audit.

## Explicit non-goals

No arbitrary ellipse axes or centres, arbitrary polygon side counts or radii, arbitrary bore/hole
counts, radii, placements or supports, tilted/oblique/freeform profiles, inward or lower/side
offsets, healing, or general multi-loop face editing is promoted by this batch.
