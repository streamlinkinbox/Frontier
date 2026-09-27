# Plan — Batch 56 native-cylinder and native-cone upper-cap face offsets

## Scope

Batch two related but distinct analytic face-offset domains through the public `FaceEditSolver` API:

1. an origin-centred native right cylinder with one analytic cylindrical side and two planar caps; and
2. an origin-centred native frustum with one analytic conical side and two planar caps.

Only the upper planar cap is supported. A finite positive distance extends the cap along +Z. The
cylinder preserves its radius; the cone continues the exact linear radius law on its native conical
support. Apex cones are intentionally refused.

## Recognition and reconstruction

`OffsetCylinderCap` recognizes the native kernel topology `V2/E3/C6/L3/F3`, one exact analytic
cylinder face, two planar one-loop caps, two rational circular rims, one seam line, a canonical origin,
a +Z axis, and a selected upper cap. It reconstructs an exact native cylinder with height increased
by the requested distance.

`OffsetConeCap` recognizes the same closed native topology, one exact analytic cone face with unequal
positive foot and top radii, two planar caps, canonical origin/+Z support, and the selected upper cap.
It computes the continued top radius from the native linear slope and reconstructs an exact frustum
with the extended height. If the continued radius reaches the apex, refusal is transactional.

Both routes are separate from the existing `BlendSolver::PushFace` tests: they are public
`FaceEditSolver::OffsetFace` domains with strict recognition, source immutability, and no healing,
Boolean, or tessellation fallback. Translated, mixed, malformed, lower/side, cross-support, invalid,
and apex/collapse cases remain refused.

## Verification and proof

`NativeCylinderAndConeCapFaceOffsetVerification` is one consolidated verifier. It checks:

- exact source topology and analytic/planar support classification;
- positive distances `0.5`, `1.5`, and `3.0` for both native domains;
- cylinder and frustum volume identities, result topology, and public dispatcher routing;
- source immutability and refusal of lower/side faces, invalid distances, translated supports,
  cross-domain calls, apex cones, apex-crossing extensions, and malformed topology;
- a durable four-body contact sheet containing sharp and offset cylinder/cone pairs.

The durable proof is `Proofs/Batch56_NativeCylinderAndConeCapFaceOffset.png`. The verifier and proof
are registered in `CMakeLists.txt` and `Tools/Build/CheckSolidArc.sh`.

## Explicit non-goals

This batch does not claim lower-cap, cylindrical-side, conical-side, inward, oblique, translated,
spindle, apex, partial/trimmed, freeform, mixed-support, or general conic face editing. Those remain
explicit refusal boundaries until separately implemented and proven.
