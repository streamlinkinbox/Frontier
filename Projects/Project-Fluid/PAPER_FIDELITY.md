# Paper-fidelity upgrade status

This checklist prevents implementation approximations from being described as completed research-paper reproductions.

## Reviewed primary methods

- Akinci et al. 2012, *Versatile Rigid-Fluid Coupling for Incompressible SPH* — sampled boundaries, pseudo-mass normalization, and equal/opposite hydrodynamic rigid coupling.
- Macklin and Müller 2013, *Position Based Fluids* — density constraints and iterative position projection.
- Akinci et al. 2013, *Versatile Surface Tension and Adhesion for SPH Fluids* — piecewise cohesion, color-field normal correction, and sampled-solid adhesion.
- Weiler et al. 2018, *A Physically Consistent Implicit Viscosity Solver for SPH Fluids* — radial Laplacian, backward Euler, symmetric matrix-free system, PCG, and block-Jacobi preconditioning.
- Yu and Turk 2010/2013, *Reconstructing Surfaces of Particle-Based Fluids Using Anisotropic Kernels* — weighted covariance/PCA, smoothed kernel centers, volume-bounded anisotropy, summed implicit field, and isosurface extraction.

Primary PDFs and adaptation notes are indexed in `RESEARCH.md`.

## Implemented in native C++

- [x] Sampled floor and wall boundary particles
- [x] Fibonacci-sampled sphere boundary
- [x] `Psi_b = rho0 / sum W_bk` pseudo-mass normalization
- [x] Boundary density and PBF-gradient contributions
- [x] Akinci piecewise cohesion and normal-difference term
- [x] Sampled-solid adhesion
- [x] Symmetric backward-Euler radial viscosity operator
- [x] Matrix-free PCG
- [x] Full 3x3 block-Jacobi preconditioner
- [x] Sampled stationary-solid viscosity blocks

## Not complete — must not be claimed as complete

- [ ] Divergence-free velocity projection before implicit viscosity
- [ ] Published-SI material conversion with explicit scene-unit scaling and validation benchmarks
- [ ] Dynamic rigid sphere inertia, angular momentum, and equal/opposite hydrodynamic forces
- [x] Yu–Turk weighted covariance/PCA, bounded volume-normalized ellipsoids, and smoothed render centers in native C++
- [x] Summed anisotropic implicit density field on sparse dirty bricks
- [x] Indexed Marching Cubes extraction with topology/volume validation
- [x] Density-gradient vertex normals
- [x] Mild Taubin smoothing followed by global volume restoration
- [x] Dirty-brick field and mesh rebuild with unchanged-frame zero-work validation
- [ ] Vulkan BLAS/TLAS construction for the extracted fluid mesh
- [ ] `VK_KHR_ray_tracing_pipeline` closest-hit/any-hit/miss integration
- [ ] Two-interface dielectric transport
- [ ] Participating-medium multiple scattering
- [ ] Physically based caustic transport
- [ ] Deterministic CPU reference path tracer matching the Vulkan RT scene

The existing screen-space renderer remains a preview until every reconstruction and ray-tracing item above is implemented and validated. Hardware Vulkan RT must be capability-gated; unsupported devices must report the missing extensions rather than silently presenting the preview as the paper-fidelity renderer.
