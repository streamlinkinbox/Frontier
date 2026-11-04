# Frontier — Windows Control Centre Exhibit

## Run

- Requires Windows 10/11 x64 with Direct3D 11 and the Windows Universal CRT.
- Extract the entire archive into a writable directory, not just the `.exe`.
- Run `FrontierUI.exe`. It uses the GLFW window/input and the ImGui DX11 display adapter.
- This is an unsigned development exhibit, not a signed production release.
- The executable was cross-compiled and inspected as Windows x64, but not executed on Windows here.
- No scene renderer, GI, render-quality, resolution, AA, or shadow controls are included.

## Controls

- Starts with a top-edge trapezoidal notch. Tap it or pull down to open.
- Slide the bottom grip, card header, or quick-card blank space upward to close. Escape also closes.
- Drag the closed notch sideways to reposition it.
- Fonts live under `EngineContent/FontArchives/<font name>/`; SVGs under `EngineContent/Icons/`.
- GLFW owns this window; DX11 displays ImGui only. Primary engine rendering remains Vulkan/Slang.
- The top quick panel opens Fonts or Theme and adjusts interface scale.
- `Ctrl+,` toggles the main Control Centre.
- Font cards, available weight chips, and size sliders edit a draft.
- Theme tiles, accent swatches, semantic colours, and corner radius edit a draft.
- Apply saves preferences; Discard restores the applied preferences.
- Scroll the main content to see the complete typography and theme sections.

Preferences are stored in `%LOCALAPPDATA%/Frontier/Appearance.toml` when that directory is available.
If it is unavailable, preferences apply for the current session only.
Fonts are restored by family name, not a fragile numerical position in the folder inventory.

## Content and licences

The bundle contains Inter, Archivo, Space Grotesk, and JetBrains Mono: 30 static faces.
Each font family includes its original SIL Open Font License notice.
`EngineContent/FontArchives/FontCatalogue.toml` lists every bundled face with weight and SHA-256.
Original and inherited compatibility SVGs are in `EngineContent/Icons/`.
Compatibility differences are retained in the SVG manifest; these are not exact browser-parity claims.
Dependency notices are in `Notices/`; retained artwork notices are beside the SVGs.
No new licence grant is asserted for inherited Frontier or Slate artwork.

`Package.json` records the toolchain, source status, and hashes of bundled files.
Source, automated tests, and CPU-generated UI images are retained in the repository exhibits.
