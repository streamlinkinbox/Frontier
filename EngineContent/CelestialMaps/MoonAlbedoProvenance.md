# VehicleHMI lunar texture

`MoonAlbedoSource2048.jpg` is a byte-identical copy of the **actual demo's** `luna_2k.jpg`, not a generated substitute.

| Property | Source |
|---|---|
| Repository | `SultanAladin/Frontier` |
| Revision | `6afb6f00b5f25c45f7cda92c7da65b64845768f3` (`arena/e9bd7db5-frontier`, inspected 2026-10-27) |
| Path | `Experimental/VehicleHmiPrototype/Assets/Celestial/luna_2k.jpg` |
| Git blob SHA-1 | `96996f2b89e1f0164b051bd2c1bd3f53516c169b` |
| Dimensions | 2048 × 1024, equirectangular, sRGB |
| Runtime conversion | `python Automation/PrepareMoonAlbedo.py` (Pillow required) |
| Runtime output | `MoonAlbedo2048.ppm`, generated/ignored, RGB bytes remain sRGB |
| Decode | `LunarAlbedoMap::LoadPpm` performs IEC sRGB decoding **before** bilinear filtering |

The source demo's `CelestialAtlas.js` declares `rgba8unorm-srgb` and puts Luna first in its six-body atlas.
The native port uses the Luna JPEG also consumed by the demo's `NativeMoonPreview.js`; other five bodies are not ported.
Orientation follows the demo: longitude zero at U=0.5, north at V=0. The exhibit checks Tycho, Plato and Mare Crisium.

The earlier branch referenced a missing, ignored 1024-pixel image and provenance file. Neither survived the checkout.
The new source image and this note are explicitly retained by `.gitignore`; the runner regenerates PPM before testing.

This is a source-parity claim, not independently calibrated lunar reflectance or a verified third-party licence grant.
Upstream distribution rights/attribution still require confirmation before commercial redistribution.
