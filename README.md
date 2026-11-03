# Frontier

General-purpose, cross-platform editor with reusable engine code.

## HTML inspector prototypes

- **[Vehicle inspector](Experimental/VehicleInspector/)** — the current step: a data-only Vehicle record in the Outliner, four parented/removable tyre sockets, coloured unit cards and editable engine/thermal/boost/grip curves. No geometry is attached.
- **[Render settings inspector](Experimental/RenderSettingsInspector/)** — the previous Render settings / Sun / Atmosphere card prototype that this work continues. Its viewport inspection captures are under `Exhibits/SceneViewport/Images/Inspection/`.
- General dashboard and card references are preserved in [`SourceImages/General UI`](SourceImages/General%20UI/).

Serve the repository root and open the linked folder, for example:

```sh
python3 -m http.server 8080 --directory .
# http://localhost:8080/Experimental/VehicleInspector/
```

The root `index.html` redirects directly to the Vehicle inspector. The full Vehicle/Tyre editor is intentionally deferred; this branch first delivers the inspector in HTML.

## Vehicle step scope

The Vehicle record carries specification, tyre-socket and curve data only. Tyres are child records that can be detached, fitted and replaced through pit-stop actions while remaining visible in the Outliner. Curves are draggable point splines with explicit physical units, and the custom-vehicle controls can save presets, spawn additional data-only records and export a portable JSON record.
