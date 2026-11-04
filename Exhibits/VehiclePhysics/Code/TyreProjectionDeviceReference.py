#!/usr/bin/env python3
#==========================================================================================================================================
#                                            TYREPROJECTIONDEVICEREFERENCE.PY
#==========================================================================================================================================
# 🧩 Compare direct Vulkan readbacks against the unchanged VehicleHMI JavaScript tyre solver.

import struct
import subprocess
import sys
from pathlib import Path

import numpy as np

Root = Path(__file__).resolve().parents[3]
ParticleCount = 160
TyreCount = 33  # Two 32-thread workgroups, including 31 inactive lanes.
StepCount = 8
CommandType = np.dtype([
    ('centre', '<f4', 4), ('orientation', '<f4', 4), ('ground', '<f4', 4),
    ('dynamics', '<f4', 4), ('iterations', '<u4', 4),
])
ParticleType = np.dtype([
    ('rest', '<f4', 4), ('local', '<f4', 4), ('previous', '<f4', 4),
    ('velocity', '<f4', 4), ('world', '<f4', 4),
])


def ReadReference():
    Text = subprocess.check_output(['node', 'Exhibits/VehiclePhysics/Code/TyreContinuumReference.mjs'],
                                   cwd=Root, text=True)
    Lines = iter(Text.splitlines())
    Cases = {}
    for Line in Lines:
        Fields = Line.split()
        if len(Fields) < 4 or Fields[0] != 'C':
            raise RuntimeError('Unexpected JavaScript observation header')
        if Fields[1] != 'projection':
            break
        Case, Step = int(Fields[2]), int(Fields[3])
        Values = np.array([float(Value) for Value in Fields[4:]], dtype=np.float64)
        Nodes = np.empty((ParticleCount, 6), dtype=np.float64)
        for N in range(ParticleCount):
            Record = next(Lines).split()
            if len(Record) != 7 or Record[0] != 'N':
                raise RuntimeError('Unexpected JavaScript particle record')
            Nodes[N] = [float(Value) for Value in Record[1:]]
        Cases[Case, Step] = (Values, Nodes)
    if len(Cases) != 12 * StepCount:
        raise RuntimeError('Incomplete JavaScript projection observations')
    return Cases


def Main():
    if len(sys.argv) != 6:
        raise RuntimeError('Expected Vulkan probe, reset SPIR-V, advance SPIR-V, surface SPIR-V, and scratch directory')
    Probe, Reset, Advance, Surface = (Path(PathName).resolve() for PathName in sys.argv[1:5])
    Scratch = Path(sys.argv[5]).resolve()
    Scratch.mkdir(parents=True, exist_ok=True)
    Cases = ReadReference()
    Commands = np.zeros((StepCount, TyreCount), dtype=CommandType)
    for Step in range(StepCount):
        for Tyre in range(TyreCount):
            Values, _ = Cases[Tyre % 12, Step]
            # seconds, deflection, pressure, lateral, centre(3), quaternion(4), ground(3), force
            Commands[Step, Tyre]['centre'][:3] = Values[4:7]
            Commands[Step, Tyre]['orientation'] = Values[7:11]
            Commands[Step, Tyre]['ground'][:3] = Values[11:14]
            Commands[Step, Tyre]['ground'][3] = Values[1]
            Commands[Step, Tyre]['dynamics'][:3] = [Values[2], Values[3], Values[0]]
            Commands[Step, Tyre]['iterations'][0] = 2
    if CommandType.itemsize != 80 or ParticleType.itemsize != 80:
        raise RuntimeError('Unexpected Python ABI size')
    Input, Output = Scratch / 'commands.bin', Scratch / 'readback.bin'
    Input.write_bytes(struct.pack('<II', TyreCount, StepCount) + Commands.tobytes())
    subprocess.run([str(Probe), str(Reset), str(Advance), str(Surface), str(Input), str(Output)], cwd=Root, check=True)
    Data = Output.read_bytes()
    ParticleBytes = TyreCount * ParticleCount * ParticleType.itemsize
    ResultBytes = TyreCount * 16
    if len(Data) != StepCount * (ParticleBytes + ResultBytes):
        raise RuntimeError('Unexpected readback size')
    WorstLocal = WorstWorld = WorstForce = 0.0
    LargestMovement = 0.0
    StaticError = 0.0
    for Step in range(StepCount):
        Offset = Step * (ParticleBytes + ResultBytes)
        Particles = np.frombuffer(Data, dtype=ParticleType, count=TyreCount * ParticleCount, offset=Offset).reshape(TyreCount, ParticleCount)
        Results = np.frombuffer(Data, dtype='<f4', count=TyreCount * 4, offset=Offset + ParticleBytes).reshape(TyreCount, 4)
        if not np.isfinite(Results).all() or not np.isfinite(Particles['local']).all() or not np.isfinite(Particles['world']).all():
            raise RuntimeError('Nonfinite GPU result')
        for Tyre in range(TyreCount):
            Values, Expected = Cases[Tyre % 12, Step]
            WorstLocal = max(WorstLocal, float(np.max(np.abs(Particles[Tyre]['local'][:, :3] - Expected[:, :3]))))
            WorstWorld = max(WorstWorld, float(np.max(np.abs(Particles[Tyre]['world'][:, :3] - Expected[:, 3:]))))
            WorstForce = max(WorstForce, abs(float(Results[Tyre, 0]) - Values[14]))
            LargestMovement = max(LargestMovement, float(np.max(np.abs(Particles[Tyre]['local'][:, :3] - Particles[Tyre]['rest'][:, :3]))))
            if Tyre % 12 == 0:
                StaticError = max(StaticError, float(np.max(np.abs(Particles[Tyre]['local'][:, :3] - Particles[Tyre]['rest'][:, :3]))))
            if Tyre >= 12 and (Particles[Tyre].tobytes() != Particles[Tyre % 12].tobytes() or
                               Results[Tyre].tobytes() != Results[Tyre % 12].tobytes()):
                raise RuntimeError('Identical tyres differed across independent invocation slots')
    print(f'TYRE_DEVICE_JS cases=12 duplicated_tyres={TyreCount} steps={StepCount} '
          f'particle_snapshots={TyreCount * StepCount * ParticleCount} max_local_m={WorstLocal:.9g} '
          f'max_world_m={WorstWorld:.9g} max_force_n={WorstForce:.9g}', flush=True)
    print(f'TYRE_DEVICE_INVARIANTS duplicated_states_exact=1 static_error_m={StaticError:.9g} '
          f'largest_deformation_m={LargestMovement:.9g}', flush=True)
    if WorstLocal > 0.0003 or WorstWorld > 0.0003 or WorstForce > 0.02 or StaticError > 0.00001 or LargestMovement < 0.005:
        raise RuntimeError('JavaScript comparison or independent state invariant failed')
    print('TYRE_DEVICE_SUMMARY PASS direct_slang_spirv=1 no_vehicle_or_jolt=1', flush=True)


if __name__ == '__main__':
    Main()
