# Tyre lattice (XPBD carcass) — how `Archive/TyreLattice.bin` is made

`Archive/TyreLattice.bin` is **one prestressed tyre** exactly as the Slate engine's `ConstructTyreLattice` builds it
(16 rings × 64 segments = 1024 particles, 3712 cords in 8 colour runs, a pressure/volume row and a shape block), exported with
the dimensions of *this* vehicle's tyre instead of the engine preview's 225/45R17:

| | value | why |
|---|---|---|
| outer radius (particles) | 0.49855 m | the contact stands `TreadThickness` = 10 mm outside the particles, so the rolling radius is `VehicleGeometry.TyreRadius` = 0.50855 m |
| rim radius | 0.3446 m | `VehicleGeometry.TyreRimRadius` |
| section width | 0.3958 m | `VehicleGeometry.TyreWidth` |
| bead width | 0.30 m | a 12 J rim |
| carcass mass | 12 kg | spread over the free particles (the engine's 3× numerical mass scale is applied by the solver) |
| inflation | 220 kPa gauge | the vehicle's reference pressure; the host re-declares it from each wheel's own pressure every frame |
| segments | 64 | the engine refuses more: rings + segments must fit the shape block's order |

`Source/TyreCarcassSolver.js` parses the stream (`ParseTyreLattice`), tiles it per wheel and steps it with the six kernels in
`Shader/Tyre*.wgsl`. `Tools/TyreCarcass.test.mjs` checks that the lattice still matches `VehicleSpecification.js`.

## Regenerating it

The kernels and the builder are the Slate engine's (`SultanAladin/Slate`, branch `arena/ca517016-slate`, commits `8e799250` and `465b55a2`).
`ExportTyreLattice.cpp` here is the engine's `Tools/TyreSolverPreview/ExportTyreLattice.cpp` with one change: the tyre is declared on the
command line. It builds against the engine's headers and `TyreSolver.cpp` only (about 1 MB), which `Build.sh` fetches:

```sh
Tools/TyreLattice/Build.sh            # needs git, g++ >= 12 (C++20) and github.com
```

Sanity check used while porting: built with no arguments the exporter reproduces the engine preview's committed `TyreLattice.bin`
**byte for byte**, so the toolchain here is the engine's.

If the vehicle's tyre dimensions change, edit the arguments in `Build.sh` (and keep `TreadThickness` in `TyreCarcassSolver.js`
in step); `TyreCarcass.test.mjs` will fail until the lattice and `VehicleSpecification.js` agree again.
