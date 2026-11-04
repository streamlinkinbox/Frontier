# Space Formats Review

Open `index.html` through the live preview for the original brief, refined plan, classified source findings,
immutable source links, 43 retained native UI captures and the new Material Library CPU capture.
No folder browsing or external image loading is needed.
This is an evidence viewer, not an independent implementation of the editor or proposed formats.

## Scope

- Baseline: `SultanAladin/Frontier-`, exact commit `338293863d7df4e585dec0c4acae45fe9c524ed5`.
- The 19 requested files plus ConsoleHost.h/cpp and CelestialSequence.cpp are pinned by Git blob and SHA-256.
- Findings distinguish confirmed source observations, suspected consequences, and proposed requirements.
- The original supplied brief body is preserved separately and checked against its retained SHA-256 snapshot.
- Current planning document: `Documentation/Plans/Ongoing/SpaceFormats.md`; implementation awaits approval.
- UI captures are regenerated from authorised production navigation fixes at `e016a0b` plus uncommitted changes.
- B is approved: editable authoring sources remain separate from compiled binary runtime packages.
- Editable projectspace persistence is implemented. Other/binary format codecs remain specification-only.
- No commit, push or Actions is authorised for this batch.

## Reproduce

Prerequisites: Python 3.9+, Node.js 18+, and a source copy of the pinned baseline.
DOM checks use jsdom 26.1.0 as a validation-only dependency in ignored scratch.
No browser/runtime package is added to the engine or Windows application.

```sh
python Exhibits/SpaceFormats/Code/BuildReview.py --baseline _ClaudeScratch/tmp/upstream
npm install --prefix _ClaudeScratch/tmp/review-browser jsdom@26.1.0 --no-audit --no-fund
NODE_PATH="$PWD/_ClaudeScratch/tmp/review-browser/node_modules" \
  node Exhibits/SpaceFormats/Code/ReviewValidation.cjs
python -m http.server 8765 --bind 0.0.0.0 --directory Exhibits/SpaceFormats
```

The baseline location can be anywhere; pass `--baseline` accordingly. Local files must match BaselinePins.json.
Serve only this exhibit directory, never the repository root or its `.git`/scratch contents.
The page includes its images and document downloads as data URIs and does not call another frontend/API service.

For HTTP verification while the server runs:

```sh
python Exhibits/SpaceFormats/Code/ValidateReview.py --url http://127.0.0.1:8765
```

The loopback URL above is only for the sandbox-side verification command. User-facing access is the live-preview URL.

## Results and limits

| Check                                | Evidence                                  |
|--------------------------------------|-------------------------------------------|
| Baseline source byte pins            | Results/SourceAudit.json                  |
| Finding classifications/line ranges  | Results/SourceAudit.json                  |
| Original brief snapshot              | Documentation/Plans/References/*.sha256   |
| Embedded PNG bytes and dimensions    | Results/SourceAudit.json                  |
| Five tabs / 43 image selections      | Results/ViewerChecks.json                 |
| HTTP response and read-only boundary | Results/ReviewChecks.json                 |
| Proposed format acceptance           | Not run; no new codec exists.             |
| Historical Space gate                | Not run in this planning pass.            |
| New native UI execution              | Passed; ControlCentre/Results/NavigationChecks.json. |
| Full-browser visual test             | Not run; DOM checks are not a browser.    |
| Windows runtime/GPU                  | Not run.                                  |

Host: Linux editing sandbox, Python 3.11 and Node.js 22. Actual versions and commands are retained by validation.
The viewer is self-contained HTML; it is not a distributed Linux application binary.
Source scope is targeted inspection, not an exhaustive consumer trace or security audit.
Do not turn static checks or native UI results into a passing claim for the proposed format codec.

## Uploaded confirmation design references

Six requested JPEGs are pinned in `Results/ConfirmationReferences.json`, separately from the older Frontier- baseline.
These are source/design references, not new production rendering evidence. The editor projectspace persistence slice is
implemented, as are top-right cards, Quick Controls Notification Stream and preference confirmation popups.
Genuine notification action callbacks,
independent severe-error presentation and binary runtime formats remain pending.

```sh
python Exhibits/SpaceFormats/Code/InspectConfirmationReferences.py
```

Reference recovery uses authenticated `gh api` and ImageMagick `identify`; files stay in ignored scratch storage.

## Texture Studio supplement — 2026-10-18

The evidence tab additionally embeds the current docked Material workspace (CPU UI plus actual Vulkan atlas/preview readbacks)
and library pointer-validation capture and links to the separate
Vulkan review. Neither is a Windows-window execution claim. The original brief, baseline pins and historical
43-capture gallery are retained unchanged. The template/builder retain this supplement on future regeneration.

Current full-browser checks for the supplement (five tabs, embedded image bytes, original download bytes and
private-path refusal) are recorded in `Exhibits/Renderer/Results/PreviewPbrBrowserChecks.json`.
Older `ReviewChecks.json` / `ViewerChecks.json` describe their dated historical viewer, not this supplemental build.
Regenerating the entire historical source audit still requires its pinned baseline and refreshed native-source checks.
