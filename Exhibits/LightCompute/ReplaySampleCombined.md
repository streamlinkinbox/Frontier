# Subpixel seed retracing combined with reservoir reuse

This extends the default-off [subpixel seed-bank mode](ReplaySampleHistory.md). Previously that mode was
mutually exclusive with temporal and spatial reservoir reuse; it now combines with them. This is still
software ray tracing on bounded fixtures, not hardware RT, non-ray-traced GI, completed ReSTIR GI, an
editor viewport or general animated reconstruction.

## Contract

| Requirement | Scope |
|---|---|
| Retained candidates | Up to three gathered, previously banked unused-draw seeds are retraced at the current surface inside `GenerateReplayReservoirs`, after the temporal candidate (only the newest enters when temporal reuse is active — see the Seed bank row). Each enters the reservoir with its current-surface target and count one, exactly like an extra fresh candidate. |
| Equal-seed handling | An equal temporal/banked seed remains a separate proposal with its own weight and count; its current path evaluation can be cached. The later [transmission audit](ReplayTransmission.md) verified 64/64 injected equal-seed pixels. |
| Output | `ResolveReplayReservoirs` keeps its reservoir estimator (`Chosen * Sum / (Count * ChosenTarget)`); the sample-history finalize stage no longer overwrites the output in combined mode. |
| Seed bank | Finalize only shifts the bank and stores the generator stream's next unused draw (after the generation draws) — never a candidate that entered the reservoir, nor the selected seed. Generation enters all banked seeds, or only the newest when temporal reuse is active. See [the correlation study](ReplaySeedCorrelation.md): the original fresh-draw banking measured −25.6% stationary bias in combination with temporal reuse. |
| Ordering | The recorder now dispatches primary, gather, generate, resolve, finalize in sample-history mode, because generation consumes the gathered bank. Legacy no-reuse mode keeps its arithmetic-mean output and prior numeric behaviour. |
| Exclusions | Still primary, untextured diffuse, one initial fresh candidate; no colour reconstruction, rough or textured mode, or audit double-count. |

## Verification

| Check | Result |
|---|---|
| Host recorder/domain CTests, release | ✔️ 5/5 (`PathReplayVulkanValidation` now accepts temporal, spatial and both combined, and checks the new dispatch order) |
| Host recorder/domain CTests, ASan/UBSan | ✔️ 5/5 |
| Slang compile to SPIR-V (Generate, Resolve, Gather, Finalize, Primary) | ✔️ Compiled with the Slang 2026.12 compiler library in the sandbox; the retained run script uses Slang 2026.14.1 |
| SwiftShader GPU validation (`ValidateReplaySampleCombined`) | 🔬 An earlier software-Vulkan run is described in [the spatial-disk report](ReplaySpatialDisk.md); this historical row was originally host-only. The combined seed-bank suite has not been rerun here against every later shader change, and finite host correlation checks do not settle general statistical questions. |
| Physical GPU / Windows / editor | 🔴 Not run |

The GPU check warms both modes through three stationary frames, then compares a half-pixel transition:
pixels retaining no seed must match reservoir-only output bit-for-bit, accepted pixels must shift the
bank around the fresh seed, the accepted count must match counter 23, and more than 50 accepted pixels
must differ from reservoir-only output.

## Reproduce

```sh
bash Exhibits/LightCompute/RunReplaySampleHistory.sh
SANITIZE=1 bash Exhibits/LightCompute/RunReplaySampleHistory.sh
```

Host-only evidence from this sandbox is retained in
[the combined host results](Results/ReplaySampleCombined-host.txt). The [seed-correlation study](ReplaySeedCorrelation.md)
addresses one specific model, not all correlations; later [visibility holdouts](ReplayMaterialGuardCandidate.md)
found additional unresolved combined-reuse failures. No equal-time speedup or real-time claim is made.
