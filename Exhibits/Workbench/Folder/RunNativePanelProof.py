#!/usr/bin/env python3
"""Build and run the native folder collection proof, then record the executed commands and hashes.

    python3 Exhibits/Workbench/Folder/RunNativePanelProof.py

Captures land in Exhibits/Gallery/FolderNative. ImGui is fetched by Tools/Bootstrap.py if absent.
"""
import hashlib
import json
import subprocess
import sys
import zlib
from pathlib import Path

Root = Path(__file__).resolve().parents[3]
Build = Root / '.cache/folderbuild'
Gallery = Root / 'Exhibits/Gallery/FolderNative'
Build.mkdir(parents=True, exist_ok=True)
Gallery.mkdir(parents=True, exist_ok=True)

if not (Root / 'ExternalPackages/imgui/imgui.h').exists():
    subprocess.run([sys.executable, str(Root / 'Tools/Bootstrap.py'), '--package', 'imgui', '--repair'],
                   cwd=Root, check=True)

Include = ['-IExternalPackages/imgui', '-IEngine', '-IEngine/Editor', '-IEngine/ContentInterchange',
           '-IEngine/DisplayPresentation', '-IExhibits/Workbench/IconArt', '-IExhibits/Workbench/Editor']
Flags = ['-std=c++20', '-DFRONTIER_DEVELOPMENT', '-O1', '-fno-strict-aliasing', '-w']

# The collection card is drawn by the shipped inspector, so the whole inspector family is linked in
# rather than mirrored: the proof then cannot drift from what the editor actually draws.
Panels = ['InspectorPanel', 'ControlPanel', 'AtmosphereSkyInspectorPanel', 'CameraInspectorPanel',
          'CloudsInspectorPanel', 'FogInspectorPanel', 'LensFlareInspectorPanel', 'LightInspectorPanel',
          'MoonInspectorPanel', 'PostProcessInspectorPanel', 'SolidArcInspectorPanel',
          'StarsInspectorPanel', 'SunInspectorPanel', 'TyreInspectorPanel', 'WeatherInspectorPanel']
Sources = (['ExternalPackages/imgui/imgui.cpp', 'ExternalPackages/imgui/imgui_draw.cpp',
            'ExternalPackages/imgui/imgui_widgets.cpp', 'ExternalPackages/imgui/imgui_tables.cpp']
           + [f'Engine/Editor/{Panel}.cpp' for Panel in Panels]
           + ['Engine/DisplayPresentation/CelestialSolver.cpp',
              'Exhibits/Workbench/Folder/NativePanelProof.cpp'])

Commands, Objects = [], []
for Source in Sources:
    Object = Build / (Path(Source).stem + '.o')
    Command = ['g++', *Flags, *Include, '-c', Source, '-o', str(Object)]
    # ImGui itself is third-party and is only rebuilt when missing.
    if 'imgui' not in Source or not Object.exists():
        subprocess.run(Command, cwd=Root, check=True)
        Commands.append(Command)
    Objects.append(str(Object))

Link = ['g++', *Objects, '-o', str(Build / 'FractureProof')]
subprocess.run(Link, cwd=Root, check=True)
Commands.append(Link)

Result = subprocess.run([str(Build / 'FractureProof')], cwd=Root, text=True,
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
print(Result.stdout, end='')
(Gallery / 'Proof.txt').write_text(Result.stdout + f'Exit: {Result.returncode}\n')
assert Result.returncode == 0, 'native folder collection proof failed'

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

Tracked = [Root / 'Engine/Editor/CollectionSequence.h',
           Root / 'Engine/Editor/InspectorPanel.cpp',
           Root / 'Exhibits/Workbench/Folder/NativePanelProof.cpp',
           Path(__file__), *sorted(Gallery.glob('*.png'))]
(Gallery / 'Commands.json').write_text(json.dumps(Commands, indent=2) + '\n')
(Gallery / 'Hashes.json').write_text(json.dumps(
    {str(Item.relative_to(Root)): hashlib.sha256(Item.read_bytes()).hexdigest() for Item in Tracked},
    indent=2) + '\n')
