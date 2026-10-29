"""Independently recalculate the paired evidence; report verified rejections without promoting them."""
import hashlib
import math
from pathlib import Path
import subprocess
import sys
import tempfile

if len(sys.argv) != 3:
    raise SystemExit('Usage: ReplayTemporalFreshHistoryEvidenceAudit.py MEASURED_LOG PRIOR_LOG')
Log, Prior = (Path(Name) for Name in sys.argv[1:])
Lines = Log.read_text().splitlines()
Stores = {Name: {} for Name in ('BATCH', 'RESULT', 'HISTORICAL', 'COMPARE', 'FRESH2_COMPARE',
                                'FRESH4_COMPARE', 'DIAGNOSTIC', 'REFERENCE', 'CONTINUATION', 'GUIDES', 'ORACLE',
                                'ARM_EXIT')}
for Line in Lines:
    Prefix, _, Rest = Line.partition(' ')
    if not Prefix.startswith('REPLAY_TEMPORAL_'):
        continue
    Kind = Prefix.removeprefix('REPLAY_TEMPORAL_')
    if Kind not in Stores:
        continue
    Row = dict(Field.split('=', 1) for Field in Rest.split())
    if Kind in ('BATCH', 'RESULT'):
        Key = tuple(Row[Name] for Name in ('arm', 'scenario', 'group', 'mode', 'floor'))
        if Kind == 'BATCH':
            Key += (int(Row['batch']),)
    elif Kind == 'HISTORICAL':
        Key = tuple(Row[Name] for Name in ('arm', 'scene', 'group', 'mode', 'floor')) + (int(Row['batch']),)
    elif Kind == 'COMPARE':
        Key = tuple(Row[Name] for Name in ('scenario', 'group', 'floor', 'left', 'right'))
    elif Kind in ('FRESH2_COMPARE', 'FRESH4_COMPARE'):
        Key = tuple(Row[Name] for Name in ('scenario', 'group', 'floor'))
    elif Kind == 'DIAGNOSTIC':
        Key = tuple(Row[Name] for Name in ('arm', 'scenario', 'group', 'floor'))
    elif Kind == 'REFERENCE':
        Key = tuple(Row[Name] for Name in ('arm', 'scenario', 'group'))
    elif Kind == 'CONTINUATION':
        Key = tuple(Row[Name] for Name in ('arm', 'scenario'))
    else:
        Key = (Row['arm'],)
    if Key in Stores[Kind]:
        raise SystemExit(f'Duplicate {Kind}: {Key}')
    Stores[Kind][Key] = Row
Batches, Results = Stores['BATCH'], Stores['RESULT']
ExpectedCounts = {'BATCH': 7680, 'RESULT': 160, 'HISTORICAL': 384, 'COMPARE': 80,
                  'FRESH2_COMPARE': 16, 'FRESH4_COMPARE': 16, 'DIAGNOSTIC': 32, 'REFERENCE': 16,
                  'CONTINUATION': 8, 'GUIDES': 2, 'ORACLE': 2, 'ARM_EXIT': 2}
if any(len(Stores[Kind]) != Expected for Kind, Expected in ExpectedCounts.items()):
    raise SystemExit(f'Incomplete retained evidence: {[(K, len(V)) for K, V in Stores.items()]}')


def Close(Actual, Expected, Description):
    if not math.isclose(float(Actual), float(Expected), rel_tol=2e-7, abs_tol=2e-9):
        raise SystemExit(f'Mismatch {Description}: {Actual} versus {Expected}')


def SE(Values):
    Mean = math.fsum(Values) / len(Values)
    return math.sqrt(math.fsum((Value - Mean) ** 2 for Value in Values) / (len(Values) * (len(Values) - 1)))


for Key, Row in Results.items():
    Cohorts = [Batches[Key + (Index,)] for Index in range(48)]
    Means = [float(Cohort['mean']) for Cohort in Cohorts]
    Squared = [float(Cohort['mse']) for Cohort in Cohorts]
    Mean, Mse = math.fsum(Means) / 48, math.fsum(Squared) / 48
    StdMean, StdMse = SE(Means), SE(Squared)
    for Field, Value in (('mean', Mean), ('batch_se', StdMean), ('mse', Mse), ('mse_se', StdMse),
                         ('rmse', math.sqrt(Mse)), ('max_pixel', max(float(C['max_pixel']) for C in Cohorts))):
        Close(Row[Field], Value, (Key, Field))
    Gate = abs(Mean - float(Row['reference'])) <= 5 * StdMean + .001
    if int(Row['mean_gate']) != Gate:
        raise SystemExit('Mean gate mismatch: ' + str(Key))

for Kind in ('COMPARE', 'FRESH2_COMPARE', 'FRESH4_COMPARE'):
    for Identity, Row in Stores[Kind].items():
        Scenario, Group, Floor = (Row[Name] for Name in ('scenario', 'group', 'floor'))
        LeftArm, LeftMode = Row['left'].split(':')
        RightArm, RightMode = Row['right'].split(':')
        LeftKey, RightKey = (LeftArm, Scenario, Group, LeftMode, Floor), (RightArm, Scenario, Group, RightMode, Floor)
        Deltas = [float(Batches[RightKey + (Index,)]['mse']) - float(Batches[LeftKey + (Index,)]['mse'])
                  for Index in range(48)]
        Delta, Std = math.fsum(Deltas) / 48, SE(Deltas)
        Close(Row['delta_mse'], Delta, (Identity, 'paired delta'))
        Close(Row['delta_se'], Std, (Identity, 'paired SE'))
        Gate = 'win' if Delta + 3 * Std < 0 else 'regression' if Delta - 3 * Std > 0 else 'inconclusive'
        Valid = Results[LeftKey]['mean_gate'] == Results[RightKey]['mean_gate'] == '1'
        if Row['gate'] != Gate or int(Row['valid_mean_controls']) != Valid:
            raise SystemExit('Comparison classification mismatch: ' + str(Identity))
        for Side, Key in (('left', LeftKey), ('right', RightKey)):
            for Name, Result in (('rmse', 'rmse'), ('final_rays', 'mean_rays'), ('warm_rays', 'warm_rays'),
                                 ('triangles', 'mean_triangles'), ('max', 'max_pixel')):
                Close(Row[Side + '_' + Name], Results[Key][Result], (Identity, Name))

FreshMismatch = FreshCount = 0
for Key, Row in Batches.items():
    Arm, Scenario, Group, Mode, Floor, Batch = Key
    if Arm == 'baseline' and Mode.startswith('fresh'):
        FreshCount += 1
        FreshMismatch += Row['hash'] != Batches[('raw', Scenario, Group, Mode, Floor, Batch)]['hash']
Previous = {}
for Line in Prior.read_text().splitlines():
    if Line.startswith('REPLAY_FRESH_MOTION_BATCH '):
        Row = dict(Field.split('=', 1) for Field in Line.split()[1:])
        if Row['arm'] == 'baseline' and Row['mode'] in ('fresh1', 'fresh4', 'temporal1') and int(Row['batch']) < 8:
            Key = tuple(Row[Name] for Name in ('arm', 'scene', 'group', 'mode', 'floor')) + (int(Row['batch']),)
            if Key in Previous:
                raise SystemExit('Duplicate prior cohort')
            Previous[Key] = Row
HistoryMismatch = HistoryCount = 0
for Key, Row in Stores['HISTORICAL'].items():
    Arm, Scene, Group, Mode, Floor, Batch = Key
    if Arm == 'raw' and Mode == 'temporal1':
        continue
    HistoryCount += 1
    HistoryMismatch += Row['hash'] != Previous[('baseline', Scene, Group, Mode, Floor, Batch)]['hash']
if FreshCount != 2304 or HistoryCount != 320:
    raise SystemExit('Unexpected control count')

for Key, Row in Stores['REFERENCE'].items():
    if abs(float(Row['reference']) - float(Row['coarse'])) >= .0002:
        raise SystemExit('Reference quadrature did not converge: ' + str(Key))
for (Arm, Scenario), Row in Stores['CONTINUATION'].items():
    Cap = '1' if Scenario.startswith('motion_') else '64'
    if (Row['count'], Row['min_accepted'], Row['expected_cap']) != ('576', '1024', Cap):
        raise SystemExit('Unexpected history continuation: ' + str((Arm, Scenario)))
for (Arm,), Row in Stores['GUIDES'].items():
    if tuple(Row[Field] for Field in ('rejected', 'accepted', 'exact_fresh', 'reset', 'cap')) != \
            ('2', '1022', '2', '0', '1'):
        raise SystemExit('Guide rejection control failed: ' + Arm)
for (Arm,), Row in Stores['ORACLE'].items():
    if int(Row['pixels']) != 393216 or float(Row['max_relative_error']) >= 2e-5 or \
            int(Row['raw_seed_checks']) != (786432 if Arm == 'raw' else 0):
        raise SystemExit('Per-pixel selection/seed controls failed: ' + Arm)
for Key, Row in Stores['DIAGNOSTIC'].items():
    if int(Row['audited_pixels']) != 24576:
        raise SystemExit('Diagnostic pixel count failed: ' + str(Key))
    if Key[0] == 'raw' and (float(Row['mean_normalization']) != 1.0 or float(Row['max_normalization']) != 1.0 or
                            int(Row['high_normalization']) != 0):
        raise SystemExit('Raw normalization was not one: ' + str(Key))

Failures = [Key for Key, Row in Results.items() if Row['mean_gate'] == '0']
for Arm in ('baseline', 'raw'):
    HasFailures = any(Key[0] == Arm for Key in Failures)
    Status = Stores['ARM_EXIT'][(Arm,)]['status']
    Marker = f'PASS ReplayTemporalFreshHistoryValidation arm={Arm}' in Lines
    if Status != ('1' if HasFailures else '0') or Marker == HasFailures:
        raise SystemExit(f'Unexpected arm completion status: {Arm}/{Status}/{Marker}/{HasFailures}')

# Verify every captured input/module hash. The result report may edit the protocol after measurement;
# the saved frozen copy must still exactly match the original pre-run protocol hash.
Hashes, FrozenInputs = 0, 0
for Line in Lines:
    Tokens = Line.split(maxsplit=1)
    if len(Tokens) != 2 or len(Tokens[0]) != 64 or any(C not in '0123456789abcdef' for C in Tokens[0]):
        continue
    Digest, Name = Tokens
    PathName = Path(Name)
    if Name == 'Exhibits/LightCompute/ReplayTemporalFreshHistory.md':
        PathName = Path('Exhibits/LightCompute/Results/ReplayTemporalFreshHistory-protocol-frozen.md')
    if Name == 'Exhibits/LightCompute/Code/ReplayTemporalFreshHistoryValidation.h':
        PathName = Path('Exhibits/LightCompute/Results/ReplayTemporalFreshHistory-startup.h')
    if Name == 'Exhibits/LightCompute/Code/ReplayTemporalFreshHistorySummary.py':
        PathName = Path('Exhibits/LightCompute/Results/ReplayTemporalFreshHistory-summary-startup.py')
    if Name in ('Exhibits/LightCompute/ReplayTemporalFreshHistory.md',
                'Exhibits/LightCompute/Code/ReplayTemporalFreshHistoryValidation.h',
                'Exhibits/LightCompute/Code/ReplayTemporalFreshHistorySummary.py'):
        FrozenInputs += 1
    if not PathName.is_file() or hashlib.sha256(PathName.read_bytes()).hexdigest() != Digest:
        raise SystemExit('Captured source/module hash differs: ' + Name)
    Hashes += 1
if Hashes < 25:
    raise SystemExit('Too few captured source/module hashes')
with tempfile.TemporaryDirectory() as Directory:
    Original, Raw = Path(Directory) / 'Diagnostic.slang', Path(Directory) / 'Raw.slang'
    Generator = Path('Exhibits/LightCompute/Code/ReplayTemporalFreshHistoryCandidate.py')
    subprocess.run([sys.executable, str(Generator), str(Original), str(Raw)], check=True, capture_output=True)
    for Generated in (Original, Raw):
        Matches = [Line.split()[0] for Line in Lines if Line.endswith('/test-only-source/' + Generated.name)]
        if len(Matches) != 1 or hashlib.sha256(Generated.read_bytes()).hexdigest() != Matches[0]:
            raise SystemExit('Generated shader does not reproduce captured input: ' + Generated.name)
print(f'REPLAY_TEMPORAL_EVIDENCE aggregates={len(Results)} paired_comparisons={len(Stores["COMPARE"])} '
      f'fresh_work_contrasts={len(Stores["FRESH2_COMPARE"]) + len(Stores["FRESH4_COMPARE"])} cohorts={len(Batches)}')
print(f'REPLAY_TEMPORAL_EVIDENCE fresh_controls={FreshCount} fresh_mismatches={FreshMismatch} '
      f'historical_controls={HistoryCount} historical_mismatches={HistoryMismatch} '
      f'startup_hashes={Hashes} frozen_inputs={FrozenInputs}')
print(f'REPLAY_TEMPORAL_EVIDENCE mean_failures={len(Failures)} baseline_failures='
      f'{sum(Key[0] == "baseline" for Key in Failures)} raw_failures={sum(Key[0] == "raw" for Key in Failures)}')
print('REPLAY_TEMPORAL_EVIDENCE reference_checks=16 continuations=8 guide_checks=2 oracle_checks=2 '
      'diagnostics=32 regenerated_test_shaders=2')
print('PASS ReplayTemporalFreshHistoryEvidenceAudit (verified failures remain failures)')
