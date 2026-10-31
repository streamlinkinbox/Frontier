#============================================================================================================================================
#                                                       VERIFYVECTORSURFACE.PY
#============================================================================================================================================
# 🧩 Rebuild and exercise seven authored Float32 vector channels on an explicit Vulkan driver, including sanitizers.

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import struct
import zipfile

Root = Path(__file__).resolve().parents[3]
Sources = [
    'Exhibits/TextureStudio/Code/VectorSurfaceValidation.cpp',
    'Engine/Render/VectorCoverageRasterization.cpp',
    'Engine/Render/VectorAuthoringCoverageRendering.cpp',
    'Engine/Render/VectorSurfaceCompilation.cpp',
    'Engine/ContentInterchange/SurfaceMapEncoding.cpp',
    'Engine/ContentInterchange/VectorAuthoringDocument.cpp',
    'Engine/ContentInterchange/VectorAuthoringEditing.cpp',
    'Engine/ContentInterchange/VectorAuthoringProperties.cpp',
    'Engine/Render/SurfaceRasterization.cpp',
    'Engine/Render/SurfaceMaterial.cpp',
    'Engine/ContentInterchange/MaterialLibrary.cpp',
    'Engine/Interface/TextFileReplacement.cpp',
]
Parser = argparse.ArgumentParser()
Parser.add_argument('--driver', type=Path, required=True)
Parser.add_argument('--shader', type=Path, required=True)
Parser.add_argument('--destination', type=Path, required=True)
Arguments = Parser.parse_args()
Destination = Arguments.destination.resolve()
Destination.mkdir(parents=True, exist_ok=True)
Executions = {}
Includes = ['-I.', '-IExternalPackages/vulkan-headers/include', '-IExternalPackages/tomlpp/include',
            '-IExternalPackages/stb']
for Sanitized in [False, True]:
    Caption = 'sanitized' if Sanitized else 'strict'
    Flags = ['-std=c++20', '-O1', '-ffunction-sections', '-fdata-sections', '-DVK_NO_PROTOTYPES',
             '-Wall', '-Wextra', '-Werror', '-Wno-missing-field-initializers', '-Wno-unused-function'] + Includes
    if Sanitized:
        Flags += ['-g1', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    Executable = Destination / ('VectorSurfaceValidation-' + Caption)
    BuildCommand = ['g++'] + Flags + Sources + ['-Wl,--gc-sections', '-ldl', '-o', str(Executable)]
    Built = subprocess.run(BuildCommand, cwd=Root, text=True, capture_output=True, timeout=240)
    Executions[Caption + '_build'] = {'command': BuildCommand, 'returncode': Built.returncode,
                                      'stdout': Built.stdout, 'stderr': Built.stderr}
    if Built.returncode:
        raise RuntimeError(Caption + ' Float32 surface build failed:\n' + Built.stderr)
    RunCommand = [str(Executable), str(Arguments.driver.resolve()), str(Arguments.shader.resolve()), str(Destination / Caption)]
    Environment = dict(os.environ, ASAN_OPTIONS='detect_leaks=1', UBSAN_OPTIONS='halt_on_error=1')
    Completed = subprocess.run(RunCommand, cwd=Root, env=Environment, text=True, capture_output=True, timeout=240)
    Executions[Caption + '_execution'] = {'command': RunCommand, 'returncode': Completed.returncode,
                                          'stdout': Completed.stdout, 'stderr': Completed.stderr}
    print(Completed.stdout, end='', flush=True)
    if Completed.returncode:
        raise RuntimeError(Caption + ' Float32 surface execution failed:\n' + Completed.stderr)
    Report = json.loads((Destination / Caption / 'VectorSurfaceChecks.json').read_text())
    if Report.get('result') != 'passed' or Report.get('channel_count') != 7 or not Report.get('cached_surface_submission'):
        raise RuntimeError(Caption + ' result report did not authenticate all seven channels.')

ChannelNames = ['Colour', 'Height', 'Normal', 'Roughness', 'Metalness', 'Opacity', 'Occlusion']
SampleImages = {}
for Name in ChannelNames:
    for Suffix in ['', 'Alpha']:
        Caption = 'GpuFloat' + Name + Suffix + '.pfm'
        StrictBytes = (Destination / 'strict' / Caption).read_bytes()
        SanitizedBytes = (Destination / 'sanitized' / Caption).read_bytes()
        if StrictBytes != SanitizedBytes:
            raise RuntimeError('Strict and sanitizer Float32 maps differ: ' + Caption)
        FirstLine, WidthLine, ScaleLine, Payload = StrictBytes.split(b'\n', 3)
        ExpectedMagic = b'PF' if Name == 'Colour' and not Suffix else b'Pf'
        if FirstLine != ExpectedMagic or WidthLine != b'32 32' or ScaleLine != b'-1.0':
            raise RuntimeError('Malformed portable Float32 PFM header: ' + Caption)
        Components = 3 if ExpectedMagic == b'PF' else 1
        if len(Payload) != 32 * 32 * Components * 4:
            raise RuntimeError('Unexpected PFM Float32 payload length: ' + Caption)
        Samples = struct.unpack('<' + 'f' * (len(Payload) // 4), Payload)
        if not all(__import__('math').isfinite(Sample) for Sample in Samples):
            raise RuntimeError('Non-finite portable Float32 sample: ' + Caption)
        SampleImages[Caption] = StrictBytes

SampleImages['README.txt'] = (
    'Frontier Float32 surface validation fixture\n'
    'Seven actual RGBA32F Vulkan maps and seven channel-contribution masks.\n'
    '32 x 32 synthetic test document with separate fabric and gold-thread layers.\n'
    'These are test fixtures, not exports of a user document or calibrated physical displacement.\n'
    'PFM floats are little-endian IEEE-754, bottom row first; RGB files use PF, scalar files use Pf.\n'
).encode()
ArchivePath = Root / 'Exhibits/TextureStudio/Outputs/SurfaceFloatSamples.zip'
ArchivePath.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(ArchivePath, 'w') as Archive:
    for Caption, Payload in sorted(SampleImages.items()):
        Entry = zipfile.ZipInfo(Caption, (1980, 1, 1, 0, 0, 0))
        Entry.compress_type = zipfile.ZIP_DEFLATED
        Entry.external_attr = 0o100644 << 16
        Archive.writestr(Entry, Payload, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
with zipfile.ZipFile(ArchivePath) as Archive:
    if set(Archive.namelist()) != set(SampleImages):
        raise RuntimeError('Float32 PFM sample archive contents mismatch.')
    if any(Archive.read(Caption) != Payload for Caption, Payload in SampleImages.items()):
        raise RuntimeError('Float32 PFM sample archive changed source pixels.')
ExhibitReport = {
    'result': 'passed', 'float_maps': 7, 'contribution_masks': 7, 'image_dimensions': [32, 32],
    'strict_sanitized_byte_identical': True, 'archive_sha256': hashlib.sha256(ArchivePath.read_bytes()).hexdigest(),
    'archive_bytes': ArchivePath.stat().st_size,
    'entries': {Caption: hashlib.sha256(Payload).hexdigest() for Caption, Payload in SampleImages.items()},
}
(Root / 'Exhibits/TextureStudio/Results/SurfaceFloatExhibitChecks.json').write_text(json.dumps(ExhibitReport, indent=2) + '\n')

Digest = lambda Filename: hashlib.sha256(Path(Filename).read_bytes()).hexdigest()
Summary = {
    'result': 'passed', 'channels': 7, 'normal_and_sanitized': True,
    'driver_sha256': Digest(Arguments.driver), 'shader_sha256': Digest(Arguments.shader),
    'source_sha256': {Source: Digest(Root / Source) for Source in Sources + [
        'Engine/Render/VectorCoverageRasterization.h', 'Engine/Render/VectorAuthoringCoverageRendering.h',
        'Engine/Render/VectorSurfaceCompilation.h',
        'Engine/Render/Shader/VectorSurface.slang', 'Engine/Render/Shader/VectorCoverage.slang',
        'Exhibits/Renderer/Code/VulkanReadback.h', 'Exhibits/TextureStudio/Code/AtlasReadback.h']},
    'executions': Executions,
}
(Destination / 'VectorSurfaceValidation.json').write_text(json.dumps(Summary, indent=2) + '\n')
print('PASS: Float32 channel dispatch and separate layer metalness passed strict and sanitizer Vulkan runs.')
