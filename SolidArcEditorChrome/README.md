# SolidArc editor chrome

Patch against `unassignedinbox/Slate` @ `85034e3` (branch `arena/01a0f758-slate`):
`git am SolidArcEditorChrome/0001-SolidArc-editor-chrome.patch`

- `Engine/Editor/EditorStyleSpecification.h` (new): the game editor's tab sheet, geometry and colour tokens in one place.
- `EditorHost::ApplyTheme` and `SolidArcEditorHost::ApplyTheme` both call `SeatEditorStyle`.
- `SolidArcEditorHost`: Control Centre notch (`SeatShade`, `TickShade`, `ShadeCoversPointer`, notch drawn at the end of `Record`).
- `SolidArcEditorProof.cpp`: new self-checking gates (tab tint, no blue, trapezoid slant, notch, pull/close).

Proof images and log: `TabBandComparison.png`, `SolidArcEditor.png`, `SolidArcEditorControlCentre.png`,
`ProjectZeroEditor.png`, `SolidArcEditorChromeProof.txt`.
