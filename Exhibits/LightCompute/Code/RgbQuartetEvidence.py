# 🧩 Audit native RGB quartet selection and compare actual RGB readbacks with exact finite references.
from pathlib import Path
import gzip
import hashlib
import math
import re
import struct
from PIL import Image, ImageDraw, ImageFont

Root = Path(__file__).resolve().parents[3]
Results = Root / 'Exhibits/LightCompute/Results'
Images = Root / 'Exhibits/LightCompute/Images'
Rows = []
HashCounts = []
for Mode in ('release', 'sanitized'):
    Text = (Results / f'RgbQuartet-{Mode}.txt').read_text()
    assert '100% tests passed, 0 tests failed out of 5' in Text
    assert not re.search(r'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer', Text)
    Semantic = [Line for Line in Text.splitlines() if Line.startswith('RGB_QUARTET_')]
    assert len(Semantic) == 38
    assert 'selector=1' in Semantic[-1] and 'production_promote=0 renderer_quality=0' in Semantic[-1]
    Rows.append(Semantic)
    Hashes = re.findall(r'^([a-f0-9]{64})  (.+)$', Text, re.M)
    for Digest, Name in Hashes:
        assert hashlib.sha256((Root / Name).read_bytes()).hexdigest() == Digest, Name
    HashCounts.append(len(Hashes))
assert Rows[0] == Rows[1]
for Stem in ('DiffuseGenerate', 'DiffuseResolve'):
    Modules = [Root / f'_ClaudeScratch/build/rgb-quartet-{Mode}/{Stem}.spv' for Mode in ('release', 'sanitized')]
    assert Modules[0].read_bytes() == Modules[1].read_bytes()
    (Results / f'RgbQuartet-{Stem}.spv.gz').write_bytes(gzip.compress(Modules[0].read_bytes(), mtime=0))
References = {}
for Line in Rows[0]:
    if Line.startswith('RGB_QUARTET_EXACT'):
        Fields = dict(re.findall(r'(\w+)=([^ ]+)', Line))
        References[int(Fields['scenario'])] = [float(Fields[f'channel{Channel}']) for Channel in range(3)]
Methods = ('quartet', 'raw8', 'zero-support', 'valid-divisor', 'current-h')
Slots = (0, 1, 10, 11, 12)
Table = ['|Birth→current|Method|R mean|G mean|B mean|RGB RMSE|Max mean error/SE|Gate|',
         '|---|---|---:|---:|---:|---:|---:|---|']
Failures = [0] * 5
Ratios = []
Measurements = []
Maximum = [0., 0., 0.]
Canvas = Image.new('RGB', (1180, 640), '#121923')
Pen = ImageDraw.Draw(Canvas)
Font, Small = ImageFont.load_default(size=15), ImageFont.load_default(size=12)
Pen.text((18, 12), 'Actual Vulkan RGB quartet selection / 4096 packets per receiver pair / supplied RGB,not traced GI', fill='white', font=Font)
Colors = ('#79d5ab', '#a69bdf')
MeanGates = 0
for Scenario in range(20):
    Paths = [Root / f'_ClaudeScratch/rgb-quartet-capture-{Mode}/RgbQuartet{Scenario}.f32' for Mode in ('release', 'sanitized')]
    Raw = Paths[0].read_bytes()
    assert Raw == Paths[1].read_bytes() and len(Raw) == 14 * 4096 * 16
    Retained = Results / f'RgbQuartet{Scenario}.f32.gz'
    Retained.write_bytes(gzip.compress(Raw, mtime=0))
    assert gzip.decompress(Retained.read_bytes()) == Raw
    Line = next(Line for Line in Rows[0] if Line.startswith(f'RGB_QUARTET_CASE scenario={Scenario} '))
    Fields = dict(re.findall(r'(\w+)=([^ ]+)', Line))
    for Index, Name in enumerate(('estimate_error', 'weight_error', 'suffix_error')):
        Maximum[Index] = max(Maximum[Index], float(Fields[Name]))
    if Scenario == 18:
        assert Fields['nulls'] == str(4096 * 8) and Fields['empty'] == str(4096 * 4)
    if Scenario == 19:
        assert Fields['nulls'] == str(4096 * 7) and Fields['empty'] == str(4096 * 3)
    if Scenario >= 16:
        if Scenario in (16, 18):
            assert all(struct.unpack_from('<3f', Raw, Pixel * 16) == (0., 0., 0.) for Pixel in range(4096))
        continue
    Reference = References[Scenario]
    Case = []
    for MethodIndex, (Method, Slot) in enumerate(zip(Methods, Slots)):
        Samples = [struct.unpack_from('<3f', Raw, (Slot * 4096 + Pixel) * 16) for Pixel in range(4096)]
        assert all(math.isfinite(Component) and Component >= 0 for Sample in Samples for Component in Sample)
        Means = [sum(Sample[Channel] for Sample in Samples) / 4096 for Channel in range(3)]
        Errors = [math.sqrt(sum((Sample[Channel] - Means[Channel]) ** 2 for Sample in Samples) / (4095 * 4096)) for Channel in range(3)]
        Passed = all(abs(Mean - Exact) <= 6 * Error + 2e-5 for Mean, Exact, Error in zip(Means, Reference, Errors))
        if MethodIndex < 2:
            assert Passed, (Scenario, Method, Means, Reference, Errors)
            MeanGates += 3
        Failures[MethodIndex] += not Passed
        Rmse = math.sqrt(sum(sum((Sample[Channel] - Reference[Channel]) ** 2 for Channel in range(3)) for Sample in Samples) / (4096 * 3))
        Standardized = max(abs(Mean - Exact) / Error if Error > 0 else (math.inf if Mean != Exact else 0.) for Mean, Exact, Error in zip(Means, Reference, Errors))
        Table.append(f'|{Scenario//4}→{Scenario%4}|{Method}|' + '|'.join(f'{Mean:.9g}' for Mean in Means) + f'|{Rmse:.9g}|{Standardized:.5g}|{int(Passed)}|')
        Case.append((Rmse, Means, Standardized))
    if Case[1][0] > 0:
        Ratios.append(Case[0][0] / Case[1][0])
    Measurements.append(Case)
    # A literal RGB readback image from the quartet and same-eight-proposal baseline.
    if Scenario in (0, 4, 8, 12):
        Column = Scenario // 4
        for RowIndex, Slot in enumerate((0, 1)):
            Pixels = [tuple(max(0, min(255, round(Component * 4 * 255))) for Component in struct.unpack_from('<3f', Raw, (Slot * 4096 + Pixel) * 16)) for Pixel in range(4096)]
            Panel = Image.new('RGB', (64, 64)); Panel.putdata(Pixels)
            File = Images / f'RgbQuartet-{Scenario}-{Slot}.png'; Panel.save(File)
            with Image.open(File) as Reopened:
                assert Reopened.tobytes() == bytes(Component for Pixel in Pixels for Component in Pixel)
            Canvas.paste(Panel.resize((160, 160), Image.Resampling.NEAREST), (24 + Column * 285, 68 + RowIndex * 205))
        Pen.text((24 + Column * 285, 44), f'Birth{Column} -> front receiver', fill='white', font=Small)
Pen.text((18, 245), 'Quartet', fill=Colors[0], font=Small)
Pen.text((18, 450), 'Raw8', fill=Colors[1], font=Small)
Pen.text((18, 493), 'Linear RGB exposure4,clipped;64x64 logical packets,not camera pixels. Same eight proposals per packet.', fill='white', font=Small)
Pen.text((18, 529), 'The quartet stores four weighted descriptors;the raw estimator averages all eight candidates. Nulls count in M.', fill='white', font=Small)
Pen.text((18, 565), 'Independent CPU selection/weights/RGB checks and stale-epoch/all-null/one-valid controls are in native logs.', fill='white', font=Small)
Pen.text((18, 605), 'This fresh-packet gate measures correctness and compression noise,not temporal reuse or a GI quality improvement.', fill='white', font=Small)
Canvas.save(Images / 'RgbQuartetReadbacks.png')
assert all(Count > 0 for Count in Failures[2:]), Failures
Canvas = Image.new('RGB', (1180, 650), '#121923'); Pen = ImageDraw.Draw(Canvas)
Pen.text((18, 12), 'Actual RGB RMSE versus exact finite sum / same eight candidates / smaller is better / not GI image quality', fill='white', font=Font)
Scale = max(Case[Method][0] for Case in Measurements for Method in (0, 1))
for Scenario, Case in enumerate(Measurements):
    Column, RowIndex = Scenario % 4, Scenario // 4
    X, Y = 20 + Column * 290, 58 + RowIndex * 112
    Pen.text((X, Y), f'Birth{RowIndex} -> current{Column}', fill='white', font=Small)
    for Method in (0, 1):
        Length = Case[Method][0] / max(Scale, 1e-20) * 165
        Pen.rectangle((X, Y + 23 + Method * 23, X + Length, Y + 37 + Method * 23), fill=Colors[Method])
        Pen.text((X + 170, Y + 23 + Method * 23), f'{Case[Method][0]:.5f}', fill=Colors[Method], font=Small)
Pen.text((18, 525), f'Green:quartet;purple:raw8. Quartet/raw RMSE range(nonzero) {min(Ratios):.3f}-{max(Ratios):.3f}x.', fill='white', font=Small)
Pen.text((18, 560), f'Failed mean cases/16:correct{Failures[0]},raw8{Failures[1]},zero-support{Failures[2]},valid-divisor{Failures[3]},current-h{Failures[4]}.', fill='white', font=Small)
Pen.text((18, 595), '144B quartet versus272B raw RGB logical layout. No allocated-VRAM,bandwidth or speed measurement.', fill='white', font=Small)
Pen.text((18, 625), 'Selection adds conditional variance to the same raw proposals;compression is not a fresh-sample quality win.', fill='white', font=Small)
Canvas.save(Images / 'RgbQuartetComparison.png')
(Results / 'RgbQuartet-comparison.md').write_text('# Actual RGB quartet comparison\n\n' + '\n'.join(Table) + '\n')
Summary = (f'PASS integrated RGB selector gate;38 identical semantic rows;hashes/build={HashCounts};20 byte-identical captures.\n'
           f'327680 selected descriptor comparisons/build;96 correct/raw RGB mean gates;8 exact PNG readbacks.\n'
           f'Max CPU normalized estimate/weight,absolute suffix errors={Maximum};negative failed cases/16={Failures[2:]}.\n'
           f'Quartet/raw8 RMSE ratio(nonzero)={min(Ratios):.9g}..{max(Ratios):.9g};no fresh-quality promotion.\n'
           'Host Release and ASan+UBSan each exit0 with five CTests. Two-dispatch descriptor wire write/read.\n'
           'Supplied RGB,finite candidates,no visibility rays,persistent reuse,physical timing or production changes.\n')
(Results / 'RgbQuartet-parity.txt').write_text(Summary)
Files = sorted(Results.glob('RgbQuartet*.gz')) + [Path(__file__)]
(Results / 'RgbQuartet-evidence-hashes.txt').write_text(''.join(hashlib.sha256(File.read_bytes()).hexdigest() + '  ' + str(File.relative_to(Root)) + '\n' for File in Files))
print(Summary, end='')
