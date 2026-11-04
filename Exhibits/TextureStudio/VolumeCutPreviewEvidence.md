# Native volume cutting-plane preview

The native Volume SDF map inspector now exposes Iso + Cut, Triplane and Stack, plus slice axis, position and palette. Bands, contours, inside/outside, normalized and confidence palettes apply to both the 2D slice and 3D planes. Drag to orbit and use the mouse wheel to zoom. The 3D renderer composites sampled planes and a coarse isosurface in depth order and caches its 72×72 raster until the field or view changes; it does not change the Float32 baker.

`Evidence/VolumeCutPreview.png` is a headless Linux ImGui capture of the default Iso + Cut mode after baking the teapot. It is not a Windows desktop capture. `VolumePreviewValidation` checks mode, axis, slice, palette, orbit, zoom, unsigned normalization and malformed buffers on an anisotropic synthetic field. `BakingLayoutEvidence` bakes and displays the volume and checks NRRD export. These checks do not establish visual parity with the web prototype or real GPU performance.
