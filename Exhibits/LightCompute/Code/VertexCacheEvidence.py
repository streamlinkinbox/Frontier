# 🧩 Audit persistent vertex payload lifecycle, stale-cache controls, paired quality and actual query savings.
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
Steps = ('cold', 'warm', 'receiver', 'light', 'material', 'geometry', 'off', 'on')
Methods = ('inline', 'cache', 'stale_light', 'stale_geometry')
Semantic, HashCounts = [], []
Measurements, References = {}, {}
for Mode in ('release', 'sanitized'):
    Text = (Results / f'VertexCache-{Mode}.txt').read_text()
    Rows = [Line for Line in Text.splitlines() if Line.startswith('VERTEX_CACHE_')]
    assert Rows[-1] == 'VERTEX_CACHE_DECISION correctness=1 promote=0 physical_gpu_timing=0'
    assert re.search(r'100% tests passed(?:, 0 tests failed)? out of 5', Text)
    assert not re.search(r'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer', Text)
    Hashes = re.findall(r'^([a-f0-9]{64})  (.+)$', Text, re.M)
    for Digest, Name in Hashes:
        assert hashlib.sha256((Root / Name).read_bytes()).hexdigest() == Digest, Name
    HashCounts.append(len(Hashes))
    CorrectCount, NegativeCount, IdentityCount = 0, 0, 0
    for Line in Rows:
        F = dict(re.findall(r'(\w+)=([^ ]+)', Line))
        if Line.startswith('VERTEX_CACHE_REFERENCE '):
            References[F['fixture'], F['step']] = float(F['integral'])
        if Line.startswith('VERTEX_CACHE_RESULT '):
            Fixture, Step, Method = F['fixture'], F['step'], F['method']
            Measurements[Fixture, Step, Method] = F
            Mean, Error = float(F['mean']), float(F['nominal_se'])
            Reference = References[Fixture, Step]
            assert all(math.isfinite(float(F[Name])) for Name in ('mean', 'nominal_se', 'rmse'))
            if Method in ('inline', 'cache'):
                CorrectCount += 1
                assert F['mean_gate'] == '1' and abs(Mean - Reference) < 4 * Error + .0001
            if F['negative_required'] == '1':
                NegativeCount += 1
                assert F['negative_gate'] == '1' and abs(Mean - Reference) > 6 * Error + .005 * Reference
            if Method == 'cache':
                if Step in ('warm', 'receiver'):
                    assert F['source_queries'] == '0' and int(F['reused']) == 32 * 4096
                    assert int(F['queries']) <= 32 * 4096
                else:
                    assert F['reused'] == '0' and int(F['source_queries']) > 0
        if Line.startswith('VERTEX_CACHE_IDENTITY '):
            IdentityCount += 1
            assert float(F['maximum_difference']) < 2e-6
    assert (CorrectCount, NegativeCount, IdentityCount) == (64, 12, 32)
    Semantic.append(Rows)
assert Semantic[0] == Semantic[1]
assert len(Measurements) == 128 and len(References) == 32
with tempfile.TemporaryDirectory(dir=Root / '_ClaudeScratch/tmp') as Temporary:
    Shader = Path(Temporary) / 'VertexCache.slang'
    subprocess.run([sys.executable, str(Root / 'Exhibits/LightCompute/Code/CompileVertexCache.py'), str(Shader)],
                   cwd=Root, check=True)
    for Mode in ('release', 'sanitized'):
        assert Shader.read_bytes() == (Root / f'_ClaudeScratch/build/vertex-cache-{Mode}/VertexCache.slang').read_bytes()

Images.mkdir(exist_ok=True)
Font = ImageFont.load_default(size=14)
Small = ImageFont.load_default(size=12)
Canvas = Image.new('RGB', (1390, 760), '#121923')
Pen = ImageDraw.Draw(Canvas)
Pen.text((20, 14), 'Actual persistent GPU estimator readbacks / partial-support fixture / first cohort / NOT camera images',
         font=Font, fill='white')
Pen.text((20, 38), 'Exposure 6, clipped; 64 x 64 reservoirs per tile / rows include deliberate stale-cache failures',
         font=Small, fill='white')
for Step, Name in enumerate(Steps):
    Pen.text((190 + 148 * Step, 70), Name, font=Small, fill='white')
Labels = ('Inline current', 'Correct cache', 'Stale-light control', 'Stale-geometry control')
for Method, Label in enumerate(Labels):
    Pen.text((16, 143 + 158 * Method), Label, font=Small, fill='white')
for Fixture in range(4):
    for Step, StepName in enumerate(Steps):
        A = Root / f'_ClaudeScratch/vertex-cache-capture-release/VertexCache{Fixture}-{Step}.f32'
        B = Root / f'_ClaudeScratch/vertex-cache-capture-sanitized/VertexCache{Fixture}-{Step}.f32'
        Raw = A.read_bytes()
        assert Raw == B.read_bytes() and len(Raw) == 4 * 4096 * 16
        for Method in range(4):
            Pixels = []
            for Sample in range(4096):
                Estimate, Queries, Reused, SourceQueries = struct.unpack_from('<4f', Raw, (Method * 4096 + Sample) * 16)
                assert all(math.isfinite(V) and V >= 0 for V in (Estimate, Queries, Reused, SourceQueries))
                if Method == 1:
                    assert Reused == int(StepName in ('warm', 'receiver'))
                    if Reused:
                        assert SourceQueries == 0 and Queries <= 1
                    Inline = struct.unpack_from('<f', Raw, Sample * 16)[0]
                    assert abs(Estimate - Inline) < 2e-6
                Gray = min(255, max(0, round(Estimate * 6 * 255)))
                Pixels.append((Gray, Gray, Gray))
            # Retain panels of the representative partial-support fixture; all 32 raw captures are audited.
            if Fixture == 2:
                Panel = Image.new('RGB', (64, 64))
                Panel.putdata(Pixels)
                Name = Images / f'VertexCache{Step}{Method}.png'
                Panel.save(Name)
                with Image.open(Name) as Retained:
                    assert Retained.tobytes() == bytes(Channel for Pixel in Pixels for Channel in Pixel)
                Canvas.paste(Panel.resize((128, 128), Image.Resampling.NEAREST), (190 + 148 * Step, 94 + 158 * Method))
Canvas.save(Images / 'VertexCacheReadbacks.png')

Lines = ['# Persistent vertex cache query comparison', '',
         'Actual nearest-facet calls. Same-seed paired estimates; not additional independent samples or GPU time.', '',
         '| Fixture | Inline eight-state queries | Cached queries | Reduction | Warm reduction | Receiver reduction |',
         '|---------|---------------------------:|---------------:|----------:|---------------:|-------------------:|']
for Fixture in range(4):
    Key = str(Fixture)
    Inline = sum(int(Measurements[Key, Step, 'inline']['queries']) for Step in Steps)
    Cached = sum(int(Measurements[Key, Step, 'cache']['queries']) for Step in Steps)
    Ratios = []
    for Step in ('warm', 'receiver'):
        Ratios.append(100 * (1 - int(Measurements[Key, Step, 'cache']['queries']) /
                             int(Measurements[Key, Step, 'inline']['queries'])))
    Lines.append(f'| {Fixture} | {Inline:,} | {Cached:,} | {100*(1-Cached/Inline):.2f}% | '
                 f'{Ratios[0]:.2f}% | {Ratios[1]:.2f}% |')
(Results / 'VertexCache-comparison.md').write_text('\n'.join(Lines) + '\n')
Message = (f'PASS persistent vertex cache: {len(Semantic[0])} identical semantic rows; checked hashes {HashCounts}; '
           '64 reference gates, 12 stale-cache negatives, 32 paired identities; 32 exact raw captures, 32 PNG arrays; '
           'warm source queries zero, lifecycle cadence and shader rederivation pass; both five-CTest sets passed. '
           'promote=0 physical_gpu_timing=0.\n')
(Results / 'VertexCache-parity.txt').write_text(Message)
print(Message, end='')
