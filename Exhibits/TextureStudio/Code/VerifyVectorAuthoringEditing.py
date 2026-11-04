#============================================================================================================================================
#                                                         VERIFYVECTORAUTHORINGEDITING.PY
#============================================================================================================================================
# 🧩 Reproduce shared production authoring checks, optional sanitizer/GPU checks and Windows cross-linking without publication.

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import struct
import subprocess
import sys

Root = Path(__file__).resolve().parents[3]
Parser = argparse.ArgumentParser()
Parser.add_argument('--build-directory', type=Path, default=Root / '_ClaudeScratch/build/VectorAuthoringEditing')
Parser.add_argument('--sanitizers', action='store_true')
Parser.add_argument('--controls', action='store_true')
Parser.add_argument('--gpu-driver', type=Path)
Parser.add_argument('--slang-wasm', type=Path)
Parser.add_argument('--windows-tools', type=Path)
Arguments = Parser.parse_args()
if bool(Arguments.gpu_driver) != bool(Arguments.slang_wasm):
    Parser.error('Supply both --gpu-driver and --slang-wasm for actual GPU readback checks.')
if hasattr(os, 'sched_getaffinity'):
    os.sched_setaffinity(0, set(sorted(os.sched_getaffinity(0))[:4]))
Build = Arguments.build_directory.resolve()
Build.mkdir(parents=True, exist_ok=True)
Results = Root / 'Exhibits/TextureStudio/Results'
Images = Root / 'Exhibits/TextureStudio/Images'
CoreSources = ['Engine/ContentInterchange/' + Caption + '.cpp' for Caption in
               ['VectorAuthoringEditing', 'VectorAuthoringArrayPlacement', 'VectorAuthoringProperties', 'VectorAuthoringDocument']]
ContractSource = 'Exhibits/TextureStudio/Code/VectorAuthoringEditingValidation.cpp'
Includes = ['-I.', '-IExternalPackages/tomlpp/include']
Compiler = ['g++', '-std=c++20', '-O1', '-Wall', '-Wextra', '-Werror']
Commands = []


def Execute(Command, Caption, Environment=None):
    Commands.append({'caption': Caption, 'command': [str(Part) for Part in Command]})
    Execution = subprocess.run(Command, cwd=Root, env=Environment, text=True, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT)
    (Build / (Caption + '.log')).write_text(Execution.stdout)
    print(Execution.stdout, end='', flush=True)
    if Execution.returncode:
        raise RuntimeError(Caption + ' failed with exit code ' + str(Execution.returncode))
    return Execution.stdout


Report = {'platform': platform.platform(), 'python': sys.version.split()[0], 'windows_executed': False,
          'workspace_ui_integrated': False, 'hardware_gpu_tested': False, 'commands': Commands,
          'revision': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=Root, text=True).strip(),
          'uncommitted_sources': True}
Report['compiler'] = subprocess.check_output(['g++', '--version'], text=True).splitlines()[0]
Execute(Compiler + Includes + CoreSources + [ContractSource, '-o', str(Build / 'VectorAuthoringEditingValidation')], 'VectorAuthoringEditingBuild')
Report['source_contracts'] = json.loads(Execute([str(Build / 'VectorAuthoringEditingValidation')], 'VectorAuthoringEditingChecks'))
if Arguments.sanitizers:
    Execute(Compiler + ['-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'] + Includes + CoreSources +
            [ContractSource, '-o', str(Build / 'VectorAuthoringEditingSanitized')], 'VectorAuthoringEditingSanitizedBuild')
    Report['sanitizers'] = json.loads(Execute([str(Build / 'VectorAuthoringEditingSanitized')], 'VectorAuthoringEditingSanitizedChecks',
                                            dict(os.environ, ASAN_OPTIONS='detect_leaks=1')))
if Arguments.controls:
    ControlSources = ['UI/TextureStudio/VectorMaskControls.cpp'] + ['Engine/Interface/' + Caption + '.cpp' for Caption in
        ['PixelSpace', 'ControlKit', 'GlyphSpace']] + ['ExternalPackages/imgui/' + Caption + '.cpp' for Caption in
        ['imgui', 'imgui_draw', 'imgui_tables', 'imgui_widgets']]
    Execute(Compiler + ['-ffunction-sections', '-fdata-sections', '-DFRONTIER_EDITOR=1',
            '-Wno-missing-field-initializers', '-Wno-unused-function', '-IExternalPackages/imgui'] + Includes + CoreSources +
            ControlSources + ['Exhibits/TextureStudio/Code/VectorAuthoringDocumentValidation.cpp', '-Wl,--gc-sections',
            '-o', str(Build / 'VectorAuthoringDocumentValidation')], 'VectorAuthoringControlBuild')
    Execute([str(Build / 'VectorAuthoringDocumentValidation'), str(Build / 'NativeVectorAuthoringControlChecks.json')], 'VectorAuthoringControlChecks')
    Report['controls'] = json.loads((Build / 'NativeVectorAuthoringControlChecks.json').read_text())
    Execute(['python3', 'Exhibits/UIFoundation/Code/EncodeEvidence.py', str(Build / 'NativeVectorAuthoringControls.ppm'),
             str(Images / 'NativeVectorAuthoringLockedControls.png')], 'VectorAuthoringControlCapture')
if Arguments.gpu_driver:
    ShaderDirectory = Build / 'ShaderArchives'
    Execute(['node', 'Automation/CompileVectorCoverage.mjs', str(Arguments.slang_wasm.resolve()), str(ShaderDirectory)], 'VectorAuthoringShaderCompilation')
    Execute(Compiler + ['-DVK_NO_PROTOTYPES', '-Wno-missing-field-initializers', '-Wno-unused-function',
            '-IExternalPackages/vulkan-headers/include', '-IExternalPackages/stb'] + Includes + CoreSources +
            ['Engine/Render/SurfaceRasterization.cpp', 'Engine/Render/VectorCoverageRasterization.cpp',
             'Exhibits/TextureStudio/Code/VectorCoverageValidation.cpp', '-ldl', '-o', str(Build / 'VectorCoverageValidation')], 'VectorAuthoringGpuBuild')
    Evidence = Build / 'GpuEvidence'
    Execute([str(Build / 'VectorCoverageValidation'), str(Arguments.gpu_driver.resolve()),
             str(ShaderDirectory / 'VectorCoverageCompute.spv'), str(Evidence)], 'VectorAuthoringGpuChecks')
    Report['gpu'] = json.loads((Evidence / 'VectorCoverageChecks.json').read_text())
    for Caption in ['GpuNativeVectorAuthoringArray', 'GpuNativeVectorAuthoringMasks']:
        shutil.copy2(Evidence / (Caption + '.png'), Images / (Caption + '.png'))
if Arguments.windows_tools:
    Environment = dict(os.environ, PYTHONPATH=str(Arguments.windows_tools.resolve()))
    Toolchain = [sys.executable, '-m', 'ziglang']
    Version = subprocess.check_output(Toolchain + ['version'], env=Environment, text=True).strip()
    if Version != '0.13.0':
        raise RuntimeError('Expected the repository-reviewed ziglang 0.13.0 cross-toolchain.')
    Executable = Build / 'VectorAuthoringEditingValidation.exe'
    Execute(Toolchain + ['c++', '-target', 'x86_64-windows-gnu', '-std=c++20', '-O1', '-static'] + Includes +
            CoreSources + [ContractSource, '-o', str(Executable)], 'VectorAuthoringWindowsCrossLink', Environment)
    Payload = Executable.read_bytes()
    Offset = struct.unpack_from('<I', Payload, 0x3c)[0]
    if Payload[:2] != b'MZ' or Payload[Offset:Offset + 4] != b'PE\0\0' or struct.unpack_from('<H', Payload, Offset + 4)[0] != 0x8664:
        raise RuntimeError('Expected a linked Windows x64 PE executable.')
    Report['windows_cross_link'] = {'result': 'passed', 'toolchain': Version, 'machine': 'x86_64',
                                   'sha256': hashlib.sha256(Payload).hexdigest(), 'executed': False,
                                   'scope': 'headless authoring validation, not TextureWorkspace.exe'}
SourcePaths = CoreSources + [Caption.replace('.cpp', '.h') for Caption in CoreSources] + [ContractSource,
    'Engine/Render/VectorCoverage.h', 'Engine/Render/Shader/VectorCoverage.slang',
    'UI/TextureStudio/VectorMaskControls.cpp', 'Exhibits/TextureStudio/Code/VectorAuthoringDocumentValidation.cpp',
    'Exhibits/TextureStudio/Code/VectorCoverageValidation.cpp', 'CMakeLists.txt', 'Automation/VectorCoverage.cmake',
    'Automation/BuildTextureStudioWindows.py', 'Exhibits/TextureStudio/Code/VerifyVectorAuthoringEditing.py']
Report['source_sha256'] = {str(Source): hashlib.sha256((Root / Source).read_bytes()).hexdigest() for Source in SourcePaths}
Report['result'] = 'passed'
(Results / 'NativeVectorAuthoringEditingChecks.json').write_text(json.dumps(Report, indent=2) + '\n')
for Caption in ['VectorAuthoringEditingChecks', 'VectorAuthoringEditingSanitizedChecks', 'VectorAuthoringControlChecks', 'VectorAuthoringGpuChecks',
                'VectorAuthoringWindowsCrossLink', 'VectorAuthoringShaderCompilation']:
    Source = Build / (Caption + '.log')
    if Source.exists():
        shutil.copy2(Source, Results / ('Native' + Caption + '.log'))
print('PASS: retained native authoring report written; no Windows execution or completed workspace-port claim.')
