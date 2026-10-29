"""Reject missing/failed arms, false means and altered fresh/historical controls."""
from pathlib import Path
import subprocess
import sys
import tempfile

Summary = Path(__file__).with_name('ReplayTemporalFreshHistorySummary.py')
Scenarios = (('motion_mixed', ('dim', 'metal')), ('motion_visibility', ('near', 'far')),
             ('stationary_mixed', ('dim', 'metal')), ('stationary_visibility', ('near', 'far')))
Modes = ('fresh1', 'fresh2', 'fresh4', 'temporal1', 'temporal4')
Floors = ('default', 'lower')


def Build(Defect):
    New, Old = [], []
    for Arm in ('baseline', 'raw'):
        for Scenario, Groups in Scenarios:
            for Group in Groups:
                for Mode in Modes:
                    for Floor in Floors:
                        Key = f'arm={Arm} scenario={Scenario} group={Group} mode={Mode} floor={Floor}'
                        Gate = int(not (Defect == 'mean' and Arm == 'baseline' and Scenario == 'motion_mixed' and
                                        Group == 'metal' and Mode == 'temporal1' and Floor == 'lower'))
                        New.append(f'REPLAY_TEMPORAL_RESULT {Key} reference=1 mean=1 batch_se=.01 rmse=1 '
                                   f'mean_rays=10 warm_rays=20 mean_triangles=5 max_pixel=2 mean_gate={Gate}')
                        for Batch in range(48):
                            Different = (Defect == 'fresh_hash' and Arm == 'raw' and Scenario == 'motion_mixed' and
                                         Group == 'dim' and Mode == 'fresh1' and Floor == 'default' and Batch == 0)
                            if not (Defect == 'truncated' and Arm == 'raw' and Scenario == 'motion_mixed' and
                                    Group == 'dim' and Mode == 'fresh1' and Floor == 'default' and Batch == 0):
                                New.append(f'REPLAY_TEMPORAL_BATCH {Key} batch={Batch} mse=1 '
                                           f'hash={"bad" if Different else "same"}')
        for Scene, Groups in (('mixed', ('dim', 'metal')), ('visibility', ('near', 'far'))):
            for Group in Groups:
                for Mode in ('fresh1', 'fresh4', 'temporal1'):
                    for Floor in Floors:
                        for Batch in range(8):
                            Different = (Defect == 'historical_hash' and Arm == 'baseline' and Scene == 'mixed' and
                                         Group == 'dim' and Mode == 'temporal1' and Floor == 'default' and Batch == 0)
                            Key = f'scene={Scene} group={Group} mode={Mode} floor={Floor} batch={Batch}'
                            New.append(f'REPLAY_TEMPORAL_HISTORICAL arm={Arm} {Key} '
                                       f'hash={"bad" if Different else "same"}')
                            if Arm == 'baseline':
                                Old.append(f'REPLAY_FRESH_MOTION_BATCH arm=baseline {Key} hash=same')
        New.extend((f'PASS ReplayTemporalFreshHistoryValidation arm={Arm}', f'REPLAY_TEMPORAL_ORACLE arm={Arm} ',
                    f'REPLAY_TEMPORAL_GUIDES arm={Arm} ', f'REPLAY_TEMPORAL_ARM_EXIT arm={Arm} '
                    f'status={134 if Defect == "exit" and Arm == "raw" else 0}'))
    if Defect == 'duplicate_exit':
        New.append('REPLAY_TEMPORAL_ARM_EXIT arm=raw status=0')
    return '\n'.join(New) + '\n', '\n'.join(Old) + '\n'


Cases = (('complete', 0, 'fresh_group_hashes=2304 historical_group_hashes=320'),
         ('mean', 1, 'failed_mean_gates=1'),
         ('fresh_hash', 1, 'kind=fresh'),
         ('historical_hash', 1, 'kind=historical'),
         ('truncated', 1, 'Incomplete experiment'),
         ('exit', 1, 'arm=raw completion_marker=1 exit_zero=0'),
         ('duplicate_exit', 1, 'arm=raw completion_marker=1 exit_zero=0'))
with tempfile.TemporaryDirectory() as Directory:
    NewPath, OldPath = Path(Directory) / 'new', Path(Directory) / 'old'
    for Defect, Expected, Message in Cases:
        New, Old = Build(Defect)
        NewPath.write_text(New)
        OldPath.write_text(Old)
        Run = subprocess.run([sys.executable, str(Summary), str(NewPath), str(OldPath)],
                             capture_output=True, text=True, check=False)
        if Run.returncode != Expected or Message not in Run.stdout + Run.stderr:
            raise SystemExit(f'Audit failed: {Defect}; exit={Run.returncode}\n{(Run.stdout + Run.stderr)[-2500:]}')
        print(f'REPLAY_TEMPORAL_SUMMARY_AUDIT case={Defect} exit={Run.returncode} expected={Expected}')
print('PASS ReplayTemporalFreshHistorySummaryAudit')
