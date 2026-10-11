#!/usr/bin/env python3
"""Build and run the native scene-emitter inspector proof, then record the executed commands and hashes.

    python3 Exhibits/Workbench/Lighting/RunNativeEmitterCards.py

Captures land in Exhibits/Gallery/LightingNative. ImGui is fetched by Tools/Bootstrap.py if absent.
"""
import hashlib
import json
import subprocess
import sys
import zlib
from pathlib import Path

Root = Path(__file__).resolve().parents[3]
Build = Root / '.cache/lightbuild'
Gallery = Root / 'Exhibits/Gallery/LightingNative'
Build.mkdir(parents=True, exist_ok=True)
Gallery.mkdir(parents=True, exist_ok=True)

if not (Root / 'ExternalPackages/imgui/imgui.h').exists():
    subprocess.run([sys.executable, str(Root / 'Tools/Bootstrap.py'), '--package', 'imgui', '--repair'],
                   cwd=Root, check=True)

Include = ['-IExternalPackages/imgui', '-IEngine/Editor', '-IEngine/ContentInterchange',
           '-IExhibits/Workbench/IconArt', '-IExhibits/Workbench/Editor']
Flags = ['-std=c++20', '-O1', '-fno-strict-aliasing', '-Wall', '-Wno-unused-parameter', '-Wno-unused-variable']

Sources = ['ExternalPackages/imgui/imgui.cpp', 'ExternalPackages/imgui/imgui_draw.cpp',
           'ExternalPackages/imgui/imgui_widgets.cpp', 'ExternalPackages/imgui/imgui_tables.cpp',
           'Engine/Editor/ControlPanel.cpp', 'Engine/Editor/LightInspectorPanel.cpp',
           'Exhibits/Workbench/Lighting/NativeEmitterCards.cpp']

Commands, Objects = [], []
for Source in Sources:
    Object = Build / (Path(Source).stem + '.o')
    Command = ['g++', *Flags, *Include, '-c', Source, '-o', str(Object)]
    # ImGui itself is third-party and is only rebuilt when missing.
    if 'imgui' not in Source or not Object.exists():
        subprocess.run(Command, cwd=Root, check=True)
        Commands.append(Command)
    Objects.append(str(Object))

Link = ['g++', *Objects, '-o', str(Build / 'EmitterProof')]
subprocess.run(Link, cwd=Root, check=True)
Commands.append(Link)

Result = subprocess.run([str(Build / 'EmitterProof')], cwd=Root, text=True,
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
print(Result.stdout, end='')
(Gallery / 'Proof.txt').write_text(Result.stdout + f'Exit: {Result.returncode}\n')
assert Result.returncode == 0, 'native emitter proof failed'

# Lossless PNG re-encoding, no retouching of native draw output. stb writes a barely-compressed
# stream; these are flat-colour UI captures, so zlib level 9 shrinks them by roughly 50x.
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

Tracked = [Root / 'Engine/Editor/LightInspectorPanel.cpp',
           Root / 'Engine/Host/EditorFeedSequence.cpp',
           Root / 'Engine/GeometricRaster/SceneStructure.h',
           Root / 'Exhibits/Workbench/Lighting/NativeEmitterCards.cpp',
           Path(__file__), *sorted(Gallery.glob('*.png'))]
(Gallery / 'Commands.json').write_text(json.dumps(Commands, indent=2) + '\n')
(Gallery / 'Hashes.json').write_text(json.dumps(
    {str(Item.relative_to(Root)): hashlib.sha256(Item.read_bytes()).hexdigest() for Item in Tracked},
    indent=2) + '\n')
