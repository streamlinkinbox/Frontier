# ReSTIR first-launch stall / fast second launch

## Evidence from the supplied Windows runs

GPU: GTX 1650 SUPER; NVIDIA 576.40. Application-requested validation OFF. First PID 9880; second PID 8424.

| Observation | First run | Second run |
|---|---:|---:|
| Scene decode (`phase_ms`) | 10.419 s | 18.853 s |
| ReSTIR shader-module creation | 0.03 s, success | <0.01 s rounded, success |
| ReSTIR compute-pipeline creation | still pending at 316.04 s | 0.45 s, success |
| Application cache input | compatible, 1,482,204 bytes | compatible, 1,482,204 bytes |
| First present returned | not shown | 32.024 s since logger origin |

This isolates the long-running operation to `vkCreateComputePipelines` for ReSTIR, not file loading, the heartbeat loop, texture decode or the BVH worker. The wrapper invokes the Vulkan function once on the original calling thread. Its independent observer prints WAIT until the function returns; it does not retry or initiate another compilation every five seconds.

The second run is faster at this pipeline call by more than 700x relative to the first run's observed lower bound. It is not instantaneous overall: first presentation returns at about 32 seconds, with scene decoding accounting for almost 19 seconds.

At the first call's start, process resident memory was 487.41 MiB and private commit 1,003.21 MiB. At 316 seconds: 739.02 MiB resident, OS peak resident 1,005.36 MiB, private commit 1,263.54 MiB. This is approximately +252 MiB resident / +260 MiB committed over the call-start sample. These are process-wide samples, not exclusive measurements of driver allocations. Temporary rises and falls do not establish a leak.

## What is and is not established

- **Established:** the application is waiting for ReSTIR pipeline creation to return; shader-module creation succeeds quickly.
- **Strong candidate:** expensive/pathological CPU-side driver compilation or optimization, with a subsequent run benefiting from driver-cached work. A driver/cache synchronization problem is also possible. Memory pressure/paging can amplify either.
- **Not established:** the precise internal driver function, a specific NVIDIA bug, actual application-cache hits, or why terminating/restarting changes behavior.
- Identical cache file sizes do **not** establish identical file contents. Neither supplied run recorded a fingerprint.
- If the first process was force-terminated before pipeline creation returned, Frontier's old shutdown-only save path could not checkpoint that newly prepared pipeline. NVIDIA's independent cache could nevertheless have changed. If the first process exited normally or printed later records not supplied, the conclusion differs.
- The unchanged path rules out a working-directory change between these two supplied runs. Application validation is OFF; external layer injection is not audited by that setting.

## Changes added for investigation and recovery

1. Log deterministic FNV-1a-64 fingerprints and byte sizes of the ReSTIR SPIR-V and the application cache. These are diagnostic comparisons, not cryptographic integrity checks.
2. Checkpoint the application pipeline cache after successful ReSTIR, denoiser and luminance pipeline creation, as well as shutdown. Export failures are logged. The cache is never exported concurrently with a pending pipeline call.
3. Write a unique temporary file and replace the old cache only after the write/close succeeds. Failed replacement preserves the previous file; there is no delete-before-rename gap. The Windows path uses `MoveFileExW` with replace/write-through flags. This is not a guarantee against power loss or a fix for an in-driver hang.
4. Bound application-cache loads/exports to 256 MiB and check header length against the bytes available.
5. Provide two **opt-in** test modes; the normal pipeline remains optimized by default. Modes use separate application-cache files so a test does not overwrite `ShaderCache.bin`.

Early cache saves cannot save a pipeline whose creation has not returned. No unsafe timeout, thread termination or same-device concurrent retry is added. The application still creates this pipeline synchronously.

## How to test on the affected machine

Rebuild once using the current code. Run tests sequentially, not with two Frontier processes competing for memory. Keep scene/settings fixed and preserve the normal cache. Save work and close unneeded memory-heavy applications if possible; do not disable the Windows pagefile. Do not clear NVIDIA's cache as the first troubleshooting step.

From the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Projects/Project-Zero/Build/ToolchainSequence.ps1 -Rebuild

# Normal mode. Keep its input fingerprint and pipeline elapsed time.
Remove-Item Env:FRONTIER_PIPELINE_TEST -ErrorAction SilentlyContinue
.\Build\Project-Zero.exe 2>&1 | Tee-Object pipeline-default.log
```

Keep the SAME working directory for subsequent tests. The earlier logs used the binary directory as working directory; root-launch tests therefore use a different cache location. To reuse the previously working cache, either launch all tests from that original directory, or copy its `ShaderCache.bin` to the root before the root-launch comparison. Do not compare runs from different directories as if their cache inputs were identical.

### Test A: request reduced driver optimization for ReSTIR

```powershell
$env:FRONTIER_PIPELINE_TEST = 'no-opt'
.\Build\Project-Zero.exe 2>&1 | Tee-Object pipeline-no-opt.log
Remove-Item Env:FRONTIER_PIPELINE_TEST
```

This sets Vulkan's `VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT` **only for ReSTIR** and uses `ShaderCache.no-opt.bin`. Shader source and rendering features are unchanged. The driver controls how it honors the flag; rendering may become substantially slower, so this is not the production default.

If this avoids a reproducible slow default path, optimization or variant-specific driver handling becomes a stronger candidate. It is not definitive proof: pipeline flags/cache identity differ and NVIDIA's cache may already be warm. A fast test when every normal launch is also fast does not isolate the original cold-start problem.

### Test B: skip Frontier's saved-cache input

```powershell
$env:FRONTIER_PIPELINE_TEST = 'empty-cache'
.\Build\Project-Zero.exe 2>&1 | Tee-Object pipeline-empty-cache.log
Remove-Item Env:FRONTIER_PIPELINE_TEST
```

This creates an empty **application** cache each time, leaves normal shader optimization enabled, and writes checkpoints to `ShaderCache.isolated.bin`. It does **not** disable NVIDIA's own shader cache; a fast result must not be called a cold compile.

If normal mode is repeatedly slow while this mode is repeatedly fast under otherwise matched conditions, application-cache interaction warrants investigation. Do not delete the original cache: preserve it for diagnosis.

For either test, retain the complete logs including shader/cache fingerprints, chosen test mode, cache-checkpoint records, WAIT records and final VkResult. If changing the NVIDIA driver is tried later, record the new version and treat it as a separate experiment; the driver's cache compatibility may change.

## When logs are insufficient

If pipeline creation remains pending for minutes, capture a Windows process dump for the logged PID (Task Manager Details → Create dump file), or inspect its threads with a debugger. Stack samples are needed to distinguish compiler work from waits/locks/page faults inside a Vulkan layer or driver. A dump can contain private process data: do not post it publicly. Sanitized stack traces are preferable for sharing.

CPU tests cover option defaults/isolation, fingerprint determinism and equal-size differences, atomic file replacement/failure preservation, heartbeat lifetime and one-call execution. The modified Vulkan source syntax-checks against real headers. **These do not reproduce NVIDIA 576.40, prove a compiler optimization improvement, or establish that the first-run stall is fixed.** The Windows API replacement path and diagnostic pipeline modes require testing on the affected machine.

Reproduce the CPU checks with `SANITIZE=1 bash Tools/Build/CheckPipelineCache.sh`, `SANITIZE=1 bash Tools/Build/CheckDriverProgress.sh`, and `python Tools/Tests/TestStartupReport.py`. Both sanitizer gates and all 12 startup-report tests passed in this environment.
