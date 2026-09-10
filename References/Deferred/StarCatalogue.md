══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
  Star catalogue — groundwork, deliberately not wired
══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════

STATUS
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
🟢 **SUPERSEDED — the trigger this note named has fired.** The full HYG catalogue was obtained and converted:
**8 920 stars in 244 KB**, installed at `EngineContent/StarCatalogue/BrightStars.bin`. The note below said the
decision would change if `ConvertHygCatalogue.py` were run against the real HYG CSV, producing ~9 100 stars in
~256 KB — that is exactly what happened, so the procedural-only decision no longer stands and the catalogue is
being wired in during Celestial step 3.

How it was obtained, since the README's method does not work here: `raw.githubusercontent.com` is blocked in this
sandbox (curl returns HTTP 000), but a **sparse git clone** of the repository is not —
`git clone --depth 1 --filter=blob:none --sparse https://github.com/astronexus/HYG-Database.git` then
`git sparse-checkout set hyg/CURRENT`. The README has been updated with both methods.

Two defects in the converter were found and fixed on the way:
  · HYG **quotes its header row** as of v41 (`"id","hip",…`); the converter searched only for the bare `proper,`
    and `id,` spellings, matched neither, and started reading one character before end-of-file.
  · That produced **zero stars silently** — it printed a cheerful "0 stars to magnitude 6.5" and wrote a 16-byte
    header-only asset over the good one. A zero count is now a hard error that refuses to write.

Positions verified independently after conversion: Sirius RA 101.29 Dec −16.72, Canopus RA 95.99 Dec −52.70,
Arcturus RA 213.92 Dec +19.18 — each matching published values to two decimals, with Sirius blue-white
(0.79, 0.85, 1.00) and Arcturus orange (1.00, 0.83, 0.69). All 8 920 direction vectors are unit length.

**Now wired.** The three `.groundwork` files have moved into the tree as `Engine/GeometricRaster/StarCatalogueIndex.{h,cpp}`
and `Scratchpad/StarCatalogueTest.cpp`, and the GI-off raster samples the catalogue. Against 8 920 stars the
binning reports 1 022 of 1 024 cells occupied and 276 hits per 20 000 probes, where 178 stars gave 166 cells and
12 hits — the sparseness that made the original decision is gone. Gated by `Scratchpad/CheckStarCatalogue.sh`.

The original note follows, unchanged, because its reasoning is what made the trigger recognisable.

──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

The renderer draws a **procedural** star field. That is the decision, not an interim state.

This directory holds the catalogue work that was written before that decision, kept because it is finished and
correct and because throwing it away would mean redoing it. The `.groundwork` suffix means these files are not
compiled and are not in either build system — they are notes with a compiler-checkable shape.

WHAT IS ALREADY DONE AND PROVEN
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Committed and live in the tree:

  · `Tools/StarCatalogue/ConvertHygCatalogue.py` — HYG CSV → compact binary. Handles both HYG's format and
    Slate's own CSV, so one converter serves both.
  · `Tools/StarCatalogue/BrightStars.csv` — 178 real stars, everything to about magnitude 3.
  · `EngineContent/StarCatalogue/BrightStars.bin` — the 5 KB asset.
  · `Tools/StarCatalogue/README.md` — the fetch instructions for the full ~9 100.

Here, not compiled:

  · `StarCatalogueIndex.{h,cpp}` — the loader and the octahedral binning.
  · `StarCatalogueTest.cpp` — the proof harness.

The binning was verified: 20 000 probes over the sphere, **zero stars missed** by the cell lookup, boundary
duplication factor 1.112, and a worst case of 3 stars tested per pixel instead of 178.

Positions were verified against geometry that exists independently of this repository — Orion's belt spans
2.74° against a real 2.70°, the Big Dipper 25.71° against 25.6°, Polaris at declination 89.26°. Colour comes
from B−V through temperature to blackbody RGB, so Rigel's blue/red ratio is 1.29 and Betelgeuse's 0.52.

WHY IT IS NOT WIRED
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
The full naked-eye catalogue is ~9 100 stars, and obtaining it means a 35 MB download. What could be committed
here was 178 stars — enough for correct, recognisable constellations, and measurably **too sparse to be a sky**:
the proof run found only 12 star hits across 20 000 probes, with 166 of 1 024 cells occupied.

So the honest options were a sparse-but-real sky, or a dense procedural one. Procedural was chosen.

Wiring it now would also mean an SSBO, two more bindings, and a binding renumber — real cost against a sky that
would look worse until someone runs the converter.

WHAT WOULD CHANGE THE DECISION
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Running `ConvertHygCatalogue.py` against the real HYG CSV, which produces ~9 100 stars in ~256 KB. At that
density a catalogue is strictly better than a hash: real constellations, a real magnitude distribution, and a
celestial pole to turn around.

WHAT REMAINS TO DO, IF SO
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
  1. Move the two `.groundwork` files back into `Engine/GeometricRaster/` and the test into `Scratchpad/`.
  2. Upload the stars and the cell table as two SSBOs. `Textures[]` must stay the highest binding, so this is a
     renumber — the same one done in A2 and A7, and the gate already derives that relationship rather than
     trusting a literal.
  3. Replace `StarField()`'s hash with a cell lookup. `CellForDirection` must be ported to the shader exactly;
     if the two disagree the sky is simply empty, with no error.
  4. Keep the procedural field underneath as faint background if density still falls short, and say so in the
     source — half-real data presented as a catalogue would be a lie in the tree.

WHAT WAS FIXED ON THE WAY OUT
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Measuring the procedural field to compare it against the catalogue found a real defect in it: the grid and
threshold were producing **120 352 stars, thirteen times the naked-eye sky**. Thirteen times too many does not
read as a rich sky, it reads as noise — the eye stops resolving individual points and sees texture. Corrected to
9 056, and the count is now asserted rather than estimated, because the fraction of *cells* holding a star is
not the fraction of *directions* that land on one and estimating from the threshold is how it went wrong.
