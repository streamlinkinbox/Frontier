# 🧩 Verify actual RGB ray-query captures and produce labelled evidence,not camera-image mockups.
from pathlib import Path
import gzip
import hashlib
import re
import struct
from PIL import Image,ImageDraw,ImageFont

Root=Path(__file__).resolve().parents[3]
Results=Root/'Exhibits/LightCompute/Results';Images=Root/'Exhibits/LightCompute/Images'
Rows=[];HashCounts=[]
for Mode in ('release','sanitized'):
    Text=(Results/f'RgbTrace-{Mode}.txt').read_text()
    assert '100% tests passed, 0 tests failed out of 5' in Text
    assert not re.search(r'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer',Text)
    Semantic=[Line for Line in Text.splitlines() if Line.startswith('RGB_TRACE_')]
    assert len(Semantic)==8 and 'correctness=1' in Semantic[-1]
    Rows.append(Semantic)
    Hashes=re.findall(r'^([a-f0-9]{64})  (.+)$',Text,re.M)
    for Digest,Name in Hashes:assert hashlib.sha256((Root/Name).read_bytes()).hexdigest()==Digest,Name
    HashCounts.append(len(Hashes))
assert Rows[0]==Rows[1]
for Stem in ('DiffuseGenerate','DiffuseResolve'):
    Modules=[Root/f'_ClaudeScratch/build/rgb-trace-{Mode}/{Stem}.spv' for Mode in ('release','sanitized')]
    assert Modules[0].read_bytes()==Modules[1].read_bytes()
    (Results/f'RgbTrace-{Stem}.spv.gz').write_bytes(gzip.compress(Modules[0].read_bytes(),mtime=0))
Labels=('Open','Blocker A','Blocker B','Relight/material','Stale epoch','Rebuilt epoch')
Canvas=Image.new('RGB',(1170,580),'#121923');Pen=ImageDraw.Draw(Canvas)
Font,Small=ImageFont.load_default(size=15),ImageFont.load_default(size=12)
Pen.text((12,10),'Actual Vulkan ray-tested RGB descriptor buffers / logical cases,not camera pixels / single direct-light suffix',font=Font,fill='white')
Cases=[];NegativeTotal=0;Queries=[0]*4
Table=['|Phase|Valid|Suffix blocked|Receiver blocked|Omitted-visibility failures|Max normalized CPU error|',
       '|---|---:|---:|---:|---:|---:|']
for Phase in range(6):
    Paths=[Root/f'_ClaudeScratch/rgb-trace-capture-{Mode}/RgbTrace{Phase}.f32' for Mode in ('release','sanitized')]
    Raw=Paths[0].read_bytes();assert Raw==Paths[1].read_bytes() and len(Raw)==4096*9*16
    assert Raw[:4096*16]==Raw[4096*16:2*4096*16]
    File=Results/f'RgbTrace{Phase}.f32.gz';File.write_bytes(gzip.compress(Raw,mtime=0))
    assert gzip.decompress(File.read_bytes())==Raw
    Fields=dict(re.findall(r'(\w+)=([^ ]+)',Rows[0][Phase+1]))
    assert int(Fields['phase'])==Phase
    assert float(Fields['error'])<2e-5
    Counts=[0,0,0]
    for Pixel in range(4096):
        Query=struct.unpack_from('<4f',Raw,(8*4096+Pixel)*16)
        assert Query==((0.,)*4 if Phase==4 else (1.,)*4)
        Queries=[A+int(B) for A,B in zip(Queries,Query)]
        Cached=struct.unpack_from('<4f',Raw,Pixel*16)
        Source=struct.unpack_from('<4f',Raw,(6*4096+Pixel)*16)
        NoVisibility=struct.unpack_from('<4f',Raw,(2*4096+Pixel)*16)
        if Phase==4:
            assert Cached==(0.,)*4 and Source==(0.,)*4
            assert struct.unpack_from('<3f',Raw,(5*4096+Pixel)*16)==(9.,8.,7.)
        else:
            Counts[0]+=Source[3]==0
            Counts[1]+=Cached[3]==0
            Counts[2]+=Cached[3]==0 and Source[3]==1 and NoVisibility[3]>0 and NoVisibility[0]>1e-8
    assert Counts==[int(Fields[Name]) for Name in ('suffix_blocked','receiver_blocked','omitted_visibility_failures')]
    NegativeTotal+=Counts[2];Cases.append((Counts,float(Fields['error'])))
    Table.append(f'|{Labels[Phase]}|{Fields["valid"]}|'+ '|'.join(map(str,Counts))+f'|{Fields["error"]}|')
    X=15+Phase*193
    Pen.text((X,43),Labels[Phase],font=Small,fill='white')
    for Row,(Name,Slot) in enumerate((('Suffix',6),('Connected',0),('NoVisibility',2))):
        Pixels=[tuple(max(0,min(255,round(Component*(4 if Slot==6 else 16)*255))) for Component in struct.unpack_from('<3f',Raw,(Slot*4096+Pixel)*16)) for Pixel in range(4096)]
        Panel=Image.new('RGB',(64,64));Panel.putdata(Pixels)
        File=Images/f'RgbTrace-{Phase}-{Name}.png';Panel.save(File)
        with Image.open(File) as Reopened:assert Reopened.tobytes()==bytes(Component for Pixel in Pixels for Component in Pixel)
        Canvas.paste(Panel.resize((128,128),Image.Resampling.NEAREST),(X,67+Row*145))
Pen.text((12,510),'Rows:ray-tested RGB suffix(exposure4);cached connected RGB(exposure16);incorrect visibility omission(exposure16).',font=Small,fill='white')
Pen.text((12,543),'Stale epoch performs zero queries and contributes zero. Each64x64 panel is a descriptor test array,not a GI image.',font=Small,fill='white')
Canvas.save(Images/'RgbTraceReadbacks.png')
assert NegativeTotal>0 and Queries==[20480]*4
Canvas=Image.new('RGB',(1170,500),'#121923');Pen=ImageDraw.Draw(Canvas)
Pen.text((12,12),'Actual binary shadow/reconnection checks / omitted visibility is incorrect,not a speed optimization',font=Font,fill='white')
Colors=('#a59bdd','#8ac6ef','#e98b8b')
for Phase,(Counts,Error) in enumerate(Cases):
    X=15+Phase*193;Pen.text((X,57),Labels[Phase],font=Small,fill='white')
    for Index,(Count,Label) in enumerate(zip(Counts,('Suffix blocked','Receiver blocked','Wrong omit-V'))):
        Y=100+Index*85
        Pen.text((X,Y),f'{Label}:{Count}',font=Small,fill=Colors[Index])
        if Count:Pen.rectangle((X,Y+24,X+Count/4096*165,Y+40),fill=Colors[Index])
Pen.text((12,382),f'{NegativeTotal} lit blocked cases expose the visibility-omission error. Cached and inline estimates match bitwise.',font=Small,fill='white')
Pen.text((12,419),'Per valid descriptor:cold cache1 suffix+1 segment query;inline1 suffix+1 segment query. No measured warm reuse.',font=Small,fill='white')
Pen.text((12,460),'Software BVH queries on SwiftShader;not hardware RT or physical timing. Prior persistent rolling-quality rejection remains.',font=Small,fill='white')
Canvas.save(Images/'RgbTraceComparison.png')
(Results/'RgbTrace-comparison.md').write_text('# Ray-tested RGB comparison\n\n'+'\n'.join(Table)+'\n')
Summary=(f'PASS RGB suffix/reconnection:8 identical semantic rows;hashes/build={HashCounts};6 identical captures;2 identical SPIR-V modules.\n'
         f'20480 valid and4096 stale descriptors/build;40960 CPU binary decisions;18 exact PNG arrays.\n'
         f'Query totals[cache suffix,inline suffix,cached reconnect,inline reconnect]={Queries};omitted visibility failures={NegativeTotal}.\n'
         f'Max normalized CPU RGB error={max(Case[1] for Case in Cases):.12g};cached/inline bitwise equal in all cases.\n'
         'Both native runs exit0,five CTests each;host ASan+UBSan only. One direct-light suffix,not multibounce GI or quality promotion.\n')
(Results/'RgbTrace-parity.txt').write_text(Summary)
Files=sorted(Results.glob('RgbTrace*.gz'))+[Path(__file__)]
(Results/'RgbTrace-evidence-hashes.txt').write_text(''.join(hashlib.sha256(File.read_bytes()).hexdigest()+'  '+str(File.relative_to(Root))+'\n' for File in Files))
print(Summary,end='')
