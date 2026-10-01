# Frontier — Cascaded LPV GI Lab

An interactive WebGL proof of concept for a **non-RTX / GTX-oriented diffuse GI path**:

```text
Direct light / RSM-style surface samples
  → transient lit surfels
  → three camera-relative LPV clipmaps
  → small directional blocker volume
  → half-resolution SSGI + GTAO refinement
  → indirect-light resolve
```

The demo intentionally avoids signed distance fields and hardware ray tracing. Its scene is rendered with Three.js/WebGL, while the lighting buffers are written in JavaScript so the intermediate RSM, LPV, blocker, and screen-space stages can be inspected live. In a production renderer these buffer updates map directly to compute passes.

## Run

```bash
npm install
npm run dev
```

Open the URL printed by Vite.

## Controls

- **Drag** to orbit; **scroll** to dolly.
- **W / A / S / D** moves the camera and causes the three LPV clipmaps to recenter.
- Change the output inspector to see the final composite, surfel injection, LPV transport, the coarse blocker volume, or the half-resolution SSGI/GTAO signal.
- Tune surfel count, propagation steps, indirect energy, temporal light motion, and individual fallback layers.

## What the prototype demonstrates

- **Transient RSM/G-buffer surfels:** surface elements are selected on the room mesh each GI update, direct-light tested against the occupancy map, and then injected as colored directional flux.
- **Three cascading, camera-relative volumes:** 18 m, 36 m, and 72 m clipmaps overlap and smoothly blend at their ranges. Their lighting history is amortized at roughly 10 Hz, while the camera renderer is continuous.
- **Directional blocker volume:** a deliberately lower-resolution occupancy volume is sampled along light transport directions to attenuate propagation crossing walls.
- **Half-resolution SSGI and GTAO:** short local rays provide contact color bleeding and the AO signal is fed to the floor material.

## Deliberate scope

This is an inspectable **GI architecture demo**, not a complete game renderer. The LPV math is represented as a compact 3D grid, while the visual resolve is projected to the floor so the color bleeding and cascades remain easy to evaluate. A shipping implementation would move the update/propagation buffers to compute shaders, scroll individual slabs instead of clearing on volume movement, inject real RSM or G-buffer samples, use real per-view screen-space depth, and add temporal/spatial denoising.
