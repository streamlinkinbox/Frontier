# Master Plan

**Owner:** Project Drive / shared Frontier engine  
**Started:** 2026-10-05  
**Standing:** Ongoing — native implementation and verification

## Goal and agreed order

Start with **native C++ XPBD tyres**, including an object-space rest SDF bake and distance fields that follow the
actual solved tyre geometry. Demonstrate the result with Project Drive tyre renders before extending dynamic
fields to complex vehicle bodywork. Keep native ReSTIR; geometry representation and sample reuse are separate.

The browser dynamic-SDF study established an executable approach, not unrestricted accuracy or a real-time update
budget. Thin features disappeared at finite voxel resolution. Its full-volume rebuild was substantially more
expensive than tracing a reused field. Those limitations remain requirements, not problems to hide.

## 1. Native tyre geometry and bake

- Use `XPBDSoftTyre` node positions and its material frame, not the browser's analytic deformation.
- Define the closed tyre-envelope representation explicitly: tread, sidewalls and bead-to-bead inner closure.
  Preserve the central wheel opening. This is a lighting envelope, not resolved rubber thickness/internal air.
- Bake a local signed volume from that triangulated surface, with a reproducible content signature and grid metadata.
  Reuse only a matching bake; changing geometry, grid settings or algorithm must invalidate it.
- Refit the surface acceleration data and reconstruct the current local field from solved positions. Track geometry,
  field and bake revisions separately; unchanged input must not trigger a redundant bake.
- Refuse invalid/nonfinite/degenerate input rather than silently tracing a stale field as if it were current.

## 2. Project Drive and shared-renderer handoff

The current native project callback sends wheel transforms only. A dynamic SDF alone does not fix this missing
vertex stream. Add a project-neutral geometry extension without giving Project Drive its own window or renderer.

- Project Drive owns the XPBD-to-visible-surface projection.
- The shared host owns mutable scene geometry, culling, raster uploads, traversal and lighting resources.
- Raster geometry and native distance construction must consume the same completed deformation snapshot.
- Changed geometry invalidates surface/radiance history and traversal data, including any rigid-only acceleration.
- Stop/reset restores authored geometry. Other projects and frozen vehicle assets must remain unaffected.
- A conservative synchronized rebuild is acceptable for initial correctness, but its cost must be exposed.

## 3. Required evidence

- Native C++ execution: unloaded, loaded/contact and asymmetric ground cases using the actual XPBD solver.
- Independent triangle-distance/intersection comparisons against field queries, including misses and error tails.
- Signed slices and field-only surfaces that do not hide the SDF behind the visible tyre.
- Project Drive geometry/field revision and restore checks, plus refused-input and unchanged-input cases.
- C++-generated tyre renders with provenance. Label CPU reference renders separately from production Vulkan images
  or native window captures; do not substitute a browser scene or generated artwork.
- Build coverage for the existing Linux/headless and Windows/MSVC routes, with failures or unexecuted paths recorded.

## 4. Performance work after correctness

Profile on the target GPU. Compare full moving-frame cost, not just cached-field tracing. Investigate per-object
fields, dirty bricks covering old and new extents, narrow-band/distance-propagation construction, and GPU uploads
that do not reconstruct the complete scene. Preserve synchronization and version checks while optimizing.

For constrained deformations, inverse-warped local fields may be useful, but distance bounds and invertibility
must be validated. Arbitrary crumpling and tearing need their own update/topology policy.

## 5. Native ReSTIR and complex vehicle geometry

Reuse current field hits/visibility in the native lighting paths. Reevaluate visibility and target weights or
reject stale reservoirs when deformation, surface identity or field coverage invalidates them. ReSTIR cannot
restore a panel absent from the field.

Move to production bodywork only after the tyre gate. Specify handling for open sheets, thin shells, nearby opposing
surfaces, material lookup and topology changes. Higher resolution is a tradeoff, not a promise of exact geometry.
Retain selective accurate queries where the field cannot meet the required error tolerance.

## Progress

- Browser SDF study and thin-feature limitations: demonstrated; see `Frontier/Docs/DistanceTransportReview.md`.
- Native investigation: completed. The missing deformation handoff and transform-only SDF refresh are identified.
- Native tyre field, project handoff and proof: in progress. Do not treat this plan as evidence of completed work.

## First native implementation — executed evidence

The following is implemented, rather than merely planned:

- `Engine/GeometricRaster/DeformationSpace.h`: closed indexed-surface validation, CPU triangle BVH, parity sign,
  cell-centred signed-volume reconstruction, unchanged-input suppression, revision and content signature.
- `Project-Drive/Source/TyreSequence.h`: the same actual-node projection for the visible outer surface and closed
  field envelope. Author rings run +Y to -Y; solver rings run -Y to +Y. The explicit reversal is tested.
- `GeometryInterchange.h` and `CodeInterchange`: optional, separately versioned synchronous C export, without
  changing ABI3 or retaining project pointers after DLL retirement. Material identity keeps rim geometry rigid.
- `Engine/Host/GeometrySequence.h`: private topology, completed snapshot publication, reconstructed smoothing,
  culling refresh and exact Stop restoration. `FrontierRuntime.cpp` rebuilds traversal and uploads the current scene;
  the existing native distance-geometry construction then invalidates its volumes/material capture/lighting history.
  Rigid-only two-level traversal and authored coarse errors are not used for the deforming scene.
- `DriveContentHost`: generates and verifies `Content/DistanceFields/DriveTyre.sdf` plus its signature. Corrupt or
  stale content fails verify-only mode. The manifest is shared by CMake and the direct Windows content build.

### What the rest asset currently does

The SDF1 bake is a local signed envelope asset and a reproducible reference, not an already-streamed GPU object-field
cache. The production native path currently reconstructs its **hybrid world field from the deformed scene**. Its
existing triangle refinements remain, and ReSTIR is unchanged. The content validator currently reconstructs the
expected bake before comparing it, so a verified existing file is not a cheap construction-cache hit.

### Checks and renders

`VisualProof/TyreDeformation/ExecuteTyres.py` compiles and executes the actual project DLL, content author and C++
checks. Its gallery is `VisualProof/TyreDeformation/index.html`; twelve PNGs are retained in `Captures`.

- Real Drive DLL + authored opening scene: 35,664 vertex records rewritten; **6.38 mm** maximum departure from the
  pose-only tyre projection after 120 Simulate frames at 240 Hz. These are non-rigid changes, not merely wheel motion.
- Native distance-geometry revision, fixed triangle count, culling containment, coarse-error invalidation,
  unchanged revision, pause, DLL retirement and byte-exact Stop restoration pass.
- Closed fixture envelope: 1,408 vertices / 2,816 triangles over the actual 9-by-128 solver lattice.
- Loaded fixtures: 207 flat-ground contacts and 197 banked contacts; 69.99 mm and 123.89 mm maximum node displacement.
  These are prescribed-hub stress tests, not a claim of vehicle load equilibrium.
- Projection matches solved tread nodes to within 0.14 micrometres in these fixtures.
- SDF1 roundtrip, changed/unchanged revision, nonfinite/open/degenerate geometry, invalid resolution/index and a
  damaged on-disk bake are checked. Manifold validation does **not** prove absence of self-intersections.
- 5,184 paired camera rays across the three fixtures: 12 / 15 / 12 reference hits missed; zero extra hits.
  Median paired depth error is about 0.34 mm, but worst paired errors reach 632 / 103 / 103 mm when an earlier
  surface is missed. Preserve the full distributions and counts; median error alone is misleading.

### Execution boundary and remaining work

The pictures are **executed C++ CPU field-only references**, including a snapshot obtained through the real Drive
DLL. They are not editor screenshots, Vulkan captures, progressive production GI or an analytic browser tyre.
Primary hits and tyre shadows have no triangle fallback. The native host translation unit was also GCC syntax-checked.
Windows/MSVC native Drive playback, bake, handoff and render checks passed for implementation commit `614d469`.
The independent job receipt is `VisualProof/TyreDeformation/WindowsVerification.json`. ASan, UBSan and leak checks
also passed for that implementation. A subsequent no-op guard avoids allocating geometry snapshots for unrelated
projects; the complete GCC check suite was rerun after that guard. The separate full-engine CI build is still pending.

A local 64-cubed CPU reconstruction took about 1.0–1.6 seconds in this environment. The host's complete scene/GPU
rebuild is deliberately conservative and unprofiled on target hardware. A native Vulkan tyre presentation capture,
optimized object-field upload/dirty updates, validated temporal reuse and target-GPU measurements are still gates,
not completed claims. Rest-grid spacing is approximately 17.8 by 8.1 by 17.8 mm. Thin bead tips, grazing rays,
self-intersection/tearing and the unresolved internal rubber/air structure remain explicit limitations.


## 8. Full Project Drive SDF presentation and fracture-editor follow-up — 2026-10-05

The user resumed full-vehicle GI work before fracture integration. The production SDF execution path now accepts
rest and loaded snapshots exported from the actual Drive opening scene and DLL. It keeps all 22 instances and
22,046 facets, including the tyre geometry handoff. Shader captures use software Vulkan, diffuse material overrides
and independent snapshots; they must not be called a hardware run, full material presentation or continuous GPU
streaming. Consult `VisualProof/SdfScene/Drive/*/Provenance.json` for executed results and limitations.

Next native gates remain the interactive vehicle presentation, production material response and deformation-frame
invalidation under continuous play. Preserve native ReSTIR and existing raster paths; do not replace them with the
browser fracture preview.

The fracture editor remains an HTML-first review at `Frontier/Experimental/FractureEditor/`. The C045 revision
(2026-10-06) removes sheet metal and the material-example gallery from the active editor. Project-Zero now binds
Enable fracture, Dynamic/Baked and expansion to a selected scene ID. Material changes preserve the selected
primitive and scale; browser triangle bakes persist per object and invalidate on geometry/recipe changes.

The prior glass-region coverage loss is replaced by closed, volume-preserving convex partitioning. Shared edge
crossings, complete caps, quality-aware triangulation and fragment-slenderness checks reject defective cuts rather
than discarding geometry. Checks pass for 168 shape/material/seed combinations, 21 browser workflows and the
existing main-editor regression. Source snapshots from both supplied branches remain hash-pinned and unchanged;
the active geometric correction is separate from those snapshots. See C045 in `Frontier/Docs/ProjectZeroHtmlChanges.md`.

Native integration still waits for visual approval. Current source geometry is the HTML editor's analytical convex
primitive plus scale, not arbitrary native mesh import; concave shapes are explicitly refused. Follow-up work must
carry over Jolt contacts and breakable supports, safe geometry ownership, author-created geometry handling,
material-appropriate failure and native serialization, without treating every material as brittle cells.


Executed result for this step: the loaded full-vehicle production SDF case passed on software Vulkan in run
`37375125331` (source `4eee806`), with GI-on/off images and zero validation errors. The authored/rest image was
captured, but its GI-off comparison timed out at 2,700 seconds, so the overall workflow did not pass. Independent
loaded-image comparison gives RGB RMS 3.5992988/255 across 58,325 changed pixels. This closes the loaded-scene
production-shader readback gap, not the interactive native-window, continuous GPU update or full-material gates.

C047 (2026-10-06): the user explicitly reaffirmed **HTML-first** rather than native fracture/SDF integration or a
browser SDF-computation prototype. Baked fracture now authors an optional per-piece SDF request at 32³/64³/128³,
planned R16F. It persists/synchronizes/exports without changing the triangle-geometry signature. Successful browser
bakes report geometry ready / SDF pending; exports explicitly set `Generated:false`. Concave/imported geometry
records can author controls, but unsupported execution remains refused. No actual SDF or arbitrary mesh importer
has been added. Square metric tiles and complete type-specific light authoring cards accompany this review; original
native cards and pinned source depots remain intact. The new 22 lighting/SDF-authoring checks, existing 22 fracture
browser checks, 168 geometry cases and main inspector regression pass. See C047 and its captures in
`Frontier/Docs/ProjectZeroHtmlChanges.md`.

After explicit native approval, each genuinely separated fragment needs its own SDF bounds, object-to-grid transform,
validated signed distances, storage/budget policy and geometry-revision invalidation. GPU component labeling/flood-fill
is conditional on a voxel connectivity workflow, not mandatory when the fracture algorithm already supplies closed,
separate pieces. Native runtime geometry, concave decomposition, collision/support integration and complete
serialization remain open gates; an HTML checkbox does not close them.
