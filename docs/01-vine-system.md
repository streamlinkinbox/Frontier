# 01 — The Vine System
### Anatomy, species, growth, latch physics, tuning

---

## 1. Vine anatomy

Every vine in the game — from a 3 cm tendril to a 40 cm strangler — is built from
the same six-part rig, so the whole system shares one growth/geometry pipeline.

```
        CANOPY ROOT                STEM / LIAISON            FREE TIP
     ═══════════════╗          ╭────────────────╮        ╭──╮
     ▓▓▓ bark, ▓▓▓▓▓▓▓▓╠════════╡  nodes (nodes  ╞════════╡  ╰─ swelling
     ▓▓▓ thick ▓▓▓▓▓▓▓▓╣  host  │  every 0.4 m  │  length │   adhesive pad
     ▓▓▓      ▓▓▓▓▓▓▓▓╚═╤══════╧═══════════════╧════════╧═════╤══
                      ╰─ host  ─╯                              │
                    attachment                             hook / barb /
                    (tree, rock,                             thorn cluster
                     canopy limb)
```

| Part | Purpose | Sim representation | Rendering |
|---|---|---|---|
| **Root** | Anchor into host (tree, rock, canopy limb) | Fixed point, infinite mass | Merged into host bark |
| **Stem / Liaison** | Load-bearing length | Polyline of nodes, mass distributed | Tube mesh, tapered |
| **Nodes** | Where creep and slip happen | Discrete mass points along stem | Bark ridge ring, slight bulge |
| **Free tip** | The business end | Polyline tip with velocity | Unrolling, glossy, wet |
| **Pad** | Adhesive contact | Attachment point on vehicle body | Sucker disc, sap sheen |
| **Hook / Thorn** | Optional secondary | Collision proxy | Hard, dark, catching light |

**Key rule:** the free tip is the only part that creates latches. The stem is a
*conduit* — it transmits tension, it does not grab. This keeps the mechanic
readable: **if it is touching you, it is the tip.**

---

## 2. The six species

Six species is the sweet spot: enough that the player must build a mental
vocabulary, few enough that each one is memorable and each has a distinct
*verb* answer.

### 2.1 TENDER TENDRIL — *the flick* (mass species, common)

| Property | Value |
|---|---|
| Mass | 0.8 – 1.6 kg |
| Stem length | 2.0 – 4.0 m |
| Stiffness `k` | 4 000 – 9 000 N/m |
| Damping `c` | 40 – 90 N·s/m |
| Break tension | 1.8 – 3.2 kN |
| Grip | Single point, low-friction pad |
| Pull characteristic | Sharp lateral snap, then a whip-y chatter |

**Behaviour.** The classic. It whips down, catches a body panel or roof edge,
and yanks. Because it is light and brittle, it breaks *easily* — the intended
answer is to steer hard or take a scrape, so most ten drils cost you a fraction
of a second and a bit of line. They are the game's punctuation, not its threat.

**Tell.** Canopy bulge → a quick unroll → a *twang* and a single-frame camera
kick on contact.

### 2.2 ANCHOR / STRANGLER — *the drag* (heavy, milestone)

| Property | Value |
|---|---|
| Mass | 9 – 26 kg |
| Stem length | 4.0 – 8.5 m |
| Stiffness `k` | 28 000 – 85 000 N/m |
| Damping `c` | 300 – 900 N·s/m |
| Break tension | 26 – 48 kN |
| Grip | High-friction wrap, 2 – 3 contact points |
| Pull characteristic | Slow, enormous, unignorable |

**Behaviour.** Anchors descend slowly and deliberately, find the rear axle or the
rocker panel, and *wrap*. The wrap is progressive: as it takes up slack, its
attachment points migrate toward the car's centre of mass, which increases the
lever arm and the yaw torque. An anchor that catches you at 60 km/h will pull the
car off-line at up to 0.35 g and hold it there. It does not stun; it *steers you
somewhere you did not choose* — usually into a tree or a boulder.

**Answer.** Braking to load the nose and then flicking unloads the rear and
breaks the wrap (the classic trail-brake escape). A tree scrape shears it in
~0.8 s. Boosting stretches it past 48 kN and it snaps with a deep woody crack.
It also takes the longest to cut — the cutter pulse takes 2.4 s of blade
contact to sever one, and the player has to survive those 2.4 s.

**Tell.** Heavy creak from above, a slow shadow crossing the hood, leaves raining
down before the vine itself arrives.

### 2.3 SNARE NET — *the tangle* (area denial)

| Property | Value |
|---|---|
| Mass | 18 – 45 kg (20 – 40 tines) |
| Stem length | 3.0 – 6.0 m |
| Stiffness `k` | 2 000 – 5 000 N/m per tine |
| Break tension | 0.6 – 1.1 kN per tine (they snap individually) |
| Grip | Many low-force points, mostly on wheels |
| Pull characteristic | Not a pull — a *loss*. |

**Behaviour.** A snare is not one vine, it is a fan of fine tines that hangs
across the corridor. It does not yank the car; it catches the *wheels*. Effect:
traction drops (slip-limited to 0.35 µ), steering response goes vague and
laggy, and a fine vibration runs through the chassis. Speed bleeds at 25–40 %/s.
The player is not being pulled — they are being *shredded of grip*, which is a
very different and more interesting problem.

**Answer.** Do not fight it in a straight line. The tines snap individually at
low tension, so (a) driving *through* at speed snaps them one by one with a
rising rattle, (b) a controlled slide (handbrake) lets them slide along the
panels and off, and (c) the cutter pulse sweeps a whole fan in one arc.

### 2.4 DEVIL'S CLAW / SUCKER PAD — *the grip* (panel fouling)

| Property | Value |
|---|---|
| Mass | 0.6 – 1.1 kg |
| Stem length | 1.5 – 3.0 m |
| Stiffness `k` | 6 000 – 14 000 N/m |
| Adhesion (normal) | 900 – 1 700 N per pad |
| Break tension | 14 – 26 kN (the *stem* breaks, not the pad) |
| Grip | Suction: resists *normal* pull hardest, shear weakly |

**Behaviour.** Claws are the inverted problem: they do not pull you, they make
you *stick*. A claw on the hood or roof reduces aero downforce (the car becomes
loose and wanders), adds a local drag, and — critically — a claw on the roof
**cancels your cutter access**, because the blade sweep is at bumper height.
Claws on the flank inhibit steering. Two claws on the same side will roll the
car in slow motion.

**Answer.** Speed. At 90+ km/h the airflow and the shear force peel the pad and
it releases with a wet slap. Or a roof-roller upgrade. Or the cutter pulse,
which requires you to be *beside* the claw (it sweeps the front arc), forcing
the player to deliberately line the claw up with the bumper — a nice positional
skill moment.

### 2.5 DRAGLINE — *the barrier* (a hazard, not a grabber)

| Property | Value |
|---|---|
| Mass | 3 – 12 kg |
| Stem length | spans the corridor, 6 – 20 m |
| Stiffness | 40 000 – 120 000 N/m (pre-tensioned) |
| Break tension | 9 – 18 kN |
| Behaviour | Impact-absorbing; does not latch |

**Behaviour.** A taut horizontal line across the trail. Hit it and it *snaps*,
costing 18 – 30 % of speed and throwing a leaf-and-wood explosion. It exists to
force braking zones and to punish players who never lift. If you hit a dragline
slowly, you may instead get **wrapped** — the liana variant (see 2.6).

**Answer.** Brake, or hit it fast (a fast impact breaks it more cheaply in
speed terms), or slide under it if it hangs low.

### 2.6 LIANA / CANOPIED TANGLE — *the slow one* (ambush)

| Property | Value |
|---|---|
| Mass | 6 – 15 kg |
| Stem length | 5 – 12 m, many branches |
| Stiffness | 12 000 – 30 000 N/m |
| Break tension | 16 – 30 kN |
| Behaviour | Wraps broadly, drags slowly, snags on roll cage |

**Behaviour.** A large mass of vine that descends in a curtain and snags
everything it touches — bumpers, mirrors, the cage. It does not pull hard; it
*fouls*. Speed drops to 40–65 %, steering gains a constant bias, and it holds on
until the player works it off. LiAna are the "climbing" vines: they are how the
forest looks like it is *taking* the vehicle.

**Answer.** Cutter pulse, tree scrape, or just drive it out — a liana will
usually shed itself after 60 – 110 m of driving if you keep the throttle on.

---

## 3. Lifecycle state machine

Every vine instance runs this machine. It is the single source of truth for
rendering, audio, and simulation.

```
                      ┌──────────────┐
                      │   DORMANT    │  seeded in canopy, invisible to player
                      └──────┬───────┘
                             │ germination trigger (see §4)
                      ┌──────▼───────┐
                      │ GERMINATING  │  0.8–1.4 s: canopy bulge, root swells
                      └──────┬───────┘
                      │ growth begins, tip unrolls
                      ▼
                  ┌───────────────┐
        ┌────────►│   EXTENDING   │  tip grows toward a target point
        │         └───────┬───────┘
        │                 │ target reached & player committed
        │         ┌───────▼───────┐
        │         │    MATURE     │  hangs, sways, may re-target
        │         └───────┬───────┘
        │                 │ tip contact with vehicle
        │         ┌───────▼───────┐
        │         │   LATCHING    │  0.15 s: adhesion ramp, first creak
        │         └───────┬───────┘
        │                 │ pad seated
        │         ┌───────▼───────┐
        │         │   LATCHED     │◄──── creep (tip migrates along body)
        │         └───┬───────┬───┘
        │             │       │
        │   ┌─────────▼──┐  ┌─▼──────────┐
        │   │  CREEPING  │  │  TENSIONED │  strain > break threshold
        │   └─────┬─────┘  └─┬──────────┘
        │         │            │
        │         │      ┌─────▼─────┐     ┌──────────┐
        └─────────┴──────┤   BREAKING │────►│  BROKEN  │ (snapped, falls)
                       └────────────┘     └──────────┘
                              │
                              ▼
                        ┌──────────┐
                        │ WITHERED │ (tension released, vine retracts & dies)
                        └──────────┘
```

**State timings are the readability budget.** A vine spends at least
`0.8 s (germinate) + 1.2 s (extend) + 0.4 s (tell) = 2.4 s` of visible life
before it can hurt you. At 60 km/h (16.7 m/s) that is 40 metres of warning.
Tunable, but it must stay above **2.0 s** at every difficulty, and the *tension
phase* must be long enough for the answer to be usable (see §7.3).

---

## 4. Growth model

The brief asks that vines be able to **grow**. Growth is the core simulation:
vines are not placed, they are *cultivated*, and the player's own behaviour
cultivates them.

### 4.1 Triggers

A DORMANT vine germinates when a **germination score** exceeds a threshold:

```
 score = w1·playerThermalSignature
       + w2·playerProximity
       + w3·canopyMoisture
       + w4·canopyPressure
       + w5·seedRandom
```

| Term | Meaning | Weight |
|---|---|---|
| `playerThermalSignature` | Engine heat + tire slip energy radiated upward, sensed within 8 m of the canopy node | 0.30 |
| `playerProximity` | Inverse distance from the player to the canopy node | 0.22 |
| `canopyMoisture` | Biome humidity (rain cells increase it 3×) | 0.12 |
| `canopyPressure` | The global difficulty dial | 0.26 |
| `seedRandom` | Per-node hash, so not every node fires at once | 0.10 |

**The important consequence:** *driving aggressively heats the car, which makes
the canopy grow faster.* This is the systemic reward the game runs on. A player
who drives smooth and cool keeps the canopy passive; a player who floors it,
slides and rips through everything wakes the forest up. The difficulty curve is
therefore partly authored by the player.

### 4.2 Extension — the vine hangs where you are going to be

Once germinating, the vine unrolls from its canopy root toward an
**interception point**, recomputed each frame.

The root hangs over a fixed *station* of the trail (the director seeds it
30–110 m ahead, scaled by the player's speed). The interception point is where
the player will be when they reach that station:

```
 gap      = rootStation − playerStation                 (metres of trail ahead)
 lead     = clamp(gap / playerSpeed, 0.15 s, 4 s)
 lateral  = playerLateral + playerLateralVelocity · lead
 target   = trailPoint(rootStation, lateral) + trailUp · 0.34 m   (car height)
 target   = clamp(target, root + maxReach)             (a vine cannot stretch)
```

and the tip moves toward it with:

```
 tipSpeed = baseSpeed(species) · growthGate
 growthGate ∈ [0,1] — ramps 0→1 over the first 0.6 s of extension
```

Base extension speeds (prototype values): tendril 7.0 m/s, claw 6.2 m/s, snare
fan 4.8 m/s, anchor 3.2 m/s, liana 2.8 m/s, dragline 2.1 m/s.

Two properties fall out of this model, and both matter:

- **The vine is already there when you arrive.** It stops chasing the car and
  hangs, swaying, over the racing line. The player sees a vine *waiting*, not a
  vine chasing — the anticipation is legible for a full second before contact.
- **Speed is the counterplay.** A vine's aim point is a function of the
  player's speed and line. Braking hard, or changing line at the last moment,
  moves the interception point; that is why the cut, the swerve and the brake
  all work as answers to the canopy.

**Touching the vine is the grab.** Contact is tested every frame in every
active state, not only when the tip arrives: the tip sphere versus the body
proxies. The moment the tip overlaps a proxy, the vine latches — there is no
separate "grab" input and no grace window. That is the rule the player has to
internalise: *do not touch the vine.*

### 4.2b Lifecycle timings (prototype)

| Beat | Time |
|---|---|
| Germination delay after seeding | 0.1 – 0.9 s (scaled down by pressure) |
| Germinating → extending | 0.55 s |
| Extending → mature (hanging) | when the tip is within 1.6 m of the interception point |
| Old vines retired behind the player | 100 m behind the station |
| Latch → full adhesion | 0.15 s |
| Creep starts | 1.2 s after latch |

### 4.3 Ageing and death

Vines are transient. A vine that never germinates dies after 25 – 40 s. A broken
vine retracts over 1.2 s and its node returns to DORMANT for 8 – 14 s (it can
regerminate later, but the canopy must be "re-seeded" — a small persistent
resource so the forest cannot infinitely spam). Nodes are a **finite resource
in the canopy**: 18 – 46 nodes per canopy cell, and cells are consumed as the
player passes through them. This is why the forest does not simply become a wall
of vines and why the player can *see* the density rising and falling ahead.

---

## 5. Latch model — how a vine grabs a car

### 5.1 Contact

The vehicle carries a set of **collision proxies** (capsules/boxes): 4 wheels,
2 rockers, front bumper bar, roof hoop, tailgate, and 2 mirrors. The vine tip is
a sphere of radius 4 – 9 cm (species dependent) plus a swept segment of its
last 3 nodes (so a fast whip cannot tunnel through a thin proxy).

Contact test: tip-vs-proxy, resolved in the physics step, not in the render loop.

### 5.2 Adhesion ramp

On first contact the vine enters LATCHING for **0.15 s**. During that window
the grip ramps `0 → 1` with a smoothstep, so the hit is a *slam* rather than a
binary flag flip. The player feels the impact build.

### 5.3 The constraint

A latched vine creates an **adhesion constraint** between the vine's tip node
and a point on the vehicle body, expressed in the vehicle's local frame:

```
 anchorLocal   : vec3   (position in body space)
 restLength    : f32    (vine length at the moment of latch)
 stiffness k   : f32    (species)
 damping c     : f32
 maxTension    : f32    (species, break threshold)
 adhesionMode  : enum   { GRIP (friction pad), SUCTION (normal-strong), WRAP (multi-point) }
```

Each physics step the constraint computes:

```
 r        = worldPoint(anchorLocal) − vineTipWorld
 len      = |r|
 dir      = r / len
 stretch  = len − restLength
 velRel   = (vehicleVelocityAtPoint − vineTipVelocity) · dir
 tension  = max(0, k·stretch + c·velRel)
```

The tension is applied **to the vehicle at the contact point** (linear) and
**about the body's CoM** (angular). The reaction is applied to the vine, which
then updates its shape (the vine visibly stretches and the tip slides along the
body).

For `WRAP` mode the vine maintains up to 3 contact points around the body; the
effective pull is the sum, and the effective lever arm grows as the contacts
migrate inward — this is the "anchor wraps and drags" behaviour.

### 5.4 Grip, slip, and creep

The constraint also resists **tangential** motion:

```
 frictionCapacity = μ_grip · normalForce
 shearForce       = clamp(tangentialSpringForce, ±frictionCapacity)
```

If the vehicle moves fast enough tangentially, the contact **slips**: the
anchorLocal migrates along the surface in the direction of travel, and the
friction force is scaled down. This is why a fast-moving car can *slide a vine
along its flank* and shed it — and why a slow-moving car gets *stuck* to it.

**Creep** (LATCHED → CREEPING) is the same mechanism run in reverse at a slow
rate: the anchor migrates *toward* the vehicle's centre, shortening the effective
lever and increasing drag. Creep rate: 0.08 – 0.25 m/s depending on species.
A vine left latched for 6 seconds is meaningfully worse than one latched for 1
second. **This is the timer that drives the whole counterplay.**

### 5.5 Breaking

```
 if tension > maxTension:  → BREAKING (0.05 s), then BROKEN
 if contact lost (tip separated > 0.35 m): → WITHERED
 if player cuts it: → BROKEN
```

Some species fail differently:
- **Snare tines** break individually at low tension (0.6 – 1.1 kN), so the net
  degrades rather than vanishing.
- **Anchors** don't snap; they *shear at a node*, which means the vine stays
  wrapped but loses its tension — visually the vine recoils with a woody snap.
- **Claws** release their pad (wet slap) and the stem whips away.

---

## 6. Effect on the vehicle — the numbers that matter

The car is 1 450 kg with a 2.95 m wheelbase and 1.68 m track. Here is what each
species does at a typical 22 m/s (≈80 km/h) approach.

| Species | Peak force | Resulting accel | Yaw torque (rear axle) | Steering bias | Speed effect |
|---|---|---|---|---|---|
| Tendril | 2.6 kN | 0.18 g lateral | 1.4 kN·m | +12° toward vine | −4 % |
| Anchor | 34 kN | 0.24 g (mostly drag) | 18 kN·m | +28° | −35 % |
| Snare | 6 kN total | ≈ 0 | — | lag, 60 % gain | −30 %/s bleed |
| Claw | 1.4 kN normal | 0 | — | −6° (stick) | −8 %, −25 % downforce |
| Liana | 12 kN | 0.09 g | 9 kN·m | +18° constant | −45 % |

The **steering bias** is the key gameplay-readable quantity. The player feels
the wheel being pulled and must decide: fight it (cost speed), cut it, or let it
drag you into a clear line. Anchors are the only species that can physically
move the car off-line on their own — and they are also the only species that
gives the player a big *satisfaction* moment when it breaks (the deep crack,
the recoil, the snap-back whip over the roof).

---

## 7. Counterplay matrix

| Species | **Cutter pulse** | **Tree/boulder scrape** | **Boost** | **Handbrake slide** | **Brake + flick** | **Speed** | **Position** |
|---|---|---|---|---|---|---|---|
| Tendril | ✔ instant | ✔ 0.3 s | ✔ moderate | ✔ (flings it off) | ✔ (unloads rear) | ~ | avoid the anchor side |
| Anchor | ✔ 2.4 s (slow) | ✔ 0.8 s (best) | ✔ snaps at 48 kN | ✖ | ✔ (best) | ~ | keep it off the rear? no — force it onto the rear for the flick |
| Snare | ✔ sweeps the fan | ✔ | ~ | ✔ (best) | ~ | ✔ (snaps tines) | keep wheels out of tines |
| Claw | ✔ if lined up | ✔ | ✔ peels pads | ~ | ~ | ✔ (peels pads) | roof/bonnet clean |
| Dragline | ✖ | ~ | ~ | ~ | ~ | ✔ (cheap) | — |
| Liana | ✔ | ✔ | ~ | ~ | ~ | ~ | — |

The matrix is deliberately **not** dominated by a single universal answer. The
cutter pulse is the general tool but has a 1.6 s cooldown and a forward cone,
so it cannot save you from a rear anchor; each species demands a different
piece of driving vocabulary.

---

## 8. Cutter pulse — specification

The cutter is a front-mounted **hydraulic roller with a blade arc** — a piece of
the vehicle, not a magic button.

| Property | Value |
|---|---|
| Coverage | 140° arc, 3.2 m forward cone from the bumper |
| Sweep time | 0.28 s out, 0.22 s back |
| Cooldown | 1.6 s |
| Effect | Severs latches whose contact point lies in the arc; cuts draglines in the arc |
| Damage | Cuts a snare fan instantly; takes 2.4 s of contact time on an anchor (blade bites progressively — visible sparks/green chips) |
| Feedback | Steel ring, hydraulic hiss, blade chatter on contact, camera FOV punch of −4° for 0.2 s |
| Upgrade path | +0.6 m radius / −0.4 s cooldown / auto-cut radius 1.5 m (Tier 3) |

The cutter also has a **cost**: it can only cut what is *in front of you*. So
"turn the car so the vine is behind you" is a real, taught, satisfying play.

---

## 9. Director — how the forest is staged

A single system owns vine placement so that difficulty is *directed*, not random.

**Canopy cells.** The corridor is partitioned into 40 – 70 m cells. Each cell has
a canopy density (from the global pressure curve, plus local modifiers like
"recently burned", "fallen tree here", "dense liana curtain") and a node budget.

**Spawn rules.**
1. A cell never spawns more than `2 + 3·pressure` vines at once.
2. A vine may only spawn in a cell the player is **approaching** (within 60 m),
   so nothing appears behind the player.
3. Spawn positions are biased to the **outside of the upcoming corner** so that
   latches pull the car *inward* toward the line at first — then the vine's own
   weight swings it outward. Reading that double-pull is expert play.
4. Never two anchors on the same side within 25 m.
5. At pressure > 0.5, a "pincer" pattern may be staged: one anchor on the
   outside (the dangerous one) with a cheap tendril on the inside (the
   distraction). The correct play is to ignore the cheap one.

**Guarantees.** The director guarantees a **minimum of 1.8 s of clear line**
between committed vines at any pressure, and never blocks the full corridor
width with latches unless the player is already under 40 km/h.

---

## 10. Tuning tables

### 10.1 Species parameters (authoritative)

| | Tendril | Anchor | Snare (per tine) | Claw | Dragline | Liana |
|---|---|---|---|---|---|---|
| Mass (kg) | 1.2 | 18 | 0.35 | 0.85 | 7 | 11 |
| Length (m) | 3.0 | 6.2 | 4.5 | 2.2 | 12 | 8.5 |
| Stiffness k (N/m) | 6 500 | 55 000 | 3 500 | 9 500 | 80 000 | 20 000 |
| Damping c (N·s/m) | 65 | 600 | 45 | 120 | 900 | 320 |
| Max tension (kN) | 2.6 | 34 | 0.85 | 18 (stem) | 14 | 24 |
| Grip μ | 0.55 | 0.85 | 0.35 | 1.2 (suction) | — | 0.75 |
| Hold time @ full load (s) | 0.9 | 4.2 | n/a | ∞ | — | 2.6 |
| Creep rate (m/s) | 0.10 | 0.22 | — | 0.06 | — | 0.16 |
| Extension speed (m/s) | 4.2 | 1.5 | 2.4 | 3.0 | n/a | 1.2 |
| Spawn weight (P=0) | 1.00 | 0.00 | 0.00 | 0.00 | 0.15 | 0.00 |
| Spawn weight (P=1) | 0.34 | 0.22 | 0.18 | 0.16 | 0.06 | 0.10 |

### 10.2 Global difficulty multipliers

| Parameter | P=0 | P=0.5 | P=1 |
|---|---|---|---|
| Extension speed × | 0.62 | 0.85 | 1.00 |
| Lead time (s) | 0.90 | 0.72 | 0.55 |
| Vines per cell | 1.2 | 2.6 | 5.0 |
| Stem stiffness × | 0.85 | 1.00 | 1.15 |
| Max tension × | 0.80 | 1.00 | 1.20 |
| Telegraph window (s) | 2.9 | 2.4 | 2.0 |
| Corridor width (m) | 18 | 13 | 8 |

### 10.3 Feel targets (measurable)

| Metric | Target |
|---|---|
| Time from vine visible to first contact | 2.0 – 3.4 s |
| Time from contact to counter-window opening | ≥ 0.6 s |
| Time to sever one tendril with cutter | 0.30 s |
| Time to sever one anchor with cutter | 2.4 s |
| Player input latency from contact to steer response | < 120 ms |
| Maximum yaw rate while latched | 42 °/s (below "spinning" threshold) |
| Recovery time after a full anchor hold | 3.5 – 5.0 s |

---

## 11. Anti-patterns (explicitly rejected designs)

These were considered and rejected. They are listed so nobody re-proposes them.

| Rejected | Why |
|---|---|
| Vines that simply "slow the car down" | No player agency, no learning, reads as a debuff. |
| Vines that attach to an invisible point at the car's centre | Feels arbitrary; players cannot reason about it. Contact must be at a real point. |
| Random vines with no telegraph | Unfair, unreadable, and makes the canopy noise instead of signal. |
| Vines that kill the player instantly | Breaks the "you are a driver" fantasy and the replay loop. |
| A single "vine meter" that shows everything | Kills the physicality; the player should read the *car*, not the UI. |
| Vines that follow the car after release | Removes the "escape" beat entirely; a released vine must feel like a victory. |
| Cartoon, rubbery, bouncy vines | Off-tone for a grounded racing game. The forest is heavy. |
| Lanes / rails / lane-change snapping | Contradicts P4. The corridor is continuous. |
