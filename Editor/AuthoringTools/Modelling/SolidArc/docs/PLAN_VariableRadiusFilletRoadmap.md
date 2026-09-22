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

## Stage 2 — variable setback laws

Add a separate law for support setback/clearance and prove it against the station geometry.
Nonlinear radius and setback laws remain refused until their own surface and curvature checks
exist.

## Stage 3 — nonlinear laws and G2 continuity

Add explicit law descriptors and a surface-construction/curvature acceptance test. Do not reuse
the linear ruled route as an approximation. Reject a law when endpoint tangent or curvature
constraints cannot be measured and satisfied.

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
