# Frontier — Vine Grapple System
## Design Overview & Pillars

> Working title of the mechanic: **THE CANOPY**. It is the game's antagonist, its
> difficulty curve, its visual signature, and its replayability engine — all at once.

---

## 1. One-paragraph pitch

You drive a stripped-down jungle racer down a living, overgrown forest corridor.
The forest is not scenery — it is the opposition. Above you, the canopy *reaches
down*. Vines grow, aim, latch onto your vehicle, and try to drag you out of the
line. You feel the weight of the jungle on your chassis: a pull on the wheel, a
yaw you did not ask for, a front end that suddenly won't turn. You answer with
body English — weight transfer, a Scandinavian flick, a cutter pulse on the front
bumper, a boost to stretch a brittle tendril until it snaps. Deeper into the run
the canopy gets smarter, and the jungle starts winning.

---

## 2. The five design pillars

Every mechanic, asset, and tuning decision in this package traces back to one of
these. If a proposal violates a pillar, it does not ship.

### P1 — **The forest aims, the player reacts.**
Vines are never random. They grow toward you, telegraph their intent, and commit
at a readable moment. The player is never hit by something they could not have
seen. Randomness creates unfairness; **anticipation creates drama**. Every vine
has a *tell* phase, a *commit* phase, and a *resolve* phase, and the three phases
are visually and audibly distinct.

### P2 — **Weight is the language.**
The vine system is not "the car gets slow". It is *something is pulling on a
specific point of a specific body, and the chassis responds the way a chassis
does* — with yaw, roll, load transfer, traction loss, and inertia. A vine on the
rear axle is not the same as a vine on the nose. Players must feel the difference
and steer the difference away.

### P3 — **Every threat has a verb.**
No vine is a dead end. Each species has a designed answer that is *learnable*,
and each answer is expressed through vehicle physics the player already owns:
throttle, brake, steering, weight transfer, and the cutter pulse. If a player
cannot describe what they did to escape, the design has failed.

### P4 — **One continuous natural corridor, not three lanes.**
The track is a spline-driven ribbon of variable width, banking, surface and
elevation — a *place*, not a track. Difficulty comes from the geometry of the
forest (chokepoints, root shelves, mud shelves, canopy cells), not from painted
lane boundaries. The player steers freely in a space, not across a state machine.

### P5 — **Diegetic everything.**
No HUD arrows for vines. No "VINE ALERT" popup. The danger is in the canopy, in
the sound of the canopy, in the way the light falls. The only abstract UI is the
one the player needs to make a split-second decision: *how much am I being held,
and how long have I got*.

---

## 3. Core loop

```
   READ            COMMIT              RESOLVE              PUNISH
 ────────────────────────────────────────────────────────────────────────────
 canopy cell      vine extends,       vine latches,       player escapes
 density rises    aims at player      pulls car off-line   → canopy escalates
 → audio cues     → tip unrolls       → tension builds    → new species
 → light shifts   → 0.6–1.1 s tell    → counter verb      → tighter cells
```

The loop is **short-cycle** (2–6 seconds) and **long-cycle** (run-length). The
short cycle is the moment-to-moment combat; the long cycle is the escalating
forest. Both are driven by the same *Canopy Pressure* value.

### 3.1 The Canopy Pressure curve (long cycle)

`Canopy Pressure (0 → 1)` is the master difficulty dial. It scales vine spawn
density, vine aggression, growth speed, species mix, and how tight the forest
corridor gets.

| Distance | Pressure | Name | New behaviour unlocked |
|---|---|---|---|
| 0 – 400 m | 0.00 – 0.12 | **Canopy Edge** | Single tendrils. Wide corridor. Pure teaching. |
| 400 – 900 m | 0.12 – 0.30 | **Understory** | Second tendril per cell; anchor vines appear. Corridor narrows to ~12 m. |
| 900 – 1600 m | 0.30 – 0.52 | **Deep Canopy** | Snare nets; canopy closes overhead; light drops 40 %. |
| 1600 – 2500 m | 0.52 – 0.76 | **Jungle Core** | Claw pads; paired simultaneous latches; mud shelves. |
| 2500 m + | 0.76 – 1.00 | **Blight** | Vines mutate: faster, thinner, multi-tip; two-cell ambushes. |

Pressure also drives a **resource**: the player can spend "Kite" (a currency
dropped by escaped vines) to push pressure down locally — this is the player's
agency over the ramp, and the reason replays diverge.

---

## 4. Player verbs (the entire control vocabulary)

| Verb | Input | Feel intent |
|---|---|---|
| **Steer** | A/D or ←/→ | Weighty. Speed-sensitive limit. Never instant. |
| **Throttle / Brake** | W/S | Engine torque curve; braking does weight transfer. |
| **Handbrake** | Space (alt) | Rear traction kill for deliberate rotation — the classic vine-escape tool. |
| **Cutter pulse** | E (or LMB) | Front-roller sweeps a blade arc; severs latches in a 3.2 m forward cone. 1.6 s cooldown. |
| **Boost** | Shift | 1.4 s of extra torque; stretches brittle vines toward their break point. |
| **Weight transfer** | Emergent | Brake to load the nose, then flick — breaks a rear-axle anchor. |

There is no "dodge" button, no "grab" button, no "interact". The player has a
vehicle and five verbs. **The whole game is what those verbs feel like when the
forest grabs you.**

---

## 5. The three feedback channels (non-negotiable)

A vine event must be legible through **all three** at once, or it does not ship:

1. **Visual** — canopy bulge → unrolling tip → contact flare → tension is shown by
   *vibration frequency and shaft colour shift*, not by a bar in the corner.
2. **Audio** — a rising rustle bed that pitches up as a vine commits, a distinct
   *twang* on contact, a creak that rises in pitch with tension, a snap on break.
3. **Haptic** — a controller rumble curve that ramps with tension, distinct
   patterns per species (light rapid taps = tendril, heavy sustained = anchor).

---

## 6. The three failure states (and why none of them are "death")

| Failure | Cause | Player experience | Recovery |
|---|---|---|---|
| **Snag** | Low-speed latch; car pinned sideways | "I'm caught" — seconds of struggle | Free yourself by reversing + cutter pulse |
| **Dragged** | Anchor holds; car is pulled off-line into the trees | "The forest is winning" | Must break it (scrape a tree, or boost until it snaps) |
| **Overgrowth** | Too many simultaneous latches; speed drops below a crawl | "I'm buried" | Bleed off latches one at a time with cutter pulses while crawling |

Crucially: **the player is never killed by an unfair hit.** A vine can end a run
only by holding the car below 8 km/h for more than 6 s while the canopy pressure
keeps adding latches — at which point the failure is a *consequence of the
player's own mismanagement*, not a gotcha.

The same rule applies to the world itself. Rolling the car, wedging it on a
boulder, burying it in the bank or wandering off the trail all end with the
car being put back on the corridor - never with a restart, a score penalty or
a lost run. The player's progress through the jungle is always preserved;
only their line is taken away.

---

## 7. Why this is AAA, not a mobile runner

The brief is Subway Surfers-*shaped* — endless forward momentum, one-thumb-ish
readability — but the fantasy is **driving**, so the implementation is a
simulation, not a lane-switcher:

- The vehicle is a rigid body with a real suspension and tire model (see
  `02-vehicle-and-track.md`). Vine forces are applied *through the same solver*
  as terrain forces, at real contact points, with real torques.
- The world is a procedural spline corridor with real width, banking, elevation,
  surface types and prop collision. There are no invisible walls except at the
  treeline, where there are *actual trees*.
- The canopy is a simulation with growth, aiming, ageing and death. Vines
  persist across the run; the forest you left behind is still growing.

---

## 8. Document index

| Document | Contents |
|---|---|
| `01-vine-system.md` | Vine anatomy, six species, growth model, latch/constraint model, tension & breaking, tuning tables |
| `02-vehicle-and-track.md` | Vehicle spec, tire & suspension model, powertrain, surfaces, the natural corridor generator, feel targets |
| `03-art-audio-camera.md` | Vine rendering, canopy & environment art, lighting, VFX, camera language, audio & haptics design |
| `04-technical-and-production.md` | Module map, threading, data structures, pseudocode, engine notes, performance budget, roadmap, risks, accessibility |
| `../prototype/` | A playable vertical slice: procedural corridor, real vehicle physics, growing vines that latch and pull, cutter counterplay |
| `../art/concepts/` | Concept art establishing the visual language |

---

## 9. One-page cheat sheet for the whole mechanic

```
SPECIES     TELL                COMMIT             HOLD         BREAKS BY
────────────────────────────────────────────────────────────────────────────
Tendril     canopy bulge,      whip-lash, one     0.6–1.4 s    hard steer,
            soft rustle        tip contact        light pull   scrape, cutter
Anchor      creak from above,  slow wrap around   3–6 s        cutter, tree
            heavy sway         rear axle          heavy drag   scrape, boost
Snare       rustle wall,       many fine tines    indefinite   low speed,
            leaf storm         catch wheels       steer loss   handbrake, cutter
Claw        wet slap           suction pad on     indefinite   roof scraper,
                             flat panel         panel grip    cutter, speed
Dragline    taut horizontal   body-bar contact    instant      impact (it is a
            line overhead     speed loss         speed loss   barrier, not a
                                                                  grabber)
Liana       slow sway         canopied tangle    slow drag    cutting
                                       that snags                        hooks
```

Full numbers, states and equations: `01-vine-system.md`.
