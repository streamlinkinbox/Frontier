# RoadWorks Editor

A self-contained browser editor for procedural road corridors, shared at-grade junctions, pavement, curbs, spline handles, and elevated bridge structures.

## Run locally

```sh
npm install
npm run dev
```

The editor runs as a Vite app at the local URL printed in the terminal. To create a static build:

```sh
npm run build
npm run preview
npm test
```

For a static GitHub-hosted preview, the entry page is `index.html`; its relative source modules and browser import map are arranged for raw.githack after the branch is published.

## Editing

- **Select (V):** select roads, junctions, and spline handles. Drag nodes or gold Bezier handles in the viewport.
- **Road (R):** click to place route points, then double-click or press Enter to finish. At-grade spline crossings are split into shared graph nodes.
- **Bridge (B):** click two abutments to create a raised span. Deck systems, support families, ramp length, pier spacing, and parapets are procedural.
- **Ctrl/Cmd+Z:** undo. **Ctrl/Cmd+Shift+Z** or **Ctrl/Cmd+Y:** redo.
- Use the Inspector to tune lane profiles, pavement, curbs, markings, spline tension, and bridge structure.
- Projects autosave locally. Export an editable `.rwn.json` network or generated `.obj` mesh from the Export menu.

RoadWorks uses a graph-first model: every at-grade crossing becomes one shared junction vertex and the original cubic curve is split without changing its shape. Bridge spans are treated as a separate elevation layer, so flyovers remain grade separated.
