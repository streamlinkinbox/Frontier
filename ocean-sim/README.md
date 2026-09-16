# FRONTIER · Ocean Simulator

A real-time, research-grounded ocean simulator in the browser: a **JONSWAP
spectral wave field** (not a scrolling normal map), a **surf break** with
shoaling/barrel/peel, **Jacobian whitecaps + 200k GPU foam/spray particles**,
and a **wave-cancellation lab** with a live interference meter.

Built from 2024–2026 real-time ocean research — see [RESEARCH.md](./RESEARCH.md).

## Run it

Any static server (ES modules + import map; three.js r160 loads from CDN):

```bash
cd ocean-sim
python serve.py 8000        # cache-proof static server (plain refresh always runs current code)
# open http://localhost:8000
```

No build step, no npm install. Requires WebGL2 (all modern browsers).

## What you get

| System | How it works |
|---|---|
| Spectral waves | 80 JONSWAP components in 3 cascades (swell / wind-sea / chop), fetch-limited Hasselmann growth, `Hs = 4√m0` energy normalization, GPU Gerstner sum with analytic normals |
| Wave lab | 2 localized trains (λ, amp, dir, phase) + live `var(A+B)/(varA+varB)` meter: destructive cancel, standing waves, crossing seas |
| Surf break | Green's-law shoaling, wavelength shortening, breaker jack-up, barrel lip throw, travelling peel pulse over a skewed reef bar |
| Foam | Simulated advection field (injected from fold/breaker physics, drifted, decayed) + instantaneous Jacobian whitecaps |
| Spray | Breaker-only GPU spray, peel-gated, near/far faded |
| Optics | depth absorption, Fresnel sky reflection, sun glitter, crest subsurface scattering, ACES, horizon-matched fog |
| Probes | instrument buoy + lab spar ride the same analytic field the GPU draws; live spectrum plot with Hs / Tp readout |

## Controls

- **Presets (1–6):** Glassy · Trades · Storm · Surf · Cancel · X-Seas
- **Cameras:** Orbit · Surf · Shore · Aerial · Lab · Buoy
- **Mouse:** drag orbit · wheel zoom · right-drag pan
- **Keys:** `WASD/QE` fly (`shift` = fast) · `space` pause · `1–6` presets
- **Transport:** pause + time-scale for inspecting breakers and interference

Try: **Cancel** preset → Lab camera — the sea inside the cyan ring goes flat
while the spar stops bobbing; drag Train-B phase and watch the meter sweep
from destructive (≈0) to constructive (≈2).

## Files

```
ocean-sim/
├── index.html      app shell + control panel
├── styles.css
├── src/
│   ├── main.js     renderer, loop, uniforms sync, transport
│   ├── config.js   params, presets, Beaufort table, time-of-day grading
│   ├── spectrum.js JONSWAP spectrum + CPU wave-field mirror
│   ├── bathy.js    analytic bathymetry (mirrors the GLSL)
│   ├── glsl.js     all shaders: ocean / sky / seabed / particles
│   ├── ocean.js    graded near grid + far plane
│   ├── sky.js      sky dome (shared sky function)
│   ├── seabed.js   sand + caustics + beach
│   ├── particles.js foam/spray GPU particle system
│   ├── props.js    buoy + lab ring + spar probes
│   └── ui.js       panel bindings, presets, spectrum plot, meter
├── RESEARCH.md     2024–2026 sources and design decisions
└── README.md
```

## Tuning notes

- Whitecaps auto-range with sea state (`uFoldGain` from Hs); the Whitecaps
  slider is an artistic multiplier on top.
- Detail fades per-component by wavelength, so long rollers survive to the
  horizon while sub-grid chop is culled instead of shimmering.
- The sea readout tells you when the fetch law saturates ("fully developed"):
  beyond that point extra fetch is physically idle, not a bug.
- Relief × is honest vertical exaggeration (1.0 = true JONSWAP heights);
  it scales waves, normals, foam fold, buoy and probes together.
- If a laptop struggles: Quality → medium/low, or lower the spray count.
