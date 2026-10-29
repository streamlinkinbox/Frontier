# Native Notification Cards, Notification Stream and Confirmations

Saved/applied preference feedback now appears in a dismissible **top-right card**, not at the left or inside
PanelExpansionTab. **Quick Controls → Notification Stream** retains the session history, newest first. It is split
beside the quick-control tiles on wide screens and stacked beneath them on narrow screens. Telemetry & Notifications
contains preference switches and hold duration only; no stream is placed there.

## Production contract

| Concern             | Implementation                                                         |
|---------------------|------------------------------------------------------------------------|
| Reusable storage    | Engine/Interface/NotificationSequence.{h,cpp}.                          |
| Shared presentation | NotificationCard, ControlKit, PixelSpace and inherited VectorCodec SVGs.|
| Visual source       | Supplied aa658ed8 light/dark cards; pinned in SpaceFormats results.     |
| Actual events       | Appearance/input/notification apply or save; validation/write failures.|
| Capacity            | 64 history entries, 16 pending alerts, at most three visible.           |
| Overflow            | Oldest history/pending entry is removed at its respective capacity.    |
| Expiry              | Applied hold preference; queued/hidden entries do not age until visible.|
| Hover/gesture       | Hover or held card gesture pauses expiry. Matching X press/release only.|
| Input ownership     | Card owns pointer input; dismissal cannot operate the drawer beneath. |
| History             | Expired/dismissed/suppressed events remain; Clear history is independent.|
| Preferences         | Existing projectspace success/error switches and hold duration reused.|
| Persistence         | Preferences persist; notification messages/history are session-only.  |

The quick Notifications toggle clears pending cards but does not erase history. Suppression affects presentation,
not history. A card avoids the shared top tab when their bounds would intersect. Extremely small viewports that
cannot contain a card retain it pending rather than drawing it offscreen. The FPS overlay moves below the stack.
Text storage is bounded (160 title bytes, 2048 description bytes) without splitting valid UTF-8 code points.
The card displays one title line and up to four description lines; longer text is ellipsized.
No Undo/Retry/Render/Delete button is invented: genuine notification action variants and severe-error UI remain pending.
This UI cannot guarantee display after an OS/driver/PC hard lockup.

## Reproduction and evidence

Prerequisites: repository dependencies/fonts prepared, configured host verification builds, and a Windows
cross-toolchain.
Use the existing full driver; it runs eleven tests in Debug and ASan/UBSan, exports 36 native CPU PNGs, then
cross-builds/packages/inspects the Windows x64 exhibit:

```sh
python Exhibits/ControlCentre/Code/ValidateNavigation.py
```

Focused execution after building:

```sh
cmake --build _ClaudeScratch/build/ui --target NotificationValidation
ctest --test-dir _ClaudeScratch/build/ui -R NotificationValidation --output-on-failure
```

`Code/NotificationValidation.cpp` exercises the production implementation, not an independent browser approximation.
It checks queue/history limits, expiry/hover/suppression, invalid time/duration, UTF-8 boundaries, stale-token refusal,
real saves and refused writes, close press/release isolation, native Clear history, responsive stream geometry, and
375×700 at 125% scale. Shared geometry tests verify 200×18 logical-pixel tabs in both directions; a native pixel
assertion verifies that Invert Y-Axis has no surrounding filled field.

| Native capture             | Meaning                                                     |
|----------------------------|-------------------------------------------------------------|
| Images/SavedTopRight.png    | Actual successful personal preference save.                 |
| Images/SaveError.png        | Actual replacement-file refusal; draft/applied bytes guarded.|
| Images/NotificationHistory.png | Actual success/error Notification Stream in Quick Controls.      |
| Images/NotificationNarrow.png  | Actual editor at 375×700 and 125% scale.                  |
| Images/CardsLight.png       | Explicitly labelled semantic fixtures, production renderer. |
| Images/CardsDark.png        | Explicitly labelled semantic fixtures, production renderer. |

All captures are authentic ImGui draw commands rasterized by the existing exhibit CPU rasterizer. The semantic card
examples
exercise information/warning presentation without claiming nonexistent operational events. They are not AI artwork.
Compiler/configuration, source hashes, commands and results are retained in
`../ControlCentre/Results/NavigationChecks.json`; detailed Debug/sanitizer logs accompany it. The working branch is
`arena/73e163a3-frontier`; results include uncommitted changes, not a new commit. Windows cross-build/package inspection
is not Windows runtime, GPU, MSVC, DPI or driver-failure execution. No Linux product package or Actions run is produced.

## Confirmation dialogues

Shared `Engine/Interface/ConfirmationDialogue.{h,cpp}` reuses the supplied rounded and semantic-header references,
ControlKit buttons, semantic colours, SVG glyphs and the applied fonts. Preference Save/Apply requests the rounded
**Apply / Cancel** modal; it does not write or change the applied profile before confirmation.

- Apply performs the existing scoped save and broadcasts the actual success/error result.
- Cancel/Escape leave drafts, applied profiles and saved bytes unchanged.
- Outside clicks do not dismiss the popup or operate the drawer; global drawer shortcuts are blocked while modal.
- Keyboard focus defaults to Cancel; Tab cycles the two actions and Enter activates the selected action.
- Detail text wraps in a bounded scroll region; ordinary toast cards do not cover the modal.
- The semantic-header variant is a labelled native fixture, not an invented error/retry/delete operation.

`Images/ConfirmApply.png` captures the real save confirmation, `Images/ConfirmSemantic.png` the second reference
variant fixture, and `Images/StreamNarrow.png` the settled stacked stream. These supplement the six images above
for nine notification/dialogue captures. Tests cover request-without-save, Cancel/file retention, outside-click
confinement, Escape, safe default Enter, Tab+Enter approval and real saved bytes.
