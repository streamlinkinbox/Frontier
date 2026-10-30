# 🧩 Compare the engine C++ pair operator with retained GPU choices,normalizers and RGB estimator captures.
from pathlib import Path
import gzip
import hashlib
import math
import os
import re
import struct
import subprocess
from PIL import Image, ImageDraw, ImageFont

Root=Path(__file__).resolve().parents[3]
Results=Root/'Exhibits/LightCompute/Results'
Images=Root/'Exhibits/LightCompute/Images'
Scratch=Root/'_ClaudeScratch/facet-area-port'
Scratch.mkdir(parents=True,exist_ok=True)
Phases=('GeometricAreaReservoir','PerspectiveAreaReservoir','DeformedAreaReservoir')
ReferenceStems=('GeometricAreaReference','PerspectiveAreaReference','DeformedAreaReference')
Count=24576
Rows=[]
Maximum=[0.,0.,0.]
for Phase in Phases:
    for Line in (Results/f'{Phase}-evidence-hashes.txt').read_text().splitlines():
        Digest,Name=Line.split('  ',1)
        assert hashlib.sha256((Root/Name).read_bytes()).hexdigest()==Digest,Name
for Mode in ('release','sanitized'):
    Text=(Results/f'FacetAreaPort-{Mode}.txt').read_text()
    assert '100% tests passed, 0 tests failed out of 1' in Text
    assert 'FACET_AREA_PORT_UNIT checks=14831 pass=1' in Text
    assert not re.search(r'ERROR: AddressSanitizer|runtime error:',Text)
    for Digest,Name in re.findall(r'^([a-f0-9]{64})  (.+)$',Text,re.M):
        assert hashlib.sha256((Root/Name).read_bytes()).hexdigest()==Digest,Name

def Read(Phase,Case):
    return gzip.decompress((Results/f'{Phase}{Case}.f32.gz').read_bytes())

def Quartet(Raw,Slab,Pixel):
    return struct.unpack_from('<4f',Raw,(Slab*Count+Pixel)*16)

Intake=Scratch/'pairs.bin'
with Intake.open('wb') as Capture:
    for Phase in Phases:
        for Case in range(3):
            Raw=Read(Phase,Case)
            for Arm in range(2):
                for Packet in range(4096):
                    Pixel=Arm*4096+Packet
                    for Lane in range(4):
                        First=Quartet(Raw,2+4*Lane,Pixel)
                        Second=Quartet(Raw,3+4*Lane,Pixel)
                        Targets=Quartet(Raw,5+4*Lane,Pixel)
                        Row=(Targets[0],First[3],Targets[1],Second[3],Targets[3])
                        Capture.write(struct.pack('<5d',*Row))
                        Rows.append(Row)
assert len(Rows)==294912
Outputs=[]
ReplayLogs=[]
for Mode in ('release','sanitized'):
    Binary=Root/f'_ClaudeScratch/build/facet-area-port-{Mode}/FacetAreaPortValidation'
    Output=Scratch/f'{Mode}.bin'
    Env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1',UBSAN_OPTIONS='halt_on_error=1')
    Process=subprocess.run([str(Binary),str(Intake),str(Output)],capture_output=True,text=True,env=Env)
    assert Process.returncode==0,(Process.stdout,Process.stderr)
    assert not Process.stderr,Process.stderr
    ReplayLogs.append(Process.stdout.strip())
    Outputs.append(Output.read_bytes())
assert Outputs[0]==Outputs[1] and len(Outputs[0])==len(Rows)*32
assert ReplayLogs[0]==ReplayLogs[1]
(Results/'FacetAreaPort-pairs.bin.gz').write_bytes(gzip.compress(Intake.read_bytes(),mtime=0))
(Results/'FacetAreaPort-native.bin.gz').write_bytes(gzip.compress(Outputs[0],mtime=0))
Native=[struct.unpack_from('<4I2d',Outputs[0],I*32) for I in range(len(Rows))]
for Row,Chosen in zip(Rows,Native):
    H0,Q0,H1,Q1,U=Row
    Weights=(H0/Q0,H1/Q1)
    Expected=0 if U<Weights[0]/sum(Weights) else 1
    assert Chosen[0]==Expected and Chosen[1:4]==(1,2,0)
    Z=sum(Weights)/(2*(H0,H1)[Expected])
    Maximum[0]=max(Maximum[0],abs(Chosen[4]-sum(Weights))/max(1.,sum(Weights)))
    Maximum[1]=max(Maximum[1],abs(Chosen[5]-Z)/max(1.,Z))
    assert Maximum[0]<2e-5 and Maximum[1]<2e-5

Font=ImageFont.load_default(size=14)
Small=ImageFont.load_default(size=12)
Canvas=Image.new('RGB',(1100,850),'#121923')
Pen=ImageDraw.Draw(Canvas)
Pen.text((16,12),'C++ operator replay /deformed captures /32x32,nearest enlarged /linear exposure64',font=Font,fill='white')
Table=['|Corpus|Fixture|Arm|CPU RMSE|GPU RMSE|raw4 RMSE|raw8 RMSE|Max RGB delta|',
       '|---|---:|---|---:|---:|---:|---:|---:|']
Means=['|Corpus|Fixture|Arm|Channel|Residual|SE|Gate|','|---|---:|---|---:|---:|---:|---|']
Index=0
Ambiguous=0
Negative=[0,0]
GpuNormalizerError=0.
GpuSumError=0.
MeanCount=0

def MeanGate(E,R):
    Measurements=[]
    for C in range(3):
        D=[A[C]-B[C] for A,B in zip(E,R)]
        Mean=sum(D)/1024
        SE=math.sqrt(sum((X-Mean)**2 for X in D)/(1023*1024))
        Measurements.append((Mean,SE,abs(Mean)<=6*SE+2e-5))
    return all(M[2] for M in Measurements),Measurements

def ImageEstimate(Raw,Method):
    return [[sum(Quartet(Raw,0,Method*4096+Run*1024+P)[C] for Run in range(4))/4
             for C in range(3)] for P in range(1024)]

def Store(E,Name):
    Encoded=[tuple(round(255*min(1,max(0,C*64))) for C in P) for P in E]
    Tile=Image.new('RGB',(32,32));Tile.putdata(Encoded);Tile.save(Images/Name)
    assert Image.open(Images/Name).tobytes()==bytes(C for P in Encoded for C in P)
    return Tile

for Corpus,Phase in enumerate(Phases):
    for Case in range(3):
        Raw=Read(Phase,Case)
        Reference=gzip.decompress((Results/f'{ReferenceStems[Corpus]}{Case}.f32.gz').read_bytes())
        Banks=[[struct.unpack_from('<3f',Reference,(B*1024+P)*16) for P in range(1024)] for B in range(2)]
        Ref=[[(Banks[0][P][C]+Banks[1][P][C])/2 for C in range(3)] for P in range(1024)]
        if Corpus==2:
            Y=70+Case*245
            Tile=Store(Ref,f'FacetAreaPort-reference-{Case}.png')
            Canvas.paste(Tile.resize((144,144),Image.Resampling.NEAREST),(16,Y))
            Pen.text((16,Y-22),f'Fixture{Case}',font=Font,fill='white')
            Pen.text((16,Y+155),'Finite GPU reference',font=Small,fill='white')
        for Arm in range(2):
            E=[[0.,0.,0.] for P in range(1024)]
            Wrong=[[0.,0.,0.] for P in range(1024)]
            for Packet in range(4096):
                Pixel=Arm*4096+Packet
                for Lane in range(4):
                    Chosen=Native[Index]
                    H0,Q0,H1,Q1,U=Rows[Index]
                    GPU=Quartet(Raw,5+4*Lane,Pixel)
                    GpuSelected=int(GPU[2])
                    if Chosen[0]!=GpuSelected:
                        Threshold=(H0/Q0)/(H0/Q0+H1/Q1)
                        assert abs(U-Threshold)<2e-6
                        Ambiguous+=1
                        raise AssertionError('Untraced alternative cannot be fabricated for image equivalence')
                    Contribution=Quartet(Raw,4+4*Lane,Pixel)
                    GpuNormalizerError=max(GpuNormalizerError,abs(Chosen[5]-Contribution[3])/max(1.,Chosen[5]))
                    assert GpuNormalizerError<2e-5
                    GpuWeightSum=Contribution[3]*2*(H0,H1)[GpuSelected]
                    GpuSumError=max(GpuSumError,abs(Chosen[4]-GpuWeightSum)/max(1.,Chosen[4]))
                    assert GpuSumError<2e-5
                    for C in range(3):
                        Integrand=Contribution[C]/Contribution[3]
                        E[Packet%1024][C]+=Integrand*Chosen[5]/16
                        Wrong[Packet%1024][C]+=Integrand*(H0+H1)/(2*(H0,H1)[GpuSelected])/16
                    Index+=1
            Gpu=ImageEstimate(Raw,Arm)
            Raw4=ImageEstimate(Raw,Arm+2)
            Raw8=ImageEstimate(Raw,Arm+4)
            Delta=max(abs(A[C]-B[C]) for A,B in zip(E,Gpu) for C in range(3))
            Maximum[2]=max(Maximum[2],Delta)
            assert Delta<=2e-5
            def MSE(V):return sum((A[C]-B[C])**2 for A,B in zip(V,Ref) for C in range(3))/3072
            def Cross(V):return sum((V[P][C]-Banks[0][P][C])*(V[P][C]-Banks[1][P][C])
                                    for P in range(1024) for C in range(3))/3072
            assert MSE(E)<=MSE(Gpu)+1e-10 and Cross(E)<=Cross(Gpu)+1e-10
            Passed,Measurements=MeanGate(E,Ref)
            assert Passed,(Phase,Case,Arm,Measurements)
            MeanCount+=3
            for C,(Mean,SE,Gate) in enumerate(Measurements):
                Means.append(f'|{Corpus}|{Case}|{Arm}|{C}|{Mean:.8g}|{SE:.8g}|{Gate}|')
            if Corpus==2 and not MeanGate(Wrong,Ref)[0]:Negative[Arm]+=1
            Table.append(f'|{Corpus}|{Case}|{Arm}|{math.sqrt(MSE(E)):.8g}|{math.sqrt(MSE(Gpu)):.8g}|'
                         f'{math.sqrt(MSE(Raw4)):.8g}|{math.sqrt(MSE(Raw8)):.8g}|{Delta:.7g}|')
            Tile=Store(E,f'FacetAreaPort-{Corpus}-{Case}-{Arm}.png')
            if Corpus==2 and Arm==0:
                for Column,(Pixels,Label) in enumerate(((E,'C++ uniform RIS'),(Gpu,'Retained GPU RIS'),
                                                       (Raw4,'GPU raw4'),(Raw8,'GPU raw8'))):
                    Display=Store(Pixels,f'FacetAreaPort-deformed-{Case}-{Column}.png')
                    X=210+Column*220
                    Canvas.paste(Display.resize((144,144),Image.Resampling.NEAREST),(X,Y))
                    Pen.text((X,Y+155),Label,font=Small,fill='white')
                    Pen.text((X,Y+174),f'RMSE {math.sqrt(MSE(Pixels)):.5g}',font=Small,fill='white')
assert Index==294912 and Ambiguous<=16 and all(N>0 for N in Negative)
Pen.text((16,820),'Capture-driven operator replay,not fresh tracing;GPU selected-path integrands reused;no timing or renderer activation.',
         font=Small,fill='white')
Canvas.save(Images/'FacetAreaPortComparison.png')
(Results/'FacetAreaPort-comparison.md').write_text('\n'.join(Table)+'\n')
(Results/'FacetAreaPort-means.md').write_text('\n'.join(Means)+'\n')
Summary=(f'PASS C++17 Release/ASan+UBSan:14831 focused checks/build,one CTest/build.\n'
         f'Native corpus pairs=294912;Release/sanitized output bytes identical;selector differences={Ambiguous}.\n'
         f'Native stdout/build:{ReplayLogs[0]}\n'
         f'Intake SHA256={hashlib.sha256(Intake.read_bytes()).hexdigest()}\n'
         f'Native output SHA256={hashlib.sha256(Outputs[0]).hexdigest()}\n'
         f'Independent Python weight/normalizer max errors={Maximum[:2]};GPU sum/Z errors={GpuSumError},{GpuNormalizerError}.\n'
         f'CPU/GPU image max channel delta={Maximum[2]};18 images;RGB mean gates={MeanCount}.\n'
         f'Deformed wrong-density failures by RIS type={Negative};image ordinary/cross-MSE equivalence gates=18/18.\n'
         'Geometric,perspective,deformed manifests verified;no historical evidence rewritten.\n'
         'Selected-path RGB integrands recovered from retained GPU lane contribution/normalizer,not independently traced.\n'
         'Library excluded from default builds;no renderer integration,layout/default change,Windows or physical GPU test.\n')
(Results/'FacetAreaPort-parity.txt').write_text(Summary)
print(Summary)
Files=sorted([*Results.glob('FacetAreaPort*'),*Images.glob('FacetAreaPort*'),*Root.glob('Exhibits/LightCompute/Code/FacetAreaPort*'),
              Root/'Exhibits/LightCompute/FacetAreaPort.md',Root/'Exhibits/LightCompute/RunFacetAreaPort.sh',
              Root/'Exhibits/LightCompute/FacetAreaPort/CMakeLists.txt',
              Root/'Frontier/Engine/Rendering/FacetAreaReservoir.h',Root/'Frontier/Engine/Rendering/FacetAreaReservoir.cpp',
              Root/'Frontier/Engine/Rendering/CMakeLists.txt'])
Manifest=Results/'FacetAreaPort-evidence-hashes.txt'
Manifest.write_text(''.join(f'{hashlib.sha256(P.read_bytes()).hexdigest()}  {P.relative_to(Root)}\n'
                            for P in Files if P!=Manifest))
