"""Analyze complete failed arms without turning failed correctness gates into a pass."""
import math
from pathlib import Path
import statistics
import sys

if len(sys.argv) < 2:
    raise SystemExit('Usage: ReplayMaterialGuardFailureAnalysis.py MEASURED_LOG [MEASURED_LOG ...]')
Text = '\n'.join(Path(Name).read_text() for Name in sys.argv[1:])
ArmPassed = {Arm: f'PASS ReplayMaterialGuardValidation arm={Arm}' in Text for Arm in ('baseline', 'guard')}

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
Failed = []
for Key, Row in Results.items():
    if Row['mean_gate'] != '1':
        Failed.append(Key)
        print(f"REPLAY_GUARD_MEAN_FAILURE arm={Key[0]} scene={Key[1]} group={Key[2]} mode={Key[3]} "
              f"floor={Key[4]} reference={Row['reference']} mean={Row['mean']} batch_se={Row['batch_se']}")

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
print(f'REPLAY_GUARD_CONTROLS matching_group_image_hashes={Controls} '
      f'mean_gates={len(Results)} failed_mean_gates={len(Failed)}')
if Failed or not all(ArmPassed.values()):
    print('FAIL ReplayMaterialGuardPairedSummary: at least one prespecified correctness gate or arm failed')
    raise SystemExit(1)
print('PASS ReplayMaterialGuardPairedSummary')
