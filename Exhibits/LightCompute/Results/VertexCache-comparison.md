# Persistent vertex cache query comparison

Actual nearest-facet calls. Same-seed paired estimates; not additional independent samples or GPU time.

| Fixture | Inline eight-state queries | Cached queries | Reduction | Warm reduction | Receiver reduction |
|---------|---------------------------:|---------------:|----------:|---------------:|-------------------:|
| 0 | 26,897,901 | 19,580,858 | 27.20% | 96.24% | 96.24% |
| 1 | 26,941,694 | 19,583,586 | 27.31% | 96.24% | 96.24% |
| 2 | 23,132,018 | 16,996,523 | 26.52% | 95.74% | 95.77% |
| 3 | 18,833,118 | 14,940,172 | 20.67% | 94.95% | 95.02% |
