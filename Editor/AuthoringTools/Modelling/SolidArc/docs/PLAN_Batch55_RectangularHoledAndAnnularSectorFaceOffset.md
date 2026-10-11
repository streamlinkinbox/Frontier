# Plan — Batch 55 exact rectangular-frame and annular-sector face offsets

## Scope

Batch the next two distinct upper-cap face-offset domains instead of spending a separate phase on
trivial distance variants:

1. an origin-centred, axis-aligned rectangular frame extruded along Z, with one exact rectangular
   through-hole; and
2. an origin-centred quarter annular sector extruded along Z, with two concentric rational circular
   arcs and two radial lines.

Only the upper planar cap is supported for either route. The operation extends the result height by a
finite positive distance and reconstructs analytic/planar supports without tessellation, Boolean
healing, or mutation of the source.

## Recognition and reconstruction

`FaceEditSolver::OffsetExtrudedRectangularHoledPrism` recognizes the exact two-loop rectangular
prism topology produced by the kernel (`V16/E24/C48/L12/F10`, genus one), centered inner bounds,
axis-aligned line edges, two planar two-loop caps, and eight extrusion walls. It rebuilds the two
closed rectangular profiles at the source lower Z level and extrudes them to `height + distance`.

`FaceEditSolver::OffsetExtrudedAnnularSectorPrism` recognizes the exact quarter annular-sector
prism (`V8/E12/C24/L6/F6`, genus zero), a planar four-edge upper cap containing two rational degree-2
quarter arcs and two exact radial lines, four extrusion walls, and an origin-centred common arc
centre. Joined profile seams are accepted only when the non-rational freeform portions are proven
straight by sampled collinearity; circular portions remain rational analytic arcs. Reconstruction
uses the exact outer and inner arcs and radial boundaries and extrudes the profile to the extended
height.

The public `OffsetFace` dispatcher tries both bounded routes after the existing analytic annular
routes. Unsupported arbitrary multi-loop, freeform, translated, tilted, mixed-support, lower-cap,
side-face, invalid-distance, malformed, and healing-dependent cases continue to refuse transactionally.

## Verification and proof

`RectangularHoledAndAnnularSectorFaceOffsetVerification` is one consolidated verifier. It checks:

- exact source construction and closed topology for both domains;
- planar/analytic extrusion supports and upper-cap recognition;
- positive distances `0.5`, `1.5`, and `3.0` with result topology and area-times-height volume identities;
- public dispatcher routing and source immutability;
- lower/side face refusal, zero/negative/non-finite distance refusal, malformed topology refusal,
  translated-profile refusal, and cross-domain refusal;
- a durable four-body contact sheet containing sharp and offset instances of both domains.

The durable proof is `Proofs/Batch55_RectangularHoledAndAnnularSectorFaceOffset.png`. The focused
verifier and proof are registered in `CMakeLists.txt` and `Tools/Build/CheckSolidArc.sh`.

## Explicit non-goals

This batch does not claim arbitrary rectangular holes, arbitrary annular-sector sweep angles,
non-concentric arcs, translated or tilted profiles, inward offsets, lower/side face edits, multiple
holes, freeform boundaries, mixed supports, general trimming/sewing, Boolean healing, or sliver
collapse. Those domains remain explicit refusal boundaries until independently implemented and proven.
