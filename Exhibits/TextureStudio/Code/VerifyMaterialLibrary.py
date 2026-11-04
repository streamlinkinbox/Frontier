#============================================================================================================================================
#                                                       VERIFYMATERIALLIBRARY.PY
#============================================================================================================================================
# 🧩 Reproduce material IO/ImGui checks, sanitizers, CPU draw-command capture and optional Windows source compilation without downloads.

from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import sys

Root = Path(__file__).resolve().parents[3]


def Main():
    Parser = argparse.ArgumentParser()
    Parser.add_argument('--destination', type=Path, required=True)
    Parser.add_argument('--windows-tools', type=Path)
    Arguments = Parser.parse_args()
    Destination = Arguments.destination.resolve()
    Destination.mkdir(parents=True, exist_ok=True)
    Environment = dict(os.environ, ASAN_OPTIONS='detect_leaks=1', UBSAN_OPTIONS='halt_on_error=1')
    Executions = {}

    def Execute(Name, Command):
        Completed = subprocess.run([str(Argument) for Argument in Command], cwd=Root, env=Environment,
                                   text=True, capture_output=True, timeout=240)
        print(Completed.stdout, end='', flush=True)
        print(Completed.stderr, end='', file=sys.stderr, flush=True)
        Executions[Name] = {'command': [str(Argument) for Argument in Command], 'exit_code': Completed.returncode,
                            'stdout': Completed.stdout, 'stderr': Completed.stderr}
        if Completed.returncode:
            raise RuntimeError(Name + ' failed')
        return Completed.stdout

    Sources = ['Engine/ContentInterchange/MaterialLibrary.cpp', 'Engine/Render/SurfaceMaterial.cpp',
               'Engine/Interface/TextFileReplacement.cpp', 'Engine/Interface/ControlKit.cpp',
               'UI/TextureStudio/MaterialLibraryPanel.cpp', 'Engine/Interface/PixelSpace.cpp',
               'Engine/Interface/PanelDockspace.cpp', 'Engine/Interface/PanelDockspaceLayout.cpp',
               'Engine/Interface/PanelDockspaceMonitor.cpp', 'Engine/Interface/MonitorPlacement.cpp']
    Sources += ['ExternalPackages/imgui/' + Name for Name in ['imgui.cpp', 'imgui_draw.cpp', 'imgui_tables.cpp', 'imgui_widgets.cpp']]
    Sources += ['Exhibits/TextureStudio/Code/MaterialLibraryValidation.cpp']
    Prefix = ['g++', '-std=c++20', '-DFRONTIER_EDITOR=1', '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
              '-I.', '-IExternalPackages/imgui', '-IExternalPackages/tomlpp/include', '-IExternalPackages/stb']
    LastOutput = None
    for Sanitized in [False, True]:
        Label = 'sanitized' if Sanitized else 'strict'
        Options = (['-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if Sanitized else
                   ['-Wall', '-Wextra', '-Werror', '-Wno-missing-field-initializers', '-Wno-unused-function'])
        Binary = Destination / ('MaterialLibraryValidation-' + Label)
        Execute(Label + '_build', Prefix + Options + Sources + ['-o', Binary])
        Stdout = Execute(Label + '_execution', [Binary, Root / 'Exhibits/Renderer/Materials', Destination / Label])
        ReportPath = Path(next(Line.removeprefix('REPORT ') for Line in Stdout.splitlines() if Line.startswith('REPORT ')))
        Checks = json.loads(ReportPath.read_text())
        assert Checks['result'] == 'passed'
        if not Sanitized:
            LastOutput = ReportPath.parent
    WindowsObjects = []
    if Arguments.windows_tools:
        Environment['PYTHONPATH'] = str(Arguments.windows_tools.resolve())
        Includes = ['-I.', '-IExternalPackages/imgui', '-IExternalPackages/imgui/backends',
                    '-IExternalPackages/glfw/include', '-IExternalPackages/vulkan-headers/include',
                    '-IExternalPackages/tomlpp/include', '-IExternalPackages/thorvg/inc']
        WindowsSources = ['Engine/Interface/WindowsApplication.cpp', 'Engine/Interface/VulkanPresentation.cpp',
                          'Exhibits/UIFoundation/Code/WindowsUI.cpp',
                          'Experimental/TextureStudio/Native/TextureStudioApplication.cpp',
                          'UI/TextureStudio/MaterialLibraryPanel.cpp', 'Engine/ContentInterchange/MaterialLibrary.cpp',
                          'UI/Editor/ControlCentrePanel.cpp', 'Engine/Render/MaterialThumbnailAtlas.cpp',
                          'Engine/Render/MaterialLibraryRendering.cpp']
        for Source in WindowsSources:
            Binary = Destination / (Path(Source).stem + '.obj')
            Execute('windows_object_' + Path(Source).stem, [sys.executable, '-m', 'ziglang', 'c++', '-target',
                    'x86_64-windows-gnu', '-std=c++20', '-DFRONTIER_EDITOR=1', '-DFRONTIER_UI_VULKAN=1',
                    '-DVK_NO_PROTOTYPES', '-DNOMINMAX'] + Includes + ['-c', Source, '-o', Binary])
            assert Binary.read_bytes()[:2] == b'\x64\x86', 'Expected x64 COFF object, not an executable'
            WindowsObjects.append({'source': Source, 'bytes': Binary.stat().st_size})
        for Source in ['Engine/Interface/WindowsApplication.cpp', 'Exhibits/UIFoundation/Code/WindowsUI.cpp']:
            Execute('dx11_object_' + Path(Source).stem, [sys.executable, '-m', 'ziglang', 'c++', '-target',
                    'x86_64-windows-gnu', '-std=c++20', '-DFRONTIER_EDITOR=1', '-DFRONTIER_UI_VULKAN=0',
                    '-DNOMINMAX'] + Includes + ['-c', Source, '-o', Destination / (Path(Source).stem + '-DX11.obj')])
    Results = Root / 'Exhibits/TextureStudio/Results'
    Images = Root / 'Exhibits/TextureStudio/Images'
    Images.mkdir(parents=True, exist_ok=True)
    Execute('cpu_capture_encoding', [sys.executable, 'Exhibits/UIFoundation/Code/EncodeEvidence.py',
                                    LastOutput / 'MaterialLibrary.ppm', Images / 'MaterialLibrary.png'])
    Sources = [Source for Source in Sources if not Source.startswith('ExternalPackages/')]
    Sources += ['Engine/ContentInterchange/MaterialLibrary.h', 'UI/TextureStudio/MaterialLibraryPanel.h',
                'UI/Editor/ControlCentrePanel.h', 'UI/Editor/ControlCentrePanel.cpp',
                'Engine/Interface/WindowsApplication.h', 'Engine/Interface/WindowsApplication.cpp',
                'Engine/Interface/VulkanPresentation.h', 'Engine/Interface/VulkanPresentation.cpp',
                'Exhibits/UIFoundation/Code/WindowsUI.cpp', 'Experimental/TextureStudio/Native/TextureStudioApplication.cpp',
                'Exhibits/TextureStudio/Code/VerifyMaterialLibrary.py',
                'Exhibits/TextureStudio/Code/RasterizeLibraryEvidence.h', 'Engine/Interface/PanelDockspace.h',
                'Engine/Render/MaterialThumbnailAtlas.h', 'Engine/Render/MaterialThumbnailAtlas.cpp',
                'Engine/Render/MaterialLibraryRendering.h', 'Engine/Render/MaterialLibraryRendering.cpp',
                'Engine/Render/MaterialPreviewGeometry.h', 'Automation/SurfaceRasterization.cmake', 'CMakeLists.txt']
    Sources += ['Automation/FontSources.json', 'Automation/PrepareFonts.py', 'Engine/Interface/ControlKit.h',
                'Exhibits/TextureStudio/Code/ExportReferenceLibrary.mjs']
    Sources += [File.relative_to(Root).as_posix() for File in (Root / 'EngineContent/MaterialArchives/TextureStudio').iterdir() if File.is_file()]
    Report = {'result': 'passed', 'checks': Checks['checks'], 'native_imgui_cpu_execution': True,
              'native_window_execution': False, 'standalone_executable_linked': False, 'windows_execution': False,
              'capture': {'file': 'Exhibits/TextureStudio/Images/MaterialLibrary.png',
                          'execution': 'CPU rasterization of actual ImGui draw commands and font atlas; not a Windows screenshot'},
              'windows_objects': WindowsObjects,
              'warning_exception': 'Unused pre-existing local FillCircle in ControlKit.cpp; no new code exemption.',
              'executions': Executions,
              'sources': [{'file': Source, 'sha256': hashlib.sha256((Root / Source).read_bytes()).hexdigest()}
                          for Source in sorted(set(Sources))]}
    Results.mkdir(parents=True, exist_ok=True)
    (Results / 'MaterialLibraryChecks.json').write_text(json.dumps(Report, indent=2) + '\n')
    print('PASS: library/source/ImGui/sanitizer checks. Windows objects are compilation evidence, not linked/executed applications.')


if __name__ == '__main__':
    Main()
