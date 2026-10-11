# Plan — Phase 40 bounded extruded convex-prism shell

## Capability slice

Move the next SolidArc increment outside the cone/bicone apex family: extend shell/thicken to one
non-box, straight-sided prism topology. The source is a capped regular hexagonal prism with one
selected planar end cap. A positive wall thickness produces an open-top shell with exact planar
inner walls, a recessed inner floor, and a six-segment top rim.

The distinct fixture is a regular hexagonal prism with circumradius 5, height 8, and wall thickness
0.65. It intentionally exercises `V12/E18/C36/L8/F8` source topology rather than reusing an
axis-aligned box.

## Source and reconstruction contract

Add a separate `FaceEditSolver::ShellExtrudedConvexPrism` route. It accepts only a closed solid with
exactly two parallel planar cap faces, six straight planar side faces, vertical axis-aligned prism
edges, one selected cap, and a convex six-edge cap loop. It derives the ordered cap polygon, checks
positive convexity and a non-consuming finite thickness, offsets each cap edge inward in the cap
plane, and leaves the source unchanged.

The reconstruction creates six retained outer walls, six exact inner walls, six top rim planes, and
lets the bounded topology builder cap the outer bottom and inner floor loops. It must return one
closed genus-zero shell solid with expected `V24/E42/C84/L20/F20` topology and positive volume:
outer prism volume minus the inner prism volume.

## Refusal boundary

The route refuses boxes through this distinct API, non-six-sided or concave profiles, non-planar or
curved supports, tilted/non-prismatic sources, non-cap selections, zero/negative/non-finite or
consuming thicknesses, malformed topology, and any healing-dependent fallback. The existing
axis-aligned box shell route remains unchanged.

## Verification and proof

`ExtrudedConvexPrismShellVerification` constructs the regular hexagonal prism independently and
checks source topology, exact shell topology, analytic volume, planar inner floor/walls and rim,
source immutability, and failure-oriented refusals for box/triangle/concave/curved/invalid cases.
The durable proof will be `Proofs/Phase40_ExtrudedConvexPrismShell.png`.
