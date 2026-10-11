# Native Camera inspector

## Delivered scope

A native C++/ImGui Camera inspector for the existing Main Camera and Cine Camera rows, integrated after Weather on immutable target `f6c99702c769ef9fd44c39ac8819e0ebeae35b9b`.

- Sensor / horizontal view-angle schematic instead of an iris in the sensor card.
- Main Camera focal length and sensor-width controls write through the project feed to `CameraProjection::AssignFieldOfView`. Generated camera rays change, and the existing renderer reads this same camera's FOV/aspect into dispatch constants.
- Linked aperture study: entrance pupil, subject framing, hyperfocal distance and acceptable-sharpness limits.
- Draggable, keyboard-adjustable **Subject plane + sharpness** card replaces the plain Focus Distance presentation. Its distance axis is logarithmic, 1–100 m.
- Actual viewport aspect for Main Camera: sensor height = sensor width / aspect. The inspector does not always claim a 36 × 24 mm frame. The inactive Cine study uses a 3:2 reference.
- Narrow layout, scrolling and framebuffer-scale captures.

Water Bodies / Fluids are untouched.

## Audit findings and explicit limitations

The target's camera is **pinhole**. It has a real vertical FOV and aspect ratio, but no rendered depth-of-field path, physical-aperture exposure, autofocus or lens-image baking. The old Cine Camera sheet exposed constant placeholder focal/aperture/focus/ISO values with no write-back. Those placeholders are replaced, not presented as functioning renderer controls.

Main Camera lens edits affect projection. Aperture and subject distance are project-owned, session-only **optical study settings**: they update diagnostics, not blur or exposure. The existing Cine Camera row is explicitly an independent, inactive lens study; editing it cannot change the live camera or switch the viewport. Unsupported ISO/shutter placeholders are not shown. Imported file-camera rows retain their existing transform-only inspection; file-camera activation/editing is outside this delivery.

No camera serialization, persistence across application launches, clipping-plane editing, GPU execution, full application link/run or Windows/MSVC verification is claimed. The main application translation unit passes syntax checking, while the actual feed/projection/native panel are compiled, linked and exercised headlessly. Shader files are unchanged; no new shader-compilation result is claimed here.

## Ownership and integration

- `Engine/DisplayPresentation/CameraOptics.h`: shared thin-lens diagnostic math. Millimetres internally, metres for subject/limits, reference circle of confusion 0.03 mm; safe infinity for the far limit beyond hyperfocal.
- `Projects/Project-Zero/Source/CameraInspectorBinding.h`: project-side sheet construction and finite/clamped write-back. Main focal length is derived from the current projection, so external projection/aspect changes are reflected when the sheet refreshes.
- `Engine/Editor/CameraInspectorPanel.{h,cpp}`: native controls and vector drawings; no project-state ownership or viewport implementation.
- `Tools/Build/Patches/CameraInspector.patch`: CMake, appearance/routing, real `EditorFeedSequence` and `GameExecution` changes against the post-Weather staging tree.
- The shared reconstruction runner copies the new files and applies Camera after Weather.

The application tick applies camera-sheet edits and refreshes readouts. Main FOV edits explicitly reset renderer accumulation: the existing `ObserveCamera` only checked position, direction and viewport size, not FOV. This avoids blending a changed lens with old converged samples. Aperture/focus-only edits do not reset projection or pretend to affect pinhole output.

Ranges: focal 1–500 mm, sensor width 16–70 mm, aperture f/1.4–22, subject 1–100 m. Active vertical FOV is bounded to 1–175 degrees; at extreme lens/aspect combinations this projection bound is reflected in the focal readout. Sensor and ray diagrams are labelled schematic, not to scale. The aperture drawing now directly transcribes the HTML inspector’s eight-blade iris, 48 rim ticks and octagonal opening. The focus graphic includes its 17 sharp/blurred targets, dashed near/far limits, logarithmic metre labels and crosshair handle. The numeric pupil diameter and sharpness limits remain calculated from the actual optical-study values.

## Verification

```sh
# Only needed when the immutable cache is absent:
python3 Exhibits/Workbench/IconArt/PrepareTarget.py
# The CPU raster's transitive Vulkan headers use the existing v1.3.290 proof dependency.
python3 Exhibits/Workbench/Camera/RunNativePanel.py
python3 Exhibits/Workbench/Camera/RunNativePanel.py --debug
python3 Exhibits/Workbench/Camera/RunNativePanel.py --sanitize
python3 Exhibits/Workbench/Camera/ServeNativePanel.py
```

Run reconstructions sequentially: all modes share `.cache/cpp-sun-full`. `--reuse-build` assumes an already reconstructed, matching mode. Cache restoration for this run used the pinned target/dependencies and Vulkan-Headers commit `b379292b2ab6df5771ba9870d53cf8b2c9295daf`.

Results:

- **43 Camera checks passed** in Release, Debug and ASan/UBSan with leak detection.
- Actual `EditorFeedSequence` roster/routing/write-back, generated ray narrowing and unchanged central direction; aperture/focus do not change pinhole projection; independent Cine state; nonfinite rejection; reference FOV/pupil/near/far/hyperfocal math; native sliders, subject-plane pointer/keyboard, narrow/scroll/2× and teardown.
- Prior stages passed in all three modes: Sun 326, Lens 74, Atmosphere 62, Moon 146, Stars 157, Clouds 582, Fog 127, Weather 1684.
- `GameExecution.cpp` passes development-mode syntax checking with the real changed application tick.
- All three Camera source/capture manifests match. Native images are losslessly re-encoded, not retouched or browser recreations.

### Measured stack

GCC `-fstack-usage`, bytes per function; not a nested-call total:

| Entry | Release | Debug | ASan/UBSan |
| --- | ---: | ---: | ---: |
| Camera recorder | 768 | 1088 | 6432 |
| Dedicated camera sheet builder | 32 | 208 | 848 |
| Optical write-back | 144 | 128 | 624 |
| Feed camera write-back | 80 | 112 | 336 |

Selected recorded functions stay below the 8 KiB gate. Release/Debug run with a 256 KiB Linux process stack; sanitizers use their default stack. This is not Windows certification.

## Preview and source state

`Exhibits/Gallery/CameraNative/index.html` is a read-only viewer for actual native captures, served on port **5186**. Commands, proof logs, stack figures, hashes and application syntax results accompany it.

At the start of this task the workspace retained the earlier native source/artifacts, but local Git history was back at `68f3016`, and those earlier files were untracked. The camera work does not claim to have recovered the earlier commits. It uses that existing integration chain and leaves unrelated inherited files and README edits outside the camera commit.
