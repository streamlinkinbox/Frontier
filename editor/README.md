# Frontier Road Editor

Web-based road editor (Three.js + Vite) for building large, clean road networks:
city grids, highways, railways, subway surface lines, countryside and offroad tracks.

```bash
cd editor
npm install
npm run dev        # http://localhost:5173
npm test           # topology tests (node --test)
npm run build
```

## Controls
- **Camera (Unreal-style):** WASD move, Q/E down/up, hold right mouse to look, Shift boost, wheel = speed.
- **Select (1):** click a road to inspect its lanes, re-roll its variation, change its profile.
- **Draw road (2):** click to place control points, double-click / Enter to finish, Backspace removes last point, Esc cancels.
- **Bridge span (3):** click a road to add a grade-separated span (type + length from the sidebar). Shift-click removes the nearest span.
- **Delete (4):** click a road to remove it. Ctrl+Z undoes.

## How the topology works (`src/network.js`)
Roads are splines sampled to a polyline. Every contact becomes a graph node:
- proper crossings of two centrelines (at the same height) -> one shared node (`+`)
- a road end landing on another road within 3 m -> T / Y junction
- three or more road ends meeting -> Y junction
- bridge spans are grade-separated: no node is created inside a span, so the bridged road passes over/under.

Node degree 1 = dead end, 2 = pass-through, >=3 = junction.

Junction paving (`src/junction.js`) is a 2D polygon union (Clipper) of every approach footprint,
then triangulated. There are no 3D boolean operations.

## Road content (`src/profiles.js`, `src/meshes.js`)
Profiles: avenue (bus + bike + parking), boulevard (median), residential, cycle street, bus rapid transit,
highway (divided, shoulders), country road, offroad track, footpath, surface railway, subway surface line.

Each road gets a seed-based variation: lane widths, asphalt tint, dash pattern, stall spacing, bollards,
speed bumps, street trees and lamps, and sidewalk drops, so repeated profiles don't look identical.

Bridge types: girder, arch, cable-stayed, viaduct, truss.
Rail crossings: a road crossing a rail line gets barrier arms and warning posts.
Zebra crossings are placed on every junction approach (except highways and countryside).

## Known limitations
- Terrain is flat. Hills, cuttings and underground subway tunnels are not implemented yet.
- Subway lines are drawn at surface level with platforms and entrance kiosks.
- Junctions are plain unions of approach footprints. Corner radii and kerb returns are not modelled yet.
- Only centrelines are checked for conflicts. Two parallel roads closer than their combined widths can overlap.
- Every edit rebuilds the whole network (about 1 s for a 2 km city). Incremental rebuilds are needed for larger maps.
- Buildings are not included, as requested.
