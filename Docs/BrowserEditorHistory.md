> Archived browser-editor README. Relative paths below were written from the repository root. See the root README for current setup.

# Frontier

Browser prototypes now live in **[Experimental/FrontierEditor](Experimental/FrontierEditor/)**: the Frontier Editor, SVG icon gallery and collection-icon options page. Native C++ sources and runtime assets remain under `Engine/`, `Projects/` and `EngineContent/`; native proof/report pages remain under `Exhibits/`.

**Native viewport work:** [selectable environment billboards and rendered synchronization proof](Exhibits/Workbench/Billboards/Native.md), with [native captures and render comparisons](Exhibits/Gallery/NativeBillboards/index.html). This verifies the native CPU renderer; Windows/Vulkan weather parity is not established.

The browser instructions and relative web-file paths below refer to `Experimental/FrontierEditor/`.

A UI-only game-engine outliner and inspector inspired by quiet, charcoal dashboard cards. No viewport or engine backend is included.

## Run

```sh
cd Experimental/FrontierEditor
npm install
npm run dev
```

## Build

```sh
# From the repository root
npm --prefix Experimental/FrontierEditor run build
```

Select Sun, Atmosphere, Clouds, Air / Wind, Forest, Terrain, Lake, or Camera to reveal object-specific controls. The orbital sun-direction gizmo supports dragging; sliders provide keyboard-accessible adjustment. Objects can be added, searched, hidden, and reset. “Save changes” stores property values, visibility, and added objects in this browser's local storage.

## Scene organization

Use the outliner’s **+** menu to create folders and collections. Folders move root objects (with their subcomponents) into a single hierarchy location. Collections reference existing objects without moving or duplicating them. Select either container to rename it, add/remove members, and control member visibility. These settings are included in **Save changes**.

Moon position uses a draggable spherical dial, distinct from the Sun’s orbital control, with arrow keys and sliders for precise adjustment. Moon size controls angular diameter (0.1–2°); disc rotation controls orientation without changing phase. The daylight cycle covers 00:00–24:00, including the night arc. Celestial rotation is also draggable. Stellar twinkle has linked amplitude and frequency controls.

## Standalone SVG icon gallery

`icons.html` is a separate, self-contained page. Open it directly in a browser, visit `/icons.html` on the dev server, or run `python3 scripts/serve-icons.py --port 5174` for an icon-only preview.

It includes 141 icons: 115 original Frontier icons (including 32 file/folder assets) plus 26 selected Slate / NewIcons designs, in flat and dimensional styles made exclusively from SVG paths, gradients, masks, and procedural filters. No raster images, embedded photos, external fonts, or network assets are used. Click an icon to inspect size samples, copy SVG source, or download it. Individual assets are also available in `custom-icons/`.

Regenerate the gallery and assets with `python3 scripts/build-icon-gallery.py`. The production build copies the standalone page unchanged into `dist/icons.html`.

The second icon pass lives in `scripts/icon_revision_two.py`, imported by the generator. It refines Stars, Clouds, Atmosphere, Precipitation, Water body, PBR material, Camera, Lens flare, Collection, and Folder while retaining the existing Sun and Moon artwork.

The reference-led Folder and Camera replacements and satin shader-ball material are defined in `scripts/icon_reference_revision.py`. These override the earlier pass during generation and remain native, self-contained SVGs.

`scripts/media_icons.py` adds four reference-led media assets: Video, Headphones, Joystick, and Clapperboard. They share the gallery’s inspect/copy/download controls.

`scripts/everyday_icons.py` adds Dice, Game controller, and Home. The repeated red-play clapper reference uses the existing Video icon rather than a duplicate asset.

`scripts/utility_icons.py` adds Package, Retro terminal, and Command key. Video, Headphones, Joystick, Folder, Water body, Atmosphere, and Precipitation are retired from the gallery and standalone SVG exports; scene-inspector entities are unchanged. Regeneration removes the retired SVG files.

## Lights category

`scripts/light_icons.py` generates ten matched 2D/3D pairs: Incandescent bulb, LED bulb, LED / DRL strip, Point light, Directional light, Area light, Spotlight, Low beam, High beam, and Front fog light. Select **Lights** in the standalone gallery; paired cards are adjacent and explicitly labelled 2D or 3D. Each is independently inspectable, copyable, and downloadable, with a transparent background and no raster assets.

The flat variants are editor/HMI design assets. Automotive colors and beam directions distinguish the subjects; these are not certified regulatory telltales or production safety-HMI specifications. The scene inspector is unchanged.

`scripts/creative_icons.py` adds nine dimensional assets: Speaker, Film reel, Document bundle, File folder, Retro TV, Cinema, Video slate, Paint palette, and Drawing compass. `file-folder` and `video-slate` are newly requested reference-based designs; the older retired `folder` and `video` exports remain removed.

The nine creative icons receive a geometry/lighting pass from `scripts/creative_depth.py`: explicit backing faces, local contact shadows, inset apertures, material shading and rounded-edge lighting from a consistent upper-left source. No raster assets or global drop-shadow backgrounds are introduced.

Moon transforms use three separate cards: **Moon size**, **Lunar position** (azimuth/elevation), and **Moon rotation** (roll/pitch). Rotation supports two-axis drag and arrow keys (Shift = 10°); pitch turns projected surface details around a horizontal axis without flattening the Moon or changing its sky position. Roll retains the existing `moonRotation` setting; `moonPitch` defaults to 0 for older saves. The orientation reset affects only roll and pitch.

`scripts/hardware_icons.py` adds Hard drive, Arcade joystick (new purple/pink design), Microphone, and paired 2D/3D Graphics card assets. The earlier retired joystick remains excluded.

`scripts/compute_icons.py` supplies the revised colored 2D / dimensional 3D GPU pair, a flat colored RAM module, and World / Earth with simplified orthographic geography. GPU and RAM flat assets use solid vector fills without gradients or filters.

`scripts/editor_icons.py` adds 20 compact, colored Editor symbols: five light types, select/move/rotate/scale, pivot, snap, mesh, camera, material, texture, collection, visibility, lock, keyframe and particle emitter. Use the **Editor** category in the gallery. These are separately simplified small-control symbols, not duplicates of the dimensional artwork.

The Editor set now contains 46 icons. Move uses colored transform axes, Rotate uses directional orbit arcs, and Keyframe uses a diamond on a keyed track. The expansion adds rigid body, box/sphere colliders, trigger, navigation mesh/agent, prefab, instance, script, visual script, animation clip, bone, audio source, terrain, foliage, hierarchy, grid, play mode, pause, and stop.

Editor refinements: Move uses orthogonal colored handles; Rotate is a clear circular three-color control; Snap has vibrant red/blue magnet poles. Bone is a rigging bone rather than an anatomical bone. Lock/Unlock and Visible/Hidden are paired, Simulate uses a bouncing-body/play metaphor, and Navigation mesh is removed (Navigation agent remains).

All 42 Editor symbols now share a saturated blue, mint, violet, gold and coral palette. Animation Clip uses multiple colors; Editor Spot/Area/Directional lights use cone, emitting-plane and parallel-ray silhouettes. Move shows a selected object moving to a target, replacing the previous axis-cross design. Other icon categories retain their own palettes.

The Editor set includes Physics and eight additional interior/vehicle lights (dome, reading, ambient strip, headlight, brake, turn signal, hazard, reverse). Stop uses a vector-lettered octagonal sign; Simulate uses a gear/play symbol; LED is primarily white. These vehicle symbols are design assets, not regulatory-certified telltales.

Editor fixture update: headlight uses an automotive lamp assembly, dome light a ceiling console, reading light a gooseneck fixture, ambient strip a cabin-trim outline, and reverse light a rear-car silhouette. Physics uses a pendulum/cradle; Move uses a cursor with opposed translation arrows. Spotlight keeps its barn-door shape in warm graphite/brass; Area Light keeps its panel shape in silver/ivory.

Seven Editor symbols have a further custom-geometry redesign: split-optic headlight, faceted dome fixture, deforming-surface physics, segmented ambient contour, reverse-light cassette, illuminated reading page, and a translation carriage for Move. These SVG paths are drawn in `scripts/editor_icons.py` rather than imported from an icon pack or traced from engine artwork. Their functional visual conventions are not claimed to be globally exclusive.

Lighting semantics correction: Editor lights are symbolic controls rather than fixture illustrations. Dome is a half-sky/sun/cloud mark; HDRI is a panoramic environment-light symbol; vehicle headlight uses a dipped-beam telltale, reading light an illuminated-book symbol, ambient light radiating bands, reverse light R/backward movement, and directional light an oriented sun/vector. Physics now uses an atom, Visible uses neutral silver, and Lock/Unlock have thicker shackles. Turn Signal is retired.

Gallery pruning: Drawing compass, Paint palette, Video slate, Cinema, Film reel, Document bundle, File folder, Retro TV, and the 3D Spotlight / Area / Directional / Point / LED strip / LED bulb variants are retired. Their source exports are removed on regeneration. The corresponding 2D lights and Editor symbols remain available.

Environment light refinement: Dome uses a blue-to-warm sky gradient and filled sun; HDRI combines a wrapped panoramic environment and reflective probe. These two Editor icons intentionally use native gradients. Reading light, Ambient strip, Vehicle headlight and Reverse light are retired from Editor and exports.

Editor media styling: Simulate retains its gear/play geometry with graphite, silver and amber colors. Animation Clip is a flat ivory-striped charcoal clapper with a coral play button. Audio Source is a flat ivory record player with dark vinyl, coral label, tonearm and green status dot. All three use solid SVG fills without gradients or shadows.

`scripts/editor_surface_icons.py` provides the reworked HDRI cube-map symbol, restrained record-player Audio Source, and volumetric Material shader ball. Navigation Agent is retired. The material uses native gradients and reflected-edge shading, intentionally departing from the flat Editor style.

### Slate / NewIcons import

The **Slate** category preserves 26 user-selected icons from the 102 displayed by [SultanAladin/Slate / References/NewIcons](https://github.com/SultanAladin/Slate/tree/master/References/NewIcons), at revision `aaa87a26945c64d712005707a56ecfecf7a6275f`. Existing Frontier designs and retirements are unchanged. Imported exports use `slate-*` filenames, native 240×240 viewBoxes, isolated gradient/filter IDs, and source attribution in the inspector.

See `vendor/slate-new-icons/NOTICE.md` for provenance and the missing upstream license caveat. These are imported assets, not claimed as Frontier originals. Regenerate with `node scripts/import-slate-icons.mjs`, then `python3 scripts/build-icon-gallery.py`. Run `npm run build && node scripts/check-slate-import.mjs` to verify geometry/style fidelity, SVG rendering, ID isolation, category filtering, inspection, copy, and download behavior.

Slate selection is maintained in `vendor/slate-new-icons/selection.json`. Only the 26 approved imports are included; the other 76 are removed from the gallery and source/production SVG exports on regeneration. “Clipboard” is interpreted as upstream **Clapboard**. The full pinned source snapshot is retained for provenance only.

### Dark file and folder assets

`Files & folders` adds 12 charcoal folders for Materials, Images, Audio, Video, 3D models, Scenes, Scripts, Shaders, Textures, Animation, Fonts and Archives, plus 20 format-specific sheets: PNG, JPG, SVG, EXR, WAV, MP3, OGG, MAT, FBX, OBJ, GLTF, BLEND, JSON, PY, GLSL, ZIP, TTF, OTF, WOFF and WOFF2. The reference-inspired designs use neutral shaded surfaces, pale inset documents, folded/layered sheets, and small muted semantic badges. Existing Document/Image variants are unchanged. Source: `scripts/file_asset_icons.py`; checks: `node scripts/check-file-assets.mjs` after building.

File/folder accent refinement: each of the 32 asset icons has an individually assigned muted color, including a warm ochre ZIP badge and four distinct font-file accents. Charcoal surfaces and neutral format labels are preserved; tinted badges, recessed motifs and small rules carry the color.

File/folder surface update: the 32 asset cards now use solid charcoal front/back/badge fills and solid pale inset sheets rather than surface gradients. Individual color accents remain. Each folder has its category name printed inside the lower pocket beside the badge; the small material-sphere glyph retains its shading.

Folder SVGs include a smaller size/item-count line beneath the name (9-unit metadata below 14-unit labels). Values in `FOLDER_METADATA` are illustrative preview data, not measured filesystem metadata.

Control refresh: Wind is now three plain rounded airflow strokes; Stars contains 62 static dots without glow. The 2D LED/DRL strip is a white segmented swept light bar, with a new flat Car LED front-view icon. Three Editor debugging controls use bug-style shells (Debug bug, Run debugger, Debug breakpoint). Incandescent 3D, both low-beam/fog variants, custom Spotlight and Directional variants in the Lights section are retired; Editor Spotlight and Directional Light remain. Imported Slate Spotlight is unchanged. Source: `scripts/control_icon_refresh.py`.

Further cleanup: removed High beam 2D/3D, Graphics card 3D, LED bulb Lights/Editor, and the colored Editor Camera, Keyframe, Rigid body, Script, Visual script, Terrain, Foliage and Hierarchy. Selected Slate replacements and Graphics card 2D remain. Car LED is now a standalone automotive headlamp assembly rather than a car. Debug icons are redrawn as compact green segmented insects with run/breakpoint badges.

Compute simplification: Graphics card 2D now uses one large five-blade fan and three broad vents on a solid blue shroud. RAM uses three plain chips on a green board with keyed gold contacts. Removed fine traces, pin details and decorative trim; both remain flat SVGs. Graphics card 3D remains retired.

GPU refinement: a clipped blue shroud, six swept blades and a recessed three-slot vent replace the rounded flower-like fan treatment. Instance is now two matching blue cubes joined by a mint reference connector. Editor Audio Source, Animation Clip and Car LED headlamp are retired from gallery and exports. RAM remains unchanged.

Dimensional effects: added Lightning (bevelled golden bolt), Smoke (volumetric turbulent billows), and Fire (layered curved flame with a hot core), all native SVGs under Environment. The custom Game controller now has a compact balanced pearl shell, tapered grips, inset touchpad and clear controls. Slate Old Gamepad Classic is unchanged. Source: `scripts/atmosphere_effect_icons.py`.

Lightning now uses natural irregular branching channels and a cool white-blue corona rather than a solid golden bolt. Added Fluid · water glass (transparent vessel, meniscus, water volume) and Fog (low drifting horizontal mist banks), preserving the existing Liquid droplet. Source: `scripts/weather_material_icons.py`.

Fog/lightning refinement: Fog is a single low continuous bank with noise-modulated density and subtle curling wisps, not stacked shelves. Lightning uses irregularly spaced channels, small seeded deviations, curved joins and fading secondary branches to remove the repeated zigzag pattern. Both remain deterministic native SVG.

Weather/environment expansion: six shaded native-SVG controls in Environment: Rain, Snow, Hail, Frost / Ice, Atmosphere / Sky scattering and Exposure. New source: `scripts/weather_expansion_icons.py`. The scattering icon is a newly requested design (`sky-scattering`), not a restoration of the retired Atmosphere asset. Fog and other previously approved artwork are unchanged.

Atmosphere is now a layered spherical cutaway without a sun: troposphere, stratosphere, mesosphere, thermosphere and exosphere, plus an overlapping ionosphere arc. Layer widths are schematic, not to scale. Rain and Hail are particle-only: small rounded liquid drops with fall streaks, and irregular milky hailstones rather than pointed drops or uniform spheres. Source: `scripts/weather_particle_revision.py`.

Atmosphere refinement: hard concentric layer boundaries and the cutaway have been replaced by smooth cyan-to-blue-to-violet altitude gradients around a shaded sphere. No sun. Frost / Ice is retired from gallery and source/production exports.

Debug redesign: three graphite circuit-beetle controls with split plates, cyan trace inlays, terminal-ended legs and amber status details replace the green bugs. Editor Directional Light and LED / DRL strip are retired.

### Restored Slate layouts and fixture refinements

All 26 selected Slate icons use larger main artwork and independently positioned, smaller bottom-left/right badges. The fixtures are upright. Spotlight uses a shaded cylindrical housing and a connected rounded mounting strap; its approved beam shape, gradient, light core and framing are preserved. Lantern uses dimensional enamel shading on its rounded hood and approved fuel tank, with integrated side tubes and glass. Original source snapshots remain available for provenance.

Rebuild the layout data with `node scripts/measure-slate-layout.mjs`, then `python3 scripts/build-icon-gallery.py && npm run build`. Validate with `node scripts/check-slate-import.mjs`, `node scripts/check-file-assets.mjs`, and `node scripts/check-weather-expansion.mjs`. The gallery server disables caching during iteration.

### Gallery icons in the scene outliner

The inspector preview runs with `npm run dev -- --port 5173 --strictPort` (0.0.0.0; proxied preview hosts supported). `outliner-icons.jsx` integrates the gallery SVGs into outliner rows, the add menu, folder/collection rows, and the project thumbnail. Precipitation follows Rain/Snow/Hail settings, and duplicated entities inherit their base icon. Folder thumbnails strip illustrative labels/counts; live object names stay in the row. Species, individual non-cube primitives, and water-body symbols retain their specific fallback icons where the gallery lacks matching artwork. SVG URL imports are versioned by Vite for production. Inspector controls and the two-panel/no-viewport layout are unchanged. A folder collapse-state inversion was corrected while testing.

Run `node scripts/check-outliner-icons.mjs` for integration checks covering icon mappings, folder thumbnails, selection, precipitation switching, collapse/expand, inherited visibility, duplicates, fallback behavior and saving.

Outliner readability: Stars and Precipitation now use dedicated 24-unit SVG glyphs in `ui-icons/` instead of shrinking dense particle illustrations. Stars uses a bold three-star cluster; Rain/Hail use a compact cloud with large falling marks, and Snow a single strong crystal. Detailed gallery artwork is unchanged.

Precipitation readability correction: the outliner now uses one large umbrella for the precipitation entity, rather than miniature clouds/particles. It stays consistent across Rain/Snow/Hail; those mode controls remain in the inspector and still work. The approved Stars glyph and detailed gallery assets are unchanged.

### Quick bake tiles (editor setup, not engine rendering)

Sun lighting, Sun disk, Atmosphere, Rainbow, Lens Flare and Stars now have property-style bake cards at the bottom of the inspector, using monochrome symbols and the same ON/OFF tile styling as the other property switches. **Bake** records a timestamped `pending-renderer` request containing target/settings; it never reports a completed render. Requests can be exported as JSON. **Load image** accepts decoded PNG/JPEG/WebP previews up to 256 KB / 8192 px; **Use baked image** selects that source in editor metadata. Existing procedural controls are preserved for editing/rebaking; no engine shading is changed without a renderer integration.

Images and per-target modes are saved with the existing project values (`bake:<target>`), with a 1.5 MB combined image budget and storage-failure reporting. Removing a file returns to procedural mode. Duplicated entities have independent bake state. Gallery **Bake** (`editor-bake.svg`) depicts light entering a checker texture. Tests: `node scripts/check-bake-controls.mjs` and `node scripts/check-outliner-icons.mjs`.

### Local atmosphere volumes

**Local Volumetric Clouds** and **Local Fog** are separate Environment entities, also available in Add to scene. Their native SVG outliner symbols reuse the cloud/fog artwork with visible boundary corners. Local Clouds reuses the exact Cloud coverage, Cloud base and Layer thickness cards and ON/OFF switches. Local Fog reuses Atmosphere’s Ground fog card and Fog switch. One **Volume bounds & position** card adds shape, positive dimensions in meters, and world-space center. Choose Box, Sphere, Cylinder, Cone, or Custom outline. Custom outlines are validated closed X/Z polygons (3–16 vertices, normalized −1…1), extruded through the configured height; self-intersections and zero-area outlines are rejected. Click Apply outline to save changes; invalid or unapplied drafts never replace the saved footprint. The SVG is a shape schematic, not a scaled rendering. Effect settings remain independent per object, using the usual project save/reset/visibility system; bounds/position use `volume*` fields. Global Clouds/Atmosphere remain unchanged. These are editor configurations and diagrams, not an implemented volumetric renderer. No viewport is added.

Folder icons are consistent in the hierarchy, selected-folder header, and overview. Environment uses a sky/cloud folder, Water Bodies uses waves, World uses the model folder, Scene uses hierarchy, and Materials uses the material folder. Uncategorized folders get a neutral generic folder instead of the Scene icon. Folder/collection member lists also use the same entity artwork as the outliner, with existing semantic fallbacks preserved.
