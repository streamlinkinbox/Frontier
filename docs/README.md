# Frontier · WebGPU Sand + Smoke (GTX tier)

Run: `python3 sim/serve.py` (no-cache dev server). Tiers: default **RTX**, `?tier=gtx`, `?lowres` (or the HUD buttons). → open http://localhost:8080 (Chrome/Edge 113+).
URL flags: `?lowres` (64×32×64 smoke, 16k sand – low-end GPUs), `?demo` (auto-drive).

- **Smoke**: 3D Eulerian grid 128×64×128 (0.25 m cells) following the car; MacCormack advection,
  vorticity confinement, buoyancy, 24 Jacobi pressure iterations; car box + crates are obstacles.
  Emitted only while drifting (rear lateral slip) or heavy wheelspin. Ray-marched volumetrically.
- **Sand**: MLS-MPM, 48k particle ring buffer, spawned only at tyre contact patches, fixed-point
  atomic P2G, no-tension granular pressure + Coulomb ground friction, pushed by the car box.
- **Car**: CPU rigid body, 4 tyres with slip/friction circle, handbrake drift, OBB-vs-OBB crate collision.
- **Gas explosions**: proximity-fused crates, 4 randomised archetypes, burning-wreck plumes.
- **Tornado**: wandering Rankine vortex (GPU dust particles + smoke-grid dust + MPM sand drag), pulls the car, lifts/flings crates.
- **Smoke LOD cascade**: fine 128×64×128 @ 0.25 m grid near the car + coarse 128×32×128 @ 1 m far grid (≈128 m, 30 Hz); the far grid hands over to the fine one inside its box, so smoke/fire/plumes/tornado dust stay active everywhere in range.
- Sim VRAM ≈ 250 MB (budget 1 GB). Test flags: `?boomtest`, `?tornadotest`, `?smoketest`.
