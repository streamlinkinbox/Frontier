# Content Browser

Native, read-only file browsing in the Windows UI host. Actual production draw-command captures are in `Images/`.
This is not a React runtime, scene renderer, import pipeline, thumbnail renderer, or Space-format implementation.

## Reference and adaptation

Pinned baseline: `SultanAladin/Frontier-@338293863d7df4e585dec0c4acae45fe9c524ed5`.
The source is `Experimental/TextureShaderEditor/src/AssetContentBrowser.jsx` and `assetContentBrowser.css`.
Their Git blobs and SHA-256 pins are in `../ControlCentre/Results/NavigationReferencePins.json`.

| Baseline mechanism                    | Native implementation                                           |
|---------------------------------------|-----------------------------------------------------------------|
| Bottom expandable workspace           | Shared `PanelExpansionTab`, upward direction, bounded travel.    |
| Resizable workspace                   | Drag/tap/flick expansion plus Maximize/Restore; no free resizing. |
| Content/category navigation           | Real directories, Back/Forward/Up/Root/Reload, file categories.   |
| Search and grid/list                  | Shared text entry and segmented control; filename filtering.     |
| Selection inspector                   | Real filename, category, relative path and byte count.           |
| Materials, brushes, generators, stamps | Excluded: depend on painting/scene runtime outside this port.    |
| Resource thumbnails                   | Existing file-type glyphs and metadata, not content previews.    |

The browser is a lifecycle-owned member of `ControlCentrePanel`, integrated by the existing Windows host.
The two panels share applied fonts, theme colours, control styling, pointer conventions and `PanelExpansionTab`.
The Control Centre retains priority while open or travelling. There is no second preferences catalogue or widget kit.

## Controls

- Bottom tab: tap or drag upward to expand; drag the grip/header downward or press Escape to dismiss.
- Maximize/Restore changes the open height. Narrow layouts use the Size button.
- Single click selects; double click or Enter opens a folder. Arrow keys change selection.
- Back/Forward, Up, Root and Reload navigate the canonical content root; folders stay visible under type filters.
- Search filters filenames in the current directory, live. Enter accepts; Escape restores the pre-edit query.
- Ctrl+F focuses search when the browser has focus. Backspace goes up; Alt+Left/Right navigates history; F5 reloads.
- Categories appear in a sidebar on wide layouts and a shared dropdown on narrow layouts.
- A type/query change clears selection if it is no longer visible. Refresh removes deleted entries and selection.
- No writes, deletes, imports, application launches or format conversions are available.

## Reproduce

Use the dependency setup and Debug/sanitizer/Windows build configurations in `../ControlCentre/README.md`, then:

```sh
python Exhibits/ControlCentre/Code/ValidateNavigation.py --baseline _ClaudeScratch/tmp/upstream
```

The driver runs nine Debug and nine address/undefined-behaviour-sanitizer tests, regenerates 23 native PNGs,
and builds/packages/inspects Windows x64. It fails on stale reference pins or any test/build/package failure.
`../ControlCentre/Results/NavigationChecks.json` retains commands, source hashes, image hashes and limitations.
`Code/ContentDirectoryValidation.cpp` exercises disposable real files, Unicode names, navigation/history, filtering,
missing/empty directories, link omission, root confinement, stale selection and the 8192-entry enumeration limit.
`Code/ContentBrowserValidation.cpp` feeds actual native pointer, wheel, key, character and focus events.
It verifies popup ownership, selection, grid/list, scrolling, maximize/restore, shared theme and 375×700 at 125%.
`../ControlCentre/Code/PanelExpansionValidation.cpp` retains top-drawer checks and adds reflected bottom parity.

## Limits

- Images are real ImGui commands rasterized by the exhibit CPU fixture, not Windows GPU screenshots.
- Windows x64 is cross-built and inspected; Windows startup/GPU/DPI behaviour still needs Windows execution.
- There is no scene view in this host. The blank area above the browser is intentional, not a missing scene preview.
- No free-height resize, breadcrumb-segment clicks, recursive search or project-root picker is claimed in this batch.
- ASCII case-insensitive filename search; literal Unicode queries and paths preserve UTF-8, without full case folding.
- Links are omitted. Navigation checks canonical path components, not vulnerable string-prefix comparisons.
- Read-only confinement is not a hostile-filesystem security guarantee: concurrent path replacement is not locked.
- Enumeration is non-recursive and capped at 8192 inspected entries; the UI explicitly reports truncation.
- No commit, push, GitHub Actions, scene integration, binary codec or inherited-folder deletion is part of this batch.
