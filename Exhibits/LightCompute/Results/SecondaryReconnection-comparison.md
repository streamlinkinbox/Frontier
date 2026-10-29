# Measured secondary-vertex correctness results

Native CPU estimates, not rendered images or physical-GPU timing. All means are linear radiance.

| Fixture | Reference | Corrected mean ± nominal SE | No fallback | No floor |
|---------|----------:|----------------------------:|------------:|---------:|
| self | 0.061760591 | 0.061793496 ± 0.000075282 | 0.061839248 | 0.061799856 |
| shift | 0.028281023 | 0.028258534 ± 0.000043136 | 0.028235657 | 0.028272752 |
| partial_support | 0.028281023 | 0.028239025 ± 0.000077819 | 0.015999083 | 0.016046076 |
| dark_donor | 0.017934607 | 0.017989269 ± 0.000064512 | 0.000000000 | 0.000000000 |

Area and solid-angle encodings agree pointwise within 2.22e-16.

All six required negative controls fail the reference as intended.
The four proposal/target support controls match the independently clipped-domain integral.
Removing the fallback or the source target floor loses target-visible/source-invisible contributions.

| Fixture | Corrected RMSE | Fresh8 RMSE |
|---------|---------------:|------------:|
| self | 0.039065924 | 0.052645656 |
| shift | 0.022766019 | 0.037234448 |
| partial_support | 0.038514422 | 0.037150492 |
| dark_donor | 0.035873609 | 0.030061085 |

These are per-reservoir errors, not errors of the cohort mean. Proposal mixes differ and no ray,
illumination, cache-build or GPU costs were measured: no efficiency win is established.
