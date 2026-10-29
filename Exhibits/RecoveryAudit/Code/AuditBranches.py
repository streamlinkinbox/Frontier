import concurrent.futures,json,subprocess
from pathlib import Path
Markers={'VulkanTriangleSpace.h','VehicleCapture.cpp','VulkanFixture.h','RadianceReservoir.h','DistanceFieldSpace.slang','VehicleInlineFresh.slang','VulkanDeviceOrdinal.h','DeviceSweep.py','RendererProgress.md','EnvironmentTodo.md','VarianceFiltering.cpp','IlluminationUnits.cpp','AtmosphereModel.cpp','CloudField.cpp','VulkanDispatchSequence.h','ClusterSurface.cpp','CardRadiance.h','DiscrepancySequence.cpp'}
def Api(Route):
 P=subprocess.run(['gh','api',Route],text=True,capture_output=True)
 if P.returncode:raise RuntimeError(Route+': '+P.stderr)
 return json.loads(P.stdout)
Branches=[]
for Repository in ['SultanAladin/Frontier','eosclient0001-rgb/Frontier']:
 for Page in range(1,10):
  Batch=Api(f'repos/{Repository}/branches?per_page=100&page={Page}')
  Branches += [{'repository':Repository,'branch':B['name'],'sha':B['commit']['sha']} for B in Batch]
  if len(Batch)<100:break
Unique={}
for B in Branches:Unique.setdefault((B['repository'],B['sha']),[]).append(B['branch'])
def Inspect(Key):
 Repository,Sha=Key
 Tree=Api(f'repos/{Repository}/git/trees/{Sha}?recursive=1')
 Paths=[P['path'] for P in Tree.get('tree',[]) if P['type']=='blob']
 return {'repository':Repository,'sha':Sha,'branches':Unique[Key],'truncated':Tree.get('truncated'),
         'files':len(Paths),'matches':[P for P in Paths if P.split('/')[-1] in Markers]}
with concurrent.futures.ThreadPoolExecutor(max_workers=6) as Pool:Results=list(Pool.map(Inspect,Unique))
Report={'branch_count':len(Branches),'unique_repository_revisions':len(Results),'markers':sorted(Markers),'results':Results}
Path('_ClaudeScratch/recovery/BranchAudit.json').write_text(json.dumps(Report,indent=2)+'\n')
print('Branches inspected:',len(Branches),'unique repository revisions:',len(Results))
for R in Results:
 if R['matches'] or R['truncated']:print(json.dumps(R))
print('All recursive trees complete:',not any(R['truncated'] for R in Results))
