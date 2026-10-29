# Vulkan secondary-vertex measured results

SwiftShader CPU Vulkan, not physical-GPU performance. Nominal SE is across 64 cohort means.

| Fixture | Reference | GPU area mean ± nominal SE | Missing J | Extra J | No fallback | No floor |
|---------|----------:|---------------------------:|----------:|--------:|------------:|---------:|
| self | 0.061760591 | 0.061691593 ± 0.000074414 | 0.061691593 | 0.061691593 | 0.061499678 | 0.061680607 |
| shift | 0.028281023 | 0.028351211 ± 0.000039376 | 0.049984083 | 0.017870239 | 0.028301965 | 0.028350280 |
| partial_support | 0.028281023 | 0.028345619 ± 0.000066967 | 0.050074565 | 0.017839165 | 0.016077994 | 0.016039670 |
| dark_donor | 0.017934605 | 0.017932988 ± 0.000071566 | 0.024719971 | 0.013476177 | 0.000000000 | 0.000000000 |
