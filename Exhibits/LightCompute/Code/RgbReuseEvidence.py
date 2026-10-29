# 🧩 Audit persistent RGB wire bytes,reset costs,paired errors and all retained Vulkan captures.
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
    Text=(Results/f'RgbReuse-{Mode}.txt').read_text()
    assert '100% tests passed, 0 tests failed out of 5' in Text
    assert not re.search(r'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer',Text)
    Semantic=[Line for Line in Text.splitlines() if Line.startswith('RGB_REUSE_')]
    assert len(Semantic)==578 and 'correctness=1 retained_checks=178176' in Semantic[-1]
    Rows.append(Semantic)
    Hashes=re.findall(r'^([a-f0-9]{64})  (.+)$',Text,re.M)
    for Digest,Name in Hashes:assert hashlib.sha256((Root/Name).read_bytes()).hexdigest()==Digest,Name
    HashCounts.append(len(Hashes))
assert Rows[0]==Rows[1]
for Stem in ('DiffuseGenerate','DiffuseResolve'):
    Modules=[Root/f'_ClaudeScratch/build/rgb-reuse-{Mode}/{Stem}.spv' for Mode in ('release','sanitized')]
    assert Modules[0].read_bytes()==Modules[1].read_bytes()
    (Results/f'RgbReuse-{Stem}.spv.gz').write_bytes(gzip.compress(Modules[0].read_bytes(),mtime=0))
Exact={};Work={};Maximum=[0.,0.]
for Line in Rows[0]:
    Fields=dict(re.findall(r'(\w+)=([^ ]+)',Line))
    if Line.startswith('RGB_REUSE_EXACT'):
        Exact[int(Fields['fixture']),int(Fields['step'])]=[float(Fields[f'channel{Channel}']) for Channel in range(3)]
    if Line.startswith('RGB_REUSE_WORK'):
        Work[int(Fields['fixture']),int(Fields['step']),int(Fields['method'])]=[int(Fields[Name]) for Name in ('draws','kept','evaluations')]
    if Line.startswith('RGB_REUSE_CASE'):
        for Index,Name in enumerate(('estimate_error','weight_error')):Maximum[Index]=max(Maximum[Index],float(Fields[Name]))
Methods=('rotate2','fresh-quartet','matched-raw-budget','raw8')
Names=('Stationary','Smooth','Receiver jumps')
Metrics=[];Decisions=[];MeanGates=0;Retained=0
Table=['|Track|Method|Draws|Current geometry evaluations|Birth-score evaluations*|Instant RGB RMSE|Rolling error RMSE|',
       '|---|---|---:|---:|---:|---:|---:|']
Means=['|Track|Step|Method|R mean|G mean|B mean|Largest error/SE|Gate|','|---|---:|---|---:|---:|---:|---:|---|']
Intervals=['|Track|Metric|Rotate minus matched-budget MSE|Block SE|95% paired interval|',
           '|---|---|---:|---:|---|']
Canvas=Image.new('RGB',(1270,665),'#121923');Pen=ImageDraw.Draw(Canvas)
Font,Small=ImageFont.load_default(size=15),ImageFont.load_default(size=12)
Pen.text((16,10),'Actual persistent RGB buffers / smooth track / logical packets,not camera pixels / supplied RGB,not traced GI',font=Font,fill='white')
Steps=(14,15,16,17,23,24)
for Fixture in range(3):
    Previous=None;History=[]
    Instant=[[[0.]*8 for Method in range(4)] for Metric in range(2)]
    DrawTotals=[0]*4;EvaluationTotals=[0]*4
    for Step in range(32):
        Paths=[Root/f'_ClaudeScratch/rgb-reuse-capture-{Mode}/RgbReuse{Fixture}-{Step}.f32' for Mode in ('release','sanitized')]
        Raw=Paths[0].read_bytes();assert Raw==Paths[1].read_bytes() and len(Raw)==19*4096*16
        File=Results/f'RgbReuse{Fixture}-{Step}.f32.gz';File.write_bytes(gzip.compress(Raw,mtime=0))
        assert gzip.decompress(File.read_bytes())==Raw
        Reset=Step in (0,16,24)
        if Reset:History=[]
        Current=[]
        for Method in range(4):
            Samples=[struct.unpack_from('<3f',Raw,(Method*1024+Pixel)*16) for Pixel in range(1024)]
            assert all(math.isfinite(Component) and Component>=0 for Sample in Samples for Component in Sample)
            Reference=Exact[Fixture,Step]
            Errors=[[Sample[Channel]-Reference[Channel] for Channel in range(3)] for Sample in Samples]
            Current.append(Errors)
            Mean=[sum(Sample[Channel] for Sample in Samples)/1024 for Channel in range(3)]
            SE=[math.sqrt(sum((Sample[Channel]-Mean[Channel])**2 for Sample in Samples)/(1023*1024)) for Channel in range(3)]
            assert all(abs(A-B)<=6*C+2e-5 for A,B,C in zip(Mean,Reference,SE)),(Fixture,Step,Method,Mean,Reference,SE)
            MeanGates+=3
            Standardized=max(abs(A-B)/C if C else (math.inf if A!=B else 0.) for A,B,C in zip(Mean,Reference,SE))
            Means.append(f'|{Names[Fixture]}|{Step}|{Methods[Method]}|'+ '|'.join(f'{A:.9g}' for A in Mean)+f'|{Standardized:.6g}|1|')
            Draws,Kept,Evaluations=Work[Fixture,Step,Method]
            assert Draws==1024*(8 if Reset or Method in (1,3) else 4)
            assert Kept==(2048 if Method==0 and not Reset else 0)
            DrawTotals[Method]+=Draws;EvaluationTotals[Method]+=Evaluations
            for Pixel in range(1024):
                Index=Method*1024+Pixel
                Telemetry=struct.unpack_from('<4f',Raw,(4096+Index)*16)
                assert Telemetry[0]==Draws/1024 and Telemetry[1]==Kept/1024 and Telemetry[3]==int(Reset)
                if Method<2:
                    for Lane in range(4):
                        Offset=((2+2*Lane)*4096+Index)*16
                        SecondOffset=Offset+4096*16
                        First=Raw[Offset:Offset+16];Second=Raw[SecondOffset:SecondOffset+16]
                        FacetIndex,Epoch=struct.unpack_from('<2I',First)
                        if FacetIndex!=0xffffffff:assert Epoch==(9 if Step>=24 else 8 if Step>=16 else 7)
                        if Method==0 and not Reset and Lane//2!=Step%2:
                            assert First==Previous[Offset:Offset+16] and Second==Previous[SecondOffset:SecondOffset+16]
                            Retained+=1
            if Fixture==1 and Step in Steps:
                Pixels=[tuple(max(0,min(255,round(Component*2*255))) for Component in Sample) for Sample in Samples]
                Panel=Image.new('RGB',(32,32));Panel.putdata(Pixels)
                File=Images/f'RgbReuse-{Step}-{Method}.png';Panel.save(File)
                with Image.open(File) as Reopened:assert Reopened.tobytes()==bytes(Component for Pixel in Pixels for Component in Pixel)
                Column=Steps.index(Step);X=135+Column*185;Y=62+Method*130
                Canvas.paste(Panel.resize((96,96),Image.Resampling.NEAREST),(X,Y))
                if Method==0:Pen.text((X,36),f'Step{Step}'+(' RESET' if Reset else ''),font=Small,fill='white')
                if Column==0:Pen.text((10,Y+36),Methods[Method],font=Small,fill='white')
        History.append(Current);History=History[-4:]
        for Method in range(4):
            for Pixel in range(1024):
                Block=Pixel//128
                Instant[0][Method][Block]+=sum(Component*Component for Component in Current[Method][Pixel])/(32*128*3)
                Instant[1][Method][Block]+=sum((sum(Past[Method][Pixel][Channel] for Past in History)/len(History))**2 for Channel in range(3))/(32*128*3)
        Previous=Raw
    BirthCounts=[EvaluationTotals[2],EvaluationTotals[3],0,0]
    Rmse=[[math.sqrt(sum(Instant[Metric][Method])/8) for Method in range(4)] for Metric in range(2)]
    Decision=Rmse[0][0]<=Rmse[0][2] and Rmse[1][0]<=Rmse[1][2] and DrawTotals[0]<DrawTotals[3]
    Decisions.append(Decision);Metrics.append((Rmse,DrawTotals,EvaluationTotals,BirthCounts))
    for Method in range(4):
        Table.append(f'|{Names[Fixture]}|{Methods[Method]}|{DrawTotals[Method]}|{EvaluationTotals[Method]}|{BirthCounts[Method]}|{Rmse[0][Method]:.9g}|{Rmse[1][Method]:.9g}|')
    for Metric,Label in enumerate(('instant','rolling')):
        Deltas=[A-B for A,B in zip(Instant[Metric][0],Instant[Metric][2])]
        Mean=sum(Deltas)/8;SE=math.sqrt(sum((A-Mean)**2 for A in Deltas)/(8*7))
        Intervals.append(f'|{Names[Fixture]}|{Label}|{Mean:.9g}|{SE:.9g}|[{Mean-2.364624251*SE:.9g},{Mean+2.364624251*SE:.9g}]|')
Pen.text((16,600),'Linear RGB exposure2,clipped. Step16 supplies RGBx2 with epoch reset;step24 translates geometry with epoch reset.',font=Small,fill='white')
Pen.text((16,631),'Top:rotate2 persistent quartet. Reset costs use eight proposals,not four. No denoising,TAA or camera reconstruction.',font=Small,fill='white')
Canvas.save(Images/'RgbReuseReadbacks.png')
assert MeanGates==1152 and Retained==178176
Canvas=Image.new('RGB',(1260,660),'#121923');Pen=ImageDraw.Draw(Canvas)
Colors=('#79d5ab','#e4ba7c','#8dc3f3','#b69cdd')
Pen.text((16,12),'Actual RGB instant / four-step rolling error RMSE / lower is better / no ray-query or physical-time claim',font=Font,fill='white')
for Fixture,(Rmse,Draws,Evaluations,Births) in enumerate(Metrics):
    X=20+Fixture*415
    Pen.text((X,48),Names[Fixture],font=Font,fill='white')
    for Metric,Label in enumerate(('Instant','Rolling error mean')):
        Y=90+Metric*185;Scale=max(Rmse[Metric])
        Pen.text((X,Y),Label,font=Small,fill='white')
        for Method in range(4):
            Width=Rmse[Metric][Method]/max(Scale,1e-20)*165
            Pen.rectangle((X,Y+28+Method*30,X+Width,Y+44+Method*30),fill=Colors[Method])
            Pen.text((X+175,Y+28+Method*30),f'{Methods[Method]} {Rmse[Metric][Method]:.5f}',font=Small,fill=Colors[Method])
    Pen.text((X,470),f'Frozen quality gate:{"PASS" if Decisions[Fixture] else "FAIL"}',font=Font,fill='white')
Pen.text((16,525),'Green:rotate2;gold:fresh quartet;blue:matched raw budget;purple:raw8. Cold/relight/geometry resets included.',font=Small,fill='white')
Pen.text((16,560),'Rotate2 and matched raw budget consume equal candidate draws;rotate2 draws45.3125% fewer than raw8.',font=Small,fill='white')
Pen.text((16,595),'Birth-target geometry evaluation overhead is counted separately in the table;fewer draws are not a speedup.',font=Small,fill='white')
Pen.text((16,630),'Rolling metric averages errors over up to four steps,restarting on resets. It is not TAA or camera-image quality.',font=Small,fill='white')
Canvas.save(Images/'RgbReuseComparison.png')
(Results/'RgbReuse-comparison.md').write_text('# Persistent RGB measured comparison\n\n'+'\n'.join(Table)+'\n\n*Birth-score counts derived from the identical matched raw proposal IDs,not an independent GPU timer.\n\n'+'\n'.join(Intervals)+'\n\nIntervals use8 paired seed blocks,Student t(df7);common seeds across tracks are not independent scenes.\n')
(Results/'RgbReuse-means.md').write_text('# Per-step RGB mean gates\n\n'+'\n'.join(Means)+'\n')
Summary=(f'PASS persistent implementation:578 identical semantic rows;hashes/build={HashCounts};96 byte-identical captures;two identical SPIR-V files.\n'
         f'786432 quartet descriptor comparisons/build;178176 exact retained-byte checks;1152 RGB mean gates;24 exact PNG arrays.\n'
         f'Max normalized CPU estimate/weight errors={Maximum};promotion_by_track={Decisions};production_promote=0.\n'
         f'Instant RMSEs by track={[A[0][0] for A in Metrics]};rolling RMSEs={[A[0][1] for A in Metrics]}.\n'
         'Methods=rotate2,fresh-quartet,matched-raw-budget,raw8;rotating draws/track=143360 vs raw8=262144.\n'
         'Both native runs exit0,five CTests each;host ASan+UBSan only. No traced RGB,visibility,camera/TAA or physical timing.\n')
(Results/'RgbReuse-parity.txt').write_text(Summary)
Files=sorted(Results.glob('RgbReuse*.gz'))+[Path(__file__)]
(Results/'RgbReuse-evidence-hashes.txt').write_text(''.join(hashlib.sha256(File.read_bytes()).hexdigest()+'  '+str(File.relative_to(Root))+'\n' for File in Files))
print(Summary,end='')
