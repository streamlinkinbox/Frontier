# SolidArc

SolidArc is the C++ CAD/modelling tool ported from `streamlinkinbox/Frontier` branch `arena/01a0b989-frontier`.

Current placement:

```text
Editor/AuthoringTools/Modelling/SolidArc
```

This keeps it in the editor authoring-tool tree beside future `TexturePainting`, `Baking`, and `Terrain` tools, instead of mixing it into the runtime `Engine/Editor` panel code.

This import contains the C++ side of the tool:

- kernel NURBS/B-rep modelling code,
- document/undo model,
- console command host,
- interaction and gizmo helpers,
- software-raster presentation proof path,
- shared-editor outliner, viewport and inspector adapters,
- optional ImGui editor shell,
- C++ verification sources,
- lightweight `.arc` sample construction journals for ToyCar, ToySailboat and ToyBiplane testing.

The SolidArc editor shell reuses the engine editor panels while presenting SolidArc-specific content:

- `OutlinerPanel` is fed CAD folders matching the live HTML panel: `Sketches`, `Bodies`, `Surfaces`, `Construction`, `Dimensions`, `Constraints`, with SolidArc-only filter labels (`Lines`, `Profiles`, `Bodies`, `Surfaces`, `Construction`, `Dimensions`) and the exact CAD category colours from the web editor (`#4fd8e0`, `#ffb454`, `#4da3ff`, `#b48cff`, `#e5d33a`).
- `ViewportPanel` switches to SolidArc chrome with `Construct`, `Body/Face/Edge/Vertex`, `Wire/Flat/Plastic/Matcap`, `Move/Rotate/Scale`, and view controls.
- `InspectorPanel` receives a CAD sheet with identity, B-rep/NURBS geometry, bounds, parametric blueprint slots, sub-selection counts, and display controls.

The upstream browser HTML panel and PNG proof artifacts are still not built into the runtime; the native editor reproduces the relevant controls in ImGui.

## Sample scripts

Native `.arc` construction journals live under:

```text
Editor/AuthoringTools/Modelling/SolidArc/Samples/
```

They can be replayed with the standalone console build:

```bash
SolidArc --continue Editor/AuthoringTools/Modelling/SolidArc/Samples/ToyCar.arc
SolidArc --continue Editor/AuthoringTools/Modelling/SolidArc/Samples/ToySailboat.arc
SolidArc --continue Editor/AuthoringTools/Modelling/SolidArc/Samples/ToyBiplane.arc
```

## Standalone build

```bash
cmake -S Editor/AuthoringTools/Modelling/SolidArc -B /tmp/solidarc-build
cmake --build /tmp/solidarc-build --target SolidArc
```

To compile the C++ verification targets too:

```bash
cmake --build /tmp/solidarc-build --target SolidArcVerification
ctest --test-dir /tmp/solidarc-build --output-on-failure
```

The dependency-free repository gate is:

```bash
Tools/Build/CheckSolidArc.sh
```

It compiles and links the console target with `g++`, checks the CAD outliner/inspector adapter, verifies the `.arc` samples are present, then compile-checks every C++ verification translation unit. This is the fallback gate for sandboxes that do not have CMake installed.

High-fidelity editor visual proofs are checked with:

```bash
Tools/Build/CheckEditorVisualProofs.sh
```

That gate uses the same headless ImGui draw-list raster path as `Exhibits/Workbench/Editor/EditorProof.cpp`; it verifies the canonical game editor proof at `Exhibits/Gallery/Editor/EditorProof_Inspector.png` and regenerates the SolidArc editor proof at `Exhibits/Gallery/Editor/EditorProof_SolidArc.png`. It does not emit SVG or simplified mockup boards.
