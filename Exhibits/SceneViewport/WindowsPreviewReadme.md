# Frontier editor preview — Windows x64

Extract the ZIP into a new folder. Keep `Frontier.exe`, `SolidArcWorkspace.exe`, `TextureWorkspace.exe`,
`EngineContent/` and `Projects/` together. Start `Frontier.exe`, then open Project Everest. Alternatively:

```text
Frontier.exe Projects\Everest\Everest.projectspace
```

`Package.json` identifies the source revision, file hashes, toolchain and omitted content. The small archive excludes
Texture Studio material archives; the scene viewport does not need them, and the Texture Workspace then starts with an
empty library (see below).

## Celestial bodies

- Moon controls now drive all four native sky slots: visibility, phase, placement, orientation, diameter, brightness
  and glow. The primary slot also controls moonlight at night.
- Use **Find in sky** to frame the selected body. **Place in view** switches off following and puts it above the
  horizon.
- Venus, Jupiter, Mars and Saturn each have their own interactive surface instruments and live native previews.
  Controls for orientation, surface appearance, atmospheric limbs and rings update the viewport.
- **Follow sky** uses the date and observer settings on the Sun. Editing manual placement switches following off.
- Stars' limiting magnitude, size, halo, rotation and scintillation controls now reach native rendering.
- Previews are isolated surface evaluations under fixed reference light; they are not copies of the scene camera.

## Cameras and viewport

| Input / mode | Action |
|--------------|--------|
| Camera dropdown | Orbit selected, Chase selected, Free/WASD, Top, Bottom, Front, Back, Left, Right, Space |
| Free / RMB + mouse | Look around |
| Free / RMB + W/A/S/D | Move forward/back/left/right |
| Free / E/Q, Shift | Up/down; speed boost |
| Free / RMB + wheel | Change flight speed |
| Orbit / middle or left drag | Rotate around the selected finite target |
| Orbit / Shift-middle | Pan |
| Orbit / Ctrl-middle or wheel | Dolly |
| Chase | Follow the active selected target; sky bodies provide directional tracking |
| F / focus button | Frame the selected target |
| Circular reset button | Restore the scene camera |
| V | Fill the application window with the viewport |
| Escape or V in fullscreen | Restore the docked panels |
| H | Toggle the detailed readout |

Axis views use fixed-axis perspective projection. Space navigation starts at 150 km, enables wide-range flight, and
restores the earlier scene position when leaving. It is not a new physical solar-system simulation.

The scene-name notch, old Scene/renderer/live text, particle-editor launcher, respawn/reassemble buttons and
Distance Slice debug view have been removed. Distance Slice is also removed from the native presentation shader.

Each panel owns its footer. The viewport footer reports FPS, render time, scene triangles and Sun elevation using
colour and status icons. Render time is not a separate GPU timestamp. No workspace-wide status footer remains.

## Outliner and settings

The Outliner shows the 15 scene/environment entries, the one **Render settings** component and the proving ground's
37 objects in eight groups (Course, Boundary, Cones, Hangar, Tunnel, Control tower, Smash alley, Lighting). The
Frontier brand/workspace banners and level badge are removed. Simulation Origin and unrendered scientific, fluid,
fracture and particle examples are not in the default scene. Their reusable engine code remains; regression examples
live in an exhibit fixture.

Select **Render settings** in the Outliner (or open **Control Centre → Quick Controls → Render settings**) to edit the
renderer. Its debug views include view 15, the analytic illumination. Save with **Ctrl+S** or **Save level**.

## Scene objects

- Select an object, for example **Boundary west** under Boundary, in the Outliner or by **clicking it in the
  viewport**. Its inspector has the heading with notes, Transform, Quick controls, Modifiers, Material and Fracture.
  **F**, Orbit selected and Chase selected use it as the camera's target.
- **Move, rotate and scale in the viewport:** with an object selected, **W** shows the move arrows (world X, Y and Z),
  **E** the rotation rings (the object's Z, Y and X angles) and **R** the scale handles (one axis, or the centre
  square for all three). Drag a handle with the left button. Hold **Ctrl** to snap (0.25 m, 15°, 0.1), and press
  **Esc** during a drag to put the object back. A click that does not move the camera selects what is under the
  pointer.
- **Picking:** the GPU draws the scene's triangles again around the clicked pixel and selects the object whose
  surface is there, so a click beside a large object or in a ring's hole no longer selects it. The ground selects
  the proving ground. Where the pixel shows the sky or the vehicle, the nearest object within 4 pixels is selected,
  and with none the selection stays as it was.
- **Add:** **Ctrl+A** or the Outliner's **+** opens **Construct**. Search or pick a category, choose an entity, name it,
  then press **Add to scene**. Under Geometry are the engine shapes Cube, Sphere, Cylinder, Cone, Torus and Plane, with
  scale and surface colour on the properties step. A new shape lands on the ground where the view looks. The other
  categories add the level's environment and weather records.
- **Row menu:** right-click an Outliner row for **Rename** (F2), **Hide**/**Show**, **Fracture** (not available
  yet), **Duplicate** (Ctrl+D) and **Remove** (Del). Removing one of the proving ground's objects hides it. A duplicate
  appears beside the original.
- **Transform:** drag a field to scrub or double-click to type (Alt for precision, Shift for speed). Every copy moves
  with the object, and the reset buttons return it to where it was placed.
- **Modifiers:** a repeated set is one object with an Array modifier stack, as in Blender. **Add modifier → Generate →
  Array** adds one. Each card folds, switches off in the viewport, moves up or down, or is removed. Count, relative
  offset, constant offset, rotation step and pivot (each copy, or the object's origin for a ring) re-lay the copies in
  the viewport. Copies beyond the scene archive's own come from a reserve of 512.
- **Quick controls:** Visible, Dynamic (relit every refresh), Cast shadows, GI and Locked act on the viewport. Physics
  is saved with the level and waits for the physics engine. Breakable objects carry Fracture settings.
- **Material:** pick the surface slot, then change the base colour, roughness, metallic, reflectance, clear coat,
  transmission or emission. The first change gives the object a surface of its own, and the other objects sharing the
  material keep theirs. **Revert to the shared family** drops it.
- **Limits:** the reserve holds at most 512 copies and constructed objects beyond the archive's own, and the inspector
  says when it runs out. At most 96 objects have surfaces of their own.
  Fracture does nothing yet.

## Three applications

| Application | Program | Opens |
|-------------|---------|-------|
| Game Engine | `Frontier.exe` | `.projectspace` game projects, for example Project Everest |
| SolidArc (modelling) | `SolidArcWorkspace.exe` | `.solidarc` models |
| Texture Workspace (texturing and baking) | `TextureWorkspace.exe` | a material library folder |

The Project browser in `Frontier.exe` reaches all three:

- **New**, the first card in the library, opens the New dialog. Choose **Game project**, **SolidArc model** or
  **Texture library**, give a name and a destination, then create it. A game project opens in the editor, a model
  opens in SolidArc and a library opens in the Texture Workspace. A library folder is created when it is missing.
- **Open from disk** lists `.projectspace` and `.solidarc` files, images and Markdown. The file opens in the reader
  with a preview. **Open in Frontier** or **Open in SolidArc** then starts the application for that file type.
- `Frontier.exe <project>.projectspace` opens the project in the editor. `Frontier.exe <model>.solidarc`, or a model
  dropped on `Frontier.exe`, hands the model to SolidArc.

Double-clicking a `.solidarc` without an installer makes Windows ask which app to use. Choose **More apps → Look for
another app on this PC**, pick `SolidArcWorkspace.exe` and tick **Always use this app to open .solidarc files**. The
model then opens directly. Earlier builds ignored the file named this way and showed an empty document unless they
were started with `--open`.

### SolidArc

- Start it directly (double-click), `SolidArcWorkspace.exe "C:\Parts\Bracket.solidarc"` or
  `SolidArcWorkspace.exe --open "C:\Parts\Bracket.solidarc"`. `--specimen` starts with the fillet and chamfer
  specimen. When a model cannot be read, the status line says why.
- Options are in `SolidArcWorkspace.toml`. Save writes `Documents/Bracket.solidarc` by default, the autosave goes to
  `Autosave/` and the log to `SolidArcTrace.log` (the **Log** button or **F12** opens it). `Samples/` holds the
  curve import sample.
- Navigation and commands follow Plasticity: orbit, pan and zoom on the mouse, **B** fillet, **E** extrude,
  **1**–**5** selection modes. Right-click or **Enter** confirms a command, **Esc** cancels it.
- The panels are Frontier's docking panels, and the sliders, switches and tabs are Frontier's controls.
- It needs a Vulkan 1.3 driver. It was cross-built like `Frontier.exe` and has not been started on Windows here;
  please report the first launch. The cross-built program carries no version details.

### Texture Workspace

- Start it from the New dialog, directly, or as `TextureWorkspace.exe --library "D:\Materials"`. Without
  `--library` it uses `%LOCALAPPDATA%\Frontier\TextureStudio\Materials`.
- The material presets (`EngineContent/MaterialArchives`, about 96 MB) are not in this ZIP because GitHub refuses
  files over 100 MB, so the library starts empty. Copy `EngineContent/MaterialArchives` from the repository into the
  extracted `EngineContent/` folder to get them.
- It was cross-built like `Frontier.exe` and has not been started on Windows here; please report the first launch.

## Local media, wind and baking

- Local Cloud and Local Volumetric Fog start disabled. Select one and **Place in front of camera**, or enable it.
  Both render natively. Local Cloud and Local Fog also have explicit creation-menu entries.
- Wind is a separate contributor under Force Field. Field vectors are evaluated velocity in m/s and are labelled as
  an X-ray overlay. No vegetation/tree-bending consumer is registered in Everest.
- Following media advect their density through fixed bounds. Stopping or unlinking retains accumulated displacement.
- Fog supports box/ellipsoid/sphere/hemisphere bounds, falloff, noise, self-shadowing and adjustable march samples.
  Cloud trace resolution and 16/32/64/128-style sample budgets affect actual rendering.
- **Atmosphere → Baking → Bake atmosphere** generates real GPU lookups and shows their input snapshot.
  **Use baked settings** freezes those optical coefficients; rebake to replace them or turn it off for live editing.
  Coefficients persist with the level; GPU images regenerate from them on load. The Sun remains dynamic.
- Lens Flare **Export HDR** writes linear RGB PFM and refuses to overwrite an existing file.

## Validation and limitations

Native C++/Vulkan changes are tested on Linux with SwiftShader. The Windows executable is cross-built, not
interactively executed on Windows here. The package is unsigned and not antivirus-scanned.

See `Exhibits/CelestialWorkspace/` and `Exhibits/LocalMedia/` in the repository for actual rendered evidence,
parameter-change checks and reproduction commands. Captures use small scene render extents for software-Vulkan
validation; they are not hardware-performance claims.

Unconnected artistic shadow controls and unsupported specialist authoring features remain explicitly unavailable.
