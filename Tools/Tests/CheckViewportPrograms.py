#!/usr/bin/env python3
"""Audit compiled SPIR-V entry-point resources; this is not a GPU execution or performance test."""
from pathlib import Path
import argparse
import struct

Parser = argparse.ArgumentParser(description=__doc__)
Parser.add_argument("--shader-dir", type=Path, default=Path(__file__).resolve().parents[2] / "Engine/Shaders")
Arguments = Parser.parse_args()


def ReadProgram(Name):
    Content = (Arguments.shader_dir / f"{Name}.spv").read_bytes()
    Words = struct.unpack(f"<{len(Content) // 4}I", Content)
    assert Words[0] == 0x07230203, Name
    Decorations, References, Operations = {}, set(), set()
    Cursor = 5
    while Cursor < len(Words):
        Count, Opcode = Words[Cursor] >> 16, Words[Cursor] & 0xFFFF
        assert Count > 0
        Operands = Words[Cursor + 1:Cursor + Count]
        Operations.add(Opcode)
        if Opcode == 71 and Operands[1] in (33, 34):  # OpDecorate: Binding / DescriptorSet
            Decorations.setdefault(Operands[0], {})[Operands[1]] = Operands[2]
        if Opcode in (61, 65, 66, 67, 68):  # Load / AccessChain / InBoundsAccessChain / PtrAccessChain / ArrayLength
            References.add(Operands[2])
        if Opcode == 62:  # Store
            References.add(Operands[0])
        Cursor += Count
    Resources = {(Description.get(34, 0), Description[33]) for Token, Description in Decorations.items()
                 if Token in References and 33 in Description}
    return Resources, Operations


Raster, RasterOperations = ReadProgram("RasterViewport")
Software, SoftwareOperations = ReadProgram("ReSTIRViewport")
Hardware, HardwareOperations = ReadProgram("RayQueryViewport")
Reservoirs = {(0, Slot) for Slot in (16, 17, 25, 26)}
Traversal = {(0, Slot) for Slot in (8, 9, 27, 28, 29, 30)}
assert not Raster & Reservoirs, Raster & Reservoirs
assert not Raster & Traversal, Raster & Traversal
assert {(0, 4), (0, 6), (1, 0), (1, 1)} <= Raster  # resolved surfaces, instances, shadow constants/maps
assert Reservoirs <= Software
assert Reservoirs <= Hardware
assert 4473 not in RasterOperations and 4473 not in SoftwareOperations  # OpRayQueryInitializeKHR
assert 4473 in HardwareOperations and (1, 0) in Hardware
print("PASS raster SPIR-V reads visibility, materials and shadow maps; no reservoir or triangle-traversal access")
print("PASS software SPIR-V retains ReSTIR reservoirs without hardware-only instructions")
print("PASS hardware SPIR-V contains inline ray-query instructions and the acceleration descriptor")
print("GPU dispatch, validation-layer execution and frame-time measurements are separate checks.")
