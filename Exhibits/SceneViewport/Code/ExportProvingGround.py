#!/usr/bin/env python3
#============================================================================================================================================
#                                                        EXPORTPROVINGGROUND.PY
#============================================================================================================================================
# 🧩 Copies the VehicleHMI proving ground out of a reference capture (Exhibits/RestirGiParity/Code/CaptureReference.sh) into project
#    content: the scene topology, the material records and the idle profiles of the parts the editor does not run yet.
#
#    ExportProvingGround.py <CaptureFolder> <ContentFolder>
#
# 📝 Written (all little-endian, byte-for-byte what the demo uploaded):
#    ProvingGround.facetspace   — FSP1 scene topology (facet sets + 2,094 occupants, float64), unchanged; the vehicle occupants
#                                 stay in the file and the editor deactivates them (no car yet)
#    Materials.bin              — the demo's 'Materials' creation upload with its bring-up rewrites applied in order
#    VehicleProfile.bin         — 'Vehicle record' of refresh 0 (10,448 B), bound by the masked shadow / surface programs
#    EffectProfile.bin          — 'Effect record' of refresh 0 (1,120 B), idle derez / spark settings
#    CloudProfile.bin           — 'Cloud renderer v2' of refresh 0 (544 B), the demo's own clouds switched off
#    CloudShadowProfile.bin     — 'Projected cloud shadows' of refresh 0 (48 B), switched off

import pathlib
import shutil
import struct
import sys


def ReadWrites(Path):
    Payload = Path.read_bytes()
    Cursor, Writes = 0, []
    while Cursor < len(Payload):
        Refresh, Category, LabelLength = struct.unpack_from('<iBH', Payload, Cursor)
        Cursor += 7
        Label = Payload[Cursor:Cursor + LabelLength].decode('utf-8')
        Cursor += LabelLength
        Offset = struct.unpack_from('<Q', Payload, Cursor)[0]
        Cursor += 8 + 16
        Count = struct.unpack_from('<I', Payload, Cursor)[0]
        Cursor += 4
        Writes.append((Refresh, Category, Label, Offset, Payload[Cursor:Cursor + Count]))
        Cursor += Count
    return Writes


def main():
    Capture, Content = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    Content.mkdir(parents=True, exist_ok=True)
    Writes = ReadWrites(Capture / 'WriteLog.bin')
    shutil.copyfile(Capture / 'Scene.facetspace', Content / 'ProvingGround.facetspace')
    Materials = None
    for Refresh, Category, Label, Offset, Bytes in Writes:
        if Refresh != -1 or Label != 'Materials':
            continue
        if Category == 1:
            Materials = bytearray(Bytes)
        elif Category == 0 and Materials is not None:
            Materials[Offset:Offset + len(Bytes)] = Bytes
    (Content / 'Materials.bin').write_bytes(bytes(Materials))
    for Label, Name in (('Vehicle record', 'VehicleProfile.bin'), ('Effect record', 'EffectProfile.bin'),
                        ('Cloud renderer v2', 'CloudProfile.bin'), ('Projected cloud shadows', 'CloudShadowProfile.bin')):
        Chosen = [Bytes for Refresh, Category, Written, Offset, Bytes in Writes if Refresh == 0 and Written == Label and Offset == 0]
        (Content / Name).write_bytes(Chosen[-1])
    for File in sorted(Content.iterdir()):
        print(f'{File.name:28s} {File.stat().st_size:>9d} B')


if __name__ == '__main__':
    main()
