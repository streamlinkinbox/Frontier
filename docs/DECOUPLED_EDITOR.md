# Slate Decoupled Editor Architecture

## 1. Architectural Decoupling Principles

Slate enforces a **strict decoupling boundary** between the engine runtime and the editor tooling layer:
1. **Zero Compile-Time UI Dependencies in Runtime Core**: The core runtime engine (`SlateCore`, `SlateRender`, `SlatePhysics`) has no direct dependency on editor UI code or widgets.
2. **Standardized Viewport Bridge**: The engine renders directly into an offscreen render target texture. The Editor samples this texture as a GPU handle or reads it through a shared-memory buffer without stalling the engine tick.
3. **Remote Telemetry & Command Protocol**: The editor orchestrates the engine via a lightweight command protocol and receives lock-free telemetry streams.

```
+-----------------------------------------------------------------------------------------------+
|                                  DECOUPLED EDITOR ARCHITECTURE                                |
+-----------------------------------------------------------------------------------------------+
|                                                                                               |
|  +-----------------------------------------------------------------------------------------+  |
|  |                                  SLATE EDITOR UI LAYER                                  |  |
|  |  * Scene Hierarchy Tree Inspector       * Property Inspector & Reflection Metadata      |  |
|  |  * Asset Browser & Live Shader Editor   * Real-time Performance & Profiler Graphs       |  |
|  |  * 3D Gizmo Manipulators (Translate/Rot) * Play-In-Editor (PIE) Control Toolbar         |  |
|  +-----------------------------------------------------------------------------------------+  |
|                                            │                                                  |
|                   Command Protocol         │  Shared Viewport Texture                         |
|                   & Telemetry Feed         │  (GPU Descriptor Handle / Zero-Copy Staging)     |
|                                            ▼                                                  |
|  +-----------------------------------------------------------------------------------------+  |
|  |                                  SLATE ENGINE RUNTIME                                   |  |
|  |                                                                                         |  |
|  |  * EngineLoop (14-Phase Dual-Clock Pipeline)                                            |  |
|  |  * Jolt Physics & Custom XPBD Softbody Physics Substepping                              |  |
|  |  * Visibility Buffer & Software Rasterizer Frontend                                     |  |
|  |  * ReSTIR GI/DI Scalable Render Graph (Outputs to Offscreen Viewport Target)            |  |
|  |  * Dedicated Fluid & Gas SDF Simulation Subsystem                                       |  |
|  |  * Spatial Audio & EOS Online Subsystem                                                 |  |
|  +-----------------------------------------------------------------------------------------+  |
|                                                                                               |
+-----------------------------------------------------------------------------------------------+
```

---

## 2. Play-In-Editor (PIE) State Machine & Snapshotting

```
                +-------------------+
                |    EDIT MODE      | <------------------------------------+
                | (Transforms Edit) |                                      |
                +-------------------+                                      |
                          |                                                |
                   [Play Button]                                      [Stop Button]
                   (Snapshot ECS World)                               (Restore Snapshot)
                          |                                                |
                          v                                                |
                +-------------------+     [Pause Button]     +-------------------+
                |   PIE: PLAYING    | ---------------------> |    PIE: PAUSED    |
                | (Full Simulation) | <--------------------- | (Step Single Frame|
                +-------------------+     [Resume Button]    +-------------------+
```

### Snapshot & Restore Sequence:
1. When entering PIE mode from Edit mode, the Editor captures a lightweight delta snapshot of all dynamic transform, rigid body, softbody particle, and fluid grid states.
2. The simulation runs live with Jolt physics, XPBD softbodies, fluid advection, and particle emitters.
3. Upon clicking Stop, the world state instantly reverts to the pre-simulation snapshot, resetting actor transforms, softbody deformations, and spawned debris without reloading scene assets from disk.

---

## 3. Remote Telemetry & Profiling Channel

The Editor Bridge streams frame telemetry over a lock-free ring buffer:
- **Phase Timing Breakdown**: CPU microseconds spent in Pre-Physics, Jolt Step, Custom Softbody Step, Fluid Sim, ReSTIR GI, Visibility Buffer rasterization, Render Submit, Audio, and EOS.
- **GPU Profiler Timings**: Hardware timestamp query measurements for Meshlet Culling, Visibility Buffer, Material Evaluation, ReSTIR DI/GI, Fluid Poisson iterations, and Post-processing.
- **Memory Tracking**: High-water mark allocations for linear frame allocators, typed pools, and GPU VRAM resident buffers.
