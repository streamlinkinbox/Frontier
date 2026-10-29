#============================================================================================================================================
#                                                        VERIFYMATERIALATLAS.PY
#============================================================================================================================================
# 🧩 Compile and execute shared Vulkan material atlas validation with an explicit local driver; no downloads or window claims.

from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess

Root = Path(__file__).resolve().parents[3]
Parser = argparse.ArgumentParser()
Parser.add_argument('--destination', type=Path, required=True)
Parser.add_argument('--driver', type=Path, required=True)
Parser.add_argument('--shaders', type=Path, required=True)
Arguments = Parser.parse_args()
Destination = Arguments.destination.resolve()
Destination.mkdir(parents=True, exist_ok=True)
Sources = ['Engine/Render/MaterialThumbnailAtlas.cpp', 'Engine/Render/MaterialLibraryRendering.cpp',
           'Engine/Render/SurfaceRasterization.cpp', 'Engine/Render/SurfaceMaterial.cpp',
           'Exhibits/TextureStudio/Code/MaterialAtlasValidation.cpp']
Executions = {}

def Execute(Name, Command, Environment=None):
    Completed = subprocess.run([str(Argument) for Argument in Command], cwd=Root, env=Environment,
                               capture_output=True, text=True, timeout=240)
    print(Completed.stdout, end='', flush=True)
    print(Completed.stderr, end='', flush=True)
    Executions[Name] = {'command': [str(Argument) for Argument in Command], 'exit_code': Completed.returncode,
                        'stdout': Completed.stdout, 'stderr': Completed.stderr}
    if Completed.returncode:
        raise RuntimeError(Name + ' failed')

Prefix = ['g++', '-std=c++20', '-O1', '-DVK_NO_PROTOTYPES', '-I.', '-IExternalPackages/vulkan-headers/include',
          '-IExternalPackages/tomlpp/include', '-IExternalPackages/stb']
for Sanitized in [False, True]:
    Label = 'sanitized' if Sanitized else 'strict'
    Binary = Destination / ('MaterialAtlasValidation-' + Label)
    Options = ['-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if Sanitized else [
        '-Wall', '-Wextra', '-Werror', '-Wno-missing-field-initializers']
    Execute(Label + '_build', Prefix + Options + Sources + ['-ldl', '-o', Binary])
    Environment = dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1')
    Execute(Label + '_execution', [Binary, Arguments.driver.resolve(), Arguments.shaders.resolve(), Destination / Label], Environment)
Report = json.loads((Destination / 'strict/MaterialAtlasChecks.json').read_text())
Report['executions'] = Executions
Report['limitations'] = ['Explicit Vulkan driver execution, not native Windows window execution',
                         'ASan/UBSan executed; external driver leak detection disabled',
                         'No hardware or validation-layer acceptance is inferred']
Sources += ['Engine/Render/MaterialThumbnailAtlas.h', 'Engine/Render/MaterialLibraryRendering.h',
            'Engine/Render/MaterialPreviewGeometry.h', 'Exhibits/TextureStudio/Code/AtlasReadback.h',
            'Exhibits/TextureStudio/Code/VerifyMaterialAtlas.py']
Sources += ['Engine/Render/MaterialPreviewGeometry.h', 'Engine/Interface/ControlKit.h']
Report['sources'] = [{'file': Source, 'sha256': hashlib.sha256((Root / Source).read_bytes()).hexdigest()} for Source in Sources]
Images = Root / 'Exhibits/TextureStudio/Images'
Images.mkdir(parents=True, exist_ok=True)
Execute('encode', ['python3', 'Exhibits/UIFoundation/Code/EncodeEvidence.py', Destination / 'strict/MaterialAtlas.ppm', Images / 'MaterialAtlas.png'])
(Root / 'Exhibits/TextureStudio/Results/MaterialAtlasChecks.json').write_text(json.dumps(Report, indent=2) + '\n')
print('PASS: strict/sanitized material atlas execution and pixel evidence')
