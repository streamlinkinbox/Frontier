# CAPABILITY-Ownership — Where Each Capability Already Lives

🔴 **Read this before writing any capability into a host.** Every entry below already exists, is already
linked into every product, and is already reachable through its `Api/` header. Writing a second copy is
the defect this document exists to prevent — it has happened at least three times.

## The rule

> A host owns `main()`, the tick order, and nothing else. Every capability lives in a library unit and is
> reached through its `Api/` header. If a host needs a behaviour that is not a unit, **the unit is what
> gets written** — never a local copy inside the host.

This is `32` §5's gate, restated so it can be checked. `Tools/VerifyHostPartition.py` enforces it.

## The register

| Capability                     | Owner                                                                    |
|--------------------------------|--------------------------------------------------------------------------|
| Editor camera, orbit, fly      | `SlateWorld/World/EditorCameraComponent`                                 |
| Player / spectator camera      | `SlateWorld/World/{PlayerCameraComponent,SpectatorCameraComponent}`       |
| Camera projection matrices     | `SlateDocument/Document/CameraProjection`                                |
| Sky, atmosphere, sun           | `SlateWorld/World/AtmosphereComponent` + `SlateCompute/Compute/AtmosphereIntegrator` |
| Sky presentation on the device | `SlateVulkan/Device/AtmospherePresentationSurface`                       |
| Directional light              | `SlateWorld/World/DirectionalLightComponent`                             |
| Ground grid, lattice, overlay  | `SlateVulkan/Device/WorkspaceOverlayPass`                                |
| Scene pass, CAD pass           | `SlateVulkan/Device/{WorkspaceScenePass,WorkspaceCadPass}`               |
| Window, device, chain, tick    | `SlateVulkan/Device/HostLifecycle`                                       |
| Bring-up, tick order, teardown | `SlateRuntime/Session/SessionSequence` — the order the two below are driven in |
| Product feature mask           | `Application/Api/HostFeature.h` — the ONE reader of the product macro    |
| Interface tick and recording   | `SlateUI/Interface/{InterfaceExchange,ViewportSequence}`                 |
| Immediate-mode drawing         | `SlateUI/Interface/RecordingSurface` (inside `InterfaceExchange`)         |
| Panels, drawers, docking       | `SlateUI/Interface/{EditorPanel,WorkspacePanel,PanelStructure,DrawerSpace}` |
| Which panels a discipline seats| `SlateWorkspace/Discipline/WorkspaceDeclaration` — the arrangement as data, not a procedure |
| Logical/physical pixels        | `SlateWorkspace/Discipline/ViewportProjection` (`DrawableScale.h`) — the one place that converts between what ImGui reports and what the swapchain is made of |
| The surface a sketch sits on   | `SlateWorkspace/Discipline/WorkplaneStanding` — standing planes, offsets, and a plane named by pointing at the viewport |
| Transform handles              | `SlateWorkspace/Discipline/TransformGizmo` — one pixel table both the hit test and the drawing read, so a handle is where it looks |
| Committing a placement         | `SlateWorkspace/Discipline/PlacementCommit` — chooses the geometry by subject AND method, and seals one revision per drawn thing |
| The planes a workspace has     | `SlateWorkspace/Discipline/WorkplaneCatalogue` — every plane, which is active, and the promise that activating one never moves the drawing |
| Authoring a constraint         | `SlateWorkspace/Discipline/ConstraintAuthoring` — what each relationship demands of the selection, its badge, and where the badge hangs |
| What a selection allows        | `SlateWorkspace/Discipline/ToolAvailability` — which tools a held record makes possible, and which row a directory opens on |
| Dragging a selection           | `SlateWorkspace/Discipline/TransformSession` — what moves, about what, by how much, and putting it back exactly |
| Viewport picking and pivots    | `SlateWorkspace/Discipline/SketchPicking` — point before control before curve, which record owns a pick, and what a transform moves |
| Record declaration             | `SlateWorkspace/Discipline/RecordDeclaration` — filing a curve, profile, dimension, constraint or point under its category folder, and declaring every enclosed area as one undo step |
| Plane/screen projection        | `SlateWorkspace/Discipline/ViewportProjection` — the frame, the projection and its inverse. `ResolveSketchBasis` is in the neighbouring `SketchBasis.h` so consumers need not link the sketch kernel |
| Transform keyboard grammar     | `SlateWorkspace/Discipline/TransformSequence` — G/R/S, axis letters, typed amounts, the curve-slide double tap. **Not the arithmetic**, which stays with the geometry |
| Vector arithmetic on `Spatial*`| `SlateShape/Geometry/CurveSpecification` — `Dot`, `Cross`, `Normalize`, `Added`, `Difference`, `Scaled`, `Negated`, `LengthSquared`. **Never re-copy these into a translation unit**; a local copy plus the header makes every call ambiguous |
| Theme, appearance, typography  | `SlateUI/Interface/{ThemeInterchange,ThemeSpecification,AppearanceSpecification}` |
| Content browser                | `SlateUI/Interface/ContentBrowserPanel`                                  |
| Scene outliner / directory     | `SlateUI/Interface/SceneDirectoryPanel`                                  |
| Texture paint layer stack      | `SlateUI/Interface/TexturePaintPanel`                                    |
| Sketch tool interface          | `SlateUI/Interface/ParametricTools` — the CATALOGUE the artist presses    |
| Shape placement, anchor counts | `SketchToolset/SketchTool/SketchPlacement` — which shape, placed how, how many anchors, when it seals |
| Texturing tool vocabulary      | `TextureToolset/TextureTool/TextureToolDeclaration` — skeleton; impressions stay in `SlateCompute` |
| Sketch geometry and solving    | `SlateShape/Sketch/*` — 20 modules, incl. `SketchStructure`, `ConstraintSolver`, `ProfileSolver` |
| Feature history, recompute     | `SlateShape/Sequence/{FeatureStructure,RecomputeScheduler}`             |
| Pick classification, provenance| `SlateShape/Reference/{PickClassifier,ProvenanceIndex}`                |
| Exact curves, surfaces, solids | `SlateShape/{Geometry,Topology}`                                      |
| Boolean, fillet, extrude, loft | `SlateShape/Operation/*`                                              |
| Tessellation                   | `SlateShape/Discrete/TessellationSpecification`                       |
| Brush intent                   | `SlateDocument/Document/BrushSpecification`                              |
| Layer sequence                 | `SlateDocument/Document/SurfaceLayerSequence`                            |
| Stroke execution               | `SlateCompute/Compute/{ImpressionSequence,StrokeSpace}`                  |
| Texture depots and tiles       | `SlateCompute/Compute/{SurfaceDepot,UvSurfaceDepot,SurfaceTileSpace,TileSpace}` |
| Scene import (glTF, OBJ)       | `SlateDocument/Format/SceneMeshImport`                                   |
| Codex interchange              | `SlateDocument/Format/{CodexInterchange,WorkspaceSceneActivation}`       |
| Revisions and undo             | `SlateDocument/Document/RevisionSequence`                                |
| Selection                      | `SlateDocument/Document/SelectionSequence`                               |
| Exact predicates (both toolchains) | `Engine/Shared/*.slang.h`                                            |
| Refusal and delivery contracts | `Engine/Foundation/DeliveryGuarantee.h`                                    |

⚠️ Unit names change during the refactor described in `References/UnitAndProductPlan.md`. Applied so
far: `SlateScene` → `SlateWorld` (step 4), `SlateGeometry` → `SlateShape` (step 5), `SlateFeature` folded
into `SlateShape` (step 6), **`SlateRuntime` created (step 7)**, and **`SketchToolset` + `TextureToolset`
created (step 8)**, and **`SlateWorkspace` created (step 9)**. Steps 10–11 remain. The **owner** does not change — only the path. Update this
register in the same commit as any rename.

🔴 **A host now owns `main()`, its panels, its own device estate, and nothing else.** The bring-up order,
the tick prologue, every recovery branch and the teardown belong to `SessionSequence`. A host that writes
its own `HostLifecycle` / `ViewportSequence` bring-up is reintroducing the duplication step 7 removed —
`PaintHost` reaches **zero** `Lifetime.*` calls, which is the shape a lifted host has.

## What went wrong, so it is not repeated

On 26 August an agent asked to run paint and sketch inside the editor wrote **a second camera, a second
sky and a second CAD editor**, all of which appear in the register above. Two mechanisms allowed it, and
both are being closed:

1. **The feature seam was dead.** `SharedViewportHostBridge.h` declared `SLATE_*_HOST` masks, the build
   defined none of them, and no caller ever read the result. An agent looking for the live mechanism
   correctly concluded there was none and wrote its own. Closed by the `[product]` define and the
   `#error` fallback — and then **only half closed**: step 7 found that `HostFeature.h`, the replacement,
   was itself included by no host at all. A seam is not live because it compiles; it is live because
   something reads it. `EditorHost` now includes it and states `HostProduct` at bring-up.
2. **Capabilities were unreachable.** `ParametricSketchHost.cpp` held 5 981 lines with 138 local function
   definitions — a library written inside an executable, reachable from no other host. An agent asked to
   reuse it had no way to. Closed by lifting those behaviours into units.
3. **A shared vocabulary was shared in name only.** `Application/Api/` declared the CAD draft subjects so
   both hosts could agree on them, and then `ParametricSketchHost` declared its own identical copy and
   `static_cast` between the two, while `EditorHost` resolved the shared one and immediately discarded the
   result with `static_cast<void>`. Both hosts satisfied a validator that checked for the include; neither
   actually consumed the seam. Step 8 deleted the header, the copy and the casts. ⚠️ **A validator that
   checks for a file name or an include proves nothing about whether the mechanism is used** — check the
   property. `ValidateHostBuildBudgets` now fails if a host re-declares the enumeration or casts between
   copies of it, and both of those checks were confirmed to fail on a deliberately reintroduced defect.

📝 `Engine/Application/EditorHost/Source/SkyImage.cpp` sat referenced by **nothing** for long enough that
it was read, assumed to be unimplemented, and rewritten. Dead source is not free; it actively misleads.
