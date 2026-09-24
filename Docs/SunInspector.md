# Sun inspector — rejected first layout

**Superseded:** the user rejected this visual layout. It is retained as historical work and stack/property evidence, not as the approved design. See `SunReferenceRestart.md` for the source-driven restart.

## Implemented

Apply `Tools/Build/Patches/SunInspector.patch` **after** `NativeOutliner.patch` to pinned C++ target `f6c99702c769ef9fd44c39ac8819e0ebeae35b9b`. The patch updates the actual InspectorPanel and CelestialSequence, not the web mock-up. Original target sources remain untouched; the build uses `.cache/cpp-sun`.

Sun now has six scrollable, collapsible cards:

1. **Bake / image** — honest capability notice. No Sun-specific bake or imported-image path exists in the current properties. It does not toggle Sky's bake under a different name.
2. **Sun Disk** — angular diameter and native RGB tint.
3. **Sunlight gain** — intensity and direct-light multipliers, explicitly **not lux**. Colour temperature is not invented.
4. **Day cycle** — a full 24-hour local-clock illustration, native Local Hours slider, Animate switch, and speed selector. The dial is informational, not a pretend manual azimuth/elevation control.
5. **Observer / date** — latitude, longitude, day and month remain available even though the reference cards did not include all of them.
6. **Solved direction** — elevation, azimuth, declination and equation of time remain read-only solver output.

All **15 original Sun properties** survive, with their original labels, ranges and write-back semantics. Labels sit above controls in Sun cards so long names do not collide with the native numeric pill. Captions wrap to card width. Other inspectors keep the old layout by default. Selection header, instance controls, notes and footer remain native; this first pass does not claim the complete reference visual redesign.

The sliders are the **unchanged C++ `ControlPanel::SliderPill`**, including its type-in cell, knob and track. No HTML slider code was ported. No project type is included by the engine inspector. Two opt-in presentation flags and a caption cross the existing property-group seam; the sheet still has six groups and ten properties per group.

## Executed proof

`Exhibits/Workbench/SunInspector/SunProof.cpp` instantiates the real project sequence, calls its real `BuildSheet`, `ApplySheet` and `Tick`, and records the real native InspectorPanel. It does not substitute a mirror Sun sheet.

Passed in release, debug, and ASan/UBSan with leak detection:

- all 15 properties present;
- eight native scalar property round trips;
- RGB tint, animation and speed round trips;
- an actual mouse click on `SliderPill` changes angular diameter and writes back through ApplySheet;
- solver direction remains read-only;
- Stars retains its default layout flags;
- three actual ImGui CPU-rendered inspector screenshots (scroll positions).

The screenshots use the default proof font and edited test values, not the complete application's typography/theme or a GPU rendering run. The proof does not test every native widget interaction, keyboard path, panel width or full application integration. No Vulkan or Windows application was executed.

## Stack measurements and corrective work

Measured with GCC `-fstack-usage`, not estimated from source. Sizes below are **individual function frames**, not summed runtime high-water marks:

| Function | Original debug `-O0` | Sun debug `-O0` | Original release `-O2` | Sun release `-O2` |
|---|---:|---:|---:|---:|
| BuildSheet | 49,888 B | 112 B | 16,736 B | 16 B |
| BuildSunSheet (new) | — | 4,240 B | — | 400 B |
| InspectorPanel::RecordCard | 320 B | 432 B | 208 B | 240 B |
| InspectorPanel::Record | 112 B | 112 B | 96 B | 96 B |
| ApplySheet | 176 B | 176 B | 128 B | 128 B |

The initial check caught a **33,472-byte debug frame even after removing the whole-sheet temporary**: numerous returned property temporaries in the multi-entity switch still occupied the frame. Sun now has a separate builder. The small dispatcher clears properties in place, and OpenGroup no longer constructs another entire group temporary. There are no per-frame heap allocations added to card drawing.

The proof owns its property sheets and sequence on the heap. `sizeof(EditorSheet)` is **17,284 bytes**, an increase of 792 bytes for the six caption/flag sets; group/property capacities were not increased. The production caller still owns its existing sheet; this patch does not relocate all application locals.

Both the **full debug and release focused proof executables pass with a 256 KiB Linux stack limit**. The script enforces an 8 KiB per-function ceiling for the measured Sun builder/dispatcher, apply path and inspector methods. Sanitizers run separately with the normal stack allowance, since their stack overhead is not comparable.

### Remaining stack limits

- The non-Sun builder still measures **29,264 bytes in debug**, 512 bytes optimized. It is deliberately reported, not hidden or claimed fixed. Subsequent entity migrations should split that path too.
- A 256 KiB Linux run is a useful conservative regression guard, **not proof of Windows/MSVC stack safety**. Compiler frames, CRT calls, exception handling and caller depth differ.
- No explicit `/STACK` setting was found in the inspected CMake and Project-Zero build scripts. The actual Windows executable's PE stack reserve/commit values have **not** been measured here. Windows follow-up must inspect the PE header (for example `dumpbin /headers`), check main and worker-thread reserves, and run this proof under the Windows toolchain. Do not solve this by merely increasing the linker stack size.
- The entire GameExecution/renderer call stack was not compiled or measured in this focused proof. Passing here is not a blanket full-application stack certification.

## Reproduce

```sh
python3 Exhibits/Workbench/IconArt/PrepareTarget.py
python3 Exhibits/Workbench/SunInspector/RunProof.py
python3 Exhibits/Workbench/SunInspector/RunProof.py --debug
python3 Exhibits/Workbench/SunInspector/RunProof.py --sanitize
```

The preparation helper now fails closed on unsafe archive members on older Python 3.11 versions that lack tar extraction filters. No system Python upgrade is needed.

Proof images, commands, hashes, stack measurements and exit codes are in `Exhibits/Gallery/SunInspector/`. The viewer is only an artifact viewer, not a live interactive native editor.
