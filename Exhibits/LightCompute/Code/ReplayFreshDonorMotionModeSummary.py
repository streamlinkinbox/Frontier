"""Exploratory paired mode contrasts from the already-measured motion cohorts."""
import math
from pathlib import Path
import statistics
import sys

if len(sys.argv) != 2:
    raise SystemExit('Usage: ReplayFreshDonorMotionModeSummary.py MEASURED_LOG')
Batches, Results = {}, {}
for Line in Path(sys.argv[1]).read_text().splitlines():
    if not (Line.startswith('REPLAY_FRESH_MOTION_BATCH ') or Line.startswith('REPLAY_FRESH_MOTION_RESULT ')):
        continue
    Row = dict(Field.split('=', 1) for Field in Line.split()[1:])
    Key = tuple(Row[Name] for Name in ('arm', 'scene', 'group', 'mode', 'floor'))
    if Line.startswith('REPLAY_FRESH_MOTION_BATCH '):
        Key += (int(Row['batch']),)
        if Key in Batches:
            raise SystemExit('Duplicate cohort: ' + str(Key))
        Batches[Key] = Row
    else:
        if Key in Results:
            raise SystemExit('Duplicate result: ' + str(Key))
        Results[Key] = Row
if len(Results) != 96 or len(Batches) != 4608:
    raise SystemExit(f'Incomplete experiment: results={len(Results)} cohorts={len(Batches)}')

Contrasts = (('fresh1', 'combined1'), ('fresh4', 'combined4'),
             ('temporal1', 'combined1'), ('fresh4', 'spatial4'))
print('EXPLORATORY paired mode contrasts: not prespecified; unequal warm and final work; nominal 3-SE gates.')
for Arm in ('baseline', 'fresh'):
    for Scene, Groups in (('mixed', ('dim', 'metal')), ('visibility', ('near', 'far'))):
        for Group in Groups:
            for Floor in ('default', 'lower'):
                for ReferenceMode, ReuseMode in Contrasts:
                    ReferenceKey = (Arm, Scene, Group, ReferenceMode, Floor)
                    ReuseKey = (Arm, Scene, Group, ReuseMode, Floor)
                    Reference, Reuse = Results[ReferenceKey], Results[ReuseKey]
                    if Reference['reference'] != Reuse['reference']:
                        raise SystemExit('Mode reference mismatch')
                    Difference = [float(Batches[ReuseKey + (Batch,)]['mse']) -
                                  float(Batches[ReferenceKey + (Batch,)]['mse']) for Batch in range(48)]
                    Delta = statistics.mean(Difference)
                    SE = statistics.stdev(Difference) / math.sqrt(len(Difference))
                    Gate = 'win' if Delta + 3 * SE < 0 else 'regression' if Delta - 3 * SE > 0 else 'inconclusive'
                    Comparable = int(Reference['mean_gate'] == '1' and Reuse['mean_gate'] == '1')
                    print(f'REPLAY_FRESH_MOTION_MODE arm={Arm} scene={Scene} group={Group} floor={Floor} '
                          f'control={ReferenceMode} reuse={ReuseMode} control_rmse={Reference["rmse"]} '
                          f'reuse_rmse={Reuse["rmse"]} delta_mse={Delta:.12g} delta_se={SE:.12g} '
                          f'gate={Gate} valid_mean_controls={Comparable} '
                          f'control_final_rays={Reference["mean_rays"]} reuse_final_rays={Reuse["mean_rays"]} '
                          f'control_warm_rays={Reference["warm_rays"]} reuse_warm_rays={Reuse["warm_rays"]}')
print('PASS ReplayFreshDonorMotionModeSummary completeness (mean failures remain in validity fields)')
