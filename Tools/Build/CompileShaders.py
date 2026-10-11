#!/usr/bin/env python3
"""Lower every Engine/Shaders entry to Vulkan SPIR-V, away from the machine that has to play the game.

The shader table is **not** written out again here. It is read from ``CMakeLists.txt``, which already
holds it, and checked against the copy in ``Tools/Build/ToolchainSequence.ps1`` so the two cannot
drift apart unnoticed. Adding a shader stays a one-line change in each of those two files.

    python3 Tools/Build/CompileShaders.py                 # compile everything that is out of date
    python3 Tools/Build/CompileShaders.py --all           # compile everything regardless
    python3 Tools/Build/CompileShaders.py --check         # fail if any .spv is missing or stale
    python3 Tools/Build/CompileShaders.py --compiler PATH # name the compiler instead of searching

Three compilers are understood, preferred in this order: ``glslc`` (Vulkan SDK / shaderc), ``slangc``
(the Slang release), and ``glslangValidator`` (the ``glslang-tools`` package). The sources are GLSL
living under a ``.slang`` name, which is why each needs its stage spelled out.

Alongside the ``.spv`` files it writes ``Engine/Shaders/ShaderManifest.json``: the compiler and its
version, and the SHA-256 of every source and every output. That is what makes a downloaded shader
package checkable rather than merely trusted.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

Root = Path(__file__).resolve().parents[2]
ShaderRoot = Root / 'Engine/Shaders'
Manifest = ShaderRoot / 'ShaderManifest.json'

# glslangValidator spells the stages differently from everyone else.
ShortStage = {'compute': 'comp', 'vertex': 'vert', 'fragment': 'frag',
              'geometry': 'geom', 'tesscontrol': 'tesc', 'tessevaluation': 'tese'}


def Digest(Where):
    return hashlib.sha256(Where.read_bytes()).hexdigest()


def ReadTable():
    """The entries from CMakeLists.txt, as (source, stage, output)."""
    Text = (Root / 'CMakeLists.txt').read_text(encoding='utf-8')
    Opening = Text.index('set(SHADER_TABLE')
    Body = Text[Opening:Text.index(')', Text.index('.spv"', Opening))]
    Entries = re.findall(r'"([^"|]+)\|([^"|]+)\|([^"|]+)"', Body)
    if not Entries:
        raise SystemExit('CMakeLists.txt carries no SHADER_TABLE entries')
    return Entries


def ReadWindowsTable():
    """The same entries as spelled in the PowerShell route, for comparison."""
    Text = (Root / 'Tools/Build/ToolchainSequence.ps1').read_text(encoding='utf-8')
    Opening = Text.index('$ShaderTable = @(')
    Body = Text[Opening:Text.index('\n)', Opening)]
    return re.findall(r"Source\s*=\s*'([^']+)';\s*Stage\s*=\s*'([^']+)';\s*Output\s*=\s*'([^']+)'", Body)


def FindCompiler(Named):
    Candidates = [Named] if Named else []
    Candidates += [os.environ.get('FRONTIER_GLSLC'), os.environ.get('FRONTIER_SLANGC')]
    Vulkan = os.environ.get('VULKAN_SDK')
    for Name in ('glslc', 'slangc', 'glslangValidator'):
        if Vulkan:
            Candidates.append(str(Path(Vulkan) / 'bin' / Name))
        Candidates.append(shutil.which(Name))
    for Candidate in Candidates:
        if Candidate and Path(Candidate).is_file() and os.access(Candidate, os.X_OK):
            return Path(Candidate)
    raise SystemExit('No shader compiler found. Install glslc, slangc or glslangValidator, '
                     'or name one with --compiler.')


def Flavour(Compiler):
    Name = Compiler.name.lower().replace('.exe', '')
    return 'glslc' if Name == 'glslc' else 'slangc' if Name == 'slangc' else 'glslang'


def Version(Compiler, Which):
    Flag = ['--version'] if Which != 'slangc' else ['-v']
    try:
        Said = subprocess.run([str(Compiler), *Flag], capture_output=True, text=True, timeout=60)
        return ((Said.stdout or '') + (Said.stderr or '')).strip().splitlines()[0]
    except Exception:
        return 'unknown'


def Commands(Compiler, Which, Source, Stage, Output, Staging):
    """Every way of asking for the same SPIR-V, in the order they should be tried."""
    Includes = [f'-I{Root / "Engine"}', f'-I{ShaderRoot}']
    Define = '-DFRONTIER_SHADER_TOOLCHAIN=1'
    if Which == 'glslc':
        # glslc insists on a recognised extension, so the source is staged under a .glsl name.
        Staged = Staging / (Output.stem + '.glsl')
        shutil.copyfile(Source, Staged)
        return [[str(Compiler), Define, *Includes, '--target-env=vulkan1.2',
                 f'-fshader-stage={Stage}', '-o', str(Output), str(Staged)]]
    if Which == 'slangc':
        Base = [str(Compiler), str(Source), Define, *Includes, '-target', 'spirv',
                '-profile', 'glsl_450', '-stage', Stage, '-entry', 'main', '-o', str(Output)]
        # -allow-glsl is required by newer Slang to accept GLSL input and rejected by older ones.
        return [Base[:1] + ['-allow-glsl'] + Base[1:], Base]
    Staged = Staging / (Output.stem + '.glsl')
    shutil.copyfile(Source, Staged)
    return [[str(Compiler), '-V', '--target-env', 'vulkan1.2', '-S', ShortStage.get(Stage, Stage),
             Define, *Includes, '-o', str(Output), str(Staged)]]


def main() -> int:
    Parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    Parser.add_argument('--compiler', help='Path to glslc, slangc or glslangValidator')
    Parser.add_argument('--all', action='store_true', help='Recompile even what is up to date')
    Parser.add_argument('--check', action='store_true',
                        help='Compile nothing; fail if any output is missing or older than a source')
    Arguments = Parser.parse_args()

    Table = ReadTable()
    Windows = ReadWindowsTable()
    if sorted(Table) != sorted(Windows):
        Missing = sorted(set(Table) - set(Windows))
        Extra = sorted(set(Windows) - set(Table))
        print('The CMake and PowerShell shader tables disagree.', file=sys.stderr)
        for Entry in Missing:
            print(f'  only in CMakeLists.txt:        {Entry[0]} -> {Entry[2]}', file=sys.stderr)
        for Entry in Extra:
            print(f'  only in ToolchainSequence.ps1: {Entry[0]} -> {Entry[2]}', file=sys.stderr)
        return 1
    print(f'{len(Table)} shaders, and both build routes agree on all of them.', flush=True)

    # Every shader includes several of its neighbours, and none of those includes are declared per
    #    file, so the newest header in the folder decides whether anything is stale.
    Newest = max((Path(p).stat().st_mtime for p in ShaderRoot.glob('*.slang')), default=0)
    Newest = max(Newest, max((Path(p).stat().st_mtime for p in ShaderRoot.glob('*.h')), default=0))

    if Arguments.check:
        Stale = [Out for _, _, Out in Table
                 if not (ShaderRoot / Out).is_file() or (ShaderRoot / Out).stat().st_mtime < Newest]
        if Stale:
            print(f'{len(Stale)} shader binaries missing or stale: {", ".join(Stale[:8])}'
                  + (' …' if len(Stale) > 8 else ''), file=sys.stderr)
            return 1
        print('Every shader binary is present and newer than every source.')
        return 0

    Compiler = FindCompiler(Arguments.compiler)
    Which = Flavour(Compiler)
    Spoken = Version(Compiler, Which)
    print(f'Compiler: {Compiler}  ({Which})\n          {Spoken}', flush=True)

    Built, Skipped, Failed = [], [], []
    with tempfile.TemporaryDirectory() as Staging:
        Staging = Path(Staging)
        for SourceName, Stage, OutputName in Table:
            Source = (ShaderRoot / SourceName).resolve()
            Output = ShaderRoot / OutputName
            if not Source.is_file():
                Failed.append((OutputName, f'source absent: {Source}'))
                continue
            if not Arguments.all and Output.is_file() and Output.stat().st_mtime >= Newest:
                Skipped.append(OutputName)
                continue
            Said = None
            for Command in Commands(Compiler, Which, Source, Stage, Output, Staging):
                Said = subprocess.run(Command, capture_output=True, text=True, errors='replace')
                if Said.returncode == 0 and Output.is_file():
                    break
            if Said is None or Said.returncode != 0:
                Failed.append((OutputName, ((Said.stdout or '') + (Said.stderr or '')).strip()))
                print(f'FAIL {SourceName} -> {OutputName}', flush=True)
                continue
            Built.append(OutputName)
            print(f'  {SourceName}  ->  {OutputName}  ({Output.stat().st_size} bytes)', flush=True)

    if Failed:
        print(f'\n{len(Failed)} of {len(Table)} shaders did not compile:', file=sys.stderr)
        for Name, Why in Failed:
            print(f'\n--- {Name}\n{Why[:2000]}', file=sys.stderr)
        return 1

    Manifest.write_text(json.dumps({
        'compiler': {'path': Compiler.name, 'flavour': Which, 'version': Spoken},
        'shaders': [{'source': s, 'stage': t, 'output': o,
                     'source_sha256': Digest((ShaderRoot / s).resolve()),
                     'spirv_sha256': Digest(ShaderRoot / o),
                     'spirv_bytes': (ShaderRoot / o).stat().st_size} for s, t, o in Table],
    }, indent=2) + '\n', encoding='utf-8')
    print(f'\nPASS {len(Table)} shaders to SPIR-V '
          f'({len(Built)} compiled, {len(Skipped)} already current). Manifest: '
          f'{Manifest.relative_to(Root)}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
