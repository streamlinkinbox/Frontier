"""Pair moving-receiver cohorts while preserving failed correctness gates."""
import math
from pathlib import Path
import statistics
import sys

if len(sys.argv) < 2:
    raise SystemExit('Usage: ReplayFreshDonorMotionSummary.py MEASURED_LOG [MEASURED_LOG ...]')
Text = '\n'.join(Path(Name).read_text() for Name in sys.argv[1:])
Arms = ('baseline', 'fresh')
ArmPassed = {Arm: Text.count(f'PASS ReplayFreshDonorMotionValidation arm={Arm}') == 1 for Arm in Arms}
ArmExitZero = {}
for Arm in Arms:
    Prefix = f'REPLAY_FRESH_MOTION_ARM_EXIT arm={Arm} status='
    ExitLines = [Line[len(Prefix):] for Line in Text.splitlines() if Line.startswith(Prefix)]
    ArmExitZero[Arm] = ExitLines == ['0']
Batches, Results = {}, {}
for Line in Text.splitlines():
    if not (Line.startswith('REPLAY_FRESH_MOTION_BATCH ') or Line.startswith('REPLAY_FRESH_MOTION_RESULT ')):
        continue
    Fields = dict(Field.split('=', 1) for Field in Line.split()[1:])
    Key = tuple(Fields[Name] for Name in ('arm', 'scene', 'group', 'mode', 'floor'))
    if Line.startswith('REPLAY_FRESH_MOTION_BATCH '):
        BatchKey = Key + (int(Fields['batch']),)
        if BatchKey in Batches:
            raise SystemExit('Duplicate cohort: ' + str(BatchKey))
        Batches[BatchKey] = Fields
    else:
        if Key in Results:
            raise SystemExit('Duplicate result: ' + str(Key))
        Results[Key] = Fields
if len(Results) != 96 or len(Batches) != 4608:
    raise SystemExit(f'Incomplete experiment: results={len(Results)} cohorts={len(Batches)}')

Failed = []
for Key, Row in Results.items():
    if Row['mean_gate'] != '1':
        Failed.append(Key)
        print(f"REPLAY_FRESH_MOTION_MEAN_FAILURE arm={Key[0]} scene={Key[1]} group={Key[2]} mode={Key[3]} "
              f"floor={Key[4]} reference={Row['reference']} mean={Row['mean']} batch_se={Row['batch_se']}")

Controls = 0
for Scene, Groups in (('mixed', ('dim', 'metal')), ('visibility', ('near', 'far'))):
    for Group in Groups:
        for Mode in ('fresh1', 'fresh4', 'temporal1', 'spatial4', 'combined1', 'combined4'):
            for Floor in ('default', 'lower'):
                Original = Results[('baseline', Scene, Group, Mode, Floor)]
                Candidate = Results[('fresh', Scene, Group, Mode, Floor)]
                if Original['reference'] != Candidate['reference']:
                    raise SystemExit('Paired reference mismatch')
                Differences = []
                for Batch in range(48):
                    OriginalBatch = Batches[('baseline', Scene, Group, Mode, Floor, Batch)]
                    CandidateBatch = Batches[('fresh', Scene, Group, Mode, Floor, Batch)]
                    Differences.append(float(CandidateBatch['mse']) - float(OriginalBatch['mse']))
                    if Mode in ('fresh1', 'fresh4', 'temporal1'):
                        if OriginalBatch['hash'] != CandidateBatch['hash']:
                            raise SystemExit(f'Negative-control hash mismatch: {Scene}/{Group}/{Mode}/{Floor}/{Batch}')
                        Controls += 1
                Delta = statistics.mean(Differences)
                SE = statistics.stdev(Differences) / math.sqrt(len(Differences))
                Gate = 'win' if Delta + 3 * SE < 0 else 'regression' if Delta - 3 * SE > 0 else 'inconclusive'
                Comparable = int(Original['mean_gate'] == '1' and Candidate['mean_gate'] == '1')
                print(f"REPLAY_FRESH_MOTION_COMPARE scene={Scene} group={Group} mode={Mode} floor={Floor} "
                      f"baseline_rmse={Original['rmse']} fresh_rmse={Candidate['rmse']} delta_mse={Delta:.12g} "
                      f"delta_se={SE:.12g} gate={Gate} valid_mean_controls={Comparable} "
                      f"baseline_final_rays={Original['mean_rays']} fresh_final_rays={Candidate['mean_rays']} "
                      f"baseline_warm_rays={Original['warm_rays']} fresh_warm_rays={Candidate['warm_rays']} "
                      f"baseline_triangles={Original['mean_triangles']} fresh_triangles={Candidate['mean_triangles']} "
                      f"baseline_max_pixel={Original['max_pixel']} fresh_max_pixel={Candidate['max_pixel']}")
for Arm in Arms:
    print(f'REPLAY_FRESH_MOTION_ARM_CHECK arm={Arm} completion_marker={int(ArmPassed[Arm])} '
          f'exit_zero={int(ArmExitZero[Arm])}')
print(f'REPLAY_FRESH_MOTION_CONTROLS matching_group_image_hashes={Controls} mean_gates={len(Results)} '
      f'failed_mean_gates={len(Failed)} candidate_mean_failures={sum(K[0] == "fresh" for K in Failed)}')
if Failed or not all(ArmPassed.values()) or not all(ArmExitZero.values()):
    print('FAIL ReplayFreshDonorMotionPairedSummary: at least one prespecified gate or arm failed')
    raise SystemExit(1)
print('PASS ReplayFreshDonorMotionPairedSummary')
