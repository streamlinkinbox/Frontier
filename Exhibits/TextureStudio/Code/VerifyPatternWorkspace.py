#============================================================================================================================================
#                                                        VERIFYPATTERNWORKSPACE.PY
#============================================================================================================================================
# 🧩 Build and exercise production native Pattern UI, retaining real pointer/GPU/capture evidence with source and toolchain provenance.

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess

Root = Path(__file__).resolve().parents[3]
Parser = argparse.ArgumentParser()
Parser.add_argument('--driver', type=Path, required=True)
Parser.add_argument('--shader', type=Path, required=True)
Parser.add_argument('--font', type=Path, default=Root / 'EngineContent/FontArchives/DMSans/DMSans-Regular.ttf')
Parser.add_argument('--build-directory', type=Path, default=Root / '_ClaudeScratch/build/PatternWorkspace')
Parser.add_argument('--sanitizers', action='store_true')
Parser.add_argument('--windows-tools', type=Path)
Arguments = Parser.parse_args()
if hasattr(os, 'sched_getaffinity'):
    os.sched_setaffinity(0, set(sorted(os.sched_getaffinity(0))[:4]))
Build = Arguments.build_directory.resolve()
Build.mkdir(parents=True, exist_ok=True)
Production = ['Engine/ContentInterchange/' + Caption + '.cpp' for Caption in
    ['VectorAuthoringDocument', 'VectorAuthoringEditing', 'VectorAuthoringArrayPlacement', 'VectorAuthoringProperties', 'VectorAuthoringSelectionEditing', 'CubicPathEditing']]
Production += ['Engine/Interface/' + Caption + '.cpp' for Caption in
    ['VectorAuthoringControls', 'PixelSpace', 'ControlKit', 'GlyphSpace', 'PanelDockspace', 'PanelDockspaceLayout', 'PanelDockspaceMonitor', 'MonitorPlacement']]
Production += ['UI/TextureStudio/PatternWorkspacePanel.cpp', 'UI/TextureStudio/VectorMaskControls.cpp',
    'Engine/Render/SurfaceRasterization.cpp', 'Engine/Render/VectorCoverageRasterization.cpp', 'Engine/Render/VectorAuthoringCoverageRendering.cpp', 'Engine/Render/VectorSelectionEvaluation.cpp']
Sources = Production + ['ExternalPackages/imgui/' + Caption + '.cpp' for Caption in
    ['imgui', 'imgui_draw', 'imgui_tables', 'imgui_widgets']] + ['Exhibits/TextureStudio/Code/PatternWorkspaceValidation.cpp']
Flags = ['-std=c++20', '-O1', '-ffunction-sections', '-fdata-sections', '-DFRONTIER_EDITOR=1', '-DVK_NO_PROTOTYPES',
    '-Wall', '-Wextra', '-Werror', '-Wno-missing-field-initializers', '-Wno-unused-function',
    '-I.', '-IExternalPackages/imgui', '-IExternalPackages/tomlpp/include', '-IExternalPackages/vulkan-headers/include', '-IExternalPackages/stb']
if Arguments.sanitizers:
    Flags += ['-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
Headers = sorted({*Root.glob('Engine/**/*.h'), *Root.glob('UI/**/*.h'), *Root.glob('Exhibits/TextureStudio/Code/*.h'),
                  *Root.glob('ExternalPackages/imgui/*.h')})
HeaderDigest = hashlib.sha256(b''.join(Header.read_bytes() for Header in Headers)).digest()
Commands = []
Objects = []
for Source in Sources:
    # Keep sanitizer instrumentation; avoid GCC's expensive variable-tracking retry in the large input fixture.
    SourceFlags = Flags + (['-g1', '-fno-var-tracking-assignments'] if Arguments.sanitizers and Source == Sources[-1] else [])
    Digest = hashlib.sha256((Root / Source).read_bytes() + HeaderDigest + json.dumps(SourceFlags).encode()).hexdigest()
    ObjectPath = Build / (Path(Source).stem + '-' + Digest[:16] + '.o')
    Command = ['g++'] + SourceFlags + ['-c', Source, '-o', str(ObjectPath)]
    Commands.append(Command)
    if not ObjectPath.exists():
        subprocess.run(Command, cwd=Root, check=True)
    Objects.append(str(ObjectPath))
Executable = Build / ('PatternWorkspaceSanitized' if Arguments.sanitizers else 'PatternWorkspaceValidation')
Command = ['g++'] + Flags + Objects + ['-Wl,--gc-sections', '-ldl', '-o', str(Executable)]
Commands.append(Command)
subprocess.run(Command, cwd=Root, check=True)
Command = [str(Executable), str(Arguments.driver.resolve()), str(Arguments.shader.resolve()), str(Arguments.font.resolve()), str(Build)]
Commands.append(Command)
Execution = subprocess.run(Command, cwd=Root, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           env=dict(os.environ, ASAN_OPTIONS='detect_leaks=1'))
(Build / 'PatternWorkspaceChecks.log').write_text(Execution.stdout)
print(Execution.stdout, end='', flush=True)
if Execution.returncode:
    raise SystemExit(Execution.returncode)
Results = Root / 'Exhibits/TextureStudio/Results'
Report = json.loads((Build / 'NativePatternWorkspaceChecks.json').read_text())
Report['platform'] = platform.platform()
Report['compiler'] = subprocess.check_output(['g++', '--version'], text=True).splitlines()[0]
Report['sanitizers'] = Arguments.sanitizers
Report['commands'] = Commands
Report['revision'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=Root, text=True).strip()
Report['uncommitted_sources'] = True
Report['source_sha256'] = {Source: hashlib.sha256((Root / Source).read_bytes()).hexdigest() for Source in Production + [Sources[-1], 'Experimental/TextureStudio/Native/TextureStudioApplication.cpp', 'UI/TextureStudio/MaterialLibraryPanel.cpp', 'CMakeLists.txt', 'Automation/VectorCoverage.cmake', 'Automation/BuildTextureStudioWindows.py', 'Automation/VectorShaderArchives.py', 'Automation/PackageWorkspacesWindows.py', 'Engine/Render/Shader/VectorCoverage.slang', 'Automation/CompileVectorCoverage.mjs', 'Exhibits/TextureStudio/Code/VerifyPatternWorkspace.py']}
Report['header_sha256'] = {str(Header.relative_to(Root)): hashlib.sha256(Header.read_bytes()).hexdigest() for Header in Headers if 'ExternalPackages' not in Header.parts}
Report['font_sha256'] = hashlib.sha256(Arguments.font.read_bytes()).hexdigest()
Report['vector_compilation_sha256'] = hashlib.sha256(Arguments.shader.with_name('VectorCompilation.json').read_bytes()).hexdigest()
Report['selection_shader_sha256'] = hashlib.sha256(Arguments.shader.with_name('VectorSelectionCompute.spv').read_bytes()).hexdigest()
Report['driver_sha256'] = hashlib.sha256(Arguments.driver.read_bytes()).hexdigest()
Report['shader_sha256'] = hashlib.sha256(Arguments.shader.read_bytes()).hexdigest()
for Caption in ['NativePatternWorkspace', 'NativePatternArrayInspector', 'NativePatternOwnedMask', 'NativePatternMarquee', 'NativePatternResize', 'NativePatternRotation', 'NativePatternContourPicking', 'NativePatternPointEditing', 'NativePatternPenDraft', 'NativePatternPenEditing', 'NativePatternRefinement', 'NativePatternContinuation', 'NativePatternJoining', 'NativePatternSegmentEditing', 'NativePatternMultipoint', 'NativePatternPenPlacement', 'NativePatternPenCorner', 'NativePatternIndependentEndpoint', 'NativePatternEndpointJoin', 'NativePatternClipboard', 'NativePatternClipboardMenu', 'NativeDropdownAddMotif', 'NativeDropdownMoreTools', 'NativeDropdownDocument', 'NativeDropdownArray']:
    Destination = Root / 'Exhibits/TextureStudio/Images' / (Caption + '.png')
    subprocess.run(['python3', 'Exhibits/UIFoundation/Code/EncodeEvidence.py', str(Build / (Caption + '.ppm')), str(Destination)], cwd=Root, check=True)
if Arguments.windows_tools:
    Environment = dict(os.environ, PYTHONPATH=str(Arguments.windows_tools.resolve()))
    Version = subprocess.check_output(['python3', '-m', 'ziglang', 'version'], cwd=Root, env=Environment, text=True).strip()
    if Version != '0.13.0':
        raise RuntimeError('Expected repository-reviewed Zig 0.13.0.')
    WindowSources = Production + ['UI/TextureStudio/MaterialLibraryPanel.cpp', 'Experimental/TextureStudio/Native/TextureStudioApplication.cpp']
    WindowsDirectory = Build / 'Windows'
    WindowsDirectory.mkdir(exist_ok=True)
    WindowsFlags = ['-target', 'x86_64-windows-gnu', '-std=c++20', '-O1', '-DNOMINMAX', '-DWIN32_LEAN_AND_MEAN',
        '-DFRONTIER_EDITOR=1', '-DFRONTIER_UI_VULKAN=1', '-DVK_NO_PROTOTYPES', '-I.', '-IExternalPackages/imgui',
        '-IExternalPackages/imgui/backends', '-IExternalPackages/tomlpp/include', '-IExternalPackages/vulkan-headers/include',
        '-IExternalPackages/glfw/include', '-IExternalPackages/stb']
    WindowsObjects = []
    for Source in WindowSources:
        Digest = hashlib.sha256((Root / Source).read_bytes() + HeaderDigest + json.dumps(WindowsFlags).encode()).hexdigest()
        Destination = WindowsDirectory / (Path(Source).stem + '-' + Digest[:16] + '.obj')
        Command = ['python3', '-m', 'ziglang', 'c++'] + WindowsFlags + ['-c', Source, '-o', str(Destination)]
        Commands.append(Command)
        if not Destination.exists():
            subprocess.run(Command, cwd=Root, env=Environment, check=True)
        WindowsObjects.append({'source': Source, 'sha256': hashlib.sha256(Destination.read_bytes()).hexdigest()})
    Report['windows_objects'] = {'result': 'passed', 'toolchain': Version, 'objects': WindowsObjects, 'application_linked': False, 'executed': False}
ReportPath = Results / ('NativePatternWorkspaceSanitizedChecks.json' if Arguments.sanitizers else 'NativePatternWorkspaceChecks.json')
ReportPath.write_text(json.dumps(Report, indent=2) + '\n')
(Results / ('NativePatternWorkspaceSanitized.log' if Arguments.sanitizers else 'NativePatternWorkspace.log')).write_text(Execution.stdout)
print('PASS: real native Pattern UI input and GPU-backed capture checks; full 1:1 parity is still open.')
