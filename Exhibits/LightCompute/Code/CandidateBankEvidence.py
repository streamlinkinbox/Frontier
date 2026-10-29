# 🧩 Verify refresh cadence, moving-receiver means, error correlation and exact Vulkan readbacks.
from pathlib import Path
import hashlib
import math
import re
import struct
import subprocess
import sys
import tempfile
from PIL import Image, ImageDraw, ImageFont

Root = Path(__file__).resolve().parents[3]
Results = Root / 'Exhibits/LightCompute/Results'
Images = Root / 'Exhibits/LightCompute/Images'
Methods = ('area8', 'bank8', 'rotate4', 'rotate2', 'fresh6', 'fresh8')
Semantic, HashCounts = [], []
Measurements, Pairs = {}, []
for Mode in ('release', 'sanitized'):
    Text = (Results / f'CandidateBank-{Mode}.txt').read_text()
    Rows = [L for L in Text.splitlines() if L.startswith('CANDIDATE_BANK_')]
    assert Rows[-1] == 'CANDIDATE_BANK_DECISION correctness=1 promote=0 physical_gpu_timing=0'
    assert re.search(r'100% tests passed(?:, 0 tests failed)? out of 5', Text)
    assert not re.search(r'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer', Text)
    Hashes = re.findall(r'^([a-f0-9]{64})  (.+)$', Text, re.M)
    for Digest, Name in Hashes:
        assert hashlib.sha256((Root / Name).read_bytes()).hexdigest() == Digest, Name
    HashCounts.append(len(Hashes))
    References, MeanCount, CheckCount, PairCount = {}, 0, 0, 0
    for Line in Rows:
        F = dict(re.findall(r'(\w+)=([^ ]+)', Line))
        if Line.startswith('CANDIDATE_BANK_REFERENCE '):
            References[F['fixture'], F['pose']] = float(F['integral'])
        if Line.startswith('CANDIDATE_BANK_MEAN '):
            MeanCount += 1
            assert F['gate'] == '1'
            assert abs(float(F['mean']) - References[F['fixture'], F['pose']]) < 4 * float(F['nominal_se']) + .0001
        if Line.startswith('CANDIDATE_BANK_RESULT '):
            Measurements[F['fixture'], F['method']] = F
            assert all(math.isfinite(float(F[K])) for K in ('rmse', 'temporal_rmse', 'flicker_rmse', 'lag1'))
            assert abs(float(F['lag1'])) <= 1 and int(F['queries']) >= int(F['source_queries'])
            Expected = 32 * 1024 * 13 * (4 if F['method'] == 'rotate4' else 6 if F['method'] == 'rotate2' else 0)
            assert int(F['reused']) == Expected
        if Line.startswith('CANDIDATE_BANK_PAIRED '):
            PairCount += 1
            if Mode == 'release': Pairs.append(F)
            assert all(math.isfinite(float(F[K])) for K in ('instantaneous_mse', 'instantaneous_se', 'temporal_mse', 'temporal_se'))
        if Line.startswith('CANDIDATE_BANK_CHECK '):
            CheckCount += 1
            assert F['mean_gates'] == '96' and float(F['maximum_bank8_difference']) < 2e-6
            assert int(F['retained_slot_checks']) == 32 * 13 * 10 * 1024
    assert (len(References), MeanCount, CheckCount, PairCount) == (64, 384, 4, 16)
    Semantic.append(Rows)
assert Semantic[0] == Semantic[1] and len(Measurements) == 24
# Fresh controls must remain numerically identical to the preceding fixed-refresh experiment.
OldControls = []
for Line in (Results / 'CacheRefresh-release.txt').read_text().splitlines():
    if Line.startswith(('CACHE_REFRESH_MEAN ', 'CACHE_REFRESH_RESULT ')) and re.search(r'method=fresh[68] ', Line):
        OldControls.append(Line.replace('CACHE_REFRESH_', 'CANDIDATE_BANK_'))
NewControls = [Line for Line in Semantic[0] if Line.startswith(('CANDIDATE_BANK_MEAN ', 'CANDIDATE_BANK_RESULT '))
               and re.search(r'method=fresh[68] ', Line)]
assert OldControls == NewControls and len(NewControls) == 136
with tempfile.TemporaryDirectory(dir=Root / '_ClaudeScratch/tmp') as Temporary:
    Shader = Path(Temporary) / 'CandidateBank.slang'
    subprocess.run([sys.executable, str(Root / 'Exhibits/LightCompute/Code/CompileCandidateBank.py'), str(Shader)],
                   cwd=Root, check=True)
    for Mode in ('release', 'sanitized'):
        assert Shader.read_bytes() == (Root / f'_ClaudeScratch/build/candidate-bank-{Mode}/CandidateBank.slang').read_bytes()
    assert 'FreshComponent(Pixel, Method == 4 ? 6 : 8)' in Shader.read_text()
    assert 'TriangleOutgoing(Point) / float(Budget)' in Shader.read_text()

Images.mkdir(exist_ok=True)
Font = ImageFont.load_default(size=14)
Small = ImageFont.load_default(size=12)
Canvas = Image.new('RGB', (1200, 760), '#121923')
Pen = ImageDraw.Draw(Canvas)
Pen.text((18, 14), 'Actual Vulkan candidate-bank readbacks / .3 strip holdout / first cohort / NOT camera images', font=Font, fill='white')
Pen.text((18, 38), 'Linear grayscale x6, clipped / 32 x 32 independent reservoir trajectories per tile; no interpolation', font=Small, fill='white')
Labels = ('Inline area8', 'Full bank8', 'Rotate4', 'Rotate2', 'Fresh cosine6', 'Fresh cosine8')
for Method, Label in enumerate(Labels):
    Pen.text((180 + Method * 166, 69), Label, font=Small, fill='white')
SelectedPoses = (2, 6, 10, 14)
for Row, Pose in enumerate(SelectedPoses):
    Pen.text((16, 143 + 158 * Row), f'Pose {Pose}', font=Small, fill='white')
for Fixture in range(4):
    for Pose in range(16):
        A = Root / f'_ClaudeScratch/candidate-bank-capture-release/CandidateBank{Fixture}-{Pose}.f32'
        B = Root / f'_ClaudeScratch/candidate-bank-capture-sanitized/CandidateBank{Fixture}-{Pose}.f32'
        Raw = A.read_bytes()
        assert Raw == B.read_bytes() and len(Raw) == 6 * 1024 * 16
        for Method in range(6):
            Pixels = []
            for Sample in range(1024):
                Estimate, Queries, Reused, SourceQueries = struct.unpack_from('<4f', Raw, (Method * 1024 + Sample) * 16)
                assert all(math.isfinite(V) and V >= 0 for V in (Estimate, Queries, Reused, SourceQueries))
                Expected = (4 if Method == 2 else 6) if Method in (2, 3) and Pose not in (0, 9, 13) else 0
                assert Reused == Expected
                assert Queries == int(Queries) and SourceQueries == int(SourceQueries)
                if Method in (1, 2, 3): assert 0 <= Queries - SourceQueries <= 8
                if Method == 1:
                    assert abs(Estimate - struct.unpack_from('<f', Raw, Sample * 16)[0]) < 2e-6
                    assert Queries == struct.unpack_from('<f', Raw, Sample * 16 + 4)[0]
                Gray = min(255, max(0, round(Estimate * 6 * 255)))
                Pixels.append((Gray, Gray, Gray))
            if Fixture == 2 and Pose in SelectedPoses:
                Panel = Image.new('RGB', (32, 32)); Panel.putdata(Pixels)
                Name = Images / f'CandidateBank{Pose}-{Method}.png'; Panel.save(Name)
                with Image.open(Name) as Retained:
                    assert Retained.tobytes() == bytes(C for P in Pixels for C in P)
                Canvas.paste(Panel.resize((128, 128), Image.Resampling.NEAREST),
                             (180 + Method * 166, 94 + SelectedPoses.index(Pose) * 158))
Canvas.save(Images / 'CandidateBankReadbacks.png')

# Measured query/temporal-error frontier. Lower-left is preferable; no timing or confidence-bound claim.
Canvas = Image.new('RGB', (1200, 530), '#121923'); Pen = ImageDraw.Draw(Canvas)
Pen.text((20, 14), 'Measured temporal-error / query frontier relative to fresh8 / lower-left is preferable', font=Font, fill='white')
Colors = ('#b2becf', '#95b5db', '#77d3a6', '#e3bd79', '#c8a0ed', '#f1a187')
for Panel, Fixture in enumerate((0, 2)):
    Control = Measurements[str(Fixture), 'fresh8']
    Ratios = [(int(Measurements[str(Fixture), M]['queries']) / int(Control['queries']),
               float(Measurements[str(Fixture), M]['temporal_rmse']) / float(Control['temporal_rmse'])) for M in Methods]
    Left, Right, Top, Bottom = 85 + Panel * 600, 510 + Panel * 600, 100, 390
    XMin, XMax = .2, max(X for X, Y in Ratios) * 1.18
    YMin, YMax = min(Y for X, Y in Ratios) * .75, max(Y for X, Y in Ratios) * 1.18
    Pen.text((Left, 58), 'No strip' if Fixture == 0 else '.3 strip holdout', font=Font, fill='white')
    for Tick in range(5):
        X, Y = Left + Tick / 4 * (Right-Left), Bottom - Tick / 4 * (Bottom-Top)
        Pen.line((X, Top, X, Bottom), fill='#35404d'); Pen.line((Left, Y, Right, Y), fill='#35404d')
        Pen.text((X-14, Bottom+8), f'{XMin+Tick/4*(XMax-XMin):.2f}', font=Small, fill='white')
        Pen.text((Left-40, Y-8), f'{YMin+Tick/4*(YMax-YMin):.2f}', font=Small, fill='white')
    for Index, (Q, E) in enumerate(Ratios):
        X, Y = Left+(Q-XMin)/(XMax-XMin)*(Right-Left), Bottom-(E-YMin)/(YMax-YMin)*(Bottom-Top)
        Pen.ellipse((X-4, Y-4, X+4, Y+4), fill=Colors[Index])
        Pen.text((X+7, Y+(12 if Index==1 else -15)), Methods[Index], font=Small, fill=Colors[Index])
    Pen.text((Left, 80), '16-pose mean-residual RMSE ratio', font=Small, fill='white')
    Pen.text((Left+90, 430), 'BVH query ratio', font=Small, fill='white')
Pen.text((20, 477), 'Independent cohorts; cold/refresh/revision costs included. Different axis ranges per panel.', font=Small, fill='white')
Pen.text((20, 501), 'Conditional trajectory diagnostic, not TAA reconstruction or physical-GPU time. Paired MSE uncertainty retained in logs.', font=Small, fill='white')
Canvas.save(Images / 'CandidateBankFrontier.png')

# Independently verify actual area densities, current geometry, null normalization and unchanged retained words.
MaximumDensityError, MaximumEstimateError, PayloadChecks, PreservedChecks = 0., 0., 0, 0
NullDivisorDifferences = 0
for Fixture, Blocker in enumerate((0., .12, .3, .6)):
    Blocker = struct.unpack('<f', struct.pack('<f', Blocker))[0]
    Previous = None
    for Pose in range(16):
        Paths = [Root / f'_ClaudeScratch/candidate-bank-capture-{Mode}/CandidateBankPayload{Fixture}-{Pose}.f32'
                 for Mode in ('release', 'sanitized')]
        Payload = Paths[0].read_bytes()
        assert Payload == Paths[1].read_bytes() and len(Payload) == 3 * 1024 * 8 * 16
        Raw = (Root / f'_ClaudeScratch/candidate-bank-capture-release/CandidateBank{Fixture}-{Pose}.f32').read_bytes()
        ReceiverX = struct.unpack('<f', struct.pack('<f', .5 - .5 * math.cos(2 * math.pi * Pose / 16)))[0]
        Scale = .5 if 9 <= Pose < 13 else 1.
        for Method in range(1, 4):
            Replace = 4 if Method == 2 else 2
            Start = (Pose * Replace) % 8
            for Sample in range(1024):
                Estimate, ValidCount = 0., 0
                for Slot in range(8):
                    Offset = (((Method-1)*1024+Sample)*8+Slot)*16
                    X, Y, Outgoing, Density = struct.unpack_from('<4f', Payload, Offset)
                    PayloadChecks += 1
                    if Method >= 2 and Pose not in (0, 9, 13) and (Slot+8-Start)%8 >= Replace:
                        assert Payload[Offset:Offset+16] == Previous[Offset:Offset+16]
                        PreservedChecks += 1
                    if Density == 0:
                        assert X == Y == Outgoing == 0
                        continue
                    ValidCount += 1
                    assert abs(X) <= .5 and abs(Y) <= .5 and Density >= .25
                    DonorVisible = Blocker == 0 or abs(X) > 2 * Blocker
                    ExpectedDensity = .25 + (.75 / (math.pi * (1+X*X+Y*Y)**2) if DonorVisible else 0)
                    MaximumDensityError = max(MaximumDensityError, abs(Density-ExpectedDensity))
                    LightVisible = Blocker == 0 or abs(X+2/3) > Blocker
                    assert abs(Outgoing - (.7*Scale if LightVisible else 0)) < 2e-6
                    ReceiverVisible = Blocker == 0 or abs(X+ReceiverX) > 2*Blocker
                    if ReceiverVisible:
                        Estimate += .6*Outgoing / (math.pi*(1+(X-ReceiverX)**2+Y*Y)**2*Density*8)
                Measured = struct.unpack_from('<f', Raw, (Method*1024+Sample)*16)[0]
                MaximumEstimateError = max(MaximumEstimateError, abs(Estimate-Measured))
                if 0 < ValidCount < 8 and Estimate > .0001:
                    assert abs(Estimate * 8 / ValidCount - Measured) > 1e-6
                    NullDivisorDifferences += 1
        Previous = Payload
assert MaximumDensityError < 2e-6 and MaximumEstimateError < 2e-6
assert PreservedChecks == 4*13*10*1024 and NullDivisorDifferences > 1024
print(f'Payload audit: {PayloadChecks} candidates, {PreservedChecks} unchanged retained slots; '
      f'max density error={MaximumDensityError:.12g}, max reconstruction error={MaximumEstimateError:.12g}; '
      f'wrong nonnull divisor rejected at {NullDivisorDifferences} estimates')

Lines = ['# Candidate-bank measured comparison', '',
         'Temporal RMSE = error in each 16-pose mean residual; not TAA. Queries include every rebuild.', '',
         '| Fixture | Method | Instant RMSE | Temporal RMSE | Flicker RMSE | Lag1 | BVH queries |',
         '|---------|--------|-------------:|--------------:|-------------:|-----:|------------:|']
for Fixture in range(4):
    for Method in Methods:
        F = Measurements[str(Fixture), Method]
        Lines.append(f"| {Fixture} | {Method} | {float(F['rmse']):.7f} | {float(F['temporal_rmse']):.7f} | "
                     f"{float(F['flicker_rmse']):.7f} | {float(F['lag1']):.4f} | {int(F['queries']):,} |")
(Results / 'CandidateBank-comparison.md').write_text('\n'.join(Lines) + '\n')
Message = (f'PASS candidate bank: {len(Semantic[0])} identical semantic rows; checked hashes {HashCounts}; '
           '384 pose mean gates, four full-bank/query identities, 16 paired contrasts; 17039360 native retained-slot checks; rotation/light revisions correct; '
           '128 exact raw captures, 24 representative PNG arrays; shader rederivation and fresh budgets verified; '
           '136 fresh-control rows unchanged; both five-CTest sets passed. promote=0 physical_gpu_timing=0.\n')
Message += (f'Payload audit: {PayloadChecks} candidate arrays, {PreservedChecks} unchanged retained slots; '
            f'max density error={MaximumDensityError:.12g} max reconstruction error={MaximumEstimateError:.12g}; '
            f'{NullDivisorDifferences} nonnull-divisor negative differences.\n')
(Results / 'CandidateBank-parity.txt').write_text(Message)
print(Message, end='')
