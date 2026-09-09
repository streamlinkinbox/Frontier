# Patches — Slate's divergence from vendored ImGui

Three patches, all against `ExternalPackages/imgui` on the **`docking`** branch at `83f6686` (`1.93.0 WIP`).

⚠️ The pin was previously `12b7977`, which this document described as being on the `docking` branch. It was
not — it is an `ocornut/master` commit, and it is an ancestor of `docking` only because that branch merges
master periodically. `ImGuiConfigFlags_DockingEnable` does not exist at that commit, so docking could not be
enabled at all. `.gitmodules` now declares `branch = docking` so the pin cannot drift back to master
unnoticed.
They are the **whole** of Slate's divergence from upstream ImGui. `Scripts/ApplyImGuiPatches.ps1` applies
them on Windows and `Scripts/ApplyImGuiPatches.py` on Linux and macOS.

Both build systems invoke the script themselves, immediately after `git submodule update` and before any unit
is translated: `Projects/Project-Zero/Build/ToolchainSequence.ps1` on Windows, and `CMakeLists.txt` at
configure time elsewhere. That ordering is not cosmetic — `git submodule update` restores the vendored tree to
its pinned commit and silently discards the patches, which is exactly how these once disappeared. Applying
inside the build means the two steps cannot be run out of order.

🔴 Never edit `ExternalPackages/` by hand. A silently adjusted vendored dependency is a defect that
reproduces on one machine only — `14` §2. The divergence lives here, as a file a reader can open.

## Why patch rather than write a docking system

`References/DockWorkspace.html` wants trapezoidal, interlocking tabs. Everything else it does —
splitters, dock trees, five-way drop targets, floating windows, layout persistence, tab reordering — is
already in ImGui's docking branch and is the hard, boring 95 % of a docking system. The divergence
needed to reach the sheet's appearance is 120 lines across four files, because ImGui already separates
tab *shape* from tab *behaviour*: `ItemAdd()` and every drag path hit-test the rectangular `bb`, exactly
as the sheet's SVG is `pointer-events:none` over a rectangular `<div>`.

## The patches

| Patch | Adds | Touches |
|-------|------|---------|
| `PatchA-TrapezoidalTabs.patch`  | `TabSlant` | `imgui.h`, `imgui.cpp`, `imgui_widgets.cpp` |

| `PatchB-TabOverlapZOrder.patch` | `TabOverlap`, `TabHeight`, `TabStripPadTop` | all four files |

`A` is independently applicable. `B` stacks on `A` and cannot apply first — its context includes A's
lines.

### A — the trapezoid

Replaces the body of `TabItemBackground()`, the one function that paints tab geometry, reached from four
call sites. The sheet's `tabPath(w,h)` states `slant = min(14, w * 0.16)` over the outline
`[0,h] [slant,0] [w-slant,0] [w,h]`, upper corners rounded, lower corners square. The sheet's
`roundedPath()` insets along both incident edges and joins the two inset points with a quadratic through
the corner — which is `PathBezierQuadraticCurveTo` exactly, so the curve is the browser's own and not an
approximation of it.

### B — interlock, height, strip, and z-order

- **Overlap** — one term subtracted from the tab advance in `TabBarLayout`, reproducing the sheet's
  `margin-right:-24px`. Every width and hit rectangle still reads `tab->Width`, which is unchanged.
- **Height and strip** — `TabHeight` overrides the derived bar height; `TabStripPadTop` reserves the
  band above the tabs that the sheet's 28 px strip shows over a 24 px tab seated at `flex-end`.
- **Z-order** — overlapping tabs are submitted leading-to-trailing, so a selected tab is buried under
  every tab after it. An `ImDrawListSplitter` on the tab bar gives two channels: quiet tabs record into
  the lower, the selected or hovered tab into the upper, merged at `EndTabBar`. This is the mechanism
  ImGui already uses for tables. **The splitter opens only when `TabOverlap > 0`**, so a stock
  configuration records the identical command stream it always did.

## Every member defaults to zero

`TabSlant`, `TabOverlap`, `TabHeight` and `TabStripPadTop` all default to `0.0f`, and each guards its
own behaviour behind `> 0.0f`. With default style the patched build emits a **byte-identical** command
stream to unpatched ImGui — verified by rasterising both and comparing the images. All four are carried
by `ImGuiStyle::ScaleAllSizes`, so a slant fixed at 14 is not half as steep on a 2× display.

## The sheet's own figures

| Style member | Sheet | Source |
|--------------|-------|--------|
| `TabSlant`         | `14.0f` | `slant = Math.min(14, w * 0.16)` |
| `TabOverlap`       | `24.0f` | `.tab { margin-right: -24px }` |
| `TabHeight`        | `24.0f` | `.tab { height: 24px }` |
| `TabStripPadTop`   | `4.0f`  | `.tabstrip { height: 28px }` at `align-items: flex-end` |
| `TabRounding`      | `0.0f`  | the sheet's `roundCorners` is off by default |
| `TabBorderSize`    | `0.0f`  | no border requested |
| `FramePadding.x`   | `38.0f` | `.tab { padding: 0 38px }` |
| `TabMinWidthShrink`| `170.0f`| `.tab { min-width: 170px }` |

⚠️ `TabOverlap` and `FramePadding.x` are coupled. The sheet's 38 px horizontal padding exists to clear
slant plus overlap; raising the overlap without raising the padding runs adjacent labels together.

## Upgrading ImGui

`ApplyImGuiPatches.ps1` reports when the submodule stands at a commit other than `12b7977` rather than
asserting, because a deliberate upgrade should reach a message naming both commits. After an upgrade,
re-cut both patches against the new tree and re-check `TabItemBackground`, `TabBarLayout` and
`BeginTabBar` — those three are the only functions either patch touches.

```powershell
powershell -File Scripts\ApplyImGuiPatches.ps1            # apply, idempotent
powershell -File Scripts\ApplyImGuiPatches.ps1 -Verify    # report which stand applied
powershell -File Scripts\ApplyImGuiPatches.ps1 -Revert    # restore pristine, in reverse order
```

🔴 Application is detected by the `SLATE PATCH A` / `SLATE PATCH B` sentinels, **not** by
`git apply --reverse --check`. B edits lines inside A's context, so once B is applied A no longer
reverse-checks — a reverse-check would report A absent on a fully patched tree and re-apply it, aborting
a build whose tree was perfectly healthy.


## PatchC — round tab buttons

`DockWorkspace.html` draws its close affordance and its `addBtn` as **discs**. ImGui draws both as
rectangles: `CloseButton` fills a square hover mark, and a tab-bar button is a full-height slab.

`Style.TabButtonRounding` is a **fraction** of the control's own extent, not a length — so it is the one
Slate style member `ScaleAllSizes` must NOT scale. `0.0f` restores the vendor's rectangles exactly, which
is what keeps a patched build with defaults byte-identical to an unpatched one.
