#============================================================================================================================================
#                                                      VERIFYMATERIALWORKSPACE.PY
#============================================================================================================================================
# 🧩 Execute docked ImGui composition with GPU atlas images; CPU capture is explicitly distinguished from native window presentation.

from pathlib import Path
import argparse
import hashlib
import json
import subprocess
from PrepareIconEvidence import PrepareIconEvidence

Root = Path(__file__).resolve().parents[3]
Parser = argparse.ArgumentParser()
Parser.add_argument('--destination', type=Path, required=True)
Parser.add_argument('--driver', type=Path, required=True)
Parser.add_argument('--shaders', type=Path, required=True)
Parser.add_argument('--icon-library', type=Path)
Arguments = Parser.parse_args()
Destination = Arguments.destination.resolve()
Destination.mkdir(parents=True, exist_ok=True)
IconLibrary = Arguments.icon_library.resolve() if Arguments.icon_library else PrepareIconEvidence(Destination / 'Icons')
Sources = ['Engine/Interface/IconArt.cpp', 'Engine/Interface/ContentDirectory.cpp', 'Engine/Render/MaterialThumbnailAtlas.cpp', 'Engine/Render/MaterialLibraryRendering.cpp',
           'Engine/Render/SurfaceRasterization.cpp', 'Engine/Render/SurfaceMaterial.cpp',
           'Engine/ContentInterchange/MaterialLibrary.cpp', 'Engine/Interface/TextFileReplacement.cpp',
           'Engine/Interface/PixelSpace.cpp', 'Engine/Interface/ControlKit.cpp', 'Engine/Interface/PanelDockspace.cpp',
           'Engine/Interface/PanelDockspaceLayout.cpp', 'Engine/Interface/PanelDockspaceMonitor.cpp',
           'Engine/Interface/MonitorPlacement.cpp', 'UI/TextureStudio/MaterialLibraryPanel.cpp',
           'Exhibits/TextureStudio/Code/MaterialWorkspaceEvidence.cpp']
Sources += ['ExternalPackages/imgui/' + Name for Name in ['imgui.cpp', 'imgui_draw.cpp', 'imgui_widgets.cpp', 'imgui_tables.cpp']]
Binary = Destination / 'MaterialWorkspaceEvidence'
Commands = [['g++', '-std=c++20', '-O1', '-DVK_NO_PROTOTYPES', '-DFRONTIER_EDITOR=1',
             '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections', '-Wall', '-Wextra', '-Werror',
             '-Wno-missing-field-initializers', '-Wno-unused-function', '-I.', '-IExternalPackages/imgui',
             '-IExternalPackages/vulkan-headers/include', '-IExternalPackages/thorvg/inc', '-DTVG_STATIC', '-IExternalPackages/tomlpp/include', '-IExternalPackages/stb']
            + Sources + [str(IconLibrary), '-pthread', '-ldl', '-o', str(Binary)],
            [str(Binary), str(Arguments.driver.resolve()), str(Arguments.shaders.resolve()),
             str(Root / 'EngineContent/MaterialArchives/TextureStudio'), str(Destination)],
            ['python3', 'Exhibits/UIFoundation/Code/EncodeEvidence.py', str(Destination / 'MaterialWorkspace.ppm'),
             str(Root / 'Exhibits/TextureStudio/Images/MaterialWorkspace.png')]]
Executions = []
for Command in Commands:
    Completed = subprocess.run(Command, cwd=Root, capture_output=True, text=True, timeout=240)
    print(Completed.stdout, end='', flush=True)
    print(Completed.stderr, end='', flush=True)
    Executions.append({'command': Command, 'exit_code': Completed.returncode,
                       'stdout': Completed.stdout, 'stderr': Completed.stderr})
    if Completed.returncode:
        raise RuntimeError('Material workspace composition check failed')
Report = json.loads((Destination / 'MaterialWorkspaceChecks.json').read_text())
Report['executions'] = Executions
Sources = [Source for Source in Sources if not Source.startswith('ExternalPackages/')]
Sources += ['UI/TextureStudio/MaterialLibraryPanel.h', 'Engine/Interface/PanelDockspace.h',
            'Exhibits/TextureStudio/Code/RasterizeLibraryEvidence.h', 'Exhibits/TextureStudio/Code/VerifyMaterialWorkspace.py']
Sources += ['Engine/Render/MaterialPreviewGeometry.h', 'Engine/ContentInterchange/MaterialLibrary.h',
            'Engine/Interface/ControlKit.h', 'Automation/FontSources.json', 'Automation/PrepareFonts.py',
            'Exhibits/TextureStudio/Code/ExportReferenceLibrary.mjs']
Sources += [File.relative_to(Root).as_posix() for File in (Root / 'EngineContent/MaterialArchives/TextureStudio').iterdir() if File.is_file()]
Report['sources'] = [{'file': Source, 'sha256': hashlib.sha256((Root / Source).read_bytes()).hexdigest()} for Source in Sources]
(Root / 'Exhibits/TextureStudio/Results/MaterialWorkspaceChecks.json').write_text(json.dumps(Report, indent=2) + '\n')
