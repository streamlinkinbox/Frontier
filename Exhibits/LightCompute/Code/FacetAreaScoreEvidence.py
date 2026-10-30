# 🧩 Audit native compensated targets and compose the proposal/score/selection chain using retained primary/path captures.
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
Prefix='FacetAreaScorePort'
Phases=('GeometricAreaReservoir','PerspectiveAreaReservoir','DeformedAreaReservoir')
Builds=[Root/f'_ClaudeScratch/build/facet-area-score-port-{M}' for M in ('release','sanitized')]
Scratch=Root/'_ClaudeScratch/facet-area-score-port';Scratch.mkdir(parents=True,exist_ok=True)
for Phase in Phases:
    for Line in (Results/f'{Phase}-evidence-hashes.txt').read_text().splitlines():
        H,N=Line.split('  ',1);assert hashlib.sha256((Root/N).read_bytes()).hexdigest()==H,N
for Previous in ('FacetAreaPort','FacetAreaProposalPort'):
    for Line in (Results/f'{Previous}-evidence-hashes.txt').read_text().splitlines():
        H,N=Line.split('  ',1)
        # Engine CMake evolved after each historical manifest;verify its own original snapshot.
        Revision='9dcfb7b' if Previous=='FacetAreaPort' else '4b32869'
        Content=subprocess.run(['git','show',Revision+':'+N],check=True,capture_output=True).stdout
        assert hashlib.sha256(Content).hexdigest()==H,N
        if N!='Frontier/Engine/Rendering/CMakeLists.txt':assert hashlib.sha256((Root/N).read_bytes()).hexdigest()==H,N
for Mode in ('release','sanitized'):
    Text=(Results/f'{Prefix}-{Mode}.txt').read_text()
    assert '100% tests passed, 0 tests failed out of 3' in Text
    for Count,Unit in ((1060,'SCORE'),(193,'PROPOSAL'),(14831,'PORT')):
        assert f'FACET_AREA_{Unit}_UNIT checks={Count} pass=1' in Text
    assert not re.search(r'ERROR: AddressSanitizer|runtime error:',Text)
    for H,N in re.findall(r'^([a-f0-9]{64})  (.+)$',Text,re.M):
        assert hashlib.sha256((Root/N).read_bytes()).hexdigest()==H,N
Proposals=(Builds[0]/'proposals.bin').read_bytes()
Source=(Builds[0]/'source-fixtures.bin').read_bytes()
assert Proposals==gzip.decompress((Results/'FacetAreaProposalPort-native.bin.gz').read_bytes())
assert len(Proposals)==589824*48 and len(Source)==9*16*21*4
for Name in ('proposals.bin','source-fixtures.bin','fixtures.bin','FacetAreaProposalScenes.h'):
    assert (Builds[0]/Name).read_bytes()==(Builds[1]/Name).read_bytes(),Name
(Results/f'{Prefix}-source-fixtures.bin.gz').write_bytes(gzip.compress(Source,mtime=0))

def F(X):return struct.unpack('<f',struct.pack('<f',X))[0]
def Dot(A,B):return sum(X*Y for X,Y in zip(A,B))
def Norm(A):return math.sqrt(Dot(A,A))
def Error(A,B):return abs(A-B)/max(1.,abs(B))
def Index(Corpus,Case,Arm,Packet,Draw):return ((((Corpus*3+Case)*2+Arm)*4096+Packet)*8+Draw)
def Proposal(I):return struct.unpack_from('<2I10f',Proposals,I*48)
def Facet(Corpus,Case,Triangle):return struct.unpack_from('<21f',Source,((Corpus*3+Case)*16+Triangle)*84)
def Q(Raw,Slab,Pixel):return struct.unpack_from('<4f',Raw,(Slab*24576+Pixel)*16)

def ResolveNormal(T,U,V):
    Weight=F(F(1-U)-V)
    Normal=[F(F(F(T[9+A]*Weight)+F(T[12+A]*U))+F(T[15+A]*V)) for A in range(3)]
    Squared=F(F(F(Normal[0]*Normal[0])+F(Normal[1]*Normal[1]))+F(Normal[2]*Normal[2]))
    Reciprocal=F(1/F(math.sqrt(Squared)))
    Normal=[F(X*Reciprocal) for X in Normal]
    if Dot(Normal,[-X for X in T[9:12]])>0:Normal=[-X for X in Normal]
    return Normal

Input=Scratch/'scores-input.bin'
with Input.open('wb') as Stream:
    for Corpus,Phase in enumerate(Phases):
        for Case in range(3):
            Raw=gzip.decompress((Results/f'{Phase}{Case}.f32.gz').read_bytes())
            for Arm in range(2):
                for Packet in range(4096):
                    Pixel=Arm*4096+Packet
                    Receiver=list(Q(Raw,20,Pixel)[:3]+Q(Raw,21,Pixel)[:3]+Q(Raw,22,Pixel)[:3])
                    for Draw in range(8):
                        P=Proposal(Index(Corpus,Case,Arm,Packet,Draw));T=Facet(Corpus,Case,P[0])
                        Row=list(T[:9])+ResolveNormal(T,P[2],P[3])+list(T[18:21])+Receiver+[P[2],P[3]]
                        Stream.write(struct.pack('<26f',*Row))

Runtime=[]
for Mode,Build in zip(('release','sanitized'),Builds):
    Process=subprocess.run([str(Build/'FacetAreaScoreValidation'),str(Input),str(Build/'scores.bin')],
        capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1',UBSAN_OPTIONS='halt_on_error=1'))
    assert Process.returncode==0 and not Process.stderr,(Process.stdout,Process.stderr)
    Runtime.append(Process.stdout.strip())
Native=(Builds[0]/'scores.bin').read_bytes()
assert Native==(Builds[1]/'scores.bin').read_bytes() and len(Native)==589824*48
assert Runtime[0]==Runtime[1]
(Results/f'{Prefix}-native.bin.gz').write_bytes(gzip.compress(Native,mtime=0))
def Score(I):return struct.unpack_from('<2I10f',Native,I*48)

# Independent exact-real double geometry,not the port's low-word implementation.
InputBytes=Input.read_bytes()
MaxDouble=[0.,0.,0.]
Controls=[[False]*3 for _ in range(3)]
for I in range(589824):
    R=struct.unpack_from('<26f',InputBytes,I*104)
    S=Score(I);assert S[:2]==(1,0),I
    U,V=R[24:26]
    Delta=[(R[15+A]-R[A])-U*(R[3+A]-R[A])-V*(R[6+A]-R[A]) for A in range(3)]
    Squared=Dot(Delta,Delta)
    Receiver=max(0,-Dot(R[18:21],Delta));SourceProjection=max(0,Dot(R[9:12],Delta))
    Geometry=[X/math.pi*Receiver*SourceProjection/(Squared*Squared) for X in R[21:24]]
    Outgoing=[A*L/math.pi*max(0,Dot(R[9:12],(.800000011920929,0,.6000000238418579)))
              for A,L in zip(R[12:15],(1,F(.7),F(.4)))]
    Proxy=[A*B for A,B in zip(Outgoing,Geometry)]
    Target=F(1e-6)+sum(Proxy)/3
    MaxDouble[0]=max(MaxDouble[0],*(Error(A,B) for A,B in zip(S[2:5],Geometry)))
    MaxDouble[1]=max(MaxDouble[1],*(Error(A,B) for A,B in zip(S[5:8],Proxy)))
    MaxDouble[2]=max(MaxDouble[2],abs(S[8]-Target))
    assert max(MaxDouble[:2])<=2e-5 and MaxDouble[2]<=2e-5,(I,MaxDouble)
    Corpus=I//65536//3;Case=(I//65536)%3
    if Corpus==2:
        Controls[Case][0]|=abs(S[8]-sum(Proxy)/3)>1e-8
        WrongGeometry=[X/math.pi*SourceProjection/(Squared*Squared) for X in R[21:24]]
        WrongTarget=F(1e-6)+sum(A*B for A,B in zip(Outgoing,WrongGeometry))/3
        Controls[Case][1]|=abs(S[8]-WrongTarget)>2e-5
        Controls[Case][2]|=abs(S[8]-(F(1e-6)+sum(Proxy)*math.pi/3))>2e-5
assert all(all(Case) for Case in Controls),Controls
# A missing floor is a structural support failure even though1e-6 is below the2e-5 absolute parity tolerance.
FloorFailures=[False]*3
for Case in range(3):
    FloorFailures[Case]=any(Score(Index(2,Case,Arm,P,D))[8]==F(1e-6) and
                            all(C==0 for C in Score(Index(2,Case,Arm,P,D))[5:8])
                            for Arm in range(2) for P in range(4096) for D in range(8))
assert all(FloorFailures)

Pairs=Scratch/'pairs-input.bin'
with Pairs.open('wb') as Stream:
    for I in range(0,589824,2):
        P0,P1=Proposal(I),Proposal(I+1)
        Stream.write(struct.pack('<5d',Score(I)[8],P0[4],Score(I+1)[8],P1[4],P0[8]))
for Mode,Build in zip(('release','sanitized'),Builds):
    Process=subprocess.run([str(Build/'FacetAreaPortValidation'),str(Pairs),str(Build/'selections.bin')],
        capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1',UBSAN_OPTIONS='halt_on_error=1'))
    assert Process.returncode==0 and not Process.stderr,(Process.stdout,Process.stderr)
    Runtime.append(Process.stdout.strip())
Selections=(Builds[0]/'selections.bin').read_bytes()
assert Selections==(Builds[1]/'selections.bin').read_bytes() and len(Selections)==294912*32
assert Runtime[2]==Runtime[3]
(Results/f'{Prefix}-selections.bin.gz').write_bytes(gzip.compress(Selections,mtime=0))

def Selected(I):return struct.unpack_from('<4I2d',Selections,(I//2)*32)
def Estimate(Raw,Method):
    return [[sum(Q(Raw,0,Method*4096+Run*1024+P)[C] for Run in range(4))/4 for C in range(3)] for P in range(1024)]
Means=['|Corpus|Fixture|Arm|Channel|Residual|SE|Gate|','|---|---:|---:|---:|---:|---:|---|']
def MeanGate(E,Ref,Corpus,Case,Arm):
    for C in range(3):
        D=[A[C]-B[C] for A,B in zip(E,Ref)];Mean=sum(D)/1024
        SE=math.sqrt(sum((X-Mean)**2 for X in D)/(1023*1024))
        Passed=abs(Mean)<=6*SE+2e-5
        Means.append(f'|{Corpus}|{Case}|{Arm}|{C}|{Mean:.8g}|{SE:.8g}|{Passed}|')
        assert Passed
    return 3

Compared=0;Differences=0;MeanGates=0
MaxGpu=0.;MaxNormalizer=0.;MaxWeightSum=0.;MaxImage=0.
Table=['|Corpus|Fixture|Arm|Native RMSE|GPU RMSE|raw4 RMSE|raw8 RMSE|Max RGB delta|',
       '|---|---:|---:|---:|---:|---:|---:|---:|']
Targets=Image.new('RGB',(1050,760),'#121923');Pen=ImageDraw.Draw(Targets)
Comparison=Image.new('RGB',(1100,850),'#121923');ComparisonPen=ImageDraw.Draw(Comparison)
Font=ImageFont.load_default(size=14);Small=ImageFont.load_default(size=12)
ComparisonPen.text((16,10),'Native proposal+score+selection RGB replay / deformed fixtures /32x32,nearest enlarged /linear exposure64',font=Font,fill='white')
Pen.text((16,10),'Actual target means / uniform-area candidates / deformed fixtures / log10 scale:floor1e-6 to1e-2',font=Font,fill='white')
for Corpus,Phase in enumerate(Phases):
    for Case in range(3):
        Raw=gzip.decompress((Results/f'{Phase}{Case}.f32.gz').read_bytes())
        RefStem=('GeometricAreaReference','PerspectiveAreaReference','DeformedAreaReference')[Corpus]
        Reference=gzip.decompress((Results/f'{RefStem}{Case}.f32.gz').read_bytes())
        Banks=[[struct.unpack_from('<3f',Reference,(B*1024+P)*16) for P in range(1024)] for B in range(2)]
        Ref=[[(Banks[0][P][C]+Banks[1][P][C])/2 for C in range(3)] for P in range(1024)]
        for Method in range(6):
            for Packet in range(4096):
                Pixel=Method*4096+Packet
                for Slab in (20,21,22):
                    assert Q(Raw,Slab,Pixel)==Q(Raw,Slab,(Method%2)*4096+Packet)
                for Lane in range(4):
                    for P in range(2 if Method<2 or Method>=4 else 1):
                        I=Index(Corpus,Case,Method%2,Packet,2*Lane+P)
                        GPU=Q(Raw,5+4*Lane,Pixel)[P]
                        MaxGpu=max(MaxGpu,abs(Score(I)[8]-GPU));Compared+=1
                        assert MaxGpu<=2e-5,(Corpus,Case,Method,Packet,Lane,P,MaxGpu)
        for Arm in range(2):
            E=[[0.,0.,0.] for P in range(1024)]
            for Packet in range(4096):
                Pixel=Arm*4096+Packet
                for Lane in range(4):
                    I=Index(Corpus,Case,Arm,Packet,2*Lane);S=Selected(I)
                    GPU=Q(Raw,5+4*Lane,Pixel);Contribution=Q(Raw,4+4*Lane,Pixel)
                    assert S[1:4]==(1,2,0)
                    if S[0]!=int(GPU[2]):
                        W0=Score(I)[8]/Proposal(I)[4];W1=Score(I+1)[8]/Proposal(I+1)[4]
                        assert abs(GPU[3]-W0/(W0+W1))<2e-6
                        Differences+=1
                    MaxNormalizer=max(MaxNormalizer,Error(S[5],Contribution[3]))
                    MaxWeightSum=max(MaxWeightSum,Error(S[4],Contribution[3]*2*GPU[int(GPU[2])]))
                    assert MaxNormalizer<=2e-5 and MaxWeightSum<=2e-5
                    assert S[0]==int(GPU[2]),'No untraced path fabrication permitted'
                    for C in range(3):E[Packet%1024][C]+=Contribution[C]/Contribution[3]*S[5]/16
            Gpu=Estimate(Raw,Arm);Raw4=Estimate(Raw,Arm+2);Raw8=Estimate(Raw,Arm+4)
            Delta=max(abs(A[C]-B[C]) for A,B in zip(E,Gpu) for C in range(3));MaxImage=max(MaxImage,Delta)
            assert Delta<=2e-5
            def MSE(V):return sum((A[C]-B[C])**2 for A,B in zip(V,Ref) for C in range(3))/3072
            def Cross(V):return sum((V[P][C]-Banks[0][P][C])*(V[P][C]-Banks[1][P][C]) for P in range(1024) for C in range(3))/3072
            assert MSE(E)<=MSE(Gpu)+1e-10 and Cross(E)<=Cross(Gpu)+1e-10
            MeanGates+=MeanGate(E,Ref,Corpus,Case,Arm)
            Table.append(f'|{Corpus}|{Case}|{Arm}|{math.sqrt(MSE(E)):.8g}|{math.sqrt(MSE(Gpu)):.8g}|'
                         f'{math.sqrt(MSE(Raw4)):.8g}|{math.sqrt(MSE(Raw8)):.8g}|{Delta:.7g}|')
            Encoded=[tuple(round(255*min(1,max(0,C*64))) for C in P) for P in E]
            Tile=Image.new('RGB',(32,32));Tile.putdata(Encoded)
            Tile.save(Images/f'{Prefix}-{Corpus}-{Case}-{Arm}.png')
            assert Image.open(Images/f'{Prefix}-{Corpus}-{Case}-{Arm}.png').tobytes()==bytes(C for P in Encoded for C in P)
            if Corpus==2 and Arm==0:
                Y=70+Case*245
                for Column,(Pixels,Label) in enumerate(((Ref,'Finite GPU reference'),(E,'Native arithmetic replay'),
                                                       (Gpu,'Retained GPU RIS'),(Raw4,'GPU raw4'),(Raw8,'GPU raw8'))):
                    Display=Image.new('RGB',(32,32))
                    Display.putdata([tuple(round(255*min(1,max(0,C*64))) for C in P) for P in Pixels])
                    X=16+Column*216
                    Comparison.paste(Display.resize((144,144),Image.Resampling.NEAREST),(X,Y))
                    ComparisonPen.text((X,Y+154),Label,font=Small,fill='white')
                    ComparisonPen.text((X,Y+173),f'RMSE {math.sqrt(MSE(Pixels)):.5g}' if Column else 'Finite same-renderer target',font=Small,fill='white')
                ComparisonPen.text((16,Y-23),f'Fixture{Case}',font=Font,fill='white')
                for Column,NativeTarget in enumerate((True,False)):
                    Colours=[]
                    for P in range(1024):
                        Samples=[]
                        for Repeat in range(4):
                            Packet=Repeat*1024+P
                            for Draw in range(8):
                                Samples.append(Score(Index(Corpus,Case,0,Packet,Draw))[8] if NativeTarget else
                                               Q(Raw,5+4*(Draw//2),Packet)[Draw%2])
                        H=sum(Samples)/32;Log=min(1,max(0,(math.log10(H)+6)/4))
                        Colours.append((round(255*Log),round(210*math.sqrt(Log)),round(100*(1-Log))))
                    Heat=Image.new('RGB',(32,32));Heat.putdata(Colours)
                    X=200+Column*300;Y=65+Case*220
                    Targets.paste(Heat.resize((176,176),Image.Resampling.NEAREST),(X,Y))
                    Pen.text((X,Y-22),'Native C++ target' if NativeTarget else 'Retained GPU target',font=Font,fill='white')
                    Pen.text((16,Y+60),f'Fixture{Case}',font=Font,fill='white')
Pen.text((16,735),'Primary receivers retained from GPU;source/proposals and targets evaluated natively. Not fresh shaded transport or a timing result.',font=Small,fill='white')
Targets.save(Images/f'{Prefix}Targets.png')
ComparisonPen.text((16,820),'Retained GPU primary receivers and traced selected-path integrands;new native proposal/score/selection. Not fresh tracing or timing.',font=Small,fill='white')
Comparison.save(Images/f'{Prefix}Comparison.png')
assert Compared==1474560 and Differences<=16 and MeanGates==54
(Results/f'{Prefix}-means.md').write_text('\n'.join(Means)+'\n')
(Results/f'{Prefix}-comparison.md').write_text('\n'.join(Table)+'\n')
Summary=(f'PASS Release/ASan+UBSan:three CTests/build;1060 score+193 proposal+14831 selector focused checks/build.\n'
         f'Native proposals=589824 byte-identical to prior;fresh native scores=589824;GPU target observations={Compared}.\n'
         f'Independent double geometry/proxy/target maxima={MaxDouble};GPU target absolute maximum={MaxGpu}.\n'
         f'Composed native selections=294912;selected ordinal differences={Differences};weight/Z maxima={MaxWeightSum},{MaxNormalizer}.\n'
         f'18 RGB replays max channel delta={MaxImage};54 mean gates and18 ordinary/cross-MSE gates pass.\n'
         f'Deformed wrong-score controls={Controls};missing-floor structural failures={FloorFailures}.\n'
         f'Native score and composed-selection bytes identical across builds;stdout={Runtime}.\n'
         f'Input SHA256={hashlib.sha256(InputBytes).hexdigest()};native score SHA256={hashlib.sha256(Native).hexdigest()}.\n'
         'Source attributes resolved from native fixtures;primary receivers/path integrands retained from GPU.\n'
         'No fresh tracing,general material port,renderer activation,physical timing or Windows verification.\n'
         'Production348.046875MiB/defaults/32B layouts unchanged.\n')
(Results/f'{Prefix}-parity.txt').write_text(Summary);print(Summary)
Files=sorted([*Results.glob(f'{Prefix}*'),*Images.glob(f'{Prefix}*'),*Root.glob('Exhibits/LightCompute/Code/FacetAreaScore*'),
    Root/'Exhibits/LightCompute/FacetAreaScorePort.md',Root/'Exhibits/LightCompute/RunFacetAreaScorePort.sh',
    Root/'Exhibits/LightCompute/FacetAreaScorePort/CMakeLists.txt',Root/'Frontier/Engine/Rendering/FacetAreaScore.h',
    Root/'Frontier/Engine/Rendering/FacetAreaScore.cpp',Root/'Frontier/Engine/Rendering/CMakeLists.txt'])
Manifest=Results/f'{Prefix}-evidence-hashes.txt'
Manifest.write_text(''.join(f'{hashlib.sha256(P.read_bytes()).hexdigest()}  {P.relative_to(Root)}\n' for P in Files if P!=Manifest))
