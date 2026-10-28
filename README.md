# Frontier — Hybrid Water: 2.5D Ocean + Volumetric Splash

A combined, working water system (open `index.html` via any static server, e.g.
`python3 -m http.server 8000`, then visit the page — Three.js loads from CDN).

## The two systems, and how they're combined

| Layer | System | Role |
|---|---|---|
| **Water / waves / ripples** | **2.5D** — Gerstner ocean + a wave-equation heightfield tile (`src/heightfield.js`, `src/ocean.js`) | The actual surface everywhere |
| **Splashes / spray / foam** | **Volumetric** — SPH-lite particles (density, pressure, viscosity, vorticity) on a **sparse 3D hash grid** (`src/splash.js`) | Realistic 3D splash crowns, droplets, white foam |

**Coupling — the Impact Registry (`src/main.js: registerImpact`)** is the single
meeting point. Anything that falls into the water — a volumetric droplet, a
dropped body, rain, a click — registers `{id, world x/z, energy, radius, source}` once.
Then both systems react:

- 2.5D: Gaussian impulse into the wave equation (rebounding ripples) + foam deposit
  into the foam mask, which diffuses, spreads and decays on the surface.
- Volumetric: energetic impacts throw a crown of new splash particles, and droplets
  that fall back are *removed at the surface* — their momentum becomes the ripple/foam
  above instead of disappearing. Foam particles ride the sampled surface height, then dissolve.

## Works everywhere — grid design

- **Splash grid is world-space and sparse**: a hash-map cell only exists where droplets
  currently are. No tank, no fixed bounds — spawn a splash at any world coordinate and
  the grid materialises there. Cell size is a slider (0.25–1.4 m), so it adapts to
  whatever grid resolution your scene uses; neighbour search, density and pressure
  all read the same hash.
- **2.5D ripple tile follows the camera focus**, snapped to its own texel size so
  ripples stay world-locked (the field is shifted, not cleared, when the tile moves).
- **Ocean covers large areas**: a high-res near tile + a far tile follow the camera
  (snapped to the vertex grid, so no swimming). Waves are analytic Gerstner, evaluated
  in world space, so the sea is effectively endless. **Practical limit:** the far tile
  is ±9 km (18 km across) before the horizon — raise `PlaneGeometry(18000,…)` in
  `src/ocean.js` for more; cost doesn't grow, only far-tile vertex spacing does.
  Detail (ripples/foam) is intentionally local: the heightfield tile is 40–220 m
  (slider) — that's the honest limit of a 2.5D ripple sim, and why splashes are volumetric.

## Controls

Frontier Editor UI (from `Experimental/` in the Frontier repo: OLED panels, Inter,
204 px systems rail, 302 px inspector, `#b7c6bd` accent).

- **Sliders** (right inspector): wave height/chop, ripple propagation & damping, tile
  size, gravity, pressure, viscosity, vorticity, droplet size, **grid cell size**,
  foam amount/lifetime/spread. Presets: Calm / Storm / Impacts.
- Toolbar: **Drop body**, **Rain**, **Big wave splash**, **Clear ripples**.
- **Alt+click** water drops a body there; drag to orbit, wheel to zoom.
- Left rail shows live sparse-grid cell count, ripple-tile position, and the
  **Impact Registry log** (every registered falling-water location + energy).
