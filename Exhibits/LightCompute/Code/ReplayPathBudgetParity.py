#============================================================================================================================================
#                                                   REPLAYPATHBUDGETPARITY.PY
#============================================================================================================================================
# 🧩 Verify final result rows, source digests, default shader digests and actual decoded Vulkan readbacks.

from pathlib import Path
import re,hashlib,math
from PIL import Image
Logs=[Path('Exhibits/LightCompute/Results/ReplayPathBudget-'+Mode+'.txt').read_text() for Mode in ['release','sanitized']]
Rows=[]
for Text in Logs:
    assert 'PASS eight-current-path comparison' in Text and '100% tests passed out of 5' in Text
    Rows.append([Line for Line in Text.splitlines() if Line.startswith('BUDGET_') and not Line.startswith('BUDGET_PROGRESS')])
assert Rows[0]==Rows[1]
Checks=[]
for Mode,Text in zip(['release','sanitized'],Logs):
    Lines=[Line for Line in Text.splitlines() if re.match(r'^[0-9a-f]{64}  ',Line)]
    for Line in Lines:
        Digest,Name=Line.split('  ',1)
        assert hashlib.sha256(Path(Name).read_bytes()).hexdigest()==Digest,Name
    Checks.append(f'{Mode}: {len(Lines)} source/tool/SPIR-V hashes match final files')
Checks.append(f'{len(Rows[0])} BUDGET rows exact between release and sanitizer')
PriorText=Path('Exhibits/LightCompute/Results/ReplayCompactStorage-release.txt').read_text()
PriorHashes={Name:Digest for Digest,Name in
             (Line.split('  ',1) for Line in PriorText.splitlines() if re.match(r'^[0-9a-f]{64}  ',Line))}
for Stem in ['Primary','ReplayGenerate','ReplayResolve','ReplayGatherSampleHistory','ReplayFinalizeSampleHistory']:
    Current=Path('_ClaudeScratch/build/replay-path-budget-release/plain')/(Stem+'.spv')
    Digests=[Digest for Name,Digest in PriorHashes.items() if Path(Name).name==Stem+'.spv']
    assert len(Digests)==1 and hashlib.sha256(Current.read_bytes()).hexdigest()==Digests[0]
    Checks.append(f'{Stem}: default SPIR-V SHA256 matches completed compact phase')
for Name in ['Frontier/Engine/Rendering/PathReplayVulkan.h','Frontier/Engine/Rendering/PathReplayVulkan.cpp']:
    assert hashlib.sha256(Path(Name).read_bytes()).hexdigest()==PriorHashes[Name]
    Checks.append(Name+': footprint/recorder source unchanged from compact phase')
for Name in ['BudgetFreshCosine','BudgetReuseCosine','BudgetMaskCosine','BudgetFreshFacet','BudgetReuseFacet','BudgetMaskFacet']:
    Images=[]
    for Mode in ['release','sanitized']:
        with Image.open(Path('_ClaudeScratch/replay-path-budget-capture-'+Mode+'/png')/(Name+'.png')) as Capture:
            Images.append(Capture.convert('RGB').tobytes())
    assert Images[0]==Images[1]
    Checks.append(f'{Name}: decoded RGB exact '+hashlib.sha256(Images[0]).hexdigest())
Checks.append('PASS final source hashes, all logged budget results, six captures and default shader parity')
Path('Exhibits/LightCompute/Results/ReplayPathBudget-parity.txt').write_text('\n'.join(Checks)+'\n')
print('\n'.join(Checks[:9]))
for Bounces,Facets in [(1,0),(1,1),(4,0),(4,1)]:
    Errors=[]
    for Mode in ['fresh_eight','spatial_seed_ris']:
        Line=next(Line for Line in Logs[0].splitlines() if Line.startswith(f'BUDGET_RESULT bounces={Bounces} facets={Facets} mode={Mode} '))
        Errors.append(float(re.search(r' rmse=([^ ]+)',Line)[1]))
    print(Bounces,Facets,'rmse change percent',100*(Errors[1]/Errors[0]-1))
print('Point reference RMS SE',.000360448503*math.sqrt(702))
