# Log Stream UI parity — approved HTML ↔ C++ ↔ upstream kit

Purpose: prove the native panel does not diverge from the approved design
(`Frontier/Projects/Project-VehicleHmiDemo/Reference/LogStreamPanel.html`, 61 ids).
Upstream contracts were re-verified against `arena/db2f9953-frontier` at `c177821`
(1 commit past 59442f7): `git diff 676eceb..c177821` over every header this panel
uses (`PanelDockspace`, `NotchMotion`, `WindowsApplication`, `ControlKit`,
`PixelSpace`, `DropdownControls`, `ThemeStructure`, `CLAUDE.md`) is **empty** —
the kit did not move under the port.

## 1:1 restyle (2026-10-22)

At the owner's direction the native drawing (`LogStreamPanelPresentation.cpp`)
was rewritten as an **exact 1:1 visual copy of the prototype's CSS** — custom
draw-list widgets at the prototype's pixel geometry and DM Sans faces (resolved
through the upstream `TypefaceRegistry`, Light/Regular/Medium/SemiBold; the host
default font is the fallback when DM Sans is absent from FontArchives). Per the
owner's exception, the **dropdown menus and the panel/dock frame remain the
upstream reusable components**: every menu body is `DropdownControlSurface`
(`BeginMenu`/`Option`); the dockspace and trapezoidal tabs are the host's
upstream `PanelDockspace`, and the notch is `ControlCentrePanel`'s trapezoidal
expansion tab — the TextureWorkspace composition (2026-10-22).
Only the menu *triggers* are custom-drawn pills, as in the prototype.

Legend: ✔️ ported 1:1 (geometry, colour, behaviour) · 🟡 ported with a noted
delta · ⬜ not drawn (documented).

| HTML element (ids) | C++ view-model (`LogStreamPanel`) | Native drawing | Status |
|---|---|---|---|
| Brand mark + title | — | 30 px mark, 4 mint bars, 21 px Medium "Log Stream" | ✔️ |
| Live pill (`liveDot`, `liveWord`) | `QueryPaused` | pill + pulsing dot + Live/Paused | 🟡 prototype's static "· Session 04" demo text not reproduced |
| Header clock (`headClock`) | `FormatLogClock` | 13 px faint + dimmed ms | ✔️ |
| Pause/Resume (`btnPause`, icons) | `ConfigurePaused` (pauses the *view*, ring keeps filling) | 36 px icon button, pause/play swap, mint active | ✔️ |
| Clear (`btnClear`) | `ClearView` + "Stream cleared" log line | trash icon button | ✔️ |
| Export (`btnExport`) | visible-entry enumeration | icon button → `FrontierLogStream.log` (host save dialogue later) | 🟡 dialog-free file target |
| Popout (`btnPopout`) | — | not drawn: panel content cannot reach the dockspace to request detach; tear-off is the dock tab's own feature | ⬜ by host design |
| Overflow menu (`btnOverflow`, `menuOverflow`) | — | `···` icon button + `DropdownControlSurface::BeginMenu` | ✔️ |
| └ Copy visible (`miCopyAll`) | visible-entry enumeration | clipboard via `ImGui::SetClipboardText` | ✔️ |
| └ Save session (`miSave`) | visible-entry enumeration | writes `FrontierLogStream.log` | 🟡 dialog-free (as Export) |
| └ Word wrap (`miWrap`) | `ConfigureWrap` | rows wrap (manual word-wrap lines) | ✔️ |
| └ Time mode (`miTimeMode`, `timeModeState`) | `ConfigureSessionClock` | rows/clocks honour it; state text "Wall clock"/"Session" | ✔️ |
| └ Collapse (`miCollapse`) | `ConfigureCollapse` | menu toggle + ×N folding | ✔️ |
| Metrics (`mEntries`, `mWarnings`, `mErrors`) | `QueryTotalCount/SeverityCount` | 38 px Light numerals, dimmed trailing thousands group (prototype `splitNum`), tracked caps labels, warning-triangle label icons | ✔️ |
| Delta badges (`mRate`, `mWarnDelta`, `mErrDelta`) | `QueryRateNow`, `QueryWarningsLastMinute`, `QueryErrorsLastMinute` (60 s windows, new in the view-model) | `+n/s` mint · `+n/min` amber/coral · flat `—` | ✔️ |
| Sparkline (`spark`, `sparkNow`, `sparkPeak`) | `QueryRateBuckets` (72 s), `QueryRatePeak` | exact canvas port: dashed guide, mint under-fill, white polyline, head dot | ✔️ |
| Severity chips (`cAll`…`cFatal`) | `ConfigureSeverity/AllSeverities` + counts | inverted pressed chip, dot, Medium count, unpressed at 55 % | ✔️ |
| Channels dropdown (`ddChannels`, `menuChannels`) | `ConfigureCategory`, counts per token | pill trigger + upstream checklist menu: "ENGINE CHANNELS"/"USER CATEGORIES", right-aligned counts, category dots | ✔️ |
| └ Category creator (`catName`, `catSwatches`, `catAdd`) | `RegisterLogCategory` | name field, 5 swatch discs, mint "Add category" | ✔️ |
| └ All/None/Invert (`chAll`, `chNone`, `chInvert`) | `ConfigureAllCategories`, `InvertCategories` | quick row at the menu **bottom** (as prototyped) | ✔️ |
| Verbosity dropdown (`ddVerbosity`, `menuVerbosity`) | `ConfigureCeiling` | pill trigger ("Verbosity" dim + value) + "SHOW UP TO" menu | ✔️ |
| Search (`searchBox`, `searchInput`) | `ConfigureQuery` (message/source/category, folded) | pill field, magnifier, mint focus border | ✔️ |
| └ Match count + clear (`matchCount`, `searchClear`) | visible count | "N shown" + clear disc | ✔️ |
| └ `<mark>` spans | query text | mint highlight rectangles + bright matched text, wrapped or ellipsised | ✔️ |
| Follow pill (`btnFollow`) | `ConfigureFollow`, breaks on scroll-up | dot + Following/Follow, mint when on | ✔️ |
| Column labels | — | TIME/CHANNEL/MESSAGE/SOURCE tracked caps (hidden < 900 px) | ✔️ |
| Rows | `LogStreamVisibleEntry` (6 s collapse) | grid: dot/triangle glyph · clock + dim ms · tag pill (engine neutral, user coloured + dot) · severity-tinted message · ×N badge · source; hover/selected/fatal washes + 3 px edge bars; narrow (< 900 px) variant drops tags | ✔️ |
| Row expand detail | `SelectVisible` | inset card, full message, meta (Severity/Verbosity/First/Last/Occurrences/Source), 3 action pills | ✔️ |
| Empty state (`streamEmpty`) | `QueryVisible().empty()` | "No entries match" + guidance | ✔️ |
| New-entries pill (`newPill`) | `QueryNewBelow`, `JumpToLatest` | floating mint pill over the stream with glow | ✔️ |
| Footer (`footShown`, `footTotal`, `footDropped`, `footSession`) | counters + session start | strong/faint stat pairs, session `mm:ss`, ring capacity | ✔️ |
| Scrubber (`scrubber`) | `QueryScrubBuckets` (300 × 1 s, worst severity) | exact canvas port: centre line, severity ticks, mint live edge | ✔️ |
| Panel frame + notch (surrounding chrome) | `MakeLogStreamPanelSpecification` | registered into a genuine upstream `PanelDockspace` under a real `ControlCentrePanel` (TextureWorkspace composition: no project-summary header, trapezoidal notch tab in the 79 px band, Viewport | Log Stream 50/50) | ✔️ rendered headlessly (below); `FrontierMain` rehosted onto this exact composition 2026-10-22 (owner direction) and compile-proved |

Remaining intentional deltas (all noted above): the demo "· Session 04" label,
the omitted Popout button (dockspace-owned), and dialog-free export targets.
CSS animations are limited to the live-dot pulse; hover/press states are
instant. Menus keep the upstream surface's own metrics (row height, padding,
check-by-selection-fill) per the owner's reuse exception, and ImGui clamps tall
menus into view where the web prototype would overflow the page.

## Rendered captures (2026-10-22, post-restyle)

Rendered headlessly from the production draw code (`PanelCapture.cpp`,
procedure in `CompileProof.md`; PNG masters in `Capture/`, 13 frames):

- `Capture/panel-*.png` — the panel alone at the prototype's own 1240×830:
  main, expanded, channels menu, live search with `<mark>` spans, overflow
  menu, new-entries pill.
- `Capture/workspace-*.png` — the same panel registered into the genuine
  upstream editor workspace at 1920×1080: all panels, Log Stream active,
  expanded row, Log Stream alone, expansion notch open, verbosity and
  channels menus (opened by scripted real mouse input).

These are CPU-rasterised frames of the production translation units — not an
OS window, GPU presentation or GLFW input. The native Windows opened-window
proof remains the owner's gate for the SDK phase.

## Outliner addendum (2026-10-22, Phase 2 native panel)

The demo's copied shell (`Projects/Project-VehicleHmiDemo/Reference/index.html`)
contains the Outliner's identity only: `<aside id="outliner" class="column
editor-column">`, its `.column-tab` labelled `Outliner`, the hide affordance
`×` titled `Hide the editor columns (P)`, and an **empty** `#outliner-body`
the demo fills from JavaScript at runtime (the JS/CSS were never copied, by the
owner's rule). So what is 1:1 here: the panel's existence in the editor
columns, its caption, its host behaviour (dockable/closable column), and its
role (the scene-object list). There are no demo pixels, row markup or CSS to
copy inside the body — the rows are drawn in the settled Frontier visual
language with the §10.2 vocabulary:

| Native Outliner (SceneDirectoryPanel) | Source of truth |
|---|---|
| Panel caption `Outliner`, dockspace tab + close | demo `.column-tab`; upstream `PanelDockspace` owns frame/tab/hide |
| Stack head: stack-glyph tile, `Outliner`, `N objects` pill, round `+` | kit `StackRailHead` (Log Stream head grammar) |
| `+` → spawn menu (`SPAWN RECORD`, one row per classification) | upstream `DropdownControlSurface` (owner's reuse exception) |
| Classification chips with live counts (`All N`, `Vehicle 1`, `Tyre 4`, …) | `CategoryChip` pattern from the Log Stream set |
| Search pill `Filter objects…` with `<n> shown` + clear disc | Log Stream `SearchBehaviour` |
| Tree rows: chevron (fold), classification dot, name, classification meta, eye (visibility) | kit `TreeRow` over the `SceneDirectory` hierarchy; rows mirror record parentage (roots when unparented) |
| Selection → identity card (name, classification, children, parent, token) | kit `IdentityHeader` |
| Foot: `N shown · M total`, `K hidden` / `Selected #t` | kit `StackFoot` |

Host contract: records appear by construction (`RecordEntry` ctor/dtor,
Editor/Development only); the panel never invents objects — spawn is a request
the host services by creating a record. Captures: `Capture/workspace-outliner.png`,
`Capture/workspace-outliner-spawn.png`, `Capture/workspace-studio-outliner.png`
(CPU-rasterised from the production draw code; same honesty note as above).
Known cosmetic delta: with many rows the child scrollbar slightly overlaps the
eye column (upstream scrollbar metrics) — fix candidates noted for the design pass.
