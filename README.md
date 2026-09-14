# STRATA — Terrain Erosion Lab

A single-page, vanilla JavaScript terrain lab inspired by node-based tools such as Gaea, World Machine and Houdini HeightFields.

## Run it

No build step is required:

```bash
python3 -m http.server 4173 --bind 0.0.0.0
```

Open `http://localhost:4173`.

## What is implemented

- Equal split between a draggable procedural node graph and a rendered oblique terrain view.
- Deterministic canyon source built from layered FBM and ridge noise.
- Hydraulic pass with separate per-cell terrain elevation, water depth, suspended sediment, and X/Y flow vectors.
- Virtual-pipe / shallow-water transport, sediment pickup/deposition, evaporation, and a thermal talus-settle pass.
- Surface, depth, and flow render modes with toggles for water, contours, and flow vectors.
- Height-map import, procedural re-seeding, PNG preview export, editable node parameters, and a method/research note dialog.

## Research direction

The interaction model follows the quantities and update order commonly used by shallow-water terrain erosion implementations: terrain height `b`, water depth `d`, sediment `s`, outflow flux, and velocity. The method notes in the UI link to:

- Jákó & Tóth, *Fast Hydraulic and Thermal Erosion on GPU* — virtual pipes, shallow water, sediment capacity, and thermal erosion.
- Št’ava et al., *Interactive Terrain Modeling Using Hydraulic Erosion* — layered materials and bank slippage beyond a single surface-only height field.
- `kristofe/terrain-erosion` — open-source CPU terrain/fluid implementation.

The browser preview is intentionally a compact interactive model, not a replacement for a full voxel solver. The separate water, sediment, hardness-ready material, and horizontal-flow channels make the next step toward a layered or voxel representation explicit rather than hiding everything in Z-only displacement.
