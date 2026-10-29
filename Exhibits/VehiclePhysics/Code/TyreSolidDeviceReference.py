#!/usr/bin/env python3
#==========================================================================================================================================
#                                              TYRESOLIDDEVICEREFERENCE.PY
#==========================================================================================================================================
# 🧩 Compare direct Vulkan Hex8 readbacks against unchanged VehicleHMI JavaScript trajectories.

import hashlib
import struct
import subprocess
import sys
from pathlib import Path

import numpy as np

Root = Path(__file__).resolve().parents[3]
ParticleCount = 160
TyreCount = 33
StepCount = 8
CommandType = np.dtype([
    ('centre', '<f4', 4), ('orientation', '<f4', 4), ('ground', '<f4', 4),
    ('dynamics', '<f4', 4), ('iterations', '<u4', 4),
])
ParticleType = np.dtype([
    ('rest', '<f4', 4), ('local', '<f4', 4), ('previous', '<f4', 4),
    ('velocity', '<f4', 4), ('world', '<f4', 4),
])
ResultType = np.dtype([('force_status_jacobian_stress', '<f4', 4), ('contacts', '<u4', 4)])


def ReadReference():
    Text = subprocess.check_output(['node', 'Exhibits/VehiclePhysics/Code/TyreContinuumReference.mjs'],
                                   cwd=Root, text=True)
    Lines = iter(Text.splitlines())
    Cases = {}
    for Line in Lines:
        Fields = Line.split()
        if len(Fields) != 22 or Fields[0] != 'C' or Fields[1] not in ('projection', 'solid'):
            raise RuntimeError('Unexpected JavaScript observation header')
        Case, Step = int(Fields[2]), int(Fields[3])
        Values = np.array([float(Value) for Value in Fields[4:]], dtype=np.float64)
        Coordinates = np.empty((ParticleCount, 6), dtype=np.float64)
        for N in range(ParticleCount):
            Components = next(Lines).split()
            if len(Components) != 7 or Components[0] != 'N':
                raise RuntimeError('Unexpected JavaScript particle coordinates')
            Coordinates[N] = [float(Value) for Value in Components[1:]]
        if Fields[1] == 'solid':
            Cases[Case, Step] = (Values, Coordinates)
    if len(Cases) != 12 * StepCount:
        raise RuntimeError('Incomplete JavaScript solid observations')
    return Cases


def Main():
    if len(sys.argv) != 7:
        raise RuntimeError('Expected Vulkan validation, reset, advance, surface, stress SPIR-V and scratch directory')
    Validation, Reset, Advance, Surface, Stress = (Path(PathName).resolve() for PathName in sys.argv[1:6])
    Scratch = Path(sys.argv[6]).resolve()
    Scratch.mkdir(parents=True, exist_ok=True)
    Cases = ReadReference()
    Commands = np.zeros((StepCount, TyreCount), dtype=CommandType)
    for Step in range(StepCount):
        for Tyre in range(TyreCount):
            Values, _ = Cases[Tyre % 12, Step]
            Commands[Step, Tyre]['centre'][:3] = Values[4:7]
            Commands[Step, Tyre]['orientation'] = Values[7:11]
            Commands[Step, Tyre]['ground'][:3] = Values[11:14]
            Commands[Step, Tyre]['ground'][3] = Values[1]
            Commands[Step, Tyre]['dynamics'][:3] = [Values[2], Values[3], Values[0]]
            Commands[Step, Tyre]['iterations'][0] = 2
    if CommandType.itemsize != 80 or ParticleType.itemsize != 80 or ResultType.itemsize != 32:
        raise RuntimeError('Unexpected solid tyre ABI size')
    Input, Output = Scratch / 'commands.bin', Scratch / 'readback.bin'
    Input.write_bytes(struct.pack('<II', TyreCount, StepCount) + Commands.tobytes())
    subprocess.run([str(Validation), str(Reset), str(Advance), str(Surface), str(Stress), str(Input), str(Output)], cwd=Root, check=True)
    Readback = Output.read_bytes()
    ParticleBytes = TyreCount * ParticleCount * ParticleType.itemsize
    ResultBytes = TyreCount * ResultType.itemsize
    if len(Readback) != StepCount * (ParticleBytes + ResultBytes) + 4096 * 32 + 64 * 48:
        raise RuntimeError('Unexpected solid tyre, surface and stress readback byte count')
    WorstLocal = WorstWorld = WorstForce = WorstJacobian = WorstStress = 0.0
    LargestMovement = StaticError = 0.0
    for Step in range(StepCount):
        Offset = Step * (ParticleBytes + ResultBytes)
        Particles = np.frombuffer(Readback, dtype=ParticleType, count=TyreCount * ParticleCount,
                                  offset=Offset).reshape(TyreCount, ParticleCount)
        Results = np.frombuffer(Readback, dtype=ResultType, count=TyreCount, offset=Offset + ParticleBytes)
        if not np.isfinite(Results['force_status_jacobian_stress']).all() or not np.isfinite(Particles['local']).all() or not np.isfinite(Particles['world']).all():
            raise RuntimeError('Nonfinite solid tyre result')
        for Tyre in range(TyreCount):
            Values, Expected = Cases[Tyre % 12, Step]
            Result = Results[Tyre]['force_status_jacobian_stress']
            WorstLocal = max(WorstLocal, float(np.max(np.abs(Particles[Tyre]['local'][:, :3] - Expected[:, :3]))))
            WorstWorld = max(WorstWorld, float(np.max(np.abs(Particles[Tyre]['world'][:, :3] - Expected[:, 3:]))))
            WorstForce = max(WorstForce, abs(float(Result[0]) - Values[14]))
            WorstJacobian = max(WorstJacobian, abs(float(Result[2]) - Values[15]))
            WorstStress = max(WorstStress, abs(float(Result[3]) - Values[16]))
            if Results[Tyre]['contacts'][0] != Values[17] or Result[1] != 1:
                raise RuntimeError('Solid contact count or command status mismatch')
            LargestMovement = max(LargestMovement, float(np.max(np.abs(Particles[Tyre]['local'][:, :3] - Particles[Tyre]['rest'][:, :3]))))
            if Tyre % 12 == 0:
                StaticError = max(StaticError, float(np.max(np.abs(Particles[Tyre]['local'][:, :3] - Particles[Tyre]['rest'][:, :3]))))
            if Tyre >= 12 and (Particles[Tyre].tobytes() != Particles[Tyre % 12].tobytes() or
                               Results[Tyre].tobytes() != Results[Tyre % 12].tobytes()):
                raise RuntimeError('Identical solid tyres differed across invocation slots')
    print(f'TYRE_SOLID_JS cases=12 duplicated_tyres={TyreCount} steps={StepCount} '
          f'particle_snapshots={TyreCount * StepCount * ParticleCount} max_local_m={WorstLocal:.9g} '
          f'max_world_m={WorstWorld:.9g} max_force_n={WorstForce:.9g} '
          f'max_jacobian={WorstJacobian:.9g} max_stress={WorstStress:.9g} contacts_exact=1', flush=True)
    print(f'TYRE_SOLID_INVARIANTS duplicated_states_exact=1 static_error_m={StaticError:.9g} '
          f'largest_deformation_m={LargestMovement:.9g}', flush=True)
    if WorstLocal > 0.0003 or WorstWorld > 0.0003 or WorstForce > 0.02 or WorstJacobian > 0.001 or WorstStress > 1.0 or StaticError > 0.00001 or LargestMovement < 0.005:
        raise RuntimeError('Solid JavaScript comparison or independent state invariant failed')
    print(f'TYRE_SOLID_READBACK_SHA256 {hashlib.sha256(Readback).hexdigest()}', flush=True)
    print('TYRE_SOLID_SUMMARY PASS direct_slang_spirv=1 no_vehicle_or_jolt=1', flush=True)


if __name__ == '__main__':
    Main()
