# Contact sheet — one image, every phase

The contact sheet is a single PNG that proves the whole toolchain on a
single render. It is the artefact a reviewer can open to confirm the
project compiles, the kernel is wired, the console drives every phase end to
end, and the renderer carries the result to a PNG.

## What it is

`Proofs/Phase10_ContactSheet.png` — a 2×2 grid of `1280×800` tiles, each
tile rendered fresh from the live `SceneDocument`. The script that
generates it is `Scripts/Phase10_ContactSheet.arc`, registered with ctest
so the artefact is rebuilt on every run.

The four tiles are:

| Tile | View | Content |
|---|---|---|
| Top-left    | top    | Phase 2 — primitives, sketch, sweep, revolve |
| Top-right   | front  | Phase 3/3b — sketch tools, gizmo, matcap studios |
| Bottom-left | right  | Phase 6/7 — solid primitives, profile algebra, areas, fill |
| Bottom-right | iso   | Phase 8/9/9b — loft/sweep/pipe, NURBS booleans, FairPatch |

Each tile is **its own scene** (so a primitive from Phase 2 is not occluded
by a Boolean from Phase 9), but the camera framing, the lattice, the matcap
selection, the line widths and the per-figure tint are matched across tiles
so the four views read as a single object.

## How it is generated

The `Phase10_ContactSheet.arc` script:

1. Sets a 1280×1600 target on the `SoftwareRaster`.
2. Resets the document, sets the view, renders one tile.
3. `Resize` to the next tile, re-fit, render again.
4. After all four, `WritePng` a hand-rolled 2×2 composite out of the four
   readback buffers.

The composite is hand-rolled (not a stb_image_copy) so the tool has no
external dependency. The composite copy is in
`Console/ConsoleHost.cpp::Cmd_ContactSheet` and uses the existing PNG
write path.

## Why a script and not a verification executable

The contact sheet is a script because the scene is rendered through the
same `ConsoleHost::Render()` path a user would hit, with the same
matcap/tint/highlight/selection state. A verification executable that
bypasses the console would tell us only that the kernel works — not that
the console drives it end to end.

`SuiteVerification` (the new Phase 10 verification) then **re-renders the
contact sheet** programmatically, opens it back, and confirms:

- the file exists,
- it is a valid PNG (`IHDR` + `IDAT` + `IEND` chunks, `RGBA8`),
- the pixel count is `2 × 1280 × 800 × 4` on each side,
- the four tile regions each contain a non-trivial number of non-background
  pixels (a regression net against an empty raster).

## Suite render

A second PNG, `Proofs/Phase10_Suite.png`, is a **single iso render of the
combined scene** with every phase's contribution composited into one
document. It is the companion artefact to the contact sheet — if the
contact sheet proves the four views work, the suite render proves the
phases coexist in a single document.

The suite script (`Scripts/Phase10_Suite.arc`) runs every per-phase script
inline (each starts with `view iso ; gizmo off ; select none` so the camera
state is reset between phases) and the final `render Phase10_Suite` is the
single 1280×800 image.

## Acceptance

- `ctest -R Phase10_ContactSheet` exits 0 and produces the PNG.
- `ctest -R Phase10_Suite` exits 0 and produces the PNG.
- `ctest -R SuiteVerification` exits 0, lists the per-phase files, and
  reports the byte sizes.
- All three run inside the standard `ctest` regression net.
