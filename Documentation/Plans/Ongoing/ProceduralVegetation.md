# Procedural Vegetation — One-Editor Foliage Merge

## TL;DR

- ✔️ Import #1 landed and verified: the vegetation generator demo from `unassignedinbox/Slate`
  `arena/01a096c9-slate` (`3b1172f7`) at `Experimental/ProceduralVegetation/` — 170/170 tests.
- 🚧 Remaining: inventory every other foliage/plant implementation the owner has, import each the
  same pinned way, then merge them into one editor surface. No inventory is invented in advance.

## Import ledger

| # | Implementation | Pinned source | Destination | Evidence |
| --- | --- | --- | --- | --- |
| 1 | Tree/grass/desert + bark (Vite/TS) | `unassignedinbox/Slate@3b1172f7` | `Experimental/ProceduralVegetation/` |

## What import #1 contains (measured, not brochure)

| Capability | Source | Notes |
| --- | --- | --- |
| 51 species presets | `tree/params.ts` + `plant/grassParams.ts` | Oaks…rocky terrain + grasses |
| One closed welded surface per plant | `tree/mesher.ts`, `plant/grassMesher.ts` | 170/170 suite, seeds 1/7/42 |
| Wind attributes, OBJ/GLB export | `src/tree/export.ts`, `validate.ts` | Deterministic per seed (suite check) |
| Worker generation | `src/worker/tree.worker.ts` | Keep with demo until native placement |
| Bark cluster laboratory | `src/bark-lab/`, `bark-lab.html` | Separate HTML entry, shares dependency graph only |

## Merge sequence (per next owner-named source)

| Step | Work | Exit evidence |
| --- | --- | --- |
| 1 | Owner names the next foliage/plant repository+branch; pin revision | Ledger row in this file |
| 2 | Import demo-only files byte-identical beside import #1 (own subfolder until merged) | Blob-identity check |
| 3 | Run its own tests/build unchanged | Logs under `Exhibits/ProceduralVegetation/Results/` |
| 4 | Compare parameter models / geometry output / export formats in one table | Merge decision table to owner |
| 5 | Unify into one editor surface; compliant renames only at graduation | One entry point + evidence |

## Boundaries

- `Experimental/` only until the owner approves graduation; no `Engine/` code, no native renderer claims.
- Upstream names inside imports are retained verbatim (reference material); new code/names here follow
  `CLAUDE.md` + the four skill documents.
- Missing locally: `Documentation/FolderStructure.md` and `Documentation/Tracker.md` (exist on
  `SultanAladin/Frontier arena/649fea90`); restore on the next main-project merge, do not fork copies.
