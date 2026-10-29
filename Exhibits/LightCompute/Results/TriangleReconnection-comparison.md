# Vulkan secondary-vertex measured results

Actual BVH queries on SwiftShader CPU Vulkan, not physical-GPU performance.

| Fixture | Reference | GPU area mean ± nominal SE | Missing J | Extra J | No fallback | No floor |
|---------|----------:|---------------------------:|----------:|--------:|------------:|---------:|
| self | 0.100571720 | 0.100431058 ± 0.000065350 | 0.100431058 | 0.100431058 | 0.100307247 | 0.100431384 |
| shift | 0.035428539 | 0.035501154 ± 0.000049598 | 0.075323798 | 0.019914724 | 0.035462054 | 0.035500991 |
| partial_support | 0.035428539 | 0.035382484 ± 0.000096976 | 0.075255824 | 0.019800637 | 0.020105917 | 0.020032351 |
| dark_donor | 0.017934605 | 0.017995020 ± 0.000070655 | 0.024819351 | 0.013512261 | 0.000000000 | 0.000000000 |
