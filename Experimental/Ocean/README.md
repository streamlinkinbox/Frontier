# Ocean — surf that has to earn its water

State: **v3 build in progress.** `Ocean.html` v1 (Gerstner +
shader foam) and v2 (spectrum sea + particle foam) were deleted after
review: the waves never looked realistic and v1 violated the standing
rule below. v3 is a Tessendorf FFT sea with particle-only foam,
built in gated stages (plan: `RESEARCH.md` §3).

Standing rules for whatever gets built next:

- **Foam is particles only.** No foam term of any kind in the water
  shader (no run-up band, no crest caps, no noise breakup on the
  surface). Emission fields may drive particles; particles are the
  only visible foam.
- **References before code.** A reference board (real surf photos)
  gates all look decisions. Default frame must read as ocean with
  particles off before any effect lands.
- **One surface both sides read.** CPU spawn logic and GPU rendering
  share the same height field — never two mirrors that drift.
- **Camera moves like Unreal.** Drag-look + WASD/QE fly, Shift boost,
  wheel dolly. No orbit toys.
- **Bar is film/Unreal, not sample projects.** No Godot-derived
  approaches anywhere in the design.

See `RESEARCH.md` for the full post-mortem, the literature survey
(Tessendorf FFT ocean, Jacobian foam emission, precedents), and the
committed v3 plan. `lib/` keeps the vendored three.js for the rebuild.

## Build log

- **Stage 1 DONE — FFT core + water + sand + sky + Unreal camera.**
  256² Tessendorf sea (TILE 240 m, float RTs with half fallback,
  manual-bilinear sampling so no float-linear extension is needed),
  Phillips wind sea (A=0.0042, Hs ~2 m at 8 m/s) + two swell trains
  (λ95 m/1.8 m, λ52 m/0.5 m), finite-depth dispersion, Unreal fly
  camera (drag-look, WASD + Q/E, Shift boost, wheel dolly), far-water
  apron to the horizon. No particles yet. Verified: static sea state
  h −3.7/+3.4 m, mean slope 0.16; animated mode stable; lineup,
  break and shore views read as ocean.
- Hard-won calibration (do not "simplify"): collect gain is
  dk·N² ≈ 1716 (unitary inverse FFT → Tessendorf sum needs the dk
  measure); Phillips high-k rolloff 1/(1+(k/1.2)⁴) (grid cannot
  carry λ<5 m — unresolved tail folds the chop field into noise);
  sub-grid swell bumps are normalized to integrate to a²/2 with a
  1.5·dk width clamp; r160 ColorManagement converts hex palettes —
  never call convertSRGBToLinear on them.
- Carry-forward: steep look-down water reads flat (opaque body, no
  transparency/subsurface yet — shore-transform look-dev in
  stage 2/3); mid-field swell lines want foam-on-crest organization
  (stage 3) to fully read.
