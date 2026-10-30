#============================================================================================================================================
#                                                             ENCODERGBCAPTURE.PY
#============================================================================================================================================
# 🧩 Encode the validation renderer's unmodified P6 RGB captures as PNG using only the Python standard library.

import pathlib
import struct
import sys
import zlib


def EncodeRgbCapture(Source, Destination):
    Header = pathlib.Path(Source).read_bytes().split(b"\n", 3)
    if len(Header) != 4 or Header[0] != b"P6" or Header[2] != b"255":
        raise ValueError("Expected the validation renderer's P6 RGB capture.")
    Width, Height = map(int, Header[1].split())
    if Width <= 0 or Height <= 0 or Width * Height > 16777216 or len(Header[3]) != Width * Height * 3:
        raise ValueError("RGB capture dimensions or byte count are invalid.")

    def Chunk(Category, Payload):
        return struct.pack(">I", len(Payload)) + Category + Payload + struct.pack(">I", zlib.crc32(Category + Payload) & 0xFFFFFFFF)

    Scanlines = b"".join(b"\0" + Header[3][Row * Width * 3:(Row + 1) * Width * 3] for Row in range(Height))
    Encoded = b"\x89PNG\r\n\x1a\n" + Chunk(b"IHDR", struct.pack(">2I5B", Width, Height, 8, 2, 0, 0, 0)) + Chunk(b"IDAT", zlib.compress(Scanlines)) + Chunk(b"IEND", b"")
    pathlib.Path(Destination).parent.mkdir(parents=True, exist_ok=True)
    pathlib.Path(Destination).write_bytes(Encoded)
    print(f"Unmodified RGB capture encoded: {Width} × {Height}; {Destination}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("Expected source PPM and destination PNG.")
    EncodeRgbCapture(sys.argv[1], sys.argv[2])
