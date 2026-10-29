# 🧩 Verify the ray-direction cause, preserved visibility contract, matched candidate banks and fair work comparison.
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
Methods = ('area8', 'bank8', 'rotate4', 'rotate2', 'fresh6', 'fresh8',
           'fastarea8', 'fastbank8', 'fastrotate4', 'fastrotate2', 'fastfresh6', 'fastfresh8')
Semantic, HashCounts, Measurements, Contrasts = [], [], {}, []
for Mode in ('release', 'sanitized'):
    Text = (Results / f'VisibilityReuse-{Mode}.txt').read_text()
    assert '100% tests passed, 0 tests failed out of 5' in Text
    assert not re.search(r'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer', Text)
    Rows = [L for L in Text.splitlines() if L.startswith('VISIBILITY_REUSE_')]
    assert Rows[-1] == 'VISIBILITY_REUSE_DECISION correctness=1 promote=0 physical_gpu_timing=0'
    Hashes = re.findall(r'^([a-f0-9]{64})  (.+)$', Text, re.M)
    for Digest, Name in Hashes:
        assert hashlib.sha256((Root / Name).read_bytes()).hexdigest() == Digest, Name
    HashCounts.append(len(Hashes))
    Reference, MeanCount, CheckCount, EdgeCount, PairCount = {}, 0, 0, 0, 0
    for Line in Rows:
        F = dict(re.findall(r'(\w+)=([^ ]+)', Line))
        if Line.startswith('VISIBILITY_REUSE_REFERENCE '):
            Reference[F['fixture'], F['pose']] = float(F['integral'])
        if Line.startswith('VISIBILITY_REUSE_MEAN '):
            MeanCount += 1
            assert F['gate'] == '1'
            assert abs(float(F['mean']) - Reference[F['fixture'], F['pose']]) < 4*float(F['nominal_se']) + .0001
        if Line.startswith('VISIBILITY_REUSE_RESULT '):
            Measurements[F['fixture'], F['method']] = F
            assert all(math.isfinite(float(F[K])) for K in ('rmse', 'temporal_rmse', 'flicker_rmse', 'lag1'))
            assert int(F['queries']) >= int(F['source_queries']) and abs(float(F['lag1'])) <= 1
            Local = Methods.index(F['method']) % 6
            assert int(F['reused']) == 32*1024*13*(4 if Local==2 else 6 if Local==3 else 0)
        if Line.startswith('VISIBILITY_REUSE_PAIRED '):
            PairCount += 1
            if Mode == 'release': Contrasts.append(F)
        if Line.startswith('VISIBILITY_REUSE_CHECK '):
            CheckCount += 1
            assert F['mean_gates'] == '192' and float(F['maximum_bank8_difference']) < 2e-6
            assert float(F['maximum_elision_difference']) < 2e-6
            assert int(F['matched_samples']) == 6*32*1024*16
            assert int(F['retained_slot_checks']) == 2*32*13*10*1024
        if Line.startswith('VISIBILITY_REUSE_EDGE '):
            EdgeCount += 1
            assert F['safe_differences'] == '0' and F['probes'] == '1024'
    assert (len(Reference), MeanCount, CheckCount, EdgeCount, PairCount) == (64, 768, 4, 4, 16)
    Semantic.append(Rows)
assert Semantic[0] == Semantic[1] and len(Measurements) == 48
Old = []
for Line in (Results / 'ReceiverBank-release.txt').read_text().splitlines():
    if Line.startswith(('RECEIVER_BANK_MEAN ', 'RECEIVER_BANK_RESULT ')):
        Old.append(Line.replace('RECEIVER_BANK_', 'VISIBILITY_REUSE_'))
New = [L for L in Semantic[0] if L.startswith(('VISIBILITY_REUSE_MEAN ', 'VISIBILITY_REUSE_RESULT '))
       and not re.search(r'method=fast', L)]
assert Old == New and len(New) == 408
for Fixture in range(4):
    for Local in range(6):
        A, B = (Measurements[str(Fixture), Methods[Local+Shift]] for Shift in (0, 6))
        for K in ('rmse', 'temporal_rmse', 'flicker_rmse', 'lag1', 'reused'): assert A[K] == B[K], (Fixture, Local, K)
        assert int(B['queries']) <= int(A['queries'])
with tempfile.TemporaryDirectory(dir=Root / '_ClaudeScratch/tmp') as Temporary:
    Shader = Path(Temporary) / 'VisibilityReuse.slang'
    subprocess.run([sys.executable, str(Root/'Exhibits/LightCompute/Code/CompileVisibilityReuse.py'), str(Shader)], cwd=Root, check=True)
    for Mode in ('release', 'sanitized'):
        assert Shader.read_bytes() == (Root/f'_ClaudeScratch/build/visibility-reuse-{Mode}/VisibilityReuse.slang').read_bytes()

Images.mkdir(exist_ok=True)
Font, Small = ImageFont.load_default(size=14), ImageFont.load_default(size=12)
Canvas = Image.new('RGB', (1200, 760), '#121923'); Pen = ImageDraw.Draw(Canvas)
Pen.text((18, 14), 'Actual Vulkan .3 holdout / identical estimates after confirmed visibility reuse / NOT camera images', font=Font, fill='white')
Pen.text((18, 38), 'Linear grayscale x6, clipped; 32 x 32 logical trajectories, first cohort; no interpolation', font=Small, fill='white')
Panels = (2, 8, 6, 9, 10, 11)
Labels = ('Legacy rotate4', 'Reuse rotate4', 'Reuse area8', 'Reuse rotate2', 'Reuse fresh6', 'Reuse fresh8')
for Column, Label in enumerate(Labels): Pen.text((180+Column*166, 69), Label, font=Small, fill='white')
SelectedPoses = (2, 6, 10, 14)
for Row, Pose in enumerate(SelectedPoses): Pen.text((16, 143+158*Row), f'Pose {Pose}', font=Small, fill='white')
MaximumEstimateDifference, CandidateChecks = 0., 0
for Fixture in range(4):
    for Pose in range(16):
        Paths = [Root/f'_ClaudeScratch/visibility-reuse-capture-{Mode}/VisibilityReuse{Fixture}-{Pose}.f32' for Mode in ('release','sanitized')]
        Raw = Paths[0].read_bytes(); assert Raw == Paths[1].read_bytes() and len(Raw)==12*1024*16
        PayloadPaths = [Root/f'_ClaudeScratch/visibility-reuse-capture-{Mode}/VisibilityReusePayload{Fixture}-{Pose}.f32' for Mode in ('release','sanitized')]
        Payload = PayloadPaths[0].read_bytes()
        assert Payload == PayloadPaths[1].read_bytes() and len(Payload)==6*1024*8*16
        assert Payload[:len(Payload)//2] == Payload[len(Payload)//2:]
        CandidateChecks += 3*1024*8
        for Local in range(6):
            for Sample in range(1024):
                A = struct.unpack_from('<4f',Raw,(Local*1024+Sample)*16)
                B = struct.unpack_from('<4f',Raw,((Local+6)*1024+Sample)*16)
                assert all(math.isfinite(V) and V>=0 for V in A+B)
                MaximumEstimateDifference=max(MaximumEstimateDifference,abs(A[0]-B[0]))
                assert abs(A[0]-B[0])<2e-6 and B[1]<=A[1] and A[2]==B[2]
                if Local>=4: assert 0<=A[1]-B[1]<= (6 if Local==4 else 8)
        if Fixture==2 and Pose in SelectedPoses:
            for Column, Method in enumerate(Panels):
                Pixels=[]
                for Sample in range(1024):
                    E=struct.unpack_from('<f',Raw,(Method*1024+Sample)*16)[0]
                    G=min(255,max(0,round(E*6*255)));Pixels.append((G,G,G))
                Panel=Image.new('RGB',(32,32));Panel.putdata(Pixels)
                Name=Images/f'VisibilityReuse{Pose}-{Method}.png';Panel.save(Name)
                with Image.open(Name) as Retained:assert Retained.tobytes()==bytes(C for P in Pixels for C in P)
                Canvas.paste(Panel.resize((128,128),Image.Resampling.NEAREST),(180+Column*166,94+SelectedPoses.index(Pose)*158))
Canvas.save(Images/'VisibilityReuseReadbacks.png')

# Actual same-origin/same-range ray diagnostics isolate direction changes, not a changed distance cutoff.
RayLines=['# Recorded ray-direction diagnostics','',
          '| Probe | Original x bits | Reconstructed x bits | Original short hit | Reconstructed short hit |',
          '|------:|----------------:|---------------------:|-------------------:|------------------------:|']
ULPRegressions=0
for Fixture in range(4):
    for Stem, Size in (('Edge',1024*16),('Rays',1024*5*16)):
        Paths=[Root/f'_ClaudeScratch/visibility-reuse-capture-{Mode}/VisibilityReuse{Stem}{Fixture}.f32' for Mode in ('release','sanitized')]
        assert Paths[0].read_bytes()==Paths[1].read_bytes() and len(Paths[0].read_bytes())==Size
    Edge=(Root/f'_ClaudeScratch/visibility-reuse-capture-release/VisibilityReuseEdge{Fixture}.f32').read_bytes()
    Rays=(Root/f'_ClaudeScratch/visibility-reuse-capture-release/VisibilityReuseRays{Fixture}.f32').read_bytes()
    for Pixel,(First,Recheck,Safe,Queries) in enumerate(struct.iter_unpack('<4f',Edge)):
        assert First in (0,1) and Recheck in (0,1) and Safe==Recheck and Queries==(5 if First else 1)
        if Fixture==2 and Pixel in (918,938,958):
            F=struct.unpack_from('<20f',Rays,Pixel*80)
            assert First==1 and Recheck==Safe==0
            BitsA=struct.unpack_from('<I',Rays,Pixel*80)[0]
            BitsB=struct.unpack_from('<I',Rays,Pixel*80+32)[0]
            assert BitsB-BitsA==1 and F[1:3]==F[9:11]
            assert F[13]==-1 and F[14]==2 and F[12] in (0,1)
            ULPRegressions+=1
            RayLines.append(f'| {Pixel} | 0x{BitsA:08x} | 0x{BitsB:08x} | miss | blocker facet2 |')
assert ULPRegressions==3
(Results/'VisibilityReuse-rays.md').write_text('\n'.join(RayLines)+'\n')

# Same estimator quality, now compare actual work against BOTH optimized sides.
Canvas=Image.new('RGB',(1200,530),'#121923');Pen=ImageDraw.Draw(Canvas)
Pen.text((20,14),'Confirmed visibility reuse: temporal error versus actual query work / fast fresh8 = 1 / lower-left preferable',font=Font,fill='white')
Colors=('#b2becf','#95b5db','#77d3a6','#e3bd79','#c8a0ed','#f1a187')
for Panel,Fixture in enumerate((0,2)):
    Control=Measurements[str(Fixture),'fastfresh8']
    Ratios=[(int(Measurements[str(Fixture),M]['queries'])/int(Control['queries']),float(Measurements[str(Fixture),M]['temporal_rmse'])/float(Control['temporal_rmse'])) for M in Methods[6:]]
    Left,Right,Top,Bottom=85+Panel*600,510+Panel*600,100,390
    XMin,XMax=min(X for X,Y in Ratios)*.7,max(X for X,Y in Ratios)*1.2
    YMin,YMax=min(Y for X,Y in Ratios)*.75,max(Y for X,Y in Ratios)*1.2
    Pen.text((Left,58),'No strip' if Fixture==0 else '.3 strip holdout',font=Font,fill='white')
    Pen.text((Left,80),'16-pose mean-residual RMSE ratio',font=Small,fill='white')
    for Tick in range(5):
        X,Y=Left+Tick/4*(Right-Left),Bottom-Tick/4*(Bottom-Top)
        Pen.line((X,Top,X,Bottom),fill='#35404d');Pen.line((Left,Y,Right,Y),fill='#35404d')
        Pen.text((X-14,Bottom+8),f'{XMin+Tick/4*(XMax-XMin):.2f}',font=Small,fill='white')
        Pen.text((Left-40,Y-8),f'{YMin+Tick/4*(YMax-YMin):.2f}',font=Small,fill='white')
    for Index,(Q,E) in enumerate(Ratios):
        X,Y=Left+(Q-XMin)/(XMax-XMin)*(Right-Left),Bottom-(E-YMin)/(YMax-YMin)*(Bottom-Top)
        Pen.ellipse((X-4,Y-4,X+4,Y+4),fill=Colors[Index]);Pen.text((X+7,Y+(12 if Index==1 else -15)),Methods[Index],font=Small,fill=Colors[Index])
    Pen.text((Left+85,430),'Nearest-facet query ratio',font=Small,fill='white')
Pen.text((20,478),'Both baseline and bank use confirmed visibility reuse; cold/periodic/revision costs included.',font=Small,fill='white')
Pen.text((20,502),'Not physical-GPU timing or production TAA. Same paired estimates; different axis ranges per panel.',font=Small,fill='white')
Canvas.save(Images/'VisibilityReuseFrontier.png')
Lines=['# Confirmed visibility reuse comparison','',
       '| Fixture | Method | Instant RMSE | Temporal RMSE | Lag1 | BVH queries |',
       '|---------|--------|-------------:|--------------:|-----:|------------:|']
for Fixture in range(4):
    for Method in Methods:
        F=Measurements[str(Fixture),Method]
        Lines.append(f"| {Fixture} | {Method} | {float(F['rmse']):.7f} | {float(F['temporal_rmse']):.7f} | {float(F['lag1']):.4f} | {int(F['queries']):,} |")
(Results/'VisibilityReuse-comparison.md').write_text('\n'.join(Lines)+'\n')
Message=(f'PASS confirmed visibility reuse: {len(Semantic[0])} identical semantic rows; hashes {HashCounts}; '
         '768 mean gates,12582912 matched estimates,34078720 retained-slot checks; '
         '408 legacy rows unchanged;24 legacy/fast quality summaries identical.\n'
         f'136 captures byte-identical,24 PNG arrays exact;{CandidateChecks} captured candidate pairs equal; '
         f'max captured estimate difference={MaximumEstimateDifference:.12g}.\n'
         '4096 confirmed-visibility regressions,original three first-hit disagreements retained; '
         'three x-direction one-ULP changes isolate original-short miss vs reconstructed-short blocker2.\n'
         'Both native commands exit0,five CTests each;shader regenerated exactly. '
         'No production promotion or physical-GPU timing.\n')
(Results/'VisibilityReuse-parity.txt').write_text(Message)
print(Message,end='')
