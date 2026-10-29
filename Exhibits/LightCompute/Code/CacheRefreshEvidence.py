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
Methods = ('ris8', 'period1', 'period4', 'period8', 'fresh6', 'fresh8')
Semantic, HashCounts = [], []
Measurements, Pairs = {}, []
for Mode in ('release', 'sanitized'):
    Text = (Results / f'CacheRefresh-{Mode}.txt').read_text()
    Rows = [L for L in Text.splitlines() if L.startswith('CACHE_REFRESH_')]
    assert Rows[-1] == 'CACHE_REFRESH_DECISION correctness=1 promote=0 physical_gpu_timing=0'
    assert re.search(r'100% tests passed(?:, 0 tests failed)? out of 5', Text)
    assert not re.search(r'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer', Text)
    Hashes = re.findall(r'^([a-f0-9]{64})  (.+)$', Text, re.M)
    for Digest, Name in Hashes:
        assert hashlib.sha256((Root / Name).read_bytes()).hexdigest() == Digest, Name
    HashCounts.append(len(Hashes))
    References, MeanCount, CheckCount, PairCount = {}, 0, 0, 0
    for Line in Rows:
        F = dict(re.findall(r'(\w+)=([^ ]+)', Line))
        if Line.startswith('CACHE_REFRESH_REFERENCE '):
            References[F['fixture'], F['pose']] = float(F['integral'])
        if Line.startswith('CACHE_REFRESH_MEAN '):
            MeanCount += 1
            assert F['gate'] == '1'
            assert abs(float(F['mean']) - References[F['fixture'], F['pose']]) < 4 * float(F['nominal_se']) + .0001
        if Line.startswith('CACHE_REFRESH_RESULT '):
            Measurements[F['fixture'], F['method']] = F
            assert all(math.isfinite(float(F[K])) for K in ('rmse', 'temporal_rmse', 'flicker_rmse', 'lag1'))
            assert abs(float(F['lag1'])) <= 1 and int(F['queries']) >= int(F['source_queries'])
            Expected = 32 * 1024 * (10 if F['method'] == 'period4' else 12 if F['method'] == 'period8' else 0)
            assert int(F['reused']) == Expected
        if Line.startswith('CACHE_REFRESH_PAIRED '):
            PairCount += 1
            if Mode == 'release': Pairs.append(F)
            assert all(math.isfinite(float(F[K])) for K in ('instantaneous_mse', 'instantaneous_se', 'temporal_mse', 'temporal_se'))
        if Line.startswith('CACHE_REFRESH_CHECK '):
            CheckCount += 1
            assert F['mean_gates'] == '96' and float(F['maximum_period1_difference']) < 2e-6
    assert (len(References), MeanCount, CheckCount, PairCount) == (64, 384, 4, 16)
    Semantic.append(Rows)
assert Semantic[0] == Semantic[1] and len(Measurements) == 24
with tempfile.TemporaryDirectory(dir=Root / '_ClaudeScratch/tmp') as Temporary:
    Shader = Path(Temporary) / 'CacheRefresh.slang'
    subprocess.run([sys.executable, str(Root / 'Exhibits/LightCompute/Code/CompileCacheRefresh.py'), str(Shader)],
                   cwd=Root, check=True)
    for Mode in ('release', 'sanitized'):
        assert Shader.read_bytes() == (Root / f'_ClaudeScratch/build/cache-refresh-{Mode}/CacheRefresh.slang').read_bytes()
    assert 'FreshComponent(Pixel, Method == 4 ? 6 : 8)' in Shader.read_text()
    assert 'TriangleOutgoing(Point) / float(Budget)' in Shader.read_text()

Images.mkdir(exist_ok=True)
Font = ImageFont.load_default(size=14)
Small = ImageFont.load_default(size=12)
Canvas = Image.new('RGB', (1200, 760), '#121923')
Pen = ImageDraw.Draw(Canvas)
Pen.text((18, 14), 'Actual Vulkan refresh readbacks / new .3 strip holdout / first cohort / NOT camera images', font=Font, fill='white')
Pen.text((18, 38), 'Linear grayscale x6, clipped / 32 x 32 independent reservoir trajectories per tile; no interpolation', font=Small, fill='white')
Labels = ('Fresh RIS8', 'Period1', 'Period4', 'Period8', 'Fresh cosine6', 'Fresh cosine8')
for Method, Label in enumerate(Labels):
    Pen.text((180 + Method * 166, 69), Label, font=Small, fill='white')
SelectedPoses = (2, 6, 10, 14)
for Row, Pose in enumerate(SelectedPoses):
    Pen.text((16, 143 + 158 * Row), f'Pose {Pose}', font=Small, fill='white')
for Fixture in range(4):
    for Pose in range(16):
        A = Root / f'_ClaudeScratch/cache-refresh-capture-release/CacheRefresh{Fixture}-{Pose}.f32'
        B = Root / f'_ClaudeScratch/cache-refresh-capture-sanitized/CacheRefresh{Fixture}-{Pose}.f32'
        Raw = A.read_bytes()
        assert Raw == B.read_bytes() and len(Raw) == 6 * 1024 * 16
        for Method in range(6):
            Pixels = []
            for Sample in range(1024):
                Estimate, Queries, Reused, SourceQueries = struct.unpack_from('<4f', Raw, (Method * 1024 + Sample) * 16)
                assert all(math.isfinite(V) and V >= 0 for V in (Estimate, Queries, Reused, SourceQueries))
                Expected = Method in (2, 3) and Pose not in (9, 13) and Pose % (4 if Method == 2 else 8) != 0
                assert Reused == int(Expected)
                if Expected: assert SourceQueries == 0 and Queries <= 1
                if Method == 1: assert abs(Estimate - struct.unpack_from('<f', Raw, Sample * 16)[0]) < 2e-6
                Gray = min(255, max(0, round(Estimate * 6 * 255)))
                Pixels.append((Gray, Gray, Gray))
            if Fixture == 2 and Pose in SelectedPoses:
                Panel = Image.new('RGB', (32, 32)); Panel.putdata(Pixels)
                Name = Images / f'CacheRefresh{Pose}-{Method}.png'; Panel.save(Name)
                with Image.open(Name) as Retained:
                    assert Retained.tobytes() == bytes(C for P in Pixels for C in P)
                Canvas.paste(Panel.resize((128, 128), Image.Resampling.NEAREST),
                             (180 + Method * 166, 94 + SelectedPoses.index(Pose) * 158))
Canvas.save(Images / 'CacheRefreshReadbacks.png')

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
    YMin, YMax = .6, max(Y for X, Y in Ratios) * 1.18
    Pen.text((Left, 58), 'No strip' if Fixture == 0 else 'New .3 strip holdout', font=Font, fill='white')
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
Canvas.save(Images / 'CacheRefreshFrontier.png')

Lines = ['# Bounded refresh measured comparison', '',
         'Temporal RMSE = error in each 16-pose mean residual; not TAA. Queries include every rebuild.', '',
         '| Fixture | Method | Instant RMSE | Temporal RMSE | Flicker RMSE | Lag1 | BVH queries |',
         '|---------|--------|-------------:|--------------:|-------------:|-----:|------------:|']
for Fixture in range(4):
    for Method in Methods:
        F = Measurements[str(Fixture), Method]
        Lines.append(f"| {Fixture} | {Method} | {float(F['rmse']):.7f} | {float(F['temporal_rmse']):.7f} | "
                     f"{float(F['flicker_rmse']):.7f} | {float(F['lag1']):.4f} | {int(F['queries']):,} |")
(Results / 'CacheRefresh-comparison.md').write_text('\n'.join(Lines) + '\n')
Message = (f'PASS bounded refresh: {len(Semantic[0])} identical semantic rows; checked hashes {HashCounts}; '
           '384 pose mean gates, four period1 identities, 16 paired contrasts; cadence/light revisions correct; '
           '64 exact raw captures, 24 representative PNG arrays; shader rederivation and fresh budgets verified; '
           'both five-CTest sets passed. promote=0 physical_gpu_timing=0.\n')
(Results / 'CacheRefresh-parity.txt').write_text(Message)
print(Message, end='')
