# Renderer progress — 2026-10-26

This is a dated exhibit; later progress and corrections are recorded in the [project tracker](../../Documentation/Tracker.md).
In particular, [spot/IES cell checks](ClusterSpotIes.md) now distinguish analytic clustering from separate,
non-clustered emissive-facet sampling; Moon and vehicle reference work have also progressed beyond this snapshot.

**Latest addition: thin-pane spectral transmittance across area-emitter quadrature, volumetric fog shafts and multi-stratum mixtures.**
See [thin-pane & shaft report](PaneTransmittance.md) and [six-pose glazing animation](GlazingMotion.md). All current
execution evidence is native C++ and **SwiftShader CPU Vulkan**, not physical RTX/GTX or the Windows editor.

**Legend:** 🟢 verified within the stated scope · 🚧 partial / still needs work · 🔴 blocked or not done.

![Four-panel Vulkan comparison of pinned leak vs accepted spectral transmittance](Images/PaneTransmittanceComparison.png)

![Actual six-pose native glazing animation](Images/GlazingMotion.gif)

**Latest addition: prior-screen exitance pyramid, rigid authored motion and unified six-pose vehicle.** See [prior-screen exitance report](PriorExitance.md). Software Vulkan (SwiftShader CPU type 4) only. The baked DistanceFieldSpace is a certified clearance query, not a traversal accelerator.

## Done and remaining

| Status | Area | Done / evidence | Remaining |
|--------|------|-----------------|-----------|
| 🚧 | Analytic and emissive lighting | Clustered analytic selection, visibility and reservoir fixtures; separate emissive-facet sampling | Emissive clustering, full engine/hardware acceptance; not every emitter type is covered |
| 🟢 | Original ControlVehicle material scene | Existing Racing Green cellular-flake coat, rubber, thin windows, BVH mirrors, cones and reused material previews captured in earlier checkpoints | General scene/editor importer; full moving-scene and hardware integration |
| 🟢 | Primary smooth closed glass | Snell, exact Fresnel/TIR, radiance-index factors, distance absorption; four analytic slab cases | General arbitrary-mesh and scale acceptance |
| 🟢 | Mirror → closed glass | Four additional slab cases; actual behind-camera geometry seen through mirror; 6,144-pixel source-off black test | More mirror layouts, moving volume captures and general recursive mirror chains |
| 🟢 | Current pole-shadow fixture | 72 combinations, 3,538,944 silhouette comparisons, zero mismatches | Does not reproduce the user's old project defect |
| 🟢 | Rough reflection history fixture | Angular-aware reconstruction, 88 native filter probes; stale authored geometry history rejection; compact-only diffuse policy retained | Broader motion/noise/convergence coverage |
| 🟢 | Sun/flare/stars and selected cellular flakes | Existing implementations retained, not replaced by unrelated rewrites | Deferred sky/cloud appearance improvements below |
| 🟢 | Sampled rough reflection → smooth glass | Flat shared trace site; cosine/GGX proposals; absorption monotonicity, seeded repeat, screen/BVH equality and source-off checks | Broader material/scale and estimator acceptance remain |
| 🟢 | Thin window → smooth glass | Four sheet/slab cases, two retained sheet-energy controls, source-off and pinned blocked-path control | Approximate sheet optics; no exhaustive multilayer/scale acceptance |
| 🟢 | Compact diffuse → smooth glass | Uploaded cosine samples and diffuse weights retained; Beer/repeat/source-off/counter checks and shielded emitter-routing fixture | Complete authored-surface MIS/caustic acceptance remains |
| 🟢 | Thin-pane emitter, fog-shaft & mixture acceptance | `SegmentShadowTransmittance` across `SampleEmissiveFacet`, `SampleFogFacet` and `IntegrateFogScattering`; 8-pane BSDF continuation; 2,016 surface + 1,463 fog probes; 4 multi-stratum preset mixtures; [report](PaneTransmittance.md) | Separate authored-surface area-emitter MIS and general CAD sheet thickness authoring |
| 🟢 | Saved volume spectrum | Native optional TOML IOR/RGB absorption, disk round trips, compiled metadata and actual slab uploads; [evidence](SavedDielectric.md) | Glass studio preview, editor controls and general renderer importer |
| 🟢 | Derived vehicle glazing | Three closed6mm components /284 triangles; saved native coefficients; fresh vehicle Vulkan acceptance | General CAD/editor importer and unified full-scene acceptance |
| 🟢 | Six-pose closed glazing | 6/6poses,284closed triangles/4mirror facets each,17718warm opaque reuse,3547glass/72mirror observations exact fresh,23040black pixels; [report](GlazingMotion.md) | Global moving authored history stays conservative; physical/editor/unified scene still open |
| 🚧 | Full glass/material system | Bounded2048-interaction /128-sample tracer, saved spectra and derived vehicle assignment | Full preset/editor integration, rough glass, nested media, inside cameras, caustics and transmitted direct shadows |
| 🚧 | Reflection reconstruction / denoising | Roughness-cone prior-screen exitance pyramid (native, pinned control, guarded refresh); sharp-strip estimator bias 5.7% measured | Full denoiser, mirror-lobe comparison and complete sampling/PDF/material acceptance |
| 🚧 | Clouds, fog and shafts | Earlier actual cloud/fog/geometry-shadow fixtures; lower-cost working cloud budget | Improve blocky clouds, horizon/sky, Milky Way/catalogues/extrasolar stars and dawn/sunset; moving full-scene acceptance |
| 🚧 | Bake / moving full scene | Earlier vehicle, triangle-BVH and distance-field-related exhibits exist | Unified ControlVehicle + baked distance field + sun/sky/cloud/fog moving-scene acceptance; no inherited full-SDF completion claim |
| 🟢 | Unified moving vehicle scene (fixture) | Six poses with closed glass, mirrors, cones, cloth, clouds, fog, analytic + emissive light, zero caps, 23,040 source-off black | Production scene, editor import, physical GPU |
| 🔴 | DistanceFieldSpace traversal acceleration | Clearance bound verified (0 violations); traversal uses exact BVH | Hierarchical skip for rigid subset only if measured faster |
| 🔴 | Physical rendering integration | Software Vulkan fallback exercises triangle BVH | Physical RTX hardware tracing, physical GTX fallback, Windows/editor/viewport validation and real device timings |
| 🔴 | Old half-pole shadow defect | Current fixture investigated; no reproduction | Need the old project/commit/file or screenshot to identify it; not marked fixed |
| 🔴 | Moon | Planned feature only | Implementation and rendered verification |
| 🔴 | Vehicle dynamics / Jolt | Native C++ direction and references retained | Audit original Project Drive/forks; native Jolt integration and modular XPBD tyre/spring/3D FEM work |

## Counts, not an invented global percentage

| Acceptance set | Current scope |
|----------------|---------------|
| Boundary arithmetic | 199/199 native/GPU cases; 149 reciprocal native identities |
| Primary slab cases | 4/4 |
| Mirror slab cases | 4/4 |
| Window/slab cases and sheet controls | 4/4 plus 2/2 |
| Rough reflector / smooth glass | 3 optical screen/BVH comparisons, Beer monotonicity, exact repeat and source-off checks |
| Compact diffuse / smooth glass | Beer monotonicity, exact repeat, counters, shielded emitter routing and source-off checks; 901-probe vehicle oracle; six compact moving poses and improved reconstruction RMSE |
| Thin-pane emitter, fog shafts & mixtures | 8/8 ASan/UBSan native checks (`7.37e-7` mixture/cosine identity); 2,016 surface probes (`1.54e-8` max); 922 analytic + 541 emissive fog probes (`1.45e-6` max); 4 multi-stratum mixtures; pinned `f5d99d4` negative control |
| Native renderer CTests | 4/4 |
| Physical-GPU / Windows checks in this batch | Not run |

Those passing fractions describe fixtures, **not overall renderer completion**. A global percentage would
be misleading while hardware/editor, material authoring and unified moving-scene
acceptance are outstanding. No real-time performance or completed renderer claim.

## Suggested execution order

1. Saved closed-glass authoring, six-pose vehicle captures, thin-pane emitter/fog-shaft transmittance and four-preset multi-stratum mixture convergence now pass.
2. Roughness-mip prior-screen exitance/colour hierarchy, continuous moving authored reflection history and broader denoising acceptance.
3. Unified moving `ControlVehicle` + baked distance field + sun/sky/cloud/fog scene acceptance (without assuming inherited full-SDF completion).
4. Physical RTX hardware ray-query, physical GTX triangle-BVH fallback and Windows/editor viewport integration with real device timings.
5. Deferred sky/cloud appearance, Moon, legacy half-pole defect reproduction (requires source commit/asset) and separately scoped native Jolt/XPBD vehicle dynamics.

Evidence: [thin-pane & volumetric shaft report](PaneTransmittance.md), [six-pose glazing motion](GlazingMotion.md), [saved volume authoring](SavedDielectric.md), [compact diffuse phase](DiffuseDielectric.md), [thin window phase](WindowDielectric.md), [rough-reflection glass phase](RoughDielectric.md), [mirror phase](MirrorDielectric.md), [primary glass](SolidDielectric.md),
[shadow/history investigation](ShadowHistory.md), [rough reflections](RoughReflections.md),
[vehicle/material scene](MaterialScene.md). Foliage remains isolated and inactive.
