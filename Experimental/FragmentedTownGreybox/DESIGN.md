# The Fragmented Town — greybox level design

One level, made of broken places held in open air. The player drives **from portal to
portal, never in laps**: each stone portal moves the car to another district *inside
the same level* (no loading, no level switch) — many small worlds strung on one route.

**Tone rules (hard constraints, from the brief):** no neon, no cyberpunk, nothing
"Gen Z", nothing nerdy-lampoon. The reference feeling is an ordinary town — brick,
stone, plaster, slate roofs, timber furniture — that has come apart and stayed that
way. Quiet, sun-bleached, overcast. Ready-Player-One only in the *structure* sense:
distinct worlds visited in sequence inside one larger game.

## The one idea everything serves

**The town is the road.** There is no track built over the level. The drivable path is
made of the place itself — torn road slabs, flat roofs, a snapped viaduct — fractured
and drifting, but always, just barely, continuous. The player reads the *town* to find
the way, not paint or arrows.

## Path & motion rules (what keeps "floating and moving" fair)

1. **Path pieces overlap in plan** (each road slab is longer than its spacing), so the
   gentle bobbing can never open a gap under the car.
2. **All pieces in a neighbourhood share near-identical motion phase** (they drift
   like wreckage from the same break, not like independent platforms). Motion is a
   slow sine: ≤0.3 m vertical, ≤0.6° sway, period 20–45 s. Natural, never rhythmic.
3. **Height steps between neighbouring pieces ≤1 m** — the car flows; it never has to
   jump or "platform".
4. **Buildings flank and carry the path**, they never block it: fractured houses stand
   beside the road, and selected flat roofs are *on* the route (District I has three;
   District III is nothing but roofs).
5. **Falling is forgiving**: drop >45 m below the last portal and you rez back at it.
   It is a drive, not a punishment.

## Districts (greybox v1 — all three playable in `index.html`)

| # | District | Character | Path | Length / target pace |
|---|---|---|---|---|
| I | **The Fragmented Town** | Brick/plaster houses, cracked and leaning, gardens gone, furniture in the sky | Winding torn road climbing over three rooftops | ≈430 m → **30 s+ at 12–14 m/s** (the brief's minimum) |
| II | **The Shattered Causeway** | A snapped viaduct: piers sinking into nothing, columns adrift beside the spans | Long, nearly straight, faster; the danger is the edges | ≈330 m |
| III | **The Roofline** | Chimneys, gables, a whole street's worth of roofs at 40 m | The road *is* the roofs, descending home | ≈230 m |
| | **Total** | one level, three places, two portals + end gate | | ≈990 m, roughly 2½–3 minutes |

## Portals & checkpoints

- Portals are **plain stone arches with a pale hanging keystone** — architecture, not
  effects. Driving through one fades briefly and places the car at the next district's
  entry, same level, everything still drifting. Each portal is the district's
  checkpoint (fall/`R` returns to it).
- Exactly **one route** is open at a time; each district is entered once. No laps, no
  backtracking requirement.

## Build v2 note (after playtest: "there is no path")

v1 scattered independent slabs and the route was unreadable — scrapped, not patched.
v2's path is **one designed centreline per district** (`route.js`, unit-tested):
drawn as a continuous kerbed deck with centre dashes, distance posts every 50 m, a
live minimap and a metres-to-portal readout. `route.test.mjs` proves the centreline
is continuous, District I exceeds 470 m, neighbouring pieces drift together (<2 cm
relative motion), and a simulated pure-pursuit drive with the game's own kerb clamp
reaches every portal without leaving the deck.

## Greybox construction notes (this build)

- Everything is untextured boxes: `slabs[]` are drivable (the car samples the *same*
  animated slab poses the renderer draws, so the moving path is exactly collidable —
  nothing is faked), `boxes[]` are scenery, `movers[]` are furniture/rubble adrift.
- Fractured houses are procedural: a main block, a sheared corner chunk leaning 2–3°,
  a dark "split cap" across the crack, and shards hovering off the break. `fracture`
  can later swap these for the Fracture system's real dynamic fragments (in the main
  prototype) — the greybox poses are authored so that swap needs no layout change.
- Seeded RNG: the town is identical every visit, so route timings are comparable.

## Where this goes next (not in greybox v1)

1. Port the districts into the Frontier prototype as a `?level=fragmented` scene:
   slabs/houses become occupants (dynamic occupants already exist there), roofs get
   surface cards, and the path picks up real lighting/shadows instead of flat shading.
2. More districts behind the far-shore gate (the design rule travels: a flooded
   mill-town, a quarried cliff village — always ordinary places, broken ordinarily).
3. Moving-path *gameplay* variety: one district where a whole street segment slowly
   ferries sideways (same overlap/phase rules, larger amplitude, still fair).

## Greybox sign-off checklist

- [x] Route is point-to-point, portal to portal — no laps
- [x] District I alone drives 30+ s (500 m class route, simulated drive reaches Portal 1)
- [x] Path is town-stuff (roads + roofs), visibly fractured, gently moving, never breaks
- [x] Furniture & rubble adrift; buildings fractured (and fracture-ready)
- [x] Tone: stone/brick/slate/timber only — zero neon/cyberpunk
- [x] Fall recovery + per-district checkpoints; completion screen with route time
