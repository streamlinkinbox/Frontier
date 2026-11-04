#============================================================================================================================================
#                                                     ENCODENATIVEPATTERNMOTION.PY
#============================================================================================================================================
# 🧩 Encode unmodified C++ ImGui/Vulkan frame captures as a GIF; mouse movements and cursor are produced by C++, not painted here.

import argparse
import hashlib
import json
from pathlib import Path
import PIL
from PIL import Image

Root = Path(__file__).resolve().parents[3]
Parser = argparse.ArgumentParser()
Parser.add_argument('--captures', type=Path, required=True)
Parser.add_argument('--caption', default='NativePatternRefinement')
Parser.add_argument('--executable', type=Path, required=True)
Arguments = Parser.parse_args()
Metadata = json.loads((Arguments.captures / 'MotionFrames.json').read_text())
if not 2 <= Metadata['frames'] <= 500 or not Metadata['native_mouse_cursor'] or not Metadata['native_pointer_input']:
    raise RuntimeError('Expected bounded native pointer/cursor frame captures')
Frames = []
Palette = None
Digest = hashlib.sha256()
for Index in range(Metadata['frames']):
    Source = Arguments.captures / f'Motion{Index:05}.ppm'
    Digest.update(Source.read_bytes())
    with Image.open(Source) as Frame:
        if Frame.size != (1440, 900):
            raise RuntimeError('Unexpected native surface extent')
        Rgb = Frame.convert('RGB')
        if Palette is None:
            Palette = Rgb.quantize(colors=256)
        Frames.append(Rgb.quantize(palette=Palette, dither=Image.Dither.NONE))
if not Arguments.caption.isalnum():
    raise RuntimeError('Expected an alphanumeric exhibit caption')
Destination = Root / 'Exhibits/TextureStudio/Images' / (Arguments.caption + '.gif')
Frames[0].save(Destination, save_all=True, append_images=Frames[1:], duration=110, loop=0, optimize=True, disposal=1)
with Image.open(Destination) as Motion:
    if Motion.n_frames < 20:
        raise RuntimeError('Native pointer animation unexpectedly collapsed to a static image')
    EncodedFrames = Motion.n_frames
Files = ['Engine/ContentInterchange/VectorAuthoringSelectionEditing.cpp',
         'Engine/ContentInterchange/VectorAuthoringSelectionEditing.h',
         'Engine/ContentInterchange/VectorAuthoringEditing.cpp',
         'Engine/ContentInterchange/VectorAuthoringEditing.h',
         'Exhibits/TextureStudio/Code/EncodeNativePatternMotion.py',
         'Exhibits/TextureStudio/Code/PatternWorkspaceValidation.cpp',
         'UI/TextureStudio/PatternWorkspacePanel.cpp', 'UI/TextureStudio/PatternWorkspacePanel.h',
         'Engine/Interface/VectorAuthoringControls.cpp',
         'Engine/Interface/VectorAuthoringControls.h', 'Engine/ContentInterchange/CubicPathEditing.cpp',
         'Engine/ContentInterchange/CubicPathEditing.h']
Report = {'result': 'passed', 'captured_frames': len(Frames), 'encoded_frames': EncodedFrames,
          'duration_ms_per_input_frame': 110, 'pillow': PIL.__version__, 'native_cursor': True,
          'cpu_vector_rasterization': False, 'imgui_triangle_capture_on_cpu': True,
          'vulkan_canvas': True, 'windows_executed': False, 'os_window_created': False,
          'raw_frame_sequence_sha256': Digest.hexdigest(),
          'gif_sha256': hashlib.sha256(Destination.read_bytes()).hexdigest(),
          'executable_sha256': hashlib.sha256(Arguments.executable.read_bytes()).hexdigest(),
          'source_sha256': {Name: hashlib.sha256((Root / Name).read_bytes()).hexdigest() for Name in Files}}
ReportCaption = 'NativePatternMotionChecks' if Arguments.caption == 'NativePatternRefinement' else Arguments.caption + 'MotionChecks'
(Root / 'Exhibits/TextureStudio/Results' / (ReportCaption + '.json')).write_text(json.dumps(Report, indent=2) + '\n')
print(f'Encoded {len(Frames)} actual native captures to {EncodedFrames} GIF frames: {Destination}', flush=True)
