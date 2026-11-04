#!/usr/bin/env python3
#==========================================================================================================================================
#                                            TYRESURFACESHADERVALIDATION.PY
#==========================================================================================================================================
# 🧩 Compare Engine tyre projection against the unchanged demo WGSL executed on Vulkan.

import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import sys

import numpy as np
import wgpu

Root = Path(__file__).resolve().parents[3]
Reference = Root / 'Experimental/VehicleHmiReference/Shader/VehicleVisual.wgsl'
ExpectedBlob = '22359e7e799da472c7cb4e6a7d518458278c5813'

#-----------------------------------------------------------------------------------------------------------------------
#                                                     REFERENCE SHADER
#-----------------------------------------------------------------------------------------------------------------------


def ConstructShader():
    Blob = subprocess.check_output(['git', 'hash-object', str(Reference)], cwd=Root, text=True).strip()
    if Blob != ExpectedBlob:
        raise RuntimeError('Retained VehicleVisual WGSL blob differs from the pinned source')
    Source = Reference.read_text()
    Boundary = Source.rfind('\n//', 0, Source.index('fn TurnAboutAxis('))
    Projection = Source[:Boundary]
    if Projection.count('fn ResolveWheelVertex(') != 1:
        raise RuntimeError('Reference projection function was not found exactly once')
    return Projection + '''
const TyreFamily: u32 = 1u;
struct VertexProbe { Position: vec4f, Normal: vec4f, }
@group(0) @binding(0) var<storage, read> VehicleState: VehicleRecord;
@group(0) @binding(1) var<storage, read> Vertices: array<VertexProbe>;
@group(0) @binding(2) var<storage, read_write> Projected: array<VertexProbe>;
@compute @workgroup_size(64)
fn CompareTyreProjection(@builtin(global_invocation_id) Global: vec3u) {
    let Index = Global.x;
    if (Index >= arrayLength(&Vertices)) { return; }
    let Sample = Vertices[Index];
    let Output = ResolveWheelVertex(0u, u32(Sample.Position.w), Sample.Position.xyz, Sample.Normal.xyz);
    Projected[Index].Position = vec4f(Output.Position, 0.0);
    Projected[Index].Normal = vec4f(Output.Normal, 0.0);
}
'''


def ConstructVertices(Count):
    Index = np.arange(Count, dtype=np.float64)
    Angle = 2 * math.pi * ((Index + .371) / Count) - math.pi
    Radii = np.array([.32, .3447, .345, .37, .40, .45, .49, .50855], dtype=np.float64)
    Radius = Radii[np.arange(Count) % len(Radii)]
    Samples = np.zeros((Count, 2, 4), dtype=np.float32)
    Samples[:, 0, 0] = Radius * np.cos(Angle)
    Samples[:, 0, 1] = -.13 + .26 * ((Index * 37) % Count) / (Count - 1)
    Samples[:, 0, 2] = Radius * np.sin(Angle)
    Samples[:, 0, 3] = (np.arange(Count) % 13 != 0).astype(np.float32)
    Normals = np.stack([np.cos(Angle), .15 * np.sin(3 * Angle), np.sin(Angle)], axis=1)
    Normals /= np.linalg.norm(Normals, axis=1, keepdims=True)
    Samples[:, 1, :3] = Normals.astype(np.float32)
    return Samples


def ConstructDisplacements():
    Sector = np.repeat(np.arange(32), 5).astype(np.float64)
    Ring = np.tile(np.arange(5), 32).astype(np.float64)
    Angle = 2 * math.pi * Sector / 32
    Displacements = np.zeros((160, 4), dtype=np.float32)
    Displacements[:, 0] = .018 * np.cos(Angle) * np.sin(Ring * .7)
    Displacements[:, 1] = .012 * np.sin(Angle * 2) * (Ring - 2) / 2
    Displacements[:, 2] = .016 * np.sin(Angle) * np.cos(Ring * .6)
    return Displacements


#-----------------------------------------------------------------------------------------------------------------------
#                                               VULKAN AND NATIVE COMPARISON
#-----------------------------------------------------------------------------------------------------------------------


def Validate(Probe, Scratch, PriorProbe):
    Adapter = wgpu.gpu.request_adapter_sync(power_preference='low-power')
    print('ADAPTER', dict(Adapter.info), flush=True)
    Device = Adapter.request_device_sync()
    Module = Device.create_shader_module(code=ConstructShader())
    Pipeline = Device.create_compute_pipeline(layout='auto', compute={'module': Module, 'entry_point': 'CompareTyreProjection'})
    Samples = ConstructVertices(2048)
    ReferenceFields = json.loads(subprocess.check_output(
        ['node', 'Exhibits/VehiclePhysics/Code/TyreProjectionReference.mjs'], cwd=Root, text=True))
    Fields = {'synthetic': ConstructDisplacements()}
    for Label in ('xpbd', 'solid'):
        Fields[Label] = np.asarray(ReferenceFields[Label], dtype=np.float32).reshape((160, 4))
    Cases = [
        ('spring-nominal', (0, .035, 220000, .02), 0, 0, 'synthetic'),
        ('spring-flat', (.5, .06, 0, -.04), 0, 0, 'synthetic'),
        ('spring-high-pressure', (-2.1, .014, 350000, .01), 0, 0, 'synthetic'),
        ('spring-near-zero', (0, .000011, 220000, 0), 0, 0, 'synthetic'),
        ('spring-zero-deflection', (1.2, 0, 220000, .02), 0, 0, 'synthetic'),
        ('spring-shredded', (0, .035, 220000, .02), 1, 0, 'synthetic'),
        ('continuum-field', (.4, .035, 220000, .02), 0, 1, 'synthetic'),
        ('continuum-xpbd', (0, .045, 220000, .02), 0, 1, 'xpbd'),
        ('continuum-solid', (0, .045, 220000, .02), 0, 1, 'solid'),
        ('continuum-zero-deflection', (0, 0, 220000, 0), 0, 1, 'synthetic'),
        ('continuum-shredded', (0, .035, 220000, 0), 1, 1, 'synthetic'),
    ]
    Usage = wgpu.BufferUsage.STORAGE | wgpu.BufferUsage.COPY_SRC | wgpu.BufferUsage.COPY_DST
    VertexBuffer = Device.create_buffer_with_data(data=Samples.tobytes(), usage=Usage)
    ResultBuffer = Device.create_buffer(size=Samples.nbytes, usage=Usage)
    Scratch.mkdir(parents=True, exist_ok=True)
    WorstPosition = WorstNormal = 0.0
    for Label, Deformation, Shredded, Continuum, Field in Cases:
        Displacements = Fields[Field]
        Profile = np.asarray(Deformation, dtype=np.float32)
        Vehicle = np.zeros((5 + 4 + 4 + 640, 4), dtype=np.float32)
        Vehicle[5, :2] = [Shredded, Continuum]
        Vehicle[9] = Profile
        Vehicle[13:13 + 160] = Displacements
        VehicleBuffer = Device.create_buffer_with_data(data=Vehicle.tobytes(), usage=Usage)
        Group = Device.create_bind_group(layout=Pipeline.get_bind_group_layout(0), entries=[
            {'binding': 0, 'resource': {'buffer': VehicleBuffer}},
            {'binding': 1, 'resource': {'buffer': VertexBuffer}},
            {'binding': 2, 'resource': {'buffer': ResultBuffer}},
        ])
        Encoder = Device.create_command_encoder()
        Compute = Encoder.begin_compute_pass()
        Compute.set_pipeline(Pipeline)
        Compute.set_bind_group(0, Group)
        Compute.dispatch_workgroups((len(Samples) + 63) // 64)
        Compute.end()
        Device.queue.submit([Encoder.finish()])
        Actual = np.frombuffer(Device.queue.read_buffer(ResultBuffer), dtype=np.float32).copy().reshape(Samples.shape)
        Fixture = struct.pack('<4f4I', *Profile, Shredded, Continuum, len(Samples), 0)
        Input = Scratch / 'projection-input.bin'
        Output = Scratch / 'projection-output.bin'
        Input.write_bytes(Fixture + Displacements.tobytes() + Samples.tobytes())
        subprocess.run([str(Probe), str(Input), str(Output)], check=True)
        NativeBytes = Output.read_bytes()
        if PriorProbe is not None:
            PriorOutput = Scratch / 'prior-output.bin'
            subprocess.run([str(PriorProbe), str(Input), str(PriorOutput)], check=True)
            if PriorOutput.read_bytes() != NativeBytes:
                raise RuntimeError(f'{Label}: standalone Engine output differs from prior Engine header')
        Expected = np.frombuffer(NativeBytes, dtype=np.float32).reshape(Samples.shape)
        if not np.isfinite(Actual).all() or not np.isfinite(Expected).all():
            raise RuntimeError(f'{Label}: nonfinite projection')
        Difference = np.abs(Actual.astype(np.float64) - Expected.astype(np.float64))
        PositionError = float(np.max(Difference[:, 0, :3]))
        NormalError = float(np.max(Difference[:, 1, :3]))
        WorstPosition = max(WorstPosition, PositionError)
        WorstNormal = max(WorstNormal, NormalError)
        if PositionError > 3e-4 or NormalError > 3e-4:
            PositionIndex = np.unravel_index(np.argmax(Difference[:, 0, :3]), Difference[:, 0, :3].shape)[0]
            NormalIndex = np.unravel_index(np.argmax(Difference[:, 1, :3]), Difference[:, 1, :3].shape)[0]
            print('POSITION_MISMATCH', PositionIndex, Samples[PositionIndex], Actual[PositionIndex], Expected[PositionIndex])
            print('NORMAL_MISMATCH', NormalIndex, Samples[NormalIndex], Actual[NormalIndex], Expected[NormalIndex])
            raise RuntimeError(f'{Label}: GPU/native difference {PositionError}, {NormalError}')
        Other = Samples[:, 0, 3] == 0
        if not np.array_equal(Actual[Other, 0, :3], Samples[Other, 0, :3]):
            raise RuntimeError(f'{Label}: non-tyre position changed')
        if Shredded and np.any(Actual[~Other, 0, :3] != 0):
            raise RuntimeError(f'{Label}: shredded tyre did not collapse')
        if Deformation[1] == 0 and not np.array_equal(Actual[:, 0, :3], Samples[:, 0, :3]):
            raise RuntimeError(f'{Label}: unloaded position changed')
        if Label in ('spring-nominal', 'spring-flat', 'spring-high-pressure', 'continuum-field',
                     'continuum-xpbd', 'continuum-solid'):
            Displacement = np.abs(Actual[:, 0, :3] - Samples[:, 0, :3])
            if np.max(Displacement) < .005:
                raise RuntimeError(f'{Label}: nontrivial tyre deformation was not exercised')
        print(f'TYRE_PROJECTION case={Label} vertices={len(Samples)} max_position_m={PositionError:.9g} '
              f'max_normal={NormalError:.9g}', flush=True)
    print(f'TYRE_PROJECTION_SUMMARY cases={len(Cases)} vertices={len(Cases) * len(Samples)} '
          f'worst_position_m={WorstPosition:.9g} worst_normal={WorstNormal:.9g} '
          'non_tyre_shredded_and_unloaded_guards=1', flush=True)
    print(f'TYRE_PRIOR_HEADER_PARITY checked={int(PriorProbe is not None)} cases={len(Cases)}', flush=True)
    print('REFERENCE_WGSL_SHA256', hashlib.sha256(Reference.read_bytes()).hexdigest())


if __name__ == '__main__':
    if len(sys.argv) not in (3, 4):
        raise SystemExit('Expected standalone probe, scratch directory and optional prior-header probe')
    PriorProbe = Path(sys.argv[3]).resolve() if len(sys.argv) == 4 else None
    Validate(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve(), PriorProbe)
