# ProjectZero_TelemetryReport — Telemetry Log

| Timestamp | Severity | Category | Record Description |
|:---|:---:|:---|:---|
| 2026-09-20 19:20:05.889 | INFO | Bootstrap | Project-Zero windowed ReSTIR renderer starting. |
| 2026-09-20 19:20:07.132 | INFO | Scene | Showcase: 229506 triangles, 499 instances, 2045 clusters, 241 materials, 3368 luminaires, bounds [-40.00 -40.00 0.00]..[40.00 40.00 6.00] m |
| 2026-09-20 19:20:08.018 | INFO | Textures | Textures: 6 resident (0 placeholder), 64.0 MB with mips, decoded in 884 ms |
| 2026-09-20 19:20:08.019 | INFO | Materials | Materials: 241 descriptors -> 241 records, 241 slabs (limit 1, 0 folded), 499 placements, 0 cameras, 0 punctual lights |
| 2026-09-20 19:20:08.047 | INFO | Interface | Panel light Low: rgb (0.000 0.008 0.042) from 4 figures, 5% coverage, 3370 luminaires now. |
| 2026-09-20 19:20:08.736 | INFO | Traversal | CWBVH: 229508 triangles → 39627 nodes, 3095.9 KB nodes + 16137.3 KB leaves (85.8 B/tri), SAH 13.84, built in 685.6 ms (spatial splits) |
| 2026-09-20 19:23:07.624 | INFO | Bootstrap | Window and Vulkan swapchain ready. |
| 2026-09-20 19:23:10.582 | INFO | Traversal | Two-level: 500 instances -> 500 BLASes over 229508 triangles, top level 598 nodes, shared blobs 2631.4 KB + 10758.2 KB, built in 821.8 ms |
| 2026-09-20 19:23:10.761 | INFO | Moons | 6 textures resident, moon slots 0..5. |
| 2026-09-20 19:23:10.764 | INFO | Stars | 9683 stars in 1024 cells uploaded to binding 23. |
| 2026-09-20 19:23:14.883 | INFO | Bootstrap | Entering render loop. |
| 2026-09-20 19:23:14.970 | INFO | Interface | Director ready: TAB switches screens. The card carries 2 converted vector segments. |
| 2026-09-20 19:23:15.395 | INFO | Audio | Panel bound to audio: drag the progress bar to change the engine note. |
| 2026-09-20 19:23:15.396 | INFO | Interface | Spatial interface ready: 14 figures, depth test off. |
| 2026-09-20 19:23:15.464 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle; inline shadow rays are counted inside GpuReSTIRMs, so GpuShadowMs=0 is expected). Opaque rays use the bounded closest-hit path and spatial winners are revalidated at the current pixel. Tier stage if GI is switched off: Hard @ 1024 px, 1 taps. |
| 2026-09-20 19:23:21.448 | INFO | Performance | CPU 54.44 ms/frame (18.4 fps, worst 100.00 ms over 93 frames), RSS 795 MiB |
| 2026-09-20 19:23:21.449 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 54.4393 [ms] |
| 2026-09-20 19:23:21.451 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 19:23:21.452 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 18.3691 [fps] |
| 2026-09-20 19:23:21.454 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 93 [count] |
| 2026-09-20 19:23:21.456 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 795.113 [MiB] |
| 2026-09-20 19:23:21.457 | INFO | GpuTiming | GPU 48.96 ms total | cull 0.08 · raster 8.19 · HiZ 0.04 · resolve 0.89 · ReSTIR 39.75 · shadow 0.00 · post 0.84 · sky 0.00 · volume 0.00 |
| 2026-09-20 19:23:21.464 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 48.9564 [ms] |
| 2026-09-20 19:23:21.466 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.081088 [ms] |
| 2026-09-20 19:23:21.469 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 8.19248 [ms] |
| 2026-09-20 19:23:21.470 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.043392 [ms] |
| 2026-09-20 19:23:21.475 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.888832 [ms] |
| 2026-09-20 19:23:21.480 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 39.7506 [ms] |
| 2026-09-20 19:23:21.482 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 19:23:21.484 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.842304 [ms] |
| 2026-09-20 19:23:21.486 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 19:23:21.493 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 19:23:21.495 | INFO | Visibility | Clusters 2046 tested -> 1995 frustum, 1995 cone, 1995 visible | draws 1995+0, 224004 triangles |
| 2026-09-20 19:23:21.497 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 19:23:21.498 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1995 [count] |
| 2026-09-20 19:23:21.499 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1995 [count] |
| 2026-09-20 19:23:21.501 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1995 [count] |
| 2026-09-20 19:23:21.502 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 224004 [count] |
| 2026-09-20 19:23:21.504 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1995 [count] |
| 2026-09-20 19:23:21.506 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 81% of GPU frame |
| 2026-09-20 19:23:21.514 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-20 19:23:21.516 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 19:23:21.518 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 19:23:21.520 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 19:23:21.522 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 19:23:21.529 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-20 19:23:21.531 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 81.1959 [percent] |
| 2026-09-20 19:23:26.751 | INFO | Performance | CPU 60.45 ms/frame (17.0 fps, worst 100.00 ms over 84 frames), RSS 853 MiB |
| 2026-09-20 19:23:26.753 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 60.4479 [ms] |
| 2026-09-20 19:23:26.756 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 19:23:26.758 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 17.0004 [fps] |
| 2026-09-20 19:23:26.762 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 84 [count] |
| 2026-09-20 19:23:26.764 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 853.426 [MiB] |
| 2026-09-20 19:23:26.767 | INFO | GpuTiming | GPU 84.92 ms total | cull 0.09 · raster 11.41 · HiZ 0.05 · resolve 0.98 · ReSTIR 72.39 · shadow 0.00 · post 0.87 · sky 0.00 · volume 0.00 |
| 2026-09-20 19:23:26.771 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 84.9163 [ms] |
| 2026-09-20 19:23:26.773 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.093856 [ms] |
| 2026-09-20 19:23:26.776 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 11.4101 [ms] |
| 2026-09-20 19:23:26.782 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.045056 [ms] |
| 2026-09-20 19:23:26.784 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.978624 [ms] |
| 2026-09-20 19:23:26.790 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 72.3886 [ms] |
| 2026-09-20 19:23:26.792 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 19:23:26.800 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.870461 [ms] |
| 2026-09-20 19:23:26.802 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 19:23:26.806 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 19:23:26.808 | INFO | Visibility | Clusters 2046 tested -> 1954 frustum, 1954 cone, 1954 visible | draws 1944+10, 220042 triangles |
| 2026-09-20 19:23:26.814 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 19:23:26.816 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1954 [count] |
| 2026-09-20 19:23:26.819 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1954 [count] |
| 2026-09-20 19:23:26.821 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1954 [count] |
| 2026-09-20 19:23:26.824 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 220042 [count] |
| 2026-09-20 19:23:26.829 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1954 [count] |
| 2026-09-20 19:23:26.831 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 85% of GPU frame |
| 2026-09-20 19:23:26.836 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-20 19:23:26.843 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 19:23:26.845 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 19:23:26.847 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 19:23:26.850 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 19:23:26.851 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-20 19:23:26.853 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 85.247 [percent] |
| 2026-09-20 19:23:32.838 | INFO | Performance | CPU 73.98 ms/frame (14.3 fps, worst 100.00 ms over 68 frames), RSS 875 MiB |
| 2026-09-20 19:23:32.839 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 73.9766 [ms] |
| 2026-09-20 19:23:32.841 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 19:23:32.843 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 14.334 [fps] |
| 2026-09-20 19:23:32.845 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 68 [count] |
| 2026-09-20 19:23:32.847 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 874.852 [MiB] |
| 2026-09-20 19:23:32.849 | INFO | GpuTiming | GPU 79.77 ms total | cull 0.10 · raster 7.84 · HiZ 0.05 · resolve 0.98 · ReSTIR 70.80 · shadow 0.00 · post 1.29 · sky 0.00 · volume 0.00 |
| 2026-09-20 19:23:32.856 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 79.769 [ms] |
| 2026-09-20 19:23:32.859 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.097984 [ms] |
| 2026-09-20 19:23:32.861 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 7.8441 [ms] |
| 2026-09-20 19:23:32.863 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.045248 [ms] |
| 2026-09-20 19:23:32.872 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.976896 [ms] |
| 2026-09-20 19:23:32.874 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 70.8047 [ms] |
| 2026-09-20 19:23:32.876 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 19:23:32.878 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.29306 [ms] |
| 2026-09-20 19:23:32.886 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 19:23:32.888 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 19:23:32.893 | INFO | Visibility | Clusters 2046 tested -> 1792 frustum, 1792 cone, 1792 visible | draws 1792+0, 202392 triangles |
| 2026-09-20 19:23:32.895 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 19:23:32.901 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1792 [count] |
| 2026-09-20 19:23:32.903 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1792 [count] |
| 2026-09-20 19:23:32.906 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1792 [count] |
| 2026-09-20 19:23:32.908 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 202392 [count] |
| 2026-09-20 19:23:32.910 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1792 [count] |
| 2026-09-20 19:23:32.913 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 89% of GPU frame |
| 2026-09-20 19:23:32.921 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-20 19:23:32.924 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 19:23:32.926 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 19:23:32.929 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 19:23:32.933 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 19:23:32.936 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-20 19:23:32.938 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 88.7623 [percent] |
| 2026-09-20 19:23:38.410 | INFO | Performance | CPU 62.63 ms/frame (15.2 fps, worst 100.00 ms over 81 frames), RSS 947 MiB |
| 2026-09-20 19:23:38.411 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 62.6341 [ms] |
| 2026-09-20 19:23:38.414 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 19:23:38.415 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 15.1808 [fps] |
| 2026-09-20 19:23:38.418 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 81 [count] |
| 2026-09-20 19:23:38.423 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 946.512 [MiB] |
| 2026-09-20 19:23:38.425 | INFO | GpuTiming | GPU 76.85 ms total | cull 0.08 · raster 5.93 · HiZ 0.05 · resolve 0.87 · ReSTIR 69.93 · shadow 0.00 · post 0.87 · sky 0.00 · volume 0.00 |
| 2026-09-20 19:23:38.429 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 76.8501 [ms] |
| 2026-09-20 19:23:38.431 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.077344 [ms] |
| 2026-09-20 19:23:38.433 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 5.92541 [ms] |
| 2026-09-20 19:23:38.441 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.046112 [ms] |
| 2026-09-20 19:23:38.443 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.8704 [ms] |
| 2026-09-20 19:23:38.446 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 69.9308 [ms] |
| 2026-09-20 19:23:38.448 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 19:23:38.450 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.872612 [ms] |
| 2026-09-20 19:23:38.454 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 19:23:38.456 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 19:23:38.458 | INFO | Visibility | Clusters 2046 tested -> 1436 frustum, 1436 cone, 1436 visible | draws 1427+9, 161080 triangles |
| 2026-09-20 19:23:38.460 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 19:23:38.463 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1436 [count] |
| 2026-09-20 19:23:38.465 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1436 [count] |
| 2026-09-20 19:23:38.471 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1436 [count] |
| 2026-09-20 19:23:38.478 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 161080 [count] |
| 2026-09-20 19:23:38.481 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1436 [count] |
| 2026-09-20 19:23:38.485 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 91% of GPU frame |
| 2026-09-20 19:23:38.489 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-20 19:23:38.492 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 19:23:38.494 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 19:23:38.497 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 19:23:38.504 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 19:23:38.508 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-20 19:23:38.510 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 90.9964 [percent] |
| 2026-09-20 19:23:44.223 | INFO | Performance | CPU 75.41 ms/frame (14.6 fps, worst 100.00 ms over 67 frames), RSS 799 MiB |
| 2026-09-20 19:23:44.225 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 75.4137 [ms] |
| 2026-09-20 19:23:44.228 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 19:23:44.230 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 14.6326 [fps] |
| 2026-09-20 19:23:44.233 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 67 [count] |
| 2026-09-20 19:23:44.236 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 798.793 [MiB] |
| 2026-09-20 19:23:44.239 | INFO | GpuTiming | GPU 80.16 ms total | cull 0.12 · raster 7.53 · HiZ 0.05 · resolve 1.16 · ReSTIR 71.30 · shadow 0.00 · post 0.84 · sky 0.00 · volume 0.00 |
| 2026-09-20 19:23:44.243 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 80.1565 [ms] |
| 2026-09-20 19:23:44.250 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.115968 [ms] |
| 2026-09-20 19:23:44.252 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 7.52822 [ms] |
| 2026-09-20 19:23:44.256 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.0464 [ms] |
| 2026-09-20 19:23:44.259 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.16454 [ms] |
| 2026-09-20 19:23:44.262 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 71.3014 [ms] |
| 2026-09-20 19:23:44.269 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 19:23:44.272 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.839554 [ms] |
| 2026-09-20 19:23:44.276 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 19:23:44.281 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 19:23:44.282 | INFO | Visibility | Clusters 2046 tested -> 1546 frustum, 1546 cone, 1546 visible | draws 1507+39, 173986 triangles |
| 2026-09-20 19:23:44.287 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 19:23:44.290 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1546 [count] |
| 2026-09-20 19:23:44.299 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1546 [count] |
| 2026-09-20 19:23:44.302 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1546 [count] |
| 2026-09-20 19:23:44.305 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 173986 [count] |
| 2026-09-20 19:23:44.308 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1546 [count] |
| 2026-09-20 19:23:44.317 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 89% of GPU frame |
| 2026-09-20 19:23:44.320 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-20 19:23:44.322 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 19:23:44.329 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 19:23:44.332 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 19:23:44.335 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 19:23:44.339 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-20 19:23:44.346 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 88.9527 [percent] |
| 2026-09-20 19:23:49.580 | INFO | Performance | CPU 63.57 ms/frame (14.7 fps, worst 100.00 ms over 79 frames), RSS 828 MiB |
| 2026-09-20 19:23:49.581 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 63.5658 [ms] |
| 2026-09-20 19:23:49.585 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 19:23:49.587 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 14.7488 [fps] |
| 2026-09-20 19:23:49.589 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 79 [count] |
| 2026-09-20 19:23:49.593 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 828.301 [MiB] |
| 2026-09-20 19:23:49.600 | INFO | GpuTiming | GPU 62.44 ms total | cull 0.09 · raster 9.32 · HiZ 0.05 · resolve 1.31 · ReSTIR 51.68 · shadow 0.00 · post 0.77 · sky 0.00 · volume 0.00 |
| 2026-09-20 19:23:49.604 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 62.4387 [ms] |
| 2026-09-20 19:23:49.607 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.086816 [ms] |
| 2026-09-20 19:23:49.614 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 9.31869 [ms] |
| 2026-09-20 19:23:49.616 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.045312 [ms] |
| 2026-09-20 19:23:49.621 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.30848 [ms] |
| 2026-09-20 19:23:49.624 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 51.6794 [ms] |
| 2026-09-20 19:23:49.631 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 19:23:49.633 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.765984 [ms] |
| 2026-09-20 19:23:49.635 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 19:23:49.638 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 19:23:49.640 | INFO | Visibility | Clusters 2046 tested -> 1934 frustum, 1934 cone, 1934 visible | draws 1934+0, 217384 triangles |
| 2026-09-20 19:23:49.646 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 19:23:49.649 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1934 [count] |
| 2026-09-20 19:23:49.651 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1934 [count] |
| 2026-09-20 19:23:49.653 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1934 [count] |
| 2026-09-20 19:23:49.655 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 217384 [count] |
| 2026-09-20 19:23:49.662 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1934 [count] |
| 2026-09-20 19:23:49.664 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 83% of GPU frame |
| 2026-09-20 19:23:49.669 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-20 19:23:49.671 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 19:23:49.677 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 19:23:49.679 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 19:23:49.683 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 19:23:49.685 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-20 19:23:49.688 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 82.7682 [percent] |
| 2026-09-20 19:23:54.792 | INFO | Performance | CPU 58.37 ms/frame (16.5 fps, worst 100.00 ms over 86 frames), RSS 904 MiB |
| 2026-09-20 19:23:54.793 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 58.3722 [ms] |
| 2026-09-20 19:23:54.795 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 19:23:54.796 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 16.4954 [fps] |
| 2026-09-20 19:23:54.799 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 86 [count] |
| 2026-09-20 19:23:54.801 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 903.809 [MiB] |
| 2026-09-20 19:23:54.804 | INFO | GpuTiming | GPU 51.84 ms total | cull 0.07 · raster 5.89 · HiZ 0.04 · resolve 0.78 · ReSTIR 45.06 · shadow 0.00 · post 0.70 · sky 0.00 · volume 0.00 |
| 2026-09-20 19:23:54.807 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 51.8444 [ms] |
| 2026-09-20 19:23:54.809 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.074304 [ms] |
| 2026-09-20 19:23:54.811 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 5.88829 [ms] |
| 2026-09-20 19:23:54.813 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.041632 [ms] |
| 2026-09-20 19:23:54.819 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.780416 [ms] |
| 2026-09-20 19:23:54.821 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 45.0598 [ms] |
| 2026-09-20 19:23:54.823 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 19:23:54.824 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.698463 [ms] |
| 2026-09-20 19:23:54.827 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 19:23:54.828 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 19:23:54.830 | INFO | Visibility | Clusters 2046 tested -> 1329 frustum, 1329 cone, 1329 visible | draws 1289+40, 149024 triangles |
| 2026-09-20 19:23:54.835 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 19:23:54.836 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1329 [count] |
| 2026-09-20 19:23:54.840 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1329 [count] |
| 2026-09-20 19:23:54.842 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1329 [count] |
| 2026-09-20 19:23:54.844 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 149024 [count] |
| 2026-09-20 19:23:54.849 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1329 [count] |
| 2026-09-20 19:23:54.851 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 87% of GPU frame |
| 2026-09-20 19:23:54.854 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-20 19:23:54.855 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 19:23:54.857 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 19:23:54.860 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 19:23:54.862 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 19:23:54.866 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-20 19:23:54.867 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 86.9135 [percent] |
| 2026-09-20 19:24:00.027 | INFO | Performance | CPU 56.66 ms/frame (17.9 fps, worst 100.00 ms over 90 frames), RSS 742 MiB |
| 2026-09-20 19:24:00.029 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 56.6626 [ms] |
| 2026-09-20 19:24:00.032 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 19:24:00.033 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 17.8636 [fps] |
| 2026-09-20 19:24:00.037 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 90 [count] |
| 2026-09-20 19:24:00.039 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 741.945 [MiB] |
| 2026-09-20 19:24:00.041 | INFO | GpuTiming | GPU 71.62 ms total | cull 0.09 · raster 9.00 · HiZ 0.04 · resolve 0.86 · ReSTIR 61.62 · shadow 0.00 · post 0.95 · sky 0.00 · volume 0.00 |
| 2026-09-20 19:24:00.045 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 71.623 [ms] |
| 2026-09-20 19:24:00.047 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.092224 [ms] |
| 2026-09-20 19:24:00.053 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 9.0047 [ms] |
| 2026-09-20 19:24:00.055 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.043872 [ms] |
| 2026-09-20 19:24:00.061 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.858176 [ms] |
| 2026-09-20 19:24:00.068 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 61.6241 [ms] |
| 2026-09-20 19:24:00.070 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 19:24:00.073 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.948414 [ms] |
| 2026-09-20 19:24:00.074 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 19:24:00.077 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 19:24:00.082 | INFO | Visibility | Clusters 2046 tested -> 1987 frustum, 1987 cone, 1987 visible | draws 1975+12, 223686 triangles |
| 2026-09-20 19:24:00.084 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 19:24:00.087 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1987 [count] |
| 2026-09-20 19:24:00.088 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1987 [count] |
| 2026-09-20 19:24:00.090 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1987 [count] |
| 2026-09-20 19:24:00.092 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 223686 [count] |
| 2026-09-20 19:24:00.097 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1987 [count] |
| 2026-09-20 19:24:00.100 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 86% of GPU frame |
| 2026-09-20 19:24:00.105 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-20 19:24:00.107 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 19:24:00.109 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 19:24:00.114 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 19:24:00.116 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 19:24:00.119 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-20 19:24:00.121 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 86.0394 [percent] |
| 2026-09-20 19:24:05.434 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle; inline shadow rays are counted inside GpuReSTIRMs, so GpuShadowMs=0 is expected). Opaque rays use the bounded closest-hit path and spatial winners are revalidated at the current pixel. Tier stage if GI is switched off: Hard @ 1024 px, 1 taps. |
| 2026-09-20 19:24:05.504 | INFO | Performance | CPU 65.98 ms/frame (16.0 fps, worst 100.00 ms over 77 frames), RSS 805 MiB |
| 2026-09-20 19:24:05.505 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 65.9835 [ms] |
| 2026-09-20 19:24:05.507 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 19:24:05.509 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 15.9663 [fps] |
| 2026-09-20 19:24:05.511 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 77 [count] |
| 2026-09-20 19:24:05.512 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 805.113 [MiB] |
| 2026-09-20 19:24:05.514 | INFO | GpuTiming | GPU 67.85 ms total | cull 0.09 · raster 8.94 · HiZ 0.04 · resolve 0.99 · ReSTIR 57.79 · shadow 0.00 · post 0.94 · sky 0.00 · volume 0.00 |
| 2026-09-20 19:24:05.517 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 67.8509 [ms] |
| 2026-09-20 19:24:05.520 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.094336 [ms] |
| 2026-09-20 19:24:05.521 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 8.9367 [ms] |
| 2026-09-20 19:24:05.528 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.044352 [ms] |
| 2026-09-20 19:24:05.530 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.987488 [ms] |
| 2026-09-20 19:24:05.532 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 57.7881 [ms] |
| 2026-09-20 19:24:05.534 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 19:24:05.539 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.942432 [ms] |
| 2026-09-20 19:24:05.541 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 19:24:05.543 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 19:24:05.544 | INFO | Visibility | Clusters 2046 tested -> 1986 frustum, 1986 cone, 1984 visible | draws 1984+0, 223494 triangles |
| 2026-09-20 19:24:05.546 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 19:24:05.548 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1986 [count] |
| 2026-09-20 19:24:05.549 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1986 [count] |
| 2026-09-20 19:24:05.551 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1984 [count] |
| 2026-09-20 19:24:05.556 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 223494 [count] |
| 2026-09-20 19:24:05.558 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1984 [count] |
| 2026-09-20 19:24:05.560 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 85% of GPU frame |
| 2026-09-20 19:24:05.563 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-20 19:24:05.567 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 19:24:05.568 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 19:24:05.576 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 19:24:05.578 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 19:24:05.581 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-20 19:24:05.584 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 85.1691 [percent] |
| 2026-09-20 19:24:06.115 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle; inline shadow rays are counted inside GpuReSTIRMs, so GpuShadowMs=0 is expected). Opaque rays use the bounded closest-hit path and spatial winners are revalidated at the current pixel. Tier stage if GI is switched off: Hard @ 1024 px, 1 taps. |
| 2026-09-20 19:24:15.992 | INFO | Performance | CPU 87.44 ms/frame (13.2 fps, worst 100.00 ms over 58 frames), RSS 928 MiB |
| 2026-09-20 19:24:15.993 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 87.4358 [ms] |
| 2026-09-20 19:24:15.995 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 19:24:15.996 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 13.2115 [fps] |
| 2026-09-20 19:24:15.998 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 58 [count] |
| 2026-09-20 19:24:16.000 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 928.363 [MiB] |
| 2026-09-20 19:24:16.002 | INFO | GpuTiming | GPU 170.51 ms total | cull 0.08 · raster 7.82 · HiZ 0.08 · resolve 1.23 · ReSTIR 161.30 · shadow 0.00 · post 3.01 · sky 0.00 · volume 0.00 |
| 2026-09-20 19:24:16.005 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 170.511 [ms] |
| 2026-09-20 19:24:16.011 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.081024 [ms] |
| 2026-09-20 19:24:16.012 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 7.81894 [ms] |
| 2026-09-20 19:24:16.014 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.084 [ms] |
| 2026-09-20 19:24:16.015 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.22944 [ms] |
| 2026-09-20 19:24:16.017 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 161.298 [ms] |
| 2026-09-20 19:24:16.019 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 19:24:16.020 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 3.01044 [ms] |
| 2026-09-20 19:24:16.024 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 19:24:16.026 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 19:24:16.028 | INFO | Visibility | Clusters 2046 tested -> 1986 frustum, 1986 cone, 1983 visible | draws 1983+0, 223430 triangles |
| 2026-09-20 19:24:16.029 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 19:24:16.031 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1986 [count] |
| 2026-09-20 19:24:16.033 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1986 [count] |
| 2026-09-20 19:24:16.035 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1983 [count] |
| 2026-09-20 19:24:16.040 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 223430 [count] |
| 2026-09-20 19:24:16.042 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1983 [count] |
| 2026-09-20 19:24:16.044 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 95% of GPU frame |
| 2026-09-20 19:24:16.049 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-20 19:24:16.050 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 19:24:16.055 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 19:24:16.057 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 19:24:16.061 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 19:24:16.063 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-20 19:24:16.066 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 94.5966 [percent] |
| 2026-09-20 19:24:27.439 | INFO | Performance | CPU 84.49 ms/frame (11.6 fps, worst 100.00 ms over 60 frames), RSS 798 MiB |
| 2026-09-20 19:24:27.440 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 84.4877 [ms] |
| 2026-09-20 19:24:27.443 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 19:24:27.444 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 11.6429 [fps] |
| 2026-09-20 19:24:27.445 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 60 [count] |
| 2026-09-20 19:24:27.447 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 797.648 [MiB] |
| 2026-09-20 19:24:27.449 | INFO | GpuTiming | GPU 214.31 ms total | cull 0.09 · raster 7.12 · HiZ 0.09 · resolve 1.59 · ReSTIR 205.43 · shadow 0.00 · post 2.45 · sky 0.00 · volume 0.00 |
| 2026-09-20 19:24:27.452 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 214.315 [ms] |
| 2026-09-20 19:24:27.456 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.085056 [ms] |
| 2026-09-20 19:24:27.457 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 7.11674 [ms] |
| 2026-09-20 19:24:27.460 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.091904 [ms] |
| 2026-09-20 19:24:27.462 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.5865 [ms] |
| 2026-09-20 19:24:27.464 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 205.435 [ms] |
| 2026-09-20 19:24:27.469 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 19:24:27.474 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 2.44801 [ms] |
| 2026-09-20 19:24:27.479 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 19:24:27.482 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 19:24:27.488 | INFO | Visibility | Clusters 2046 tested -> 1827 frustum, 1827 cone, 1827 visible | draws 1818+9, 205980 triangles |
| 2026-09-20 19:24:27.490 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 19:24:27.493 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1827 [count] |
| 2026-09-20 19:24:27.497 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1827 [count] |
| 2026-09-20 19:24:27.500 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1827 [count] |
| 2026-09-20 19:24:27.506 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 205980 [count] |
| 2026-09-20 19:24:27.508 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1827 [count] |
| 2026-09-20 19:24:27.511 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 96% of GPU frame |
| 2026-09-20 19:24:27.514 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-20 19:24:27.522 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 19:24:27.524 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 19:24:27.527 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 19:24:27.530 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 19:24:27.532 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-20 19:24:27.539 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 95.8565 [percent] |
| 2026-09-20 19:24:42.088 | INFO | Performance | CPU 89.45 ms/frame (11.6 fps, worst 100.00 ms over 56 frames), RSS 871 MiB |
| 2026-09-20 19:24:42.091 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 89.4451 [ms] |
| 2026-09-20 19:24:42.094 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 19:24:42.098 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 11.5534 [fps] |
| 2026-09-20 19:24:42.100 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 56 [count] |
| 2026-09-20 19:24:42.109 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 871.406 [MiB] |
| 2026-09-20 19:24:42.123 | INFO | GpuTiming | GPU 252.77 ms total | cull 0.07 · raster 3.29 · HiZ 0.09 · resolve 0.62 · ReSTIR 248.70 · shadow 0.00 · post 2.73 · sky 0.00 · volume 0.00 |
| 2026-09-20 19:24:42.126 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 252.766 [ms] |
| 2026-09-20 19:24:42.138 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.06784 [ms] |
| 2026-09-20 19:24:42.140 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.28746 [ms] |
| 2026-09-20 19:24:42.142 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.088064 [ms] |
| 2026-09-20 19:24:42.144 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.620064 [ms] |
| 2026-09-20 19:24:42.153 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 248.702 [ms] |
| 2026-09-20 19:24:42.154 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 19:24:42.158 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 2.73152 [ms] |
| 2026-09-20 19:24:42.159 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 19:24:42.172 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 19:24:42.174 | INFO | Visibility | Clusters 2046 tested -> 850 frustum, 850 cone, 823 visible | draws 823+0, 93444 triangles |
| 2026-09-20 19:24:42.181 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 19:24:42.186 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 850 [count] |
| 2026-09-20 19:24:42.188 | INFO | TelemetryMetrics | Measurement: ClustersCone = 850 [count] |
| 2026-09-20 19:24:42.191 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 823 [count] |
| 2026-09-20 19:24:42.199 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 93444 [count] |
| 2026-09-20 19:24:42.203 | INFO | TelemetryMetrics | Measurement: DrawCalls = 823 [count] |
| 2026-09-20 19:24:42.205 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 98% of GPU frame |
| 2026-09-20 19:24:42.211 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-20 19:24:42.218 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 19:24:42.221 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 19:24:42.224 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 19:24:42.226 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 19:24:42.233 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-20 19:24:42.235 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 98.3924 [percent] |
| 2026-09-20 19:24:56.371 | INFO | Performance | CPU 83.81 ms/frame (11.5 fps, worst 100.00 ms over 60 frames), RSS 779 MiB |
| 2026-09-20 19:24:56.372 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 83.813 [ms] |
| 2026-09-20 19:24:56.375 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 19:24:56.377 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 11.4968 [fps] |
| 2026-09-20 19:24:56.379 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 60 [count] |
| 2026-09-20 19:24:56.381 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 779.141 [MiB] |
| 2026-09-20 19:24:56.382 | INFO | GpuTiming | GPU 197.52 ms total | cull 0.09 · raster 5.94 · HiZ 0.09 · resolve 1.06 · ReSTIR 190.34 · shadow 0.00 · post 2.43 · sky 0.00 · volume 0.00 |
| 2026-09-20 19:24:56.389 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 197.515 [ms] |
| 2026-09-20 19:24:56.392 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.085568 [ms] |
| 2026-09-20 19:24:56.393 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 5.93962 [ms] |
| 2026-09-20 19:24:56.395 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.086272 [ms] |
| 2026-09-20 19:24:56.397 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.05907 [ms] |
| 2026-09-20 19:24:56.400 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 190.345 [ms] |
| 2026-09-20 19:24:56.401 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 19:24:56.403 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 2.42694 [ms] |
| 2026-09-20 19:24:56.405 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 19:24:56.407 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 19:24:56.408 | INFO | Visibility | Clusters 2046 tested -> 1477 frustum, 1477 cone, 1477 visible | draws 1477+0, 166504 triangles |
| 2026-09-20 19:24:56.410 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 19:24:56.411 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1477 [count] |
| 2026-09-20 19:24:56.414 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1477 [count] |
| 2026-09-20 19:24:56.418 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1477 [count] |
| 2026-09-20 19:24:56.420 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 166504 [count] |
| 2026-09-20 19:24:56.422 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1477 [count] |
| 2026-09-20 19:24:56.424 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 96% of GPU frame |
| 2026-09-20 19:24:56.427 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-20 19:24:56.429 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 19:24:56.434 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 19:24:56.435 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 19:24:56.438 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 19:24:56.439 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-20 19:24:56.441 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 96.3696 [percent] |
| 2026-09-20 19:25:08.402 | INFO | Performance | CPU 76.97 ms/frame (12.5 fps, worst 100.00 ms over 65 frames), RSS 750 MiB |
| 2026-09-20 19:25:08.404 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 76.9747 [ms] |
| 2026-09-20 19:25:08.407 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 19:25:08.409 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 12.4778 [fps] |
| 2026-09-20 19:25:08.411 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 65 [count] |
| 2026-09-20 19:25:08.412 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 750.23 [MiB] |
| 2026-09-20 19:25:08.416 | INFO | GpuTiming | GPU 165.53 ms total | cull 0.07 · raster 5.74 · HiZ 0.09 · resolve 1.25 · ReSTIR 158.38 · shadow 0.00 · post 2.35 · sky 0.00 · volume 0.00 |
| 2026-09-20 19:25:08.420 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 165.528 [ms] |
| 2026-09-20 19:25:08.422 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.073664 [ms] |
| 2026-09-20 19:25:08.424 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 5.73622 [ms] |
| 2026-09-20 19:25:08.426 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.08608 [ms] |
| 2026-09-20 19:25:08.429 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.25318 [ms] |
| 2026-09-20 19:25:08.432 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 158.379 [ms] |
| 2026-09-20 19:25:08.434 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 19:25:08.437 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 2.34647 [ms] |
| 2026-09-20 19:25:08.438 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 19:25:08.440 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 19:25:08.442 | INFO | Visibility | Clusters 2046 tested -> 1477 frustum, 1477 cone, 1477 visible | draws 1477+0, 166504 triangles |
| 2026-09-20 19:25:08.444 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 19:25:08.448 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1477 [count] |
| 2026-09-20 19:25:08.450 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1477 [count] |
| 2026-09-20 19:25:08.453 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1477 [count] |
| 2026-09-20 19:25:08.455 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 166504 [count] |
| 2026-09-20 19:25:08.458 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1477 [count] |
| 2026-09-20 19:25:08.460 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 96% of GPU frame |
| 2026-09-20 19:25:08.468 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-20 19:25:08.471 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 19:25:08.476 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 19:25:08.481 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 19:25:08.482 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 19:25:08.487 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-20 19:25:08.489 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 95.681 [percent] |
| 2026-09-20 19:25:10.786 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle; inline shadow rays are counted inside GpuReSTIRMs, so GpuShadowMs=0 is expected). Opaque rays use the bounded closest-hit path and spatial winners are revalidated at the current pixel. Tier stage if GI is switched off: Hard @ 1024 px, 1 taps. |
| 2026-09-20 19:25:11.533 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle; inline shadow rays are counted inside GpuReSTIRMs, so GpuShadowMs=0 is expected). Opaque rays use the bounded closest-hit path and spatial winners are revalidated at the current pixel. Tier stage if GI is switched off: Hard @ 1024 px, 1 taps. |
| 2026-09-20 19:25:15.385 | INFO | Performance | CPU 74.16 ms/frame (13.2 fps, worst 100.00 ms over 68 frames), RSS 826 MiB |
| 2026-09-20 19:25:15.386 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 74.1593 [ms] |
| 2026-09-20 19:25:15.388 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 19:25:15.389 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 13.1581 [fps] |
| 2026-09-20 19:25:15.390 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 68 [count] |
| 2026-09-20 19:25:15.392 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 826.133 [MiB] |
| 2026-09-20 19:25:15.393 | INFO | GpuTiming | GPU 72.55 ms total | cull 0.09 · raster 6.73 · HiZ 0.05 · resolve 0.90 · ReSTIR 64.78 · shadow 0.00 · post 0.91 · sky 0.00 · volume 0.00 |
| 2026-09-20 19:25:15.396 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 72.5514 [ms] |
| 2026-09-20 19:25:15.401 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.087776 [ms] |
| 2026-09-20 19:25:15.402 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 6.73283 [ms] |
| 2026-09-20 19:25:15.405 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.049568 [ms] |
| 2026-09-20 19:25:15.406 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.903264 [ms] |
| 2026-09-20 19:25:15.408 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 64.778 [ms] |
| 2026-09-20 19:25:15.409 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 19:25:15.413 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.911774 [ms] |
| 2026-09-20 19:25:15.418 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 19:25:15.419 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 19:25:15.423 | INFO | Visibility | Clusters 2046 tested -> 1491 frustum, 1491 cone, 1491 visible | draws 1490+1, 168388 triangles |
| 2026-09-20 19:25:15.424 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 19:25:15.427 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1491 [count] |
| 2026-09-20 19:25:15.428 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1491 [count] |
| 2026-09-20 19:25:15.433 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1491 [count] |
| 2026-09-20 19:25:15.434 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 168388 [count] |
| 2026-09-20 19:25:15.438 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1491 [count] |
| 2026-09-20 19:25:15.440 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 89% of GPU frame |
| 2026-09-20 19:25:15.443 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-20 19:25:15.451 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 19:25:15.452 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 19:25:15.455 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 19:25:15.456 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 19:25:15.465 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-20 19:25:15.467 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 89.2856 [percent] |
| 2026-09-20 19:25:20.931 | INFO | Performance | CPU 64.57 ms/frame (14.7 fps, worst 100.00 ms over 78 frames), RSS 868 MiB |
| 2026-09-20 19:25:20.933 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 64.5697 [ms] |
| 2026-09-20 19:25:20.935 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 19:25:20.936 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 14.6765 [fps] |
| 2026-09-20 19:25:20.938 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 78 [count] |
| 2026-09-20 19:25:20.939 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 868.301 [MiB] |
| 2026-09-20 19:25:20.941 | INFO | GpuTiming | GPU 82.12 ms total | cull 0.11 · raster 10.66 · HiZ 0.05 · resolve 1.58 · ReSTIR 69.72 · shadow 0.00 · post 1.33 · sky 0.00 · volume 0.00 |
| 2026-09-20 19:25:20.944 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 82.124 [ms] |
| 2026-09-20 19:25:20.949 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.112096 [ms] |
| 2026-09-20 19:25:20.951 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 10.663 [ms] |
| 2026-09-20 19:25:20.953 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.050816 [ms] |
| 2026-09-20 19:25:20.959 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.57526 [ms] |
| 2026-09-20 19:25:20.963 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 69.7229 [ms] |
| 2026-09-20 19:25:20.965 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 19:25:20.968 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.32784 [ms] |
| 2026-09-20 19:25:20.970 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 19:25:20.975 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 19:25:20.978 | INFO | Visibility | Clusters 2046 tested -> 1971 frustum, 1971 cone, 1865 visible | draws 1932+0, 217724 triangles |
| 2026-09-20 19:25:20.980 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 19:25:20.983 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1971 [count] |
| 2026-09-20 19:25:20.986 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1971 [count] |
| 2026-09-20 19:25:20.987 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1865 [count] |
| 2026-09-20 19:25:20.994 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 217724 [count] |
| 2026-09-20 19:25:20.995 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1932 [count] |
| 2026-09-20 19:25:21.000 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 85% of GPU frame |
| 2026-09-20 19:25:21.003 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-20 19:25:21.006 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 19:25:21.010 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 19:25:21.012 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 19:25:21.013 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 19:25:21.015 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-20 19:25:21.019 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 84.8995 [percent] |
| 2026-09-20 19:25:26.760 | INFO | Performance | CPU 67.99 ms/frame (15.2 fps, worst 100.00 ms over 74 frames), RSS 897 MiB |
| 2026-09-20 19:25:26.761 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 67.9934 [ms] |
| 2026-09-20 19:25:26.763 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 19:25:26.765 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 15.1894 [fps] |
| 2026-09-20 19:25:26.766 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 74 [count] |
| 2026-09-20 19:25:26.769 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 897.445 [MiB] |
| 2026-09-20 19:25:26.770 | INFO | GpuTiming | GPU 67.14 ms total | cull 0.08 · raster 8.13 · HiZ 0.05 · resolve 0.49 · ReSTIR 58.39 · shadow 0.00 · post 1.37 · sky 0.00 · volume 0.00 |
| 2026-09-20 19:25:26.773 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 67.1355 [ms] |
| 2026-09-20 19:25:26.777 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.082976 [ms] |
| 2026-09-20 19:25:26.778 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 8.12688 [ms] |
| 2026-09-20 19:25:26.783 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.049696 [ms] |
| 2026-09-20 19:25:26.784 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.488608 [ms] |
| 2026-09-20 19:25:26.787 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 58.3873 [ms] |
| 2026-09-20 19:25:26.790 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 19:25:26.791 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.37418 [ms] |
| 2026-09-20 19:25:26.794 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 19:25:26.795 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 19:25:26.799 | INFO | Visibility | Clusters 2046 tested -> 1878 frustum, 1878 cone, 1848 visible | draws 1848+0, 208604 triangles |
| 2026-09-20 19:25:26.800 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 19:25:26.803 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1878 [count] |
| 2026-09-20 19:25:26.806 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1878 [count] |
| 2026-09-20 19:25:26.808 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1848 [count] |
| 2026-09-20 19:25:26.809 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 208604 [count] |
| 2026-09-20 19:25:26.810 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1848 [count] |
| 2026-09-20 19:25:26.814 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 87% of GPU frame |
| 2026-09-20 19:25:26.817 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-20 19:25:26.818 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 19:25:26.821 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 19:25:26.822 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 19:25:26.824 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 19:25:26.826 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-20 19:25:26.829 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 86.9694 [percent] |
| 2026-09-20 19:25:32.223 | INFO | Performance | CPU 68.63 ms/frame (14.5 fps, worst 100.00 ms over 74 frames), RSS 746 MiB |
| 2026-09-20 19:25:32.224 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 68.6276 [ms] |
| 2026-09-20 19:25:32.227 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 19:25:32.228 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 14.4956 [fps] |
| 2026-09-20 19:25:32.230 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 74 [count] |
| 2026-09-20 19:25:32.232 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 745.617 [MiB] |
| 2026-09-20 19:25:32.236 | INFO | GpuTiming | GPU 113.43 ms total | cull 0.09 · raster 7.82 · HiZ 0.05 · resolve 1.43 · ReSTIR 104.04 · shadow 0.00 · post 1.16 · sky 0.00 · volume 0.00 |
| 2026-09-20 19:25:32.240 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 113.432 [ms] |
| 2026-09-20 19:25:32.244 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.08688 [ms] |
| 2026-09-20 19:25:32.245 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 7.82314 [ms] |
| 2026-09-20 19:25:32.247 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.051808 [ms] |
| 2026-09-20 19:25:32.248 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.4335 [ms] |
| 2026-09-20 19:25:32.252 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 104.037 [ms] |
| 2026-09-20 19:25:32.254 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 19:25:32.257 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.15853 [ms] |
| 2026-09-20 19:25:32.259 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 19:25:32.261 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 19:25:32.263 | INFO | Visibility | Clusters 2046 tested -> 1766 frustum, 1766 cone, 1766 visible | draws 1766+0, 198406 triangles |
| 2026-09-20 19:25:32.267 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 19:25:32.269 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1766 [count] |
| 2026-09-20 19:25:32.271 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1766 [count] |
| 2026-09-20 19:25:32.273 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1766 [count] |
| 2026-09-20 19:25:32.276 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 198406 [count] |
| 2026-09-20 19:25:32.278 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1766 [count] |
| 2026-09-20 19:25:32.283 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 92% of GPU frame |
| 2026-09-20 19:25:32.288 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-20 19:25:32.289 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 19:25:32.294 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 19:25:32.297 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 19:25:32.299 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 19:25:32.302 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-20 19:25:32.303 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 91.7172 [percent] |
| 2026-09-20 19:25:38.404 | INFO | Performance | CPU 84.67 ms/frame (13.0 fps, worst 100.00 ms over 60 frames), RSS 746 MiB |
| 2026-09-20 19:25:38.405 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 84.6737 [ms] |
| 2026-09-20 19:25:38.408 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 19:25:38.409 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 13.0437 [fps] |
| 2026-09-20 19:25:38.411 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 60 [count] |
| 2026-09-20 19:25:38.415 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 745.621 [MiB] |
| 2026-09-20 19:25:38.417 | INFO | GpuTiming | GPU 105.64 ms total | cull 0.12 · raster 8.32 · HiZ 0.05 · resolve 1.53 · ReSTIR 95.61 · shadow 0.00 · post 1.09 · sky 0.00 · volume 0.00 |
| 2026-09-20 19:25:38.421 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 105.638 [ms] |
| 2026-09-20 19:25:38.427 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.121664 [ms] |
| 2026-09-20 19:25:38.429 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 8.31997 [ms] |
| 2026-09-20 19:25:38.433 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.052064 [ms] |
| 2026-09-20 19:25:38.434 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.52986 [ms] |
| 2026-09-20 19:25:38.436 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 95.6141 [ms] |
| 2026-09-20 19:25:38.441 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 19:25:38.443 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.09241 [ms] |
| 2026-09-20 19:25:38.446 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 19:25:38.449 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 19:25:38.451 | INFO | Visibility | Clusters 2046 tested -> 1845 frustum, 1845 cone, 1845 visible | draws 1845+0, 207310 triangles |
| 2026-09-20 19:25:38.455 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 19:25:38.457 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1845 [count] |
| 2026-09-20 19:25:38.460 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1845 [count] |
| 2026-09-20 19:25:38.461 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1845 [count] |
| 2026-09-20 19:25:38.466 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 207310 [count] |
| 2026-09-20 19:25:38.468 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1845 [count] |
| 2026-09-20 19:25:38.476 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 91% of GPU frame |
| 2026-09-20 19:25:38.483 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-20 19:25:38.488 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 19:25:38.491 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 19:25:38.493 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 19:25:38.495 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 19:25:38.497 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-20 19:25:38.499 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 90.5114 [percent] |
| 2026-09-20 19:25:44.227 | INFO | Performance | CPU 77.01 ms/frame (12.4 fps, worst 100.00 ms over 65 frames), RSS 802 MiB |
| 2026-09-20 19:25:44.228 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 77.0149 [ms] |
| 2026-09-20 19:25:44.230 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 19:25:44.231 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 12.44 [fps] |
| 2026-09-20 19:25:44.233 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 65 [count] |
| 2026-09-20 19:25:44.234 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 802.188 [MiB] |
| 2026-09-20 19:25:44.235 | INFO | GpuTiming | GPU 80.13 ms total | cull 0.08 · raster 6.48 · HiZ 0.05 · resolve 1.13 · ReSTIR 72.39 · shadow 0.00 · post 1.03 · sky 0.00 · volume 0.00 |
| 2026-09-20 19:25:44.240 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 80.1312 [ms] |
| 2026-09-20 19:25:44.243 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.07776 [ms] |
| 2026-09-20 19:25:44.245 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 6.48224 [ms] |
| 2026-09-20 19:25:44.246 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.0512 [ms] |
| 2026-09-20 19:25:44.248 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.13037 [ms] |
| 2026-09-20 19:25:44.251 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 72.3896 [ms] |
| 2026-09-20 19:25:44.254 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 19:25:44.256 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.03369 [ms] |
| 2026-09-20 19:25:44.258 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 19:25:44.260 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 19:25:44.262 | INFO | Visibility | Clusters 2046 tested -> 1520 frustum, 1520 cone, 1520 visible | draws 1520+0, 170908 triangles |
| 2026-09-20 19:25:44.263 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 19:25:44.266 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1520 [count] |
| 2026-09-20 19:25:44.271 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1520 [count] |
| 2026-09-20 19:25:44.273 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1520 [count] |
| 2026-09-20 19:25:44.277 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 170908 [count] |
| 2026-09-20 19:25:44.279 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1520 [count] |
| 2026-09-20 19:25:44.281 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 90% of GPU frame |
| 2026-09-20 19:25:44.286 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-20 19:25:44.288 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 19:25:44.293 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 19:25:44.294 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 19:25:44.297 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 19:25:44.299 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-20 19:25:44.305 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 90.3389 [percent] |
| 2026-09-20 19:25:49.719 | INFO | Performance | CPU 71.51 ms/frame (13.9 fps, worst 100.00 ms over 70 frames), RSS 879 MiB |
| 2026-09-20 19:25:49.720 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 71.5077 [ms] |
| 2026-09-20 19:25:49.722 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 19:25:49.723 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 13.9436 [fps] |
| 2026-09-20 19:25:49.725 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 70 [count] |
| 2026-09-20 19:25:49.726 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 879.199 [MiB] |
| 2026-09-20 19:25:49.728 | INFO | GpuTiming | GPU 66.98 ms total | cull 0.07 · raster 3.84 · HiZ 0.05 · resolve 0.86 · ReSTIR 62.16 · shadow 0.00 · post 0.99 · sky 0.00 · volume 0.00 |
| 2026-09-20 19:25:49.731 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 66.9801 [ms] |
| 2026-09-20 19:25:49.732 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.073568 [ms] |
| 2026-09-20 19:25:49.735 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.84189 [ms] |
| 2026-09-20 19:25:49.740 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.047648 [ms] |
| 2026-09-20 19:25:49.742 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.86016 [ms] |
| 2026-09-20 19:25:49.746 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 62.1568 [ms] |
| 2026-09-20 19:25:49.747 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 19:25:49.751 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.986626 [ms] |
| 2026-09-20 19:25:49.752 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 19:25:49.758 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 19:25:49.759 | INFO | Visibility | Clusters 2046 tested -> 895 frustum, 895 cone, 895 visible | draws 895+0, 100184 triangles |
| 2026-09-20 19:25:49.762 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 19:25:49.764 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 895 [count] |
| 2026-09-20 19:25:49.766 | INFO | TelemetryMetrics | Measurement: ClustersCone = 895 [count] |
| 2026-09-20 19:25:49.768 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 895 [count] |
| 2026-09-20 19:25:49.773 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 100184 [count] |
| 2026-09-20 19:25:49.775 | INFO | TelemetryMetrics | Measurement: DrawCalls = 895 [count] |
| 2026-09-20 19:25:49.777 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 93% of GPU frame |
| 2026-09-20 19:25:49.780 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-20 19:25:49.782 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 19:25:49.788 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 19:25:49.790 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 19:25:49.794 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 19:25:49.795 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-20 19:25:49.798 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 92.799 [percent] |
| 2026-09-20 19:25:55.623 | INFO | Performance | CPU 72.99 ms/frame (13.9 fps, worst 100.00 ms over 69 frames), RSS 961 MiB |
| 2026-09-20 19:25:55.625 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 72.99 [ms] |
| 2026-09-20 19:25:55.628 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 19:25:55.630 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 13.8894 [fps] |
| 2026-09-20 19:25:55.631 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 69 [count] |
| 2026-09-20 19:25:55.633 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 961.145 [MiB] |
| 2026-09-20 19:25:55.635 | INFO | GpuTiming | GPU 81.67 ms total | cull 0.08 · raster 8.20 · HiZ 0.05 · resolve 1.52 · ReSTIR 71.81 · shadow 0.00 · post 1.03 · sky 0.00 · volume 0.00 |
| 2026-09-20 19:25:55.638 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 81.6667 [ms] |
| 2026-09-20 19:25:55.640 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.083936 [ms] |
| 2026-09-20 19:25:55.643 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 8.20266 [ms] |
| 2026-09-20 19:25:55.647 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.049824 [ms] |
| 2026-09-20 19:25:55.649 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.51578 [ms] |
| 2026-09-20 19:25:55.654 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 71.8145 [ms] |
| 2026-09-20 19:25:55.655 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 19:25:55.657 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.02784 [ms] |
| 2026-09-20 19:25:55.659 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 19:25:55.666 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 19:25:55.667 | INFO | Visibility | Clusters 2046 tested -> 1779 frustum, 1779 cone, 1779 visible | draws 1779+0, 199790 triangles |
| 2026-09-20 19:25:55.669 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 19:25:55.671 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1779 [count] |
| 2026-09-20 19:25:55.674 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1779 [count] |
| 2026-09-20 19:25:55.676 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1779 [count] |
| 2026-09-20 19:25:55.683 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 199790 [count] |
| 2026-09-20 19:25:55.684 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1779 [count] |
| 2026-09-20 19:25:55.687 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 88% of GPU frame |
| 2026-09-20 19:25:55.691 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-20 19:25:55.695 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 19:25:55.698 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 19:25:55.699 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 19:25:55.703 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 19:25:55.705 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-20 19:25:55.707 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 87.9361 [percent] |
| 2026-09-20 19:26:03.090 | INFO | Shutdown | Render loop exited cleanly. |
