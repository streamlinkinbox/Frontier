# 🧩 Audit positive-target weighted pairs, unchanged controls, actual buffers and quality/work rejection gates.
from pathlib import Path
import hashlib
import math
import re
import struct
import subprocess
import sys
import tempfile
from PIL import Image, ImageDraw, ImageFont
from WeightedPairFinite import ValidateWeightedPairFinite

Root=Path(__file__).resolve().parents[3]
Results=Root/'Exhibits/LightCompute/Results'
Images=Root/'Exhibits/LightCompute/Images'
Methods=('area8','bank8','rotate4','rotate2','fresh6','fresh8','fastarea8','fastbank8',
         'fastrotate4','fastrotate2','fastfresh6','fastfresh8','full2','rotate1')
Semantic,Hashes,Measurements,Paired=[],[],{},[]
for Mode in ('release','sanitized'):
    Text=(Results/f'WeightedPair-{Mode}.txt').read_text()
    assert '100% tests passed, 0 tests failed out of 5' in Text
    assert not re.search(r'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer',Text)
    Rows=[L for L in Text.splitlines() if L.startswith('WEIGHTED_PAIR_')]
    assert Rows[-1]=='WEIGHTED_PAIR_DECISION correctness=1 promote=0 physical_gpu_timing=0'
    Digests=re.findall(r'^([a-f0-9]{64})  (.+)$',Text,re.M)
    for Digest,Name in Digests:assert hashlib.sha256((Root/Name).read_bytes()).hexdigest()==Digest,Name
    Hashes.append(len(Digests));Semantic.append(Rows)
    Counts={'MEAN':0,'RESULT':0,'PAIRED':0,'CHECK':0,'COMPRESSION':0,'EDGE':0,'REFERENCE':0}
    References={}
    for Line in Rows:
        F=dict(re.findall(r'(\w+)=([^ ]+)',Line))
        Category=Line.split()[0].removeprefix('WEIGHTED_PAIR_')
        if Category in Counts:Counts[Category]+=1
        if Category=='REFERENCE':References[F['fixture'],F['pose']]=float(F['integral'])
        if Category=='MEAN':
            assert F['gate']=='1'
            assert abs(float(F['mean'])-References[F['fixture'],F['pose']])<4*float(F['nominal_se'])+.0001
        if Category=='RESULT':
            Measurements[F['fixture'],F['method']]=F
            assert all(math.isfinite(float(F[K])) for K in ('rmse','temporal_rmse','flicker_rmse','lag1'))
            assert int(F['queries'])>=int(F['source_queries']) and abs(float(F['lag1']))<=1
        if Category=='PAIRED' and Mode=='release':Paired.append(F)
        if Category=='CHECK':
            assert F['mean_gates']=='224' and float(F['maximum_bank8_difference'])<2e-6
            assert float(F['maximum_elision_difference'])==0
            assert int(F['matched_samples'])==6*32*1024*16 and int(F['retained_slot_checks'])==8519680
        if Category=='COMPRESSION':
            assert float(F['maximum_empty_difference'])<2e-6 and F['logical_bytes']=='48'
            assert int(F['retained'])==32*13*1024
        if Category=='EDGE':assert F['probes']=='1024' and F['safe_differences']=='0'
    assert Counts=={'MEAN':896,'RESULT':56,'PAIRED':16,'CHECK':4,'COMPRESSION':4,'EDGE':4,'REFERENCE':64},Counts
assert Semantic[0]==Semantic[1]
Historical=(Results/'VisibilityReuse-release.txt').read_text()
OldCaptureHashes={Path(N).name:H for H,N in re.findall(r'^([a-f0-9]{64})  (.+)$',Historical,re.M)}
Old=[L for L in Historical.splitlines() if L.startswith(('VISIBILITY_REUSE_MEAN ','VISIBILITY_REUSE_RESULT '))]
Current=[L.replace('WEIGHTED_PAIR_','VISIBILITY_REUSE_') for L in Semantic[0]
         if L.startswith(('WEIGHTED_PAIR_MEAN ','WEIGHTED_PAIR_RESULT ')) and not re.search(r'method=(full2|rotate1) ',L)]
assert Old==Current and len(Old)==816
(Root/'_ClaudeScratch/tmp').mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(dir=Root/'_ClaudeScratch/tmp') as Temporary:
    Shader=Path(Temporary)/'WeightedPair.slang'
    subprocess.run([sys.executable,str(Root/'Exhibits/LightCompute/Code/CompileWeightedPair.py'),str(Shader)],cwd=Root,check=True)
    for Mode in ('release','sanitized'):
        assert Shader.read_bytes()==(Root/f'_ClaudeScratch/build/weighted-pair-{Mode}/WeightedPair.slang').read_bytes()
FiniteCount,NegativeMeans=ValidateWeightedPairFinite()


def Float32(X):return struct.unpack('<f',struct.pack('<f',X))[0]


def Target(X,Y,Receiver):return 1/(math.pi*(1+(X-Receiver)**2+Y*Y)**2)


MaximumQ,MaximumWeight,MaximumZ,MaximumEstimate=0.,0.,0.,0.
SelectionChecks=0
Images.mkdir(exist_ok=True)
Font,Small=ImageFont.load_default(size=14),ImageFont.load_default(size=12)
Canvas=Image.new('RGB',(1200,760),'#121923');Pen=ImageDraw.Draw(Canvas)
Pen.text((18,14),'Actual .3 Vulkan holdout / weighted selection vs corrected controls / NOT camera images',font=Font,fill='white')
Pen.text((18,38),'32 x 32 logical trajectories, first cohort; grayscale x6 clipped; nearest-neighbor display only',font=Small,fill='white')
Panels=(8,7,11,10,12,13)
for Column,Method in enumerate(Panels):Pen.text((180+Column*166,69),Methods[Method],font=Small,fill='white')
Poses=(2,6,10,14)
for Row,Pose in enumerate(Poses):Pen.text((16,143+158*Row),f'Pose {Pose}',font=Small,fill='white')
for Fixture,Blocker in enumerate((0.,.12,.3,.6)):
    Blocker=Float32(Blocker)
    Previous=None
    for Pose in range(16):
        Captures=[]
        for Stem,Size in (('',14*1024*16),('Payload',6*1024*8*16),('Selected',2*1024*17*16)):
            Paths=[Root/f'_ClaudeScratch/weighted-pair-capture-{Mode}/WeightedPair{Stem}{Fixture}-{Pose}.f32' for Mode in ('release','sanitized')]
            Raw=Paths[0].read_bytes()
            assert Raw==Paths[1].read_bytes() and len(Raw)==Size
            Captures.append(Raw)
        Output,Bank,Selected=Captures
        assert hashlib.sha256(Output[:12*1024*16]).hexdigest()==OldCaptureHashes[f'VisibilityReuse{Fixture}-{Pose}.f32']
        assert hashlib.sha256(Bank).hexdigest()==OldCaptureHashes[f'VisibilityReusePayload{Fixture}-{Pose}.f32']
        assert Bank[:len(Bank)//2]==Bank[len(Bank)//2:]
        Receiver=Float32(.5-.5*math.cos(2*math.pi*Pose/16))
        for Arm in range(2):
            for Sample in range(1024):
                Offset=(Arm*1024+Sample)*17*16
                Estimate=0.
                for Lane in range(2):
                    C=struct.unpack_from('<4f',Selected,Offset+Lane*16)
                    Sum,H,Chosen,Anchor=struct.unpack_from('<4f',Selected,Offset+(11+Lane)*16)
                    Uniforms=struct.unpack_from('<4f',Selected,Offset+(13+Lane)*16)
                    Weights=struct.unpack_from('<4f',Selected,Offset+(15+Lane)*16)
                    Total,Index=0.,4
                    for Draw in range(4):
                        X,Y,L,Q=struct.unpack_from('<4f',Selected,Offset+(3+Lane*4+Draw)*16)
                        W=Weights[Draw]
                        assert 0<=Uniforms[Draw]<1 and W>=0
                        if Q==0:assert X==Y==L==W==0
                        else:
                            HExpected=Target(X,Y,Anchor)
                            QExpected=.25+(.75*HExpected if Blocker==0 or abs(X+Anchor)>2*Blocker else 0)
                            MaximumQ=max(MaximumQ,abs(Q-QExpected))
                            MaximumWeight=max(MaximumWeight,abs(W-HExpected/Q))
                        Total=Float32(Total+W)
                        if W>0 and Float32(Uniforms[Draw]*Total)<W:Index=Draw
                    assert Sum==Total and Chosen==Index
                    if Index==4:assert C==(0.,0.,0.,0.) and H==0
                    else:
                        Source=struct.unpack_from('<4f',Selected,Offset+(3+Lane*4+Index)*16)
                        assert C[:3]==Source[:3] and H>0
                        MaximumZ=max(MaximumZ,abs(C[3]-Total/(4*H)))
                        assert abs(H-Target(C[0],C[1],Anchor))<2e-6
                    SelectionChecks+=1
                    if C[3]>0 and (Blocker==0 or abs(C[0]+Receiver)>2*Blocker):
                        Estimate+=.6*C[2]*Target(C[0],C[1],Receiver)*C[3]/2
                    Born=Arm==0 or Pose in (0,9,13) or Lane==Pose%2
                    if Born:assert Anchor==Receiver
                    else:
                        assert Selected[Offset+Lane*16:Offset+(Lane+1)*16]==Previous[Offset+Lane*16:Offset+(Lane+1)*16]
                Measured=struct.unpack_from('<f',Output,((12+Arm)*1024+Sample)*16)[0]
                MaximumEstimate=max(MaximumEstimate,abs(Estimate-Measured))
        Previous=Selected
        if Fixture==2 and Pose in Poses:
            for Column,Method in enumerate(Panels):
                Pixels=[]
                for Sample in range(1024):
                    E=struct.unpack_from('<f',Output,(Method*1024+Sample)*16)[0]
                    Gray=min(255,max(0,round(E*6*255)));Pixels.append((Gray,)*3)
                Panel=Image.new('RGB',(32,32));Panel.putdata(Pixels)
                Name=Images/f'WeightedPair{Pose}-{Method}.png';Panel.save(Name)
                with Image.open(Name) as Retained:assert Retained.tobytes()==bytes(C for P in Pixels for C in P)
                Canvas.paste(Panel.resize((128,128),Image.Resampling.NEAREST),(180+Column*166,94+Poses.index(Pose)*158))
assert MaximumQ<2e-6 and MaximumWeight<2e-6 and MaximumZ<2e-6 and MaximumEstimate<2e-6
Canvas.save(Images/'WeightedPairReadbacks.png')
for Fixture in range(4):
    for Stem,Size in (('Edge',1024*16),('Rays',1024*5*16)):
        Paths=[Root/f'_ClaudeScratch/weighted-pair-capture-{Mode}/WeightedPair{Stem}{Fixture}.f32' for Mode in ('release','sanitized')]
        assert Paths[0].read_bytes()==Paths[1].read_bytes() and len(Paths[0].read_bytes())==Size
        assert hashlib.sha256(Paths[0].read_bytes()).hexdigest()==OldCaptureHashes[f'VisibilityReuse{Stem}{Fixture}.f32']

Lines=['# Measured weighted-pair comparison','',
       '| Fixture | Method | Instant RMSE | Temporal RMSE | Lag1 | Queries |',
       '|---------|--------|-------------:|--------------:|-----:|--------:|']
Acceptance=[]
for Fixture in range(4):
    Control=Measurements[str(Fixture),'fastfresh8'];Arm=Measurements[str(Fixture),'rotate1']
    Acceptance.append(float(Arm['rmse'])<=float(Control['rmse']) and
                      float(Arm['temporal_rmse'])<=float(Control['temporal_rmse']) and int(Arm['queries'])<=int(Control['queries']))
    for Method in Methods:
        F=Measurements[str(Fixture),Method]
        Lines.append(f"| {Fixture} | {Method} | {float(F['rmse']):.7f} | {float(F['temporal_rmse']):.7f} | {float(F['lag1']):.4f} | {int(F['queries']):,} |")
(Results/'WeightedPair-comparison.md').write_text('\n'.join(Lines)+'\n')
Canvas=Image.new('RGB',(1200,960),'#121923');Pen=ImageDraw.Draw(Canvas)
Pen.text((20,14),'Temporal error vs actual query work / corrected fresh8 = 1 / lower-left preferable / not GPU timing',font=Font,fill='white')
PlotMethods=('fastarea8','fastrotate4','fastfresh6','fastfresh8','full2','rotate1')
Colors=('#95b5db','#77d3a6','#c8a0ed','#f1a187','#e7ce72','#f27ba8')
for Fixture in range(4):
    Control=Measurements[str(Fixture),'fastfresh8']
    Ratios=[(int(Measurements[str(Fixture),M]['queries'])/int(Control['queries']),float(Measurements[str(Fixture),M]['temporal_rmse'])/float(Control['temporal_rmse'])) for M in PlotMethods]
    Left,Right=85+(Fixture%2)*600,510+(Fixture%2)*600
    Top,Bottom=100+(Fixture//2)*440,390+(Fixture//2)*440
    XMin,XMax=min(X for X,Y in Ratios)*.7,max(X for X,Y in Ratios)*1.15
    YMin,YMax=min(Y for X,Y in Ratios)*.7,max(Y for X,Y in Ratios)*1.15
    Pen.text((Left,Top-42),f'Strip {(0,.12,.3,.6)[Fixture]} / mean-residual RMSE ratio',font=Font,fill='white')
    for Tick in range(5):
        X,Y=Left+Tick/4*(Right-Left),Bottom-Tick/4*(Bottom-Top)
        Pen.line((X,Top,X,Bottom),fill='#35404d');Pen.line((Left,Y,Right,Y),fill='#35404d')
        Pen.text((X-14,Bottom+8),f'{XMin+Tick/4*(XMax-XMin):.2f}',font=Small,fill='white')
        Pen.text((Left-40,Y-8),f'{YMin+Tick/4*(YMax-YMin):.2f}',font=Small,fill='white')
    for Index,(Q,E) in enumerate(Ratios):
        X,Y=Left+(Q-XMin)/(XMax-XMin)*(Right-Left),Bottom-(E-YMin)/(YMax-YMin)*(Bottom-Top)
        Pen.ellipse((X-4,Y-4,X+4,Y+4),fill=Colors[Index])
        Pen.text((X+7,Y+(10 if Index in (4,5) else -15)),PlotMethods[Index],font=Small,fill=Colors[Index])
    Pen.text((Left+85,Bottom+40),'Nearest-facet query ratio',font=Small,fill='white')
Pen.text((20,925),'Cold/light rebuilds included. Different axis ranges per panel. Logical 48B is NOT measured allocation/bandwidth.',font=Small,fill='white')
Canvas.save(Images/'WeightedPairFrontier.png')
Message=(f'PASS correctness:rows={len(Semantic[0])} identical;hashes={Hashes};896 mean gates;816 old control rows unchanged.\n'
         f'200 byte-identical captures;136 old control/diagnostic capture hashes unchanged;24 PNG arrays exact;selected replay checks={SelectionChecks};finite configurations={FiniteCount}.\n'
         f'Max q/weight/Z/estimate errors={MaximumQ:.12g}/{MaximumWeight:.12g}/{MaximumZ:.12g}/{MaximumEstimate:.12g}.\n'
         f'Negative finite means={NegativeMeans};correct=3.9;all three incorrect estimators rejected.\n'
         f'Rotate1 acceptance per fixture={Acceptance};decision={"ACCEPT_RESTRICTED" if all(Acceptance) else "REJECT_QUALITY"};production_promote=0.\n'
         'Both native commands exit0,five CTests each;shader regenerated exactly;no physical-GPU timing/allocation claim.\n')
(Results/'WeightedPair-parity.txt').write_text(Message)
print(Message,end='')
