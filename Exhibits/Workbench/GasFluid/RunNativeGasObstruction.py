#!/usr/bin/env python3
"""Build and run the obstruction checks across all three collision levels.

    python3 Exhibits/Workbench/GasFluid/RunNativeGasObstruction.py

Unlike the rest of this directory this one is NOT dependency-free: level ② is bound to the engine's own
Engine/GeometricRaster/GlobalDistanceFieldSpace, and the proof links it rather than mocking a distance
function. That is deliberate — a mock would have tested the mock. Captures land in
Exhibits/Gallery/GasObstruction.

The run is executed twice into separate build directories and the two transcripts are compared, because
obstruction feeds the reproducible field and anything that is only nearly deterministic desynchronises
multiplayer somewhere else entirely.
"""
import hashlib
import json
import subprocess
import zlib
from pathlib import Path

Root = Path(__file__).resolve().parents[3]
Gallery = Root / 'Exhibits/Gallery/GasObstruction'
Gallery.mkdir(parents=True, exist_ok=True)

Include = ['-IEngine/VolumetricDynamics', '-IEngine/DisplayPresentation', '-IEngine/ContentInterchange',
           '-IEngine/GeometricRaster', '-IEngine']
Flags = ['-std=c++20', '-O2', '-Wall', '-Wextra', '-Wno-unused-parameter']
Sources = ['Exhibits/Workbench/GasFluid/NativeGasObstruction.cpp',
           'Engine/GeometricRaster/GlobalDistanceFieldSpace.cpp',
           'Engine/GeometricRaster/DistanceFieldSpace.cpp']

Commands, Transcripts = [], []
for Attempt in ('first', 'second'):
    Build = Root / f'.cache/gasobsbuild-{Attempt}'
    Build.mkdir(parents=True, exist_ok=True)
    Binary = Build / 'GasObstructionProof'
    Compile = ['g++', *Flags, *Include, *Sources, '-o', str(Binary)]
    subprocess.run(Compile, cwd=Root, check=True)
    Commands.append(Compile)

    Result = subprocess.run([str(Binary)], cwd=Root, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    Transcripts.append(Result.stdout)
    if Result.returncode != 0:
        print(Result.stdout, end='')
        raise SystemExit('native gas obstruction proof failed')

print(Transcripts[0], end='')
assert Transcripts[0] == Transcripts[1], 'the two runs disagree; obstruction is not reproducible'
print('  PASS  the whole transcript is identical across two separate builds of the same source')
(Gallery / 'Proof.txt').write_text(Transcripts[0])

# Lossless PNG re-encoding, no retouching of native draw output.
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

Tracked = [Root / 'Engine/VolumetricDynamics/GasObstructionConsent.h',
           Root / 'Engine/VolumetricDynamics/GasSceneDistanceIntake.h',
           Root / Sources[0], Path(__file__), *sorted(Gallery.glob('*.png'))]
(Gallery / 'Commands.json').write_text(json.dumps(Commands, indent=2) + '\n')
(Gallery / 'Hashes.json').write_text(json.dumps(
    {str(Item.relative_to(Root)): hashlib.sha256(Item.read_bytes()).hexdigest() for Item in Tracked},
    indent=2) + '\n')
