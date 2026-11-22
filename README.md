# FRONTIER — *The Canopy*

A AAA-scale design package for a forward racing game in which a living jungle
canopy grows vines into your line, grapples the car, drags it off the road,
and has to be answered with driving.

Everything here is new work: the design package, the concept art, and a
playable vertical slice that runs the mechanic for real.

---

## 1. The mechanic in one paragraph

You drive forward down a natural jungle corridor. There are no lanes. The
canopy above the trail grows vines that hang across the line; when one touches
the car it sticks, and it pulls — hard, harder, hardest depending on the
species. You answer it with the car you are driving: a cutter pulse on the
bumper blade, speed, a line change, a scrape against a tree, or the boost.
That is the whole game.

---

## 2. What is in the package

| Path | What it is |
|---|---|
| `docs/00-overview.md` | The pitch, the pillars, the core loop, the control vocabulary, the three failure states, the feedback rules |
| `docs/01-vine-system.md` | Vine anatomy, the six species, lifecycle, growth, latch, tension, creep, peel, breaking, the director, tuning, counterplay |
| `docs/02-vehicle-and-track.md` | The vehicle, suspension, tyre model, powertrain, aero, steering, the solver, surfaces, the procedural corridor, props, QA targets |
| `docs/03-art-audio-camera.md` | Art pillars, procedural vine rendering, environment and weather, vehicle and VFX language, camera behaviour, the audio mix, haptics, readability QA |
| `docs/04-technical-and-production.md` | Module ownership, threading and frame flow, data structures, the growth and constraint algorithms, engine mapping, performance budgets, tooling, roadmap, risks, accessibility |
| `art/concepts/*.png` | Four AAA concept frames: the hero grapple, the tendril contact macro, the corridor, the canopy threat |
| `prototype/` | The playable vertical slice (TypeScript + Three.js + Vitest) |

---

## 3. Running the prototype

```bash
cd prototype
npm install
npm run dev      # http://localhost:5173
npm test         # simulation test suite
npm run build    # type-check + production bundle
```

### Controls

| Key | Verb |
|---|---|
| `W` / `↑` | Throttle |
| `S` / `↓` | Brake — hold at a standstill to reverse |
| `A` `D` / `←` `→` | Steer |
| `Space` | Handbrake |
| `E` | Cutter pulse — the blade sweeps across whatever is latched to the front of the car |
| `Shift` | Boost |
| `R` | Recover to the trail |
| `M` | Mute |

### What to look for

- The corridor is a single continuous ribbon carved through uneven ground.
  Width, banking, surface and elevation drift continuously — there is no lane
  structure anywhere.
- Vines **grow** out of the canopy ahead of you, hang over the line, and grab
  the car when the tip touches the bodywork. They pull with real force at the
  real contact point, so the car is genuinely dragged.
- The HUD shows tension as a percentage of the vine's failure load, plus the
  canopy pressure, the number of latches, cutter cooldown and boost charge.
- Loose stones and rotten logs can be shoved aside with momentum transfer.
  Boulder fields cannot: you have to pick a line.

---

## 4. Status

- Design package: complete.
- Concept art: four frames.
- Vertical slice: playable, with a headless simulation test suite
  (track, vehicle, vine physics, cutter, director, a two-minute soak and a
  scripted play session).
