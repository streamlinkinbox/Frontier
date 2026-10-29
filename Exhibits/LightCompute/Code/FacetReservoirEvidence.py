# 🧩 Verify actual descriptor bytes, typed SPIR-V stride, RGB support moments and honest layout comparisons.
from pathlib import Path
import gzip
import hashlib
import os
import re
import struct
import subprocess
from PIL import Image,ImageDraw,ImageFont
from FacetReservoirFinite import ValidateFacetSupport

Root=Path(__file__).resolve().parents[3]
Results=Root/'Exhibits/LightCompute/Results'
Images=Root/'Exhibits/LightCompute/Images'
Build=Root/'_ClaudeScratch/build/facet-layout'
Build.mkdir(parents=True,exist_ok=True);Images.mkdir(exist_ok=True)
Rows,Counts=[],[]
for Mode in ('release','sanitized'):
    Text=(Results/f'FacetReservoir-{Mode}.txt').read_text()
    assert '100% tests passed, 0 tests failed out of 5' in Text
    assert not re.search(r'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer',Text)
    Semantic=[L for L in Text.splitlines() if L.startswith('FACET_RESERVOIR_')]
    assert Semantic[-1]=='FACET_RESERVOIR_DECISION descriptor=1 production_promote=0 renderer_quality=0'
    assert len(Semantic)==8
    Hashes=re.findall(r'^([a-f0-9]{64})  (.+)$',Text,re.M)
    for Digest,Name in Hashes:assert hashlib.sha256((Root/Name).read_bytes()).hexdigest()==Digest,Name
    Rows.append(Semantic);Counts.append(len(Hashes))
assert Rows[0]==Rows[1]
# Compile and run the native sizeof/offsetof reference independently of the Vulkan host.
Layouts=[]
for Mode in ('release','sanitized'):
    Binary=Build/Mode
    Flags=['-O2'] if Mode=='release' else ['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']
    subprocess.run([os.environ.get('CXX','g++'),'-std=c++20','-Wall','-Wextra','-Werror',*Flags,
                    str(Root/'Exhibits/LightCompute/Code/FacetReservoirLayout.cpp'),'-o',str(Binary)],check=True)
    Text=subprocess.check_output([str(Binary)],text=True)
    (Results/f'FacetReservoir-layout-{Mode}.txt').write_text(Text)
    Layouts.append(Text)
assert Layouts[0]==Layouts[1]
assert 'descriptor=32 expanded_sample=48 quartet=144 expanded_quartet=208 raw_octet=272 revision_offset=128' in Layouts[0]
Compiler=Root/'ExternalPackages/VulkanCompute/Slang/slangtorch/bin/slangc'
Probe=Root/'Exhibits/LightCompute/Code/FacetReservoirStride.slang'
Module=Build/'stride.spv'
subprocess.run([str(Compiler),str(Probe),'-I',str(Probe.parent),'-entry','InspectDescriptorStride','-target','spirv',
                '-profile','spirv_1_3','-emit-spirv-directly','-O0','-o',str(Module)],check=True)
Words=struct.unpack('<'+'I'*(len(Module.read_bytes())//4),Module.read_bytes())
assert Words[0]==0x07230203
Offsets,Strides,Arrays={}, {}, {}
Index=5
while Index<len(Words):
    Length,Opcode=Words[Index]>>16,Words[Index]&65535
    assert Length>0
    A=Words[Index+1:Index+Length]
    if Opcode==72 and len(A)==4 and A[2]==35:Offsets.setdefault(A[0],{})[A[1]]=A[3]
    if Opcode==71 and len(A)==3 and A[1]==6:Strides[A[0]]=A[2]
    if Opcode==29:Arrays[A[0]]=A[1]
    Index+=Length
assert any(Strides.get(A)==32 and Offsets.get(T)=={0:0,1:4,2:8,3:12,4:16,5:28} for A,T in Arrays.items())
Checks,Maximum,Failures,Finite=ValidateFacetSupport()
# Inspect all six actual captures,not just successful descriptor cases.
Canvas=Image.new('RGB',(1140,445),'#121923');Pen=ImageDraw.Draw(Canvas)
Font,Small=ImageFont.load_default(size=14),ImageFont.load_default(size=12)
Pen.text((16,12),'Actual Vulkan descriptor buffers / supplied cached RGB x geometry / NOT a GI render or camera image',font=Font,fill='white')
Labels=('front receiver','back receiver','lateral receiver','oblique receiver','stale epoch','rebuilt epoch/back')
Totals=[0,0,0];Maxima=[0.,0.,0.]
for Phase in range(6):
    Paths=[Root/f'_ClaudeScratch/facet-reservoir-capture-{Mode}/FacetReservoir{Phase}.f32' for Mode in ('release','sanitized')]
    Raw=Paths[0].read_bytes();assert Raw==Paths[1].read_bytes() and len(Raw)==4096*6*16
    RetainedBytes=gzip.compress(Raw,mtime=0)
    Retained=Results/f'FacetReservoir{Phase}.f32.gz'
    Retained.write_bytes(RetainedBytes)
    assert gzip.decompress(Retained.read_bytes())==Raw
    F=dict(re.findall(r'(\w+)=([^ ]+)',Rows[0][Phase+1]))
    assert F['phase']==str(Phase)
    for K,Name in enumerate(('position_error','normal_error','estimate_error')):
        Maxima[K]=max(Maxima[K],float(F[Name]));assert float(F[Name])<2e-5
    Local=[0,0,0];Pixels=[];Normals=[]
    for I in range(4096):
        E=struct.unpack_from('<4f',Raw,I*16)
        Expected=0 if I%32==0 else 1 if I%32>=14 and Phase!=4 else 2
        assert E[3]==Expected and all(0<=X and X<float('inf') for X in E)
        Local[Expected]+=1
        if Expected!=1:assert E[:3]==(0.,0.,0.)
        N=struct.unpack_from('<4f',Raw,(2*4096+I)*16)
        H=struct.unpack_from('<4f',Raw,(3*4096+I)*16)
        if Expected==1:assert H[0]>=1/64 and abs(H[0]-H[1]-1/64)<2e-5
        Pixels.append(tuple(min(255,max(0,round(X*16*255))) for X in E[:3]))
        Normals.append(tuple(min(255,max(0,round((X*.5+.5)*255))) for X in N[:3]) if Expected==1 else (0,0,0))
    assert Local==[int(F['empty']),int(F['valid']),int(F['rejected'])]
    Totals=[A+B for A,B in zip(Totals,Local)]
    X=20+Phase*185
    Pen.text((X,45),Labels[Phase],font=Small,fill='white')
    for Row,(Name,Array) in enumerate((('RGB',Pixels),('Normal',Normals))):
        Panel=Image.new('RGB',(64,64));Panel.putdata(Array)
        File=Images/f'FacetReservoir{Phase}-{Name}.png';Panel.save(File)
        with Image.open(File) as Retained:assert Retained.tobytes()==bytes(C for P in Array for C in P)
        Canvas.paste(Panel.resize((128,128),Image.Resampling.NEAREST),(X,68+Row*155))
    Pen.text((X,365),f'valid {Local[1]} / reject {Local[2]}',font=Small,fill='white')
Pen.text((16,400),'Top:linear RGB exposure16,clipped. Bottom:interpolated intrinsic normal encoded RGB. 64x64 logical cases.',font=Small,fill='white')
Pen.text((16,423),'Phase4 rejects stale geometry revision;phase5 rebuilds descriptors but the receiver is behind the facets.',font=Small,fill='white')
Canvas.save(Images/'FacetReservoirReadbacks.png')
Canvas=Image.new('RGB',(1100,530),'#121923');Pen=ImageDraw.Draw(Canvas)
Pen.text((18,14),'Unpacked logical packet comparison + exact finite RGB support tests / no allocated VRAM or quality claim',font=Font,fill='white')
for Row,(Label,Bytes,Color) in enumerate((('Scalar quartet only',80,'#8894a5'),('RGB facet quartet',144,'#77d3a6'),('Expanded RGB quartet',208,'#e6bd77'),('Raw RGB octet',272,'#b29dde'))):
    Y=80+Row*68;Pen.text((20,Y),Label,font=Small,fill=Color)
    Pen.rectangle((220,Y,220+Bytes*1.0,Y+24),fill=Color);Pen.text((505,Y),f'{Bytes} B',font=Small,fill='white')
Pen.text((600,60),'Detected biased receiver-pair tests /16',font=Font,fill='white')
for Row,(Label,N) in enumerate(zip(('Zero birth geometry','Valid-count divisor','Current-h denominator'),Failures)):
    Y=110+Row*85;Pen.text((600,Y),Label,font=Small,fill='white')
    Pen.rectangle((600,Y+23,600+N*25,Y+43),fill='#e38a88');Pen.text((1010,Y+23),str(N),font=Small,fill='white')
Pen.text((18,385),'Scalar80B cannot encode general facet identity,barycentrics and cached RGB. RGB descriptor32B;typed SPIR-V stride32.',font=Small,fill='white')
Pen.text((18,420),f'Positive floor1/64:all16 receiver-pair RGB means agree;max error {Maximum:.3g}. Zero scores lose future support.',font=Small,fill='white')
Pen.text((18,460),'The floor is a correctness prerequisite here,not a validated low-variance choice for a general renderer.',font=Small,fill='white')
Pen.text((18,495),'Production348.046875MiB/defaults unchanged. Descriptor GPU test uses two float4 wire slots;typed stride checked in SPIR-V.',font=Small,fill='white')
Canvas.save(Images/'FacetReservoirComparison.png')
# Retain every supplementary source/module digest as well as the hashes from both native runs.
Files=[Root/'Exhibits/LightCompute/Code'/N for N in ('FacetReservoirDescriptor.h','FacetReservoirGpu.slang',
       'FacetReservoirValidation.h','FacetReservoirLayout.cpp','FacetReservoirStride.slang','FacetReservoirFinite.py','FacetReservoirEvidence.py')]
Files += [Module,Build/'release',Build/'sanitized']
Files += sorted(Results.glob('FacetReservoir?.f32.gz'))
(Results/'FacetReservoirStride.spv').write_bytes(Module.read_bytes())
(Results/'FacetReservoir-finite.txt').write_text(
    f'checks={Checks} max_error={Maximum:.12g} negative_failures={Failures}\n'+
    '\n'.join(repr(Row) for Row in Finite)+'\n')
Files += [Results/'FacetReservoirStride.spv',Results/'FacetReservoir-finite.txt']
DigestText=''.join(hashlib.sha256(P.read_bytes()).hexdigest()+'  '+str(P.relative_to(Root))+'\n' for P in Files)
(Results/'FacetReservoir-supplementary-hashes.txt').write_text(DigestText)
Message=(f'PASS descriptor/support gate:8 identical semantic rows;hashes/build={Counts};6 byte-identical captures.\n'
         f'24576 descriptor roundtrips:empty/valid/rejected={Totals};12 exact PNG arrays;max position/normal/estimate={Maxima}.\n'
         f'Native C++ descriptor32/quartet144/expanded208/raw272;SPIR-V stride32,member offsets0/4/8/12/16/28.\n'
         f'RGB finite checks={Checks};error={Maximum:.12g};negative failures/16={Failures}.\n'
         'Both native runs exit0,five CTests each;ABI checks also run release/ASan+UBSan.\n'
         'No integrated RGB reservoir sampling,new GI quality,ray-traversal or production allocation claim.\n')
(Results/'FacetReservoir-parity.txt').write_text(Message)
print(Message,end='')
