# 0005 — Real Cloud Shadows (slab-spanning transmittance)

Moon CLOSED/parked. 0004 DEAD (fake lens-flare-style noise removed).
0005 computes REAL cloud shadows: the sun ray from each surface point is
marched through the SAME `CloudDensityCore` the sky uses (same extinction
0.01/m, same `exp(-OD)` law, same OD>4 early-out). No noise textures.

## Files (16 — copy over your tree, paths mirror the transplant layout)

```
Engine/DisplayPresentation/VolumetricMedia.h      REFACTOR: CloudDensity split
                                                  into CloudDensityCore +
                                                  Anvil wrapper (bit-identical),
                                                  + SunTransmittanceAt wrapper
Engine/DisplayPresentation/CloudShadowStaging.h   NEW: frozen diorama/panel
                                                  staging (single-sourced FIN3)
Engine/DisplayPresentation/SkyConstantRecord.h    +4 shadow lanes (packer)
Engine/DisplayPresentation/PostConstantRecord.h   +7 shadow lanes (packer)
Engine/Shaders/CloudShadow.slang                  NEW: GPU transcription of
                                                  SunTransmittanceAt + Core
Engine/Shaders/SkyRecords.slang                   shadow-lane comments
Engine/Shaders/PostRecords.slang                  shadow-lane comments
Engine/Shaders/ReSTIRViewport.slang               include + xT at both sun
                                                  sites (initial + reuse)
Projects/Project-Zero/Source/SkyFogIntegrator.h  Assign/Query decl
Projects/Project-Zero/Source/SkyFogIntegrator.cpp Assign + Query + wind
Projects/Project-Zero/Source/RendererHost.h       shadow-signature defaults
Projects/Project-Zero/Source/RendererHost.cpp     CloudShadow init + 2 xT
Projects/Project-Zero/Source/CpuReferenceMain.cpp CLI flags + FIN3 defaults
Projects/Project-Zero/Source/CelestialSequence.h  staging holder + setters
Projects/Project-Zero/Source/CelestialSequence.cpp FoldShadowDrift + packs
Projects/Project-Zero/Source/GameExecution.cpp    level-conditional hook
                                                  (Showcase->diorama FIN3,
                                                  else panel-km)
```

## Diorama staging (FROZEN inputs — FIN3)

Cumulus, base 250 m, thick 200 m, cover 0.55, density 4.0, scale 0.05,
anvil 0.5, ceiling 14000 m, wind 7 m/s @ 250 deg, t = 40 s.
Single-sourced in `CloudShadowStaging.h`, assigned once by `GameExecution`
(`IsShowcaseLevel` -> `AssignCloudShadowStaging`). `CelestialSequence`
stays level-blind.

## GPU design (frozen)

- 11-float payload rides spare record lanes (no record growth):
  SKY Mie.w=Base[m], Ozone.w=Thick[m], Planet.w=Scale, Twilight.w=Enabled01;
  POST Spare0[1]=Coverage, [2]=Density, [3]=Type, Spare1=DriftX/DriftY[m],
  Anvil, Ceiling[m].
- GPU time is FROZEN (40 s diorama / 0 s panel) + `AssignCloudShadowTime`
  scrub setter. No free clock (memcmp sky-change would restart accumulation).
- Shadow is a receiver property: xT at the current hit position in BOTH
  the initial-sampling and temporal-reuse paths (reuse-safe). Parity by
  instant: `--cloudtime 40` == GPU frozen 40 s.

## Verify (CPU reference, g++ sandbox stand-in for your GPU build)

```
cd Projects/Project-Zero && make -s
./bin/Project-Zero-CpuReference --sun 10.5 --cloudshadow 0  # OFF
./bin/Project-Zero-CpuReference --sun 10.5                  # FIN3 default
./bin/Project-Zero-CpuReference --sun 10.5 --cloudcover 1.0 # overcast
sha256sum Diagnostics/ProjectZero_Showcase.ppm
```

Expected (640x480 PPM, Showcase path, sun 10.5):

```
OFF  260aa1f6d161c121d03821081814d6fb879080f75a8bd75029c403d9a39ce85f
FIN3 8688374c66b7f0becb3d436cfa561aad03d85b6d686387d10928d907102bada4
OVC  3b488241b95b91a34ae87d40e3e1f2266635b64e408ca83f5ade922e7bf7f553
```

Proofs: refactor bit-identical (4205-sample cmp clean), wrapper exact
(13/13 points), wiring alive (OFF/=FIN3/=OVC), determinism exact
(repeat run identical), cost +19.7% CPU (+921 ms @640x480).
Proof frames: `Projects/Project-Zero/Diagnostics/Proof/0005_{OFF,FIN3,OVC}.png`.
GPU `.slang` is blind-written (no-GPU sandbox) and review-verified
(14-point manual pass); it predicts what your Vulkan build computes.
