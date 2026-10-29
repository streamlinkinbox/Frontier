# Traced RGB quartet measured comparison

|Track|Method|Draws|Current geometry evaluations|Suffix calls|Connection calls|Total calls|Instant RMSE|Rolling RMSE|
|---|---|---:|---:|---:|---:|---:|---:|---:|
|Stationary|rotate2|143360|129404|65240|98196|163436|0.00954507604|0.0070403201|
|Stationary|fresh-quartet|262144|129435|107290|98175|205465|0.00955437862|0.0057846364|
|Stationary|matched-raw-budget|143360|127350|101002|94345|195347|0.0102250688|0.00567918921|
|Stationary|raw8|262144|232886|154935|144945|299880|0.00744249133|0.0045153882|
|Smooth|rotate2|143360|129404|65087|72301|137388|0.00963439762|0.00680152201|
|Smooth|fresh-quartet|262144|129435|106679|72332|179011|0.00951642604|0.00563858138|
|Smooth|matched-raw-budget|143360|127350|83707|66522|150229|0.0105597514|0.00567049378|
|Smooth|raw8|262144|232886|128516|102152|230668|0.00763557064|0.00450302609|
|Receiver jumps|rotate2|143360|129404|65166|58409|123575|0.00727931321|0.00572701325|
|Receiver jumps|fresh-quartet|262144|129435|106998|58375|165373|0.00719600113|0.00487851866|
|Receiver jumps|matched-raw-budget|143360|127350|59640|54553|114193|0.00805216964|0.00478690427|
|Receiver jumps|raw8|262144|232886|89822|82261|172083|0.00591409309|0.00391275592|

Suffix/connection calls are wrapper instrumentation,CPU verified;proposal-matched arms are not query/time matched.

|Track|Metric|Rotate minus matched-budget MSE|Block SE|95% paired interval|
|---|---|---:|---:|---|
|Stationary|instant|-1.34435546e-05|9.67971427e-07|[-1.57324434e-05,-1.11546659e-05]|
|Stationary|rolling|1.73129171e-05|6.57454103e-07|[1.57582851e-05,1.8867549e-05]|
|Smooth|instant|-1.86867323e-05|1.44873013e-06|[-2.21124347e-05,-1.52610299e-05]|
|Smooth|rolling|1.41062019e-05|6.09616531e-07|[1.26646879e-05,1.5547716e-05]|
|Receiver jumps|instant|-1.18490351e-05|4.11321049e-07|[-1.28216548e-05,-1.08764154e-05]|
|Receiver jumps|rolling|9.88422825e-06|3.10419459e-07|[9.15020287e-06,1.06182536e-05]|

Intervals use8 paired seed blocks,Student t(df7);common seeds across tracks are not independent scenes.
