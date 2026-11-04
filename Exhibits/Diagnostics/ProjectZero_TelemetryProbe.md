# Project-Zero Telemetry Probe

Development/debug-only probe (compiled out of ship builds). All records were held in RAM and written once, at application close.

## Events

| Event | At [ms] |
|---|---:|
| Boot | 0.000 |
| StartupComplete | 189511.057 |
| FirstFrameComplete | 189981.012 |
| Shutdown | 349867.248 |

## Startup Phases

| Phase | Begin [ms] | End [ms] | Duration [ms] |
|---|---:|---:|---:|
| SceneDecode | 663.757 | 1246.612 | 582.856 |
| TextureDecode | 1246.614 | 2132.878 | 886.264 |
| CwbvhBuild | 2161.857 | 2849.263 | 687.406 |
| VulkanBringUp | 2868.783 | 181737.034 | 178868.251 |
| ShadingTableBake | 181739.092 | 183629.046 | 1889.954 |
| SceneUpload | 183629.054 | 183851.851 | 222.797 |
| InterfaceBringUp | 188998.400 | 189511.055 | 512.655 |

## Shader Loads and Bring-up Stages

| Kind | Name | At [ms] | Duration [ms] |
|---|---|---:|---:|
| Stage | BringInstance | 4674.307 | 1359.444 |
| Stage | BringSurface | 4674.354 | 0.040 |
| Stage | BringPhysicalDevice | 4678.297 | 3.942 |
| Stage | BringLogicalDevice | 4815.456 | 137.155 |
| Stage | BringSwapchain | 6151.059 | 1335.597 |
| Stage | BringStorageImage | 6172.261 | 21.198 |
| Stage | BringCommandRecording | 6172.584 | 0.318 |
| Shader | Engine/Shaders/ReSTIRViewport.spv | 6206.723 | 31.872 |
| Stage | BringComputePipeline | 180755.836 | 174583.246 |
| Shader | Engine/Shaders/AtrousDenoise.spv | 180893.743 | 75.270 |
| Stage | BringDenoisePipeline | 180940.655 | 184.805 |
| Shader | Engine/Shaders/LuminanceReduce.spv | 181035.626 | 23.419 |
| Stage | BringLuminanceReduction | 181073.696 | 133.035 |
| Stage | BringSkyRecord | 181074.376 | 0.673 |
| Stage | BringMoonRecord | 181074.812 | 0.433 |
| Stage | BringPostRecord | 181075.251 | 0.436 |
| Stage | BringStarTables | 181075.675 | 0.422 |
| Stage | BringDescriptorSet | 181076.129 | 0.449 |
| Stage | BringCycleSlots | 181092.626 | 16.491 |
| Stage | BringImGui | 181280.227 | 187.596 |
| Shader | Engine/Shaders/ClusterCull.spv | 181333.951 | 45.762 |
| Shader | Engine/Shaders/HiZReduce.spv | 181373.152 | 23.136 |
| Shader | Engine/Shaders/SurfaceResolve.spv | 181468.531 | 93.423 |
| Shader | Engine/Shaders/VisibilityRaster.vert.spv | 181517.407 | 47.753 |
| Shader | Engine/Shaders/VisibilityRaster.frag.spv | 181524.639 | 7.226 |
| Shader | Engine/Shaders/ShadowRaster.vert.spv | 181543.010 | 15.117 |
| Shader | Engine/Shaders/ShadowRaster.frag.spv | 181569.377 | 26.360 |
| Shader | Engine/Shaders/ShadowResolve.spv | 181660.052 | 88.856 |
| Stage | BringVisibility | 181736.797 | 456.565 |
| Shader | Engine/Shaders/InterfaceRaster.vert.spv | 189043.499 | 27.715 |
| Shader | Engine/Shaders/InterfaceRaster.frag.spv | 189076.799 | 33.291 |

## Frames

1583 frames recorded. Full per-frame rows: ProjectZero_TelemetryProbe_Frames.csv

| Column | Mean [ms] | Peak [ms] |
|---|---:|---:|
| Frame Δτ | 70.1946 | 100.0000 |
| CPU InputAndUi | 0.5184 | 66.3707 |
| CPU CelestialTick | 0.0175 | 0.1195 |
| CPU EditorAndPanels | 3.0766 | 167.3822 |
| CPU SimulationAndInterface | 0.0678 | 0.4311 |
| CPU ScenePush | 0.0153 | 5.1191 |
| CPU RecordAndPresent | 96.2829 | 586.9418 |
| CPU FrameCapWait | 0.0013 | 0.0048 |
| GPU Cull | 0.0884 | 0.2850 |
| GPU Raster | 7.6246 | 18.4609 |
| GPU HiZ | 0.0554 | 0.3994 |
| GPU Resolve | 1.0409 | 2.6522 |
| GPU Kernel | 89.5414 | 289.5571 |
| GPU Shadow | 0.0000 | 0.0000 |
| GPU ReSTIR | 89.5414 | 289.5571 |
| GPU Post | 1.2999 | 3.2498 |
| GPU Sky | 0.0000 | 0.0000 |
| GPU Volume | 0.0000 | 0.0000 |

1581 of 1583 frames carried valid device timestamps.
