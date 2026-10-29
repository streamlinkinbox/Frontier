#!/usr/bin/env python3
#==========================================================================================================================================
#                                             TYREMEMBRANEDEVICEREFERENCE.PY
#==========================================================================================================================================
# 🧩 Compare Engine membrane GPU trajectories against the unchanged demo continuum-shell class.
import hashlib
import struct
import subprocess
import sys
from pathlib import Path

import numpy as np
from TyreProjectionDeviceReference import CommandType, ParticleType, ParticleCount, StepCount, TyreCount

Root = Path(__file__).resolve().parents[3]


def ReadReference():
    Text = subprocess.check_output(['node', 'Exhibits/VehiclePhysics/Code/TyreMembraneReference.mjs'],
                                   cwd=Root, text=True)
    Lines = iter(Text.splitlines())
    Cases = {}
    for Line in Lines:
        Fields = Line.split()
        if len(Fields) != 22 or Fields[0:2] != ['C', 'membrane']:
            raise RuntimeError('Unexpected JavaScript membrane header')
        Case, Step = int(Fields[2]), int(Fields[3])
        Inputs = np.array([float(Component) for Component in Fields[4:]], dtype=np.float64)
        Coordinates = np.empty((ParticleCount, 6), dtype=np.float64)
        for N in range(ParticleCount):
            Components = next(Lines).split()
            if len(Components) != 7 or Components[0] != 'N':
                raise RuntimeError('Unexpected JavaScript particle coordinates')
            Coordinates[N] = [float(Component) for Component in Components[1:]]
        Cases[Case, Step] = Inputs, Coordinates
    if len(Cases) != 12 * StepCount:
        raise RuntimeError('Incomplete membrane observations')
    return Cases


def Main():
    if len(sys.argv) != 6:
        raise RuntimeError('Expected Vulkan validation, reset, advance, surface SPIR-V and scratch directory')
    Validation, Reset, Advance, Surface = (Path(PathName).resolve() for PathName in sys.argv[1:5])
    Scratch = Path(sys.argv[5]).resolve()
    Scratch.mkdir(parents=True, exist_ok=True)
    Cases = ReadReference()
    Commands = np.zeros((StepCount, TyreCount), dtype=CommandType)
    for Step in range(StepCount):
        for Tyre in range(TyreCount):
            Inputs, _ = Cases[Tyre % 12, Step]
            Commands[Step, Tyre]['centre'][:3] = Inputs[4:7]
            Commands[Step, Tyre]['orientation'] = Inputs[7:11]
            Commands[Step, Tyre]['ground'][:3] = Inputs[11:14]
            Commands[Step, Tyre]['ground'][3] = Inputs[1]
            Commands[Step, Tyre]['dynamics'][:3] = [Inputs[2], Inputs[3], Inputs[0]]
            Commands[Step, Tyre]['iterations'][0] = 2
    if CommandType.itemsize != 80 or ParticleType.itemsize != 80:
        raise RuntimeError('Unexpected membrane device ABI')
    Input, Output = Scratch / 'commands.bin', Scratch / 'readback.bin'
    Input.write_bytes(struct.pack('<II', TyreCount, StepCount) + Commands.tobytes())
    subprocess.run([str(Validation), str(Reset), str(Advance), str(Surface), str(Input), str(Output), 'membrane'],
                   cwd=Root, check=True)
    Readback = Output.read_bytes()
    ParticleBytes = TyreCount * ParticleCount * ParticleType.itemsize
    ResultBytes = TyreCount * 16
    if len(Readback) != StepCount * (ParticleBytes + ResultBytes):
        raise RuntimeError('Unexpected membrane readback byte count')
    WorstLocal = WorstWorld = WorstForce = 0.0
    LargestMovement = StaticError = 0.0
    for Step in range(StepCount):
        Offset = Step * (ParticleBytes + ResultBytes)
        Particles = np.frombuffer(Readback, dtype=ParticleType, count=TyreCount * ParticleCount,
                                  offset=Offset).reshape(TyreCount, ParticleCount)
        Results = np.frombuffer(Readback, dtype='<f4', count=TyreCount * 4,
                                offset=Offset + ParticleBytes).reshape(TyreCount, 4)
        if not np.isfinite(Results).all() or not np.isfinite(Particles['local']).all() or not np.isfinite(Particles['world']).all():
            raise RuntimeError('Nonfinite membrane result')
        for Tyre in range(TyreCount):
            Inputs, Expected = Cases[Tyre % 12, Step]
            WorstLocal = max(WorstLocal, float(np.max(np.abs(Particles[Tyre]['local'][:, :3] - Expected[:, :3]))))
            WorstWorld = max(WorstWorld, float(np.max(np.abs(Particles[Tyre]['world'][:, :3] - Expected[:, 3:]))))
            WorstForce = max(WorstForce, abs(float(Results[Tyre, 0]) - Inputs[14]))
            LargestMovement = max(LargestMovement, float(np.max(np.abs(Particles[Tyre]['local'][:, :3] - Particles[Tyre]['rest'][:, :3]))))
            if Tyre % 12 == 0:
                StaticError = max(StaticError, float(np.max(np.abs(Particles[Tyre]['local'][:, :3] - Particles[Tyre]['rest'][:, :3]))))
            if Tyre >= 12 and (Particles[Tyre].tobytes() != Particles[Tyre % 12].tobytes() or
                               Results[Tyre].tobytes() != Results[Tyre % 12].tobytes()):
                raise RuntimeError('Identical membrane tyres differed across invocation slots')
    print(f'TYRE_MEMBRANE_JS cases=12 duplicated_tyres={TyreCount} steps={StepCount} '
          f'particle_snapshots={TyreCount * StepCount * ParticleCount} max_local_m={WorstLocal:.9g} '
          f'max_world_m={WorstWorld:.9g} max_force_n={WorstForce:.9g}', flush=True)
    print(f'TYRE_MEMBRANE_INVARIANTS duplicated_states_exact=1 static_error_m={StaticError:.9g} '
          f'largest_deformation_m={LargestMovement:.9g}', flush=True)
    if WorstLocal > 0.0003 or WorstWorld > 0.0003 or WorstForce > 0.02 or StaticError > 0.00001 or LargestMovement < 0.005:
        raise RuntimeError('Membrane JavaScript comparison or independent state invariant failed')
    print(f'TYRE_MEMBRANE_READBACK_SHA256 {hashlib.sha256(Readback).hexdigest()}', flush=True)
    print('TYRE_MEMBRANE_SUMMARY PASS direct_slang_spirv=1 no_vehicle_or_jolt=1', flush=True)


if __name__ == '__main__':
    Main()
