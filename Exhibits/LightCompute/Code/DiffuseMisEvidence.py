# 🧩 Audit same-ceiling diffuse MIS,actual query counts,null outcomes and area/solid-angle density conversion.
from pathlib import Path
import gzip
import hashlib
import math
import re
import struct
from PIL import Image,ImageDraw,ImageFont

Root=Path(__file__).resolve().parents[3]
Results=Root/'Exhibits/LightCompute/Results'
Images=Root/'Exhibits/LightCompute/Images'
Prefix='DiffuseMis';Count=131072;Samples=32768
Methods=('facet-uniform','area-proportional','cosine-BSDF','balance-MIS')
Rows=[];HashCounts=[];Maximum=[0.,0.]
for Mode in ('release','sanitized'):
    Text=(Results/f'{Prefix}-{Mode}.txt').read_text()
    assert '100% tests passed, 0 tests failed out of 5' in Text
    assert not re.search(r'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer',Text)
    Semantic=[L for L in Text.splitlines() if L.startswith('DIFFUSE_MIS_')]
    assert len(Semantic)==8 and 'correctness=1 checks=393216' in Semantic[-1]
    Rows.append(Semantic)
    Hashes=re.findall(r'^([a-f0-9]{64})  (.+)$',Text,re.M)
    for Digest,Name in Hashes:assert hashlib.sha256((Root/Name).read_bytes()).hexdigest()==Digest,Name
    HashCounts.append(len(Hashes))
assert Rows[0]==Rows[1]
for L in Rows[0]:
    if not L.startswith('DIFFUSE_MIS_CASE'):continue
    F=dict(re.findall(r'(\w+)=([^ ]+)',L))
    for I,K in enumerate(('error','pdf_error')):Maximum[I]=max(Maximum[I],float(F[K]))
for Line in (Results/'GeometricAreaReservoir-evidence-hashes.txt').read_text().splitlines():
    Digest,Name=Line.split('  ',1)
    assert hashlib.sha256((Root/Name).read_bytes()).hexdigest()==Digest,Name
for Case in range(3):
    Experimental={(P+0x12f7c413)^(Case*0x6c8e9cf5 & 0xffffffff) for P in range(Samples)}
    Banks=[]
    for Bank in range(2):
        Seeds={((P+0x5e65b9c7)^((Bank*8+Batch)*0x9e3779b9 & 0xffffffff)^
                (Case*0x6c8e9cf5 & 0xffffffff)) for P in range(Count) for Batch in range(8)}
        assert len(Seeds)==Count*8 and not Seeds.intersection(Experimental)
        Banks.append(Seeds)
    assert not Banks[0].intersection(Banks[1])
for Stem in ('DiffuseGenerate','DiffuseResolve'):
    Files=[Root/f'_ClaudeScratch/build/diffuse-mis-{M}/{Stem}.spv' for M in ('release','sanitized')]
    assert Files[0].read_bytes()==Files[1].read_bytes()
    (Results/f'{Prefix}-{Stem}.spv.gz').write_bytes(gzip.compress(Files[0].read_bytes(),mtime=0))

def Readback(Stem,Case,Size):
    Files=[Root/f'_ClaudeScratch/diffuse-mis-capture-{M}/{Stem}{Case}.f32' for M in ('release','sanitized')]
    Raw=Files[0].read_bytes();assert Raw==Files[1].read_bytes() and len(Raw)==Size
    (Results/f'{Stem}{Case}.f32.gz').write_bytes(gzip.compress(Raw,mtime=0))
    return Raw

def Triples(Raw,Start):
    return [struct.unpack_from('<3f',Raw,(Start+P)*16) for P in range(1024)]

def Estimate(Raw,Method,Slab=0):
    Runs=[Triples(Raw,Slab*Count+Method*Samples+R*1024) for R in range(32)]
    return [[sum(R[P][C] for R in Runs)/32 for C in range(3)] for P in range(1024)]

def MeanGate(Estimate,Reference):
    Measurements=[]
    for C in range(3):
        Residuals=[E[C]-R[C] for E,R in zip(Estimate,Reference)]
        Mean=sum(Residuals)/1024
        SE=math.sqrt(sum((R-Mean)**2 for R in Residuals)/(1023*1024))
        Measurements.append((Mean,SE,abs(Mean)<=6*SE+2e-5))
    return all(M[2] for M in Measurements),Measurements

def Encode(Pixels,Name,Scale):
    Encoded=[]
    for P in Pixels:
        assert all(math.isfinite(C) and C>=0 for C in P)
        Encoded.append(tuple(round(255*min(1,C*Scale)) for C in P))
    Tile=Image.new('RGB',(32,32));Tile.putdata(Encoded);Tile.save(Images/Name)
    assert Image.open(Images/Name).tobytes()==bytes(C for P in Encoded for C in P)
    return Tile

Font=ImageFont.load_default(size=14);Small=ImageFont.load_default(size=12)
Canvas=Image.new('RGB',(1120,850),'#121923');Pen=ImageDraw.Draw(Canvas)
Errors=Image.new('RGB',(1120,850),'#121923');ErrorPen=ImageDraw.Draw(Errors)
Pen.text((16,12),'Actual fresh diffuse MIS /32x32,nearest enlarged /linear exposure64 /32 proposals per pixel',font=Font,fill='white')
ErrorPen.text((16,12),'Absolute linear RGB error against finite reference /scale1024,clipped',font=Font,fill='white')
Table=['|Layout|Arm|Image RMSE|Cross MSE|Actual calls/pixel|Null proposals|',
       '|---:|---|---:|---:|---:|---:|']
Means=['|Layout|Arm|Channel|Mean residual|SE|Gate|','|---:|---|---:|---:|---:|---|']
Intervals=['|Layout|MIS minus area cross-MSE|Tile SE|Approx95% interval|','|---:|---:|---:|---|']
Metrics=[];CrossMetrics=[];ActualCosts=[];NullCounts=[];ReferenceNoise=[];NegativeFailures=[0,0];Gates=[]
for Case in range(3):
    Raw=Readback(Prefix,Case,23*Count*16)
    ReferenceRaw=Readback('DiffuseMisReference',Case,2048*16)
    Banks=[Triples(ReferenceRaw,B*1024) for B in range(2)]
    Reference=[[(Banks[0][P][C]+Banks[1][P][C])/2 for C in range(3)] for P in range(1024)]
    ReferenceNoise.append(math.sqrt(sum((A[C]-B[C])**2 for A,B in zip(*Banks) for C in range(3))/(4*3072)))
    ReferenceTile=Encode(Reference,f'{Prefix}-reference-{Case}.png',64)
    Y=70+Case*245
    for Sheet,Writer in ((Canvas,Pen),(Errors,ErrorPen)):
        Sheet.paste(ReferenceTile.resize((160,160),Image.Resampling.NEAREST),(16,Y))
        Writer.text((16,Y-22),f'Layout{Case}',font=Font,fill='white')
        Writer.text((16,Y+166),'Finite reference',font=Small,fill='white')
    Estimates=[];Rmses=[];Cross=[];Costs=[];Nulls=[]
    for Method in range(4):
        E=Estimate(Raw,Method);Estimates.append(E)
        Passed,Measurements=MeanGate(E,Reference)
        assert Passed,(Case,Method,Measurements)
        for C,(Mean,SE,Gate) in enumerate(Measurements):
            Means.append(f'|{Case}|{Methods[Method]}|{C}|{Mean:.8g}|{SE:.8g}|{Gate}|')
        if Method>=2 and not MeanGate(Estimate(Raw,Method,6),Reference)[0]:NegativeFailures[Method-2]+=1
        RMSE=math.sqrt(sum((A[C]-B[C])**2 for A,B in zip(E,Reference) for C in range(3))/3072)
        CrossMSE=sum((V[C]-A[C])*(V[C]-B[C]) for V,A,B in zip(E,*Banks) for C in range(3))/3072
        Rmses.append(RMSE);Cross.append(CrossMSE)
        Work=[struct.unpack_from('<4f',Raw,(18*Count+Method*Samples+P)*16) for P in range(Samples)]
        assert all(W[0]==1 and W[1] in (0,1) and W[2] in (0,1) and W[3]==1+W[1]+W[2] for W in Work)
        Costs.append(sum(W[3] for W in Work)/1024)
        Null=sum(struct.unpack_from('<I',Raw,(2*Count+Method*Samples+P)*16)[0]==0xffffffff for P in range(Samples))
        Nulls.append(Null)
        if Method<2:assert Null==0
        Tile=Encode(E,f'{Prefix}-{Case}-{Method}.png',64)
        ErrorTile=Encode([[abs(A[C]-B[C]) for C in range(3)] for A,B in zip(E,Reference)],
                         f'{Prefix}-error-{Case}-{Method}.png',1024)
        X=232+Method*220
        for Display,Sheet,Writer in ((Tile,Canvas,Pen),(ErrorTile,Errors,ErrorPen)):
            Sheet.paste(Display.resize((160,160),Image.Resampling.NEAREST),(X,Y))
            Writer.text((X,Y+166),Methods[Method],font=Small,fill='white')
            Writer.text((X,Y+182),f'RMSE {RMSE:.5g};calls {Costs[-1]:.2f}',font=Small,fill='white')
        Table.append(f'|{Case}|{Methods[Method]}|{RMSE:.9g}|{CrossMSE:.9g}|{Costs[-1]:.6f}|{Null}|')
    Differences=[]
    for P in range(1024):
        def CrossError(E):
            return sum((E[P][C]-Banks[0][P][C])*(E[P][C]-Banks[1][P][C]) for C in range(3))/3
        Differences.append(CrossError(Estimates[3])-CrossError(Estimates[1]))
    Blocks=[sum(Differences[(Y*4+DY)*32+X*4+DX] for DY in range(4) for DX in range(4))/16
            for Y in range(8) for X in range(8)]
    Mean=sum(Blocks)/64;SE=math.sqrt(sum((B-Mean)**2 for B in Blocks)/(63*64))
    Intervals.append(f'|{Case}|{Mean:.7g}|{SE:.7g}|[{Mean-1.96*SE:.7g},{Mean+1.96*SE:.7g}]|')
    Gates.append(Rmses[3]<=Rmses[1] and Cross[3]<=Cross[1])
    Metrics.append(Rmses);CrossMetrics.append(Cross);ActualCosts.append(Costs);NullCounts.append(Nulls)
assert all(N>0 for N in NegativeFailures),NegativeFailures
for Sheet,Writer,Name in ((Canvas,Pen,'DiffuseMisComparison.png'),(Errors,ErrorPen,'DiffuseMisErrors.png')):
    Writer.text((16,820),'GPU readbacks;96 maximum calls/pixel,actual work differs;no reservoir reuse,denoiser or physical GPU timing.',font=Small,fill='white')
    Sheet.save(Images/Name)
(Results/f'{Prefix}-comparison.md').write_text('\n'.join(Table+['']+Intervals)+'\n')
(Results/f'{Prefix}-means.md').write_text('\n'.join(Means)+'\n')
Summary=(f'PASS correctness:8 identical semantic rows;runtime hashes/build={HashCounts};six identical buffers;two identical modules.\n'
         f'CPU comparisons/build=393216;maximum RGB/PDF errors={Maximum}.\n'
         f'RGB mean gates=36;missing-Jacobian negative control failures BSDF/MIS={NegativeFailures}.\n'
         f'Frozen MIS versus area-proportional quality gates={Gates};all-three acceptance={all(Gates)};production_promote=0.\n'
         f'RMSE by layout={Metrics}\nCross MSE by layout={CrossMetrics}\n'
         f'Actual calls/pixel={ActualCosts}\nNull proposals by layout={NullCounts}\n'
         f'Estimated combined-reference RMS noise={ReferenceNoise}\n'
         'Experiments:393216 primary queries;proposal/suffix calls are measured,zero-support proposals elided.\n'
         'References:6291456 proposals/build,1024 per bank/pixel;primary geometry invariant.\n'
         'Host ASan/UBSan only;driver/SPIR-V uninstrumented;Windows/physical GPUs untested.\n')
(Results/f'{Prefix}-parity.txt').write_text(Summary);print(Summary)
Files=sorted([*Results.glob('DiffuseMis*'),*Images.glob('DiffuseMis*'),*Root.glob('Exhibits/LightCompute/Code/DiffuseMis*'),
              Root/'Exhibits/LightCompute/RunDiffuseMis.sh',Root/'Exhibits/LightCompute/DiffuseMis.md'])
Manifest=Results/f'{Prefix}-evidence-hashes.txt'
Manifest.write_text(''.join(f'{hashlib.sha256(P.read_bytes()).hexdigest()}  {P.relative_to(Root)}\n' for P in Files if P!=Manifest))
