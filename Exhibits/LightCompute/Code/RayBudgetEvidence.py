# 🧩 Retain a reproducible numerical-boundary rejection; do not relabel a failed runtime gate as passing.
from pathlib import Path
import hashlib
import re
import struct
import subprocess
import sys
import tempfile
from PIL import Image, ImageDraw, ImageFont

Root = Path(__file__).resolve().parents[3]
Results = Root / 'Exhibits/LightCompute/Results'
Images = Root / 'Exhibits/LightCompute/Images'
Failure = 'First-hit/recheck numerical edge disagreement blocks query elision'
Semantic, HashCounts, RawHashes = [], [], []
for Mode in ('release', 'sanitized'):
    Text = (Results / f'RayBudget-{Mode}.txt').read_text()
    assert Text.rstrip().endswith(Failure)
    assert '100% tests passed, 0 tests failed out of 5' in Text
    assert not re.search(r'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer', Text)
    assert not re.search(r'^RAY_BUDGET_(MEAN|RESULT|DECISION)', Text, re.M)
    Hashes = re.findall(r'^([a-f0-9]{64})  (.+)$', Text, re.M)
    for Digest, Name in Hashes:
        assert hashlib.sha256((Root / Name).read_bytes()).hexdigest() == Digest, Name
    HashCounts.append(len(Hashes))
    Rows = [L for L in Text.splitlines() if L.startswith('RAY_BUDGET_')]
    Semantic.append(Rows)
    Fixtures, Details = [], []
    for Line in Rows:
        F = dict(re.findall(r'(\w+)=([^ ]+)', Line))
        if Line.startswith('RAY_BUDGET_EDGE '):
            Fixtures.append(F)
        if Line.startswith('RAY_BUDGET_EDGE_DETAIL '):
            Details.append(F)
    assert len(Fixtures) == 4 and len(Details) == 3
    assert [int(F['disagreements']) for F in Fixtures] == [0, 0, 3, 0]
    assert [int(F['hits']) for F in Fixtures] == [561, 335, 215, 77]
    assert [int(F['pixel']) for F in Details] == [918, 938, 958]
    for F in Details:
        assert F['fixture'] == '2' and F['first'] == '1' and F['recheck'] == '0'
        assert float(F['receiver_x']) == 1 and abs(float(F['target_x']) + .399999887) < 1e-9
assert Semantic[0] == Semantic[1]
with tempfile.TemporaryDirectory(dir=Root / '_ClaudeScratch/tmp') as Temporary:
    Shader = Path(Temporary) / 'RayBudget.slang'
    subprocess.run([sys.executable, str(Root / 'Exhibits/LightCompute/Code/CompileRayBudget.py'), str(Shader)],
                   cwd=Root, check=True)
    for Mode in ('release', 'sanitized'):
        assert Shader.read_bytes() == (Root / f'_ClaudeScratch/build/ray-budget-{Mode}/RayBudget.slang').read_bytes()

Images.mkdir(exist_ok=True)
Canvas = Image.new('RGB', (1120, 470), '#121923')
Pen = ImageDraw.Draw(Canvas)
Font = ImageFont.load_default(size=15)
Small = ImageFont.load_default(size=13)
Pen.text((18, 15), 'Actual Vulkan ray-boundary probes / blanket query elision REJECTED / NOT camera images', font=Font, fill='white')
Pen.text((18, 42), '32 x 32 ordered probes per fixture; nearest-neighbor display. Same boundary x tested at three y coordinates.', font=Small, fill='white')
for Fixture, Width in enumerate((0, .12, .3, .6)):
    Paths = [Root / f'_ClaudeScratch/ray-budget-capture-{Mode}/RayBudgetEdge{Fixture}.f32'
             for Mode in ('release', 'sanitized')]
    Raw = Paths[0].read_bytes()
    assert Raw == Paths[1].read_bytes() and len(Raw) == 1024 * 16
    RawHashes.append((Fixture, hashlib.sha256(Raw).hexdigest()))
    Pixels, Hits, Differences = [], 0, []
    for Pixel, (First, Recheck, Queries, Spare) in enumerate(struct.iter_unpack('<4f', Raw)):
        assert First in (0, 1) and Recheck in (0, 1) and Queries == 1 + First and Spare == 0
        Hits += int(First)
        if First != Recheck:
            Differences.append(Pixel)
        Pixels.append((240, 83, 89) if First != Recheck else (103, 199, 150) if First else (23, 31, 43))
    assert Differences == ([918, 938, 958] if Fixture == 2 else [])
    assert Hits == (561, 335, 215, 77)[Fixture]
    Panel = Image.new('RGB', (32, 32)); Panel.putdata(Pixels)
    Name = Images / f'RayBudgetEdge{Fixture}.png'; Panel.save(Name)
    with Image.open(Name) as Retained:
        assert Retained.tobytes() == bytes(C for P in Pixels for C in P)
    Left = 20 + Fixture * 276
    Pen.text((Left, 83), f'Strip half-width {Width:g}', font=Font, fill='white')
    Canvas.paste(Panel.resize((256, 256), Image.Resampling.NEAREST), (Left, 110))
    Pen.text((Left, 378), f'{Hits} hits / {len(Differences)} disagreements', font=Small, fill='white')
Pen.text((20, 421), 'Green: valid first hit + recheck agree. Dark: rejected first hit. Red: first hit accepted, recheck rejected.', font=Small, fill='white')
Pen.text((20, 446), 'Edge-enriched deterministic set, not a failure-rate estimate. Both runtime commands exit 1 at the safety gate.', font=Small, fill='white')
Canvas.save(Images / 'RayBudgetEdges.png')
Message = (f'REPRODUCED REJECTION, NOT RUNTIME PASS: {len(Semantic[0])} identical diagnostic rows; '
           f'{HashCounts} logged hashes verified; five CTests/build pass; runtime safety gate rejects in both builds.\n'
           '4096 retained probe entries, 1188 valid first hits, three disagreements at one x boundary and three y coordinates; '
           'four raw arrays byte-identical, four PNG arrays exact, shader rederivation exact.\n'
           '12-arm trajectories, matched estimator/noise/candidate comparisons and work reductions NOT RUN; '
           'blocked before those checks. No production promotion or physical-GPU timing.\n')
for Fixture, Digest in RawHashes:
    Message += f'capture_sha256 fixture={Fixture} sha256={Digest}\n'
(Results / 'RayBudget-parity.txt').write_text(Message)
print(Message, end='')
