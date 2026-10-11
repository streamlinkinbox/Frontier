# Plan — Batch 57 regular triangular and hexagonal prism upper-cap face offsets

## Scope

Batch two regular polygonal straight-prism face-offset domains:

1. an origin-centred regular triangular prism; and
2. an origin-centred regular hexagonal prism.

Only the upper planar cap is supported. A finite positive distance extends the upper cap along +Z,
while preserving the exact regular profile, planar caps, and extrusion walls.

## Recognition and reconstruction

`OffsetExtrudedTriangularPrism` recognizes the exact convex-prism topology `V6/E9/C18/L5/F5`,
three straight cap edges, a regular origin-centred profile with a canonical +X vertex, two planar
caps, and three extrusion walls. `OffsetExtrudedHexagonalPrism` recognizes the corresponding
`V12/E18/C36/L8/F8` profile with six regular sides and six extrusion walls.

The shared strict reader rejects translated, non-regular, non-convex, wrong-sided, malformed, and
non-planar/freeform profiles. Reconstruction uses the source lower profile and a fresh Z extrusion
at `height + distance`; no polygon approximation, Boolean healing, or source mutation is used.
The existing five-sided route remains separate, and the public `OffsetFace` dispatcher reaches both
new routes without widening their recognition domains.

## Verification and proof

`TriangularAndHexagonalPrismFaceOffsetVerification` is one consolidated verifier. It checks:

- exact regular source construction and topology for both profiles;
- positive distances `0.5`, `1.5`, and `3.0`;
- exact regular-polygon area times extended-height volume identities;
- upper-cap-only selection, public dispatch, source immutability, and analytic planar/extrusion results;
- cross-domain, lower/side, invalid-distance, translated, non-regular, and malformed refusal;
- a durable four-body contact sheet containing sharp and offset instances of both profiles.

The durable proof is `Proofs/Batch57_TriangularAndHexagonalPrismFaceOffset.png`. The verifier and
proof are registered in `CMakeLists.txt` and `Tools/Build/CheckSolidArc.sh`.

## Explicit non-goals

This batch does not prove arbitrary polygon counts, irregular polygons, concave profiles, translated
or tilted prisms, inward/lower/side offsets, multi-loop profiles, freeform supports, healing, or
general polygonal face editing.
