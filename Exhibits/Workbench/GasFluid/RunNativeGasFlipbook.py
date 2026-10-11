#!/usr/bin/env python3
"""Build and run the flipbook checks, baking one real sheet from the shipped solver.

    python3 Exhibits/Workbench/GasFluid/RunNativeGasFlipbook.py

Dependency-free apart from toml++, which the descriptor is written for and read back with. No ImGui, no
window, no Vulkan. The sheet, its descriptor and the transcript land in Exhibits/Gallery/GasFlipbook.

The bake runs twice inside the proof itself and the two sheets are compared byte for byte, because a sheet is
shipped as content: an effect that bakes differently on the build machine than on an artist's is not content,
it is a liability.
"""
import hashlib
import json
import subprocess
import sys
import zlib
from pathlib import Path

Root = Path(__file__).resolve().parents[3]
Build = Root / '.cache/gasflipbuild'
Gallery = Root / 'Exhibits/Gallery/GasFlipbook'
Build.mkdir(parents=True, exist_ok=True)
Gallery.mkdir(parents=True, exist_ok=True)

# toml++ is a proof-profile package, so Tools/Bootstrap.py has already staged it in CI; a bare checkout is
# repaired here rather than failing with a missing header nobody can place.
if not (Root / 'ExternalPackages/tomlpp/include/toml++/toml.hpp').exists():
    subprocess.run([sys.executable, str(Root / 'Tools/Bootstrap.py'), '--package', 'tomlpp', '--repair'],
                   cwd=Root, check=True)

Include = ['-IEngine/VolumetricDynamics', '-IEngine/DisplayPresentation', '-IEngine/ContentInterchange',
           '-IExternalPackages/tomlpp/include']
Flags = ['-std=c++20', '-O2', '-Wall', '-Wextra', '-Wno-unused-parameter']
Source = 'Exhibits/Workbench/GasFluid/NativeGasFlipbook.cpp'

Binary = Build / 'GasFlipbookProof'
Compile = ['g++', *Flags, *Include, Source, '-o', str(Binary)]
subprocess.run(Compile, cwd=Root, check=True)

Result = subprocess.run([str(Binary)], cwd=Root, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
print(Result.stdout, end='')
(Gallery / 'Proof.txt').write_text(Result.stdout + f'Exit: {Result.returncode}\n')
assert Result.returncode == 0, 'native gas flipbook proof failed'

# Lossless PNG re-encoding, no retouching of the bake. The writer emits stored deflate blocks; zlib level 9
# shrinks the sheet by roughly an order of magnitude without changing a single pixel.
for Capture in sorted(Gallery.glob('*.png')):
    Blob = Capture.read_bytes()
    Chunks, Cursor = [], 8
    while Cursor < len(Blob):
        Size = int.from_bytes(Blob[Cursor:Cursor + 4], 'big')
        Chunks.append((Blob[Cursor + 4:Cursor + 8], Blob[Cursor + 8:Cursor + 8 + Size]))
        Cursor += Size + 12
    Packed = zlib.compress(zlib.decompress(b''.join(Body for Tag, Body in Chunks if Tag == b'IDAT')), 9)
    Rebuilt, Written = bytearray(Blob[:8]), False
    for Tag, Body in Chunks:
        if Tag == b'IDAT':
            if Written:
                continue
            Body, Written = Packed, True
        Rebuilt += len(Body).to_bytes(4, 'big') + Tag + Body + (zlib.crc32(Tag + Body) & 0xffffffff).to_bytes(4, 'big')
    Capture.write_bytes(Rebuilt)

Tracked = [Root / 'Engine/VolumetricDynamics/GasFlipbookSheet.h', Root / Source, Path(__file__),
           *sorted(Gallery.glob('*.png')), *sorted(Gallery.glob('*.toml'))]
(Gallery / 'Commands.json').write_text(json.dumps([Compile], indent=2) + '\n')
(Gallery / 'Hashes.json').write_text(json.dumps(
    {str(Item.relative_to(Root)): hashlib.sha256(Item.read_bytes()).hexdigest() for Item in Tracked},
    indent=2) + '\n')
