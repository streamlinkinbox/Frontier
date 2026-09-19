# Slate Game Engine: State-of-the-Art Architecture Master Plan

## 1. Executive Summary & Core Architectural Pillars

**Slate** is a high-performance, data-oriented C++20 game engine architecture designed for extreme simulation fidelity, scalable visual realism, and modular runtime decoupling.

The engine architecture is structured around seven core pillars:

1. **Deterministic Custom Dual-Clock Engine Loop**:
   - High-precision dual-clock pipeline decoupling fixed-frequency physics substepping ($\Delta t_{fixed} = 1/60\text{s}$ or $1/120\text{s}$) from variable-rate frame rendering with accumulator-based state interpolation ($\alpha = \frac{t_{accum}}{\Delta t_{fixed}}$).
   - Strict 14-phase deterministic frame execution pipeline.
2. **Visibility Buffer & Software Rasterizer Frontend**:
   - Decoupled geometry pass outputting compact visibility IDs (`[InstanceID | TriangleID]`).
   - Software rasterizer pipeline for fine cluster/meshlet binning with sub-pixel edge equations and Hierarchical-Z (Hi-Z) culling.
   - Material Evaluation Pass that fetches vertex attributes, computes analytic screen-space derivatives, and evaluates materials only on visible surface pixels before shading.
3. **Hardware Scalability (GTX 1060 Pascal to RTX 4090+ Ada)**:
   - Dynamic GPU hardware capability classification at startup.
   - **Tier 1 (GTX 1060 / Non-RTX)**: Clustered Deferred Shading, Screen-Space ReSTIR, Compute Raymarching, SDF Contact Shadows, FSR 2.2 / TAA.
   - **Tier 2 (RTX 20/30/40 / DXR 1.1)**: Hardware Top-Level/Bottom-Level Acceleration Structures (TLAS/BLAS), Hardware ReSTIR Direct Illumination (DI) and Global Illumination (GI), DLSS 3.5 / Ray Reconstruction.
4. **Global Illumination using ReSTIR (Reservoir-based Spatiotemporal Importance Resampling)**:
   - **ReSTIR DI**: Streams millions of dynamic emissive triangles and point/spot lights with initial candidate sampling, temporal reuse, and spatial cross-bilateral reuse.
   - **ReSTIR GI**: Indirect radiosity path resampling across spatial-temporal neighbors using the **Jacobian determinant shift** to correct for screen-space geometric disparities.
5. **Dual Physics Architecture (Jolt Physics + Custom XPBD Softbody Physics)**:
   - **Jolt Physics**: Multi-core rigid body dynamics, continuous collision detection (CCD), collision filtering layers, character virtual controllers, and constraint solving.
   - **Custom Softbody & Deformable Physics (XPBD)**: Extended Position-Based Dynamics solver running alongside Jolt in the fixed substep loop to simulate soft bodies, elastic tetrahedral solids (with volume conservation), cloth sheets, and custom non-rigid constraints that Jolt cannot handle.
6. **Dedicated Fluid & Gas SDF Simulation Subsystem**:
   - **Signed Distance Field (SDF)** is used **strictly for fluid and smoke/gas dynamics** (liquid free-surface level-set tracking $\phi(\mathbf{x}) = 0$, solid obstacle boundary conditions, and volumetric density fields). Game objects are represented via triangle meshes and Visibility Buffer / Jolt / XPBD physics.
   - **3D Eulerian Navier-Stokes Solver**: Semi-Lagrangian / MacCormack advection, incompressibility divergence projection ($\nabla \cdot \mathbf{u} = 0$), and multi-iteration Poisson pressure solver.
   - **Buoyant Gas & Smoke Dynamics**: Thermal buoyancy force ($\mathbf{f}_{buoy} = (-\alpha \rho + \beta(T - T_{amb}))\mathbf{g}$), vorticity confinement ($\mathbf{f}_{vort} = \epsilon_{vort} \Delta x (\mathbf{N} \times \boldsymbol{\omega})$), and Henyey-Greenstein volumetric raymarching.
   - **GPU Compute Particle Physics**: Ballistic mass-spring integration, curl noise turbulence, fluid velocity advection, and deflection against boundary fields.
7. **First-Class Subsystem Integrations & Decoupled Editor**:
   - **Epic Online Services (EOS)**: Connect/Auth, P2P NAT punching, Lobbies/Sessions, Anti-Cheat client/server lifecycle, WebRTC 3D Voice rooms.
   - **Spatial Audio Engine**: 3D HRTF listener, acoustic occlusion raymarching, multi-bus DSP hierarchy (Master, SFX, Music, Voice, Ambience), convolution reverb, and EOS Voice routing.
   - **Decoupled Slate Editor**: Complete runtime isolation, offscreen shared viewport target, Play-In-Editor (PIE) state machine, and real-time profiler telemetry streaming.

---

## 2. High-Level System Topology Blueprint

```
+---------------------------------------------------------------------------------------------------------------+
|                                            SLATE ENGINE ARCHITECTURE                                          |
+---------------------------------------------------------------------------------------------------------------+
|                                                                                                               |
|  +-------------------------------------------+               +---------------------------------------------+  |
|  |           Decoupled Slate Editor          | <-----------> |       Epic Online Services (EOS) SDK        |  |
|  |   (ImGui / Slate UI, Viewport Bridge,     |               |   (Auth, Sessions, P2P NAT, Anti-Cheat,     |  |
|  |    PIE State Machine, Profiler Telemetry) |               |    WebRTC Positional 3D Voice Rooms)        |  |
|  +-------------------------------------------+               +---------------------------------------------+  |
|                         │                                                           │                         |
|                         │ (Offscreen Render Target / IPC Shared Memory)             │ (Network / Voice Ingest)|
|                         ▼                                                           ▼                         |
|  +---------------------------------------------------------------------------------------------------------+  |
|  |                                         CUSTOM GAME ENGINE LOOP                                         |  |
|  |                                                                                                         |  |
|  |   [Input & OS] ──► [EOS Tick] ──► [Pre-Physics] ──► [Fixed Physics Substep Loop] ──► [Post-Physics]      |  |
|  |                                                              │                                          |  |
|  |                                      ┌───────────────────────┴───────────────────────┐                  |  |
|  |                                      ▼                                               ▼                  |  |
|  |                            +--------------------+                         +--------------------+        |  |
|  |                            |    Jolt Physics    |                         |  Custom XPBD Soft- |        |  |
|  |                            |  (Rigid Bodies,    |                         |  body & Cloth Sim  |        |  |
|  |                            |   CCD, Characters) |                         |  (Tetrahedrals)    |        |  |
|  |                            +--------------------+                         +--------------------+        |  |
|  |                                                                                                         |  |
|  |   [Animation] ──► [Fluid / Gas SDF Sim] ──► [Visibility Buffer & Software Raster] ──► [Render Graph]    |  |
|  |                                                              │                                          |  |
|  |                                      ┌───────────────────────┴───────────────────────┐                  |  |
|  |                                      ▼                                               ▼                  |  |
|  |                            +--------------------+                         +--------------------+        |  |
|  |                            | Tier 1: GTX 1060   |                         | Tier 2: RTX 20/30  |        |  |
|  |                            | (Clustered Def,    |                         | (Hardware DXR RT,  |        |  |
|  |                            |  SS-ReSTIR DI/GI)  |                         |  ReSTIR DI & GI)   |        |  |
|  |                            +--------------------+                         +--------------------+        |  |
|  |                                                                                                         |  |
|  |   [Spatial Audio & Occlusion] ──► [EOS Outbound Sync] ──► [Decoupled Viewport Present] ──► [Mem Reset]  |  |
|  +---------------------------------------------------------------------------------------------------------+  |
|                                                                                                               |
|  +---------------------------------------------------------------------------------------------------------+  |
|  |                                            CORE PLATFORM SERVICES                                       |  |
|  |   * Work-Stealing Fiber / Task Job DAG Scheduler      * Linear Frame, Typed Pool & Virtual Ring Allocators|  |
|  |   * SIMD Vector / Matrix / Quaternion Math            * Lock-Free Pub/Sub Event Dispatcher Bus          |  |
|  |   * Hardware Feature Detection & Permutation Manager  * Cross-Platform RHI (Vulkan 1.3 / Direct3D 12)   |  |
|  +---------------------------------------------------------------------------------------------------------+  |
+---------------------------------------------------------------------------------------------------------------+
```

---

## 3. Data-Oriented Design (DOD) & Memory Hierarchy

Slate is architected to maximize CPU L1/L2/L3 cache residency, saturate memory bus bandwidth, and eliminate dynamic runtime heap allocations during gameplay:

1. **Linear Frame Allocators (Double/Triple-Buffered)**:
   - Per-worker thread contiguous memory arenas (default: 64MB).
   - Transient allocations made during a frame tick advance an atomic pointer with $O(1)$ overhead.
   - At Phase 14 (Frame Memory Reset), the pointer is set back to `0` with zero heap fragmentation.
2. **Typed Fixed-Size Object Pools**:
   - Intrusive freelist pools for dynamic transient entities (particle bursts, sound voice instances, collision contact manifolds).
   - Guarantees $O(1)$ allocation and deallocation without memory fragmentation.
3. **Cache-Aligned Component Tables (Sparse-Set ECS)**:
   - Entity data is stored in contiguous, 64-byte aligned arrays.
   - Hot transform matrices ($4 \times 4$ SIMD floats) and velocities are stored separately from cold metadata (names, asset paths) to maximize SIMD vectorization during visibility passes and physics sync.
4. **GPU Upload / Readback Ring Buffers**:
   - Triple-buffered persistent-mapped host-visible staging memory for updating instance transforms, light descriptors, and ReSTIR constants without CPU-GPU pipeline bubbles.

---

## 4. Hardware Scalability Strategy: GTX 1060 to RTX 4090+

Slate dynamically queries GPU feature flags at initialization and configures the rendering and simulation pipelines into one of two operational tiers:

```
                                      +-------------------------------+
                                      | Hardware Capability Detection |
                                      +-------------------------------+
                                                      │
                            ┌─────────────────────────┴─────────────────────────┐
                            ▼                                                   ▼
                +-----------------------+                           +-----------------------+
                |     TIER 1 (GTX)      |                           |      TIER 2 (RTX)     |
                |  GTX 1060 / Pascal /  |                           | RTX 20/30/40 / DXR1.1 |
                | Non-RayTracing Cores  |                           | Hardware RT Cores     |
                +-----------------------+                           +-----------------------+
                | * Visibility Buffer & |                           | * Visibility Buffer & |
                |   Software Raster     |                           |   Software / HW Mesh  |
                | * Clustered Deferred  |                           | * Hybrid Deferred     |
                | * Screen-Space ReSTIR |                           | * Hardware BVH DXR    |
                | * Compute Raymarch GI |                           | * Hardware ReSTIR GI  |
                | * SDF Contact Shadows |                           | * ReSTIR DI Reservoirs|
                | * AMD FSR 2.2 / TAA   |                           | * NVIDIA DLSS 3.5     |
                +-----------------------+                           +-----------------------+
```

### Feature Comparison Matrix

| Subsystem Component | Tier 1: GTX 1060 (Pascal / Non-RTX) | Tier 2: RTX 20/30/40 / Modern DXR |
|---|---|---|
| **Target Resolution / FPS** | 1080p @ 60 FPS | 1440p - 4K @ 60 - 144 FPS |
| **Geometry Frontend** | Visibility Buffer + Compute Software Raster | Visibility Buffer + Mesh Shaders / VisBuffer |
| **Direct Illumination** | Clustered Light Binning + Screen-Space ReSTIR DI | Hardware DXR Ray Traced ReSTIR DI (Millions of lights) |
| **Global Illumination** | Screen-Space ReSTIR GI + Compute SDF Fallback | Hardware Ray Traced ReSTIR GI (Spatiotemporal Reservoirs) |
| **Shadows** | Cascaded Shadow Maps (CSM) + Compute SDF Contact Shadows | Ray Traced Soft Penumbra Shadows via BVH Traversal |
| **Reflections** | Screen-Space Reflections (SSR) + Fallback Cube/SDF | Hardware DXR Ray Traced Multi-Bounce Reflections |
| **Gas & Volumetrics** | 3D Eulerian Grid Raymarcher with Henyey-Greenstein | Hardware Accelerated Heterogeneous Volume Scattering |
| **Upscaling & Anti-Aliasing**| AMD FSR 2.2 / Temporal Anti-Aliasing (TAA) | NVIDIA DLSS 3.5 (Super Resolution + Ray Reconstruction) |
