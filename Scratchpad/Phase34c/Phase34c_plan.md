# Phase 34c — exact planar chamfers by topology

## Why
The cutter-Boolean chamfer produced a genus-1 body with a 90° wedge volume for a 120° prism edge without refusing
(`Scratchpad/Phase34b/Repro_HexChamfer.arc`); its 1e-3 volume gate cannot tell the two wedges apart. Lists were refused,
loops unreachable, and the live-dim path still called `BrepBody::ChamferEdge`, which left open sheets.

## Route (`Kernel/ChamferSolver.{h,cpp}`)
- [x] single straight convex edge between planar faces, three-valent ends: in-place Euler edit reusing the dead slots
      (ΔV +2, ΔE +3, ΔF +1), neighbours re-trimmed on their planes, natural quads become trimmed planes.
- [x] planar face rim: inset polygon, lowered side edges, one quad per rim edge with shared mitre edges (ΔV +n, ΔE +2n,
      ΔF +n); corners whose two chamfer planes cut the side edge at different points refuse (vertex face needed).
- [x] dispatch: one edge → edge route; exactly a face's rim as a set → rim route; anything else refuses.
- [x] self-check: closed/manifold/oriented and volume = source − Σ three tetrahedra per edge, within VolumeTolerance.
- [x] console `chamfer --edges=i[,j…] | --face=i`, native cylinder cap fallback, live dims through the same solver.
- [x] `PlanarChamferVerification` (38 checks) and `Proofs/Phase34c_PlanarChamfer.png`.

## Findings
- The removed prism of a cap edge whose end faces lean is longer than the edge: `½d² × (L + 2·(d/3)·cot 60°)` on the
  hexagon — the probe's naive `½d²·L` expectation was wrong, the solver's tetrahedra were right.
- Hexagon side-face rims genuinely need vertex faces (Phase 34 "still required").
