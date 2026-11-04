#============================================================================================================================================
#                                                    PACKAGEEDITORPREVIEWWINDOWS.PY
#============================================================================================================================================
# 🧩 Package a built Windows x64 Frontier.exe (MSVC or the pinned Zig cross-toolchain) with EngineContent, its Scene Viewport
#    shader archives and Project Everest, so the editor and the live Scene Viewport can be tried without a build.

from pathlib import Path
import argparse
import hashlib
import json
import shutil
import struct
import subprocess
import tempfile
import zipfile

RepositoryRoot = Path(__file__).resolve().parents[1]
# 📝 What Frontier.exe opens before the first click (Experimental/Frontier/Native/WindowsFrontier.cpp). The 21a0ccb4 preview had no
#    ShaderArchives/Surface and stopped at start-up with "Missing or malformed browser shader archive".
StartupFiles = ['EngineContent/FontArchives/FontCatalogue.json', 'Projects/Everest/Everest.projectspace'] + \
    [f'EngineContent/ShaderArchives/Surface/{Entry}.spv'
     for Entry in ['BrowserPreviewVertex', 'BrowserPreviewFragment', 'BrowserDraftingVertex', 'BrowserDraftingFragment']]
StartupFolders = ['EngineContent/Icons']


def Digest(Filename):
    return hashlib.sha256(Filename.read_bytes()).hexdigest()


def VerifyExecutable(Executable):
    Payload = Executable.read_bytes()
    Offset = struct.unpack_from('<I', Payload, 0x3c)[0]
    if Payload[:2] != b'MZ' or Payload[Offset:Offset + 4] != b'PE\0\0' or \
            struct.unpack_from('<H', Payload, Offset + 4)[0] != 0x8664 or struct.unpack_from('<H', Payload, Offset + 24)[0] != 0x20b:
        raise RuntimeError('Expected a Windows x64 PE32+ executable: ' + str(Executable))


def LocateSolidArcExecutable(Executable, Requested=None):
    Payload = Executable.read_bytes()
    Required = Requested is not None or any(Marker in Payload for Marker in
        (b'SolidArcWorkspace.exe', 'SolidArcWorkspace.exe'.encode('utf-16-le')))
    if not Required:
        return None
    Candidate = Requested.resolve() if Requested is not None else Executable.with_name('SolidArcWorkspace.exe')
    if not Candidate.is_file():
        raise RuntimeError('SolidArc-enabled Frontier requires SolidArcWorkspace.exe beside it; build the SolidArc target')
    VerifyExecutable(Candidate)
    if any(Marker not in Candidate.read_bytes() for Marker in (b'Shift straight', b'Surface cutters')):
        raise RuntimeError('Refusing a stale pre-selection/cutter/Shift SolidArc executable')
    return Candidate


def LocateTextureExecutable(Executable, Requested=None):
    # 📝 The browser's New dialog › Texture library starts TextureWorkspace.exe beside Frontier.exe (target TextureStudio).
    Payload = Executable.read_bytes()
    Required = Requested is not None or any(Marker in Payload for Marker in
        (b'TextureWorkspace.exe', 'TextureWorkspace.exe'.encode('utf-16-le')))
    if not Required:
        return None
    Candidate = Requested.resolve() if Requested is not None else Executable.with_name('TextureWorkspace.exe')
    if not Candidate.is_file():
        raise RuntimeError('Frontier starts TextureWorkspace.exe from its New dialog; build the TextureStudio target beside it')
    VerifyExecutable(Candidate)
    return Candidate


def VerifySpirv(Binary):
    Payload = Binary.read_bytes() if Binary.is_file() else b''
    if len(Payload) < 20 or len(Payload) % 4 or struct.unpack_from('<I', Payload)[0] != 0x07230203:
        raise RuntimeError('Missing or malformed SPIR-V: ' + str(Binary))


def VerifyShaderSets(Installation):
    # 📝 Every set stored in the repository (Automation/StoreShaderArchives.py) arrives complete, whether the build compiled its own
    #    copy or used the stored one.
    Counts = {}
    for Manifest in sorted((RepositoryRoot / 'EngineContent/ShaderArchives').glob('*/ShaderCompilation.json')):
        Entries = json.loads(Manifest.read_text(encoding='utf-8'))['entries']
        for Entry in Entries:
            VerifySpirv(Installation / 'EngineContent/ShaderArchives' / Manifest.parent.name / Entry['file'])
        Counts[Manifest.parent.name] = len(Entries)
    if set(Counts) != {'GlobalIllumination', 'Environment', 'VirtualizedGeometry', 'AnalyticIllumination', 'Surface'}:
        raise RuntimeError('Expected the GlobalIllumination, Environment, VirtualizedGeometry, AnalyticIllumination and Surface shader sets, found ' +
                           ', '.join(sorted(Counts)))
    return Counts


def VerifyStartup(Installation):
    for Required in StartupFiles:
        if not (Installation / Required).is_file():
            raise RuntimeError('Frontier.exe needs this at start-up: ' + Required)
    for Required in StartupFolders:
        if not any((Installation / Required).iterdir()):
            raise RuntimeError('Frontier.exe needs this at start-up: ' + Required)
    for Required in StartupFiles:
        if Required.endswith('.spv'):
            VerifySpirv(Installation / Required)


def Main():
    Parser = argparse.ArgumentParser()
    Parser.add_argument('--executable', type=Path, required=True, help='the built Frontier.exe')
    Parser.add_argument('--solidarc-executable', type=Path, help='override the CAD executable beside a SolidArc-enabled Frontier')
    Parser.add_argument('--texture-executable', type=Path, help='override TextureWorkspace.exe beside Frontier.exe')
    Parser.add_argument('--toolchain', required=True, help='e.g. "Zig 0.13.0 x86_64-windows-gnu (Automation/WindowsToolchain.cmake)"')
    Parser.add_argument('--output', type=Path, default=RepositoryRoot / 'Binaries/FrontierSceneViewport-Windows-x64.zip')
    Parser.add_argument('--omit-material-archives', action='store_true',
                        help='leave out the Texture Studio material archives (~96 MiB); the Scene Viewport does not read them '
                             'and TextureWorkspace then starts with an empty library')
    Arguments = Parser.parse_args()
    Executable = Arguments.executable.resolve()
    VerifyExecutable(Executable)
    SolidArcExecutable = LocateSolidArcExecutable(Executable, Arguments.solidarc_executable)
    TextureExecutable = LocateTextureExecutable(Executable, Arguments.texture_executable)
    BuildContent = Executable.parent / 'EngineContent'
    StagedProject = RepositoryRoot / 'Projects/Everest/Content'
    for Required in [BuildContent / 'FontArchives/FontCatalogue.json', StagedProject / 'EnvironmentArchives/ProvingGround/ProvingGround.facetspace',
                     StagedProject / 'EnvironmentArchives/CelestialMaps/Hipparcos8.csv']:
        if not Required.is_file():
            raise RuntimeError('Missing (build Frontier first; it stages the project content): ' + str(Required))
    Scratch = RepositoryRoot / '_ClaudeScratch/tmp'
    Scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=Scratch, prefix='editor-preview-') as Temporary:
        Installation = Path(Temporary)
        shutil.copy2(Executable, Installation / 'Frontier.exe')
        if SolidArcExecutable is not None:
            shutil.copy2(SolidArcExecutable, Installation / 'SolidArcWorkspace.exe')
            shutil.copy2(RepositoryRoot / 'Experimental/SolidArc/Native/Package/SolidArcWorkspace.toml',
                         Installation / 'SolidArcWorkspace.toml')
            (Installation / 'Samples').mkdir()
            shutil.copy2(RepositoryRoot / 'Experimental/VehicleSurfacing/Samples/SyntheticBonnet.toml',
                         Installation / 'Samples/SyntheticBonnet.toml')
        if TextureExecutable is not None:
            shutil.copy2(TextureExecutable, Installation / 'TextureWorkspace.exe')
        # 📝 The EngineContent the build placed beside the executable: stored archives replaced by the sets it compiled.
        Omitted = shutil.ignore_patterns('MaterialArchives') if Arguments.omit_material_archives else None
        shutil.copytree(BuildContent, Installation / 'EngineContent', ignore=Omitted)
        if SolidArcExecutable is not None:
            Symbols = list((RepositoryRoot / 'EngineContent/Icons/SolidArc').glob('*.svg'))
            if len(Symbols) < 66:
                raise RuntimeError('Incomplete SolidArc icon source collection')
            for Symbol in Symbols:
                Bundled = Installation / 'EngineContent/Icons/SolidArc' / Symbol.name
                if not Bundled.is_file() or Digest(Bundled) != Digest(Symbol):
                    raise RuntimeError('Missing or stale packaged SolidArc icon: ' + Symbol.name)
            if not (Installation / 'EngineContent/FontArchives/DMSans/DMSans-Regular.ttf').is_file():
                raise RuntimeError('SolidArc requires the bundled DM Sans font')
        ShaderCounts = VerifyShaderSets(Installation)
        Project = Installation / 'Projects/Everest'
        Project.mkdir(parents=True)
        shutil.copy2(RepositoryRoot / 'Projects/Everest/Everest.projectspace', Project / 'Everest.projectspace')
        Staging = ['.staged_manifest.json'] + (['MaterialArchives'] if Arguments.omit_material_archives else [])
        shutil.copytree(StagedProject, Project / 'Content', ignore=shutil.ignore_patterns(*Staging))
        VerifyStartup(Installation)
        Catalogue = json.loads((Installation / 'EngineContent/FontArchives/FontCatalogue.json').read_text(encoding='utf-8'))
        for Typeface in Catalogue['faces']:
            if Digest(Installation / 'EngineContent/FontArchives' / Typeface['file']) != Typeface['sha256']:
                raise RuntimeError('Packaged font differs: ' + Typeface['file'])
        Notices = Installation / 'Notices'
        Notices.mkdir()
        for Package, Filename in [('imgui', 'LICENSE.txt'), ('glfw', 'LICENSE.md'), ('tomlpp', 'LICENSE'), ('stb', 'LICENSE'), ('thorvg', 'LICENSE')]:
            shutil.copy2(RepositoryRoot / 'ExternalPackages' / Package / Filename, Notices / (Package + '-LICENSE.txt'))
        if SolidArcExecutable is not None:
            shutil.copy2(RepositoryRoot / 'ExternalPackages/volk/LICENSE.md', Notices / 'volk-LICENSE.txt')
        shutil.copytree(RepositoryRoot / 'ExternalPackages/vulkan-headers/LICENSES', Notices / 'Vulkan-Headers')
        shutil.copy2(RepositoryRoot / 'Engine/Interface/Lucide.NOTICE', Notices / 'Lucide.NOTICE')
        shutil.copy2(RepositoryRoot / 'Documentation/SourceNotices.md', Notices / 'SourceNotices.md')
        shutil.copy2(RepositoryRoot / 'Exhibits/SceneViewport/WindowsPreviewReadme.md', Installation / 'README.md')
        Revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=RepositoryRoot, text=True).strip()
        Metadata = {
            'format': 1, 'target': 'Windows x64', 'toolchain': Arguments.toolchain, 'revision': Revision,
            'uncommitted_changes': bool(subprocess.check_output(['git', 'status', '--porcelain', '--untracked-files=no'], cwd=RepositoryRoot)),
            'signed': False, 'antivirus_scanned': False, 'windows_runtime_verified': False,
            'solidarc': {'included': SolidArcExecutable is not None,
                         'executable_sha256': Digest(SolidArcExecutable) if SolidArcExecutable is not None else None},
            'texture_workspace': {'included': TextureExecutable is not None,
                                  'executable_sha256': Digest(TextureExecutable) if TextureExecutable is not None else None,
                                  'material_presets': not Arguments.omit_material_archives},
            'scene_viewport': 'Project Everest > Scene Viewport: ported VehicleHMI ReSTIR GI + PlanetaryAtmosphere environment',
            'omitted': ['EngineContent/MaterialArchives', 'Projects/Everest/Content/MaterialArchives'] if Arguments.omit_material_archives else [],
            'shader_sets': ShaderCounts, 'startup_files_verified': StartupFiles + StartupFolders,
            'files': {Filename.relative_to(Installation).as_posix(): Digest(Filename)
                      for Filename in sorted(Installation.rglob('*')) if Filename.is_file()},
        }
        (Installation / 'Package.json').write_text(json.dumps(Metadata, indent=2) + '\n', encoding='utf-8')
        Arguments.output.parent.mkdir(parents=True, exist_ok=True)
        # 📝 One top-level folder named after the archive: an extraction never merges with an older one.
        Root = Path(Arguments.output.stem)
        with zipfile.ZipFile(Arguments.output, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as Archive:
            for Filename in sorted(Installation.rglob('*')):
                if Filename.is_file():
                    Archive.write(Filename, Root / Filename.relative_to(Installation))
        with zipfile.ZipFile(Arguments.output) as Archive:
            if Archive.testzip() is not None:
                raise RuntimeError('ZIP integrity validation failed')
        Checksum = Digest(Arguments.output)
        Arguments.output.with_suffix('.zip.sha256').write_text(Checksum + '  ' + Arguments.output.name + '\n')
        print('Windows editor preview:', Arguments.output, f'({Arguments.output.stat().st_size / 1048576:.1f} MiB)')
        print('SHA256:', Checksum)
        print('Unsigned, not antivirus-scanned, never launched on Windows here: interactive startup is a user acceptance check.')


if __name__ == '__main__':
    Main()
