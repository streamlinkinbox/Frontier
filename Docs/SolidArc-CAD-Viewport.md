# SolidArc CAD viewport: interaction and diagnostics

The windowed **SolidArc** in Authoring Tools uses its own Z-up analytic CAD raster and workplane; it does **not** run the ProjectDrive ReSTIR raytracing kernel. This implementation is currently a **CPU software raster** uploaded to the window, not a native GPU CAD raster. To keep editing responsive it renders at most ~1.1 million working pixels at rest and ~460,000 while dragging/navigating, with one sample while interacting. Those are work budgets, not measured frame-rate promises. The underlying renderer, tessellation, selection and GPU upload may still require further optimisation on complex models.

## Drawing and editing

- Open **Construct**, then choose a tile. Selecting a tile **arms** a tool; it no longer inserts a figure on an automatic ring or refits the camera.
- For Line, Polyline, Rectangle, Circle and other sketches, click the XY workplane points; the existing ToolSession provides snapping, rubber-band curves and prompts. **Enter** completes an optional multi-point sketch; **Escape** discards it. The tool may complete immediately when its required last point is clicked.
- For solids, surfaces and the reference plane, click a workplane position to set an anchor, then press **Enter** to create the figure there; **Escape** discards the pending placement. No click means no figure.
- The 10 mm XY lattice and X/Y axes provide the construction reference; while constructing, cyan pointer-aligned guides and the world-space XY readout show where the click will land. Orbit/pan/wheel remain available while a tool is armed.
- Select body edges in Edge mode (`3`) before pressing **B** (rounded bevel / fillet) or **C** (flat chamfer). The current default distance is 0.1 model units. The native kernel refuses unsupported or self-intersecting selections rather than silently altering unrelated edges. Parameters can also be entered explicitly with `fillet <body> <radius> --edges=…` and `chamfer <body> <setback> --edges=…`.

## Frontier Vulkan diagnostics

A line reporting `Software BVH` while hardware inline ray queries are active was a telemetry labeling error: the shadow-path log now reports the **active** hardware query route, not merely the requested capability tier. RT-off with GI still on is a different compute path (distance-field GI, with surfel fallback); RT-off and GI-off is plain raster. The allocation of SDF/surfel resources on startup alone does not prove either route was used.

If an acquire, fence wait/reset, submit or present returns a failing `VkResult`, the runtime now records the **API name and result** in `VulkanFrame`, flushes the log and stops reusing that frame's semaphores/fence. It also avoids acquiring an image before scene descriptors are ready. This is *error containment and diagnosis*, not proof that an AMD driver/device-loss or a PC-wide freeze has been fixed. A result of `-4` is `VK_ERROR_DEVICE_LOST`; the exact failing call and driver/event logs are needed to isolate the origin. Do not repeatedly trigger a machine-wide crash to collect diagnostics.

ProjectDrive temporarily reduces ReSTIR candidates, spatial taps, denoising levels and bounce counts **only during a native editor gizmo drag**, then resets accumulation and restores the configured budget. This does not change the still-frame raytracing setting or fix unrelated full-scene reuploads on geometry changes. The user's logged 13–23 FPS was GPU-bound mainly in ReSTIR; runtime FPS on their RX 9060 XT and the RT-off transition must be measured on Windows with the updated package.

## Verification

`cmake -S Editor/AuthoringTools/Modelling/SolidArc -B build/SolidArc -DSOLIDARC_BUILD_MODE_TESTS=ON && cmake --build build/SolidArc && ctest --test-dir build/SolidArc --output-on-failure` runs the native geometry/gesture regression. Linux/CI builds verify compilation and the test seam, but cannot reproduce a Windows AMD device loss or certify interactive FPS. Use the Windows desktop bundle from the corresponding Actions run to verify pointer construction, undo, the B/C edge operations, and the exact `VulkanFrame` line if Frontier reports a device failure.
