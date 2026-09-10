══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
  Rain puddles and wetness — deferred by request
══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════

**Status: out of scope for the Celestial port, at the user's explicit direction.** Recorded so the shape of the
work is not lost, and so nobody fills the gap with the thing that was rejected.

WHAT WAS ASKED FOR, AND WHAT WAS REJECTED
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Rain should eventually wet the ground: small puddles that collect where water runs, a darker and glossier surface
where it has been raining, and drying back out afterwards.

**The reference demo's splash is explicitly not wanted.** It draws an expanding screen-space ellipse per landed
drop (`g.ellipse(sx, sy, rr, rr*.35, ...)` scaled by `pixels-per-metre at this depth`), which is a 2D ring painted
at the drop's projected position. It does not respect the surface's orientation, it does not accumulate, and it
disappears with the particle. It reads as a decal sprite rather than water.

WHAT THE SHAPE OF THE REAL WORK IS
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
A wetness FIELD, not a particle effect — structurally the same answer as `SnowField` in `Precipitation.h`:

  • A grid over the ground holding accumulated water depth, fed in O(1) per landed drop exactly as snow is, and
    drained by an evaporation term. Fixed memory, no growth with rainfall.
  • The renderer consumes that field as a material modification rather than as geometry: roughness down and
    specular up where it is wet, a shallow normal perturbation where depth is enough to pool, and a flat mirror
    at the deepest cells. This is a shader change, not a particle system, which is why it belongs with the
    post/material work rather than here.
  • Tier-keyed splashes remain in scope for a later step and are the ONE thing that stays in screen space:
    off at Minimal, a few taps at Standard, full at Reference.

WHY IT IS SEPARABLE
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
The precipitation system already retires landed particles into a field, and `SnowField` is that field for snow.
A `WetnessField` is the same class with a decay term. Nothing in the particle simulation needs to change to add
it later — the settle path is already the hook.
