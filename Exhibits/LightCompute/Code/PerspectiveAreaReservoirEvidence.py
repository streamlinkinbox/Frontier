# 🧩 Audit camera-space GPU RIS,independent references and exact displayed image arrays.
from pathlib import Path
import gzip
import hashlib
import math
import re
import struct
from PIL import Image, ImageDraw, ImageFont

Root=Path(__file__).resolve().parents[3]
Results=Root/'Exhibits/LightCompute/Results'
Images=Root/'Exhibits/LightCompute/Images'
Prefix='PerspectiveAreaReservoir'
Count=24576
Methods=('RIS-uniform','RIS-area','raw4-uniform','raw4-area','raw8-uniform','raw8-area')
Rows=[];HashCounts=[]
for Mode in ('release','sanitized'):
    Text=(Results/f'{Prefix}-{Mode}.txt').read_text()
    assert '100% tests passed, 0 tests failed out of 5' in Text
    assert not re.search(r'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer',Text)
    Semantic=[L for L in Text.splitlines() if L.startswith('PERSPECTIVE_AREA_')]
    assert len(Semantic)==9 and 'correctness=1 candidates=491520 traced=393216' in Semantic[-1]
    Rows.append(Semantic)
    Hashes=re.findall(r'^([a-f0-9]{64})  (.+)$',Text,re.M)
    for Digest,Name in Hashes:assert hashlib.sha256((Root/Name).read_bytes()).hexdigest()==Digest,Name
    HashCounts.append(len(Hashes))
assert Rows[0]==Rows[1]
Occlusion=[L for L in Rows[0] if L.startswith('PERSPECTIVE_AREA_OCCLUSION')][0]
OcclusionFields=dict(re.findall(r'(\w+)=([^ ]+)',Occlusion))
assert int(OcclusionFields['receiver_suffix_blocks'])>0
assert int(OcclusionFields['changed'])>0 and int(OcclusionFields['wall_primaries'])>0
assert OcclusionFields['primary_checks']=='73728' and OcclusionFields['reference_primary_checks']=='2359296'

# 📝 Verify nominal packet seed domains are disjoint before using cross-reference errors.
for Case in range(3):
    Experimental={(P+0x4a615e23)^(Case*0x6c8e9cf5 & 0xffffffff) for P in range(4096)}
    Banks=[]
    for Bank in range(2):
        Seeds={((P+0x174b6d39)^((Bank*16+Batch)*0x9e3779b9 & 0xffffffff)^                (Case*0x6c8e9cf5 & 0xffffffff)) for P in range(24576) for Batch in range(16)}
        assert len(Seeds)==393216 and not Seeds.intersection(Experimental)
        Banks.append(Seeds)
    assert not Banks[0].intersection(Banks[1])
for Stem in ('DiffuseGenerate','DiffuseResolve'):
    Modules=[Root/f'_ClaudeScratch/build/perspective-area-reservoir-{M}/{Stem}.spv' for M in ('release','sanitized')]
    assert Modules[0].read_bytes()==Modules[1].read_bytes()
    (Results/f'{Prefix}-{Stem}.spv.gz').write_bytes(gzip.compress(Modules[0].read_bytes(),mtime=0))
for L in (Results/'GeometricAreaReservoir-evidence-hashes.txt').read_text().splitlines():
    Digest,Name=L.split('  ',1)
    assert hashlib.sha256((Root/Name).read_bytes()).hexdigest()==Digest,Name

def Readback(Stem,Case,Size):
    Files=[Root/f'_ClaudeScratch/perspective-area-reservoir-capture-{M}/{Stem}{Case}.f32' for M in ('release','sanitized')]
    Raw=Files[0].read_bytes()
    assert len(Raw)==Size and Raw==Files[1].read_bytes()
    (Results/f'{Stem}{Case}.f32.gz').write_bytes(gzip.compress(Raw,mtime=0))
    return Raw

def PixelTriples(Raw,Offset=0):
    return [struct.unpack_from('<3f',Raw,Offset+P*16) for P in range(1024)]

def AveragePacket(Raw,Method,Slab=0):
    Samples=[PixelTriples(Raw,(Slab*Count+Method*4096+Repeat*1024)*16) for Repeat in range(4)]
    return [[sum(S[P][C] for S in Samples)/4 for C in range(3)] for P in range(1024)]

def MeanGate(Estimate,Reference):
    Passed=True;Measurements=[]
    for C in range(3):
        Differences=[A[C]-B[C] for A,B in zip(Estimate,Reference)]
        Mean=sum(Differences)/1024
        SE=math.sqrt(sum((D-Mean)**2 for D in Differences)/(1023*1024))
        Gate=abs(Mean)<=6*SE+2e-5
        Passed &= Gate
        Measurements.append((Mean,SE,Gate))
    return Passed,Measurements

def StoreImage(Pixels,Name,Error=False):
    Encoded=[]
    for P in Pixels:
        assert all(math.isfinite(C) and C>=0 for C in P)
        Encoded.append(tuple(round(255*min(1,C*(1024 if Error else 64))) for C in P))
    Output=Image.new('RGB',(32,32));Output.putdata(Encoded)
    Output.save(Images/Name)
    assert Image.open(Images/Name).tobytes()==bytes(C for P in Encoded for C in P)
    return Output

Font=ImageFont.load_default(size=14)
Small=ImageFont.load_default(size=12)
Canvas=Image.new('RGB',(1240,850),'#121923');Pen=ImageDraw.Draw(Canvas)
Pen.text((16,10),'Actual camera RGB / 32x32,nearest enlarged / linear exposure64,clipped / no denoiser',font=Font,fill='white')
Errors=Image.new('RGB',(1240,850),'#121923');ErrorPen=ImageDraw.Draw(Errors)
ErrorPen.text((16,10),'Absolute RGB error against finite combined reference / linear scale1024,clipped',font=Font,fill='white')
Table=['|Layout|Arm|Calls/pixel|Image RMSE|Cross-reference MSE|Mean gates|','|---:|---|---:|---:|---:|---|']
Means=['|Layout|Arm|Channel|Mean residual|SE|Gate|','|---:|---|---:|---:|---:|---|']
Ratios=['|Layout|Uniform RIS/raw4 RMSE|Area RIS/raw4 RMSE|Uniform cross-MSE ratio|Area cross-MSE ratio|',
        '|---:|---:|---:|---:|---:|']
Intervals=['|Layout|Proposal|Paired cross-MSE delta|Tile SE|Approx95% interval|',
           '|---:|---|---:|---:|---|']
Metrics=[];CrossMetrics=[];NegativeFailures=[0,0];MeanGates=0;ReferenceNoise=[];Wins=[0,0]
for Case in range(3):
    Raw=Readback(Prefix,Case,23*Count*16)
    Primary=[]
    for P in range(1024):
        Records=[struct.unpack_from('<4f',Raw,((19+Slab)*Count+P)*16) for Slab in range(4)]
        assert all(math.isfinite(C) for R in Records for C in R)
        assert Records[0][1]>0 and Records[0][1]<10
        assert Records[1][3]==1 and Records[2][3]==0 and Records[3][3]==0
        assert abs(sum(C*C for C in Records[2][:3])-1)<2e-5
        Eye=((-0.3,0,1.2),(-0.6,0.15,1),(-0.4,-0.25,1.3))[Case]
        Centre,Half=(0.25,1.25) if Case==2 else (-0.35,1)
        Plane=(Centre+Half*(2*((P%32+.5)/32)-1),1.2*(2*((P//32+.5)/32)-1),4)
        Position=Records[1][:3]
        Scale=(4-Eye[2])/(Position[2]-Eye[2])
        Projected=[Eye[A]+(Position[A]-Eye[A])*Scale for A in range(3)]
        assert max(abs(A-B) for A,B in zip(Projected,Plane))<2e-5
        Primary.append(Records)
    for Arm in range(6):
        for Repeat in range(4):
            for Slab in range(4):
                First=((19+Slab)*Count+Arm*4096+Repeat*1024)*16
                Second=(19+Slab)*Count*16
                assert Raw[First:First+1024*16]==Raw[Second:Second+1024*16]
    for Label in ('normal','depth','material'):
        Encoded=[]
        for R in Primary:
            Colour=([(C+1)/2 for C in R[2][:3]] if Label=='normal' else
                    [(R[1][2]-3.2)/1.6]*3 if Label=='depth' else list(R[3][:3]))
            Encoded.append(tuple(round(255*min(1,max(0,C))) for C in Colour))
        Output=Image.new('RGB',(32,32));Output.putdata(Encoded)
        Output.save(Images/f'{Prefix}-{Label}-{Case}.png')
        assert Image.open(Images/f'{Prefix}-{Label}-{Case}.png').tobytes()==bytes(C for P in Encoded for C in P)
    ReferenceRaw=Readback('PerspectiveAreaReference',Case,2*1024*16)
    Banks=[PixelTriples(ReferenceRaw,B*1024*16) for B in range(2)]
    Reference=[[sum(B[P][C] for B in Banks)/2 for C in range(3)] for P in range(1024)]
    ReferenceNoise.append(math.sqrt(sum((A[C]-B[C])**2 for A,B in zip(*Banks) for C in range(3))/(4*3072)))
    RefImage=StoreImage(Reference,f'{Prefix}-reference-{Case}.png')
    Y=70+Case*245
    for PenInstance,CanvasInstance in ((Pen,Canvas),(ErrorPen,Errors)):
        PenInstance.text((12,Y-22),f'Layout{Case}',font=Font,fill='white')
        PenInstance.text((12,Y+154),'Reference',font=Small,fill='white')
        CanvasInstance.paste(RefImage.resize((144,144),Image.Resampling.NEAREST),(12,Y))
    Rmses=[];Cross=[];Estimates=[]
    for Method in range(6):
        Estimate=AveragePacket(Raw,Method)
        Estimates.append(Estimate)
        Pass,Measurements=MeanGate(Estimate,Reference)
        assert Pass,(Case,Method,Measurements)
        MeanGates+=3
        for C,(Mean,SE,Gate) in enumerate(Measurements):
            Means.append(f'|{Case}|{Methods[Method]}|{C}|{Mean:.8g}|{SE:.8g}|{Gate}|')
        if Method<2:
            Wrong=AveragePacket(Raw,Method,1)
            if not MeanGate(Wrong,Reference)[0]:NegativeFailures[Method]+=1
        RMSE=math.sqrt(sum((A[C]-B[C])**2 for A,B in zip(Estimate,Reference) for C in range(3))/3072)
        CrossMSE=sum((E[C]-A[C])*(E[C]-B[C]) for E,A,B in zip(Estimate,*Banks) for C in range(3))/3072
        Rmses.append(RMSE);Cross.append(CrossMSE)
        ImagePixels=StoreImage(Estimate,f'{Prefix}-{Case}-{Method}.png')
        ErrorPixels=StoreImage([[abs(A[C]-B[C]) for C in range(3)] for A,B in zip(Estimate,Reference)],
                               f'{Prefix}-error-{Case}-{Method}.png',True)
        X=188+Method*174
        for Display,Sheet,Writer in ((ImagePixels,Canvas,Pen),(ErrorPixels,Errors,ErrorPen)):
            Sheet.paste(Display.resize((144,144),Image.Resampling.NEAREST),(X,Y))
            Writer.text((X,Y+154),Methods[Method],font=Small,fill='white')
            Writer.text((X,Y+172),f'RMSE {RMSE:.5g}',font=Small,fill='white')
        Table.append(f'|{Case}|{Methods[Method]}|{68 if Method>=4 else 36}|{RMSE:.9g}|{CrossMSE:.9g}|PASS3/3|')
    for Proposal in range(2):
        Differences=[]
        for P in range(1024):
            def CrossError(Estimate):
                return sum((Estimate[P][C]-Banks[0][P][C])*(Estimate[P][C]-Banks[1][P][C]) for C in range(3))/3
            Differences.append(CrossError(Estimates[Proposal])-CrossError(Estimates[Proposal+2]))
        Blocks=[sum(Differences[(Y*4+DY)*32+X*4+DX] for DY in range(4) for DX in range(4))/16
                for Y in range(8) for X in range(8)]
        Mean=sum(Blocks)/64
        SE=math.sqrt(sum((B-Mean)**2 for B in Blocks)/(63*64))
        Intervals.append(f'|{Case}|{Methods[Proposal]}|{Mean:.6g}|{SE:.6g}|[{Mean-1.96*SE:.6g},{Mean+1.96*SE:.6g}]|')
        Wins[Proposal]+=int(Rmses[Proposal]<=Rmses[Proposal+2] and Cross[Proposal]<=Cross[Proposal+2])
    Ratios.append(f'|{Case}|'+ '|'.join(f'{R:.6f}' for R in [Rmses[0]/Rmses[2],Rmses[1]/Rmses[3],
        Cross[0]/Cross[2],Cross[1]/Cross[3]])+'|')
    Metrics.append(Rmses);CrossMetrics.append(Cross)
assert all(N>0 for N in NegativeFailures),NegativeFailures
for Sheet,Writer,Name in ((Canvas,Pen,'PerspectiveAreaReservoirComparison.png'),(Errors,ErrorPen,'PerspectiveAreaReservoirErrors.png')):
    Writer.text((16,820),'GPU readbacks;perspective BVH primary hits,including wall primaries;finite references;not production editor capture.',
                font=Small,fill='white')
    Sheet.save(Images/Name)
Geometry=Image.new('RGB',(690,650),'#121923');GeometryPen=ImageDraw.Draw(Geometry)
GeometryPen.text((16,12),'Actual GPU primary attributes / normals,depth[3.2,4.8],diffuse RGB',font=Font,fill='white')
for Case in range(3):
    for Column,Label in enumerate(('normal','depth','material')):
        Tile=Image.open(Images/f'{Prefix}-{Label}-{Case}.png')
        Geometry.paste(Tile.resize((160,160),Image.Resampling.NEAREST),(20+Column*225,60+Case*195))
        GeometryPen.text((20+Column*225,225+Case*195),f'Layout{Case} {Label}',font=Small,fill='white')
Geometry.save(Images/f'{Prefix}Geometry.png')
(Results/f'{Prefix}-comparison.md').write_text('\n'.join(Table+['']+Ratios+['']+Intervals)+'\n')
(Results/f'{Prefix}-means.md').write_text('\n'.join(Means)+'\n')
Summary=(f'PASS runtime:9 identical semantic rows;hashes/build={HashCounts};six identical readbacks;two identical modules.\n'
         'CPU comparisons/build:491520 candidates,393216 selected paths,786432 binary visibility decisions.\n'
         f'CPU receiver-occlusion negative control:{OcclusionFields};GPU primary records verified.\n'
         f'Mean gates={MeanGates};negative control failures/layouts={NegativeFailures}.\n'
         f'Frozen same-query ordinary AND cross-reference image gates:uniform/area wins={Wins} of3 each.\n'
         f'Acceptance per proposal={[N==3 for N in Wins]};production_promote=0.\n'
         f'RMSE by layout={Metrics}\nCross-reference MSE={CrossMetrics}\n'
         f'Estimated combined-reference RMS noise={ReferenceNoise}\n'
         'Calls/pixel including primaries:RIS/raw4=36,raw8=68;reference=13056 total. Reference GPU paths/build=18874368.\n'
         'Host ASan/UBSan only;driver/SPIR-V uninstrumented. No physical timing or Windows test.\n')
(Results/f'{Prefix}-parity.txt').write_text(Summary)
print(Summary)
Files=sorted([*Results.glob(f'{Prefix}*'),*Results.glob('PerspectiveAreaReference*'),*Images.glob(f'{Prefix}*'),
              *Root.glob('Exhibits/LightCompute/Code/PerspectiveAreaReservoir*'),
              Root/'Exhibits/LightCompute/RunPerspectiveAreaReservoir.sh',Root/'Exhibits/LightCompute/PerspectiveAreaReservoir.md'])
Manifest=Results/f'{Prefix}-evidence-hashes.txt'
Manifest.write_text(''.join(f'{hashlib.sha256(P.read_bytes()).hexdigest()}  {P.relative_to(Root)}\n' for P in Files if P!=Manifest))
