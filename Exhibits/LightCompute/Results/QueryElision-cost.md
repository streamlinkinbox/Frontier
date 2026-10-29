# Lossless packet-local cost comparison

|Track|Method|Unoptimized calls|Optimized calls|Reduction|
|---|---|---:|---:|---:|
|Stationary|rotate2|200167|163436|18.350%|
|Stationary|fresh-quartet|258870|205465|20.630%|
|Stationary|matched-raw-budget|254700|195347|23.303%|
|Stationary|raw8|465772|299880|35.617%|
|Smooth|rotate2|200167|137388|31.363%|
|Smooth|fresh-quartet|258870|179011|30.849%|
|Smooth|matched-raw-budget|254700|150229|41.017%|
|Smooth|raw8|465772|230668|50.476%|
|Receiver jumps|rotate2|200167|123575|38.264%|
|Receiver jumps|fresh-quartet|258870|165373|36.117%|
|Receiver jumps|matched-raw-budget|254700|114193|55.166%|
|Receiver jumps|raw8|465772|172083|63.054%|
