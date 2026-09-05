# 🧩 Phase R6 — ReSTIR DI proper (plan; implementation authorized)

Plan v2.2 phase R6, pulled ahead of R5 (approved: R5's rayQuery path cannot be proven here — no Vulkan
in the sandbox, Pascal has no `VK_KHR_ray_query` — so R6 first, R5 waits for an RTX run). R4b left the
kernel at *initial-candidates RIS only* with the spatial loop mislabelled (extra same-pixel candidates,
biased as "spatial"). R6 makes it ReSTIR as published. No new data formats; Tier A only (Tier B path
does not exist yet — R6 must not depend on R5).

## 0. Scope line

**In:** reservoir v2 (stored light index + uv + visibility); temporal reuse via R2 motion-vector
back-projection with 25°/10% validation + M-clamp 20× + visibility re-trace; true spatial neighbour
reuse with pairwise MIS; full-BSDF target function; M/W/age debug views; R4b diagnostics carry-over
(complexity histogram + LOD stats); many-light proof on ShaderBall (2210 luminaires).
**Out (explicit):** GI reservoirs (R7), denoiser (R7; NRD optional), alias-table light pick stays
uniform until row 3 (identity first), ray pipeline (Tier C), motion-vector consumption for the running
mean (history still restarts on camera move — temporal *reservoirs* are the R6 mechanism).

## 1. Design (classic ReSTIR DI [Bitterli et al. 2020], adapted to this kernel)

* **Row 1 — reservoir v2 (this commit):** `SelectedLight` (luminaire-table index) + `SelectedUv` +
  `Visible` join the reservoir; `ResampleCandidate` copies the payload on select. Kills the three O(L)
  nearest-emissive brute-force searches (each shade did 3 × LightTriangleCount distance compares —
  on ShaderBall that is 3 × 2210 per pixel). Target formula unchanged → converged image identical.
  New `DispatchFeature` bits + integrator toggles `TemporalReuse`/`SpatialReuse` (default true, in the
  RenderScheduler ReSTIR section; accumulation resets on change) so every later row has an off-switch.
* **Row 2 — temporal reuse:** new kernel bindings (motion image RO, prev-reservoir SSBO RO,
  curr-reservoir SSBO WO; bindless `Textures[]` moves 15→18 — same drill as R4b 11→15). Back-project by
  the R2 motion vector; reject when normal > 25° or depth > 10% off the reprojected surface; merge with
  M-cap `min(Mprev + Mcur, 20 × Mcur)`; re-trace visibility at the current pixel (standard, keeps W
  unbiased); store the final reservoir. Target becomes full-BSDF `f·Le·cos/d²` (needed for correct
  re-evaluation at foreign pixels; converged image unchanged, noise improves).
* **Row 3 — spatial reuse + views:** 2 passes × 4 neighbour taps (screen-space Poisson, radius ~32 px),
  same validation, pairwise-MIS merge; debug views M (11), W (12), Age (13); R4b carry-over
  (complexity histogram + LOD stats in the F3 popup); alias-table light pick for many-light scenes
  (uniform pick kept behind the toggle for the identity proof).
* **Row 4 — phase note.**

## 2. Proofs

1. **Row 1 (sandbox):** `ReSTIRReservoirTest` — stored-index W bit-identical to brute-force W (1000
   builds); RIS identity `E[p̂(z)·W] = E[p̂]` within 1% (200 k reservoirs); pairwise combine == union
   reservoir. Plus Cornell converged-identity on hardware.
2. **Rows 2–3 (sandbox):** port-discipline harnesses for the merge/validation math (M-cap, 25°/10%
   reject, MIS weights) with fixed vectors; GPU execution on the 1060.
3. **Hardware (1060, all rows):** toggles off == R4b image (converged); toggles on == same converged
   image with lower noise at equal frame count (measure: variance at 256 frames, Cornell + Sponza +
   ShaderBall); F3 `kernel` ms must not regress > +10% at 1080p (reuse is pure win minus one shadow
   re-trace per pixel); many-light ShaderBall shows the win concentrated in previously-noisy pixels.

## 3. Decisions taken (from the R5-plan approval)

* R6 before R5 (R5 unprovable here). R5 design recorded: **per-instance BLAS + TLAS**,
  `customIndex` = Slate instance id (kills the R4b linear scan), single-BLAS alternative rejected.
* R4b diagnostics carry-over lands in R6 row 3 with the M/W/age views (not R5).
* R5 plan stays as written (`RestirPhaseR5-HardwareAS-Plan.md`); Sponza baseline table (§3.5 of the R4b
  note) lands in the R6 phase note as the pre-reuse reference for R7.
