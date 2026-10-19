# Three connected bridge networks

These are compact, reference-inspired elevated urban networks, not cloverleaf motorway interchanges or literal reproductions of the supplied photographs. The second reference depicts a roundabout over a through-road; its implementation raises the roundabout rather than excavating a terrain tunnel.

## Built examples

1. **Pearl loop interchange** — a 200 × 310 m elevated oval, one-way two-lane circulation, two diagonal approach ramps and a four-lane boulevard beneath. Surface return streets connect the approaches and underpass to the shared access road.
2. **Lantern roundabout interchange** — a 156 m diameter elevated distributor with four approaches. A separate, supported landscaped central deck covers the through-road, with a perimeter expansion joint, planted circle and radial paths. Inner curbs are lower than the external safety barriers.
3. **Dragonfly branching viaduct** — a two-way curved Y-shaped elevated junction. Three approach ramps meet ground-level return streets; two uninterrupted streets pass beneath its branches.

The complete starter now has 306 road segments. All network edges are reachable from the original map through the new approach road. They are ordinary editable road graph edges, not decorative disconnected objects. The road ends, curves, junctions, terrain and 2D view use the same project state.

## Find them

Use **Bridge networks · 3**, or the **Connected bridge networks** group at the top of **Find a sample**. Each card offers **View interchange** and **Fit all connections**. First installation frames the Pearl loop. Reloading an existing upgraded save retains the normal Eastbank arrival behavior. The two earlier single bridge examples remain available separately.

The locally stored photographs in `public/references/bridge-networks/` are user-supplied reference material, not renders produced by the editor. Their watermarks are retained; rights remain with their respective owners.

## Geometry and safeguards

- Explicit node elevations and linear span profiles keep successive bridge segments at the same height. A ramp uses six segments sampled from a smoothstep height profile, rather than a separate sine-shaped hump on every segment.
- Elliptical/circular fillet radii produce continuous ring bends. Roads, merged junctions, concrete safety shoulders and support slabs reuse the same trim geometry.
- The slab skin is quad-based. The renderer builds a closed indexed solid from it, with no transverse barriers across connected road mouths. Piers and caps stay outside lower road envelopes. Sparse light standards sit beyond the safety shoulders.
- The deck geometry includes junction slabs, not just isolated straight bridge spans. Its source skin is checked for degenerate, duplicate, non-convex and non-manifold faces. Closed-volume edge incidence and finite position/normal/UV attributes are also tested.
- Road and shoulder audits include same-level positive-area overlap checks and welded mouths. Grade-separated crossings are not falsely merged.
- The network audit rejects ramp grades above 6.5% and modeled crossing clearance below 5.5 m, allowing 1.2 m structural depth and a width/slope allowance. The supplied full-map fixture is below 5.5% maximum grade and above 7 m minimum checked crossing clearance.
- At identical plan coordinates, ground and elevated endpoints are distinct graph nodes. Dragging an elevated node retains its explicit elevation. Ordinary surface roads cannot silently join elevated nodes.

### Current scope

These are geometric prototypes, **not structural, highway or traffic engineering approval**. The simplified checks do not replace swept-vehicle paths, full 3D swept-envelope collision detection, sight distance, vertical-curve design, superelevation, geotechnical work, load analysis, drainage or local standards. The visual landscaped island is not a public pedestrian facility: accessible pedestrian routes and barriers at its access points have not been designed.

Network profiles currently support carriageways and 0.9 m concrete safety shoulders, not sidewalks or protected-cycle street profiles. Unsupported profile changes are rejected rather than generating unsupported paving. Node positions, lane counts and widths remain editable subject to the geometry/clearance checks. There is no traffic simulation, lane routing or operational signal control.

Road and streetscape surfaces remain exportable quad meshes. Concrete slab solids, piers, barriers, lights and the landscaped island are generated viewport detail, not included in the existing road-surface OBJ. Project JSON retains the graph, explicit elevations, curve metadata and network identifiers so these details regenerate.

## Preservation

Installation clones the entire project, places the district beyond existing road/rail bounds and connects to an existing ground-road endpoint. It validates the complete candidate before assignment. Existing road and node records are not replaced. If space or geometry validation fails, the original project remains unchanged.

The browser saves `frontier-before-bridge-networks-v1` before installation. **Backup ↓** exposes this backup alongside the earlier Eastbank-repair and transport backups, without hiding one behind another. Undo is available during the session. JSON reload does not append duplicate networks. The manual Eastbank repair is still scoped to its own tagged district; the bridge district has separate tags.

## Tests

```sh
npm test
npm run build
npm run preview
npm run test:bridge-networks
npm run test:startup
npm run test:second-city
TRANSPORT_PHASE=repair npm run test:transport
```

Unit checks cover connectivity to the original road, idempotence, atomic rejection, shared endpoint heights, grades, crossing clearance, closed slab solids, lower-road pier exclusion, metadata validation and elevated node moves. Browser checks cover upgrading an existing transport map, byte-for-byte backup, all three selectors, reference picker, 2D view and saved reload.
