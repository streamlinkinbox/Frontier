# Material and shoreline research → Frontier v0.5

Research/design pass: 2026-09-08. Scope: **surface appearance and water optics**.
The sculpt brushes and erosion equations are deliberately unchanged. A procedural
material is not a measured specimen, and choosing a rock surface does not silently
replace the volume or change its mechanical parameters.

## 1. Sources that informed the implementation

| Source | Relevant finding | Applied here |
| --- | --- | --- |
| Google **Filament**, standard material model, specular BRDF, parameterization | Material color/roughness/reflectance must be separate from lighting; GGX with Smith visibility describes a microfacet lobe; lighting uses linear RGB. [5](https://google.github.io/filament/main/filament.html) | Linearized color picker, GGX + correlated Smith visibility, Fresnel-derived dielectric reflectance, bounded roughness, separate albedo/normal/roughness generation. |
| **Physically Based Rendering**, 4th ed., §9.3 | Dielectric reflection/transmission depends on IOR and incident direction. Water is approximately 1.333; rays exiting the denser medium can undergo total internal reflection. [4](https://pbr-book.org/4ed/Reflection_Models/Specular_Reflection_and_Transmission) | Explicit air→water versus water→air indices, exact unpolarized Fresnel for water, zero-vector/TIR guard. |
| Epic Games, **Single Layer Water** | Water rendering separates scattering, absorption, refraction, reflection and shadowing; depth/color behind water is essential to transmission. [4](https://dev.epicgames.com/documentation/en-us/unreal-engine/single-layer-water-shading-model-in-unreal-engine) | Beer–Lambert RGB attenuation from traced travel, lit (not emissive) medium contribution, safe first-entry rays, independent camera-water path lengths. This is not Unreal's renderer. |
| **Utah Geological Survey**, rock classification | Sandstone comprises cemented sand grains; granite is coarse crystalline/intrusive, while basalt is fine grained/extrusive and commonly dark. [3](https://geology.utah.gov/map-pub/survey-notes/glad-you-asked/igneous-sedimentary-metamorphic-rocks/) | Distinct granular, crystalline and fine-matrix models, rather than re-coloring one sandstone shader. |
| **Encyclopaedia Britannica**, limestone | Limestone is predominantly calcium carbonate (calcite/aragonite); textures range from microscopic grains to visible constituents. [4](https://www.britannica.com/science/limestone) | Fine chalky/calcitic matrix and restrained mottling/pitting, not granite-like large crystals. |
| **Geology In**, sandstone classification | Sandstone grains are sand-sized, approximately 0.06–2 mm, rather than centimeter-scale bright blobs. [2](https://www.geologyin.com/2023/12/sandstone-sedimentary-rock.html) | Millimeter grain controls and pixel-footprint filtering; unresolved grains average into color/roughness. |

These sources give principles and material structure, **not a measured RGB/roughness
scan of a specific rock**. The shipped colors, pore coverage, weathering and roughness
are authored starting points. Effective IOR defaults to the common dielectric 1.5
(F0 = 4%); it is editable and is not claimed to be a measured bulk-rock optical index.
No source photographs or third-party texture maps are embedded in the application.
Image search results were used only as optional references, not as material assets.

## 2. What was wrong with the previous surface

Repository inspection showed a single sandstone palette, meter/centimeter-scale
noise presented as grain, a fixed diffuse/bounce treatment and a hand-tuned specular
power. Increasing `Surface detail` amplified that appearance rather than providing
independent material properties. Under magnification, pale mineral spots could read
as oversized flecks and the whole surface as a soft, uniformly noisy material.

The replacement is in `src/engine/shaders/materials.wgsl` and is shared by the WebGPU
and WebGL2 paths:

- **Sandstone:** cemented granular matrix with controllable bedding and iron-colored
  weathering; sand-sized grains are normally below pixel resolution at canyon scale.
- **Limestone:** light, fine matrix with low-contrast mottling and solution-pit-style
  relief. This is a generic weathered limestone, not a fossil/bedding reconstruction.
- **Granite:** jittered cellular mineral domains; dark mica-like grains, neutral
  quartz-like grains and warmer feldspar-like grains vary color, facet normal and
  roughness. Igneous presets do not inherit sedimentary stripe shading.
- **Basalt:** dark fine matrix, subtle oxide variation and independently controlled
  vesicle-style surface pores. Coverage can be reduced for a dense basalt.

Controls: base color (sRGB UI → linear shader), perceptual roughness, grain size in
millimeters, relief in millimeters, mottle/pore scale, pore coverage, sedimentary
layer contrast, weathering, effective IOR, and moisture. The old detail control is
only a final normal-detail gain. Pore coverage is a rendering parameter, **not bulk
porosity or a mechanical material constant**.

### Light response and scale

The direct-light term uses GGX/Trowbridge–Reitz D, correlated Smith visibility and
Schlick Fresnel for the rock surface. Perceptual roughness is squared once for the
microfacet distribution. Entry/exit Fresnel factors attenuate diffuse energy instead
of simply adding a bright specular term on top. CPU tests check reciprocity and a
rough white-furnace quadrature. This is still a single-scattering real-time model;
we do not claim a complete multiscattering/spectral/path-traced solution.

Grain and pore detail are filtered by world-space pixel footprint including grazing
angle. Unresolved structure contributes to an averaged albedo and broader roughness,
not larger screen-space dots. Relief modifies shading normals only: the 0.667–1 m
volume grid cannot represent individual grains as geometry.

Moisture is an optical treatment: porous areas darken, the lobe tightens, and effective
surface reflectance approaches a water film. Automatic shoreline/runoff moisture and
an explicit moisture control combine. This is not a measured multilayer wet-rock BSDF.
The sky/ground environment and illumination are still analytic approximations, not
an HDR probe or calibrated photometric lighting rig.

## 3. The colored water boundary

The old code offset a refracted ray by a fixed **0.10 m**. At a bank with only 0.02 m
of clearance, that can put the origin **inside the solid**. The ordinary SDF trace
then searches for a later exit. Treating that exit distance as water thickness can
turn a genuinely thin water contact into a many-meter absorbing layer. A fallback
depth/tint and unshadowed volume/foam contributions made the error more visible.
Underwater partial coverage also used one shortened path for both background and
water, creating a discontinuity at the edge.

Changes in `render.wgsl`:

1. Limit offsets to a fraction of local clearance and check the candidate point.
2. Use **entry-only** water rays: an origin at/inside rock is an immediate contact,
   not permission to traverse the bank. Ray-budget exhaustion is marked uncertain.
3. Do not invent an eight-meter water depth for an uncertain ray. Keep the known
   background with no extra absorption; a true volume exit uses its bounded distance.
4. Keep foreground terrain in front, find the first crossing inside a bounded wave
   slab (rather than accepting a farther Newton root), validate its side, and smooth
   sub-pixel shoreline coverage rather than hard-discarding at a fixed distance.
5. Beer–Lambert transmission tends continuously to 1 as optical thickness tends to 0.
   Secondary rock shading retains macro shadows/AO rather than using a fully sunlit
   fast path; the bed must not glow underneath a shaded overhang.
6. Light the water-volume and neutral foam contributions; do not emit cyan in shadow.
7. Handle above/below-water indices and TIR explicitly. Attenuate the two optical
   paths **before** mixing partial coverage, so a tiny sliver does not shorten the
   opaque path for the entire pixel.

Water may legitimately reflect blue sky and deeper water can be blue/green. The goal
is to eliminate an unjustified colored **boundary strip**, not to forbid all blue.
The procedural waves remain a visual surface, not a fluid free-surface solver.

## 4. Verification and integration

- Unit tests: sRGB conversion, dielectric F0, scale filtering, BRDF reciprocity and
  rough furnace bounds, zero/thin transmission, clearance-bounded offsets, TIR,
  partial-coverage continuity, parameter validation and GLB material metadata.
- Browser tests: four material structures on the **same unchanged volume**, live
  roughness/moisture/IOR controls, and above/below-water overhang scenes.
- A shader-level WebGL probe uses a known bank 2 cm away: its ray origin must remain
  outside rock and the first entry must be near, not the far side of the solid.
- GLB carries selected broad-band material color, roughness and `KHR_materials_ior`.
  Millimeter patterns are averaged for coarse mesh vertices, not falsely baked as
  noisy triangles; shader-only detail/optical water is not baked into the mesh.
- Shared uniform layout: **36 vec4 slots / 576 bytes** (v0.7 includes the channel arc table and layered-relief controls). `UNIFORM_BYTES` drives allocation
  on both backends. Old projects get material defaults without modifying saved SDFs.

The numerical and screenshot tests validate implementation behavior. They cannot
establish that a procedural material exactly matches an arbitrary real specimen.

## v0.7 extension: the missing middle scale

The original millimeter-scale grain model did not supply enough **centimeter-scale
surface structure** at ordinary inspection distances. v0.7 adds a separately
controlled, band-limited height/gradient stack: domain-warped fBm, smoothed ridged
noise, broken sedimentary layers and a restrained cavity response. It is not a
material-color swap, and it does not alter the saved voxel geometry. The new
surface-detail presets are authored art controls, not measured specimen scans.
The procedural river also gains arc-distance downstream advection along the
seeded canyon route. It is an explicitly routed surface visualization, not a
claim to reconstruct the fluid velocity field from erosion data.
