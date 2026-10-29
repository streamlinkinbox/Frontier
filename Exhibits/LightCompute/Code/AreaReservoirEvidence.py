# 🧩 Audit fresh continuous-area RIS against equal-query and equal-proposal raw controls.
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
for Line in (Results/'AreaReconnection-evidence-hashes.txt').read_text().splitlines():
    Digest,Name=Line.split('  ',1)
    assert hashlib.sha256((Root/Name).read_bytes()).hexdigest()==Digest,Name
Reference={}
for Line in (Results/'AreaReconnection-release.txt').read_text().splitlines():
    if not Line.startswith('AREA_RECONNECTION_REFERENCE'):continue
    Fields=dict(re.findall(r'(\w+)=([^ ]+)',Line))
    Reference[int(Fields['case']),int(Fields['resolution'])]=[float(Fields[C]) for C in ('r','g','b')]
Rows=[];HashCounts=[];Maximum=[0.,0.]
for Mode in ('release','sanitized'):
    Text=(Results/f'AreaReservoir-{Mode}.txt').read_text()
    assert '100% tests passed, 0 tests failed out of 5' in Text
    assert not re.search(r'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer',Text)
    Semantic=[Line for Line in Text.splitlines() if Line.startswith('AREA_RESERVOIR_')]
    assert len(Semantic)==11 and 'correctness=1 candidates=368640 traced=294912' in Semantic[-1]
    Rows.append(Semantic)
    Hashes=re.findall(r'^([a-f0-9]{64})  (.+)$',Text,re.M)
    for Digest,Name in Hashes:assert hashlib.sha256((Root/Name).read_bytes()).hexdigest()==Digest,Name
    HashCounts.append(len(Hashes))
assert Rows[0]==Rows[1]
for Line in Rows[0]:
    if not Line.startswith('AREA_RESERVOIR_CASE'):continue
    F=dict(re.findall(r'(\w+)=([^ ]+)',Line))
    for Index,Key in enumerate(('error','normalizer_error')):Maximum[Index]=max(Maximum[Index],float(F[Key]))
for Stem in ('DiffuseGenerate','DiffuseResolve'):
    Modules=[Root/f'_ClaudeScratch/build/area-reservoir-{Mode}/{Stem}.spv' for Mode in ('release','sanitized')]
    assert Modules[0].read_bytes()==Modules[1].read_bytes()
    (Results/f'AreaReservoir-{Stem}.spv.gz').write_bytes(gzip.compress(Modules[0].read_bytes(),mtime=0))
Methods=('RIS-uniform','RIS-area','raw4-uniform','raw4-area','raw8-uniform','raw8-area')
Table=['|Case|Method|Proposals/packet|Calls/packet|RGB RMSE|Mean gate|','|---:|---|---:|---:|---:|---|']
Ratios=['|Case|Uniform RIS/raw4|Area RIS/raw4|Uniform RIS/raw8|Area RIS/raw8|','|---:|---:|---:|---:|---:|']
Means=['|Case|Method|Mean R|Mean G|Mean B|Reference R|Reference G|Reference B|','|---:|---|---:|---:|---:|---:|---:|---:|']
Intervals=['|Case|ProposalMeasure|Paired RIS minus raw4 MSE|Block SE|95% interval|','|---:|---|---:|---:|---|']
NegativeFailures=[0,0];MeanGates=0;Metrics=[];WireChecks=0;PreviousMaximum=0
Font=ImageFont.load_default(size=15);Small=ImageFont.load_default(size=12)
Canvas=Image.new('RGB',(1250,1030),'#121923');Pen=ImageDraw.Draw(Canvas)
Pen.text((16,12),'Actual fresh continuous-area packet RGB / exposure64,clipped / logical packets,NOT camera pixels',font=Font,fill='white')
for Case in range(9):
    Paths=[Root/f'_ClaudeScratch/area-reservoir-capture-{Mode}/AreaReservoir{Case}.f32' for Mode in ('release','sanitized')]
    Raw=Paths[0].read_bytes();assert Raw==Paths[1].read_bytes() and len(Raw)==19*6144*16
    (Results/f'AreaReservoir{Case}.f32.gz').write_bytes(gzip.compress(Raw,mtime=0))
    Previous=gzip.decompress((Results/f'AreaReconnection{Case}.f32.gz').read_bytes())
    for ProposalMeasure in range(2):
        for Pixel in range(1024):
            Old=struct.unpack_from('<3f',Previous,(ProposalMeasure*2048+Pixel)*16)
            New=struct.unpack_from('<3f',Raw,((ProposalMeasure+4)*1024+Pixel)*16)
            PreviousMaximum=max(PreviousMaximum,max(abs(A-B)/max(1.,abs(A)) for A,B in zip(Old,New)))
    assert PreviousMaximum<2e-5
    Fine=Reference[Case,256];Drift=[abs(A-B) for A,B in zip(Fine,Reference[Case,128])]
    Rmses=[];BlockErrors=[]
    for Method in range(6):
        Samples=[struct.unpack_from('<3f',Raw,(Method*1024+Pixel)*16) for Pixel in range(1024)]
        assert all(math.isfinite(A) and A>=0 for S in Samples for A in S)
        Mean=[sum(S[C] for S in Samples)/1024 for C in range(3)]
        SE=[math.sqrt(sum((S[C]-Mean[C])**2 for S in Samples)/(1023*1024)) for C in range(3)]
        assert all(abs(A-B)<=6*C+2*D+2e-5 for A,B,C,D in zip(Mean,Fine,SE,Drift)),(Case,Method,Mean,Fine,SE)
        MeanGates+=3
        Means.append(f'|{Case}|{Methods[Method]}|'+ '|'.join(f'{A:.8g}' for A in Mean+Fine)+'|')
        Errors=[sum((S[C]-Fine[C])**2 for C in range(3))/3 for S in Samples]
        Blocks=[sum(Errors[128*B:128*(B+1)])/128 for B in range(8)];BlockErrors.append(Blocks)
        Rmse=math.sqrt(sum(Errors)/1024);Rmses.append(Rmse)
        Proposals=8 if Method<2 or Method>=4 else 4
        Calls=16 if Method>=4 else 8
        Table.append(f'|{Case}|{Methods[Method]}|{Proposals}|{Calls}|{Rmse:.9g}|PASS|')
        for Pixel in range(1024):
            Index=Method*1024+Pixel
            Work=struct.unpack_from('<4f',Raw,(18*6144+Index)*16)
            assert Work==(Calls/2,Calls/2,Proposals,0.)
            for Lane in range(4):
                Wires=[struct.unpack_from('<I3f',Raw,((2+4*Lane+P)*6144+Index)*16) for P in range(2)]
                Telemetry=struct.unpack_from('<4f',Raw,((5+4*Lane)*6144+Index)*16)
                Selected=int(Telemetry[2]);assert Selected in (0,1)
                for P,Wire in enumerate(Wires):
                    if P==1 and Method in (2,3):assert Wire==(0xffffffff,0.,0.,0.);continue
                    WireChecks+=1
                    assert (Wire[0]&0xffff)<10 and Wire[1]>=0 and Wire[2]>=0 and Wire[1]+Wire[2]<=1.000001 and Wire[3]>0
                    Traced=Method>=2 or P==Selected
                    assert bool(Wire[0]&(1<<18))==Traced
                    if not Traced:assert Wire[0]>>16==0
        if Method<2:
            Negative=[struct.unpack_from('<3f',Raw,(6144+Method*1024+P)*16) for P in range(1024)]
            NegativeMean=[sum(S[C] for S in Negative)/1024 for C in range(3)]
            NegativeSE=[math.sqrt(sum((S[C]-NegativeMean[C])**2 for S in Negative)/(1023*1024)) for C in range(3)]
            Failed=any(abs(A-B)>6*C+2*D+2e-5 for A,B,C,D in zip(NegativeMean,Fine,NegativeSE,Drift))
            NegativeFailures[Method]+=int(Failed)
        # Three layouts at receiver1 show six same-input estimator arms without pretending to be a camera image.
        if Case%3==1:
            Pixels=[tuple(max(0,min(255,round(A*64*255))) for A in Sample) for Sample in Samples]
            Panel=Image.new('RGB',(32,32));Panel.putdata(Pixels)
            File=Images/f'AreaReservoir-{Case}-{Method}.png';Panel.save(File)
            with Image.open(File) as Reopened:assert Reopened.tobytes()==bytes(A for P in Pixels for A in P)
            X=145+(Case//3)*365;Y=62+Method*151
            Canvas.paste(Panel.resize((120,120),Image.Resampling.NEAREST),(X,Y))
            Pen.text((X,Y-20),f'Case{Case} / layout{Case//3}',font=Small,fill='white')
            if Case==1:Pen.text((8,Y+44),Methods[Method],font=Small,fill='white')
    Metrics.append(Rmses)
    Ratios.append(f'|{Case}|'+ '|'.join(f'{Rmses[A]/Rmses[B]:.8g}' for A,B in ((0,2),(1,3),(0,4),(1,5)))+'|')
    for ProposalMeasure in range(2):
        Deltas=[A-B for A,B in zip(BlockErrors[ProposalMeasure],BlockErrors[ProposalMeasure+2])]
        Mean=sum(Deltas)/8;SE=math.sqrt(sum((A-Mean)**2 for A in Deltas)/(8*7))
        Intervals.append(f'|{Case}|{("uniform","area")[ProposalMeasure]}|{Mean:.9g}|{SE:.9g}|[{Mean-2.364624251*SE:.8g},{Mean+2.364624251*SE:.8g}]|')
assert MeanGates==162 and WireChecks==368640 and all(A>0 for A in NegativeFailures)
Canvas.save(Images/'AreaReservoirReadbacks.png')
Chart=Image.new('RGB',(1260,820),'#121923');Pen=ImageDraw.Draw(Chart)
Pen.text((16,12),'Actual continuous-area RGB RMSE / lower is better / RIS and raw4 equal query counts / raw8 double queries',font=Font,fill='white')
Colors=('#79d5ab','#e4ba7c','#8dc3f3','#c1a3e0','#5b869e','#8d769e')
for Case,Rmses in enumerate(Metrics):
    X=20+(Case%3)*415;Y=55+(Case//3)*225
    Pen.text((X,Y),f'Layout{Case//3},receiver{Case%3} / case{Case}',font=Font,fill='white')
    for Method in range(6):
        Pen.rectangle((X,Y+29+Method*27,X+Rmses[Method]/max(Rmses)*135,Y+43+Method*27),fill=Colors[Method])
        Pen.text((X+145,Y+29+Method*27),f'{Methods[Method]} {Rmses[Method]:.5g}',font=Small,fill=Colors[Method])
Pen.text((16,755),'Four fresh reservoirs select from two proposals using h/qArea;only selected source/connection queries are traced.',font=Small,fill='white')
Pen.text((16,790),'No temporal reuse,physical GPU timing or production promotion. Quadrature drift is empirical,not a certified bound.',font=Small,fill='white')
Chart.save(Images/'AreaReservoirComparison.png')
Passes=[all(R[F]<=R[F+2] for R in Metrics) for F in range(2)]
Wins=[sum(R[F]<=R[F+2] for R in Metrics) for F in range(2)]
Pooled=[math.sqrt(sum(R[M]**2 for R in Metrics)/9) for M in range(6)]
(Results/'AreaReservoir-comparison.md').write_text('# Fresh continuous-area measured comparison\n\n'+'\n'.join(Table)+'\n\n'+'\n'.join(Ratios)+'\n\n'+'\n'.join(Intervals)+'\n\nIntervals:8 paired seed blocks,Student t(df7);descriptive,not general independent-scene acceptance.\n')
(Results/'AreaReservoir-means.md').write_text('# Per-case empirical RGB mean gates\n\n'+'\n'.join(Means)+'\n')
Summary=(f'PASS fresh continuous-area RIS correctness:11 identical rows;runtime hashes/build={HashCounts};9 identical captures;2 identical modules.\n'
         f'Candidates/build=368640;traced paths=294912;binary visibility comparisons=589824;162 RGB mean gates;18 exact PNG arrays.\n'
         f'Max CPU contribution/normalizer errors={Maximum};negative variant failures={NegativeFailures}.\n'
         f'Previous raw8 overlapping18432 packet RGB comparisons max normalized difference={PreviousMaximum}.\n'
         f'Frozen equal-query quality wins uniform/area={Wins};all-nine variant gates={Passes};production_promote=0.\n'
         f'RMSEs by case={Metrics};exploratory equal-case pooled RMSEs={Pooled}.\n'
         'Methods=RIS-uniform,RIS-area,raw4-uniform,raw4-area,raw8-uniform,raw8-area. Calls/packet=8,8,8,8,16,16.\n'
         'Both builds pass five CTests and targeted runtime checks;host ASan/UBSan only,driver/SPIR-V uninstrumented.\n')
(Results/'AreaReservoir-parity.txt').write_text(Summary)
Files=(sorted(Results.glob('AreaReservoir*.gz'))+sorted(Images.glob('AreaReservoir*.png'))+
       [Results/f'AreaReservoir-{N}' for N in ('release.txt','sanitized.txt','comparison.md','means.md','parity.txt')]+[Path(__file__)])
(Results/'AreaReservoir-evidence-hashes.txt').write_text(''.join(hashlib.sha256(P.read_bytes()).hexdigest()+'  '+str(P.relative_to(Root))+'\n' for P in Files))
print(Summary,end='')
