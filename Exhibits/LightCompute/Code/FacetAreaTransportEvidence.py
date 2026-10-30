# 🧩 Compare freshly traced native captures with retained GPU transport,reference banks and independent-ray audit logs.
from pathlib import Path
import gzip
import hashlib
import math
import lzma
import re
import struct
import subprocess
import sys
from PIL import Image, ImageDraw, ImageFont

Root=Path(__file__).resolve().parents[3]
Results=Root/'Exhibits/LightCompute/Results';Images=Root/'Exhibits/LightCompute/Images'
Prefix='FacetAreaTransport'
Phases=('GeometricAreaReservoir','PerspectiveAreaReservoir','DeformedAreaReservoir')
Builds=[Root/f'_ClaudeScratch/build/facet-area-transport-{M}' for M in ('release','sanitized')]
for Phase in Phases:
    for Line in (Results/f'{Phase}-evidence-hashes.txt').read_text().splitlines():
        H,N=Line.split('  ',1);assert hashlib.sha256((Root/N).read_bytes()).hexdigest()==H,N
for Old,Revision in (('FacetAreaPort','9dcfb7b'),('FacetAreaProposalPort','4b32869'),('FacetAreaScorePort','bdd8ed2')):
    for Line in (Results/f'{Old}-evidence-hashes.txt').read_text().splitlines():
        H,N=Line.split('  ',1)
        Blob=subprocess.run(['git','show',Revision+':'+N],capture_output=True,check=True).stdout
        assert hashlib.sha256(Blob).hexdigest()==H,N
        if N!='Frontier/Engine/Rendering/CMakeLists.txt':assert hashlib.sha256((Root/N).read_bytes()).hexdigest()==H,N
Semantic=[]
for Mode in ('release','sanitized'):
    Text=(Results/f'{Prefix}-{Mode}.txt').read_text()
    assert '100% tests passed, 0 tests failed out of 4' in Text
    for Unit,Count in (('TRANSPORT',1058),('SCORE',1060),('PROPOSAL',193),('PORT',14831)):
        assert f'FACET_AREA_{Unit}_UNIT checks={Count} pass=1' in Text
    assert not re.search(r'ERROR: AddressSanitizer|runtime error:',Text)
    Lines=[L for L in Text.splitlines() if L.startswith('FRESH_NATIVE_')]
    assert len(Lines)==10 and Lines[-1]=='FRESH_NATIVE_TOTAL primary=221184 candidates=1474560 paths=1179648 visibility=2359296 double_primary_ties=0 pass=1'
    Semantic.append(Lines)
    for H,N in re.findall(r'^([a-f0-9]{64})  (.+)$',Text,re.M):
        assert hashlib.sha256((Root/N).read_bytes()).hexdigest()==H,N
assert Semantic[0]==Semantic[1]
assert (Builds[0]/'FacetAreaProposalScenes.h').read_bytes()==(Builds[1]/'FacetAreaProposalScenes.h').read_bytes()
Count=24576
Methods=('RIS uniform','RIS area','raw4 uniform','raw4 area','raw8 uniform','raw8 area')
Table=['|Corpus|Fixture|Method|Native RMSE|GPU RMSE|Cross-reference MSE|Max image delta|',
       '|---|---:|---|---:|---:|---:|---:|']
Means=['|Corpus|Fixture|Arm|Channel|Residual|SE|Gate|','|---|---:|---:|---:|---:|---:|---|']
Negative=['|Deformed fixture|Omitted connection changed packets|Omitted suffix changed packets|',
          '|---:|---:|---:|']
MaxPacket=0.;MaxImage=0.;MaxNormalizer=0.;MaxScore=0.;MaxRay=0.
CandidateChecks=0;PathChecks=0;MeanChecks=0;PrimaryChecks=0
ControlResults=[]
MaxLane=0.
Font=ImageFont.load_default(size=14);Small=ImageFont.load_default(size=12)
Canvas=Image.new('RGB',(1100,850),'#121923');Pen=ImageDraw.Draw(Canvas)
Pen.text((16,10),'Fresh native primary + selected-path transport /32x32,nearest enlarged /linear exposure64',font=Font,fill='white')

def Q(Raw,Slab,Pixel):return struct.unpack_from('<4f',Raw,(Slab*Count+Pixel)*16)
def Bits(Raw,Slab,Pixel):return struct.unpack_from('<I',Raw,(Slab*Count+Pixel)*16)[0]
def Estimate(Raw,Method,Slab=0):
    return [[sum(Q(Raw,Slab,Method*4096+Run*1024+P)[C] for Run in range(4))/4 for C in range(3)] for P in range(1024)]
def Store(Pixels,Name):
    assert all(math.isfinite(C) and C>=0 for P in Pixels for C in P)
    Encoded=[tuple(round(255*min(1,C*64)) for C in P) for P in Pixels]
    Tile=Image.new('RGB',(32,32));Tile.putdata(Encoded);Tile.save(Images/Name)
    assert Image.open(Images/Name).tobytes()==bytes(C for P in Encoded for C in P)
    return Tile

for Corpus,Phase in enumerate(Phases):
    for Fixture in range(3):
        Name=f'NativeAreaTransport{Corpus}{Fixture}.f32'
        Native=(Builds[0]/'captures'/Name).read_bytes()
        assert len(Native)==41*Count*16 and Native==(Builds[1]/'captures'/Name).read_bytes()
        Archive=Results/f'{Prefix}-{Corpus}{Fixture}.f32.xz'
        Archive.write_bytes(lzma.compress(Native,preset=6))
        assert lzma.decompress(Archive.read_bytes())==Native
        GPU=gzip.decompress((Results/f'{Phase}{Fixture}.f32.gz').read_bytes())
        # All primary hits,positions,normals,albedos happen to match bitwise,which is stricter than frozen gates.
        assert Native[19*Count*16:23*Count*16]==GPU[19*Count*16:23*Count*16]
        PrimaryChecks+=Count
        for Pixel in range(Count):
            Method=Pixel//4096
            assert Q(Native,18,Pixel)==Q(GPU,18,Pixel)
            A=Q(Native,0,Pixel);B=Q(GPU,0,Pixel)
            MaxPacket=max(MaxPacket,*(abs(X-Y) for X,Y in zip(A[:3],B[:3])))
            assert MaxPacket<=2e-5
            Slots=int(Q(Native,18,Pixel)[0])
            for Lane in range(4):
                NT=Q(Native,5+4*Lane,Pixel);GT=Q(GPU,5+4*Lane,Pixel)
                assert NT[2:]==GT[2:]
                for P in range(2):
                    Slab=2+4*Lane+P
                    assert Bits(Native,Slab,Pixel)==Bits(GPU,Slab,Pixel),(Corpus,Fixture,Pixel,Lane,P)
                    assert Q(Native,Slab,Pixel)[1:]==Q(GPU,Slab,Pixel)[1:]
                    MaxScore=max(MaxScore,abs(NT[P]-GT[P]))
                    if P==0 or Method<2 or Method>=4:CandidateChecks+=1
                NC=Q(Native,4+4*Lane,Pixel);GC=Q(GPU,4+4*Lane,Pixel)
                MaxLane=max(MaxLane,*(abs(A-B) for A,B in zip(NC[:3],GC[:3])))
                assert MaxLane<=2e-5
                NZ=NC[3];GZ=GC[3]
                MaxNormalizer=max(MaxNormalizer,abs(NZ-GZ)/max(1,abs(GZ)))
            assert MaxScore<=2e-5 and MaxNormalizer<=2e-5
            for Slot in range(8):
                Origin=Q(Native,23+2*Slot,Pixel);Direction=Q(Native,24+2*Slot,Pixel)
                if Slot>=Slots:
                    assert Origin==Direction==(0.,0.,0.,0.)
                else:
                    assert all(math.isfinite(X) for X in Origin+Direction)
                    assert Origin[3]>.00001 and Direction[3]==0
                    assert abs(sum(X*X for X in Direction[:3])-1)<2e-5
                if Corpus==2:
                    MaxRay=max(MaxRay,*(abs(A-B) for A,B in zip(Origin,Q(GPU,23+2*Slot,Pixel))),
                               *(abs(A-B) for A,B in zip(Direction,Q(GPU,24+2*Slot,Pixel))))
                    assert MaxRay<=2e-5
            PathChecks+=Slots
        RefStem=('GeometricAreaReference','PerspectiveAreaReference','DeformedAreaReference')[Corpus]
        Reference=gzip.decompress((Results/f'{RefStem}{Fixture}.f32.gz').read_bytes())
        Banks=[[struct.unpack_from('<3f',Reference,(B*1024+P)*16) for P in range(1024)] for B in range(2)]
        Ref=[[(Banks[0][P][C]+Banks[1][P][C])/2 for C in range(3)] for P in range(1024)]
        def MSE(V):return sum((A[C]-B[C])**2 for A,B in zip(V,Ref) for C in range(3))/3072
        def Cross(V):return sum((V[P][C]-Banks[0][P][C])*(V[P][C]-Banks[1][P][C]) for P in range(1024) for C in range(3))/3072
        NativeImages=[];GpuImages=[]
        for Method in range(6):
            E=Estimate(Native,Method);G=Estimate(GPU,Method)
            NativeImages.append(E);GpuImages.append(G)
            Delta=max(abs(A[C]-B[C]) for A,B in zip(E,G) for C in range(3));MaxImage=max(MaxImage,Delta)
            assert Delta<=2e-5
            if Method<2:
                assert MSE(E)<=MSE(G)+1e-10 and Cross(E)<=Cross(G)+1e-10
                for C in range(3):
                    D=[A[C]-B[C] for A,B in zip(E,Ref)];Mean=sum(D)/1024
                    SE=math.sqrt(sum((X-Mean)**2 for X in D)/(1023*1024));Passed=abs(Mean)<=6*SE+2e-5
                    Means.append(f'|{Corpus}|{Fixture}|{Method}|{C}|{Mean:.8g}|{SE:.8g}|{Passed}|')
                    assert Passed;MeanChecks+=1
            Store(E,f'{Prefix}-{Corpus}-{Fixture}-{Method}.png')
            Table.append(f'|{Corpus}|{Fixture}|{Methods[Method]}|{math.sqrt(MSE(E)):.8g}|{math.sqrt(MSE(G)):.8g}|'
                         f'{Cross(E):.8g}|{Delta:.7g}|')
        if Corpus==2:
            Changed=[]
            for Slab in (39,40):
                Altered=sum(any(abs(A-B)>1e-12 for A,B in zip(Q(Native,Slab,P)[:3],Q(Native,0,P)[:3]))
                            for P in range(Count))
                Changed.append(Altered)
            Negative.append(f'|{Fixture}|{Changed[0]}|{Changed[1]}|')
            ControlResults.append(Changed)
            Y=70+Fixture*245
            for Column,(Pixels,Label) in enumerate(((Ref,'Finite GPU reference'),(NativeImages[0],'Fresh native uniform RIS'),
                                                   (GpuImages[0],'Retained GPU uniform RIS'),
                                                   (NativeImages[2],'Fresh native raw4'),(NativeImages[4],'Fresh native raw8'))):
                Tile=Store(Pixels,f'{Prefix}-shown-{Fixture}-{Column}.png');X=16+Column*216
                Canvas.paste(Tile.resize((144,144),Image.Resampling.NEAREST),(X,Y))
                Pen.text((X,Y+155),Label,font=Small,fill='white')
                Pen.text((X,Y+173),f'RMSE {math.sqrt(MSE(Pixels)):.5g}' if Column else 'Finite same-renderer target',font=Small,fill='white')
            Pen.text((16,Y-23),f'Fixture{Fixture}',font=Font,fill='white')
assert (PrimaryChecks,CandidateChecks,PathChecks,MeanChecks)==(221184,1474560,1179648,54)
Pen.text((16,820),'Native executable receives only scene/camera constructors and an output path. GPU captures/reference banks used afterward for comparison.',font=Small,fill='white')
Canvas.save(Images/f'{Prefix}Comparison.png')
(Results/f'{Prefix}-comparison.md').write_text('\n'.join(Table)+'\n')
(Results/f'{Prefix}-means.md').write_text('\n'.join(Means)+'\n')
(Results/f'{Prefix}-negative.md').write_text('\n'.join(Negative)+'\n')
Accepted=all(all(C>0 for C in Row) for Row in ControlResults)
Summary=(f'HELD: numerical transport parity passes;frozen per-fixture visibility-control sensitivity fails.\n'
         f'PASS strict Release/ASan+UBSan,four CTests/build;1058 transport+1060 score+193 proposal+14831 selector units/build.\n'
         f'Fresh primary hits={PrimaryChecks},candidate scores={CandidateChecks},traced selected/raw paths={PathChecks}.\n'
         'Independent double BVH and linear-double rays agree exactly on2359296 emitted-ray visibility decisions/build.\n'
         'All primary captures byte-identical to GPU;all proposal ordinals,visibility bits and selections match GPU.\n'
         f'Max lane RGB={MaxLane};packet RGB={MaxPacket};image RGB={MaxImage};normalizer={MaxNormalizer};target={MaxScore};deformed ray={MaxRay}.\n'
         'Nine native captures and10 semantic rows identical across builds;54 images/54 means,18 RIS MSE gates pass.\n'
         f'Omitted connection/suffix changed packets by deformed fixture={ControlResults};acceptance={int(Accepted)}.\n'
         'Empty/invalid BVH is rejected. Original sensitivity requirement is retained,not waived.\n'
         'Native execution consumes no GPU capture/receiver/visibility/RGB;GPU and references only post-run comparisons.\n'
         'Fixed diffuse nine-fixture corpus,not general materials/history/editor integration or physical GPU/Windows acceptance.\n'
         'Production348.046875MiB/defaults/32B descriptors unchanged;no physical speedup claim.\n')
(Results/f'{Prefix}-parity.txt').write_text(Summary);print(Summary)
Files=sorted([*Results.glob(f'{Prefix}*'),*Images.glob(f'{Prefix}*'),*Root.glob('Exhibits/LightCompute/Code/FacetAreaTransport*'),
              Root/'Exhibits/LightCompute/FacetAreaTransport.md',Root/'Exhibits/LightCompute/RunFacetAreaTransport.sh',
              Root/'Exhibits/LightCompute/FacetAreaTransport/CMakeLists.txt',Root/'Frontier/Engine/Rendering/FacetAreaTransport.h',
              Root/'Frontier/Engine/Rendering/FacetAreaTransport.cpp',Root/'Frontier/Engine/Rendering/CMakeLists.txt'])
Manifest=Results/f'{Prefix}-evidence-hashes.txt'
Manifest.write_text(''.join(f'{hashlib.sha256(P.read_bytes()).hexdigest()}  {P.relative_to(Root)}\n' for P in Files if P!=Manifest))

if not Accepted:
    print("FROZEN_ACCEPTANCE_FAILED:connection controls1/3,suffix controls2/3;no promotion.",file=sys.stderr)
    sys.exit(1)
