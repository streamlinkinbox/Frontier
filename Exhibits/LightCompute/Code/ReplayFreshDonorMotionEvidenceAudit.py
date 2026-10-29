"""Recompute retained cohort aggregates and inspect the actual motion/reset controls."""
import math
from pathlib import Path
import statistics
import sys

if len(sys.argv) != 3:
    raise SystemExit('Usage: ReplayFreshDonorMotionEvidenceAudit.py MEASURED_LOG MODE_CONTRASTS')
Lines = Path(sys.argv[1]).read_text().splitlines()
Batches, Results, Comparisons = {}, {}, {}
Continuations, Sequences, Links, References, Exits = [], [], [], [], []
for Line in Lines:
    Prefix, _, Remainder = Line.partition(' ')
    if Prefix not in ('REPLAY_FRESH_MOTION_BATCH', 'REPLAY_FRESH_MOTION_RESULT',
                      'REPLAY_FRESH_MOTION_COMPARE', 'REPLAY_FRESH_MOTION_CONTINUATION',
                      'REPLAY_FRESH_MOTION_SEQUENCE', 'REPLAY_GUARD_LINKS',
                      'REPLAY_FRESH_MOTION_REFERENCE', 'REPLAY_FRESH_MOTION_ARM_EXIT'):
        continue
    Row = dict(Field.split('=', 1) for Field in Remainder.split())
    if Prefix in ('REPLAY_FRESH_MOTION_BATCH', 'REPLAY_FRESH_MOTION_RESULT'):
        Key = tuple(Row[Name] for Name in ('arm', 'scene', 'group', 'mode', 'floor'))
        if Prefix == 'REPLAY_FRESH_MOTION_BATCH':
            Key += (int(Row['batch']),)
            Destination = Batches
        else:
            Destination = Results
        if Key in Destination:
            raise SystemExit('Duplicate measurement: ' + str(Key))
        Destination[Key] = Row
    elif Prefix == 'REPLAY_FRESH_MOTION_COMPARE':
        Key = tuple(Row[Name] for Name in ('scene', 'group', 'mode', 'floor'))
        if Key in Comparisons:
            raise SystemExit('Duplicate paired comparison: ' + str(Key))
        Comparisons[Key] = Row
    else:
        {'REPLAY_FRESH_MOTION_CONTINUATION': Continuations, 'REPLAY_FRESH_MOTION_SEQUENCE': Sequences,
         'REPLAY_GUARD_LINKS': Links, 'REPLAY_FRESH_MOTION_REFERENCE': References,
         'REPLAY_FRESH_MOTION_ARM_EXIT': Exits}[Prefix].append(Row)


def Close(Actual, Expected, Description):
    if not math.isclose(float(Actual), float(Expected), rel_tol=1e-7, abs_tol=1e-9):
        raise SystemExit(f'Aggregate mismatch {Description}: {Actual} versus {Expected}')


def StandardError(Samples):
    Mean = math.fsum(Samples) / len(Samples)
    return math.sqrt(math.fsum((Sample - Mean) ** 2 for Sample in Samples) / (len(Samples) * (len(Samples) - 1)))


if (len(Batches), len(Results), len(Comparisons)) != (4608, 96, 48):
    raise SystemExit('The retained experiment is incomplete')
for Key, Row in Results.items():
    Cohorts = [Batches[Key + (Index,)] for Index in range(48)]
    Means = [float(Cohort['mean']) for Cohort in Cohorts]
    Squared = [float(Cohort['mse']) for Cohort in Cohorts]
    Mean = math.fsum(Means) / 48
    Mse = math.fsum(Squared) / 48
    SE = StandardError(Means)
    for Actual, Expected, Name in ((Row['mean'], Mean, 'mean'), (Row['batch_se'], SE, 'mean SE'),
                                   (Row['mse'], Mse, 'MSE'), (Row['mse_se'], StandardError(Squared), 'MSE SE'),
                                   (Row['rmse'], math.sqrt(Mse), 'RMSE'),
                                   (Row['max_pixel'], max(float(C['max_pixel']) for C in Cohorts), 'maximum')):
        Close(Actual, Expected, (Key, Name))
    Gate = abs(Mean - float(Row['reference'])) <= 5 * SE + .001
    if int(Row['mean_gate']) != Gate:
        raise SystemExit('Mean gate mismatch: ' + str(Key))

ControlHashes = 0
for Key, Row in Comparisons.items():
    Scene, Group, Mode, Floor = Key
    OriginalKey = ('baseline', Scene, Group, Mode, Floor)
    CandidateKey = ('fresh', Scene, Group, Mode, Floor)
    Differences = [float(Batches[CandidateKey + (Index,)]['mse']) -
                   float(Batches[OriginalKey + (Index,)]['mse']) for Index in range(48)]
    Delta = math.fsum(Differences) / 48
    SE = StandardError(Differences)
    Close(Row['delta_mse'], Delta, (Key, 'paired delta'))
    Close(Row['delta_se'], SE, (Key, 'paired SE'))
    Gate = 'win' if Delta + 3 * SE < 0 else 'regression' if Delta - 3 * SE > 0 else 'inconclusive'
    Valid = Results[OriginalKey]['mean_gate'] == Results[CandidateKey]['mean_gate'] == '1'
    if Row['gate'] != Gate or int(Row['valid_mean_controls']) != Valid:
        raise SystemExit('Paired gate mismatch: ' + str(Key))
    if Mode in ('fresh1', 'fresh4', 'temporal1'):
        for Index in range(48):
            if Batches[OriginalKey + (Index,)]['hash'] != Batches[CandidateKey + (Index,)]['hash']:
                raise SystemExit('Negative-control hash mismatch: ' + str(Key))
            ControlHashes += 1
if ControlHashes != 1152:
    raise SystemExit('Unexpected negative-control count')
ModeRows = []
for Line in Path(sys.argv[2]).read_text().splitlines():
    if Line.startswith('REPLAY_FRESH_MOTION_MODE '):
        ModeRows.append(dict(Field.split('=', 1) for Field in Line.split()[1:]))
if len(ModeRows) != 64:
    raise SystemExit('Exploratory mode contrasts are incomplete')
ModeKeys = set()
for Row in ModeRows:
    Prefix = tuple(Row[Name] for Name in ('arm', 'scene', 'group'))
    Floor = Row['floor']
    ControlKey = Prefix + (Row['control'], Floor)
    ReuseKey = Prefix + (Row['reuse'], Floor)
    Identity = Prefix + (Floor, Row['control'], Row['reuse'])
    if Identity in ModeKeys:
        raise SystemExit('Duplicate exploratory mode contrast: ' + str(Identity))
    ModeKeys.add(Identity)
    Differences = [float(Batches[ReuseKey + (Index,)]['mse']) -
                   float(Batches[ControlKey + (Index,)]['mse']) for Index in range(48)]
    Delta = math.fsum(Differences) / 48
    SE = StandardError(Differences)
    Close(Row['delta_mse'], Delta, (Identity, 'mode delta'))
    Close(Row['delta_se'], SE, (Identity, 'mode SE'))
    Gate = 'win' if Delta + 3 * SE < 0 else 'regression' if Delta - 3 * SE > 0 else 'inconclusive'
    Valid = Results[ControlKey]['mean_gate'] == Results[ReuseKey]['mean_gate'] == '1'
    if Row['gate'] != Gate or int(Row['valid_mean_controls']) != Valid:
        raise SystemExit('Exploratory mode gate mismatch: ' + str(Identity))
    for PrefixName, Key in (('control', ControlKey), ('reuse', ReuseKey)):
        for OutputName, ResultName in (('rmse', 'rmse'), ('final_rays', 'mean_rays'), ('warm_rays', 'warm_rays')):
            Close(Row[PrefixName + '_' + OutputName], Results[Key][ResultName], (Identity, OutputName))
if len(References) != 8 or any(abs(float(R['reference']) - float(R['coarse'])) >= .0002 for R in References):
    raise SystemExit('Reference quadrature check failed')
if len(Continuations) != 4 or any(R['continued_frames'] != '864' or R['capped_frames'] != '864' or
                                  R['minimum_temporal'] != '1024' for R in Continuations):
    raise SystemExit('Moving direct-recorder continuation control failed')
if len(Sequences) != 4 or any(R['changed_resets'] != '4' or R['explicit_equal'] != '1' or
                              R['changed_temporal'] != '0' or R['stationary_continuation'] != '1' or
                              R['stationary_temporal'] != '1024' for R in Sequences):
    raise SystemExit('Sequence reset/continuation control failed')
if len(Links) != 4 or any(R['eligible'] != R['resolve_links'] for R in Links):
    raise SystemExit('Spatial link audit failed')
if {(R['arm'], R['status']) for R in Exits} != {('baseline', '1'), ('fresh', '1')} or len(Exits) != 2:
    raise SystemExit('Expected both measured arms to retain their mean-gate failures')
Failures = [Key for Key, Row in Results.items() if Row['mean_gate'] == '0']
if len(Failures) != 9 or sum(Key[0] == 'fresh' for Key in Failures) != 2:
    raise SystemExit('Unexpected retained mean-gate outcome')
print('REPLAY_FRESH_MOTION_EVIDENCE aggregates=96 paired_comparisons=48 mode_contrasts=64 cohorts=4608 hashes=1152')
print('REPLAY_FRESH_MOTION_EVIDENCE references=8 continuations=4 sequences=4 link_audits=4')
print('REPLAY_FRESH_MOTION_EVIDENCE mean_failures=9 candidate_failures=2 expected_arm_exits=1,1')
print('PASS ReplayFreshDonorMotionEvidenceAudit arithmetic and retained controls (mean failures remain)')
