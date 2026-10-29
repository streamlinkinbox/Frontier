"""Exercise completeness, correctness, hash and exit checks on synthetic cohort logs."""
from pathlib import Path
import subprocess
import sys
import tempfile

Summary = Path(__file__).with_name('ReplayFreshDonorMotionSummary.py')
Scenes = (('mixed', ('dim', 'metal')), ('visibility', ('near', 'far')))
Modes = ('fresh1', 'fresh4', 'temporal1', 'spatial4', 'combined1', 'combined4')


def ConstructCohortLog(Defect):
    Lines = []
    for Arm in ('baseline', 'fresh'):
        for Scene, Groups in Scenes:
            for Group in Groups:
                for Mode in Modes:
                    for Floor in ('default', 'lower'):
                        Key = f'arm={Arm} scene={Scene} group={Group} mode={Mode} floor={Floor}'
                        Selected = Arm == 'fresh' and Scene == 'mixed' and Group == 'dim' and Mode == 'combined1'
                        Gate = 0 if Defect == 'mean' and Selected and Floor == 'default' else 1
                        Lines.append(f'REPLAY_FRESH_MOTION_RESULT {Key} reference=1 mean=1 batch_se=.01 '
                                     f'rmse=1 max_pixel=2 mean_rays=10 warm_rays=20 mean_triangles=5 '
                                     f'mean_gate={Gate}')
                        for Batch in range(48):
                            Hash = 'different' if (Defect == 'hash' and Arm == 'fresh' and Scene == 'mixed' and
                                                   Group == 'dim' and Mode == 'fresh4' and Floor == 'default' and
                                                   Batch == 0) else 'same'
                            Mse = .5 if Selected else 1
                            Lines.append(f'REPLAY_FRESH_MOTION_BATCH {Key} batch={Batch} mse={Mse} hash={Hash}')
        Lines.append(f'PASS ReplayFreshDonorMotionValidation arm={Arm}')
        Status = 134 if Defect == 'exit' and Arm == 'fresh' else 0
        Lines.append(f'REPLAY_FRESH_MOTION_ARM_EXIT arm={Arm} status={Status}')
    if Defect == 'truncated':
        Lines = [Line for Line in Lines if not Line.startswith('REPLAY_FRESH_MOTION_RESULT arm=baseline')][1:]
    if Defect == 'duplicate_exit':
        Lines.append('REPLAY_FRESH_MOTION_ARM_EXIT arm=fresh status=0')
    return '\n'.join(Lines) + '\n'


Cases = (
    ('complete', 0, 'matching_group_image_hashes=1152', 'gate=win valid_mean_controls=1'),
    ('mean', 1, 'failed_mean_gates=1 candidate_mean_failures=1', 'gate=win valid_mean_controls=0'),
    ('hash', 1, 'Negative-control hash mismatch', None),
    ('truncated', 1, 'Incomplete experiment', None),
    ('exit', 1, 'arm=fresh completion_marker=1 exit_zero=0', None),
    ('duplicate_exit', 1, 'arm=fresh completion_marker=1 exit_zero=0', None),
)
with tempfile.TemporaryDirectory() as Directory:
    Input = Path(Directory) / 'synthetic.txt'
    for Defect, Expected, First, Second in Cases:
        Input.write_text(ConstructCohortLog(Defect))
        Run = subprocess.run([sys.executable, str(Summary), str(Input)], capture_output=True, text=True, check=False)
        Combined = Run.stdout + Run.stderr
        if Run.returncode != Expected or First not in Combined or (Second and Second not in Combined):
            raise SystemExit(f'Summary audit failed: {Defect}; exit={Run.returncode}\n{Combined[-2500:]}')
        print(f'REPLAY_FRESH_MOTION_SUMMARY_AUDIT case={Defect} exit={Run.returncode} expected={Expected}')
print('PASS ReplayFreshDonorMotionSummaryAudit')
