# Variable-radius fillet roadmap — bounded implementation plan

The requested follow-on work is broad. It will be delivered as individually verified slices;
no slice will be described as completing the whole variable-radius or healing roadmap.

## Stage 1 — rolling-ball variable-radius application on one straight planar corner

Accept one finite, straight, two-plane corner with a positive **linear** radius law along the
edge. At every edge station, the meridian is an exact quarter-circle rolling-ball section. The
result is reconstructed as five ruled/lofted side surfaces plus two planar end caps, with the
analytic volume checked against the integrated rounded-corner area.

Boundaries for this stage:

- complete finite straight edge only;
- two perpendicular planar supports;
- positive linear law and positive remaining corner width;
- no partial-edge selection, nonlinear law, G2 requirement, freeform support, or healing of an
  arbitrary input B-rep;
- input selection/console dispatch is a later slice if the explicit reconstruction is sound.

## Stage 2 — variable setback laws ✅

`VariableSetbackCornerSpecification` adds a separate positive linear law for support
setback/clearance on the same bounded straight planar corner. At each station the measured
support extent is `d(t) = r(t) + s(t)`, and the verifier checks that identity at multiple
stations, the analytic volume, topology, normals, refusals, and a distinct comparison render.
The route is intentionally one common setback law for the two perpendicular supports; unequal
support setbacks, nonlinear laws, and general input-edge selection remain outside the stage.
See `docs/PLAN_VariableSetbackLaw.md`.

## Stage 3a — quadratic nonlinear radius law ✅

`QuadraticRadiusLaw` and `QuadraticVariableRadiusSurface` add an explicit nonlinear radius
interpolant through endpoint and middle-station values. A quadratic loft through three exact
quarter-circle sections reconstructs one bounded straight planar corner, with analytic volume
and sampled circumferential/meridional curvature acceptance. See
`docs/PLAN_NonlinearRadiusLaw.md`.

## Stage 3b — bounded G2 continuity profile ✅

`G2PlanarCornerSpecification` adds a separate quintic non-rolling corner profile with
support-aligned tangents and zero endpoint curvature. The profile is extruded and its endpoint
curvature is measured against both planar supports. This proves the G2 construction boundary,
not G2 for the circular rolling-ball or nonlinear-radius routes. See
`docs/PLAN_G2PlanarCorner.md`.

## Stage 3c — nonlinear setback laws ✅

`NonlinearVariableSetbackCornerSpecification` adds a genuinely nonlinear quadratic support-setback
law while the radius may remain constant or linear. Three exact station sections are quadratic-
lofted, with analytic extent/removal volume and positivity/refusal checks. See
`docs/PLAN_NonlinearSetbackLaw.md`.

## Stage 3d — rolling-ball G2 continuity

Still open. Replace the circular/rolling support join with a construction whose tangent and
curvature match are measured on both sides, without describing the Stage 3b quintic profile as
a rolling-ball solution.

## Stage 4 — partial edges, apexes, broader supports, curved loops, and healing

Each is a separate classifier/reconstruction slice:

- partial-edge blends with exact endpoint caps;
- complete and partial apex fillets;
- non-coaxial/oblique/mixed-support routes;
- arbitrary curved edge loops and corner patches;
- general intersection/trim/sew healing only after explicit topology ownership exists.

## Stage 5 — proof coverage

Persist only distinct proof images. Add the older Phase 25/31/32 baseline PNGs and gate entries
where they are still part of the required visible-proof set; do not create renamed copies of an
existing fixture.

## Immediate exit gate

Stage 1 needs a production API, a dedicated verifier, transactional refusals, analytic volume
and topology checks, an exterior-facing filled proof image, and a full focused gate with zero
failures. The verifier must also prove that zero/negative/consuming laws and non-planar,
non-perpendicular, partial, nonlinear, and apex requests refuse.
