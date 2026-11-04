#============================================================================================================================================
#                                                       VERIFYRASTEREVIDENCE.PY
#============================================================================================================================================
# 🧩 Reproduce host CPU/Vulkan/sanitizer checks and Windows x64 cross-compilation using explicitly provisioned tools; download nothing.

from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import zlib

Root = Path(__file__).resolve().parents[3]


def ConfirmExecution(Command, Environment, Expected=0):
    Completed = subprocess.run(Command, cwd=Root, env=Environment, text=True,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=240)
    print(Completed.stdout, end='', flush=True)
    if Completed.stderr:
        print(Completed.stderr, end='', file=sys.stderr, flush=True)
    if Completed.returncode != Expected:
        raise RuntimeError(f'Unexpected exit {Completed.returncode}: {Command}')
    return {'command': [str(Argument) for Argument in Command], 'exit_code': Completed.returncode,
            'stdout': Completed.stdout, 'stderr': Completed.stderr}


def QueryDigest(Source):
    return hashlib.sha256(Source.read_bytes()).hexdigest()


def VerifyCapture(Png, Ppm):
    Encoded = Png.read_bytes()
    assert Encoded[:8] == b'\x89PNG\r\n\x1a\n'
    Offset = 8
    Compressed = b''
    while Offset < len(Encoded):
        Length = struct.unpack_from('>I', Encoded, Offset)[0]
        Category = Encoded[Offset+4:Offset+8]
        Payload = Encoded[Offset+8:Offset+8+Length]
        if Category == b'IHDR':
            Width, Height = struct.unpack_from('>II', Payload)
        if Category == b'IDAT':
            Compressed += Payload
        Offset += Length + 12
    Rows = zlib.decompress(Compressed)
    Stride = Width * 3 + 1
    assert len(Rows) == Stride * Height
    assert all(Rows[Offset] == 0 for Offset in range(0, len(Rows), Stride))
    Pixels = b''.join(Rows[Offset+1:Offset+Stride] for Offset in range(0, len(Rows), Stride))
    assert Pixels == Ppm.read_bytes().split(b'\n', 3)[3], 'Evidence pixels differ from real device readback'
    return {'file': Png.relative_to(Root).as_posix(), 'width': Width, 'height': Height,
            'sha256': QueryDigest(Png), 'readback_rgb_sha256': hashlib.sha256(Pixels).hexdigest()}


def Main():
    Parser = argparse.ArgumentParser(description='Local proof on an already provisioned GCC/Node/Python/Zig editing host')
    Parser.add_argument('--slang-wasm', type=Path, required=True, dest='SlangWasm')
    Parser.add_argument('--driver', type=Path, required=True, dest='Driver')
    Parser.add_argument('--windows-tools', type=Path, required=True, dest='WindowsTools')
    Parser.add_argument('--destination', type=Path, required=True, dest='Destination')
    Arguments = Parser.parse_args()
    Destination = Arguments.Destination.resolve()
    Destination.mkdir(parents=True, exist_ok=True)
    ReportDirectory = Root / 'Exhibits/Renderer/Results'
    Shaders = Destination / 'Shaders'
    Readback = Destination / 'Results'
    Environment = os.environ.copy()
    Environment['PYTHONPATH'] = str(Arguments.WindowsTools.resolve()) + os.pathsep + Environment.get('PYTHONPATH', '')
    # 📝 Keep the cross-toolchain's first-use runtime compilation bounded on high-core editing hosts.
    if hasattr(os, 'sched_getaffinity'):
        os.sched_setaffinity(0, set(sorted(os.sched_getaffinity(0))[:2]))
    Executions = {}
    Executions['shader_compilation'] = ConfirmExecution(['node', 'Automation/CompileSurfaceShaders.mjs',
        str(Arguments.SlangWasm.resolve()), str(Shaders)], Environment)
    Prefix = ['g++', '-std=c++20', '-DVK_NO_PROTOTYPES', '-I.', '-IExternalPackages/vulkan-headers/include',
              'Engine/Render/SurfaceRasterization.cpp']
    for Sanitized in (False, True):
        for Name in ('SurfaceGeometryValidation', 'SurfaceRasterizationValidation', 'MatCapRasterizationValidation','PreviewPbrRasterizationValidation','MaterialRasterizationValidation'):
            Binary = Destination / (Name + ('Sanitized' if Sanitized else ''))
            Options = (['-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if Sanitized else
                       ['-Wall', '-Wextra', '-Werror', '-Wno-missing-field-initializers'])
            Compile = Prefix + Options + ['Exhibits/Renderer/Code/' + Name + '.cpp']
            if Name == 'MaterialRasterizationValidation':
                Compile += ['Engine/Render/SurfaceMaterial.cpp', '-IExternalPackages/tomlpp/include', '-IExternalPackages/stb']
            if Name != 'SurfaceGeometryValidation' and os.name != 'nt':
                Compile += ['-ldl']
            Executions[Name + ('_sanitized_build' if Sanitized else '_build')] = ConfirmExecution(
                Compile + ['-o', str(Binary)], Environment)
            Invocation = [str(Binary)]
            if Name != 'SurfaceGeometryValidation':
                Invocation += [str(Arguments.Driver.resolve()), str(Shaders),
                               str(Destination / 'SanitizedResults' if Sanitized else Readback)]
            if Name == 'MaterialRasterizationValidation':
                Invocation += [str(Root / 'Exhibits/Renderer/Materials')]
            Executions[Name + ('_sanitized' if Sanitized else '')] = ConfirmExecution(Invocation, Environment)
    Malformed = Destination / 'MalformedShaders'
    Malformed.mkdir(exist_ok=True)
    (Malformed / 'CadSurfaceVertex.spv').write_bytes(b'\x03\x02\x23\x07')
    Executions['malformed_archive_refusal'] = ConfirmExecution([str(Destination / 'SurfaceRasterizationValidation'),
        str(Arguments.Driver.resolve()), str(Malformed), str(Destination / 'RejectedReadback')], Environment, 1)
    assert 'Malformed shader file' in Executions['malformed_archive_refusal']['stderr']
    Executions['cross_toolchain_version'] = ConfirmExecution([sys.executable, '-m', 'ziglang', 'version'], Environment)
    assert Executions['cross_toolchain_version']['stdout'].strip() == '0.13.0'
    WindowsBinaries = []
    for Name in ('SurfaceRasterizationValidation','MatCapRasterizationValidation','PreviewPbrRasterizationValidation','MaterialRasterizationValidation'):
        Binary = Root / ('Binaries/RendererValidation/' + Name + '.exe')
        Binary.parent.mkdir(parents=True, exist_ok=True)
        MaterialSources = (['Engine/Render/SurfaceMaterial.cpp', '-IExternalPackages/tomlpp/include', '-IExternalPackages/stb']
                           if Name == 'MaterialRasterizationValidation' else [])
        Executions[Name + '_windows_cross_compile'] = ConfirmExecution([sys.executable, '-m', 'ziglang', 'c++',
            '-target', 'x86_64-windows-gnu', '-std=c++20', '-O1', '-s', '-static', '-DVK_NO_PROTOTYPES',
            '-I.', '-IExternalPackages/vulkan-headers/include', 'Engine/Render/SurfaceRasterization.cpp',
            'Exhibits/Renderer/Code/' + Name + '.cpp'] + MaterialSources + ['-o', str(Binary)], Environment)
        Executable = Binary.read_bytes()
        assert Executable[:2] == b'MZ'
        Header = struct.unpack_from('<I', Executable, 0x3c)[0]
        assert Executable[Header:Header+4] == b'PE\0\0'
        assert struct.unpack_from('<H', Executable, Header+4)[0] == 0x8664
        assert not (struct.unpack_from('<H', Executable, Header+22)[0] & 0x2000), 'Expected an executable, not a DLL'
        assert struct.unpack_from('<H', Executable, Header+24)[0] == 0x20b
        WindowsBinaries.append({'path': Binary.relative_to(Root).as_posix(), 'bytes': len(Executable),
                               'architecture': 'Windows x64 PE32+ executable', 'sha256': QueryDigest(Binary)})
    ConfirmExecution([sys.executable, 'Exhibits/Renderer/Code/EncodeRasterEvidence.py', str(Readback),
                      'Exhibits/Renderer'], Environment)
    for Reflection in Shaders.glob('*.json'):
        shutil.copyfile(Reflection, ReportDirectory / Reflection.name)
    Sources = ['CMakeLists.txt', 'Automation/SurfaceRasterization.cmake', 'Automation/CompileSurfaceShaders.mjs',
               'Engine/Render/SurfaceGeometry.h', 'Engine/Render/SurfaceNormalProjection.h', 'Engine/Render/ScatteringSequence.h', 'Engine/Render/MatCapProjection.h', 'Engine/Render/SurfaceRasterization.h',
               'Engine/Render/SurfaceRasterization.cpp']
    Sources += ['Engine/Render/AnisotropicReflection.h','Engine/Render/SurfaceMaterial.h','Engine/Render/SurfaceMaterial.cpp','Automation/UIDependencies.json']
    Sources += [Source.relative_to(Root).as_posix() for Source in (Root / 'Engine/Render/Shader').glob('*.slang')]
    Sources += [Source.relative_to(Root).as_posix() for Source in Path(__file__).parent.glob('*.cpp')]
    Sources += [Source.relative_to(Root).as_posix() for Source in Path(__file__).parent.glob('*.h')]
    Sources += ['Exhibits/Renderer/Code/VerifyRasterEvidence.py', 'Exhibits/Renderer/Code/EncodeRasterEvidence.py']
    Sources += [Source.relative_to(Root).as_posix() for Source in (Root / 'Exhibits/Renderer/Materials').rglob('*') if Source.is_file()]
    Sources += ['Exhibits/Renderer/Code/GenerateMaterialFixtures.py']
    Report = {'result': 'passed', 'date': '2026-10-18',
              'host': sys.platform, 'windows_execution_verified': False, 'hardware_gpu_verified': False,
              'validation_layers_enabled': False, 'cmake_configure_executed': False,
              'cmake_limitation': 'Host CMake unavailable; no package installed to obtain it. Direct compiler checks executed.',
              'windows_binary': WindowsBinaries[0], 'windows_binaries': WindowsBinaries,
              'sources': [{'file': Source, 'sha256': QueryDigest(Root / Source)} for Source in sorted(Sources)],
              'captures': [VerifyCapture(Image, Readback / (Image.stem + '.ppm'))
                           for Image in sorted((Root / 'Exhibits/Renderer/Images').glob('*.png'))
                           if Image.name not in ('PreviewPbrBrowser.png','MaterialBrowser.png')],
              'executions': Executions}
    (ReportDirectory / 'Verification.json').write_text(json.dumps(Report, indent=2) + '\n')
    print('PASS: final CAD/MatCap/PBR/authored-material CPU/Vulkan/ASan/UBSan checks, malformed archive refusal, Windows x64 cross-build and unchanged evidence pixels')


if __name__ == '__main__':
    Main()
