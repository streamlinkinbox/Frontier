# Gas sample scenes — refinement deferred

Deferred 2026-10-08, at the user's direction, after the first eight samples were reviewed in the browser and
judged not yet good enough. The **format is settled and the C++ side proceeds**; only the *content* of the
eight scenes is held.

## Why this is safe to defer

The scenes are data, not code. `Experimental/Fluid/src/SampleScenes.js` holds eight definitions, each a preset
identity plus a handful of overridden settings, a camera framing and four object names. Changing one is an
edit to that file and nothing else:

- The native reader parses the **format**, not these particular scenes, so it does not care what they contain.
- Each sample resolves through defaults → preset → its own differences, so a retuned preset flows in without
  the sample being touched.
- The round-trip check iterates `SampleIdentities()`, so an added, removed or retuned sample is covered the
  moment it exists.
- The corpus is written to `.gasscene.toml`, and that file is the same file whatever the content — which is
  the user's own point: refining later costs a re-export, not a re-port.

The one check that pins the size is `Assert.equal(SampleCount, 8, …)`, which exists so the corpus cannot grow
or shrink without the coverage claims being revisited. Changing the count is a deliberate one-line edit, and
that is the intent.

## What is known to need work

Recorded now, while the review is fresh, rather than rediscovered later.

| Area | What is wrong or unknown |
|---|---|
| Tuning | The eight were assembled from existing presets with minimal overrides. None has been sat with in the viewport and dialled in, so each is "the preset, framed" rather than an authored scene. |
| Framings | Four camera framings are shared across eight scenes. Two of them read as arbitrary rather than chosen — the distant and megaton scenes in particular are framed by a rule of thumb, not by what they need to show. |
| Object names | The outliner names are plausible placeholders. `collider = "None"` in the distant plume is a name standing in for an absence, which is not what a name is for. |
| Coverage gaps | Nothing covers: a domain with **two** emitters; an emitter attached to a moving object; a scene whose dynamic bounds actually surge during capture; the deflector *and* the tyre ring together. |
| Scale honesty | Bounds are inherited from presets authored for a browser viewport. Whether those metres correspond to anything in a Frontier level has not been checked, and the budget governor's distance thresholds assume they do. |
| Framing, now measured | 📝 **Added after the native host rendered all eight.** `tyre_burnout_ring` through its own authored camera comes back **6.5 % covered and almost entirely black** — dark soot, low sun, and a framing that has drifted off the plume. It is the clearest single piece of evidence for this deferral, and it is why the gallery capture of an obstructed plume uses `deflector_obstacle` instead. `camp_fire_steady` frames tightly enough that the plume is cut off at the top. |
| One-shots are silent | `fracture_dust_burst` and `open_bounds_megaton` carry no emitter: advancing either without firing its blast leaves an **empty cube**, and the native side can only infer that they are one-shots from "no emitter, loaded blast". A refinement should decide whether a scene ought to say so outright. See §7 of the ongoing plan. |
| Count | Eight is a guess. The corpus should be as small as it can be while covering the cases; it has not been argued down or up. |

## What must not change without revisiting

Three properties are load bearing for checks elsewhere, and a refinement that breaks one should break
deliberately:

1. **`tyre_burnout_ring` selects obstacle 5.** Asserted against `GasColliderShape::TyreRing`; the two agreeing
   is what stops a burnout smoking through its own tyre.
2. **`cold_dust_fall` carries no fuel and no heat.** It is the only scene in which the raymarch's scattering
   path is the sole source of light, so losing that makes the emission term untestable.
3. **A retuned scene must still cross byte for byte.** `ctest --test-dir Build/GasOnly` and the CI corpus
   step both run `Project-Gas cross` over every file; re-exporting from the browser is what keeps them
   agreeing, and hand-editing a `.gasscene.toml` is what stops them.
4. **The cost range stays spread.** `hero_detonation` at 128³ and `far_cheap_plume` at 32³ are what the budget
   governor is exercised against; narrowing the range quietly narrows that check.

## Resuming

1. Sit with each scene in `Experimental/Fluid`, tune, and re-export.
2. Revisit the coverage gaps above and decide which earn a scene.
3. Re-run `node --test src/SceneMetrics.mjs`; the corpus checks are identity-driven and need no edit unless
   the count changes.
4. Re-export the `.gasscene.toml` corpus; the native round-trip check reads whatever is there.
5. Look at each scene natively — `Project-Gas view <scene> --picture <out.png>` renders through the scene's
   own camera, so a framing can be judged without a browser.

No native work is blocked by this.
