# 🧩 Audit continuous-area PDFs,independent quadrature,negative controls and actual Vulkan packet arrays.
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
Rows=[];HashCounts=[]
for Mode in ('release','sanitized'):
    Text=(Results/f'AreaReconnection-{Mode}.txt').read_text()
    assert '100% tests passed, 0 tests failed out of 5' in Text
    assert not re.search(r'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer',Text)
    Semantic=[Line for Line in Text.splitlines() if Line.startswith('AREA_RECONNECTION_')]
    assert len(Semantic)==29 and 'correctness=1' in Semantic[-1]
    Rows.append(Semantic)
    Hashes=re.findall(r'^([a-f0-9]{64})  (.+)$',Text,re.M)
    for Digest,Name in Hashes:assert hashlib.sha256((Root/Name).read_bytes()).hexdigest()==Digest,Name
    HashCounts.append(len(Hashes))
assert Rows[0]==Rows[1]
for Stem in ('DiffuseGenerate','DiffuseResolve'):
    Modules=[Root/f'_ClaudeScratch/build/area-reconnection-{Mode}/{Stem}.spv' for Mode in ('release','sanitized')]
    assert Modules[0].read_bytes()==Modules[1].read_bytes()
    (Results/f'AreaReconnection-{Stem}.spv.gz').write_bytes(gzip.compress(Modules[0].read_bytes(),mtime=0))
Reference={};Cases={}
for Line in Rows[0]:
    Fields=dict(re.findall(r'(\w+)=([^ ]+)',Line))
    if Line.startswith('AREA_RECONNECTION_REFERENCE'):
        Reference[int(Fields['case']),int(Fields['resolution'])]=[float(Fields[Channel]) for Channel in ('r','g','b')]
    if Line.startswith('AREA_RECONNECTION_CASE'):Cases[int(Fields['case'])]=Fields
Table=['|Case|Proposal|Mean R|Mean G|Mean B|RGB RMSE|Calls|Mean gate|','|---:|---|---:|---:|---:|---:|---:|---|']
Negatives=['|Case|Proposal|Missing area mean|Missing Jacobian mean|Reference mean|Area fails|Jacobian fails|',
           '|---:|---|---:|---:|---:|---|---|']
Methods=('uniform-facet','area-weighted')
MeanGates=0;MomentGates=0;NegativeFailures=[0,0];Grazing=0;MaximumEquivalence=0;Metrics=[]
Font=ImageFont.load_default(size=15);Small=ImageFont.load_default(size=12)
Canvas=Image.new('RGB',(1240,690),'#121923');Pen=ImageDraw.Draw(Canvas)
Pen.text((16,12),'Actual fresh continuous-area RGB arrays / logical packets,not camera pixels / linear exposure64,clipped',font=Font,fill='white')
for Case in range(9):
    Paths=[Root/f'_ClaudeScratch/area-reconnection-capture-{Mode}/AreaReconnection{Case}.f32' for Mode in ('release','sanitized')]
    Raw=Paths[0].read_bytes();assert Raw==Paths[1].read_bytes() and len(Raw)==19*4096*16
    (Results/f'AreaReconnection{Case}.f32.gz').write_bytes(gzip.compress(Raw,mtime=0))
    Fine=Reference[Case,256];Coarse=Reference[Case,128]
    Drift=[abs(A-B) for A,B in zip(Fine,Coarse)]
    Rmses=[]
    assert int(Cases[Case]['suffix'])==32768 and int(Cases[Case]['connection'])==32768
    Grazing+=int(Cases[Case]['grazing'])
    for Method in range(2):
        Samples=[struct.unpack_from('<3f',Raw,(Method*2048+Pixel)*16) for Pixel in range(2048)]
        Direction=[struct.unpack_from('<3f',Raw,(4096+Method*2048+Pixel)*16) for Pixel in range(2048)]
        assert all(math.isfinite(A) and A>=0 for Sample in Samples for A in Sample)
        MaximumEquivalence=max(MaximumEquivalence,max(abs(A-B) for X,Y in zip(Samples,Direction) for A,B in zip(X,Y)))
        assert MaximumEquivalence<2e-5
        Means=[sum(Sample[Channel] for Sample in Samples)/2048 for Channel in range(3)]
        SE=[math.sqrt(sum((Sample[Channel]-Means[Channel])**2 for Sample in Samples)/(2047*2048)) for Channel in range(3)]
        assert all(abs(A-B)<=6*C+2*D+2e-5 for A,B,C,D in zip(Means,Fine,SE,Drift)),(Case,Method,Means,Fine,SE,Drift)
        MeanGates+=3
        Rmse=math.sqrt(sum((Sample[Channel]-Fine[Channel])**2 for Sample in Samples for Channel in range(3))/(2048*3))
        Rmses.append(Rmse)
        Table.append(f'|{Case}|{Methods[Method]}|'+ '|'.join(f'{A:.9g}' for A in Means)+f'|{Rmse:.9g}|32768|PASS|')
        Work=[struct.unpack_from('<4f',Raw,(18*4096+Method*2048+Pixel)*16) for Pixel in range(2048)]
        assert all(Query[0]==8 and Query[1]==8 for Query in Work)
        NegativeMeans=[];Failures=[]
        for Index in range(2):
            Values=[Query[2+Index] for Query in Work];Mean=sum(Values)/2048
            StandardError=math.sqrt(sum((A-Mean)**2 for A in Values)/(2047*2048))
            Failure=abs(Mean-sum(Fine)/3)>6*StandardError+2*sum(Drift)/3+2e-5
            NegativeFailures[Index]+=int(Failure);NegativeMeans.append(Mean);Failures.append(Failure)
        Negatives.append(f'|{Case}|{Methods[Method]}|{NegativeMeans[0]:.9g}|{NegativeMeans[1]:.9g}|{sum(Fine)/3:.9g}|{int(Failures[0])}|{int(Failures[1])}|')
        Barycentrics=[[],[]]
        for Draw in range(8):
            for Pixel in range(2048):
                Packed,U,V,Density=struct.unpack_from('<I3f',Raw,((2+2*Draw)*4096+Method*2048+Pixel)*16)
                assert (Packed&0xffff)<10 and U>=0 and V>=0 and U+V<=1.000001 and Density>0
                Barycentrics[0].append(U);Barycentrics[1].append(V)
        for Values in Barycentrics:
            for Power,Expected in ((1,1/3),(2,1/6)):
                Moments=[A**Power for A in Values];Mean=sum(Moments)/len(Moments)
                StandardError=math.sqrt(sum((A-Mean)**2 for A in Moments)/(len(Moments)*(len(Moments)-1)))
                assert abs(Mean-Expected)<=6*StandardError+2e-5,(Case,Method,Power,Mean)
                MomentGates+=1
        Pixels=[tuple(max(0,min(255,round(A*64*255))) for A in Sample) for Sample in Samples]
        Panel=Image.new('RGB',(64,32));Panel.putdata(Pixels)
        File=Images/f'AreaReconnection-{Case}-{Method}.png';Panel.save(File)
        with Image.open(File) as Reopened:assert Reopened.tobytes()==bytes(A for Pixel in Pixels for A in Pixel)
        X=16+(Case%3)*410;Y=58+(Case//3)*205+Method*83
        Canvas.paste(Panel.resize((256,128//2),Image.Resampling.NEAREST),(X,Y+18))
        Pen.text((X,Y),f'Case{Case} {Methods[Method]}',font=Small,fill='white')
    Metrics.append(Rmses)
assert MeanGates==54 and MomentGates==72 and Grazing>0 and all(A>0 for A in NegativeFailures)
Canvas.save(Images/'AreaReconnectionReadbacks.png')
Chart=Image.new('RGB',(1250,760),'#121923');Pen=ImageDraw.Draw(Chart)
Pen.text((16,12),'Actual continuous-surface RGB RMSE / lower is better / equal queries,NOT physical time / no temporal reuse',font=Font,fill='white')
for Case,Rmses in enumerate(Metrics):
    X=20+(Case%3)*415;Y=58+(Case//3)*205
    Pen.text((X,Y),f'Layout{Case//3},receiver{Case%3} / case{Case}',font=Font,fill='white')
    Scale=max(Rmses)
    for Method in range(2):
        Color=('#8dc3f3','#79d5ab')[Method]
        Pen.rectangle((X,Y+32+Method*49,X+Rmses[Method]/max(Scale,1e-30)*155,Y+50+Method*49),fill=Color)
        Pen.text((X+165,Y+32+Method*49),f'{Methods[Method]} {Rmses[Method]:.6g}',font=Small,fill=Color)
Pen.text((16,690),'Reference:double CPU 256x256/facet quadrature;128x128 drift is included in empirical mean gates,not a certified bound.',font=Small,fill='white')
Pen.text((16,725),'All10 facets sampled continuously. Joint facet/direction Jacobian verified;not a marginal directional MIS PDF.',font=Small,fill='white')
Pooled=[math.sqrt(sum(Row[Method]**2 for Row in Metrics)/9) for Method in range(2)]
Pen.text((16,660),f'Equal-case pooled RMSE:uniform={Pooled[0]:.7g};area={Pooled[1]:.7g}. Case wins and pooled error rank differently.',font=Small,fill='white')
Chart.save(Images/'AreaReconnectionComparison.png')
(Results/'AreaReconnection-comparison.md').write_text('# Continuous-area measured comparison\n\n'+'\n'.join(Table)+'\n\n'+'\n'.join(Negatives)+f'\n\nExploratory equal-case pooled RMSE:uniform={Pooled[0]:.9g},area={Pooled[1]:.9g};not a frozen promotion gate.\n')
AreaWins=sum(B<A for A,B in Metrics)
Summary=(f'PASS continuous-area implementation:29 identical semantic rows;hashes/build={HashCounts};nine identical captures;two identical SPIR-V files.\n'
         f'294912 continuous paths and589824 binary CPU visibility comparisons/build;54 RGB mean gates;72 barycentric moment gates;18 exact PNG arrays.\n'
         f'Grazing paths={Grazing};negative failed mean gates missing_area/missing_Jacobian={NegativeFailures};area-weighted RMSE wins={AreaWins}/9.\n'
         f'Exploratory equal-case pooled RMSE uniform/area={Pooled};not a frozen gate.\n'
         f'Max area/direction estimate difference={MaximumEquivalence};CPU max estimate={max(float(A["error"]) for A in Cases.values())};density={max(float(A["density_error"]) for A in Cases.values())}.\n'
         'Total queries/build=589824,equal32768/arm/case;no physical time,production promotion,temporal reuse or multibounce claim.\n')
(Results/'AreaReconnection-parity.txt').write_text(Summary)
Files=(sorted(Results.glob('AreaReconnection*.gz'))+sorted(Images.glob('AreaReconnection*.png'))+
       [Results/f'AreaReconnection-{Name}' for Name in ('release.txt','sanitized.txt','comparison.md','parity.txt')]+[Path(__file__)])
(Results/'AreaReconnection-evidence-hashes.txt').write_text(''.join(hashlib.sha256(P.read_bytes()).hexdigest()+'  '+str(P.relative_to(Root))+'\n' for P in Files))
print(Summary,end='')
