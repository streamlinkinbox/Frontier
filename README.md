# Frontier Fluid Lab

A self-contained WebGPU fluid playground for game-engine prototyping. Open `app/index.html` from a static server and drag in the viewport to inject dye and velocity.

## What is implemented

- GPU compute solver with ping-pong `rgba16float` storage textures.
- Semi-Lagrangian velocity and dye advection.
- Vorticity confinement for curl detail.
- Divergence calculation, Jacobi pressure projection, and gradient subtraction.
- HDR-ish dye rendering with density gradients, edge lighting, and a dark composited background.
- Presets for ink bloom, smoke plume, vortex ink, and a fast cool current.
- Live controls for grid size, brush radius, injection force, vorticity, dye retention, buoyancy, and an ambient emitter.
- WebGPU feature detection with a visual Canvas 2D fallback, so the prototype remains usable on browsers without WebGPU.
- Pointer and touch input, pause/reset/fullscreen shortcuts, and a small solver readout for profiling.

## Run locally

```bash
cd app
python3 -m http.server 4173 --bind 0.0.0.0
```

Then visit `http://localhost:4173`. WebGPU requires a secure context in many browsers; `localhost` is treated as secure. For production, serve the page over HTTPS and request the adapter with the desired power preference.

## Moving it into an engine

The compute passes live inline in `app/index.html` as WGSL modules. The `WebGPUFluid` class is the portable part: keep its texture allocation, uniform layout, and pass order, then connect its output dye texture to the engine's render graph instead of the demo fullscreen render pass. A production integration should add obstacle/SDF textures, boundary conditions, fixed timestep accumulation, resize-safe resource pooling, and a capability tier for lower-end GPUs.
