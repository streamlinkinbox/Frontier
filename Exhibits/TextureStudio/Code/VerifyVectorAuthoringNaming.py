#============================================================================================================================================
#                                                    VERIFYVECTORAUTHORINGNAMING.PY
#============================================================================================================================================
# 🧩 Verify renamed native translation units and preserve source hashes without changing serialized document tags.

import hashlib
import json
from pathlib import Path
import subprocess

Root = Path(__file__).resolve().parents[3]
Sources = [str(Path('Engine/ContentInterchange') / (Caption + '.cpp')) for Caption in
           ['VectorAuthoringDocument', 'VectorAuthoringEditing', 'VectorAuthoringArrayPlacement',
            'VectorAuthoringProperties', 'VectorAuthoringSelectionEditing']]
Sources += ['Engine/Interface/VectorAuthoringControls.cpp', 'Engine/Render/VectorAuthoringCoverageRendering.cpp',
            'UI/TextureStudio/VectorMaskControls.cpp', 'UI/TextureStudio/PatternWorkspacePanel.cpp',
            'Exhibits/TextureStudio/Code/PatternWorkspaceValidation.cpp',
            'Exhibits/TextureStudio/Code/VectorAuthoringDocumentValidation.cpp',
            'Exhibits/TextureStudio/Code/VectorCoverageValidation.cpp']
Flags = ['-std=c++20', '-DFRONTIER_EDITOR=1', '-DVK_NO_PROTOTYPES', '-Wall', '-Wextra', '-Werror',
         '-Wno-missing-field-initializers', '-Wno-unused-function', '-I.', '-IExternalPackages/imgui',
         '-IExternalPackages/tomlpp/include', '-IExternalPackages/vulkan-headers/include', '-IExternalPackages/stb']
Commands = []
for Source in Sources:
    Command = ['g++'] + Flags + ['-fsyntax-only', Source]
    subprocess.run(Command, cwd=Root, check=True)
    Commands.append(Command)
    print('Validated:', Source, flush=True)
Document = (Root / 'Engine/ContentInterchange/VectorAuthoringDocument.cpp').read_text()
assert 'frontier.vector_artwork' in Document
for Directory in ['Engine', 'UI', 'Experimental/TextureStudio/Native']:
    for Source in (Root / Directory).rglob('*'):
        if Source.suffix in ['.h', '.cpp']:
            assert 'Artwork' not in Source.read_text(), str(Source)
Report = {'result': 'passed', 'translation_units': len(Sources), 'windows_executed': False,
          'gpu_executed': False, 'serialized_format_preserved': 'frontier.vector_artwork',
          'compiler': subprocess.check_output(['g++', '--version'], text=True).splitlines()[0],
          'commands': Commands,
          'source_sha256': {Source: hashlib.sha256((Root / Source).read_bytes()).hexdigest() for Source in Sources}}
Destination = Root / 'Exhibits/TextureStudio/Results/VectorAuthoringNamingChecks.json'
Destination.write_text(json.dumps(Report, indent=2) + '\n')
print(Destination, flush=True)
