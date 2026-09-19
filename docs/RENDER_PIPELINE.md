# Slate Scalable Rendering Pipeline Specification

## 1. Visibility Buffer & Software Rasterizer Architecture

Slate uses a **Visibility Buffer (VisBuffer)** rendering paradigm to completely decouple geometric rasterization from material evaluation and shading.

### 1.1 The Problems Solved by Visibility Buffer
Traditional Deferred and Forward shading suffer from:
1. **G-Buffer VRAM Bloat**: Writing 4-6 fat G-Buffer render targets (Albedo, Normals, Roughness/Metallic, Motion Vectors, Depth) consumes 30-50 MB/frame and saturates memory bandwidth.
2. **Overdraw Shading Waste**: Complex pixel shaders execute multiple times per pixel on obscured geometry.
3. **Small Triangle / Dense Geometry Quad-Overdraw Waste**: GPUs rasterize in $2 \times 2$ pixel quads; sub-pixel triangles waste up to 75% of pixel shading throughput.

### 1.2 VisBuffer 32-bit & 64-bit Pixel Encoding
Instead of writing fat material attributes, the rasterizer outputs only a compact integer visibility identifier:

```
32-bit Compact Visibility Buffer Layout:
+-------------------------------+-------------------------------+
|     Instance ID (18 bits)     |     Primitive ID (14 bits)    |
|       (Up to 262,144 meshes)   |      (Up to 16,384 triangles) |
+-------------------------------+-------------------------------+

64-bit High-Precision Layout (with explicit Barycentrics):
+-------------------------------+-------------------------------+---------------+---------------+
|     Instance ID (24 bits)     |     Primitive ID (24 bits)    | Bary U (8 bit)| Bary V (8 bit)|
+-------------------------------+-------------------------------+---------------+---------------+
```

### 1.3 Software Rasterizer Pipeline
For dense geometry, micromesh clusters, and subpixel triangles, Slate executes a compute-driven **Software Rasterizer**:
1. **Meshlet / Cluster Binning**: Meshes are divided into 64-128 triangle meshlets. A compute culling pass tests meshlet bounding cones against the view frustum and Hierarchical-Z (Hi-Z) depth pyramid.
2. **Tile Binning**: Surviving meshlets are binned into $8 \times 8$ or $16 \times 16$ screen-space tiles.
3. **Fixed-Point Edge Equations**:
   Compute threads evaluate 2D fixed-point edge functions using sub-pixel precision:
   $$E_{ab}(x, y) = (x - x_a)(y_b - y_a) - (y - y_a)(x_b - x_a)$$
   If $E_{01} \ge 0$, $E_{12} \ge 0$, and $E_{20} \ge 0$, the pixel center lies inside the triangle.
4. **Depth Test & Atomic VisBuffer Write**:
   Interpolates $z_{NDC}$ and performs an atomic depth test (`InterlockedMin` on 32-bit depth/VisID uint) writing into the Visibility Buffer texture.

### 1.4 Material Evaluation Pass
After geometry rasterization finishes:
1. A fullscreen compute shader reads each pixel's `VisBufferPixel`.
2. If `VisBufferPixel.rawData == 0xFFFFFFFF`, the pixel is marked as background/sky.
3. For valid pixels:
   - Fetches instance metadata (world transform, material ID, vertex buffer offsets).
   - Fetches the 3 triangle vertex positions, normals, and UVs from the scene vertex buffer.
   - Computes perspective-correct barycentric coordinates $(\lambda_0, \lambda_1, \lambda_2)$ and analytic screen-space derivatives ($\frac{\partial uv}{\partial x}, \frac{\partial uv}{\partial y}$).
   - Samples textures and evaluates material parameters (Albedo, Normal Map, Roughness, Metallic) **exactly once per visible pixel**.
4. Outputs surface attributes directly into the **ReSTIR DI & GI Shading Stages**.

---

## 2. ReSTIR (Reservoir-based Spatiotemporal Importance Resampling)

ReSTIR enables real-time evaluation of millions of complex dynamic lights (Direct Illumination) and multi-bounce indirect paths (Global Illumination) at 60+ FPS.

### 2.1 The Reservoir Structure
```cpp
struct ReSTIRReservoir {
    uint32_t selectedSample; // Candidate light index or indirect ray path hash
    float weightSum;         // Sum of weights (w_sum) of candidate samples seen so far
    uint32_t sampleCount;    // Total candidate samples considered (M)
    float W;                 // Unbiased final weight multiplier: W = (1 / TargetPDF) * (weightSum / M)
    
    // Payload for GI indirect bounce
    Math::Vector3 sampleHitPos;
    float hitDistance;
    Math::Vector3 sampleNormal;
    float sampleRadiance;
};
```

### 2.2 ReSTIR DI (Direct Illumination) Execution Sequence
1. **Initial Candidate Generation**: Selects $M_0$ (e.g. 32) light candidates randomly from the active scene lights.
   - Target PDF: $\hat{p}(x) = \frac{L_e \cdot \max(0, \mathbf{n} \cdot \mathbf{l})}{\|\mathbf{p}_{light} - \mathbf{p}_{surf}\|^2}$.
   - Proposal PDF: $q(x) = \frac{1}{N_{lights}}$.
   - Weight: $w_i = \frac{\hat{p}(x_i)}{q(x_i)}$.
2. **Temporal Reuse**:
   - Reprojects current pixel to previous frame using motion vectors: $\mathbf{x}_{prev} = \mathbf{x} - \mathbf{v}_{motion} \cdot \text{ScreenDim}$.
   - Fetches temporal reservoir $R_{prev}$ (clamping history $M \le 30$ to prevent ghosting on moving objects).
   - Merges $R_{prev}$ into current reservoir using statistical combination.
3. **Spatial Cross-Bilateral Reuse**:
   - Samples $K$ (e.g. 4-8) spatial neighboring pixels within radius $R_{spatial} \approx 16\text{px}$.
   - Rejects neighbors failing cross-bilateral geometric threshold:
     $$\mathbf{n}_{center} \cdot \mathbf{n}_{neighbor} \ge \cos(25^\circ), \quad |\Delta z| \le 0.1 \cdot z_{center}$$
   - Merges valid neighbor reservoirs into a combined candidate reservoir.
4. **Visibility Verification**:
   - **Tier 1 (GTX 1060)**: Casts a software shadow ray or SDF shadow cone.
   - **Tier 2 (RTX)**: Dispatches a single hardware DXR Ray Query against the scene BVH.
   - Evaluates direct radiance using the single winner light sample multiplied by unbiased weight $W$.

### 2.3 ReSTIR GI (Indirect Global Illumination)
- Traces 1 initial indirect bounce ray per pixel.
- Reuses indirect bounce radiosity across spatial and temporal neighbors.
- **Jacobian Shift Correction**: Corrects for geometric screen-space disparity between neighboring hit points:
  $$J = \frac{\cos\theta_{new} \cdot r_{old}^2}{\cos\theta_{old} \cdot r_{new}^2}$$
  $$w_{resample} = \hat{p}_{new} \cdot J \cdot W_{prev} \cdot M_{prev}$$

---

## 3. Render Graph DAG & Pass Scheduling

```
+-----------------------------------------------------------------------------------------------+
|                                    SLATE RENDER GRAPH DAG                                     |
+-----------------------------------------------------------------------------------------------+
|                                                                                               |
|  [Pass 1: Meshlet Culling & Hi-Z Occlusion Pass]                                              |
|        │  - Culls non-visible clusters against previous frame's Hi-Z depth pyramid            |
|        ▼                                                                                      |
|  [Pass 2: Visibility Buffer & Software Rasterizer]                                            |
|        │  - Outputs 32-bit compact VisBuffer [InstanceID | TriangleID] + Depth                |
|        ▼                                                                                      |
|  [Pass 3: 3D Clustered Light Assignment (Async Compute)]                                      |
|        │  - Slices view frustum into 16x16x32 3D cluster grid and bins active lights          |
|        ▼                                                                                      |
|  [Pass 4: Material Evaluation Pass]                                                           |
|        │  - Fetches vertex attributes, computes barycentrics, samples textures once per pixel |
|        ▼                                                                                      |
|  [Pass 5: ReSTIR DI - Candidate Sampling & Spatiotemporal Reuse]                              |
|        │  - Tier 1 (GTX): Screen-Space ReSTIR + Compute Raymarching                           |
|        │  - Tier 2 (RTX): Hardware DXR Ray Queries                                            |
|        ▼                                                                                      |
|  [Pass 6: ReSTIR GI - Indirect Radiosity Resampling]                                         |
|        │  - Spatiotemporal reservoir filtering with Jacobian geometric shift                 |
|        ▼                                                                                      |
|  [Pass 7: Volumetric Fluid/Gas Raymarching]                                                   |
|        │  - Henyey-Greenstein phase function integration through 3D Navier-Stokes grid        |
|        ▼                                                                                      |
|  [Pass 8: GPU Particle Rendering]                                                             |
|        │  - Alpha-blended or Weighted Blended Order-Independent Transparency (WBOIT)         |
|        ▼                                                                                      |
|  [Pass 9: Post-Processing, Tonemapping & Upscaling]                                           |
|        │  - ACES Tonemapping, Bloom, Motion Blur, AMD FSR 2.2 / NVIDIA DLSS 3.5               |
|        ▼                                                                                      |
|  [Pass 10: Decoupled Editor Viewport Blit]                                                    |
|                                                                                               |
+-----------------------------------------------------------------------------------------------+
```
