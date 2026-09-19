# Hand-off: bring SolidArc (ParametricSketcher) Phases 1–20 into `streamlinkinbox/Frontier` @ `arena/01a0881a-frontier`

Source of truth: `SultanAladin/Frontier-` branch `arena/01a08c57-frontier`, folder `Editor/EditorTools/ParametricSketcher/`.
The two repositories share **no git history**, so this is a tree transplant, not a merge.

## Steps (for the agent working in streamlinkinbox/Frontier)
1. Move the existing folder into the new layout (history-preserving):
   `git mv ParametricSketcher Editor/EditorTools/ParametricSketcher`
2. Replace its contents with the newer tree. Either
   - `git diff` between the two trees is ~1 M lines (binary proofs) — do not patch; instead
   - `rm -rf Editor/EditorTools/ParametricSketcher && git archive --remote=<SultanAladin clone> arena/01a08c57-frontier Editor/EditorTools/ParametricSketcher | tar x`
3. Keep the two files that only exist on your side if still wanted:
   `Presentation/Shaders/GridProjection.slang`, `Proofs/Proof_02a_Grid.png` (superseded by `LatticeProjection.slang` — "Grid" is a banned word, see CLAUDE.md §3.1).
4. Copy `Editor/README.md`, `Editor/EditorTools/README.md`, `Editor/EditorTools/TextureEditor/README.md`.
5. Copy CLAUDE.md §3.1 (SolidArc vocabulary) and the `.gitignore` SolidArc block; update any path refs (`grep -rn ParametricSketcher/`).
6. Build & verify: `bash Scratchpad/BuildSolidArc.sh` (g++ only) or `cmake -S Editor/EditorTools/ParametricSketcher -B build/SolidArc && ctest --test-dir build/SolidArc` — 22 suites / 552+ checks expected to pass.

## What changes vs. your current `ParametricSketcher/` (40 files → 204 files)
Phases 2–20 of SolidArc: software raster + Slang shaders, camera, scene document, console `.arc` host, modal sketch tools/snapping/hotkeys, GizmoPRO, selection/undo, B-rep topology, ProfileSolver (2D booleans/fillet/offset/trim), loft/sweep/pipe/Coons/N-sided, NURBS SSI booleans, FairPatch (G0/G1/G2), solidify/chamfer, arrays/bridge/construction planes, dimensions (auto + live edit), 2D constraint graph (Newton, 9 types), mirror/radial/empty ops.
