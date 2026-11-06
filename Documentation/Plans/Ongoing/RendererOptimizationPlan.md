# Lossless Renderer Optimization Plan — C++ ReSTIR GI & Vulkan 1.1 Volumetric/SDF Pipeline

**Date:** 2026-11-06  
**Scope:**
1. **Part I — C++ / Vulkan / Slang ReSTIR GI Renderer** (`Frontier/Engine/Rendering/GlobalIllumination/`, `Frontier/Engine/Shaders/GlobalIllumination/`, `Frontier/Engine/DeviceExchange/`, and the shared 1:1 WebGPU shaders in `Experimental/VehicleHmiPrototype/Shader/`).
2. **Part II — Vulkan 1.1 Pyro Volumetric & 3D SDF World Renderer** (`Frontier/Engine/Rendering/`, `Frontier/Engine/VolumetricDynamics/`, `Tools/Build/GenerateVulkanShaders.py`).

**Strict Quality Invariant (Zero Quality Reduction):**  
Every optimization in this plan preserves the exact mathematical estimators, ray-triangle/ray-SDF intersections, Monte Carlo unbiased reservoir weights ($W = \Sigma w / (Z \cdot \hat{p})$), bounce counts, candidate counts, resolution scales, filter kernels, and FP16/FP32 precision. None of these items trade visual fidelity, sharpness, shadow accuracy, or convergence rate for speed.

---

## Summary Matrix

| ID | Subsystem | Optimization | Primary Bottleneck Bound | Expected Stage Win | Quality Impact |
|---|---|---|---|---|---|
| **GI-1** | ReSTIR GI / BVH | Eliminate duplicate popped-node ray–AABB slab tests in `TraceSceneRay` & `TraceSceneOcclusion` | Ray traversal ALU + L1/L2 BW | **35%–45% faster BVH traversal** | Bit-identical hits |
| **GI-2** | ReSTIR GI / BVH | Move private `uint[48]` per-thread BVH stack to `groupshared uint[64][24]` LDS | VGPR scratch spills to VRAM | **1.4×–1.9× higher wave occupancy** | Bit-identical hits |
| **GI-3** | ReSTIR GI / Vulkan | Replace per-dispatch `ALL_COMMANDS_BIT` barriers, `GENERAL` layouts & sync fence waits with fine-grained barriers, DCC/Hi-Z layouts & 2 frames-in-flight | GPU idle drains + memory BW | **25%–40% faster full refresh** | Bit-identical |
| **GI-4** | ReSTIR GI / RT | Optional `VK_KHR_ray_query` (`RayQuery`) backend behind `TraceSceneRay` / `TraceSceneOcclusion` | Software BVH compute | **4×–10× faster ray tracing on RT GPUs** | Identical geometry & shading |
| **GI-5** | ReSTIR Spatial | Eliminate `224 B` spilled `DonorPositions/Normals/Tallies[8]` arrays & triplicate `ResolveReservoirTarget` `rsqrt` calls | VGPR spilling + SFU ops | **25%–35% faster `ResampleSpatial`** | Bit-identical `1/Z` & irradiance |
| **GI-6** | SVGF Wavelet | `groupshared` tile preload, 6-mul `x^64` (vs `pow`), pre-decoded normals & hoisted reciprocals across 7 à-trous passes | Texture fetches + SFU `pow`/`div` | **2×–3× faster `ConvolveWavelet`** | Exact IEEE-754 equivalence |
| **GI-7** | SVGF / Display | Fuse `VarianceUpsample.ProjectScreenSignal` (Diffuse + Glossy) inline into `DisplayRecombination` | Full-screen RGBA16F DRAM BW | **Removes 2 dispatches + 4 full-screen RW passes** | Bit-identical |
| **GI-8** | ReSTIR / SDF | Restore HZB nearest-depth pyramid `Resume` skip in Slang `ReservoirTemporal` & `ReflectionTrace` | Near-camera empty SDF steps | **20%–35% fewer SDF steps** | Unbiased parity with WebGPU |
| **GI-9** | Emissive NEE | Precompute $O(1)$ Vose alias table + triangle cluster IDs for `SampleEmissiveLight` & `IsListedEmitter` | $O(N)$ linear buffer scans per bounce | **3×–8× faster emissive NEE** | Identical discrete PDF |
| **GI-10** | Cabin HMI NEE | Vehicle bounding-sphere early-out + CPU-precomputed world HMI screen planes in `EvaluateHmiAreaLightNee` | `Occupants[0]` SSBO + 4×4 matmul on 99% exterior | **Eliminates exterior HMI NEE overhead** | Bit-identical |
| **GI-11** | RT Reflections | Subgroup ballot / active-tile compaction for `TraverseMirrorReflection` & smooth-cutoff `TraverseReflection` | Warp divergence (80%+ idle lanes) | **2×–4× faster `MirrorTrace`** | Bit-identical |
| **GI-12** | GI C++ Host | Zero-allocation frame encoders, dirty-checked `RefreshTlas`, & conditional shadow-cascade skip in pure RTX mode | CPU heap churn + redundant uploads/rasters | **Eliminates per-frame CPU allocations** | Bit-identical |
| **VOL-1** | Pyro Raymarch | Native `VK_IMAGE_TYPE_3D` + `REPEAT` sampler instead of 8-tap software modulo trilinear | 8× `texelFetch` + modulo ALU | **~3×–5× faster World-Grid sampling** | Identical FP16 trilinear |
| **VOL-2** | Pyro Raymarch | `4×4×4` max-occupancy mip empty-space skipping in `PyroRaymarchFrag` | Stepping through 85%–95% empty air | **~3×–5× faster volumetric raymarch** | Zero pixel difference |
| **VOL-3** | World SDF | Sky early-exit (`z > 4.35 && rd.z > 0`), obstacle AABB gate, & analytic flat-ground normal | 110-step sky rays & distant primitive evals | **~60%–70% faster World SDF pass** | Identical SDF hits |
| **VOL-4** | Vulkan Context | Collapse 3–4 synchronous `SubmitAndWaitFrameCommands` fence waits into 1 pipelined frame submission | CPU–GPU serialization stalls | **1.4×–1.8× higher interactive FPS** | Bit-identical |
| **VOL-5** | World SDF | Move `BuildDomainSdfAtlas` (`64³` CPU loop + scalar FP16 conversion + PCIe upload) onto GPU | 262k scalar CPU evals + fence flush | **100% CPU & PCIe upload elimination** | Identical FP16 SDF |
| **VOL-6** | Render Targets | Narrow `PresImg`/`DivImg` to `RG16F` and `SceneDepthImage` to `R32F`/`R16F` | Unused `.ba` / `.gba` DRAM traffic | **50%–75% less pressure/depth BW** | Identical precision |
| **VOL-7** | Pyro Solver | Split `ReadVelIdx` / `ReadThermoIdx` to remove per-step `vkCmdCopyImage(ThermoImg[2] -> [Nxt])` | Redundant full-atlas copy + 2 barriers | **Removes 1 copy + 2 barriers/step** | Bit-identical |
| **VOL-8** | Vulkan Context | State-change guard on `UpdateDescriptorSets`/`CmdBindPipeline` + push constants for vehicle parts | 100+ redundant descriptor rewrites/frame | **Lower CPU driver overhead** | Bit-identical |
| **VOL-9** | Meshes & Math | Enable backface culling on `PipeVehicleOpaque`, `LOAD_OP_DONT_CARE`, and hoist `log`/`pow` to UBO | Back-face overdraw + per-voxel SFU math | **~40% fewer vehicle fragments** | Bit-identical |

---

## Part I — C++ / Slang ReSTIR GI Renderer Optimizations

### GI-1: Eliminate Duplicate Popped-Node Ray–AABB Slab Tests in `TraceSceneRay` & `TraceSceneOcclusion`
- **Files:**
  - `Frontier/Engine/Shaders/GlobalIllumination/RayIntersection.slang` (`lines 115–227`, `344–450`)
  - `Experimental/VehicleHmiPrototype/Shader/RayTraversal.wgsl` (`lines 112–233`, `336–453`)
- **Problem:**
  When visiting an internal TLAS or BLAS node (`Entry.Count == 0`), the loop **already loads both children (`Left` and `Right`) and computes `LDist = IntersectEntryBounds(...)` and `RDist = IntersectEntryBounds(...)`**, pushing a child onto `Stack` *only* when `Dist < Nearest` (or `Dist < MaximumTravel` in `TraceSceneOcclusion`). However, on the very next iteration when that child is popped off `Stack`, lines `117`, `160`, `346`, and `393` **re-fetch `Entry.Minimum` / `Entry.Maximum` and execute the full 6-plane `IntersectEntryBounds` slab test a second time on the exact same node**:
  - In `TraceSceneOcclusion` (used by Direct Sun NEE, ReSTIR Temporal visibility, ReSTIR Spatial visibility, Multi-Bounce NEE, Clustered IES shadow rays, and HMI area-light shadow rays), `MaximumTravel` is **constant** for the entire ray. Every popped internal and leaf node test (except root `0` and TLAS->BLAS root transitions) is **100% redundant duplicate work**.
  - In `TraceSceneRay`, `Nearest` only shrinks when a leaf triangle hits; storing the already-computed scalar entry distance `StackTMin[Depth] = LDist` alongside `Stack[Depth]` replaces a 24-byte global memory load + 18-op slab test with a single register/LDS comparison (`if (PoppedTMin >= Nearest) continue;`).
- **Lossless Optimization:**
  1. Test root `BvhEntries[0]` once before entering the `while` loop, and test the BLAS root `BvhEntries[Entry.LeftOrFirst]` when transitioning at a TLAS leaf.
  2. In `TraceSceneOcclusion`, remove the popped-node `IntersectEntryBounds` calls completely.
  3. In `TraceSceneRay`, push `(ChildIndex, ChildEntryDist)` and prune popped entries via `if (ChildEntryDist >= Nearest) continue;`.

---

### GI-2: Move Private `uint[48]` Per-Thread BVH Stack into `groupshared` Memory & Bound Depth to `24`
- **Files:**
  - `Frontier/Engine/Shaders/GlobalIllumination/RayIntersection.slang` (`lines 37, 94, 326`)
  - `Experimental/VehicleHmiPrototype/Shader/RayTraversal.wgsl` (`lines 33, 91, 319`)
- **Problem:**
  `BvhStackCapacity` is `48u` (`192 bytes` per stack), and `var Stack : uint[48]` is dynamically indexed (`Stack[Depth]`). Because `TraceMultiBouncePath` and `TraceMultiBounceReflection` inline both `TraceSceneRay` and `TraceSceneOcclusion`, SPIR-V compilers on AMD RDNA and NVIDIA lower `Stack` to **per-thread local/scratch memory in VRAM**, throttling L1 cache bandwidth and slashing compute subgroup occupancy. Furthermore, a binary SAH BVH with near-first child ordering over `< 256` TLAS instances and `< 128k` BLAS triangles (`BlasLeafCapacity = 4`) pushes at most 1 sibling per tree level (`max depth <= 24`).
- **Lossless Optimization:**
  1. Allocate `groupshared uint SharedBvhStack[64][24]` (`6 KB` per `8×8` workgroup — well under Vulkan's guaranteed `16 KB–32 KB` `maxComputeSharedMemorySize`), indexed by `[GroupThreadIndex][Depth]`.
  2. Eliminate all per-thread VRAM scratch spills and raise active waves per CU/SM with zero change to traversal order or intersections.

---

### GI-3: Replace `DeviceExchange` Per-Dispatch Global Barriers, `VK_IMAGE_LAYOUT_GENERAL`, and Synchronous Fence Waits
- **Files:**
  - `Frontier/Engine/DeviceExchange/DeviceExchange.cpp` (`lines 1511–1545`, `1607`, `1625–1635`)
  - `Frontier/Engine/Rendering/GlobalIllumination/IlluminationHost.cpp` (`lines 451–548`)
- **Problem:**
  To mimic WebGPU's implicit ordering during the initial 1:1 port, `DeviceExchange` enforces three severe hardware serializations on every refresh:
  1. **Global `ALL_COMMANDS_BIT` Barrier Before Every Single Dispatch (`DeviceExchange.cpp:1628`):** `CommandSequence::Dispatch()` unconditionally calls `OrderAfterEarlierWork()`, draining 100% of GPU shader cores between ~25 consecutive dispatches — even when consecutive passes read the exact same G-Buffer/BVH/SDF inputs and write to completely disjoint outputs:
     - `DirectRadiance`, `ResampleTemporal`, and `TraverseReflection` have **zero data dependency** on one another.
     - `Diffuse` (`VarianceIntegrator` 4× à-trous wavelets) and `Glossy` (`VarianceIntegrator` 3× à-trous wavelets) are **100% independent** of one another.
  2. **All Textures Kept Permanently in `VK_IMAGE_LAYOUT_GENERAL`:** Keeping depth, G-Buffer, shadow cascades, ReSTIR guide/irradiance/reflection, and SVGF wavelet textures in `GENERAL` disables **lossless GPU Delta Color Compression (DCC / FB Compression) and Hi-Z depth compression**, inflating VRAM bandwidth by ~20%–35%.
  3. **Multiple Synchronous `vkWaitForFences(..., UINT64_MAX)` Per Frame (`DeviceExchange.cpp:1511, 1530`):** `FlushWrites()` submits staging copies and stalls CPU-to-GPU idle on a fence; `Submit()` stalls again; and when `RefreshInterlude` (`EnvironmentOverlay`) is active (`IlluminationHost.cpp:526–545`), `SubmitSeries()` runs twice more — causing **3 to 5 full GPU pipeline flushes per single frame**.
- **Lossless Optimization:**
  1. Replace unconditional `OrderAfterEarlierWork()` in `CommandSequence::Dispatch` with resource-state tracking (`BarrierDerivation`), batching independent compute passes (`DirectRadiance || ResampleTemporal || TraverseReflection`, and `Diffuse.Wavelet[i] || Glossy.Wavelet[i]`) between shared `COMPUTE_SHADER_BIT` barriers so tail wavefronts overlap.
  2. Transition read-only sampled targets to `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL` (`DEPTH_STENCIL_READ_ONLY_OPTIMAL` for `SurfaceDepth` / `ShadowDepth`) and raster targets to `COLOR_ATTACHMENT_OPTIMAL` / `DEPTH_STENCIL_ATTACHMENT_OPTIMAL` to enable hardware lossless DCC and Hi-Z.
  3. Record staging `vkCmdCopyBuffer` commands at the head of the frame's command buffer (`TRANSFER_BIT -> SHADER_READ_BIT` barrier instead of a separate `FlushWrites` fence wait) and double-buffer transient host regions (`2` frames-in-flight).

---

### GI-4: Optional Hardware `VK_KHR_ray_query` (`RayQuery`) Backend Behind `TraceSceneRay` & `TraceSceneOcclusion`
- **Files:**
  - `Frontier/Engine/Shaders/GlobalIllumination/RayIntersection.slang` (`TraceSceneRay`, `TraceSceneOcclusion`)
  - `Frontier/Engine/Rendering/GlobalIllumination/RayTracingSpace.cpp`
- **Problem:**
  All ReSTIR GI multi-bounce paths, multi-bounce reflections, direct sun shadows, clustered light shadows, and spatial/temporal visibility rays execute software Möller–Trumbore BVH traversal in compute shaders even on GPUs with dedicated hardware RT cores (NVIDIA RTX, AMD RDNA2/3/4, Intel Arc, Apple M3/M4).
- **Lossless Optimization:**
  Because 100% of ray queries in the GI architecture funnel through just two functions (`TraceSceneRay` and `TraceSceneOcclusion`), build parallel Vulkan `VkAccelerationStructureKHR` BLAS/TLAS handles in `RayTracingSpace` when `VK_KHR_acceleration_structure` + `VK_KHR_ray_query` are supported, and compile a Slang specialization variant using `RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH>` for `TraceSceneOcclusion` and `RayQuery<RAY_FLAG_NONE>` for `TraceSceneRay` (preserving exact `BvhTriangles` smooth octahedral normal interpolation and `VehicleGlassFamily` / `ParticipationShredded` non-opaque candidate skip). Keep the existing software BVH path as the automatic fallback for Vulkan 1.1 / MoltenVK / software adapters.

---

### GI-5: Eliminate `224 B` Spilled Donor Arrays & Redundant `rsqrt` Calls in `ReservoirSpatial`
- **Files:**
  - `Frontier/Engine/Shaders/GlobalIllumination/ReservoirSpatial.slang` (`lines 80–174`)
  - `Experimental/VehicleHmiPrototype/Shader/ReservoirSpatial.wgsl` (`lines 78–167`)
- **Problem:**
  1. Every thread allocates three dynamically indexed arrays: `DonorPositions : float3[8]`, `DonorNormals : float3[8]`, and `DonorTallies : float[8]` (`224 bytes` = **56 32-bit registers per thread**), solely to evaluate the `1/Z` pairwise horizon loop:
     ```hlsl
     if (ResolveReservoirTarget(Chosen, DonorPositions[Donor], DonorNormals[Donor]) > 0.0)
         Z += DonorTallies[Donor];
     ```
  2. Inside that loop, `ResolveReservoirTarget` executes `normalize(Chosen.Position - DonorPos)` (`rsqrt`) and `ResolveLuminance(Chosen.Radiance)` up to **8 times**, even though `ResolveLuminance(Chosen.Radiance) > 0.0` is invariant across donors and `dot(Normal, normalize(v)) > 0.0` has the **exact same boolean value** as the unnormalized dot product `dot(Normal, v) > 0.0`!
  3. Immediately after the loop (`lines 162–173`), `ResolveReservoirTarget(Chosen, Position, Normal)` and `ResolveSampleDirection(Chosen, Position)` are called **three times back-to-back** on the exact same `(Chosen, Position, Normal)` tuple (`3× normalize()` + `2× ResolveLuminance()`).
  4. Inside the neighbor loop (`lines 105–114`), `UnpackReservoir(CurrentReservoirs[...])` (`32 B` SSBO load) executes **before** the coplanarity rejection (`abs(dot(DonorPosition - Position, Normal))`), and `ResolveTracePosition(Donor)` fetches full-resolution `SurfaceDepth` even though `DonorGuide.z` is already in registers. (Similarly, `ReservoirTemporal.slang:145–155` computes `Target / SourcePdf = (Lum * Cosine) / (Cosine * InversePi)` instead of `Lum * Pi`.)
- **Lossless Optimization:**
  1. Store accepted donors as packed `(uint16x2 DonorPixel, float Tally)` (`64 bytes` total, or replay the deterministic `Seed` over an 8-bit `AcceptedMask` using `0` array storage!) instead of `224 bytes` of `float3` arrays.
  2. Move the `CurrentReservoirs` 32-byte SSBO load **after** the normal + coplanarity rejection, and reconstruct `DonorPosition` directly from `DonorGuide.z`.
  3. Replace `ResolveReservoirTarget(Chosen, DonorPos, DonorNorm) > 0.0` in the `Z` loop with `(ChosenLum > 0.0) && dot(DonorNorm, Chosen.Position - DonorPos) > 0.0` (saving up to **8 `rsqrt`s + 8 luminance dots per pixel**).
  4. Compute `SampleDir`, `Cosine`, and `Target = ChosenLum * Cosine` **once** for the receiver pixel (saving **2 `rsqrt`s + 2 luminance dots per pixel**), and simplify cosine-hemisphere `Target / SourcePdf` in `ReservoirTemporal.slang` to `Lum * Pi`.

---

### GI-6: `groupshared` Tile Preload, 6-Mul `x^64`, & Hoisted Reciprocals in `VarianceWavelet` (SVGF)
- **Files:**
  - `Frontier/Engine/Shaders/GlobalIllumination/VarianceWavelet.slang` (`lines 35–88`)
  - `Frontier/Engine/Rendering/GlobalIllumination/ResamplingSequence.cpp` (`lines 240–254`)
- **Problem:**
  `ConvolveWavelet` runs **7 full-trace-screen dispatches every refresh** (4 Diffuse iterations + 3 Glossy iterations), each executing a `5×5 = 25`-tap nested loop per pixel (`175` taps/pixel/refresh):
  1. Line `70`: `pow(max(dot(DecodeOctahedral(TapGuide.xy), Normal), 0.0), 64.0)` executes **`DecodeOctahedral` 175 times/pixel** (on `CurrentGuide`, which never changes across all 7 passes) and **`pow(x, 64.0)` (`exp2(64 * log2(x))` SFU transcendental) 175 times/pixel**.
  2. Lines `69` & `71`: Two floating-point divisions (`/ (0.015 * float(Step) * Guide.z + 1.0e-3)` and `/ (Variance.ColourSigma * Deviation + 1.0e-3)`) sit *inside* the `5×5` loop despite their denominators depending **only on the centre pixel** (`350` redundant FP divisions/pixel/refresh).
  3. Each `8×8` workgroup performs `64 × 25 × 2 = 3,200` global `LoadTexel` fetches of heavily overlapping neighbors.
- **Lossless Optimization:**
  1. Hoist `InvDepthDenom = -1.0 / (0.015 * float(Step) * Guide.z + 1.0e-3)` and `InvLumDenom = -1.0 / (Variance.ColourSigma * Deviation + 1.0e-3)` outside the `5×5` loop so the inner loop uses FMA/multiply.
  2. Replace `pow(nDot, 64.0)` with 6 exact squarings (`float w = max(dot(...), 0.0); w *= w; w *= w; w *= w; w *= w; w *= w; w *= w;`) — exact power of $2^6$, running on full-rate FP32 ALU pipes instead of quarter-rate SFU pipes.
  3. For `Step == 1` and `Step == 2`, cooperatively preload a `12×12` / `16×16` tile of `(SourceSignal.rgba, Luminance, DecodedNormal.xyz, Depth)` into `groupshared` memory once per workgroup, cutting global texture fetches and octahedral decodes by **> 6×**.

---

### GI-7: Fuse `VarianceUpsample.ProjectScreenSignal` Inline Into `DisplayRecombination`
- **Files:**
  - `Frontier/Engine/Rendering/GlobalIllumination/ResamplingSequence.cpp` (`lines 249–254`)
  - `Frontier/Engine/Shaders/GlobalIllumination/VarianceUpsample.slang`
  - `Frontier/Engine/Shaders/GlobalIllumination/DisplayRecombination.slang`
- **Problem:**
  When `TraceScale > 1`, both `Diffuse` and `Glossy` `VarianceIntegrator` instances dispatch `VarianceUpsample.ProjectScreenSignal` at full `ScreenExtent` to write two full-resolution `Rgba16Float` textures (`Diffuse->ScreenTexture` and `Glossy->ScreenTexture` — `16 MB` each at 1440p, `33 MB` at 4K), which are read **once** at the exact same integer `ScreenPixel` by `DisplayRecombination` and never sampled elsewhere.
- **Lossless Optimization:**
  Evaluate the `2×2` guide-weighted bilateral trace-to-screen upsample directly inside `DisplayRecombination` (which already loads `SurfaceDepth` and `SurfaceAttributes` at `ScreenPixel`!), eliminating **2 full-screen compute dispatches**, **2 full-screen `RGBA16F` allocations**, and **4 full-screen `RGBA16F` DRAM read/write passes** per frame with bit-identical output.

---

### GI-8: Restore Screen-Space HZB `Resume` Hand-Off in C++ Slang `ReservoirTemporal` & `ReflectionTrace`
- **Files:**
  - `Frontier/Engine/Shaders/GlobalIllumination/ReservoirTemporal.slang` (`lines 115–138`)
  - `Frontier/Engine/Shaders/GlobalIllumination/ReflectionTrace.slang` (`lines 69–78`)
  - Reference: `Experimental/VehicleHmiPrototype/Shader/ReservoirTemporal.wgsl` (`lines 112–139`)
- **Problem:**
  In the WebGPU shaders, rays first walk the hierarchical Nearest-Depth Pyramid (`TraverseScreenDepth`), settling screen-visible contact GI/reflections cheaply and advancing the SDF start distance to `Resume = max(0.5 * Bias, Strike.Travel - Bias)` (the last point proven unoccluded by the depth pyramid). In the C++ Slang port, `TraverseSceneDistance` starts from `0.5 * Bias` on every pixel, marching fine Cascade-0 SDF steps through empty near-camera space already known to be free space.
- **Lossless Optimization:**
  Port the `NearestDepthSpace` HZB free-space interval skip (`Resume = max(0.5 * Bias, Strike.Travel - Bias)`) to `ReservoirTemporal.slang` and `ReflectionTrace.slang` so SDF rays start directly at the end of the unoccluded depth-pyramid segment without missing any scene geometry.

---

### GI-9: Precompute $O(1)$ Vose Alias Table & Triangle Cluster Tags for Emissive Triangle NEE
- **Files:**
  - `Experimental/VehicleHmiPrototype/Shader/RayTraversal.wgsl` (`lines 618–738`)
  - `Experimental/VehicleHmiPrototype/Source/EmissiveLightSpace.js` (and corresponding C++ `LuminaireRegister` / `RayIntersection.slang` emissive table)
- **Problem:**
  1. `SampleEmissiveLight` (`RayTraversal.wgsl:697–722`) runs up to `EmissiveSamplesPerVertex = 2` times at **every bounce vertex** (`6×` per candidate path) and performs **two $O(N)$ linear scans** over all `Cluster.Meta.z` triangles in the selected emissive cluster — fetching `EmissiveTriangles` + `RayMaterials` to sum `TriangleTotal` and then scanning again to invert the CDF.
  2. `IsListedEmitter` (`lines 618–642`) performs an **$O(8 \times N)$ nested linear scan** over `EmissiveTriangles` on every BSDF hit against an emissive occupant just to check whether `Hit.Triangle` belongs to a listed cluster.
- **Lossless Optimization:**
  1. Precompute `ClusterTotalWeight` and a **Vose Alias Table** (`(ProbThreshold, AliasTriangleIdx)` — 8 bytes per emissive triangle) on the CPU when building `EmissiveClusters`. `SampleEmissiveLight` then draws from the **exact same discrete triangle PDF in $O(1)$ (1 table fetch, zero loops)**.
  2. Store each triangle's `EmissiveClusterId` (`u16`, `0xFFFF` if non-emissive) in unused triangle metadata bits so `IsListedEmitter` checks only the `<= 8` cell slots (`Words`) in **$O(1)$ without touching `EmissiveTriangles`**.

---

### GI-10: Vehicle Bounding-Sphere Early-Out & Frame-Precomputed World HMI Planes in `EvaluateHmiAreaLightNee`
- **Files:**
  - `Frontier/Engine/Shaders/GlobalIllumination/RayIntersection.slang` (`lines 666–770`)
  - `Frontier/Engine/Shaders/GlobalIllumination/DirectRadiance.slang` (`line 163`)
- **Problem:**
  `EvaluateHmiAreaLightNee` is called on **every primary pixel in `DirectRadiance`** and **every bounce vertex in `TraceMultiBouncePath` & `TraceMultiBounceReflection` across the entire 220 m world**. Every exterior road/arch/building vertex fetches `Occupants[0].WorldToLocal` from the SSBO and executes a 4×4 matrix-vector multiply just to fail the cabin AABB check (`line 678`). Inside the cabin, every shading point recomputes `TabWorldPt`, `TabWorldNorm`, `CluWorldPt`, and `CluWorldNorm` via 4 matrix multiplies + 2 `normalize()` calls even though the tablet and instrument cluster are rigid in `Occupants[0]` for the entire refresh.
- **Lossless Optimization:**
  1. Store `VehicleCabinSphere` (`xyz = world centre`, `w = R² = 4.0`) and the world-space HMI tablet/cluster centres, axes, and normals in `RefreshConstants` once per refresh on the CPU.
  2. Gate `EvaluateHmiAreaLightNee` with a 3-instruction uniform check (`dot(d, d) > Refresh.VehicleCabinSphere.w`) — skipping 100% of `Occupants[0]` SSBO reads and matrix math for the ~99% of vertices outside the vehicle cabin, and eliminating 4 matrix transforms per interior vertex.

---

### GI-11: Subgroup / Tile Worklist Compaction for `TraverseMirrorReflection` & Smooth-Cutoff `TraverseReflection`
- **Files:**
  - `Frontier/Engine/Shaders/GlobalIllumination/ReflectionTrace.slang` (`lines 30–136`)
  - `Frontier/Engine/Rendering/GlobalIllumination/ResamplingSequence.cpp` (`lines 116–134`)
- **Problem:**
  `TraverseMirrorReflection` (`Roughness < 0.20`) and `TraverseReflection` (`Roughness <= Cutoff`) launch full-screen `8×8` compute grids. Along vehicle clearcoat edges, windows, chrome trim, or mirror panes (often 10%–20% of screen pixels; cf. `RayTracedLighting.md §3`), threads hitting rough asphalt/sky return immediately while 4–8 threads in the same subgroup execute 2-bounce BVH ray tracing + NEE — stalling the entire 32/64-wide GPU wavefront at < 15% SIMT utilization.
- **Lossless Optimization:**
  During `ProjectResamplingGuide` / G-Buffer classification, emit compact coordinates (`uint16x2`) of active smooth pixels into a GPU worklist buffer (via subgroup `WaveActiveBallot` + 1 atomic `InterlockedAdd` per subgroup) and dispatch `TraverseMirrorReflection` via `vkCmdDispatchIndirect` (`1D` packed waves of 64 guaranteed-active smooth pixels). Output texels are bit-identical while eliminating 80%+ idle SIMT lanes.

---

### GI-12: Zero-Allocation CPU Frame Encoders, Dirty-Checked `RefreshTlas`, & Conditional Shadow-Cascade Skip
- **Files:**
  - `Frontier/Engine/Rendering/GlobalIllumination/RayTracingSpace.cpp` (`lines 335–510`)
  - `Frontier/Engine/Rendering/GlobalIllumination/SceneDistanceSpace.cpp` (`lines 95–195`)
  - `Frontier/Engine/Rendering/GlobalIllumination/IlluminationHost.cpp` (`lines 395–492`)
- **Problem:**
  1. `RayTracingSpace::RefreshTlas` heap-allocates **11 `std::vector`s** and a heap-captured `std::function` on **every refresh**, and rebuilds + uploads the entire TLAS even when zero occupants moved.
  2. `SceneDistanceSpace::ScheduleInvalidation` heap-allocates `std::vector<uint8_t> Flags(Edge³)` 4 times per refresh, while `EncodeDistanceConstants`, `EncodeOccupantProfiles`, `RefreshEncoding::Encode`, and `LuminaireRegister::Update` allocate and free temporary `std::vector<uint8_t>` buffers every refresh.
  3. `Shadow->RecordCascades` rasterizes all 3 shadow cascades every refresh even when `RayTracing && RayTracedShadows` are enabled and volumetric fog shafts (`EnvironmentOverlay`) are disabled (`IlluminationHost.cpp:490`).
- **Lossless Optimization:**
  1. Retain scratch vectors on `RayTracingSpace` / `SceneDistanceSpace`, replace `std::function` recursion with a template/self-passing lambda or explicit 16-entry stack, and skip `RefreshTlas` + `UploadTlas` when occupant transform generation counters are clean.
  2. Encode refresh/luminaire/distance constants directly into mapped staging slices (`DeviceExchange::AllocateStagingSpan`).
  3. Skip `Shadow->RecordCascades` when `RayTracing && RayTracedShadows` are active and no volumetric shadow-map consumer (`EnvironmentOverlay` fog shafts) is enabled.

---

## Part II — Vulkan 1.1 Pyro Volumetric & 3D SDF World Renderer Optimizations

### VOL-1: Native `VK_IMAGE_TYPE_3D` + `VK_SAMPLER_ADDRESS_MODE_REPEAT` Instead of 8-Tap Software Modulo Trilinear
- **Files:** `Tools/Build/GenerateVulkanShaders.py` (`lines 130–148`), `Frontier/Engine/VolumetricDynamics/PyroVolumeSolver.cpp` (`lines 365–395`)
- **Problem:** In `isWorldGrid()` mode, `sample3DLinear()` executes 8 manual `fetch3D()` calls with integer modulo wrapping (`((wc % N) + N) % N`), 2D tile coordinate division/modulo, and 8 `texelFetch` instructions (`800–1,000+` fetches per pixel in `PyroRaymarchFrag`).
- **Lossless Optimization:** Allocate `Vel`, `Thermo`, `Pres`, `Curl`, and `Div` as native Vulkan 1.1 `VK_IMAGE_TYPE_3D` (`N×N×N`, `R16G16B16A16_SFLOAT`) sampled with `VK_SAMPLER_ADDRESS_MODE_REPEAT` on X/Z (`CLAMP_TO_EDGE` on Y) and UVW offset `worldGridOrigin / float(N)`. Collapses 8 integer-modulo `texelFetch` ops + 7 `mix` ops into **1 hardware trilinear `texture()` instruction**.

### VOL-2: `4×4×4` Max-Occupancy Mip Empty-Space Skipping in `PyroRaymarchFrag`
- **Files:** `Tools/Build/GenerateVulkanShaders.py` (`lines 678–754`)
- **Problem:** Primary rays march uniformly (`64–120` steps) across the `38.4m × 11.52m × 38.4m` World-Grid box even though 85%–95% of voxels contain empty air (`smoke <= 0.004 && temp <= 0.02`, where the shader loop already contributes zero radiance and `1.0` transmittance).
- **Lossless Optimization:** Generate a `16³` max-occupancy mip (`max(smoke, temp)` over each `4×4×4` voxel brick) once per simulation step and advance `t` directly to the brick exit face whenever the brick maximum falls below the existing `smoke <= 0.004 && temp <= 0.02` threshold (**mathematically zero pixel difference, ~3×–5× faster volumetric raymarching**).

### VOL-3: Sky Early-Exit, Obstacle AABB Gate, & Analytic Flat-Ground Normals in `WorldSdfRaymarchFrag`
- **Files:** `Tools/Build/GenerateVulkanShaders.py` (`lines 824–921`)
- **Problem:** Sky rays above the `4.31 m` portal arch (`p.z > 4.35 && rd.z > 0.0`) march all 110 steps out to `155 m`; `mapWorld()` evaluates all 6 obstacle SDF primitives even 80 m away; and flat asphalt (`hitMat == 1.0`) runs 6-tap finite-difference `calcNormal()`.
- **Lossless Optimization:** Early-exit sky rays once `p.z > 4.35 && rd.z > 0.0`, gate obstacle primitives in `mapWorld()` behind one bounding-box check (`dObsBox < res.x`), intersect Lobby mode (`!proving`) analytically in 1 step, and use exact `vec3(0, 0, 1)` normal on flat ground away from kerbs/ramp (**~60%–70% faster World SDF pass**).

### VOL-4: Collapse 3–4 Synchronous `SubmitAndWaitFrameCommands()` Fence Stalls Into 1 Pipelined Submission
- **Files:** `Frontier/Engine/Rendering/VulkanContext.cpp` (`lines 995, 1317, 1558`), `PyroVolumeSolver.cpp` (`line 1049`), `WorldSdfScene.cpp` (`line 542`)
- **Problem:** A single interactive frame submits and waits on `WorkFence` (`UINT64_MAX`) 3 to 4 separate times (SDF upload -> Pyro step -> World render -> Swapchain UI), draining the GPU idle between every stage.
- **Lossless Optimization:** Record simulation, world rendering + volumetric composite, and swapchain UI into a single command buffer separated by `TransitionImage` pipeline barriers, backed by `2` frames-in-flight (`Cmd[2]`, `Fence[2]`, `UniformArena[2]`).

### VOL-5: Move `BuildDomainSdfAtlas` Off the CPU onto GPU
- **Files:** `Frontier/Engine/VolumetricDynamics/FluidPlumeBridge.cpp` (`lines 360–366`), `WorldSdfScene.cpp` (`lines 217–259`), `VulkanContext.cpp` (`lines 956–996`)
- **Problem:** Every 2nd tick heap-allocates `4.2 MB`, evaluates `262,144` voxels on 1 CPU thread, scalar-converts `1,048,576` floats to FP16, copies `2.1 MB` via staging, and stalls on a fence.
- **Lossless Optimization:** Evaluate the identical analytic course + oriented car-hull SDF directly on the GPU into `WorldSdfImg` (or inline in `PyroSplatFrag`), eliminating 100% of CPU evaluation, heap allocation, FP16 conversion, PCIe staging copy, and fence wait.

### VOL-6: Narrow Render Target Formats Where Channels Are Unused (`RG16F` Pressure/Div & `R32F`/`R16F` Depth)
- **Files:** `Frontier/Engine/VolumetricDynamics/PyroVolumeSolver.cpp` (`lines 379–386`), `WorldSdfScene.cpp` (`line 378`)
- **Problem:** `PresImg[0..1]` and `DivImg` use `RGBA16F` (`64 bpp`) while `.ba` are always `0.0`; `SceneDepthImage` uses `RGBA16F` (`64 bpp`) while `.gba` are unused.
- **Lossless Optimization:** Use `VK_FORMAT_R16G16_SFLOAT` (`32 bpp`) for `PresImg`/`DivImg` (**50% bandwidth reduction across all 12–48 Jacobi sweeps**) and `VK_FORMAT_R32_SFLOAT` / `VK_FORMAT_R16_SFLOAT` for `SceneDepthImage` (**2×–4× less MRT depth write/read bandwidth**).

### VOL-7: Remove Per-Step Full-Atlas `vkCmdCopyImage(ThermoImg[2] -> ThermoImg[Nxt])`
- **Files:** `Frontier/Engine/VolumetricDynamics/PyroVolumeSolver.cpp` (`lines 1029–1045`)
- **Problem:** Every simulation step transitions and copies `ThermoImg[2]` into `ThermoImg[Nxt]` (plus a runtime `vkGetDeviceProcAddr` lookup) solely to keep `VelImg` and `ThermoImg` at the same index `ReadSlotIdx`.
- **Lossless Optimization:** Track `ReadVelIdx` and `ReadThermoIdx` independently — zero copy, zero extra layout barriers.

### VOL-8: Eliminate Redundant `vkUpdateDescriptorSets` / `vkCmdBindPipeline` & Use Push Constants for Vehicle Parts
- **Files:** `Frontier/Engine/Rendering/VulkanContext.cpp` (`lines 1380–1431, 1734–1740`), `WorldSdfScene.cpp` (`lines 469–515`)
- **Problem:** Every ImGui draw command, every Jacobi iteration (`12–48×`), and all 9 vehicle part draws allocate and rewrite a 6-binding descriptor set via `vkUpdateDescriptorSets`.
- **Lossless Optimization:** Cache `(LastPipeline, LastTex[0..4])` in `VulkanContext` to skip redundant `UpdateDescriptorSets`/`CmdBindPipeline` calls, pre-allocate the 2 ping-pong Jacobi descriptor sets, and push per-part vehicle `ModelMat + Tint + Thermal` (`88 B`) via `vkCmdPushConstants`.

### VOL-9: Enable Backface Culling on Opaque `.facet` Meshes, `LOAD_OP_DONT_CARE`, & Hoist Uniform Shader Transcendentals
- **Files:** `Frontier/Engine/Rendering/WorldSdfScene.cpp` (`line 314`), `Tools/Build/GenerateVulkanShaders.py` (`lines 299, 395, 526, 966`)
- **Lossless Optimization:**
  1. Set `Spec.CullBackFaces = true` on `PipeVehicleOpaque` (closed opaque car body, tyres, rims).
  2. Set `VK_ATTACHMENT_LOAD_OP_DONT_CARE` on `WorldMrtClear` color attachments (`WorldSdfRaymarchFrag` overwrites 100% of pixels).
  3. Hoist per-voxel `-log(uAdvectAndForces0.y) / (dt * 0.35)` and `pow(boxSpan, ...)` to `PopulateCommonPyroUbo` on CPU, and guard fire-light `sqrt` behind `dot(toL, toL) < maxR2`.

---

## Part III — Recommended Implementation Sequence & Parity Verification

1. **Phase 1 — ReSTIR GI Shader & BVH Wins (Highest ROI, Zero Host API Churn):**
   - Implement **GI-1** (popped-node AABB elimination), **GI-2** (`groupshared` BVH stack), **GI-5** (`ReservoirSpatial` donor register & `rsqrt` elimination), **GI-6** (`VarianceWavelet` `x^64` + reciprocal hoisting + LDS tile), and **GI-10** (cabin sphere early-out + uniform HMI planes).
   - **Verification Gate:** Run `RestirGiParity` / deterministic seed captures at refreshes `0` and `11` and verify **0.0 RMSE** (`bit-identical` on integer/hit buffers, `< 1 ULP` FP32 FMA difference on filtered irradiance).

2. **Phase 2 — `DeviceExchange` & GI Frame-Graph Bandwidth Wins:**
   - Implement **GI-3** (fine-grained compute barriers, `SHADER_READ_ONLY_OPTIMAL` / `DEPTH_STENCIL_READ_ONLY_OPTIMAL` DCC/Hi-Z layouts, batched staging + 2 frames-in-flight), **GI-7** (inline upsample in `DisplayRecombination`), **GI-8** (HZB `Resume` skip), **GI-9** (Vose alias table for emissive NEE), **GI-11** (active smooth-pixel compaction for `MirrorTrace`), and **GI-12** (zero-alloc host encoders + dirty-checked `RefreshTlas`).
   - **Verification Gate:** Compare `DirectTarget`, `CurrentReservoirs`, `IrradianceTarget`, `ReflectionTarget`, and final `CaptureTexture` texel-for-texel against baseline refresh dumps + GPU timestamp query pass timings.

3. **Phase 3 — Hardware RT Specialization & Pyro/SDF Volumetric Pipeline Wins:**
   - Implement **GI-4** (`VK_KHR_ray_query` optional specialization) alongside **VOL-1** through **VOL-9**.
   - **Verification Gate:** Run `SessionValidation` + `VolumetricAndEditorValidation --write-proofs` and compare PSNR/max-diff against `VisualProof/01..04.png`.
