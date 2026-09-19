# Slate Game Engine: State-of-the-Art Architecture Plan

[![Architecture](https://img.shields.io/badge/Architecture-Data--Oriented-green.svg)](docs/ARCHITECTURE_OVERVIEW.md)
[![Dual-Clock Loop](https://img.shields.io/badge/Engine--Loop-Dual--Clock%20(14--Phase)-blue.svg)](docs/ENGINE_LOOP.md)
[![Visibility Buffer](https://img.shields.io/badge/Renderer-Visibility%20Buffer%20%2B%20Software%20Raster-purple.svg)](docs/RENDER_PIPELINE.md)
[![Physics](https://img.shields.io/badge/Physics-Jolt%20%2B%20Custom%20XPBD%20Softbody-orange.svg)](docs/INTEGRATIONS_JOLT_EOS_AUDIO.md)
[![Online](https://img.shields.io/badge/Online-Epic%20Online%20Services-yellow.svg)](docs/INTEGRATIONS_JOLT_EOS_AUDIO.md)
[![Scalability](https://img.shields.io/badge/Hardware%20Scalability-GTX%201060%20to%20RTX%20(ReSTIR)-red.svg)](docs/RENDER_PIPELINE.md)

**Slate** is a high-performance, data-oriented game engine architecture plan designed for extreme physical simulation fidelity, visual scalability from GTX 1060 to RTX 4090+, and clean modular decoupling.

---

## Architectural Highlights & Core Pillars

### 1. Custom Game Engine Loop
- **Dual-Clock Execution**: Fixed simulation substepping ($\Delta t_{fixed} = 1/60\text{s}$ or $1/120\text{s}$) decoupled from variable-rate display rendering via accumulator interpolation ($\alpha = \frac{t_{accum}}{\Delta t_{fixed}}$).
- **14-Phase Deterministic Pipeline**: Strict scheduling from input/EOS ingestion, through pre-physics, Jolt + XPBD softbody steps, animation, dedicated fluid/gas SDF sequences, Visibility Buffer software rasterization, Material Evaluation, ReSTIR DI/GI shading, and spatial audio to decoupled presentation.
- **Operating Modes**: Headless Dedicated Server, Standalone Client Runtime, and Decoupled Editor.

### 2. Visibility Buffer & Software Rasterizer Frontend
- **Decoupled Geometry Representation**: Rasterizes 32-bit compact visibility identifiers (`[InstanceID: 18 bits | PrimitiveID: 14 bits]`) + depth.
- **Software Rasterizer Pipeline**: Compute-driven tile-based meshlet binning with fixed-point sub-pixel edge equations and Hierarchical-Z (Hi-Z) culling for fine geometry.
- **Material Evaluation Pass**: Evaluates vertex attributes, computes analytic screen-space derivatives, and samples textures **once per visible pixel** before shading.

### 3. Hardware Scalability (GTX 1060 to RTX 4090+)
- **Tier 1 (GTX 1060 / Pascal / Non-RTX)**:
  - Clustered Deferred Shading with 3D view frustum light binning ($16 \times 16 \times 32$ clusters).
  - Screen-Space ReSTIR Direct Illumination & Global Illumination with compute raymarching.
  - Cascaded Shadow Maps (CSM) + SDF Contact Shadows.
  - AMD FSR 2.2 / Spatial TAA upscaling.
- **Tier 2 (RTX 20/30/40 Series / Hardware DXR / VK_KHR_ray_tracing)**:
  - Hardware Top-Level & Bottom-Level Acceleration Structures (TLAS/BLAS).
  - Hardware ReSTIR Direct Illumination (ReSTIR DI) & Global Illumination (ReSTIR GI) with spatiotemporal reservoir reuse and Jacobian determinant shift.
  - Volumetric heterogeneous media raymarching with Henyey-Greenstein scattering.
  - NVIDIA DLSS 3.5 / Ray Reconstruction.

### 4. Dual Physics Architecture (Jolt + Custom XPBD Softbody)
- **Jolt Physics Integration**: Multi-threaded rigid body dynamics, continuous collision detection (CCD), broadphase layers, collision matrix filtering, and virtual character controllers.
- **Custom Softbody & Cloth Physics (XPBD)**: Extended Position-Based Dynamics engine running alongside Jolt in the fixed substep loop to simulate deformable bodies, elastic tetrahedral solids (with volume conservation), cloth sheets, and custom constraints that Jolt cannot handle.

### 5. Dedicated Fluid & Gas SDF Simulation Subsystem
- **SDF Scope**: **Signed Distance Fields (SDF) are dedicated strictly to fluid and smoke/gas simulations** (liquid free-surface level-set tracking $\phi(\mathbf{x}) = 0$, solid obstacle boundary conditions, and volumetric density fields). Game objects are represented via triangle meshes and Visibility Buffer / Jolt / XPBD physics.
- **3D Eulerian Navier-Stokes Solver**: MacCormack/BFECC advection, incompressibility divergence projection ($\nabla \cdot \mathbf{u} = 0$), and multi-iteration Poisson pressure solver.
- **Buoyant Gas & Smoke Dynamics**: Thermal buoyancy force ($\mathbf{f}_{buoy} = (-\alpha \rho + \beta(T - T_{amb}))\mathbf{g}$), vorticity confinement ($\mathbf{f}_{vort} = \epsilon_{vort} \Delta x (\mathbf{N} \times \boldsymbol{\omega})$), and Henyey-Greenstein volumetric raymarching.
- **GPU Particle Physics**: Ballistic Euler integration, curl noise turbulence, fluid velocity advection, and deflection against boundary fields.

### 6. Subsystem Integrations & Decoupled Editor
- **Epic Online Services (EOS)**: Connect/Auth, P2P NAT punching, Lobbies/Sessions, Anti-Cheat client/server lifecycle, WebRTC 3D Voice rooms.
- **Spatial Audio Engine**: 3D HRTF listener, acoustic occlusion raymarching, multi-bus DSP hierarchy (Master, SFX, Music, Voice, Ambience), convolution reverb, and EOS Voice routing.
- **Decoupled Slate Editor**: Complete runtime isolation, offscreen shared viewport target, Play-In-Editor (PIE) state machine, and real-time profiler telemetry streaming.

---

## Architecture Specification Suite

Detailed technical specifications and mathematical formulations are available in the [`docs/`](docs/) directory:

- [**Master Architecture Blueprint**](docs/ARCHITECTURE_OVERVIEW.md) — System topology, data-oriented design (DOD), memory hierarchy, and hardware tiers.
- [**Custom Game Engine Loop Specification**](docs/ENGINE_LOOP.md) — 14-Phase deterministic pipeline, dual-clock substepping, and state interpolation.
- [**Scalable Render Pipeline & ReSTIR Specification**](docs/RENDER_PIPELINE.md) — Visibility buffer, software rasterizer, ReSTIR DI/GI, and GTX 1060 to RTX scaling.
- [**Dedicated Fluid & Gas SDF Simulation Specification**](docs/SIMULATION_SDF_FLUIDS.md) — Liquid level-set tracking, obstacle fields, 3D Navier-Stokes, gas buoyancy/vorticity, and GPU particles.
- [**Dual Physics (Jolt + XPBD Softbody), EOS, & Audio Specification**](docs/INTEGRATIONS_JOLT_EOS_AUDIO.md) — Multi-core Jolt rigid bodies, Custom XPBD tetrahedral softbodies, EOS SDK, and 3D spatial audio.
- [**Decoupled Editor Architecture Specification**](docs/DECOUPLED_EDITOR.md) — Runtime isolation, offscreen viewport bridge, and Play-In-Editor (PIE) state machine.
