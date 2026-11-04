#============================================================================================================================================
#                                                       VERIFYPAINTDELIVERY.PY
#============================================================================================================================================
# 🧩 Refuse stale test evidence or package payloads and record the exact linked Windows deliverable without claiming Windows execution.

from pathlib import Path
import argparse
import hashlib
import json
import struct
import subprocess
import zipfile

RepositoryRoot = Path(__file__).resolve().parents[3]


def Main():
    Parser = argparse.ArgumentParser()
    Parser.add_argument('--package', type=Path, default=RepositoryRoot / 'Binaries/TextureStudio-Windows-x64.zip')
    Arguments = Parser.parse_args()
    Results = RepositoryRoot / 'Exhibits/TextureStudio/Results'
    ReportPaths = [Results / (Name + '.json') for Name in ('MaterialLibraryChecks', 'MaterialWorkspaceChecks',
                   'MaterialAtlasChecks', 'ProceduralPaintChecks')]
    ReportPaths.append(RepositoryRoot / 'Exhibits/Renderer/Results/Verification.json')
    Reports = {}
    VerifiedSources = 0
    for ReportPath in ReportPaths:
        Report = json.loads(ReportPath.read_text())
        if Report.get('result') != 'passed':
            raise RuntimeError('Verification did not pass: ' + ReportPath.name)
        for Source in Report.get('sources', []):
            if hashlib.sha256((RepositoryRoot / Source['file']).read_bytes()).hexdigest() != Source['sha256']:
                raise RuntimeError('Stale evidence: ' + ReportPath.name + ' / ' + Source['file'])
            VerifiedSources += 1
        Reports[ReportPath.name] = {'sha256': hashlib.sha256(ReportPath.read_bytes()).hexdigest(),
                                  'file': ReportPath.relative_to(RepositoryRoot).as_posix()}
    PackagePath = Arguments.package.resolve()
    with zipfile.ZipFile(PackagePath) as Package:
        if Package.testzip() is not None:
            raise RuntimeError('ZIP integrity failure.')
        Manifest = json.loads(Package.read('Package.json'))
        if not Manifest['executable_linked'] or Manifest['windows_execution'] or Manifest['hardware_execution']:
            raise RuntimeError('Package incorrectly represents the execution boundary.')
        for Filename, Digest in Manifest['files'].items():
            if hashlib.sha256(Package.read(Filename)).hexdigest() != Digest:
                raise RuntimeError('Package payload differs: ' + Filename)
        for Filename, Digest in {**Manifest['sources'], **Manifest['build_source_sha256']}.items():
            if hashlib.sha256((RepositoryRoot / Filename).read_bytes()).hexdigest() != Digest:
                raise RuntimeError('Package source is stale: ' + Filename)
        Executable = Package.read('TextureStudio.exe')
        Offset = struct.unpack_from('<I', Executable, 0x3c)[0]
        if Executable[:2] != b'MZ' or Executable[Offset:Offset + 4] != b'PE\0\0':
            raise RuntimeError('Missing linked Windows executable.')
        if struct.unpack_from('<H', Executable, Offset + 4)[0] != 0x8664:
            raise RuntimeError('Not a Windows x64 executable.')
        if struct.unpack_from('<H', Executable, Offset + 24 + 68)[0] != 2:
            raise RuntimeError('Not a native Windows GUI subsystem executable.')
        Presets = [Name for Name in Package.namelist() if Name.endswith('.material')]
        Shaders = [Name for Name in Package.namelist() if Name.endswith('.spv')]
        if len(Presets) != 127 or len(Shaders) != 10:
            raise RuntimeError('Incomplete native catalogue or shader package.')
        ShaderManifest = json.loads(Package.read('EngineContent/ShaderArchives/Surface/ShaderCompilation.json'))
        for Source in ShaderManifest['sources']:
            if hashlib.sha256((RepositoryRoot / Source['file']).read_bytes()).hexdigest() != Source['sha256']:
                raise RuntimeError('Packaged shaders are stale: ' + Source['file'])
        for Entry in ShaderManifest['entries']:
            if hashlib.sha256(Package.read('EngineContent/ShaderArchives/Surface/' + Entry['entry'] + '.spv')).hexdigest() != Entry['spirv_sha256']:
                raise RuntimeError('Compiled shader bytes do not match reflection provenance.')
    Images = {}
    for Name in ('MaterialWorkspace.png', 'ReferenceMaterialWorkspace.png', 'Paint-GreenAutomotivePaint.png', 'Paint-Disabled.png'):
        Image = Results.parent / 'Images' / Name
        Bytes = Image.read_bytes()
        Images[Name] = {'width': struct.unpack_from('>I', Bytes, 16)[0], 'height': struct.unpack_from('>I', Bytes, 20)[0],
                        'sha256': hashlib.sha256(Bytes).hexdigest()}
    Report = {'result': 'passed', 'branch': subprocess.check_output(['git', 'branch', '--show-current'], cwd=RepositoryRoot, text=True).strip(),
              'verified_source_fingerprints': VerifiedSources, 'reports': Reports, 'images': Images,
              'package': {'file': PackagePath.relative_to(RepositoryRoot).as_posix(), 'bytes': PackagePath.stat().st_size,
                          'sha256': hashlib.sha256(PackagePath.read_bytes()).hexdigest(), 'verified_payload_files': len(Manifest['files']),
                          'material_presets': len(Presets), 'procedural_automotive_presets': 7, 'compiled_shaders': len(Shaders),
                          'executable_linked': True, 'windows_execution': False, 'hardware_execution': False},
              'limitations': ['Other HTML procedural families remain unported', 'Exact HTML/native appearance is not certified',
                              'Workspace evidence is Vulkan software-driver readback plus CPU ImGui/SVG composition, not an OS screenshot',
                              'Windows launch, hardware drivers and multi-window acceptance require user testing']}
    (Results / 'DeliveryChecks.json').write_text(json.dumps(Report, indent=2) + '\n')
    print('PASS: current source/evidence fingerprints, Windows x64 GUI executable, ZIP integrity and complete content/shaders')
    print('Package:', PackagePath)
    print('SHA256:', Report['package']['sha256'])
    print('Windows execution remains unverified.')


if __name__ == '__main__':
    Main()
