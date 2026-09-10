══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
  Atmosphere LUTs — deferred to last, on measured evidence and on looks
══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════

**Status: deliberately not built. Revisit only after every celestial component ships, and only with a measurement
in hand.** This is the obvious first optimisation for an analytic sky, it is what the literature recommends, and
it is exactly the thing that should not be done first here.

Related but distinct: `SkyViewSurface.md` in this directory analyses the *parameterisation* of transmittance and
sky-view tables in a previous engine, and concludes a sky-view surface was structurally missing there. That note
predates the Celestial port and describes a different codebase. This one is about whether our current analytic
path should be replaced by tables at all.


WHY NOT
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Two independent reasons, one measured and one aesthetic. Either alone would justify deferring; together they
settle it.

**① The source branch built them and reverted them.** `SultanAladin/Frontier-@arena/01a08682-frontier` shipped a
full LUT path at `f5b5d3d`: a 256×64 transmittance table on the Bruneton horizon-exact parameterisation rebuilt
only when the medium changes, plus a 192×108 per-frame sky-view table with horizon-centred sqrt elevation, 48
samples, sun transmittance sampled from the T-LUT with ground shadowing. Per-pixel sky collapsed to two texture
reads. It had float/half-float FBO detection with automatic fallback, a Camera › Quality toggle, and a HUD
readout — a complete, careful implementation, not a sketch.

It was reverted three commits later at `2fe78ed`, with the reason stated plainly: **no measurable speedup on a
GPU-bound frame, and the analytic path looks better.**

There is a subtlety worth keeping: the intermediate commit `57269bb` shows the first LUT attempt was silently
broken — the tables were bound to texture units 2/3, which the moon loop rebinds every frame, so the sky-view
table was sampling a white 1×1 texture and the sky rendered white. That was fixed before the revert, so the
"no speedup" verdict was measured against a *working* LUT path, not a broken one. The revert is trustworthy.

**② You looked at it and said it kills the realism.** That is the decisive reason, and it agrees with what the
source branch found by eye. Worth understanding *why*, because it is not arbitrary: a LUT is a fixed sampling of
a function that is very smooth over most of the dome and very sharp in two places — the horizon band and the few
degrees around the sun. Those are precisely the regions the eye judges a sky by. Any table small enough to be
worth the bandwidth is under-resolved exactly where it matters, and the failure mode is a *banded* sunset over a
smooth zenith. Hillaire 2020 §5.3 says the same thing about linear latitude parameterisation, which is why the
sky-view mapping is quadratic about the horizon.

Our twilight work makes this worse rather than better. The white line is a hairline **~0.11° wide** — about a
fifth of the solar disc. A 192×108 sky-view table spanning 180° of elevation gives roughly 1.7° per texel: the
line is **fifteen times narrower than one texel**. It cannot survive tabulation at any plausible resolution; it
would be smeared into the glow it is supposed to sit on top of, and the transition that was specifically asked
for would simply disappear.


WHAT WOULD CHANGE THE ANSWER
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
The verdict is not "LUTs are wrong". It is "LUTs are unproven here and cost the look". Three things could reopen
it, and the burden of proof sits with the LUT in every case:

  • **A measurement on real hardware showing the analytic path is actually the bottleneck.** The source branch's
    verdict was "no speedup on a GPU-bound frame" — measured in WebGL on a GTX-class part. We are Vulkan compute
    with async potential and a very different frame. The `sky` timestamp span reserved in Celestial step 0
    (queries 16/17) is what settles this, and it reports nothing until the engine runs on a GPU.
  • **A hybrid rather than a replacement.** Table the smooth part, keep the sharp part analytic: sample the LUT
    for the general dome and evaluate the horizon band and the solar aureole per pixel. That keeps the hairline
    and the sunset gradient exact and only tabulates what tabulates well. This is the version most likely to
    survive, and it is strictly more work than either extreme.
  • **Reference tier exempt regardless.** Whatever ships, Reference should brute-force, as the source branch did
    ("Reference tier and above-atmosphere always brute-force"). Our Reference already means "most realistic, no
    compromise" and a table contradicts that by construction.


IF IT IS EVER BUILT
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Notes so the next attempt does not repeat the first one's mistakes:

  • Bind the tables to texture units nothing else touches, and verify by sampling them back — `57269bb` lost a
    whole cycle to a unit collision that presented as a white sky.
  • Avoid T-LUT feedback while the T-LUT is rebuilding.
  • Parameterise sky-view quadratically about the horizon (Hillaire §5.3), not linearly in elevation.
  • Compare against the analytic path *as images*, not only as frame times. The reason this was reverted twice —
    once by the source branch, once here — is that it looked worse, and a timing table cannot show that.
  • Gate it behind the tier ladder in `FidelityClassifier`, never a second copy of the budgets
    (`CheckCelestialTiers.sh` enforces this).
