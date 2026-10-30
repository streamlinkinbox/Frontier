#============================================================================================================================================
#                                                     ENCODENATIVEPBRBRIDGE.PY
#============================================================================================================================================
# 🧩 Encode actual C++ Vulkan PBR captures and record their Float32 authoring bridge provenance.

import argparse
import hashlib
import json
from pathlib import Path
import PIL
from PIL import Image

Root = Path(__file__).resolve().parents[3]
Parser = argparse.ArgumentParser()
Parser.add_argument('--captures', type=Path, required=True)
Parser.add_argument('--executable', type=Path, required=True)
Arguments = Parser.parse_args()
Metadata = json.loads((Arguments.captures / 'BridgeFrames.json').read_text())
FrameNames = Metadata.get('frames', [])
if Metadata.get('result') != 'passed' or Metadata.get('native_cpp_vulkan_capture') is not True:
    raise RuntimeError('Expected the successful native C++ Vulkan PBR capture manifest')
if Metadata.get('channel_source') != 'VectorSurfaceMaterialBridge GPU dispatch/readback':
    raise RuntimeError('Capture source is not the Float32 vector-to-material bridge')
if Metadata.get('resolution') != [128, 128] or Metadata.get('authoring_map_resolution') != [512, 512] or len(FrameNames) != 3:
    raise RuntimeError('Expected three bounded 128×128 PBR captures from 512×512 authoring maps')
RgbFrames = []
Digest = hashlib.sha256()
FrameDigests = {}
for Name in FrameNames:
    Source = Arguments.captures / Name
    Payload = Source.read_bytes()
    Digest.update(Payload)
    FrameDigests[Name] = hashlib.sha256(Payload).hexdigest()
    with Image.open(Source) as Capture:
        if Capture.size != (128, 128):
            raise RuntimeError('Unexpected integrated PBR capture extent')
        RgbFrames.append(Capture.convert('RGB'))
if len(set(FrameDigests.values())) != 3:
    raise RuntimeError('PBR capture sequence does not show three distinct rendered states')
PaletteSource = Image.new('RGB', (128, 128 * len(RgbFrames)))
for Index, Rgb in enumerate(RgbFrames):
    PaletteSource.paste(Rgb, (0, 128 * Index))
Palette = PaletteSource.quantize(colors=256)
Frames = [Rgb.quantize(palette=Palette, dither=Image.Dither.NONE) for Rgb in RgbFrames]
Destination = Root / 'Exhibits/TextureStudio/Images/NativePbrBridgeSampling.gif'
Frames[0].save(Destination, save_all=True, append_images=Frames[1:], duration=700, loop=0, optimize=True, disposal=1)
with Image.open(Destination) as Motion:
    if Motion.n_frames != 3:
        raise RuntimeError('Float32 PBR evidence GIF lost a capture frame')
Report = {
    'result': 'passed',
    'frames': len(Frames),
    'resolution': [128, 128],
    'authoring_map_resolution': Metadata['authoring_map_resolution'],
    'duration_ms_per_frame': 700,
    'pillow': PIL.__version__,
    'native_cpp_vulkan_capture': True,
    'float32_authoring_gpu_dispatch_readback': True,
    'pbr_gpu_sampling': True,
    'native_windows_executed': False,
    'os_window_created': False,
    'cpu_vector_rasterization': False,
    'raw_frames_sha256': Digest.hexdigest(),
    'frame_sha256': FrameDigests,
    'gif_sha256': hashlib.sha256(Destination.read_bytes()).hexdigest(),
    'executable_sha256': hashlib.sha256(Arguments.executable.read_bytes()).hexdigest(),
    'source_sha256': {
        Name: hashlib.sha256((Root / Name).read_bytes()).hexdigest()
        for Name in (
            'Engine/Render/VectorCoverageRasterization.cpp',
            'Engine/Render/VectorSurfaceMaterialBridge.h',
            'Engine/Render/SurfaceMaterial.cpp',
            'Engine/Render/SurfaceRasterization.cpp',
            'Engine/Render/Shader/MaterialSurface.slang',
            'Exhibits/Renderer/Code/MaterialRasterizationValidation.cpp',
            'Exhibits/TextureStudio/Code/EncodeNativePbrBridge.py',
        )
    },
}
ReportPath = Root / 'Exhibits/TextureStudio/Results/NativePbrBridgeSamplingChecks.json'
ReportPath.write_text(json.dumps(Report, indent=2) + '\n')
print(f'Encoded three native C++ Vulkan PBR captures: {Destination}', flush=True)
