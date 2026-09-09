══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
  Star catalogue — groundwork, deliberately not wired
══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════

STATUS
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
