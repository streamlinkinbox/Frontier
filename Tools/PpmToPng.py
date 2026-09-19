#!/usr/bin/env python3
#=============================================================================================================================================
# 📦 Frontier/Tools/PpmToPng.py — Standard Portable Pixmap (.ppm) to Portable Network Graphics (.png) Converter
#=============================================================================================================================================

import sys
import zlib
import struct
import os

def PpmToPng(ppmPath, pngPath):
    if not os.path.exists(ppmPath):
        print(f"[Error] PPM file not found: {ppmPath}")
        return False
    
    with open(ppmPath, 'rb') as f:
        header = f.readline().decode('latin-1').strip()
        if header != 'P6':
            print(f"[Error] Unsupported PPM format (expected P6, got {header})")
            return False
        
        # Read dimensions, skipping comments
        line = f.readline().decode('latin-1').strip()
        while line.startswith('#') or not line:
            line = f.readline().decode('latin-1').strip()
        
        parts = line.split()
        if len(parts) == 2:
            width, height = int(parts[0]), int(parts[1])
        else:
            width = int(parts[0])
            line = f.readline().decode('latin-1').strip()
            while line.startswith('#') or not line:
                line = f.readline().decode('latin-1').strip()
            height = int(line)
        
        # Read max color value
        maxValLine = f.readline().decode('latin-1').strip()
        while maxValLine.startswith('#') or not maxValLine:
            maxValLine = f.readline().decode('latin-1').strip()
        maxVal = int(maxValLine)
        
        # Read pixel stream
        rawPixels = f.read(width * height * 3)

    # Encode to PNG using zlib
    rawScanlines = bytearray()
    rowSize = width * 3
    for y in range(height):
        rawScanlines.append(0) # Filter type 0 (None)
        rawScanlines.extend(rawPixels[y * rowSize : (y + 1) * rowSize])

    compressed = zlib.compress(bytes(rawScanlines), 9)

    def PngChunk(chunkType, data):
        length = len(data)
        crc = zlib.crc32(chunkType + data) & 0xffffffff
        return struct.pack('>I', length) + chunkType + data + struct.pack('>I', crc)

    pngHeader = b'\x89PNG\r\n\x1a\n'
    ihdrData = struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0) # 8-bit truecolor (RGB)
    ihdrChunk = PngChunk(b'IHDR', ihdrData)
    idatChunk = PngChunk(b'IDAT', compressed)
    iendChunk = PngChunk(b'IEND', b'')

    with open(pngPath, 'wb') as f:
        f.write(pngHeader + ihdrChunk + idatChunk + iendChunk)
    
    print(f"[PpmToPng] Converted {ppmPath} ({width}x{height}) -> {pngPath}")
    return True

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python3 PpmToPng.py <input.ppm> <output.png>")
        sys.exit(1)
    PpmToPng(sys.argv[1], sys.argv[2])
