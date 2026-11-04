# Native authoring resumption — verification

HTML retesting passed, and the first modular C++ increment is implemented and verified.
**This is not a completed native Pattern/Stamp workspace port.**

## Results

| Check | Observed | Boundary |
| --- | --- | --- |
| Pattern/Stamp HTML regression | 57/57 passed, 5.5 min | Selected current interactions and raster tests |
| Preserved painting/document/Stamp checks | 48/48 passed | Separate model specifications |
| HTML production build | Passed, 12.39 s | Existing bundle-size warning |
| C++ source contracts | 2,269 assertions passed | GCC 12.2, strict warnings |
| Address/undefined sanitizers | 2,269 repeated assertions passed | Leak detection enabled |
| Native pointer/control regression | 42 assertions passed | Actual CPU ImGui input/render capture |
| Slang/Vulkan readbacks | 54 assertions, 13 dispatches | SwiftShader software device, not hardware |
| Windows x64 headless cross-link | Passed; PE signature/machine verified | Zig 0.13.0; executable not run |
| Full Windows app / Windows runtime | Not run | No updated TextureWorkspace download |

## Shared C++ implementation

```text
FrontierArtwork (Engine; no ImGui dependency)
  ArtworkArrayPlacement       -> neutral layouts and five scale profiles
  ArtworkAuthoringProperties  -> neutral channels, changed-field patches and enclosures
  ArtworkEditing              -> retained-vector copies, nested scope and protected edits
  VectorArtworkDocument       -> source-only TOML and bounded undo/redo
               |
               v
Existing shared Slang/Vulkan coverage -> actual generated array/mask pixels
```

- Arrays retain editable Boolean/cubic sources, owned masks and independent placement enclosures.
- Group visibility/protection and default-off group-wide channel patches are validated and serialized.
- Fixed locked-mask pointer editing, no-op history erasing redo and history workspace-contract changes.
- Fixed wrong-typed/unknown TOML properties silently defaulting/disappearing.
- Fixed loss of small unselected coordinates caused by an unnecessary identity transformation.
- Neutral placement/channel APIs can be reused by Texture Painting without a vector document or editor UI.

## Actual evidence

![Native Slang/Vulkan array readback](../Images/GpuNativeArtworkArray.png)

The image above is a native compute readback of three independently editable Boolean motifs.
It is not an application window or a browser mockup. See also
`Images/GpuNativeArtworkMasks.png` and `Images/NativeArtworkLockedControls.png`.

Detailed commands, toolchain, source SHA-256 and execution boundaries:
`Results/NativeArtworkEditingChecks.json`. Reproduction:
`Code/VerifyArtworkEditing.py`; instructions are in the TextureStudio exhibit README.

## Remaining acceptance gates

- Hosted 1:1 Pattern and Stamp UI, preserving original text/image/vector/PBR tools and docking.
- Native channel rendering, height/normal generation, imported normal maps and map inspection/export.
- Painted/image masks and GPU texture residence; stored groups and corresponding-feature linked editing.
- Native Texture Painting integration, full Unicode caption folding and text/image placement parity.
- Deferred material detail/performance fixes requiring hardware profiling and actual Windows review.
- Full application build/run and separately authorised scan/publication; no commit, push or Actions run.

Channel contributions are currently validated authored properties, **not completed native PBR evaluation**.
Existing native budgets remain: 128 GPU instructions, 24 Stamp/64 Pattern layers, 64 enclosures,
12 nesting tiers, 80 history edits and 600 KB TOML. Complex arrays can reach the instruction budget first.
The full application test suite and full CMake/application build were not run in this increment.
