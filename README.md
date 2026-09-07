# Frontier — SDF Terrain Lab

An interactive browser prototype for authoring a small, unique desert canyon terrain without relying on imported terrain textures or a conventional world heightmap.

## Run locally

```bash
npm install
npm run dev
```

## What the prototype demonstrates

- **Analytic signed-distance terrain stack** — canyon, shelf, crag, and sculpt brush operations are combined procedurally. The visible mesh is only a preview extraction of the SDF surface.
- **Realtime local weathering** — the Erosion panel exposes rainfall, flow, sediment capacity, and rock hardness. The solver moves sediment through a compact local SDF-shell displacement cache and updates the viewport as it runs.
- **Sculptable terrain** — select a sculpt operator and click the terrain to add or carve volume. Drag to orbit the scene; scroll to zoom.
- **Surface-bound water** — a small seasonal creek follows the canyon course with depth, mineral tint, and subtle wind-driven ripples.
- **Portable recipe export** — Export writes the scene's SDF brush operations plus erosion and water settings to JSON, rather than exporting a baked texture.

The renderer is deliberately scoped to a 24 × 24 m authoring area rather than an infinite world. It detects WebGPU availability and clearly labels the browser fallback while keeping the erosion controls live for the prototype.
