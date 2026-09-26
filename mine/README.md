# Frontier · Mine Run

Procedural mine race level built on splines. It runs in the browser (Three.js) and exports to OBJ (real quads) or GLB.

```bash
cd mine && npm install && npm run dev      # http://localhost:5173
npm test                                   # mesh topology + vertical profile + cart-lane clearance checks
npm run test:drive                         # headless autopilot drive through the maze
```

## What it builds
* **Spline network**: junction *nodes* plus tunnel *edges*. Each edge is a centripetal Catmull-Rom spline through its control points. There is a maze of 20 junctions with 3 to 6 tunnels each, diagonal criss-cross tunnels, and one **overpass** where one tunnel dives under another.
* **One continuous closed all-quad mesh** for the cave walls, the arched roof, the paved road and the grooved cart tracks (the rail grooves and rail heads are part of the floor profile). The validator checks that the mesh is 2-manifold, has no boundary edges, has consistent winding and has no degenerate quads.
* **Floor is not flat**: height changes along the splines, roads bank in curves, and tunnels leave each junction level so the junction floor doesn't form a ramp.
* **Timber supports** (posts, cap, knee braces, steel plates) and **hanging lamps** are placed along every spline frame. The nearest lamps get real point lights.
* **Ore-train traffic**: a loco plus 1–3 wagons. Trains run in the grooves, keep to the right, take smooth Bezier turns at junctions, avoid hairpins and keep their distance from each other. Hitting one costs a 2 s penalty.
* **Box rally car**: raycast suspension, all-wheel drive, ~750 hp, a boost tank, handbrake drifts and downforce. Stability control stops it from flipping, and collision spheres make it bounce off walls instead of driving through them.

## Junction topology (the important part)
A junction with K tunnels is built like this:
1. **Corners use L flow.** Between two neighbouring tunnels the wall is a curved strip of 2k × wallSegs quads. The horizontal wall loops of one tunnel go round the corner and continue into the next tunnel. The floor/wall crease does the same. There are no `| + _` T-junctions.
2. **Arm strips.** Each tunnel carries on into the junction as an a × k strip. Its side edges are exactly the halves of the corner fillets, so every tunnel edge loop continues straight in.
3. **Y / + pole.** The rest of the floor is a K-gon (and the roof has a matching domed K-gon). It is filled with K quad patches that meet in **one pole of valence K**. The spokes leave that pole at equal angles and curve into the arms. A 3-way junction is a clean **Y**, not a `V + |` with a flattened 180° corner.

With this layout the only irregular vertices are the centre pole of each junction (one on the floor, one on the roof) and a single valence-5 vertex per corner on the floor and on the roof. That is the smallest number of irregular vertices this shape allows.

## Drivable vertical profile

The floor is never flat, but it is bounded so the car stays planted:

- Each tunnel spline is resampled at 1 m and its heights are relaxed until the crest radius is at least
  `min crest radius` (65 m by default, so liftoff happens at roughly 92 km/h), sags are at least 40 m, and grade is at most `max grade` (16 %).
  Both are GUI sliders: lower the radius for more jumps.
- The last few metres before each junction ease to about 3.5 % grade, so tunnels enter hubs almost level.
- Junction floors use a height field: an inverse-distance blend of every arm's extended road plane.
  This removes the creases a pure Coons patch leaves when the arms arrive at different heights.
- The overpass keeps about 4 m of rock between the two tunnels (checked by `tests/profile.mjs`).
- Cart transitions through junctions are clearance-checked (a 1 m sphere at 1.3 m height, tested along the whole Bezier path).
  Handles widen until the path fits, and turns that never fit are dropped. `tests/lanes.mjs` checks that no lane dead-ends.
- While you drag a handle in the editor, rebuilds use a coarse preview (loop spacing ×2.2, no rock noise), which takes about 250 ms.
  The full mesh is rebuilt when you release the handle.

## Editor (TAB)
Click an orange junction node or a cyan control point and drag the gizmo. The cave, road, grooves, supports, lamps, cart lanes, collision, minimap and checkpoints all rebuild. While you drag, the mesh updates about 7 times a second; everything else rebuilds when you let go. Tunnels that intersect are shown in red. The panel has settings for the profile (width, height, bulge, groove depth, rock noise, banking), the topology density (loop spacing, wall and roof loops, corner segments k), props and traffic. It can also export OBJ (quads, one group per patch) and GLB, and save or load the spline JSON.

## Files
`src/network.js` maze + splines · `src/profile.js` cross-section · `src/mineBuilder.js` mesh, junctions, lanes, props placement · `src/car.js` vehicle · `src/traffic.js` trains · `src/editor.js` spline editor · `src/materials.js` procedural shader · `src/exporters.js` OBJ/GLB/JSON
