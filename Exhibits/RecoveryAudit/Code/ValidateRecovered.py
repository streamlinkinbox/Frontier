from pathlib import Path
import re,subprocess,json
Root=Path.cwd(); Results=[]
for Exhibit in ['IlluminationChain','IndirectIllumination','EnvironmentPipeline']:
 Directory=Root/'Exhibits'/Exhibit
 Specification=(Directory/'CMakeLists.txt').read_text()
 Match=re.search(r'add_executable\((\w+)\s+(.*?)\)',Specification,re.S)
 Name,SourceText=Match.groups()
 Sources=[]
 for Token in SourceText.split():
  Pathname=Path(Token.replace('${FRONTIER_ROOT}',str(Root/'Frontier')))
  Sources.append(str(Pathname if Pathname.is_absolute() else Directory/Pathname))
 Executable=Root/'_ClaudeScratch'/'recovery'/Name
 Command=['c++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-pthread','-I'+str(Root/'Frontier'),*Sources,'-o',str(Executable)]
 Compilation=subprocess.run(Command,text=True,capture_output=True)
 (Executable.with_suffix('.build.txt')).write_text(Compilation.stdout+Compilation.stderr)
 if Compilation.returncode:raise RuntimeError(Exhibit+' compilation failed: '+Compilation.stderr)
 Completion=subprocess.run([str(Executable)],text=True,capture_output=True)
 (Executable.with_suffix('.results.txt')).write_text(Completion.stdout+Completion.stderr)
 Results.append({'exhibit':Exhibit,'build_exit':Compilation.returncode,'test_exit':Completion.returncode,'command':Command,'output':Completion.stdout+Completion.stderr})
 print(Exhibit,'exit',Completion.returncode,Completion.stdout[-250:])
 if Completion.returncode:raise RuntimeError(Exhibit+' checks failed')
Path('_ClaudeScratch/recovery/Validation.json').write_text(json.dumps(Results,indent=2)+'\n')
