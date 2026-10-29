# Traced RGB quartet measured comparison

|Track|Method|Draws|Valid samples|Suffix calls|Connection calls|Total calls|Instant RMSE|Rolling RMSE|
|---|---|---:|---:|---:|---:|---:|---:|---:|
|Stationary|rotate2|143360|129546|65173|98522|163695|0.00956448958|0.00706982156|
|Stationary|fresh-quartet|262144|129512|107442|98416|205858|0.00955492092|0.00582287533|
|Stationary|calibrated-raw|113279|100791|84459|78753|163212|0.011364224|0.00654081158|
|Stationary|raw8|262144|233131|155294|145336|300630|0.00741900228|0.0045094023|
|Smooth|rotate2|143360|129546|64996|72270|137266|0.009563775|0.00670158703|
|Smooth|fresh-quartet|262144|129512|106846|72476|179322|0.00945897507|0.00557471447|
|Smooth|calibrated-raw|127694|113518|76149|60963|137112|0.0114795855|0.00641407428|
|Smooth|raw8|262144|233131|128841|102647|231488|0.00762117604|0.00446459594|
|Receiver jumps|rotate2|143360|129546|65106|58527|123633|0.00722924125|0.00562696349|
|Receiver jumps|fresh-quartet|262144|129512|107092|58614|165706|0.00716546187|0.00482791009|
|Receiver jumps|calibrated-raw|367657|326909|56540|52074|108614|0.00865467342|0.00536818137|
|Receiver jumps|raw8|262144|233131|90260|82774|173034|0.00591026553|0.00387924624|

Suffix/connection calls are wrapper instrumentation,CPU verified.
Separate pilot calibrates expected query budget;check held-out budget gates,not physical time.

|Track|Metric|Rotate minus calibrated-budget MSE|Block SE|95% paired interval|
|---|---|---:|---:|---|
|Stationary|instant|-3.76661255e-05|9.41337229e-07|[-3.98920343e-05,-3.54402166e-05]|
|Stationary|rolling|7.20016078e-06|8.77782153e-07|[5.12453581e-06,9.27578574e-06]|
|Smooth|instant|-4.03150909e-05|1.08356861e-06|[-4.28773236e-05,-3.77528583e-05]|
|Smooth|rolling|3.77091979e-06|6.98983183e-07|[2.1180872e-06,5.42375238e-06]|
|Receiver jumps|instant|-2.26414429e-05|8.58198084e-07|[-2.46707589e-05,-2.06121269e-05]|
|Receiver jumps|rolling|2.84534692e-06|6.00262952e-07|[1.42595058e-06,4.26474325e-06]|

Intervals use8 paired seed blocks,Student t(df7);common seeds across tracks are not independent scenes.
