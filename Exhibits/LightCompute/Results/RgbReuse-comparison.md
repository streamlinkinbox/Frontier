# Persistent RGB measured comparison

|Track|Method|Draws|Current geometry evaluations|Birth-score evaluations*|Instant RGB RMSE|Rolling error RMSE|
|---|---|---:|---:|---:|---:|---:|
|Stationary|rotate2|143360|129404|127350|0.0987549769|0.0729179226|
|Stationary|fresh-quartet|262144|129435|232886|0.0987070998|0.0599865213|
|Stationary|matched-raw-budget|143360|127350|0|0.109022641|0.0606744895|
|Stationary|raw8|262144|232886|0|0.0793810562|0.0484864285|
|Smooth|rotate2|143360|129404|127350|0.0752422351|0.0532456124|
|Smooth|fresh-quartet|262144|129435|232886|0.0730485056|0.0444332939|
|Smooth|matched-raw-budget|143360|127350|0|0.0818532433|0.0448747235|
|Smooth|raw8|262144|232886|0|0.0596105445|0.0359231584|
|Receiver jumps|rotate2|143360|129404|127350|0.0721258128|0.0518946962|
|Receiver jumps|fresh-quartet|262144|129435|232886|0.0697136954|0.043394313|
|Receiver jumps|matched-raw-budget|143360|127350|0|0.082266921|0.0449991937|
|Receiver jumps|raw8|262144|232886|0|0.0597921368|0.0358000256|

*Birth-score counts derived from the identical matched raw proposal IDs,not an independent GPU timer.

|Track|Metric|Rotate minus matched-budget MSE|Block SE|95% paired interval|
|---|---|---:|---:|---|
|Stationary|instant|-0.00213339086|0.000107053635|[-0.00238653248,-0.00188024924]|
|Stationary|rolling|0.00163562976|2.48865909e-05|[0.00157678232,0.00169447719]|
|Smooth|instant|-0.00103855949|6.24759417e-05|[-0.00118629162,-0.000890827366]|
|Smooth|rolling|0.000821354431|2.80241976e-05|[0.000755087734,0.000887621129]|
|Receiver jumps|instant|-0.00156571342|8.85579146e-05|[-0.00177511961,-0.00135630722]|
|Receiver jumps|rolling|0.000668132058|3.73100534e-05|[0.000579907801,0.000756356315]|

Intervals use8 paired seed blocks,Student t(df7);common seeds across tracks are not independent scenes.
