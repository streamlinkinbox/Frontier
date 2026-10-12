# Frontier Road Editor (experimental)

Browser-based road / city editor (three.js + Vite). Built for NFS-style large maps:
WASD fly camera (RMB to look), lane-typed roads, clean merged topology, bridges, subways,
railways, props and a procedural city/countryside generator.

## Run

    npm install
    npm run dev        # http://localhost:5173
    npm test           # topology + generator tests
    npm run build

## What it does

- **Roads**: centripetal Catmull-Rom centrelines; 14 presets (residential, arterial with bus/bike lanes,
  tram avenue, boulevard with median, segregated cycle boulevard, highway, country road, gravel, off-road,
  footpath, cycle path, railway). Every lane (driving, bus, bike, parking, sidewalk, median, tram, rail,
  shoulder, grass, gravel, dirt…) is editable per road.
- **Markings**: dashed/solid white, double yellow centre lines, red bus lanes, green bike lanes,
  arrows, edge lines, zebra crossings at junctions, patches/cracks.
- **Topology** (`src/core/network.js`): same-level crossings, T-contacts and endpoint contacts become shared
  nodes; roads are split into edges between nodes, so edges only touch at nodes. Junctions get merged
  asphalt plates (+, T, Y, dead-end caps). Overlapping roads are reported as warnings.
  Different levels (bridge over road, subway under road) are grade-separated and never merged.
  Railways never merge; railway x ground road becomes an automatic level crossing (gates, lights, crossbucks).
- **Bridges**: 4 structure models: girder, tied arch, suspension (two towers, catenary cables, hangers),
  cable-stayed (single tower, fan of stays). Ramps blend from ground to deck at each end.
- **Subways**: underground lines (toggle the underground view with G).
- **Props**: bollard rows, speed bumps, street lamps, railway crossings.
- **Generator**: seeded city with arterial grid, local streets, a river with automatic bridge splitting,
  metro, railway, highway, country roads and off-road trails. Every road gets its own seed, lane-width jitter
  and wear, so roads are not copy-pasted.

## Limits (v1)

- Whole-model rebuild on every edit (~0.3 s for ~70 roads, ~2.7 s for the generated city in headless software GL). Chunked rebuilds are the next step.
- Junction plates are convex and flat; no corner curbs or turn lanes yet.
- Bridges are whole roads (split a road into two parts to bridge only a section).
- Project JSON is the source of truth (Save/Open); no Unreal export yet.
