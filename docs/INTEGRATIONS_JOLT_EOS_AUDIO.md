# Slate Subsystem Integrations: Dual Physics (Jolt + Custom Softbody XPBD), EOS, & Spatial Audio

## 1. Dual Physics Architecture (Jolt Physics + Custom XPBD Softbody)

Slate employs a **Dual Physics Architecture** to achieve uncompromising simulation breadth:
1. **Jolt Physics**: Industry-standard rigid-body dynamics, continuous collision detection (CCD), character controllers, vehicle physics, and broadphase queries.
2. **Custom Softbody Physics (XPBD)**: Extended Position-Based Dynamics (XPBD) engine running alongside Jolt to simulate deformable objects, elastic tetrahedral solids, cloth sheets, and volume-conserving soft bodies that Jolt rigid body physics does not handle.

```
+-----------------------------------------------------------------------------------------------+
|                                    SLATE DUAL PHYSICS PIPELINE                                |
+-----------------------------------------------------------------------------------------------+
|                                                                                               |
|  [Slate Job System] ◄────────────────────────────────────────► [Task Worker Threads]         |
|                                                                                               |
|  ┌─────────────────────────────────────────┐   ┌───────────────────────────────────────────┐  |
|  │        JOLT RIGID BODY SUBSYSTEM        │   │    CUSTOM XPBD SOFTBODY & CLOTH SUBSYSTEM │  |
|  ├─────────────────────────────────────────┤   ├───────────────────────────────────────────┤  |
|  │ * BroadPhaseLayerInterface              │   │ * XPBD Particle Nodes (x, p, v, invMass)  │  |
|  │ * Dynamic / Kinematic / Static Bodies   │   │ * Distance & Elastic Spring Constraints   │  |
|  │ * Continuous Collision Detection (CCD)  │   │ * Tetrahedral Volume Conservation (3D)    │  |
|  │ * ContactListener & ActivationListener  │   │ * Isometric Bending Constraints (Cloth)   │  |
|  │ * CharacterVirtual Controller           │   │ * Sub-stepped XPBD Compliance (alpha, dt) │  |
|  └─────────────────────────────────────────┘   └───────────────────────────────────────────┘  |
|                         │                                             │                       |
|                         └──────────────────────┬──────────────────────┘                       |
|                                                ▼                                              |
|  [Two-Way Coupling]: Softbody particles collide with Jolt rigid body hulls and kinematic shapes |
|                                                                                               |
+-----------------------------------------------------------------------------------------------+
```

### 1.1 Custom Softbody XPBD Formulation
For deformable bodies (sponges, organic tissue, cloth, inflatables), Slate implements Extended Position-Based Dynamics (XPBD) with time-step independent compliance $\alpha$:

1. **Position Prediction**:
   $$\tilde{\mathbf{x}}_i = \mathbf{x}_i + \mathbf{v}_i \Delta t + \mathbf{g} \Delta t^2$$
2. **Tetrahedral Volume Conservation Constraint**:
   For each tetrahedron with vertices $(\mathbf{x}_1, \mathbf{x}_2, \mathbf{x}_3, \mathbf{x}_4)$ and rest volume $V_0$:
   $$C_{vol}(\mathbf{x}_1, \mathbf{x}_2, \mathbf{x}_3, \mathbf{x}_4) = \frac{1}{6} (\mathbf{x}_2 - \mathbf{x}_1) \cdot \left((\mathbf{x}_3 - \mathbf{x}_1) \times (\mathbf{x}_4 - \mathbf{x}_1)\right) - V_0$$
   The Lagrange multiplier correction $\Delta \lambda$ is:
   $$\Delta \lambda = \frac{-C(\mathbf{x}) - \tilde{\alpha} \lambda}{\sum_i w_i \|\nabla_{\mathbf{x}_i} C\|^2 + \tilde{\alpha}}, \quad \tilde{\alpha} = \frac{\alpha}{\Delta t^2}$$
   $$\Delta \mathbf{x}_i = w_i \nabla_{\mathbf{x}_i} C \Delta \lambda$$
3. **Distance / Edge Constraint**:
   $$C_{dist}(\mathbf{x}_1, \mathbf{x}_2) = \|\mathbf{x}_1 - \mathbf{x}_2\| - L_0$$
4. **Velocity Update & Collision Resolution**:
   $$\mathbf{v}_i = \frac{\mathbf{x}_i - \mathbf{x}_i^{prev}}{\Delta t}$$

---

## 2. Epic Online Services (EOS) Subsystem

Slate encapsulates the **Epic Online Services (EOS) C SDK** with an asynchronous, deterministic manager ticked in Phase 03 of the engine loop:

```
+-----------------------------------------------------------------------------------------------+
|                                     EOS ARCHITECTURE IN SLATE                                 |
+-----------------------------------------------------------------------------------------------+
|                                                                                               |
|  +-----------------------------------------------------------------------------------------+  |
|  |                              EOS Platform Handle (EOS_HPlatform)                        |  |
|  |                      Ticked deterministically in Engine Loop Phase 03                   |  |
|  +-----------------------------------------------------------------------------------------+  |
|           │                                │                               │                  |
|           ▼                                ▼                               ▼                  |
|  +------------------+            +-------------------+           +-------------------+        |
|  |  Auth / Connect  |            |  P2P & Sessions   |           |  Anti-Cheat &     |        |
|  |  Interface       |            |  Interface        |           |  EOS Voice        |        |
|  +------------------+            +-------------------+           +-------------------+        |
|  | * Epic Account   |            | * NAT Punching    |           | * Client integrity|        |
|  | * Device ID      |            | * Lobby Creation  |           | * Server monitor  |        |
|  | * Token Refresh  |            | * Matchmaking     |           | * 3D Positional   |        |
|  | * User Presence  |            | * Reliable Packets|           |   Voice Rooms     |        |
|  +------------------+            +-------------------+           +-------------------+        |
|                                                                                               |
+-----------------------------------------------------------------------------------------------+
```

### 2.1 Core Capabilities
- **Authentication**: Epic Account Services (EAS) and anonymous Device ID logins with automatic token refreshing.
- **P2P NAT Punching**: Zero-allocation reliable and unreliable packet dispatch between peers without requiring dedicated relay servers for small lobbies.
- **Lobbies & Matchmaking**: Dynamic lobby creation, search attributes, player invites, and state replication.
- **Anti-Cheat Lifecycle**: Integrated Easy Anti-Cheat (EAC) client integrity checks and authoritative server validation hooks.
- **WebRTC 3D Voice Rooms**: Positional audio room integration streaming spatialized peer voice channels.

---

## 3. Spatial Audio Subsystem

Slate integrates a high-performance 3D spatial audio mixer with physical acoustic propagation:

```
+-----------------------------------------------------------------------------------------------+
|                                      SPATIAL AUDIO PIPELINE                                   |
+-----------------------------------------------------------------------------------------------+
|                                                                                               |
|   [Sound Emitted at World Pos P_s]                                                            |
|                 │                                                                             |
|                 ▼                                                                             |
|   [Acoustic Occlusion Raymarch: Source P_s -> Listener P_l]                                   |
|                 │                                                                             |
|                 ├──► Is Direct Path Obstructed by Geometry?                                   |
|                 │      ├── Yes: Calculate Obstruction Distance & Material Absorption          |
|                 │      │        Apply Dynamic Low-Pass Filter (cutoff: 800Hz - 2000Hz)       |
|                 │      │        Increase Reverb Wet Send Level (early reflections)            |
|                 │      └── No:  Direct Line of Sight (Full 20kHz bandwidth)                   |
|                 ▼                                                                             |
|   [3D HRTF & Panning Evaluation] (Azimuth & Elevation constant-power panning)                 |
|                 │                                                                             |
|                 ▼                                                                             |
|   [Multi-Bus DSP Hierarchy]                                                                   |
|         ├──────► SFX Bus (World, Weapons, Debris) ─────────┐                                  |
|         ├──────► Music Bus (Dynamic Score)      ───────────┼──► Master Bus ──► Audio Output   |
|         ├──────► Voice Bus (EOS Voice Stream)   ───────────┤                                  |
|         ├──────► Ambience Bus                   ───────────┤                                  |
|         └──────► Convolution Reverb Bus         ───────────┘                                  |
|                                                                                               |
+-----------------------------------------------------------------------------------------------+
```

### 3.1 Acoustic Attenuation & Distance Models
- **Inverse Square Law**: $G(d) = \frac{d_{min}}{d_{min} + \text{rolloff} \cdot (d - d_{min})}$.
- **Dynamic Occlusion Low-Pass Filter**: When a sound source is occluded by walls, a multi-pole low-pass filter dynamically attenuates high frequencies:
  $$f_{cutoff} = 20000\text{Hz} \cdot (1 - \Omega) + 800\text{Hz} \cdot \Omega, \quad \Omega \in [0, 1]$$
