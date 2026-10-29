"""Pair held-out cohorts across the baseline and test-only guard; fail on incomplete controls."""
import math
from pathlib import Path
import statistics
import sys

if len(sys.argv) != 2:
    raise SystemExit('Usage: ReplayMaterialGuardSummary.py MEASURED_LOG')
Text = Path(sys.argv[1]).read_text()
for Arm in ('baseline', 'guard'):
    if f'PASS ReplayMaterialGuardValidation arm={Arm}' not in Text:
        raise SystemExit('Missing successful experiment arm: ' + Arm)

Batches = {}
Results = {}
for Line in Text.splitlines():
    if not (Line.startswith('REPLAY_GUARD_BATCH ') or Line.startswith('REPLAY_GUARD_RESULT ')):
        continue
    Fields = dict(Field.split('=', 1) for Field in Line.split()[1:])
    Key = tuple(Fields[Name] for Name in ('arm', 'scene', 'group', 'mode', 'floor'))
    if Line.startswith('REPLAY_GUARD_BATCH '):
        BatchKey = Key + (int(Fields['batch']),)
        if BatchKey in Batches:
            raise SystemExit('Duplicate batch: ' + str(BatchKey))
        Batches[BatchKey] = Fields
    else:
        if Key in Results:
            raise SystemExit('Duplicate result: ' + str(Key))
        Results[Key] = Fields
if len(Results) != 32 or len(Batches) != 1536:
    raise SystemExit(f'Incomplete experiment: results={len(Results)} batches={len(Batches)}')
if any(Row['mean_gate'] != '1' for Row in Results.values()):
    raise SystemExit('At least one independent mean gate failed')

Controls = 0
for Scene, Groups in (('mixed', ('dim', 'metal')), ('visibility', ('near', 'far'))):
    for Group in Groups:
        for Mode in ('fresh1', 'temporal_disk'):
            for Floor in ('default', 'lower'):
                Old = Results[('baseline', Scene, Group, Mode, Floor)]
                New = Results[('guard', Scene, Group, Mode, Floor)]
                if Old['reference'] != New['reference']:
                    raise SystemExit('Paired reference mismatch')
                Differences = []
                for Batch in range(48):
                    Base = Batches[('baseline', Scene, Group, Mode, Floor, Batch)]
                    Guard = Batches[('guard', Scene, Group, Mode, Floor, Batch)]
                    Differences.append(float(Guard['mse']) - float(Base['mse']))
                    if Mode == 'fresh1' or Scene == 'visibility':
                        if Base['hash'] != Guard['hash']:
                            raise SystemExit(f'Negative-control image hash mismatch: {Scene}/{Group}/{Mode}/{Floor}/{Batch}')
                        Controls += 1
                Delta = statistics.mean(Differences)
                SE = statistics.stdev(Differences) / math.sqrt(len(Differences))
                Gate = 'win' if Delta + 3 * SE < 0 else 'regression' if Delta - 3 * SE > 0 else 'inconclusive'
                print(f"REPLAY_GUARD_COMPARE scene={Scene} group={Group} mode={Mode} floor={Floor} "
                      f"baseline_rmse={Old['rmse']} guard_rmse={New['rmse']} delta_mse={Delta:.12g} "
                      f"delta_se={SE:.12g} gate={Gate} baseline_final_rays={Old['mean_rays']} "
                      f"guard_final_rays={New['mean_rays']} baseline_warm_rays={Old['warm_rays']} "
                      f"guard_warm_rays={New['warm_rays']} baseline_max_pixel={Old['max_pixel']} "
                      f"guard_max_pixel={New['max_pixel']}")
print(f'REPLAY_GUARD_CONTROLS matching_group_image_hashes={Controls} mean_gates={len(Results)}')
print('PASS ReplayMaterialGuardPairedSummary')
