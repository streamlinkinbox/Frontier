# Particle system and Particle Editor — deferred, with the reference still to come

**Status:** deferred on purpose, not forgotten. The user is supplying a reference Particle Editor before any
of this is designed, and designing it twice would be the waste.

**Why it is written down now:** because the gas work keeps bumping into it. Embers above a camp fire, ash
falling out of a smoke column, sparks off a lightning strike — none of those are volumetric gas, and every
one of them is about to be faked inside the gas domain if nobody records that they belong somewhere else.
A fake that ships is a fake that stays.

---

## What it is for

One system, driven by data, covering all of these. The spread matters: a design that only serves embers
will not survive the second use.

| Driver | Emits | Notes that already constrain the design |
|---|---|---|
| Gas domain | Embers, ash | Spawned from the hot voxels of a live domain; must outlive the domain that spawned them, because a one-shot retires in seconds and its embers do not |
| Lightning / thunder | Sparks, flash debris | Burst of hundreds in one tick, then nothing. Peak-rate shaped, not steady-rate |
| Wind field | Leaves, litter, blown objects | **Advected by the same wind field the gas reads** — `GasWindContribution.h` already resolves it. One field, two consumers |
| Trees | Falling leaves | Emitted from a mesh, not a point. Needs surface emission |
| Weather | Rain, snow | The precipitation the editor already has, moved onto this system rather than kept as a second one |
| Sandstorm / tornado | Grains, dust | Hundreds of thousands, screen-covering, driven by a vortex rather than a field |
| Destruction | Debris | Collides and comes to rest; the only class here that needs real contact rather than a fake floor |

## The decisions already forced by the gas work

These are not open; they fall out of what was built. Recording them so the design starts from them.

1. **Particles are not gas and are not voxels.** They are points with a position and a velocity. The whole
   reason embers look right is that they are *not* on the lattice — a 64³ cube cannot hold a spark.
2. **They read the fields they fly through, and do not write them.** A leaf samples the wind field; an ember
   samples the gas velocity. Nothing in this list needs the one-way direction broken, so it stays one-way,
   and the two-way coupling consent in `GasWindContribution.h` is not extended to particles.
3. **An emitter is already an entity.** `gas-emitter` is a child row with its own transform, re-parentable
   onto any object. A particle emitter is the same shape of thing, and should be the same shape of row —
   not a second, parallel notion of "emitter" in the outliner.
4. **The same lifecycle.** Gas got Dormant → Running → Fading → Released this turn. A burst of sparks is a
   one-shot with exactly those phases, and a design that invents different ones for particles will have two
   budget ceilings that have to be reconciled.
5. **Determinism only for multiplayer**, as everywhere else in this programme. Embers may be cosmetic and
   frame-rate dependent; debris that gameplay can trip over may not.
6. **Rendering is sprites, not geometry**, and shares the flipbook path the gas far tier uses.

## What is NOT decided, and is waiting on the reference

- The editor's shape: curve-over-life, module stack, graph, or something else. The user's reference decides
  this, and guessing now would mean porting the guess.
- Whether the solver is CPU, GPU, or picks per emitter by count.
- Collision: whether debris gets the distance-field route the gas colliders use, or real contact.
- Whether precipitation moves onto this system or stays as it is. Moving it is right and is also a
  migration, and migrations are cheaper once the system exists than as part of building it.

## Cost of deferring

Low, and lower than the cost of guessing. Nothing shipped so far assumes particles exist, and nothing so far
blocks them: the gas domain exposes its velocity field and its hot voxels, the wind field is already
resolved for an arbitrary position, and the emitter row pattern is in place to be copied. The one thing to
avoid in the meantime is **faking embers inside the gas solver**, which would be quick, would look fine, and
would have to be torn out.
