# Frontier

General-purpose, cross-platform editor with reusable engine code.

## HTML inspector prototypes

- **[Scene inspector workbench](Experimental/SceneInspector/)** — the current step. It keeps the data-only Vehicle/tyre pages and adds rich **Sun**, **Atmosphere**, **Clouds** and geometry-backed **3D Object** records. Every record has explicit-unit cards, coloured icons/text, editable fields and draggable point curves; objects are also visible in a dependency-free software 3D scene preview. The historical implementation folder is [`Experimental/VehicleInspector/`](Experimental/VehicleInspector/).
- **[Render settings inspector](Experimental/RenderSettingsInspector/)** — the previous Render settings / Sun / Atmosphere card prototype that this work continues. Its viewport inspection captures are under `Exhibits/SceneViewport/Images/Inspection/`.
- General dashboard and card references are preserved in [`SourceImages/General UI`](SourceImages/General%20UI/).

Serve the repository root and open the linked folder, for example:

```sh
python3 -m http.server 8080 --directory .
# http://localhost:8080/Experimental/VehicleInspector/
# or http://localhost:8080/Experimental/SceneInspector/
```

The root `index.html` redirects directly to the workbench. The full Vehicle/Tyre editor and native/C++ engine integration remain intentionally deferred; this branch delivers the HTML inspector and scene-record surface first.

## Scene record scope

The Vehicle remains a data-only record: specification, four parented/removable tyre sockets and curves, with no vehicle mesh. The new environment records drive procedural Sun, Atmosphere and Clouds parameters without spawning geometry. The four initial object records — Proving plinth, Start gantry, Track beacon and Suzanne proxy — are geometry-backed and own Mesh, Material and Collider child records. Box, cone and icosahedron primitives are depth-sorted in the centre-pane 3D view; drag to orbit, wheel to zoom, and click a mesh to select its record.

Custom controls can add another 3D object from the current spec, save/load browser presets for scene records, add labelled curves with explicit X/Y units, and export portable JSON. Curve points are draggable point splines with click-to-insert, double-click-to-remove, keyboard editing and smooth/linear toggles.

---
