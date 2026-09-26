# Frontier

## D-Day: Atlantic Wall (`dday/`)

A low-poly driving level in Three.js (vendored, no build step).

```bash
cd dday && python3 -m http.server 8080   # open http://localhost:8080
```

**Goal:** drive from the landing craft on Omaha Beach to the gate in the Atlantic Wall, about 2.2 km of route, before the rising tide catches you.

- **Beach:** Belgian gates, 5 staggered belts of Czech hedgehogs, mined stakes (Rommel's asparagus), log ramps topped with Teller mines, Teller mines in the sand, and triple-concertina wire along the shingle
- **Bluffs:** three draws, one of them sealed. MG bunkers watch the beach, and a trench line runs along the crest
- **Inland:** winding dirt roads that fork (West Loop or Mine Alley), five trench lines you can only cross on the roads, wire belts, bocage hedgerows, huge earth mounds, craters, dug-in and wrecked tanks (static), concrete-block slaloms, knife-rest barricades, Teller-mine patterns on the roads, and AP minefields marked "ACHTUNG MINEN"
- **Wall approach:** dragon's teeth, a Teller minefield, and a 17 m wall with twin-MG turrets on top. The gate is marked with a blue light column
- **Enemies:** 29 MG turrets that scan, spot you (needs line of sight), lead the target and fire in bursts of discrete tracer rounds. Stuka dive bombers release sticks of 3 bombs on a ballistic solution
- **Extras:** checkpoints, repair crates, three difficulty levels, a paint choice, a minimap and synthesized audio

Code: `dday/js/` holds `layout.js` (authored battlefield data), `terrain.js`, `props.js`, `models.js`, `vehicle.js`, `enemies.js`, `fx.js`, `audio.js` and `main.js`.
