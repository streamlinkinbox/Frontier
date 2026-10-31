#============================================================================================================================================
#                                                          VERIFYDROPDOWNSTYLE.PY
#============================================================================================================================================
# 🧩 Refuse bypasses of the shared native dropdown surface and retain the current source/capture provenance.

import hashlib
import json
from pathlib import Path
import re

Root = Path(__file__).resolve().parents[3]
Canonical = Root / 'Engine/Interface/DropdownControls.h'
Violations = []
for Directory in ['Engine', 'UI']:
    for File in (Root / Directory).rglob('*'):
        if File.suffix not in ['.cpp', '.h'] or File == Canonical:
            continue
        if re.search(r'ImGui::(?:BeginCombo|Combo)\s*\(', File.read_text()):
            Violations.append(File.relative_to(Root).as_posix())
if Violations:
    raise RuntimeError('Native dropdown bypasses: ' + ', '.join(Violations))
Sources = ['Engine/Interface/DropdownControls.h', 'Engine/Interface/ControlKit.h',
           'Engine/Interface/ControlKit.cpp', 'Engine/Interface/VectorAuthoringControls.cpp',
           'UI/TextureStudio/VectorMaskControls.cpp', 'UI/TextureStudio/PatternWorkspacePanel.cpp',
           'UI/TextureStudio/MaterialLibraryPanel.cpp', 'Exhibits/TextureStudio/Code/VerifyDropdownStyle.py']
for Token in ['CornerRadius', 'RowRadius', 'PaddingX', 'PaddingY', 'RowHeight']:
    assert 'ControlKitTokens::Dropdown' + Token in Canonical.read_text(), Token
Images = ['NativeDropdownAddMotif', 'NativeDropdownMoreTools', 'NativeDropdownDocument',
          'NativeDropdownArray', 'NativeDropdownMaterial']
CaptureDigests = {}
for Caption in Images:
    File = Root / 'Exhibits/TextureStudio/Images' / (Caption + '.png')
    if not File.is_file():
        raise RuntimeError('Run native dropdown capture validation first: ' + str(File))
    CaptureDigests[File.relative_to(Root).as_posix()] = hashlib.sha256(File.read_bytes()).hexdigest()
Geometry = {}
for Key, Token in [('popup_radius', 'CornerRadius'), ('row_radius', 'RowRadius'), ('padding_x', 'PaddingX'),
                   ('padding_y', 'PaddingY'), ('row_height', 'RowHeight'), ('row_gap', 'RowGap')]:
    Match = re.search(r'Dropdown' + Token + r'\s*=\s*([0-9.]+)f', (Root / 'Engine/Interface/ControlKit.h').read_text())
    if not Match:
        raise RuntimeError('Missing shared dropdown geometry token: ' + Token)
    Geometry[Key] = float(Match[1])
Report = {'result': 'passed', 'native_selection_bypasses': Violations,
          'shared_geometry': Geometry,
          'coverage': 'Native Pattern action/selection dropdowns, material/mask selectors and shared PixelSpace geometry',
          'exclusions': 'Colour pickers and modal dialog layouts are not list dropdowns',
          'source_sha256': {Caption: hashlib.sha256((Root / Caption).read_bytes()).hexdigest() for Caption in Sources},
          'capture_sha256': CaptureDigests, 'windows_executed': False,
          'hardware_gpu_execution': False}
Destination = Root / 'Exhibits/TextureStudio/Results/DropdownStyleChecks.json'
Destination.write_text(json.dumps(Report, indent=2) + '\n')
print('PASS: no native selection dropdown bypasses; shared geometry and five actual native captures recorded.')
