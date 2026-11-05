#============================================================================================================================================
#                                                       RECOMPILESHADERARCHIVES.PY
#============================================================================================================================================
# 🧩 Compiles an archive set's Slang entry points with a slangc, exactly as its CMake file does, and compares every SPIR-V file with the
#    one stored in EngineContent/ShaderArchives. Without a CMake build this is how the stored archives are checked against their sources:
#    a stored archive that a compiler of the recorded version cannot reproduce byte for byte is a stale archive or a different compiler.
#
#        python Automation/RecompileShaderArchives.py --set ParticleSimulation --slangc <slangc> [--out <dir>] [--store]
#
#    --store writes the compiled files and the manifest through StoreShaderArchives.py's own routine, which is how a set is first stored.

from pathlib import Path
import argparse
import os
import re
import subprocess
import sys
import json
import hashlib

RepositoryRoot = Path(__file__).resolve().parents[1]
StoredRoot     = RepositoryRoot / 'EngineContent/ShaderArchives'

# 📝 Each set's CMake file, the folder of its units and the folders its includes resolve in (the -I flags of the CMake commands).
Recipes = {
    'ParticleSimulation': {
        'cmake':    'Automation/ParticleSimulationRendering.cmake',
        'source':   'Frontier/Engine/Shaders/ParticleSimulation',
        'includes': ['Frontier/Engine/Shaders/ParticleSimulation', 'Frontier/Engine/Shaders/GlobalIllumination'],
    },
    'XRayScan': {
        'cmake':    'Automation/XRayScanRendering.cmake',
        'source':   'Frontier/Engine/Shaders/XRayScan',
        'includes': ['Frontier/Engine/Shaders/XRayScan', 'Frontier/Engine/Shaders/GlobalIllumination'],
    },
    'PlumeSimulation': {
        'cmake':    'Automation/PlumeSimulationRendering.cmake',
        'source':   'Frontier/Engine/Shaders/PlumeSimulation',
        'includes': ['Frontier/Engine/Shaders/PlumeSimulation', 'Frontier/Engine/Shaders/GlobalIllumination'],
    },
}
EntryPattern = re.compile(r'"([A-Za-z0-9]+):([A-Za-z0-9]+):(compute|vertex|fragment)"')
SlangFlags   = ['-target', 'spirv', '-profile', 'spirv_1_3', '-emit-spirv-directly', '-matrix-layout-row-major', '-fvk-use-entrypoint-name', '-O0']


def Digest(Filename):
    return hashlib.sha256(Path(Filename).read_bytes()).hexdigest()


def Compile(Slangc, Recipe, Unit, Entry, Stage, Output):
    Source = RepositoryRoot / Recipe['source'] / f'{Unit}.slang'
    Command = [str(Slangc), str(Source)]
    for Include in Recipe['includes']:
        Command += ['-I', str(RepositoryRoot / Include)]
    Command += ['-entry', Entry, '-stage', Stage] + SlangFlags + ['-o', str(Output)]
    Result = subprocess.run(Command, capture_output=True, text=True, env={**os.environ, 'LD_LIBRARY_PATH': str(Path(Slangc).parent) + os.pathsep + os.environ.get('LD_LIBRARY_PATH', '')})
    return Result


def Main():
    Parser = argparse.ArgumentParser()
    Parser.add_argument('--set', required=True, choices=list(Recipes))
    Parser.add_argument('--slangc', required=True, type=Path)
    Parser.add_argument('--out', type=Path, default=None, help='where the SPIR-V is written (default: _ClaudeScratch/build/ShaderArchives/<set>)')
    Parser.add_argument('--store', action='store_true', help='store the compiled archives and manifest (StoreShaderArchives.py) instead of comparing')
    Arguments = Parser.parse_args()
    Recipe = Recipes[Arguments.set]
    Entries = EntryPattern.findall((RepositoryRoot / Recipe['cmake']).read_text(encoding='utf-8'))
    Out = Arguments.out or (RepositoryRoot / '_ClaudeScratch/build/ShaderArchives' / Arguments.set)
    Out.mkdir(parents=True, exist_ok=True)
    Version = subprocess.run([str(Arguments.slangc), "-version"], capture_output=True, text=True,
                             env={**os.environ, 'LD_LIBRARY_PATH': str(Arguments.slangc.parent)})
    Version = (Version.stdout + Version.stderr).strip()
    print(f'{Arguments.set}: {len(Entries)} entry points, slangc {Version}')
    Failures = 0
    for Unit, Entry, Stage in Entries:
        Output = Out / f'{Unit}.{Entry}.spv'
        Result = Compile(Arguments.slangc, Recipe, Unit, Entry, Stage, Output)
        if Result.returncode != 0 or not Output.is_file():
            Failures += 1
            print(f'[FAIL] {Unit}.{Entry} ({Stage}) did not compile\n{Result.stdout}{Result.stderr}')
    if Arguments.store:
        if Failures:
            sys.exit(1)
        sys.path.insert(0, str(Path(__file__).parent))
        import StoreShaderArchives
        StoreShaderArchives.Store(Arguments.set, Out.parent.parent, f'{Arguments.slangc}')
        return
    Manifest = json.loads((StoredRoot / Arguments.set / 'ShaderCompilation.json').read_text(encoding='utf-8'))
    print(f'stored by: {Manifest["compiler"]}')
    Stored = {Entry['file']: Entry['spirv_sha256'] for Entry in Manifest['entries']}
    Compiled = {Path(File).name: Digest(File) for File in Out.glob('*.spv')}
    Identical = 0
    for File in sorted(set(Stored) | set(Compiled)):
        if File not in Compiled:
            Failures += 1; print(f'[FAIL] stored but not produced: {File}')
        elif File not in Stored:
            Failures += 1; print(f'[FAIL] produced but not stored: {File}')
        elif Compiled[File] != Stored[File]:
            Failures += 1; print(f'[FAIL] differs: {File}')
        else:
            Identical += 1
    print(f'{Arguments.set}: {Identical} of {len(Stored)} stored SPIR-V files reproduced byte for byte, {Failures} failures')
    sys.exit(1 if Failures else 0)


if __name__ == '__main__':
    Main()
