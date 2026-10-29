# Engine Content

- `Icons/`: shared SVG content used by the native interface.
- `CelestialMaps/`: measured lunar albedo (source JPG, runtime PPM, provenance note),
  loaded by `Frontier::LunarAlbedoMap`; regenerate with `Automation/PrepareMoonAlbedo.py`.
- `FontArchives/<font name>/`: downloaded static fonts and original OFL licence notices.
- `FontArchives/FontCatalogue.toml`: generated metadata consumed by native C++ font discovery.
- `FontArchives/FontCatalogue.json`: equivalent inventory for automation and retained reports.

Recover ignored fonts with `python Automation/PrepareFonts.py` after installing
`Automation/FontRequirements.txt`. Regenerate metadata with `--scan-only` after adding fonts, then restart.
All engine-authored native runtime descriptors use TOML through toml++.

## Overlay projects (`Frontier/Projects/`)

This tree holds the engine-provided fonts and interface art that every
project can use immediately. Projects **reference** it (the host resolves a
content-relative path against the project's own `Content/` first, then here);
nothing is copied into a project when it is created. A project's build
stages only the entries its `ContentManifest.toml` lists.

## Fonts

`FontArchives/` holds vendored typefaces (Inter, Source Sans 3, Source Code
Pro, Euclid Circular A, DM Sans, Space Grotesk, Plus Jakarta Sans and the
nerd-font glyph fallback in the upstream checkout) plus the generated
`FontCatalogue.toml` above. The font binaries are intentionally **not**
tracked in git (licence-safe, size-safe) — `EngineContent/FontArchives/` is
ignored and recovered by `Automation/PrepareFonts.py`; projects
can equally drop their own typeface into their project `Content/Fonts/` to
shadow the shared one. Until fonts are recovered here, text rendering must
be reported as blocked, never faked.
