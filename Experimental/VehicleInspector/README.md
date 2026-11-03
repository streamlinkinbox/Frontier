# Vehicle inspector · HTML prototype

A data-only **Vehicle** record added beside the Render settings, Sun and Atmosphere records in the Frontier Outliner. This is the inspector-first step: there is deliberately **no vehicle mesh, wheel mesh, terrain, or other geometry**. The Vehicle owns four parented tyre sockets that can be detached and replaced at pit stops.

![Design intent: dark dashboard cards, coloured icons, big unit readouts, compact editable curves](../../SourceImages/General%20UI/0bab27674eea6c04ccaf172f194ce6ef.jpg)

> The reference images live in [`SourceImages/General UI`](../../SourceImages/General%20UI/). The dock palette and cards follow the existing [`RenderSettingsInspector`](../RenderSettingsInspector/).

## Open it

From the repository root:

```sh
python3 -m http.server 8080 --directory .
# http://localhost:8080/Experimental/VehicleInspector/
```

The repository root `index.html` also redirects straight to this prototype. All CSS, JavaScript and fonts are local, so `index.html` also opens from disk in a modern browser.

## What is in the prototype

- **Outliner** — Rendering, Environment and Vehicles groups. The Vehicle row is a parent; FL / FR / RL / RR tyres are nested child sockets. Empty sockets remain visible with a Fit action.
- **Scene composition** — a node view of the parent record and its four sockets, explicitly marked `0 meshes / no geometry`. No 3D geometry is spawned.
- **Vehicle inspector** — coloured unit cards for mass `kg`, power `kW`, torque `N·m`, wheelbase `mm`, drag area `m²`, downforce `N`, top speed `km/h`, 0–100 `s`, pressure `kPa`, temperature `°C`, wear `%`, grip `µ`, fuel `kg`, redline `rpm` and more.
- **Tyre inspector** — condition fields, compound selection, detach, fit and replace actions. A detached tyre becomes an empty socket rather than disappearing from the hierarchy.
- **Editable curves** — engine torque (`N·m / rpm`), thermal derate (`% / °C`), boost (`bar / rpm`) and grip (`µ / °C`). Drag points, click the plot to insert a point, double-click to remove one, use arrow keys for fine edits, toggle smooth/linear, reset, or add a custom labelled curve with its own units.
- **Custom vehicles** — edit the spec, save/load browser presets, add another data-only Vehicle from the current spec, and export the full record (spec + sockets + curves) as JSON.

## Deliberately deferred

The full Vehicle editor and Tyre editor are **not** built yet. This step shows the inspector first, as requested. The pit-stop actions and curve points are live prototype controls, but they are UI state only; they do not create engine geometry or bind to a physics solver.

## Files

| File | Contents |
| --- | --- |
| `index.html` | Outliner, scene composition, Log Stream and Inspector docks; curve/JSON dialogs |
| `VehicleInspector.css` | Dark InspectorPalette tokens, coloured card tones, outliner, charts, fields and dialogs |
| `VehicleInspector.js` | Scene records, tyre sockets, inspector pages, curve editor, presets and JSON export |
| `Fonts/` | Bundled DM Sans 300 / 400 / 500 with the OFL licence |

A DOM smoke test covers initial rendering, tyre detach/refit, curve tabs and point editing, spec edits, custom curve creation, adding a second vehicle, and JSON export.
