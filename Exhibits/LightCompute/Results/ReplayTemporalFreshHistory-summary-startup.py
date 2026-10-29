"""Check and pair the preregistered raw-seed temporal-history holdout."""
import math
from pathlib import Path
import statistics
import sys

if len(sys.argv) != 3:
    raise SystemExit('Usage: ReplayTemporalFreshHistorySummary.py NEW_LOG PRIOR_UNINSTRUMENTED_LOG')
Text, Prior = (Path(Name).read_text() for Name in sys.argv[1:])
Arms = ('baseline', 'raw')
Scenarios = (('motion_mixed', ('dim', 'metal')), ('motion_visibility', ('near', 'far')),
             ('stationary_mixed', ('dim', 'metal')), ('stationary_visibility', ('near', 'far')))
Modes = ('fresh1', 'fresh2', 'fresh4', 'temporal1', 'temporal4')
Floors = ('default', 'lower')
Batches, Results, Historical, Previous = {}, {}, {}, {}


def Fields(Line):
    return dict(Field.split('=', 1) for Field in Line.split()[1:])


def Insert(Store, Key, Row):
    if Key in Store:
        raise SystemExit('Duplicate experiment row: ' + str(Key))
    Store[Key] = Row


for Line in Text.splitlines():
    if Line.startswith(('REPLAY_TEMPORAL_BATCH ', 'REPLAY_TEMPORAL_RESULT ')):
        Row = Fields(Line)
        Key = tuple(Row[Name] for Name in ('arm', 'scenario', 'group', 'mode', 'floor'))
        if Line.startswith('REPLAY_TEMPORAL_BATCH '):
            Insert(Batches, Key + (int(Row['batch']),), Row)
        else:
            Insert(Results, Key, Row)
    elif Line.startswith('REPLAY_TEMPORAL_HISTORICAL '):
        Row = Fields(Line)
        Key = tuple(Row[Name] for Name in ('arm', 'scene', 'group', 'mode', 'floor')) + (int(Row['batch']),)
        Insert(Historical, Key, Row)
for Line in Prior.splitlines():
    if Line.startswith('REPLAY_FRESH_MOTION_BATCH '):
        Row = Fields(Line)
        if Row['arm'] == 'baseline' and Row['mode'] in ('fresh1', 'fresh4', 'temporal1') and int(Row['batch']) < 8:
            Key = tuple(Row[Name] for Name in ('arm', 'scene', 'group', 'mode', 'floor')) + (int(Row['batch']),)
            Insert(Previous, Key, Row)
if (len(Results), len(Batches), len(Historical), len(Previous)) != (160, 7680, 384, 192):
    raise SystemExit('Incomplete experiment: '
                     f'results={len(Results)} cohorts={len(Batches)} historical={len(Historical)} prior={len(Previous)}')

Failures = []
FreshControls = HistoricalControls = 0
for Key, Row in Results.items():
    if Row['mean_gate'] != '1':
        Failures.append('mean ' + '/'.join(Key))
        print('REPLAY_TEMPORAL_MEAN_FAILURE ' + ' '.join(f'{Name}={Value}' for Name, Value in zip(
            ('arm', 'scenario', 'group', 'mode', 'floor'), Key)) +
              f" reference={Row['reference']} mean={Row['mean']} batch_se={Row['batch_se']}")

for Key, Row in Historical.items():
    Arm, Scene, Group, Mode, Floor, Batch = Key
    if Arm == 'raw' and Mode == 'temporal1':
        continue
    Old = Previous[('baseline', Scene, Group, Mode, Floor, Batch)]
    HistoricalControls += 1
    if Row['hash'] != Old['hash']:
        Failures.append('historical hash ' + '/'.join(map(str, Key)))
        print(f"REPLAY_TEMPORAL_CONTROL_FAILURE kind=historical arm={Arm} scene={Scene} group={Group} "
              f"mode={Mode} floor={Floor} batch={Batch} actual={Row['hash']} old={Old['hash']}")


def Compare(Scenario, Group, Floor, LeftArm, LeftMode, RightArm, RightMode, Prefix):
    Left = Results[(LeftArm, Scenario, Group, LeftMode, Floor)]
    Right = Results[(RightArm, Scenario, Group, RightMode, Floor)]
    if Left['reference'] != Right['reference']:
        raise SystemExit('Paired reference mismatch')
    Differences = []
    for Batch in range(48):
        Old = Batches[(LeftArm, Scenario, Group, LeftMode, Floor, Batch)]
        New = Batches[(RightArm, Scenario, Group, RightMode, Floor, Batch)]
        Differences.append(float(New['mse']) - float(Old['mse']))
    Delta = statistics.mean(Differences)
    SE = statistics.stdev(Differences) / math.sqrt(len(Differences))
    Gate = 'win' if Delta + 3 * SE < 0 else 'regression' if Delta - 3 * SE > 0 else 'inconclusive'
    Valid = int(Left['mean_gate'] == '1' and Right['mean_gate'] == '1')
    print(f'{Prefix} scenario={Scenario} group={Group} floor={Floor} left={LeftArm}:{LeftMode} '
          f"right={RightArm}:{RightMode} left_rmse={Left['rmse']} right_rmse={Right['rmse']} "
          f'delta_mse={Delta:.12g} delta_se={SE:.12g} gate={Gate} valid_mean_controls={Valid} '
          f"left_final_rays={Left['mean_rays']} right_final_rays={Right['mean_rays']} "
          f"left_warm_rays={Left['warm_rays']} right_warm_rays={Right['warm_rays']} "
          f"left_triangles={Left['mean_triangles']} right_triangles={Right['mean_triangles']} "
          f"left_max={Left['max_pixel']} right_max={Right['max_pixel']}")


for Scenario, Groups in Scenarios:
    for Group in Groups:
        for Mode in Modes:
            for Floor in Floors:
                Compare(Scenario, Group, Floor, 'baseline', Mode, 'raw', Mode, 'REPLAY_TEMPORAL_COMPARE')
                if Mode.startswith('fresh'):
                    for Batch in range(48):
                        Original = Batches[('baseline', Scenario, Group, Mode, Floor, Batch)]
                        Candidate = Batches[('raw', Scenario, Group, Mode, Floor, Batch)]
                        FreshControls += 1
                        if Original['hash'] != Candidate['hash']:
                            Failures.append(f'fresh hash {Scenario}/{Group}/{Mode}/{Floor}/{Batch}')
                            print(f'REPLAY_TEMPORAL_CONTROL_FAILURE kind=fresh scenario={Scenario} group={Group} '
                                  f'mode={Mode} floor={Floor} batch={Batch}')
        for Floor in Floors:
            Compare(Scenario, Group, Floor, 'raw', 'fresh2', 'raw', 'temporal1', 'REPLAY_TEMPORAL_FRESH2_COMPARE')
            Compare(Scenario, Group, Floor, 'raw', 'fresh4', 'raw', 'temporal4', 'REPLAY_TEMPORAL_FRESH4_COMPARE')

for Arm in Arms:
    Complete = Text.count(f'PASS ReplayTemporalFreshHistoryValidation arm={Arm}') == 1
    ExitLines = [Line for Line in Text.splitlines() if Line.startswith(f'REPLAY_TEMPORAL_ARM_EXIT arm={Arm} ')]
    ExitZero = ExitLines == [f'REPLAY_TEMPORAL_ARM_EXIT arm={Arm} status=0']
    Oracle = Text.count(f'REPLAY_TEMPORAL_ORACLE arm={Arm} ') == 1
    Guides = Text.count(f'REPLAY_TEMPORAL_GUIDES arm={Arm} ') == 1
    print(f'REPLAY_TEMPORAL_ARM_CHECK arm={Arm} completion_marker={int(Complete)} exit_zero={int(ExitZero)} '
          f'oracle={int(Oracle)} guides={int(Guides)}')
    if not Complete or not ExitZero or not Oracle or not Guides:
        Failures.append('arm ' + Arm)
print(f'REPLAY_TEMPORAL_CONTROLS fresh_group_hashes={FreshControls} historical_group_hashes={HistoricalControls} '
      f'mean_gates={len(Results)} failed_mean_gates={sum(Failure.startswith("mean ") for Failure in Failures)} '
      f'failed_checks={len(Failures)}')
if Failures:
    print('FAIL ReplayTemporalFreshHistoryPairedSummary: at least one prespecified gate or arm failed')
    raise SystemExit(1)
print('PASS ReplayTemporalFreshHistoryPairedSummary')
