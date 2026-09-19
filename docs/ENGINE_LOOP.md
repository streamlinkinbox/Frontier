# Slate Custom Game Engine Loop Specification

## 1. Overview & Dual-Clock Architecture

Slate's custom engine loop uses a deterministic, dual-clock execution architecture that completely isolates high-frequency physical simulation from variable-rate frame presentation.

### 1.1 Dual-Clock Formulation
- **Fixed Simulation Timestep ($\Delta t_{fixed}$)**: Configured at `1/60s` (16.66ms) or `1/120s` (8.33ms) for Jolt rigid body steps, Custom XPBD softbody constraints, and 3D Eulerian Navier-Stokes fluid updates.
- **Variable Presentation Timestep ($\Delta t_{frame}$)**: Dictated by the monitor refresh rate (60Hz, 144Hz, 240Hz, or uncapped).
- **Interpolation Alpha ($\alpha$)**:
  $$\alpha = \frac{t_{accumulator}}{\Delta t_{fixed}}$$
  Render transforms and visual proxies are smoothly interpolated between the previous physics state ($S_{prev}$) and the current physics state ($S_{curr}$):
  $$S_{render} = (1 - \alpha) S_{prev} + \alpha S_{curr}$$

---

## 2. The 14-Phase Deterministic Frame Pipeline

```
+-------------------------------------------------------------------------------------------------------+
|                                        SLATE FRAME TICK PIPELINE                                      |
+-------------------------------------------------------------------------------------------------------+
|                                                                                                       |
|  [PHASE 01: Frame Clock & High-Precision Time Sync]                                                  |
|        │                                                                                              |
|        ▼                                                                                              |
|  [PHASE 02: Platform & OS Window Events]  ──► (Keyboard, Mouse, Gamepad, Viewport Resize)             |
|        │                                                                                              |
|        ▼                                                                                              |
|  [PHASE 03: Epic Online Services (EOS) Tick]  ──► (EOS_Platform_Tick, Packet Ingestion, Voice)        |
|        │                                                                                              |
|        ▼                                                                                              |
|  [PHASE 04: Pre-Physics Job DAG Dispatch]  ──► (Player Controller, AI, Kinematic Hierarchy Updates)  |
|        │                                                                                              |
|        ▼                                                                                              |
|  [PHASE 05: Fixed-Timestep Physics Substep Loop]                                                      |
|        │                                                                                              |
|        ├──────► While (accumulator >= fixed_dt):                                                      |
|        │          1. Save Previous Transform States (S_prev = S_curr)                                 |
|        │          2. Step Jolt Physics (Rigid Bodies, Character Virtual, Contacts)                    |
|        │          3. Step Custom XPBD Softbody & Cloth Solver (Volume Conservation, Springs)          |
|        │          4. Step 3D Fluid & Gas Navier-Stokes Solver (SDF Boundaries, Advection)             |
|        │          5. Accumulator -= fixed_dt                                                          |
|        │                                                                                              |
|        ▼                                                                                              |
|  [PHASE 06: Post-Physics Transform Sync & Alpha Interpolation] (SIMD Lerp)                            |
|        │                                                                                              |
|        ▼                                                                                              |
|  [PHASE 07: Animation & Pose Evaluation] (Skeletal Blend Trees, IK)                                   |
|        │                                                                                              |
|        ▼                                                                                              |
|  [PHASE 08: Simulation Compute Subsystem]                                                             |
|        │                                                                                              |
|        ├──────► 8a. Dedicated Fluid/Gas SDF Update Sequence (Level-Set & Obstacle Masks)             |
|        ├──────► 8b. Gas Buoyancy & Vorticity Confinement Compute                                      |
|        └──────► 8c. GPU Particle Physics Compute (Mass-Spring, Curl Noise, Fluid Advection)           |
|        │                                                                                              |
|        ▼                                                                                              |
|  [PHASE 09: Scene Visibility Culling & 3D Clustered Light Binning] (16x16x32 Clusters)                |
|        │                                                                                              |
|        ▼                                                                                              |
|  [PHASE 10: Render Graph Compilation & GPU Submission]                                                |
|        │                                                                                              |
|        ├──────► 10a. Meshlet Culling & Software Rasterizer / Hardware Raster                          |
|        ├──────► 10b. Visibility Buffer Output [InstanceID | TriangleID] + Depth                       |
|        ├──────► 10c. Material Evaluation Pass (Vertex fetch, barycentrics, texture sampling)          |
|        ├──────► 10d. Tier 1 (GTX): Screen-Space ReSTIR DI/GI / Compute Raymarching                   |
|        ├──────► 10e. Tier 2 (RTX): Hardware DXR BVH Ray Query ReSTIR DI & GI                          |
|        ├──────► 10f. Volumetric Gas Raymarching (Henyey-Greenstein) & GPU Particle Render             |
|        └──────► 10g. Post-Processing (Tonemapping, TAA / AMD FSR 2.2 / NVIDIA DLSS 3.5)              |
|        │                                                                                              |
|        ▼                                                                                              |
|  [PHASE 11: 3D Spatial Audio Pipeline Update]                                                         |
|        │  (Listener geometry, Acoustic Occlusion Raymarch, Bus DSP, Reverb, EOS Voice)                |
|        │                                                                                              |
|        ▼                                                                                              |
|  [PHASE 12: Epic Online Services (EOS) Outbound Sync & Packet Dispatch]                               |
|        │                                                                                              |
|        ▼                                                                                              |
|  [PHASE 13: Decoupled Editor Bridge Sync / Viewport Presentation]                                     |
|        │                                                                                              |
|        ▼                                                                                              |
|  [PHASE 14: Frame Memory Clean & Ring Buffer Recycling] (O(1) Linear Frame Allocator Reset)          |
|                                                                                                       |
+-------------------------------------------------------------------------------------------------------+
```

---

## 3. Operational Execution Profiles

1. **Headless Dedicated Server (`SLATE_MODE_HEADLESS_SERVER`)**:
   - Disables GPU rasterization, presentation, and audio DAC output.
   - Executes authoritative fixed-rate Jolt rigid body physics, Custom XPBD softbodies, EOS Anti-Cheat server lifecycle, and network state replication.
   - Operates with minimal CPU overhead on Linux cloud instances.

2. **Standalone Client Runtime (`SLATE_MODE_STANDALONE_CLIENT`)**:
   - Direct swapchain presentation to OS window with low-latency frame pacing (NVIDIA Reflex / AMD Anti-Lag).

3. **Decoupled Editor Viewport (`SLATE_MODE_EDITOR`)**:
   - Renders offscreen into a shared render target texture handle.
   - Supports Play-In-Editor (PIE) pause, step-by-step frame ticking, and snapshot state restoration.
