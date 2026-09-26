# Native main-editor integration — first verified pass

Status: **binding integration implemented; full task not yet complete**. Sun and Moon artwork is now rendered using source-verified browser bakes (see `Docs/CelestialOutlinerIcons.md`). This is not a full application/GPU or Windows certification.

## Implemented

`Tools/Build/Patches/MainEditorIntegration.patch` applies after the existing Camera patch to the immutable target reconstruction. `Projects/Project-Zero/Source/EditorInspectorSequence.h` is the shared project-owned binding used by GameExecution and the new full-host proof.

- EditorHost obtains the sheet **after** Outliner records the current selection, then commits immediately **after** Inspector records. It no longer renders the previous selection's sheet or relies on another frame to commit it.
- Engine exchange callbacks carry no project semantics; RenderScheduler forwards them to the project binding.
- Stable row/sheet identities preserve inspector ownership across reordering. Feed lookup uses the original source identity, not the current physical position or editable name.
- The completed environment routes and Camera run through the actual EditorHost, OutlinerPanel and InspectorPanel, not a standalone inspector window.
- Main Camera edits update real projection and request accumulation invalidation. Cine remains an independent, inactive study. Aperture/focus remain diagnostic, not rendered DOF.
- Stars' switch writes back to the outliner eye. Ancestor visibility gates effective celestial visibility without destroying child author visibility. Precipitation inherits its Clouds parent gate.
- The project requests a two-pane Outliner/Inspector workspace; the engine retains its normal three-pane option.
- Environment folder uses the approved environment artwork; Atmosphere uses sky-scattering artwork; both camera rows explicitly use the dark Camera artwork. Existing completed row mappings are retained.
- No Water/Fluids or deferred object/material inspectors added. Previous Camera/Weather drawing corrections are unchanged.

## Verification actually run

`Exhibits/Workbench/MainEditor/NativeIntegrationProof.cpp` and `RunIntegrationProof.py` build the real native editor host and project bindings against patched ImGui, with the actual ThorVG-backed icon adapter.

| Mode | Result |
|---|---|
| Release | 46 checks PASS; 256 KiB Linux stack limit |
| Debug | 46 checks PASS; 256 KiB Linux stack limit |
| ASan + UBSan | 46 checks PASS; sanitizer default stack |
| GameExecution development syntax | PASS |
| GameExecution shipping syntax | PASS |
| Integration patch apply-check against saved pre-integration sources | PASS |

Checks include immediate selection of every completed environment inspector, Camera input through the real host, independent Cine, Camera edits after row reordering, Stars switch/eye synchronization, parent hide/restore, Clouds/Precipitation inheritance, Wind speed/Cloud density/Precipitation rate write-back, no viewport window, and native CPU draw capture.

The proof programmatically selects through EditorHost; it does **not** claim mouse-driven outliner drag-and-drop coverage. Reordering is tested by moving real roster entries. Camera slider input is driven through native ImGui pointer events.

Measured GCC stack records and exact commands/source hashes are in `Exhibits/Gallery/MainEditorNative/`. Host Record frames: Release 96 B, Debug 160 B, Sanitized 384 B. Binding Update: 112 B, 96 B, 144 B respectively. The linked ThorVG dependency is a Release static archive, not sanitizer-instrumented. No Windows stack claim. The old lexical/allocation gates and the entire earlier inspector regression chain were not rerun as part of these 46 checks.

`MainEditor-Camera.png` is unretouched CPU rendering of the real host. **It contains known icon diagnostic placeholders and is not visual acceptance.**

## Remaining blockers — do not mark the request complete

1. **Approved artwork compatibility:** runtime reports Camera, Environment folder, Stars, Sky scattering, Wind, umbrella Precipitation, Rainbow and Lens Flare ready. Clouds, Local Cloud and Local Fog are rejected by the strict SVG backend due to filter primitives including turbulence, displacement and compositing. These remain visibly diagnostic; no filter stripping or substitute artwork is being passed off as approved. A faithful native rendering/baking path is still required.
2. **Global fog artwork:** Height Fog and Atmospheric Fog retain their existing fallback glyphs. The rejected blurred-bank `fog.svg` was deliberately not installed. There is no approved global-fog replacement established by this work.
3. **Remaining consumer visibility audit:** row/ancestor synchronization now works, but not every render consumer honors every eye. In particular, Wind's effective visibility is not yet used to mute its simulation field, and Atmosphere/Sky visibility needs consistent CPU/GPU consumer treatment. Do not infer that every downstream eye now works from the binding tests.
4. **Full deployment/rendering:** GameExecution compiles in syntax-only checks. No whole-app link/launch, Vulkan execution, Windows execution, persistence, or exhaustive downstream property certification is claimed.
5. **Broader roster semantics:** stable identities now protect inspector ownership. Other pre-existing scene/physics reorder consumers remain outside this patch's certification.

## Reproduction

From the repository root, with the pinned dependencies available:

```sh
python3 Exhibits/Workbench/MainEditor/RunIntegrationProof.py
python3 Exhibits/Workbench/MainEditor/RunIntegrationProof.py --reuse-target --debug
python3 Exhibits/Workbench/MainEditor/RunIntegrationProof.py --reuse-target --sanitize
```

The first command reconstructs the completed Camera baseline, applies the integration patch and copies the shared binding. Subsequent modes reuse those sources but rebuild their own objects. `--reuse-objects` is a manual incremental-development escape hatch only: it does not track header dependencies and must not be used after arbitrary source changes without invalidating affected objects.

The inherited README and unrelated older deliveries are excluded from this scoped change.
