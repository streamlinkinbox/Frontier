#============================================================================================================================================
#                                                    VERIFYNATIVECONTROLSURFACES.PY
#============================================================================================================================================
# 🧩 Capture C++ material controls and actual Vulkan preview changes using explicitly supplied local build products.

import argparse
import json
from pathlib import Path
import subprocess

Root = Path(__file__).resolve().parents[3]
Parser = argparse.ArgumentParser()
Parser.add_argument('--build-directory', type=Path, required=True)
Parser.add_argument('--driver', type=Path, required=True)
Parser.add_argument('--shaders', type=Path, required=True)
Parser.add_argument('--destination', type=Path, required=True)
Arguments = Parser.parse_args()
Build = Arguments.build_directory.resolve()
Destination = Arguments.destination.resolve()
Destination.mkdir(parents=True, exist_ok=True)
Binary = Destination / 'MaterialControlSurfaceEvidence'
Libraries = ['FrontierTextureStudioUI', 'FrontierVectorAuthoring', 'FrontierMaterialLibrary', 'FrontierSurfaceRasterization',
             'FrontierInterface', 'FrontierTextFiles', 'FrontierImGui', 'thorvg_static']
Commands = [
    ['g++', '-std=c++20', '-O1', '-DFRONTIER_EDITOR=1', '-DVK_NO_PROTOTYPES', '-DTVG_STATIC',
     '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-missing-field-initializers',
     '-I.', '-IExternalPackages/imgui', '-IExternalPackages/vulkan-headers/include', '-IExternalPackages/tomlpp/include',
     'Exhibits/TextureStudio/Code/MaterialControlSurfaceEvidence.cpp',
     *[str(Build / ('lib' + Library + '.a')) for Library in Libraries], '-pthread', '-ldl', '-o', str(Binary)],
    [str(Binary), str(Arguments.driver.resolve()), str(Arguments.shaders.resolve()),
     str(Root / 'EngineContent/MaterialArchives/TextureStudio'), str(Destination)],
]
for Command in Commands:
    subprocess.run(Command, cwd=Root, check=True)
for Caption in ['NativeFlakePalette', 'NativeDropdown', 'NativeFlakeRamp', 'NativeFlakeSingle', 'NativeColourWheel']:
    subprocess.run(['python3', 'Exhibits/UIFoundation/Code/EncodeEvidence.py', str(Destination / (Caption + '.ppm')),
                    str(Root / 'Exhibits/TextureStudio/Images' / (Caption + '.png'))], cwd=Root, check=True)
Report = json.loads((Destination / 'NativeControlSurfaceChecks.json').read_text())
(Root / 'Exhibits/TextureStudio/Results/NativeControlSurfaceChecks.json').write_text(json.dumps(Report, indent=2) + '\n')
