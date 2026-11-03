# Scene inspector workbench · HTML prototype

A single Outliner/Inspector workbench for **Vehicle**, **Sun**, **Atmosphere**, **Clouds** and geometry-backed **3D Object** records, continuing the Render settings / Sun / Atmosphere card language. The folder keeps its original `VehicleInspector/` name for branch continuity; an alias also exists at [`../SceneInspector/`](../SceneInspector/).

The initial **Vehicle** page is retained and deliberately stays data-only: no vehicle mesh, wheel mesh, terrain or other geometry. It owns four parented tyre sockets that can be detached and replaced at pit stops. The new environment and object records add editable unit cards, fields, point curves, child records and a live software 3D preview.

![Design intent: dark dashboard cards, coloured icons, big unit readouts, compact editable curves](../../SourceImages/General%20UI/0bab27674eea6c04ccaf172f194ce6ef.jpg)

> The reference images live in [`SourceImages/General UI`](../../SourceImages/General%20UI/). The dock palette and cards follow the existing [`RenderSettingsInspector`](../RenderSettingsInspector/).

## Open it

From the repository root:

```sh
python3 -m http.server 8080 --directory .
# http://localhost:8080/Experimental/VehicleInspector/
# or http://localhost:8080/Experimental/SceneInspector/
```

The repository root `index.html` also redirects straight to this prototype. All CSS, JavaScript and fonts are local, so `index.html` also opens from disk in a modern browser.

## What is in the prototype

- **Outliner** — Rendering, Environment, Objects and Vehicles groups. Sun, Atmosphere and Clouds are live records; Clouds owns Cumulus and Fog layers. Four geometry-backed object records own Mesh, Material and Collider children. Vehicle rows keep FL / FR / RL / RR nested tyre sockets, and empty sockets remain visible with a Fit action.
- **Scene composition** — a records/hierarchy node view with a **3D scene** tab. The 3D view is a dependency-free canvas renderer with a procedural sky, Sun, drifting clouds, a perspective ground grid and depth-sorted box, cone and icosahedron meshes. Drag to orbit, wheel to zoom, click a mesh to inspect its record.
- **Vehicle inspector** — coloured unit cards for mass `kg`, power `kW`, torque `N·m`, wheelbase `mm`, drag area `m²`, downforce `N`, top speed `km/h`, 0–100 `s`, pressure `kPa`, temperature `°C`, wear `%`, grip `µ`, fuel `kg`, redline `rpm` and more.
- **Tyre inspector** — condition fields, compound selection, detach, fit and replace actions. A detached tyre becomes an empty socket rather than disappearing from the hierarchy.
- **Sun / Atmosphere / Clouds inspectors** — explicit-unit cards, grouped editable fields, child-record chips and editable curves such as solar elevation `° / h`, spectral irradiance `W/m²/nm / nm`, Mie scattering, cloud coverage and height fog density.
- **3D Object inspectors** — triangle counts, transforms in `m` / `°` / `×`, bounds `m³`, LOD distance `m`, mass `kg`, material roughness `%`, collider friction `µ`, and Mesh/Material/Collider child pages. Transform and mesh edits update the 3D preview.
- **Editable curves** — drag points, click the plot to insert one, double-click to remove one, use arrow keys for fine edits, toggle smooth/linear, reset built-ins, or add a custom labelled curve with its own X/Y labels, units, maxima and accent. Curves exist on Vehicle, environment, object and component records.
- **Custom records** — edit specification fields, save/load browser presets, add another data-only Vehicle, add another geometry-backed 3D Object from spec, and export the selected record (parameters + children + curves) as portable JSON.

## Deliberately deferred

The full Vehicle editor and Tyre editor are **not** built yet. The 3D preview is a lightweight software renderer for prototype scene records, not a native engine viewport or a physics/C++ integration. Pit-stop actions, curve points, record fields and presets are live prototype UI state.

## Files

| File | Contents |
| --- | --- |
| `index.html` | Outliner, records/3D composition, Log Stream and Inspector docks; curve/JSON dialogs |
| `VehicleInspector.css` | Dark InspectorPalette tokens, coloured card tones, outliner, 3D overlay, charts, fields and dialogs |
| `VehicleInspector.js` | Vehicle/tyre pages, generic scene-record inspector, curve editor, presets, actions and JSON export |
| `SceneRecords.js` | Sun, Atmosphere, Clouds and geometry-backed Object/child record catalogue with units and curves |
| `ScenePreview3D.js` | Dependency-free canvas sky, cloud, ground-grid and depth-sorted mesh renderer |
| `Fonts/` | Bundled DM Sans 300 / 400 / 500 with the OFL licence |

A DOM smoke test covers initial rendering, tyre detach/refit, vehicle curve tabs and point editing, spec edits, custom curves, adding a second vehicle, Sun/Atmosphere/Clouds/Object inspectors, record fields, 3D view switching, adding a 3D object, presets and JSON export.
