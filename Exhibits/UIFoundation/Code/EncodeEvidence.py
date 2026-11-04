#============================================================================================================================================
#                                                         ENCODEEVIDENCE.PY
#============================================================================================================================================
# 🧩 Encode the actual RGB pixels emitted by native validation programs into a lossless PNG.

from pathlib import Path
import struct
import sys
import zlib


def EncodeChunk(Category, Payload):
    return (struct.pack('>I', len(Payload)) + Category + Payload
            + struct.pack('>I', zlib.crc32(Category + Payload) & 0xffffffff))


def Main():
    Source = Path(sys.argv[1]).read_bytes()
    Signature, Dimensions, Maximum, Pixels = Source.split(b'\n', 3)
    if Signature != b'P6' or Maximum != b'255':
        raise ValueError('Expected RGB8 PPM emitted by InterfaceValidation')
    Width, Height = map(int, Dimensions.split())
    if len(Pixels) != Width * Height * 3:
        raise ValueError('Unexpected pixel extent')
    Rows = b''.join(b'\0' + Pixels[Offset:Offset + Width * 3]
                    for Offset in range(0, len(Pixels), Width * 3))
    Header = struct.pack('>IIBBBBB', Width, Height, 8, 2, 0, 0, 0)
    Output = b'\x89PNG\r\n\x1a\n'
    Output += EncodeChunk(b'IHDR', Header)
    Output += EncodeChunk(b'IDAT', zlib.compress(Rows))
    Output += EncodeChunk(b'IEND', b'')
    Path(sys.argv[2]).write_bytes(Output)
    print(f'Encoded actual RGB evidence: {Width} x {Height}; no synthetic artwork')


if __name__ == '__main__':
    Main()
