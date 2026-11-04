#============================================================================================================================================
#                                                        ENCODERASTEREVIDENCE.PY
#============================================================================================================================================
# 🧩 Encode unmodified Vulkan RGB readback into PNG using the standard library, preserving the executed verification report.

from pathlib import Path
import argparse
import binascii
import shutil
import struct
import zlib


def EncodeChunk(Category, Payload):
    return (struct.pack('>I', len(Payload)) + Category + Payload +
            struct.pack('>I', binascii.crc32(Category + Payload) & 0xffffffff))


def EncodeCapture(Source, Destination):
    with Source.open('rb') as Input:
        assert Input.readline() == b'P6\n', 'Expected the authored Vulkan capture format'
        Width, Height = map(int, Input.readline().split())
        assert Input.readline() == b'255\n', 'Expected 8-bit RGB'
        assert 0 < Width <= 8192 and 0 < Height <= 8192
        Pixels = Input.read()
    assert len(Pixels) == Width * Height * 3
    Rows = b''.join(b'\x00' + Pixels[Offset:Offset + Width*3]
                    for Offset in range(0, len(Pixels), Width*3))
    Header = struct.pack('>IIBBBBB', Width, Height, 8, 2, 0, 0, 0)
    Destination.write_bytes(b'\x89PNG\r\n\x1a\n' + EncodeChunk(b'IHDR', Header) +
                            EncodeChunk(b'IDAT', zlib.compress(Rows, 9)) + EncodeChunk(b'IEND', b''))
    print(f'{Source.name} -> {Destination.name}: {Width}x{Height}, actual readback pixels retained')


def Main():
    Arguments = argparse.ArgumentParser()
    Arguments.add_argument('captures', type=Path)
    Arguments.add_argument('exhibit', type=Path)
    Options = Arguments.parse_args()
    Images = Options.exhibit / 'Images'
    Results = Options.exhibit / 'Results'
    Images.mkdir(parents=True, exist_ok=True)
    Results.mkdir(parents=True, exist_ok=True)
    for Source in sorted(Options.captures.glob('*.ppm')):
        if not Source.stem.startswith(('Cad','MatCap','Preview','Material')):
            continue
        EncodeCapture(Source, Images / (Source.stem + '.png'))
    for Name in ('CadRasterChecks.json','MatCapRasterChecks.json','PreviewPbrChecks.json','MaterialChecks.json'):
        if (Options.captures / Name).is_file():
            shutil.copyfile(Options.captures / Name, Results / Name)


if __name__ == '__main__':
    Main()
