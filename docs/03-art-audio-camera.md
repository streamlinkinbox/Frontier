# 03 — Art, Audio, Camera

---

## 1. Art direction pillars

1. **Heavy, wet, alive.** Nothing in this forest is dry. Vines are wet; the air
   is heavy; the light is filtered through a million leaves. The player's car is
   the only hard, dry, manufactured object in frame — that contrast is the
   visual identity of the game.
2. **Readable at speed.** The forest is dense, but every gameplay-critical
   shape (vine tip, canopy bulge, boulder edge, mud patch) is *silhouetted
   against a contrasting background* by the lighting design. Art serves the
   mechanic first, mood second.
3. **Growth is the animation.** The forest is never static. Vines unroll, leaves
   turn, spores drift, and the canopy breathes. Stillness reads as "cheap".
4. **No cartoon.** Real proportions, real grime, believable material response.
   The reference is *Planet of the Apes* (1968) forest craft meets modern
   racing-game rendering.

**Palette.** Deep desaturated greens (#1E3A2A, #2F5C3E) as the field; wet
highlights in #7FB069 and #C6D66B; warm sunlight shafts in #E8C37A; the vehicle
in oxidised orange #C1552B with grey primer patches; the only pure saturated
colour in the game is the **cutter blade's** safety orange and the **HUD amber**
— so the eye learns that amber = the player's tools.

---

## 2. Rendering the vines

### 2.1 Geometry

Vines are **procedurally generated tube meshes** rebuilt each frame from the
simulation polyline.

| Property | Approach |
|---|---|
| Sweep | Parallel-transport frames along the polyline (no twist popping) |
| Radius profile | `r(t) = r_base · (1 − 0.55·t)^0.7 · (1 + 0.12·sin(11·t·π))` — taper plus organic swell |
| Radial segments | 6 (LOD0) / 4 (LOD1) / 2 (LOD2), 16–40 rings by length |
| Twist | 1.1 rad/m for the liana, 0.3 for the tendril — twist sells "fibrous" |
| Bark detail | Normal map, triplanar-free (tubular UVs: U = arc length × 4, V = ring) |
| Nodes | A 12 % radius bulge every 0.4 m with a darkened ring in the albedo — the visual "weak point" where an anchor shears |
| Tip | Separate material: darker, wetter (roughness 0.12), a swollen glossy cap, and a micro-droplet normal scroll |

### 2.2 Material

A two-layer shader:
- **Base:** bark/green skin, albedo from a scanned vine atlas (4 variants ×
  hue shift), roughness 0.55–0.85 with a wetness mask driven by a global
  `humidity` uniform.
- **Translucency:** a wrap-diffuse + back-scatter term (the classic foliage
  translucency) so thin tendrils glow when backlit by sun shafts. This is what
  makes a vine *readable in silhouette* against bright canopy — it is a gameplay
  readability feature, not decoration.

### 2.3 Growth rendering

| Phase | Visual |
|---|---|
| Germinating | The canopy node bulges; the bark splits with a pale seam; a tiny leaf cluster unfurls |
| Extending | The tip unrolls like a paper scroll — the geometry tip is a **cone of decreasing radius** that straightens as it extends, with a slight lag/overshoot |
| Mature | The vine hangs and sways with a 2-band wind noise; it *searches* (small involuntary sweeps of ±8°) |
| Latching | Contact flare: a burst of sap droplets + a leaf-shed puff + a spiderweb-thin adhesion filament |
| Creeping | The wrap visibly tightens; the vine's surface stretches (a stretch-driven normal-map stretch, like real bark cracking) |
| Breaking | Splintered fibre at the break point, a recoil whip (critically damped spring over 0.4 s), falling debris |

### 2.4 LOD & budget

| LOD | Distance | Geometry | Shadows |
|---|---|---|---|
| 0 | < 25 m | Full tube, 6 radial, 40 rings | Cast + receive |
| 1 | 25–60 m | 4 radial, 20 rings | Cast (coarse) |
| 2 | 60–120 m | Crossed billboard + normal map | No |
| 3 | > 120 m | Merged into the canopy card cluster | No |

Target: ≤ 60 live vine LOD0 instances, ≤ 220 total vines. Vines beyond 120 m
are folded into the static canopy mesh entirely.

---

## 3. Environment art

### 3.1 The corridor

The road ribbon is a **continuous mesh**, not a strip of quads: 1.2 m rings along
the spline, lateral resolution 0.5 m in the drivable band, expanding to 2.5 m
out to the treeline. Vertex attributes carry surface blend weights, so the
transition between dirt → mud → leaf litter is a shader blend, not a texture
seam.

The treeline is where the corridor ends: a **wall of vegetation** (trunks, buttress
roots, ferns, hanging lianas) that is a real collision boundary. The player can
see exactly where the world stops. There are no invisible fences.

### 3.2 Scatter

| Element | Technique | Count on screen |
|---|---|---|
| Trees | Trunk mesh + instanced leaf clusters, 3 species | 120 |
| Ferns / undergrowth | Alpha-tested cards, wind-animated | 1 800 |
| Canopy leaves overhead | Alpha cards with a canopy depth-mask | 900 |
| Rocks | Noise-displaced icosahedra, 4 base meshes, 7 rotations | 260 |
| Hanging lianas (decorative) | Instanced tube strips | 90 |
| Spores / dust motes | GPU points, drifting | 4 000 |

### 3.3 Lighting

- **Sun:** directional, 4 200 K, with a **cascaded shadow map** (3 cascades,
  2048², 12 m / 45 m / 140 m). The cascade splits are snapped to texel
  increments to stop shadow shimmer while driving.
- **Canopy occlusion:** the canopy is a real occluder; dappled light on the
  road is produced by a **projected canopy cookie** (a scrolling, animated
  texture that multiplies the sun), not by per-leaf shadow casters (too
  expensive). The cookie animates with the wind so the dapple moves.
- **Ambient:** a hemispheric term, sky #6E8FA8 / ground #23301F, plus a cheap
  4-tap AO on the road (pre-baked into vertex colours at generation time).
- **Volumetrics:** screen-space god rays from the sun, plus 30–60 instanced
  light-shaft volumes near canopy gaps, camera-facing, additive.
- **Exposure:** auto-exposure with a 1.5 s adaptation, biased to keep the
  canopy readable when the player enters a dense cell.

### 3.4 Weather

Rain cells are a first-class environmental system, because wetness changes the
vine behaviour (slipperier pads, faster growth) *and* the driving (less grip):

- Rain: GPU particle streaks + screen-space droplet overlay on the camera lens +
  ripple decals on puddles + wetness on all materials (roughness drop).
- Rain reduces grip by up to 18 %, makes vine pads 25 % weaker (easier to
  break) but also makes them stick harder to panels (adhesion +20 %).
- Rain cells are telegraphed by sound and by a darkening of the canopy.

---

## 4. The vehicle's look

- **Dirt accumulation** is a shader mask driven by the surface the car is on
  (mud spatters the lower body, rain streaks the glass, leaves stick to the
  bonnet).
- **Vine interaction** leaves physical evidence: a pad on the bonnet leaves a
  wet, sap-stained circle; a wrapped anchor leaves compression bands in the
  paint and torn leaves clinging to the cage; after a run the car should look
  like it fought a garden.
- **Damage** is cosmetic only (scratches, dents). No health bar — the forest
  doesn't damage you, it *holds* you.

---

## 5. VFX

| Event | Effect |
|---|---|
| Vine extends | A 5 cm puff of dust/spores from the canopy node |
| Vine contacts car | Sap droplet spray + 8–14 leaf shards + a bright specular flash on the contact point |
| Vine tensioned | Vibration of the vine mesh (amplitude ∝ tension) + a subtle chromatic edge on the vines only (not the world) |
| Vine breaks | Fibre snap + splinter burst + the recoiling vine whipping overhead (audio-synchronised) |
| Cutter fires | Steel ring + hydraulic hiss + a 0.15 m blade arc ribbon + sparks on hard contact |
| Wheels in mud | Continuous spray of mud clods with correct velocity inheritance |
| Wheels on leaf litter | Leaf particles + a rustle bed |
| Impact with boulder | Dust plume + camera shake + debris + a low-end thud |
| Jump / landing | Speed lines, suspension compression, dust ring |

---

## 6. Camera language

| Situation | Camera behaviour |
|---|---|
| Normal driving | Chase, 5.2 m back / 2.1 m up, spring-damped (ω=9, ζ=0.85), look-ahead biased into the corner by 0.35 × steering |
| Speed | FOV 78° → 92° over 0–150 km/h, plus a 0.4 % radial stretch above 110 km/h |
| Latch (tendril) | 60 ms hitstop, 0.18 s shake at 14 Hz decaying, FOV punch −3° |
| Latch (anchor) | 120 ms hitstop, 0.45 s low-frequency shake, camera drops 0.15 m as if the car sags |
| Creep | A slow 0.2 Hz sway, amplitude ∝ creep rate, so the player *feels* the vine tightening |
| Break | 0.25 s whip — the camera yaws 4° toward the released vine, then settles |
| Airtime | Pull back 0.8 m, FOV +5°, subtle roll to match the car's attitude |
| Driver look | Right stick / mouse look, 70° yaw, 40° pitch, with a rubber-band return |

The camera **never** takes control of steering, and its shake never exceeds
2.5° of rotation — it communicates, it does not nauseate. (A "reduced shake"
option is available and defaults to on if the accessibility profile detects
motion sensitivity via the OS setting.)

---

## 7. Audio design

The forest is an instrument. Three layers, always running.

### 7.1 The bed

| Layer | Content | Modulated by |
|---|---|---|
| Canopy bed | Filtered pink noise (band-pass 400–2 400 Hz) + random leaf rustles | Intensity ∝ canopy density; rustle rate ∝ player speed |
| Insect life | Cicada shimmer, distant bird calls, frogs | Biome; density ∝ moisture; **mutes as the player nears a germinating node** (a "held breath" cue — the forest goes quiet when something is about to drop) |
| Sub | 40 Hz sine + 55 Hz, very low | ∝ canopy pressure |

**The mute cue is the single most valuable audio design decision in the game.**
When the insects stop, the player knows to look up — and that is a mechanic
delivered entirely through sound.

### 7.2 Events

| Event | Sound design |
|---|---|
| Germination | A soft woody "tick" plus a rustle burst |
| Extension | A continuous "shhhk" that rises in pitch as the tip accelerates (the whip tell) |
| Contact | Sharp **twang** (a filtered string pluck with a fast decay) + a wet slap |
| Tension | Wood/rope creak, pitch and gain rising with tension; a creak type per species (tendril = fibrous squeak, anchor = deep groan, liana = layered rustle) |
| Creep | Slow fibrous grinding, panned to the contact side |
| Break | Brittle snap (short bright transient), woody crack (deep, with a long tail), wet release (for claws) |
| Cutter | Hydraulic hiss + steel ring; chatter when cutting a hard target |
| Engine | RPM-driven additive (3 harmonics + noise), load-dependent filter opening, turbo whistle, anti-lag pops on lift |
| Wheels | Per-surface loops: dirt rumble, gravel scatter, mud squelch, leaf rustle, water spray |

### 7.3 The duck

When a vine latches, the music/ambient bed **ducks by −4 dB** for 400 ms and a
low-pass at 900 Hz sweeps in for the duration of the latch. The world sounds
muffled because the player's attention is trapped. On release, the filter sweeps
back over 250 ms. This is a small piece of sound design that does enormous
work for tension.

---

## 8. Haptics

| Event | Controller pattern |
|---|---|
| Tendril contact | 30 ms, 0.35 amplitude, 120 Hz tap |
| Anchor contact | 180 ms, 0.8 amplitude, 60 Hz, plus a 2 Hz "pull" LFO |
| Tension ramp | Continuous amplitude ramp mapped to tension (not a loop) |
| Creep | Very low 8 Hz amplitude creep |
| Cutter | 60 ms hard click + 120 ms hydraulic buzz |
| Break | 40 ms snap, sharp |
| Off-road | Continuous 20–80 Hz rumble ∝ speed × surface roughness |

---

## 9. Readability checklist (art QA)

Before any asset ships, it is checked against this list:

- [ ] Can a player spot an extending vine tip at 120 km/h in peripheral vision? (Requires a 2 % luminance lift on the tip and a back-lit rim.)
- [ ] Is a latched contact point visible from the chase camera? (Requires the contact flare to be visible in the mirror/roof reflection and the camera to not hide the flank.)
- [ ] Is the tension state visible on the vine itself? (Requires vibration + colour shift, not just a HUD bar.)
- [ ] Does the canopy bulge read from 80 m away? (Requires a canopy "lift" animation and a dust puff.)
- [ ] Can the player distinguish a mud patch from a leaf patch at 100 m? (Requires distinct albedo value, not just hue.)
