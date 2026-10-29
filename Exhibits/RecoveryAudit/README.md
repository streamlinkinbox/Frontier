# Project recovery and loss audit

The GitHub project is restored. The recent native Vulkan/Slang ReSTIR integration was not found;
this is a substantial renderer-work loss, not loss of the entire editor or its original rendering references.

## Restored revision

- Repository: `SultanAladin/Frontier`.
- Branch: `arena/5e581235-frontier` at `063e5917ea60e74a46ea06e4e88f40a7d9419bb5`.
- Restored on this session's existing `arena/bd3633ff-frontier`; no branch switch or remote write.
- 3,585 tracked files, about 221 MiB of uncompressed file contents.
- Before adding this audit, the restored index tree exactly matched upstream tree
  `050f2b07953313a5b126fbc4eef9aa40d3612037` (`git write-tree` comparison).
- This revision only reverts the temporary CI trigger from successful Windows build revision `892d3453...`;
  its application code is identical to that build. No new Windows build was run here.
- Windows build: <https://github.com/SultanAladin/Frontier/actions/runs/40110183702>.
- That run reports success and retains the Windows package and diagnostics artifacts.
- Merged PR15 commit `e53d6d17...` is an ancestor of the restored revision.

## What survived versus what is missing

| Area | Finding |
|---|---|
| Editor, Texture Workspace, UI and Windows build scripts | Restored from GitHub |
| Original ControlVehicle body/rim/tyre archives | Restored |
| Original vehicle rendering reference | 52 WGSL shader files restored |
| Earlier reusable rendering code | 44 files restored under `Frontier/Engine/Rendering` |
| CPU reservoirs, filtering, SDF/cards and atmosphere/cloud models | Present; focused tests rerun below |
| New Vulkan/Slang ReSTIR exhibit | `Exhibits/ReSTIRPort` not found |
| Native lit/moving vehicle integration | `Exhibits/ControlVehicleRender` not found |
| New cluster-surface integration | `Exhibits/ClusterSurface` not found |
| Native triangle-BVH ray route and its exhibit | `Exhibits/TriangleRays` not found |
| BLAS/TLAS, inline queries and automatic route exhibit | `Exhibits/InlineRays` not found |
| Latest adapter-selection/lifecycle tooling | Recent ordinal/UUID selection and device-sweep files not found |
| Recent proof images, buffers, manifests and run logs | The five missing exhibit directories are not recovered |
| Environment/Moon requirements | Reconstructed from conversation; not recovered original document bytes |

These findings distinguish the earlier CPU/reference implementations from the missing integrated Vulkan/Slang code.
Similar older CPU modules are not a substitute for the missing 48-view native vehicle renderer.
The existing approximate lunar-phase calculation is not proof of a rendered Moon.

## Search scope and limits

`BranchAudit.json` records **106 current branch heads / 104 distinct repository revisions** across
`SultanAladin/Frontier` and `eosclient0001-rgb/Frontier`. All recursive tree responses were complete.
The search used 18 filename anchors from the retained conversation: five were found and thirteen were not.
This is a selected-anchor inventory, **not a percentage of source code recovered**.

The recent five exhibit-directory paths and selected Vulkan paths also have no log entries in the restored
upstream ancestry or the older pinned `b8387897...` ancestry checked locally. This is not an exhaustive inspection
of every historical commit on every branch, deleted refs, private forks or external workspace snapshots.
The empty original local checkout had no recoverable dangling Git objects or implementation commits.

The missing implementation might still exist in an Arena snapshot, another machine, or an unexamined historical
revision. Therefore “not found/recovered” is deliberate; irreversible deletion has not been established.

## Verification rerun after restoration

| Focused native C++ validation | Result |
|---|---|
| Illumination chain: reservoirs, reuse, filtering and scheduling | 55/55 |
| Indirect illumination: analytic/baked SDF and card radiosity | 60/60 |
| Environment: celestial, atmosphere, cloud and related calculations | 73/73 |
| **Total** | **188/188** |

Built locally using C++17, `-O2 -Wall -Wextra -Werror -pthread` with the available GCC toolchain.
`Validation.json` contains actual compilation commands and outputs; `ValidationResults.txt` is the readable log.
These are CPU tests, not Vulkan execution, physical-GPU testing, Windows runtime or a newly rendered vehicle capture.
Existing capture images in the recovered project are upstream evidence, not screenshots produced by this audit.

Reproduce the read-only GitHub inventory and focused local tests from the repository root:

```bash
mkdir -p _ClaudeScratch/recovery
python3 Exhibits/RecoveryAudit/Code/AuditBranches.py
python3 Exhibits/RecoveryAudit/Code/ValidateRecovered.py
```

## How much was lost?

**Most of the recent native Vulkan integration and its reproducibility/evidence package is currently unrecovered.**
The original project, source algorithms, geometry and WGSL reference remain available, so rebuilding would not
start from zero. However, the former approximately 60% integrated-renderer completion estimate cannot be treated
as verified progress on this restored tree.

There is no defensible exact percentage, line count or time estimate: the lost 121-source/230-product manifest
reported in the conversation is itself missing, and cannot provide a full byte-for-byte comparison inventory.
The concrete lower-bound finding is five missing recent exhibit directories plus the selected missing engine files.

## Checkpoint status

Restored source and this audit were committed and pushed after explicit user approval in `12344d0b`,
to `eosclient0001-rgb/Frontier`, branch `arena/bd3633ff-frontier`. The `[skip ci]` marker avoids an unrequested
Actions run. The original source also remains available at the upstream revision above.

The later `Exhibits/VehicleReference` rerender and shadow repair are new work, not recovered bytes from the
missing Vulkan implementation. The missing-file findings above describe the recovery inspection before that work.
