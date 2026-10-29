# Triangle reconnection query/error comparison

Actual nearest-facet invocations, including repeated validation suffix/connection queries; not GPU time.

| Fixture | Area RMSE | Fresh8 RMSE | Area queries | Fresh8 queries |
|---------|----------:|------------:|-------------:|---------------:|
| self | 0.041363721 | 0.063297770 | 6,881,898 | 4,695,160 |
| shift | 0.026700412 | 0.041222885 | 6,885,117 | 4,370,607 |
| partial_support | 0.052988816 | 0.041339849 | 5,959,756 | 4,371,355 |
| dark_donor | 0.035812225 | 0.030091804 | 4,735,419 | 4,284,143 |
