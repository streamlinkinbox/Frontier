# 🧩 Verify unchanged default output allocation against all six retained perspective captures.
from pathlib import Path
import gzip,subprocess,os
R=Path(__file__).resolve().parents[3];B=R/'_ClaudeScratch/build/deformed-area-reservoir-release'
S=R/'_ClaudeScratch/perspective-regression-shaders';O=R/'_ClaudeScratch/perspective-regression-capture'
S.mkdir(parents=True,exist_ok=True);O.mkdir(parents=True,exist_ok=True)
for Stem in ('DiffuseGenerate','DiffuseResolve'):
 (S/f'{Stem}.spv').write_bytes(gzip.decompress((R/f'Exhibits/LightCompute/Results/PerspectiveAreaReservoir-{Stem}.spv.gz').read_bytes()))
Env=dict(os.environ)
Env['LD_LIBRARY_PATH']=str(R/'ExternalPackages/VulkanCompute/SwiftShader')+':'+Env.get('LD_LIBRARY_PATH','')
Env['VK_ICD_FILENAMES']=str(R/'ExternalPackages/VulkanCompute/SwiftShader/vk_swiftshader_icd.json')
p=subprocess.run([str(B/'LightComputeValidation'),str(R/'ExternalPackages/VulkanCompute/SwiftShader/libvulkan.so.1'),str(S),'--perspective-area-reservoir',str(O)],capture_output=True,text=True,env=Env)
T=p.stdout+p.stderr
assert p.returncode==0,T
for Stem in ('PerspectiveAreaReservoir','PerspectiveAreaReference'):
 for Case in range(3):
  assert (O/f'{Stem}{Case}.f32').read_bytes()==gzip.decompress((R/f'Exhibits/LightCompute/Results/{Stem}{Case}.f32.gz').read_bytes())
T+='PASS shared submission default32-slab regression:six perspective buffers byte-identical.\n'
(R/'Exhibits/LightCompute/Results/DeformedAreaReservoir-regression.txt').write_text(T)
print(T)
