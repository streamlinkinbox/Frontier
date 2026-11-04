# LayerStackPort evidence

Baked by the production C++ dispatcher (`BakeProceduralChannelRgba`, Albedo, 256 px) from the same
parameters the shipped camouflage `.material` files carry. Pixelated and smooth variants share the
periodic domain-warped field, so opposite tile edges meet without a seam; the pixelated bake holds
exactly the four palette colours, the smooth bake interpolates between them.

| File                              | Op                  | Params                                   |
| --------------------------------- | ------------------- | ---------------------------------------- |
| camouflage-albedo-256.png         | `camouflage`        | scale 8 · seed 12 · pixelSize 4          |
| camouflage-smooth-albedo-256.png  | `camouflage-smooth` | scale 8 · seed 12 · softness .012        |
| noise-albedo-256.png              | `noise`             | scale 8 · seed 12 (grayscale ramp)       |
| cells-albedo-256.png              | `cells`             | scale 8 · seed 12 (grayscale ramp)       |

Palette (all camouflage bakes): `#b5a477 #566044 #514334 #232921`.

## Verification

- Platform: Linux sandbox, GCC 12, CMake/Ninja, `-DFRONTIER_HOST_VERIFICATION=ON -DBUILD_TESTING=ON`,
  base `arena/db2f9953-frontier@34537e1` plus this port. Windows/MSVC and GPU preview not run.
- `ctest`: 26/26 passed. `MaterialAuthoringValidation` checks coded-field samples against goldens
  recorded from `Experimental/TextureStudio/src/FieldSpecification.js` (tolerance 1e-5, seam 1e-9),
  normalize rejection, link/designer projection, `[Authoring]` TOML round-trip stability, and the four
  shipped camouflage presets (authoring document present, coded Albedo channel, non-flat bake,
  pixelated palette confinement).
- HTML: `npm run build` clean; Node model checks 7/7 (preset authoring shape, normalize rejection,
  camouflage determinism/range/seam, registry range, resolve projection, catalogue 131 intact).
  Playwright specs (`material-authoring`, `camouflage`) ship with the port but were not run here:
  the sandbox cannot download a browser and none is installed.
- Catalogue export is deterministic: two consecutive `ExportReferenceLibrary.mjs` runs produce
  byte-identical `Library.toml` and camouflage `.material` files (role-seeded identities).
