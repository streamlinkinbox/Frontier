#!/usr/bin/env python3
"""Exercise/rasterise the shared production ImGui card; on Windows also execute/read back its real DX11 host."""
from pathlib import Path
import os
import struct
import subprocess
import sys
import zlib

Root = Path(__file__).resolve().parents[3]
Output = Root / "_AgentScratch/build/project-opening"
Output.mkdir(parents=True, exist_ok=True)
ImGui = Root / "Frontier/ExternalPackages/imgui"
Windows = sys.platform == "win32"
Sources = [ImGui / (Name + ".cpp") for Name in ("imgui", "imgui_draw", "imgui_tables", "imgui_widgets")]


def Check(Name, Extra, Native=False):
    Executable = Output / (Name + (".exe" if Windows else ""))
    Files = [*Sources, *Extra]
    if Windows:
        Command = ["cl.exe", "/nologo", "/std:c++20", "/EHsc", "/O2", "/MD", "/utf-8", "/UNDEBUG",
                   "/DNOMINMAX", "/DWIN32_LEAN_AND_MEAN", "/D_CRT_SECURE_NO_WARNINGS", "/I" + str(ImGui),
                   *map(str, Files), "/Fe:" + str(Executable)]
        if Native:
            Command += ["/link", "d3d11.lib", "dxgi.lib", "d3dcompiler.lib", "dwmapi.lib", "imm32.lib", "ole32.lib", "uuid.lib", "windowscodecs.lib", "user32.lib", "gdi32.lib", "shell32.lib"]
    else:
        Command = [os.environ.get("CXX", "g++"), "-std=c++20", "-O1", "-UNDEBUG", "-I" + str(ImGui),
                   *map(str, Files), "-o", str(Executable)]
    with (Output / (Name + ".log")).open("w", encoding="utf-8") as Log:
        Result = subprocess.run(Command, cwd=Output, stdout=Log, stderr=subprocess.STDOUT)
    print((Output / (Name + ".log")).read_text(encoding="utf-8", errors="replace"))
    Result.check_returncode()
    subprocess.run([str(Executable), str(Output / "ProjectBrowserWindows.ppm" if Native else Output)], cwd=Root, check=True, timeout=90)


Check("ProjectOpeningProof", [Root / "VisualProof/ProjectOpening/ProjectOpeningProof.cpp"])
if Windows:
    Check("ProjectOpeningWindows", [Root / "VisualProof/ProjectOpening/ProjectOpeningWindows.cpp",
          Root / "Frontier/Engine/Host/ProjectOpeningSequence.cpp",
          Root / "Frontier/Engine/ProjectInterchange/ProjectSpecification.cpp",
          ImGui / "backends/imgui_impl_win32.cpp", ImGui / "backends/imgui_impl_dx11.cpp"], Native=True)
    Magic, Extent, Maximum, Pixels = (Output / "ProjectBrowserWindows.ppm").read_bytes().split(b"\n", 3)
    Width, Height = map(int, Extent.split())
    assert Magic == b"P6" and Maximum == b"255" and (Width, Height) == (840, 640)
    assert len(Pixels) == Width * Height * 3 and len(set(Pixels)) > 32, "Blank native render target"
    ColourPixels = sum(max(Pixels[Offset:Offset+3]) - min(Pixels[Offset:Offset+3]) > 10
                       for Row in range(145, 270) for Column in range(36, 296)
                       for Offset in [(Row * Width + Column) * 3])
    assert ColourPixels > 500, "Native project preview is missing or not sampled by DX11"
    def Chunk(Kind, Data):
        return struct.pack(">I", len(Data)) + Kind + Data + struct.pack(">I", zlib.crc32(Kind + Data) & 0xFFFFFFFF)
    Raster = b"".join(b"\0" + Pixels[Y*Width*3:(Y+1)*Width*3] for Y in range(Height))
    Png = b"\x89PNG\r\n\x1a\n" + Chunk(b"IHDR", struct.pack(">IIBBBBB", Width, Height, 8, 2, 0, 0, 0))
    (Output / "ProjectBrowserWindows.png").write_bytes(Png + Chunk(b"IDAT", zlib.compress(Raster)) + Chunk(b"IEND", b""))
print("PASS startup browser checks; proofs in", Output)
