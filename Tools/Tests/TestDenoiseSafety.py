#!/usr/bin/env python3
"""CPU arithmetic, adversarial history model, source/ABI guards; NOT GPU execution."""
from pathlib import Path
import csv, itertools, random, re, subprocess
R=Path(__file__).resolve().parents[2]
B=R/'build/denoise-safety'; B.mkdir(parents=True,exist_ok=True)
s=(R/'Engine/Shaders/ReSTIRViewport.slang').read_text()
h=(R/'Engine/DeviceExchange/SwapchainExchange.cpp').read_text()
expr=re.search(r'vec3\s+demodAlbedo\s*=\s*(.*);',s).group(1)
cpp=r'''
#include "DenoiseCpuShim.h"
#include "PresentationDither.slang"
#include "TelemetryProbe.h"
#include "VisibilityExchange.h"
#include <cassert>
#include <cmath>
#include <cstdio>
inline vec3 floor(vec3 x){return vec3(std::floor(x.x),std::floor(x.y),std::floor(x.z));}
vec3 Quantize(vec3 albedo){return EXPRESSION;}
float Store8(float v){return std::floor(clamp(v,0.f,1.f)*255.f+.5f)/255.f;}
int main(int argc,char** argv){
 unsigned checks=0;
 for(int i=0;i<=4096;++i){
  vec3 a(float(i)/4096,0.f,1.f-float(i)/4096);
  vec3 q=Quantize(a);
  for(float v:{q.x,q.y,q.z}){
   assert(v>0&&v<=1&&Store8(v)==v);++checks;
   for(float light:{0.f,.001f,.1f,1.f,30.f,1000.f}){
    assert(std::abs((light/v)*Store8(v)-light)<=1.e-6f*std::max(1.f,light));++checks;
   }
  }
 }
 for(float a:{-1.f,0.f,.019f,.02f,.021f,1.f,2.f}){
  vec3 q=Quantize(vec3(a));assert(Store8(q.x)==q.x&&q.x>0&&q.x<=1);++checks;
 }
 double sum=0;
 for(int y=0;y<128;++y)for(int x=0;x<128;++x){
  float d=PresentationDither(ivec2(x,y));assert(std::abs(d)<=.5f/255.f+1.e-9f);
  assert(d==PresentationDither(ivec2(x,y)));sum+=d;++checks;
 }
 assert(std::abs(sum/(128*128))<.00005);++checks;
 // Real CSV exporter, including the invalid-frame zeroing path.
 auto& p=Frontier::TelemetryProbe::Access();p.MarkBoot();
 Frontier::VisibilityTelemetry t{};t.Valid=true;t.HistorySnapshotMilliseconds=.125f;
 for(int i=0;i<5;++i)t.DenoiseLevelMilliseconds[i]=float(i+1)*.1f;
 p.BeginFrame(.016f);p.EndFrame(t,60,100);t.Valid=false;
 p.BeginFrame(.016f);p.EndFrame(t,60,100);p.SaveReport(argv[1]);
 std::printf("PASS %u actual shader-expression/dither arithmetic checks (CPU, not Vulkan)\n",checks);
}
'''.replace('EXPRESSION',expr)
(B/'proof.cpp').write_text(cpp)
includes=['Engine/Shaders','Exhibits/Workbench/Materials','Engine/DeviceExchange']
cmd=['g++','-std=c++20','-O2','-DFRONTIER_DEVELOPMENT','-fsanitize=address,undefined','-fno-omit-frame-pointer',*[f'-I{R/i}' for i in includes],str(B/'proof.cpp'),str(R/'Engine/DeviceExchange/TelemetryProbe.cpp'),'-o',str(B/'proof')]
subprocess.run(cmd,check=True)
subprocess.run([str(B/'proof'),str(B)],check=True)
rows=list(csv.DictReader((B/'ProjectZero_TelemetryProbe_Frames.csv').open()))
assert len(rows)==2 and None not in rows[0]
for i in range(5):
 assert abs(float(rows[0][f'GpuDenoiseL{i}Ms'])-(i+1)*.1)<1e-6
 assert float(rows[1][f'GpuDenoiseL{i}Ms'])==0
assert float(rows[0]['GpuHistorySnapshotMs'])==.125
assert float(rows[1]['GpuHistorySnapshotMs'])==0
print('PASS real telemetry CSV round-trip / invalid-frame zeroing')
# Guard the actual resource wiring, not merely the model below.
for name,pair,binding in [('HistoryImage','HistoryPair',3),('HistorySurfaceImage','HistorySurfacePair',18),('MomentImage','MomentPair',19)]:
 assert f'image2D {pair}[2];' in s
 assert f'#define Previous{name} {pair}[1]' in s
 assert f'#define {name} {pair}[0]' in s
 assert f'imageLoad({name},' not in s
 assert f'imageLoad(Previous{name},' in s and f'imageStore({name},' in s
 assert f'imageStore(Previous{name},' not in s
assert 'VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) == 10u' in h
assert 'LayoutBindings[3].descriptorCount = 2u' in h
assert '(B==18u || B==19u) ? 2u : 1u' in h
assert 'Write.pImageInfo      = HistoryInfo;' in h
assert 'WriteImage (18u, HistorySurfaceInfo[0])' in h and 'WriteImage (19u, MomentInfo[0])' in h
assert 'Copy.extent={RenderWidth,RenderHeight,1}' in h
assert 'VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT' in h
assert 'VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT' in h
record=h[h.index('void SwapchainExchange::RecordComputeCommands'):]
assert record.index('vkCmdCopyImage(')<record.index('vkCmdDispatch(')
assert 'if(LiveDispatch.AccumulationIndex > 0u)' in record
assert 'Vulkan->HistoryWidth!=RenderWidth || Vulkan->HistoryHeight!=RenderHeight' in record
assert 'ShadowStageRecorded) Vulkan->HistoryContentsValid=false' in record
for shader in ['ReSTIRViewport','AtrousDenoise','ShadowResolve']:
 text=(R/f'Engine/Shaders/{shader}.slang').read_text()
 assert '#include "PresentationDither.slang"' in text
 assert 'PresentationDither(' in text
print('PASS snapshot ownership/copy/reset/descriptor and shared-dither source guards')
# Explicit concurrency MODEL: each tuple represents aligned mean/surface/moments.
# A ring reprojection deliberately creates read-after-write dependencies.
old=[(float(i),100+i,200+i) for i in range(8)]
def evaluate(order,snapshot):
 current=list(old); previous=list(old) if snapshot else current
 for i in order:
  a,b,c=previous[(i+1)%8];current[i]=(a+.25,b,c)
 return current
expected=evaluate(range(8),True)
rng=random.Random(71); unsafe=False
for _ in range(200):
 order=list(range(8));rng.shuffle(order)
 assert evaluate(order,True)==expected
 unsafe |= evaluate(order,False)!=expected
assert unsafe
print('PASS 200 adversarial invocation-order history models; unsafe in-place control detected')
print('PASS denoise safety suite; Vulkan dispatch/validation/performance still required')
