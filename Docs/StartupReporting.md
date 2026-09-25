# Startup timing and RAM: overlapping workers are not additive

`TextureDecode`, `CwbvhBuild` and `VulkanBringUp` intentionally overlap. Do not serialize these jobs to make a timing total look simpler. Their individual spans are wall durations, not exclusive CPU time.

## Generate an overlap-aware report

From the repository root, using the CSV path printed by the application:

```powershell
python Tools/Build/ReportStartup.py "PATH\TO\startup-1790366346292.csv" --output Build/Diagnostics/startup-report.md
```

The path is relative to the application's working directory when it wrote the file, not necessarily the source checkout. The tool accepts the existing seven-column CSV and the new CSV with an appended process ID.

The report separates:

- elapsed timestamps to `FrameLoopReady` and `FirstPresentReturned`, when available;
- the union of completed phase intervals (overlap counted once);
- the sum of individual job spans, explicitly **not** startup time or CPU time;
- incomplete phases, which are flagged rather than assigned an invented end;
- process resident memory, OS peak resident memory and private committed memory.

It uses begin/end timestamps on one monotonic clock. Small differences from the supplied `phase_ms` values reflect where timers and log calls occur. Completed-phase union excludes uninstrumented gaps and unfinished phases: it is not a replacement for elapsed startup-to-first-present time.

### Supplied log example

Texture decode occupies approximately 87.267–92.408 seconds. BVH construction occupies 87.280–91.065 seconds, entirely inside that texture interval. Together they cover approximately **5.141 seconds of wall time**, not 8.926 seconds. Vulkan bring-up begins at 87.272 seconds but has no end in the supplied excerpt, so its completed duration cannot be calculated.

Scene decode takes approximately 84 seconds and remains a separate issue. Patch baking currently runs synchronously during mesh registration, even with the patch debug view off; this is a suspect for cold-cache startup cost, not a measured attribution of all 84 seconds. Compare a second launch with the cache retained. This reporting change does not optimize the baker or reduce allocated rendering memory.

## Why Task Manager can show less than an earlier 4 GB reading

The supplied screenshot shows **1,634.7 MB for Project-Zero (2)**, not exactly 1 GB. This is an application group containing two processes. The machine is at **90% total RAM usage**.

Possible explanations, not proven diagnoses:

1. A startup high-water mark was compared with a later working set after temporary import, driver compilation or staging allocations were released.
2. Under memory pressure Windows trimmed/paged the working set. Resident memory falls, while private committed memory can remain high.
3. The runs used different scene, resolution, texture limits or rendering settings. GPU reservoir allocation scales with pixel count, but that must not be counted directly as process resident RAM.
4. Driver and application caches changed which startup operations ran. A warm cache can avoid work; it does not prove a permanent memory saving.

The previous log's roughly 389 MiB resident sample was taken at texture decode completion. The later screenshot is not the same event or necessarily the same accounting category. Neither it nor a single earlier 4 GB reading establishes a leak or a leak fix.

### Capture comparable measurements

New logs append `process_id` to CSV rows and `pid=` to console samples. In Task Manager **Details**, select that PID instead of comparing the collapsed two-process group. Inspect working set, peak working set, private working set and commit size separately. GPU dedicated/shared memory are separate measurements.

The existing startup checkpoints remain. After presentation, the render loop now records `RuntimeMemory` about every ten seconds while frames return. It does not sample a hung initialization or a blocked presentation call. The logger records process-wide memory; a BVH-labelled row does not mean all memory in the row belongs to BVH construction.

- **Resident falls, commit stays high:** consistent with trimming/paging or a change in residency; not proof of deallocation.
- **Resident and commit both fall after startup:** consistent with released allocations; inspect matching phase/event samples.
- **Commit keeps growing during repeated steady-state samples:** investigate retention/leaks, but allocator growth and changing workloads still need to be ruled out.

A 0% reading on Task Manager's displayed 3D engine does not establish that the renderer never uses the GPU. Frontier's software-BVH path runs ray traversal in GPU compute shaders; “software BVH” does not mean CPU-only rendering. If startup is still at shader/pipeline initialization, GPU rendering may not have begun yet.

## Verification

```sh
python Tools/Tests/TestStartupReport.py
```

12 tests exercise overlapping/nested/disjoint intervals, repeated phases, missing endpoints, incomplete startup, old CSV compatibility, process IDs and unavailable memory counters. A native Linux logger smoke test also verifies the appended CSV field. Windows Task Manager counter comparison and the full renderer remain unexecuted here for this change.
