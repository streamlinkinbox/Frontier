# Sun and Moon outliner artwork

Browser reference paths in this document are relative to `Experimental/FrontierEditor/`.

The Sun and Moon SVGs **existed throughout**. The failure was native SVG-filter compatibility, not asset availability. Both now render in the actual native outliner using the existing artwork; no replacement Moon design or photograph was needed.

## Implementation

- `BakeCelestialIcons.mjs` verifies that the shipped SVG bytes equal `custom-icons/sun.svg` and `custom-icons/moon.svg`, then rasterizes those exact SVGs with Chromium. No filters are stripped.
- `EngineContent/Icons/Baked/` contains transparent 256×256 previews, straight-alpha RGBA runtime assets and source/output hashes. Chromium is a build-time dependency only.
- `IconArt.cpp` loads these two approved bakes when the SVG backend reports unsupported filters. Other icons retain strict diagnostics.
- `BakedIconArt.h` verifies the embedded source bytes against the currently loaded SVG, validates lengths/dimensions and area-filters in premultiplied alpha for the requested native resolution. Missing, malformed or stale bakes cannot be reported as ready.
- This preserves browser-rendered filter detail without claiming that ThorVG gained support for turbulence/displacement. Downsampling and native texture filtering are not a browser-pixel-equivalence claim.

## Verification

49 dedicated checks PASS in Release, Debug and ASan/UBSan: real IconArt output, 1×–4× sizes, alpha margins, caching, rectangular letterboxing and stale/missing/malformed asset rejection. All 46 main-editor integration checks also PASS again in all three modes. The ThorVG static dependency remains Release-only; no Windows or GPU verification is claimed.

All 32 native exhibit captures were regenerated. Open `Exhibits/Gallery/MainEditorNative/index.html` (live preview port 5187). Sun and Moon now appear in the outliner. Remaining Cloud/Local Cloud/Local Fog placeholders are unchanged by this scoped correction.

## Rebuild

```sh
npm install --prefix .cache/native-icon-browser --no-audit --no-fund playwright-core@1.63.0 @sparticuz/chromium@153.0.0
node Exhibits/Workbench/IconArt/BakeCelestialIcons.mjs
python3 Exhibits/Workbench/IconArt/RunCelestialBakeProof.py
python3 Exhibits/Workbench/MainEditor/RunIntegrationProof.py
python3 Exhibits/Workbench/MainEditor/RenderExhibit.py
```

The browser package's bundled NSS/NSPR libraries are extracted into the workspace cache for minimal Linux workers. Dependencies/binaries are not committed. Shipped baked assets allow normal native execution without a browser or Node.js.
