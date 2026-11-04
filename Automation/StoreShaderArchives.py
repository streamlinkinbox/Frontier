#============================================================================================================================================
#                                                       STORESHADERARCHIVES.PY
#============================================================================================================================================
# 🧩 Keeps the editor's compiled SPIR-V in EngineContent/ShaderArchives, so an editor build without a Slang compiler still starts (the
#    Project Browser's Surface set) and runs the Scene Viewport: `--build <dir>` stores the archives a build compiled, `--verify`
#    checks the stored archives against the current Slang sources (and against the build's archives when that build compiled them).

from pathlib import Path
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys

RepositoryRoot = Path(__file__).resolve().parents[1]
StoredRoot     = RepositoryRoot / 'EngineContent/ShaderArchives'

# 📝 Folder of each set's Slang units, the units its entries compile (None: every unit of the folder) and, optionally, further
#    folders its includes resolve in.
ArchiveSets = {
    'GlobalIllumination': ('Frontier/Engine/Shaders/GlobalIllumination', None),
    # 📝 The virtualized geometry units include the illumination port's refresh profile and intrinsics.
    'VirtualizedGeometry': ('Frontier/Engine/Shaders/VirtualizedGeometry',
                            ['VirtualizedGeometryCull', 'HiZPyramid', 'HiZDownsample', 'VisibilityRaster', 'MaterialResolve',
                             'ClusterDebugFallback'], ['Frontier/Engine/Shaders/GlobalIllumination']),
    # 📝 The analytic illumination's units include the illumination port's scene layouts and intrinsics as well.
    'AnalyticIllumination': ('Frontier/Engine/Shaders/AnalyticIllumination', ['SolarDepthRasterization', 'SurfaceShadingRasterization'],
                             ['Frontier/Engine/Shaders/GlobalIllumination']),
    'Environment': ('Frontier/Engine/Shaders', ['AtmosphereScattering', 'AtmosphereProjection', 'VisibilityProjection',
                                                 'PlanetaryProjection', 'CloudShadowProjection', 'VolumetricClouds', 'FogSpace',
                                                 'OpticalLensFlare', 'PreviewRaster']),
    # 📝 Everything a build compiles into ShaderArchives/Surface: Project Browser previews (loaded at start-up), CAD, MatCap,
    #    material surfaces, vector coverage and the bake field. Frontier.exe stops at start-up without the Browser entries.
    'Surface': ('Engine/Render/Shader', ['CadSurface', 'CadEdge', 'MatCapSurface', 'PreviewPbrSurface', 'MaterialSurface',
                                         'BrowserPreview', 'BrowserDrafting', 'VectorCoverage', 'VectorSurface',
                                         'BakeFieldEvaluation']),
}
IncludePattern = re.compile(r'^\s*(?:#include\s+"([^"]+)"|import\s+([A-Za-z_][A-Za-z0-9_.]*)\s*;)', re.MULTILINE)


def Digest(Filename):
    return hashlib.sha256(Filename.read_bytes()).hexdigest()


def ResolveSources(Set):
    Folder, Units, *Further = ArchiveSets[Set]
    Folders = [RepositoryRoot / Folder] + [RepositoryRoot / Extra for Extra in (Further[0] if Further else [])]
    if Units is None:
        return sorted(Folders[0].glob('*.slang'))
    # 📝 The include/import closure of the compiled units, each include resolved in the first folder that holds it.
    Pending, Found = [Folders[0] / (Unit + '.slang') for Unit in Units], set()
    while Pending:
        Unit = Pending.pop()
        if Unit in Found or not Unit.is_file():
            continue
        Found.add(Unit)
        for Included, Imported in IncludePattern.findall(Unit.read_text(encoding='utf-8')):
            Relative = Included if Included else Imported.replace('.', '/') + '.slang'
            Pending.append(next((Candidate / Relative for Candidate in Folders if (Candidate / Relative).is_file()), Folders[0] / Relative))
    return sorted(Found)


def DescribeCompiler(Compiler, Compiled):
    if not Compiler:
        return 'unrecorded'
    if Compiler.endswith('.js'):
        # 📝 The portable Slang WASM path (FRONTIER_SLANG_WASM, Automation/CompileSurfaceShaders.mjs) records its version and
        #    hashes beside the SPIR-V it wrote.
        Recorded = Compiled / 'ShaderCompilation.json'
        Facts = json.loads(Recorded.read_text(encoding='utf-8')) if Recorded.is_file() else {}
        return (f'slang-wasm {Facts.get("compiler", "unrecorded")} ({Path(Compiler).name}, '
                f'wasm sha256 {Facts.get("compiler_wasm_sha256", "unrecorded")})')
    Version = subprocess.run([Compiler, '-version'], capture_output=True, text=True)
    return 'slangc ' + (Version.stdout + Version.stderr).strip()


def Store(Set, Build, Compiler):
    Compiled = Build / 'ShaderArchives' / Set
    Binaries = sorted(Compiled.glob('*.spv'))
    if not Binaries:
        raise SystemExit(f'{Set}: no SPIR-V in {Compiled} (build the shader targets with the pinned slangc first)')
    Target = StoredRoot / Set
    Target.mkdir(parents=True, exist_ok=True)
    for Stale in Target.glob('*.spv'):
        Stale.unlink()
    for Binary in Binaries:
        shutil.copy2(Binary, Target / Binary.name)
    Manifest = {
        'format': 1,
        'set': Set,
        'compiler': DescribeCompiler(Compiler, Compiled),
        'refresh': f'python Automation/StoreShaderArchives.py --build <build> --compiler <slangc or slang-wasm.js> --sets {Set}',
        'entries': [{'file': Binary.name, 'spirv_sha256': Digest(Binary)} for Binary in Binaries],
        'sources': [{'file': Source.relative_to(RepositoryRoot).as_posix(), 'sha256': Digest(Source)} for Source in ResolveSources(Set)],
    }
    (Target / 'ShaderCompilation.json').write_text(json.dumps(Manifest, indent=2) + '\n', encoding='utf-8')
    print(f'{Set}: stored {len(Binaries)} SPIR-V archives from {len(Manifest["sources"])} Slang sources')


def Verify(Set, Build):
    # 📝 Build: compare that build's compiled SPIR-V with the stored copy (only meaningful for the compiler that stored it).
    Failures = []
    Target   = StoredRoot / Set
    Manifest = json.loads((Target / 'ShaderCompilation.json').read_text(encoding='utf-8'))
    Recorded = {Entry['file']: Entry['sha256'] for Entry in Manifest['sources']}
    Current  = {Source.relative_to(RepositoryRoot).as_posix(): Digest(Source) for Source in ResolveSources(Set)}
    for File in sorted(set(Recorded) | set(Current)):
        if Recorded.get(File) != Current.get(File):
            Failures.append(f'source changed since the archives were stored: {File}')
    for Entry in Manifest['entries']:
        Stored = Target / Entry['file']
        Payload = Stored.read_bytes() if Stored.is_file() else b''
        if len(Payload) < 20 or len(Payload) % 4 or int.from_bytes(Payload[:4], 'little') != 0x07230203:
            Failures.append(f'malformed stored SPIR-V: {Entry["file"]}')
        elif Digest(Stored) != Entry['spirv_sha256']:
            Failures.append(f'stored SPIR-V differs from its manifest: {Entry["file"]}')
    if Build is not None:
        Compiled = {Binary.name: Digest(Binary) for Binary in (Build / 'ShaderArchives' / Set).glob('*.spv')}
        Kept     = {Entry['file']: Entry['spirv_sha256'] for Entry in Manifest['entries']}
        for File in sorted(set(Compiled) | set(Kept)):
            if Compiled.get(File) != Kept.get(File):
                Failures.append(f'compiled and stored SPIR-V differ: {File}')
    for Failure in Failures:
        print(f'[FAIL] {Set}: {Failure}')
    print(f'{Set}: {len(Manifest["entries"])} stored archives, {len(Current)} sources, {len(Failures)} failures')
    return not Failures


def Main():
    Parser = argparse.ArgumentParser()
    Parser.add_argument('--build', type=Path, help='build folder whose ShaderArchives/<Set> holds freshly compiled SPIR-V')
    Parser.add_argument('--compiler', default='', help='the slangc that compiled them (recorded in ShaderCompilation.json)')
    Parser.add_argument('--sets', nargs='+', default=list(ArchiveSets), choices=list(ArchiveSets))
    Parser.add_argument('--verify', action='store_true', help='check instead of store; --build adds the compiled-vs-stored check')
    Parser.add_argument('--compare', nargs='*', choices=list(ArchiveSets),
                        help='with --verify --build: the sets whose compiled SPIR-V must equal the stored copy (default: every --sets)')
    Arguments = Parser.parse_args()
    if Arguments.verify:
        Compared = set(Arguments.sets if Arguments.compare is None else Arguments.compare)
        Results = [Verify(Set, Arguments.build.resolve() if Arguments.build and Set in Compared else None) for Set in Arguments.sets]
        if not all(Results):
            print('Refresh: python Automation/StoreShaderArchives.py --build <build> --compiler <slangc or slang-wasm.js> --sets <set>')
            sys.exit(1)
        return
    if Arguments.build is None:
        Parser.error('--build is required to store archives')
    for Set in Arguments.sets:
        Store(Set, Arguments.build.resolve(), Arguments.compiler)


if __name__ == '__main__':
    Main()
