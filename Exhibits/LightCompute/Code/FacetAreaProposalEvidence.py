# 🧩 Audit newly generated native proposals against independent float32 algebra and retained GPU observations.
from pathlib import Path
import gzip
import hashlib
import math
import re
import struct
import subprocess
from PIL import Image, ImageDraw, ImageFont

Root=Path(__file__).resolve().parents[3]
Results=Root/'Exhibits/LightCompute/Results'
Images=Root/'Exhibits/LightCompute/Images'
Prefix='FacetAreaProposalPort'
Phases=('GeometricAreaReservoir','PerspectiveAreaReservoir','DeformedAreaReservoir')
Builds=[Root/f'_ClaudeScratch/build/facet-area-proposal-port-{M}' for M in ('release','sanitized')]
for Phase in Phases:
    for Line in (Results/f'{Phase}-evidence-hashes.txt').read_text().splitlines():
        Digest,Name=Line.split('  ',1)
        assert hashlib.sha256((Root/Name).read_bytes()).hexdigest()==Digest,Name
# Previous arithmetic-port manifest describes its parent snapshot,including the then-current engine CMake.
for Line in (Results/'FacetAreaPort-evidence-hashes.txt').read_text().splitlines():
    Digest,Name=Line.split('  ',1)
    Original=subprocess.run(['git','show','9dcfb7b:'+Name],check=True,capture_output=True).stdout
    assert hashlib.sha256(Original).hexdigest()==Digest,Name
    if Name!='Frontier/Engine/Rendering/CMakeLists.txt':
        assert hashlib.sha256((Root/Name).read_bytes()).hexdigest()==Digest,Name
for Mode in ('release','sanitized'):
    Text=(Results/f'{Prefix}-{Mode}.txt').read_text()
    assert '100% tests passed, 0 tests failed out of 2' in Text
    assert 'FACET_AREA_PROPOSAL_UNIT checks=193 pass=1' in Text
    assert 'FACET_AREA_PORT_UNIT checks=14831 pass=1' in Text
    assert 'FACET_AREA_PROPOSAL_CAPTURE distinct=589824 fixtures=9 pass=1' in Text
    assert not re.search(r'ERROR: AddressSanitizer|runtime error:',Text)
    for Digest,Name in re.findall(r'^([a-f0-9]{64})  (.+)$',Text,re.M):
        assert hashlib.sha256((Root/Name).read_bytes()).hexdigest()==Digest,Name
Raw=(Builds[0]/'proposals.bin').read_bytes()
Geometry=(Builds[0]/'fixtures.bin').read_bytes()
assert Raw==(Builds[1]/'proposals.bin').read_bytes() and len(Raw)==589824*48
assert Geometry==(Builds[1]/'fixtures.bin').read_bytes() and len(Geometry)==9*16*9*4
assert (Builds[0]/'FacetAreaProposalScenes.h').read_bytes()==(Builds[1]/'FacetAreaProposalScenes.h').read_bytes()
for Name,Content in (('native',Raw),('fixtures',Geometry)):
    (Results/f'{Prefix}-{Name}.bin.gz').write_bytes(gzip.compress(Content,mtime=0))

def F(X):return struct.unpack('<f',struct.pack('<f',X))[0]
def Hash(Bits):
    Bits&=0xffffffff
    Bits^=Bits>>16;Bits=Bits*0x7feb352d&0xffffffff
    Bits^=Bits>>15;Bits=Bits*0x846ca68b&0xffffffff
    return Bits^(Bits>>16)
def Uniform(Bits):return (Hash(Bits)>>8)/16777216

def Native(Corpus,Case,Arm,Packet,Draw):
    Index=((((Corpus*3+Case)*2+Arm)*4096+Packet)*8+Draw)
    return struct.unpack_from('<2I10f',Raw,Index*48)
def Triangles(Corpus,Case):
    return [[struct.unpack_from('<3f',Geometry,((Corpus*3+Case)*16*9+T*9+C*3)*4)
             for C in range(3)] for T in range(16)]
def Area(T):
    A,B,C=T
    First=[F(B[I]-A[I]) for I in range(3)]
    Second=[F(C[I]-A[I]) for I in range(3)]
    Cross=[F(F(First[J]*Second[K])-F(First[K]*Second[J])) for J,K in ((1,2),(2,0),(0,1))]
    return F(.5*F(math.sqrt(F(F(F(Cross[0]*Cross[0])+F(Cross[1]*Cross[1]))+F(Cross[2]*Cross[2])))))
def World(T,U,V):
    return [F(F(A+F(U*F(B-A)))+F(V*F(C-A))) for A,B,C in zip(*T)]
def Normalized(A,B):return abs(A-B)/max(1.,abs(B))

Maximum=[0.,0.,0.]
GpuMaximum=[0.,0.,0.]
Ambiguous=0
Compared=0
UniformChecks=0
Controls=[0,0,0]
Rows=['|Corpus|Fixture|RNG checks|GPU proposals|CDF differences|Max PDF error|Max barycentric error|',
      '|---|---:|---:|---:|---:|---:|---:|']
Moments=['|Corpus|Fixture|Measure|Count bins|Mean U|Mean V|Mean U²|Mean V²|Mean UV|',
         '|---|---:|---:|---|---:|---:|---:|---:|---:|']
Distributions={}
for Corpus,Phase in enumerate(Phases):
    for Case in range(3):
        T=Triangles(Corpus,Case)
        Areas=list(map(Area,T))
        Total=0.
        for A in Areas:Total=F(Total+A)
        Cdf=[];Boundary=0.
        for A in Areas:Boundary=F(Boundary+F(A/Total));Cdf.append(Boundary)
        Gpu=gzip.decompress((Results/f'{Phase}{Case}.f32.gz').read_bytes())
        def Quartet(Slab,Pixel):return struct.unpack_from('<4f',Gpu,(Slab*24576+Pixel)*16)
        Local=[0.,0.];LocalAmbiguous=0
        for Arm in range(2):
            Counts=[0]*16
            Statistics=[[] for _ in range(5)]
            WrongRoot=[]
            Negative=[False,False,False]
            for Packet in range(4096):
                Seed=Hash((Packet+(0x739ab531,0x4a615e23,0x5e92b341)[Corpus])^(Case*0x6c8e9cf5&0xffffffff))
                for Draw in range(8):
                    Choice,First,Second=[Uniform(Seed^((3*Draw+C+1)*0x9e3779b9&0xffffffff)) for C in range(3)]
                    Selection=Uniform(Seed^((0x7f4a7c15+(Draw//2)*0x9e3779b9)&0xffffffff))
                    UniformIndex=min(int(F(Choice*16)),15)
                    AreaIndex=next((I for I,B in enumerate(Cdf) if Choice<B),15)
                    Index=UniformIndex if Arm==0 else AreaIndex
                    U=F(1-F(math.sqrt(First)));V=F(F(math.sqrt(First))*Second)
                    Pdf=F(F(1/16)/Areas[Index]) if Arm==0 else F(1/Total)
                    Point=World(T[Index],U,V)
                    N=Native(Corpus,Case,Arm,Packet,Draw)
                    assert N[:2]==(Index,1) and N[8]==Selection and N[9:12]==(Choice,First,Second)
                    UniformChecks+=1
                    for J,(A,B) in enumerate(((N[2],U),(N[3],V),(N[4],Pdf))):
                        Maximum[J]=max(Maximum[J],Normalized(A,B))
                    assert max(abs(N[2]-U),abs(N[3]-V))<2e-6 and Normalized(N[4],Pdf)<2e-5
                    assert max(Normalized(A,B) for A,B in zip(N[5:8],Point))<2e-5
                    Counts[N[0]]+=1
                    for S,X in zip(Statistics,(N[2],N[3],N[2]**2,N[3]**2,N[2]*N[3])):S.append(X)
                    WrongRoot.append(1-First)
                    Negative[0]|=UniformIndex!=AreaIndex
                    WrongPdf=F(1/Total) if Arm==0 else F(F(1/16)/Areas[Index])
                    Negative[2]|=Normalized(N[4],WrongPdf)>2e-5
            Expected=[1/16]*16 if Arm==0 else [A/Total for A in Areas]
            for Count,P in zip(Counts,Expected):
                assert abs(Count/32768-P)<=6*math.sqrt(P*(1-P)/32768)+2e-5
            Means=[]
            for Samples,ExpectedMean in zip(Statistics,(1/3,1/3,1/6,1/6,1/12)):
                Mean=sum(Samples)/32768
                SE=math.sqrt(sum((X-Mean)**2 for X in Samples)/(32767*32768))
                assert abs(Mean-ExpectedMean)<=6*SE+2e-5,(Corpus,Case,Arm,Mean,ExpectedMean)
                Means.append(Mean)
            Mean=sum(WrongRoot)/32768
            SE=math.sqrt(sum((X-Mean)**2 for X in WrongRoot)/(32767*32768))
            Negative[1]=abs(Mean-1/3)>6*SE+2e-5
            if Corpus==2 and Arm==1:
                for J,Rejected in enumerate(Negative):Controls[J]+=int(Rejected)
            Distributions[Corpus,Case,Arm]=(Counts,Expected)
            Moments.append(f'|{Corpus}|{Case}|{Arm}|16/16|'+'|'.join(f'{X:.8g}' for X in Means)+'|')
        for Method in range(6):
            for Packet in range(4096):
                Pixel=Method*4096+Packet
                Seed=Hash((Packet+(0x739ab531,0x4a615e23,0x5e92b341)[Corpus])^(Case*0x6c8e9cf5&0xffffffff))
                for Lane in range(4):
                    for Proposal in range(2 if Method<2 or Method>=4 else 1):
                        Draw=2*Lane+Proposal
                        N=Native(Corpus,Case,Method%2,Packet,Draw)
                        G=Quartet(2+4*Lane+Proposal,Pixel)
                        GpuIndex=struct.unpack('<I',struct.pack('<f',G[0]))[0]&0xffff
                        if N[0]!=GpuIndex:
                            Choice=Uniform(Seed^((3*Draw+1)*0x9e3779b9&0xffffffff))
                            assert Method%2==1 and min(abs(Choice-B) for B in Cdf)<2e-6
                            LocalAmbiguous+=1
                        else:
                            Local[0]=max(Local[0],Normalized(N[4],G[3]))
                            assert Local[0]<=2e-5
                            GPUPoint=World(T[GpuIndex],G[1],G[2])
                            GpuMaximum[2]=max(GpuMaximum[2],max(Normalized(A,B) for A,B in zip(N[5:8],GPUPoint)))
                            assert GpuMaximum[2]<=2e-5
                        Local[1]=max(Local[1],abs(N[2]-G[1]),abs(N[3]-G[2]))
                        assert Local[1]<=2e-6
                        if Method<2:assert N[8]==Quartet(5+4*Lane,Pixel)[3]
                        Compared+=1
        Ambiguous+=LocalAmbiguous
        GpuMaximum[0]=max(GpuMaximum[0],Local[0]);GpuMaximum[1]=max(GpuMaximum[1],Local[1])
        Rows.append(f'|{Corpus}|{Case}|65536|163840|{LocalAmbiguous}|{Local[0]:.7g}|{Local[1]:.7g}|')
assert Compared==1474560 and UniformChecks==589824 and Ambiguous<=16 and Controls==[3,3,3]
Font=ImageFont.load_default(size=14)
Small=ImageFont.load_default(size=12)
Canvas=Image.new('RGB',(1150,680),'#121923');Pen=ImageDraw.Draw(Canvas)
Pen.text((14,10),'Actual newly generated C++ area proposals / deformed fixtures / XZ projection / no tracing or shading',font=Font,fill='white')
for Case in range(3):
    T=Triangles(2,Case)
    Xs=[P[0] for Triangle in T for P in Triangle];Zs=[P[2] for Triangle in T for P in Triangle]
    for Arm in range(2):
        Left=20+Case*380;Top=65+Arm*295
        def Project(P):return (Left+15+int(325*(P[0]-min(Xs))/(max(Xs)-min(Xs))),
                              Top+210-int(195*(P[2]-min(Zs))/(max(Zs)-min(Zs))))
        for Triangle in T:Pen.line([Project(P) for P in Triangle]+[Project(Triangle[0])],fill='#65788b',width=1)
        for Packet in range(0,4096,5):
            N=Native(2,Case,Arm,Packet,0)
            X,Y=Project(N[5:8]);Colour=('#efb44d','#61c3bd','#bb8be0','#cddb74')[N[0]%4]
            Pen.ellipse((X-1,Y-1,X+1,Y+1),fill=Colour)
        Pen.text((Left,Top-24),f'Fixture {Case} / '+('uniform facet' if Arm==0 else 'proportional area'),font=Font,fill='white')
        Pen.text((Left,Top+228),'820 native points;colours=facet ordinal mod4',font=Small,fill='white')
Pen.text((14,650),'Same triangle order and seeds as retained GPU fixtures;all three corpora and both proposal types audited numerically.',font=Small,fill='white')
Canvas.save(Images/f'{Prefix}Points.png')
Summary=(f'PASS:Release/ASan+UBSan two CTests/build;193 proposal checks+14831 selector regression checks/build.\n'
         f'Fresh native proposals={UniformChecks};GPU observations={Compared};CDF ambiguities={Ambiguous}.\n'
         f'Native/Python normalized U,V,PDF maxima={Maximum};GPU PDF,barycentric,point maxima={GpuMaximum}.\n'
         f'18 count distributions(288 bins),90 barycentric moment gates pass;deformed negative controls={Controls}.\n'
         'Native fixtures,proposals and generated exact constructor headers byte-identical across builds.\n'
         'Native points are freshly generated;GPU transport is retained;no fresh tracing,RGB-equivalence or timing claim.\n'
         'Production348.046875MiB/defaults/layouts unchanged;no renderer activation,Windows or physical GPU test.\n')
(Results/f'{Prefix}-parity.txt').write_text(Summary)
(Results/f'{Prefix}-comparison.md').write_text('\n'.join(Rows)+'\n')
(Results/f'{Prefix}-moments.md').write_text('\n'.join(Moments)+'\n')
print(Summary)
Files=sorted([*Results.glob(f'{Prefix}*'),*Images.glob(f'{Prefix}*'),*Root.glob('Exhibits/LightCompute/Code/FacetAreaProposal*'),
              Root/'Exhibits/LightCompute/FacetAreaProposalPort.md',Root/'Exhibits/LightCompute/RunFacetAreaProposalPort.sh',
              Root/'Exhibits/LightCompute/FacetAreaProposalPort/CMakeLists.txt',
              Root/'Frontier/Engine/Rendering/FacetAreaProposals.h',Root/'Frontier/Engine/Rendering/FacetAreaProposals.cpp',
              Root/'Frontier/Engine/Rendering/CMakeLists.txt'])
Manifest=Results/f'{Prefix}-evidence-hashes.txt'
Manifest.write_text(''.join(f'{hashlib.sha256(P.read_bytes()).hexdigest()}  {P.relative_to(Root)}\n'
                            for P in Files if P!=Manifest))
