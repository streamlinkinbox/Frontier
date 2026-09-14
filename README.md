# Frontier

Frontier is a browser prototype for an SDF-first open-world terrain laboratory.

## What is implemented

- **Direct signed-distance preview:** the viewport ray-marches a procedural terrain SDF in a WebGL fragment shader. Ridged noise, multifractal detail, domain warping, a basin carve, and weathering masks are evaluated in volume space; there is no heightfield or downloaded imagery in the preview path.
- **Realtime particle-weathering controls:** paint a rainfall source in the viewport, run the live rain pass, and adjust impact velocity, dissolution, sediment capacity, wind abrasion, and chemical weathering. The UI communicates the intended contact solve: droplets collide with the SDF, remove material, carry sediment, and deposit it downstream.
- **Non-destructive process graph:** the graph exposes the SDF operators, particle solver, sediment stage, and procedural material resolver as live operators. Parameters are inspector-driven and update the preview without baking a map.
- **Procedural material response:** beauty, signed-distance diagnostic, and flow views use generated altitude, slope, AO, curvature/pointiness-style surface response, and erosion masks. The satellite-material stack is explicitly procedural rather than an internet image.
- **Product-style tooling:** export manifest, local save feedback, graph selection, deterministic seeds, camera orbit/zoom, simulation cache indicator, and responsive dark editor UI.

## Run locally

```bash
python3 -m http.server 4173 --bind 0.0.0.0 --directory app
```

Then open `http://localhost:4173`.

The current implementation is intentionally self-contained in `app/index.html` so the terrain preview can run without a build step or external assets. The UI is a design/interaction prototype; production OpenVDB storage, GPU compute particle integration, and sparse-volume streaming would be the next native/WebGPU backend layer.
