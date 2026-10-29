# 🧩 Verify Vulkan secondary-vertex controls and present actual estimator readbacks without camera-render claims.
from pathlib import Path
import hashlib
import math
import re
import struct
from PIL import Image, ImageDraw, ImageFont

Root = Path(__file__).resolve().parents[3]
Results = Root / 'Exhibits/LightCompute/Results'
Images = Root / 'Exhibits/LightCompute/Images'
Images.mkdir(exist_ok=True)
Semantic, HashCounts = [], []
Measurements, References = {}, {}
for Mode in ('release', 'sanitized'):
    Text = (Results / f'TriangleReconnection-{Mode}.txt').read_text()
    Rows = [Line for Line in Text.splitlines() if Line.startswith('TRIANGLE_GPU_')]
    assert Rows[-1] == 'TRIANGLE_GPU_DECISION correctness=1 promote=0 physical_gpu_timing=0'
    assert re.search(r'100% tests passed(?:, 0 tests failed)? out of 5', Text)
    assert not re.search(r'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer', Text)
    Hashes = re.findall(r'^([a-f0-9]{64})  (.+)$', Text, re.M)
    for Digest, Name in Hashes:
        assert hashlib.sha256((Root / Name).read_bytes()).hexdigest() == Digest, Name
    HashCounts.append(len(Hashes))
    NegativeCount, CorrectCount, IdentityCount, ProbeCount, ShadowCount = 0, 0, 0, 0, 0
    for Line in Rows:
        Fields = dict(re.findall(r'(\w+)=([^ ]+)', Line))
        if Line.startswith('TRIANGLE_GPU_REFERENCE '):
            References[Fields['fixture']] = Fields
            assert float(Fields['refinement']) < 1e-7
        if Line.startswith('TRIANGLE_GPU_RESULT '):
            Fixture, Method = Fields['fixture'], Fields['method']
            Measurements[Fixture, Method] = Fields
            Mean, Error = float(Fields['mean']), float(Fields['nominal_se'])
            assert int(Fields['queries']) > 0
            Reference = float(References[Fixture]['integral'])
            assert all(math.isfinite(float(Fields[Name])) for Name in ('mean', 'nominal_se', 'rmse'))
            if Method in ('area', 'angular', 'fresh8'):
                CorrectCount += 1
                assert Fields['mean_gate'] == '1' and abs(Mean - Reference) < 4 * Error + .0001
            if Fields['negative_required'] == '1':
                NegativeCount += 1
                assert Fields['negative_gate'] == '1' and abs(Mean - Reference) > 6 * Error + .005 * Reference
            if Method in ('no_fallback', 'no_floor'):
                assert abs(Mean - float(References[Fixture]['donor_support'])) < 4 * Error + .0001
        if Line.startswith('TRIANGLE_GPU_SUFFIX '):
            ProbeCount += int(Fields['probes'])
            ShadowCount += int(Fields['shadowed'])
            assert int(Fields['probes']) == 64 * 4096
            assert Fields['shadowed'] == Fields['unshadowed_negative']
        if Line.startswith('TRIANGLE_GPU_IDENTITY '):
            IdentityCount += 1
            assert float(Fields['maximum_difference']) < 2e-6
            assert 0 < int(Fields['present']) < int(Fields['considered']) == 64 * 4096 * 8
    assert CorrectCount == 12 and NegativeCount == 6 and IdentityCount == 4
    assert ProbeCount == 1048576 and ShadowCount > 0
    Semantic.append(Rows)
assert Semantic[0] == Semantic[1], 'GPU semantic rows differ across host builds'
assert len(Measurements) == 28 and len(References) == 4

FixtureNames = ('self', 'shift', 'partial_support', 'dark_donor')

Methods = ('angular', 'area', 'missing_j', 'double_j', 'no_fallback', 'no_floor', 'fresh8', 'suffix_probe')
Labels = ('Angular control', 'Area estimator', 'Missing angular J', 'Extra area J', 'No fallback', 'No source floor', 'Fresh8', 'Outgoing suffix')
Font = ImageFont.load_default(size=14)
Small = ImageFont.load_default(size=12)
Canvas = Image.new('RGB', (1360, 760), '#121923')
Pen = ImageDraw.Draw(Canvas)
Pen.text((20, 14), 'Actual Vulkan estimator readbacks / first batch / each panel = 4096 independent reservoirs / NOT camera images',
         fill='white', font=Font)
Pen.text((20, 38), 'Linear grayscale x6, clipped for display / no interpolation / metrics use unclamped float readbacks over all 64 cohorts',
         fill='white', font=Small)
for Method, Label in enumerate(Labels):
    Pen.text((150 + 148 * Method, 69), Label, fill='white', font=Small)
for Fixture in range(4):
    A = Root / f'_ClaudeScratch/triangle-reconnection-capture-release/TriangleGpu{Fixture}.f32'
    B = Root / f'_ClaudeScratch/triangle-reconnection-capture-sanitized/TriangleGpu{Fixture}.f32'
    Raw = A.read_bytes()
    assert Raw == B.read_bytes() and len(Raw) == 8 * 4096 * 16
    Pen.text((16, 143 + 158 * Fixture), FixtureNames[Fixture], fill='white', font=Small)
    for Method in range(8):
        Pixels = []
        for Sample in range(4096):
            Estimate, Present, Count, Weight = struct.unpack_from('<4f', Raw, (Method * 4096 + Sample) * 16)
            assert Count == 8 and (Method == 7 or (0 <= Present <= 8 and Present == int(Present)))
            assert Weight >= 1 and Weight == int(Weight)
            if Method == 7:
                assert Weight == 1
            assert all(math.isfinite(V) and V >= 0 for V in (Estimate, Present, Weight))
            Gray = min(255, max(0, round(Estimate * 6 * 255)))
            Pixels.append((Gray, Gray, Gray))
        ImagePath = Images / f'TriangleGpu{Fixture}{Method}.png'
        Panel = Image.new('RGB', (64, 64))
        Panel.putdata(Pixels)
        Panel.save(ImagePath)
        with Image.open(ImagePath) as Retained:
            assert Retained.tobytes() == bytes(Channel for Pixel in Pixels for Channel in Pixel)
        Canvas.paste(Panel.resize((128, 128), Image.Resampling.NEAREST), (150 + 148 * Method, 94 + 158 * Fixture))
Canvas.save(Images / 'TriangleReconnectionReadbacks.png')

Lines = ['# Vulkan secondary-vertex measured results', '',
         'Actual BVH queries on SwiftShader CPU Vulkan, not physical-GPU performance.', '',
         '| Fixture | Reference | GPU area mean ± nominal SE | Missing J | Extra J | No fallback | No floor |',
         '|---------|----------:|---------------------------:|----------:|--------:|------------:|---------:|']
for Fixture in range(4):
    Key = str(Fixture)
    Area = Measurements[Key, 'area']
    Columns = [FixtureNames[Fixture], f"{float(References[Key]['integral']):.9f}",
               f"{float(Area['mean']):.9f} ± {float(Area['nominal_se']):.9f}"]
    Columns += [f"{float(Measurements[Key, Method]['mean']):.9f}"
                for Method in ('missing_j', 'double_j', 'no_fallback', 'no_floor')]
    Lines.append('| ' + ' | '.join(Columns) + ' |')
(Results / 'TriangleReconnection-comparison.md').write_text('\n'.join(Lines) + '\n')
Message = (f'PASS triangle BVH reconnection: {len(Semantic[0])} identical semantic rows; '
           f'checked hashes {HashCounts}; 12 reference gates, six required negative controls, four identities; '
           '1048576 suffix probes with shadow negative control; four exact raw readbacks and 32 retained PNG arrays; '
           'both five-CTest sets passed. promote=0 physical_gpu_timing=0.\n')



# Regenerate the measured shader from the retained template rather than trusting stale build output.
import subprocess
import sys
import tempfile
with tempfile.TemporaryDirectory(dir=Root / '_ClaudeScratch/tmp') as Temporary:
    Generated = Path(Temporary) / 'TriangleReconnection.slang'
    subprocess.run([sys.executable, str(Root / 'Exhibits/LightCompute/Code/CompileTriangleReconnection.py'),
                    str(Generated)], cwd=Root, check=True)
    for Mode in ('release', 'sanitized'):
        assert Generated.read_bytes() == (Root / f'_ClaudeScratch/build/triangle-reconnection-{Mode}/TriangleReconnection.slang').read_bytes()

Cost = ['# Triangle reconnection query/error comparison', '',
        'Actual nearest-facet invocations, including repeated validation suffix/connection queries; not GPU time.', '',
        '| Fixture | Area RMSE | Fresh8 RMSE | Area queries | Fresh8 queries |',
        '|---------|----------:|------------:|-------------:|---------------:|']
for Fixture in range(4):
    A, B = (Measurements[str(Fixture), Name] for Name in ('area', 'fresh8'))
    Cost.append(f"| {FixtureNames[Fixture]} | {float(A['rmse']):.9f} | {float(B['rmse']):.9f} | {int(A['queries']):,} | {int(B['queries']):,} |")
(Results / 'TriangleReconnection-cost.md').write_text('\n'.join(Cost) + '\n')

(Results / 'TriangleReconnection-parity.txt').write_text(Message)
print(Message, end='')
