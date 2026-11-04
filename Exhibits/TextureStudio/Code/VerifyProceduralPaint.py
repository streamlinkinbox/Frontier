#============================================================================================================================================
#                                                      VERIFYPROCEDURALPAINT.PY
#============================================================================================================================================
# 🧩 Automatically execute strict/sanitized authored paint, real GPU sensitivity and scoped production SVG checks.

from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
from PrepareIconEvidence import PrepareIconEvidence

RepositoryRoot = Path(__file__).resolve().parents[3]


def Main():
    Parser = argparse.ArgumentParser()
    Parser.add_argument('--destination', type=Path, required=True)
    Parser.add_argument('--driver', type=Path, required=True)
    Parser.add_argument('--shaders', type=Path, required=True)
    Parser.add_argument('--icon-library', type=Path)
    Arguments = Parser.parse_args()
    Destination = Arguments.destination.resolve()
    Destination.mkdir(parents=True, exist_ok=True)
    IconLibrary = Arguments.icon_library.resolve() if Arguments.icon_library else PrepareIconEvidence(Destination / 'Icons')
    Environment = dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1')
    Executions = {}

    def Execute(Name, Command):
        Completed = subprocess.run([str(Term) for Term in Command], cwd=RepositoryRoot, env=Environment,
                                   text=True, capture_output=True, timeout=300)
        print(Completed.stdout, end='', flush=True)
        print(Completed.stderr, end='', flush=True)
        Executions[Name] = {'command': [str(Term) for Term in Command], 'exit_code': Completed.returncode,
                            'stdout': Completed.stdout, 'stderr': Completed.stderr}
        if Completed.returncode:
            raise RuntimeError(Name + ' failed')

    Prefix = ['g++', '-std=c++20', '-O1', '-I.', '-IExternalPackages/vulkan-headers/include',
              '-IExternalPackages/tomlpp/include', '-IExternalPackages/stb', '-IExternalPackages/thorvg/inc',
              '-DVK_NO_PROTOTYPES', '-DTVG_STATIC']
    Sources = ['Engine/Render/SurfaceRasterization.cpp', 'Engine/Render/SurfaceMaterial.cpp',
               'Engine/ContentInterchange/MaterialLibrary.cpp', 'Engine/Interface/TextFileReplacement.cpp',
               'Exhibits/TextureStudio/Code/AutomotivePaintValidation.cpp']
    IconSources = ['Engine/Interface/IconArt.cpp', 'Engine/Interface/ContentDirectory.cpp',
                   'Exhibits/TextureStudio/Code/IconDomainValidation.cpp']
    for Sanitized in (False, True):
        Label = 'sanitized' if Sanitized else 'strict'
        Output = Destination / Label
        Output.mkdir(exist_ok=True)
        Options = ['-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if Sanitized else [
                   '-Wall', '-Wextra', '-Werror', '-Wno-missing-field-initializers']
        PaintBinary = Output / 'AutomotivePaintValidation'
        Execute(Label + '_paint_build', Prefix + Options + Sources + ['-ldl', '-o', PaintBinary])
        Execute(Label + '_paint_execution', [PaintBinary, Arguments.driver.resolve(), Arguments.shaders.resolve(),
                RepositoryRoot / 'EngineContent/MaterialArchives/TextureStudio', Output])
        IconBinary = Output / 'IconDomainValidation'
        Execute(Label + '_icon_build', Prefix + Options + IconSources + [IconLibrary, '-pthread', '-o', IconBinary])
        Execute(Label + '_icon_execution', [IconBinary, Output / 'IconDomainChecks.json'])
    Images = RepositoryRoot / 'Exhibits/TextureStudio/Images'
    for Name in ('GreenAutomotivePaint', 'Disabled', 'Coverage', 'ParticleSize', 'OrangePeel'):
        Execute('encode_' + Name, ['python3', 'Exhibits/UIFoundation/Code/EncodeEvidence.py',
                Destination / 'strict' / (Name + '.ppm'), Images / ('Paint-' + Name + '.png')])
    Sources += IconSources + ['Engine/Render/AutomotivePaint.h', 'Engine/Render/SurfaceMaterial.h',
                'Engine/Render/MaterialPreviewGeometry.h', 'Engine/Render/Shader/AutomotivePaint.slang',
                'Engine/Render/Shader/MaterialSurface.slang', 'Engine/Render/Shader/ScatteringEvaluation.slang',
                'Engine/Interface/IconArt.h', 'Engine/Interface/IconPresentation.cpp', 'Engine/Interface/IconPresentation.h',
                'Engine/Interface/IconSymbols.inc', 'Engine/Interface/Icons/EditorSymbols.inc',
                'Engine/Interface/Icons/TextureSymbols.inc', 'Engine/Interface/Icons/FluidSymbols.inc',
                'Exhibits/TextureStudio/Code/VerifyProceduralPaint.py', 'Exhibits/TextureStudio/Code/PrepareIconEvidence.py']
    Sources += [Source.relative_to(RepositoryRoot).as_posix() for Directory in ('Editor', 'Texture', 'Fluid')
                for Source in sorted((RepositoryRoot / 'EngineContent/Icons' / Directory).glob('*.svg'))]
    Report = {'result': 'passed', 'paint': json.loads((Destination / 'strict/AutomotivePaintChecks.json').read_text()),
              'icons': json.loads((Destination / 'strict/IconDomainChecks.json').read_text()), 'executions': Executions,
              'limitations': ['Offscreen Vulkan software-driver execution, not Windows or hardware execution',
                              'External-driver leak detection disabled; authored CPU/renderer code uses ASan/UBSan',
                              'Other HTML procedural shader families remain unported'],
              'sources': [{'file': Source, 'sha256': hashlib.sha256((RepositoryRoot / Source).read_bytes()).hexdigest()}
                          for Source in sorted(set(Sources))]}
    (RepositoryRoot / 'Exhibits/TextureStudio/Results/ProceduralPaintChecks.json').write_text(json.dumps(Report, indent=2) + '\n')
    print('PASS: strict/sanitized automotive paint GPU and icon-domain checks', flush=True)


if __name__ == '__main__':
    Main()
