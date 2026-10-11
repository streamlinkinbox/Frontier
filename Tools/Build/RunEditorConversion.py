#!/usr/bin/env python3
"""Run actual native C++ panels on a 256 KiB stack and retain losslessly encoded pixels."""
from pathlib import Path
import json
import os
import struct
import subprocess
import sys
import zlib

Root = Path(__file__).resolve().parents[2]
Program = Path(sys.argv[1]).resolve()
Output = Path(sys.argv[2]).resolve()
Output.mkdir(parents=True, exist_ok=True)


def LimitStack():
    import resource
    resource.setrlimit(resource.RLIMIT_STACK, (262144, 262144))


Result = subprocess.run([str(Program), "--editor-conversion", str(Output)], cwd=Root,
                        text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                        preexec_fn=LimitStack if os.name != "nt" else None)
(Output / "Verification.log").write_text(Result.stdout)
print(Result.stdout)
Result.check_returncode()
for Image in Output.glob("*.png"):
    Original = Image.read_bytes()
    Position = 8
    Chunks = []
    while Position < len(Original):
        Length = struct.unpack(">I", Original[Position:Position + 4])[0]
        Chunks.append((Original[Position + 4:Position + 8], Original[Position + 8:Position + 8 + Length]))
        Position += Length + 12
    Raw = zlib.decompress(b"".join(Content for Label, Content in Chunks if Label == b"IDAT"))
    Encoded = bytearray(Original[:8])
    Written = False
    for Label, Content in Chunks:
        if Label == b"IDAT":
            if Written:
                continue
            Content = zlib.compress(Raw, 9)
            Written = True
        Encoded += struct.pack(">I", len(Content)) + Label + Content + struct.pack(">I", zlib.crc32(Label + Content) & 0xffffffff)
    Image.write_bytes(Encoded)
Report = {"exit": Result.returncode, "stackBytes": 262144 if os.name != "nt" else "default MSVC reserve",
          "execution": "Production C++ inspectors; CPU raster of native ImGui commands. Not a GPU screenshot.",
          "images": sorted(Image.name for Image in Output.glob("*.png"))}
(Output / "Verification.json").write_text(json.dumps(Report, indent=2) + "\n")

# Compiler-reported frames, not sizeof estimates; scope is these compiled proof TUs.
Frames = []
Critical = ("BuildLayout(", "RecordCollection(", "RecordSunInspector(", "RecordAtmosphereSkyInspector(",
            "RecordProbe(", "RecordCurve(", "RecordLensFlareInspector(", "RecordWeatherInspector(", "ConstructInspectorLayout(", "RunEditorConversion(")
for Record in Program.parent.rglob("*.su"):
    for Line in Record.read_text().splitlines():
        Fields = Line.split("\t")
        if len(Fields) < 3 or not any(Name in Fields[0] for Name in Critical):
            continue
        Size = int(Fields[1])
        Frames.append({"function": Fields[0].replace(str(Root.parent) + "/", ""), "bytes": Size, "classification": Fields[2]})
        assert Size <= 8192, "Critical native UI frame exceeds 8 KiB: " + Fields[0]
if Frames:
    (Output / "StackUsage.json").write_text(json.dumps({"compiler": "GCC -O2 -fstack-usage", "budgetBytes": 8192, "frames": Frames}, indent=2) + "\n")
