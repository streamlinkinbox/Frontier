# Project Everest — UI-first native port

## Scope and source selection

First milestone: browser → select VehicleHMI application → Project Everest workspace.
Owner clarification: reproduce the editor UI of the VehicleHMI demo containing ReSTIR, not a new UI design.
Panel priority: LogStream → Outliner → Inspector; renderer work follows these panels.
Current workspace is the integration checkpoint, not completed VehicleHMI editor UI parity.
Keep the canonical shared UI; do not import a competing editor or start renderer integration yet.
Persistent links, pinned revisions, synchronization direction and lifetime rules are in `CLAUDE.md`.

| Reference       | Inspected evidence / code                                  | Recommendation                       |
|-----------------|------------------------------------------------------------|--------------------------------------|
| Canonical       | `UI/Editor`, `PanelDockspace`, projectspace codec, native app | UI integration baseline              |
| `08e7de48`      | LightCompute README, recent CDF and reservoir history         | Sampling proposals and CDF research  |
| `5ec53579`      | LightCompute README, endpoint audit history                   | Visibility / temporal audit evidence |
| `9ce908bc`      | LogStream implementation, scene inspector API, native app    | Reuse LogStream; inspect scene API next |

No branch is globally best. This is a focused source assessment, not an exhaustive audit of every file or a rerun
of their GPU experiments. Their numerical results are inherited claims until reproduced on the selected renderer.
The LightCompute README in `5ec53579` and `9ce908bc` explicitly identifies software Vulkan / SwiftShader execution,
not physical GPU or hardware ray-query acceptance. Do not label all three branches as hardware-raytracing-only.

## UI port sequence

| Order | Component                  | Implementation direction / acceptance                                    |
|-------|----------------------------|--------------------------------------------------------------------------|
| 1     | Browser and application    | Everest catalogue entry, application selection, validated native handoff |
| 1     | Docking / Control Centre   | Existing PanelDockspace and ControlCentrePanel; no duplicate widgets      |
| 1     | LogStream                  | Existing ring, category registry, filters and presentation from `9ce908bc` |
| 2     | Outliner                   | SceneDirectoryPanel projects live registered RecordTokens                |
| 2     | Inspector                  | SceneInspectorPanel renders the selected record's authored description   |
| 3     | Sun / sky / atmosphere     | Shared environment record inspectors; no project-specific duplicate UI   |
| 3     | Vehicle                    | User-authored specification, construction and inspector descriptions     |
| 4     | Materials / lighting       | Shared controls; validated edits and authored project persistence        |
| 4     | Fracture / fluid / particle | Dedicated docked editors, opened from inspector actions                  |
| 5     | Simulation / HMI / telemetry| Bind actual native simulation only after lifecycle tests                 |
| 6     | Rendering controls         | Capability-selected Vulkan implementations, truthful unavailable labels |

Orders 2–6 are planned, not implemented by this milestone. No fake Outliner or Inspector has been added.

## Construction and destruction contract

```text
User constructor
    → construct scene payload and user-authored inspector description
    → reserve stable RecordToken / register SceneDirectory entry
    → publish selection and inspector availability only after success

Selection → authenticate RecordToken → resolve authored description → draw shared inspector controls

User destructor / teardown
    → invalidate selection and inspector access
    → unregister SceneDirectory entry / invalidate token generation
    → release scene payload
```

- Use existing RecordStore / SceneDirectory mechanisms before creating another registry.
- Constructor failure rolls back reservations and published descriptions.
- Destruction is idempotent; no pointer to destroyed fields may survive in an inspector or dock callback.
- Recycled storage must reject stale tokens. Undo recreates a valid registration, not a stale pointer.
- Application records author their own design and inspector; environment controls are reusable engine components.
- Dedicated editors use the same lifetime token and close or become unavailable on destruction.
- Acceptance: create, select, edit, delete, undo, constructor failure, repeated teardown and stale-token rejection.

## Renderer follow-up, explicitly deferred

| Path                       | Selection criterion                         | Required verification                     |
|----------------------------|---------------------------------------------|-------------------------------------------|
| Hardware ray query         | Vulkan acceleration structure / ray query   | Physical-device captures and validation   |
| Software BVH + SDF         | Compute/storage limits, no ray-query need   | Lower-capability GPU captures and budgets |
| High-quality ray tracing   | Measured throughput / memory, user setting  | Quality and timing against frozen scenes  |

GTX/RTX branding alone cannot select a path. SDF is an implicit-surface representation; a software BVH may also
intersect triangle surfaces. Neither is the same as hardware traversal. ReSTIR sampling/reuse is a separate concern
and must be verified on both tracing paths. Preserve visibility, transmittance, history rejection and material
contracts before comparing performance. Audit the three branches' actual shader/device requirements next.

## Current acceptance

See `Exhibits/ProjectEverest/README.md` for reproduction, actual images, current checks and limitations.
No Windows binaries requested or delivered. No Actions run. No GPU rendering claim.
