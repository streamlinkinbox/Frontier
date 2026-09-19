# Slate Simulation Subsystem: Dedicated Fluid/Gas SDF, Navier-Stokes, & GPU Particles

## 1. Scope & Role of Signed Distance Fields (SDF) in Slate

> **Architectural Boundary Principle**:
> In Slate, **Signed Distance Fields (SDF) are used EXCLUSIVELY for Fluid, Smoke, and Gas simulation and surface tracking**.
> 
> **Game objects, terrain, and characters are NOT represented by SDFs**—they are represented by standard triangle meshes rasterized through the **Visibility Buffer / Software Rasterizer** and simulated via **Jolt Rigid Body Physics** and **Custom XPBD Softbody Physics**.

### 1.1 Why SDFs are Dedicated Strictly to Fluid and Gas Simulation:
1. **Free-Surface Liquid Tracking**: A continuous 3D scalar field $\phi(\mathbf{x})$ tracks the moving liquid interface where the fluid surface is defined by the zero level-set $\phi(\mathbf{x}) = 0$.
2. **Solid Obstacle Boundary Conditions**: The fluid simulation domain maintains a 3D distance field of scene obstacles (e.g. walls, containers) to rapidly enforce no-penetration boundary conditions $(\mathbf{u} \cdot \mathbf{n} = 0)$ and free-slip / no-slip boundary masks without expensive triangle queries.
3. **Smoke & Gas Volume Reconstruction**: Smoke density gradients and flame reaction zones are tracked along level-set shells for accelerated raymarching and sharp interface rendering.

---

## 2. 3D SDF Loop / Sequence for Fluid & Gas

The fluid domain executes a dedicated 3D SDF update sequence every fixed simulation substep:

```
+-----------------------------------------------------------------------------------------------+
|                                  FLUID / GAS SDF SIMULATION LOOP                              |
+-----------------------------------------------------------------------------------------------+
|                                                                                               |
|  [Step 1: Solid Obstacle Voxelization]                                                        |
|        │  - Ingests rigid body and static geometry bounding proxies into local fluid grid    |
|        ▼                                                                                      |
|  [Step 2: Narrow-Band Fast Sweeping / Jump Flooding]                                          |
|        │  - Builds signed distance field phi(x) within a narrow band of +/- 6 voxels          |
|        ▼                                                                                      |
|  [Step 3: Boundary Mask Generation]                                                           |
|        │  - Marks cells where phi(x) <= 0 as solid obstacles                                  |
|        ▼                                                                                      |
|  [Step 4: Navier-Stokes Boundary Enforcement]                                                 |
|        │  - Projects fluid velocity u at solid cells: u_solid = u_obstacle                    |
|        ▼                                                                                      |
|  [Step 5: Free-Surface Level-Set Advection]                                                   |
|        │  - Advects liquid interface phi(x) along velocity streamlines: dphi/dt + u . grad(phi) = 0
|        ▼                                                                                      |
|  [Step 6: Level-Set Re-initialization (Eikonal Equation)]                                    |
|        │  - Solves ||grad(phi)|| = 1 to preserve true Euclidean distance after advection      |
|                                                                                               |
+-----------------------------------------------------------------------------------------------+
```

### 2.1 Eikonal Re-initialization
After advection, the level-set field distorts. The simulation solves the Eikonal PDE to restore Euclidean distances:
$$\frac{\partial \phi}{\partial \tau} + \text{sign}(\phi_0)(\|\nabla \phi\| - 1) = 0$$

---

## 3. 3D Eulerian Grid Fluid Simulation

Slate simulates incompressible fluids using the 3D Eulerian Navier-Stokes equations:
$$\frac{\partial \mathbf{u}}{\partial t} = -(\mathbf{u} \cdot \nabla)\mathbf{u} - \frac{1}{\rho}\nabla p + \nu \nabla^2 \mathbf{u} + \mathbf{f}_{ext}$$
$$\nabla \cdot \mathbf{u} = 0 \quad \text{(Incompressibility Constraint)}$$

### 3.1 Simulation Compute Steps
1. **Semi-Lagrangian / MacCormack Advection**:
   - Backtraces along the velocity field $\mathbf{u}$ over $\Delta t$: $\mathbf{x}_{prev} = \mathbf{x} - \mathbf{u}(\mathbf{x}) \Delta t$.
   - Uses MacCormack error correction (BFECC) to eliminate numerical diffusion and preserve swirling vortex details.
2. **Force Injection**:
   - Injects gravity $\mathbf{g} = (0, -9.81, 0)$, emitter momentum, and user interactions.
3. **Boundary Condition Enforcement**:
   - Queries the Fluid Domain SDF $\phi(\mathbf{x})$. For any cell where $\phi(\mathbf{x}) \le 0$, normal velocity is canceled: $\mathbf{u}_{normal} = 0$.
4. **Divergence Computation**:
   $$\nabla \cdot \mathbf{u} = \frac{u_{x+1} - u_{x-1}}{2\Delta x} + \frac{u_{y+1} - u_{y-1}}{2\Delta y} + \frac{u_{z+1} - u_{z-1}}{2\Delta z}$$
5. **Pressure Poisson Solver (Jacobi / Red-Black Gauss-Seidel)**:
   - Solves $\nabla^2 p = \frac{\rho}{\Delta t} \nabla \cdot \mathbf{u}$ over 10-20 iterations on GPU compute.
6. **Pressure Gradient Subtraction (Projection)**:
   - Projects the velocity field into a divergence-free state: $\mathbf{u}^{n+1} = \mathbf{u}^* - \frac{\Delta t}{\rho} \nabla p$.

---

## 4. Gas Dynamics & Smoke/Fire Simulation

Gas dynamics extends the fluid solver with thermodynamic buoyancy and vorticity confinement:

### 4.1 Thermal Buoyancy
$$\mathbf{f}_{buoy} = \left(-\alpha \rho_{smoke} + \beta (T - T_{ambient})\right) \mathbf{g}$$
- $\alpha$: Mass density coefficient (causes cool smoke to sink).
- $\beta$: Thermal expansion coefficient (causes hot gas/fire to rise rapidly).
- $T$: Temperature field in Kelvin (dissipates dynamically via Newton's law of cooling).

### 4.2 Vorticity Confinement
To counteract numerical grid dissipation and maintain micro-scale turbulent vortices:
1. Compute vorticity: $\boldsymbol{\omega} = \nabla \times \mathbf{u}$.
2. Compute vorticity magnitude gradient: $\boldsymbol{\eta} = \nabla \|\boldsymbol{\omega}\|$.
3. Compute unit direction: $\mathbf{N} = \frac{\boldsymbol{\eta}}{\|\boldsymbol{\eta}\| + \epsilon}$.
4. Apply confinement force: $\mathbf{f}_{vort} = \epsilon_{vort} \Delta x (\mathbf{N} \times \boldsymbol{\omega})$.

### 4.3 Volumetric Raymarching & Henyey-Greenstein Scattering
Raymarched in a single compute pass through the 3D density and temperature volume:
- **Transmittance**: $T(t) = \exp\left(-\sigma_t \int_0^t \rho(s) ds\right)$.
- **In-Scattering**: $L_{in}(t) = \sigma_s \rho(t) L_{light}(t) \cdot P(\theta) + L_{emission}(T)$.
- **Phase Function (Henyey-Greenstein)**:
  $$P(\theta) = \frac{1 - g^2}{4\pi (1 + g^2 - 2g\cos\theta)^{3/2}}$$

---

## 5. GPU Compute Particle Physics

Massive GPU particle simulations (up to 2,000,000 active particles) run purely on compute:

### 5.1 Particle Compute Kernel Workflow
1. **Euler / Verlet Integration**: Position $\mathbf{p} \leftarrow \mathbf{p} + \mathbf{v}\Delta t$, velocity updated with gravity and drag.
2. **Curl Noise Turbulence**: Evaluates 3D divergence-free curl noise: $\mathbf{v}_{curl} = \nabla \times \mathbf{\Psi}(\mathbf{p})$.
3. **Fluid Grid Advection**: Reads fluid velocity $\mathbf{u}_{fluid}(\mathbf{p})$ from the Navier-Stokes grid to simulate drifting sparks, bubbles, and debris carried by air/water currents.
4. **Boundary Deflection**: Deflects against fluid boundary fields and ground planes with configurable restitution $e$ and friction $\mu$.
