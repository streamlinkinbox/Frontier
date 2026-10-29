# Completion-gated replay submission sequence

## Scope

`Frontier/Engine/Rendering/PathReplaySubmissionSequence.{h,cpp}` adds an optional C++ sequence around the
existing `RecordPathReplay` recorder. It validates the bounded scene, records the existing compute dispatches and
holds a candidate history snapshot until the caller confirms completion. The caller still creates buffers and
pipelines, supplies barriers and descriptors, submits the command buffer and waits on the fence. This is an
**offscreen integration step**, not an editor viewport or a complete resource-owning renderer.

```text
validate scene/revisions -> record -> submit/wait by caller -> Finalize(ticket)
                                  \-> failed/uncertain work -> Abandon(ticket) -> reset next recording
```

Only one submission may be pending. Changes to the output storage revision, external input revision, pipeline or
descriptor handles, scene data, sampling policy, spatial/temporal mode or extent force a reset. A caller may opt
into bounded camera-only continuation after supplying matching previous-camera data. It must increment revisions
when referenced GPU inputs or allocations change in ways not captured by the scene view. `Abandon` cannot cancel
GPU work; the caller must quiesce the queue or use disjoint resources before reuse.

## Tests

| Check | Evidence |
| --- | --- |
| Direct versus sequenced Vulkan recording | 4 offscreen frames, exact RGBA, 16 history slabs and 24 counters. |
| Subpixel camera motion | 2 approved camera-only continuations; temporal, bank and variance histories exercised. |
| Simulated interruption | Abandoned before submission; next result matched an explicit fresh reset. |
| Storage revision | Revision change reset history; result matched an explicit fresh reset. |
| Host lifecycle | Pending/wrong ticket, policy/input/pipeline changes, invalid ranges and camera cuts. |
| Native tests | 10/10 Release and 10/10 host ASan/UBSan CTests. |

The Release comparison was rerun after restoring pinned dependencies into empty ignored directories. The Vulkan
fixture uses `32x32` diffuse triangles with seed history, temporal/spatial replay and SVGF. The simulated
interruption is an exhibit-side exception before `vkQueueSubmit`, not a physical device-loss
test. Shader and driver code are not sanitizer-instrumented. [Release log](Results/ReplaySubmissionSequence-release.txt)
and [sanitizer log](Results/ReplaySubmissionSequence-sanitized.txt) retain the details.

## Reproduce

```bash
python3 Exhibits/LightCompute/Code/Prepare.py
bash Exhibits/LightCompute/RunReplaySubmissionSequence.sh
```

The run requires CMake, Ninja, a C++20 compiler and the pinned Linux Slang/SwiftShader dependencies. The log's
source hashes describe the tested snapshot. The production editor still lacks scene-to-GPU export, resource
creation, image presentation and end-to-end
physical-GPU and Windows verification.
