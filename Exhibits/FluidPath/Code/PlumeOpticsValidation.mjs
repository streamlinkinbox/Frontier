import assert from 'node:assert/strict';
import fs from 'node:fs';
import { create, globals } from '../../../Experimental/VehicleHmiPrototype/Tools/node_modules/webgpu/index.js';

Object.assign(globalThis, globals);
const Gpu = create(['backend=vulkan', 'enable-dawn-features=allow_unsafe_apis']);
const Adapter = await Gpu.requestAdapter({ forceFallbackAdapter: true });
if (!Adapter) throw new Error('No Vulkan fallback adapter; see exhibit setup instructions');
const Device = await Adapter.requestDevice();
Device.pushErrorScope('validation');
const Records = fs.readFileSync(new URL('../../../Experimental/VehicleHmiPrototype/Shader/PlumeRecords.wgsl', import.meta.url), 'utf8');
const Source = fs.readFileSync(new URL('../../../Experimental/VehicleHmiPrototype/Shader/PlumeAdvection.wgsl', import.meta.url), 'utf8');
const Start = Source.indexOf('fn ResolveOpticalDepth(');
const End = Source.indexOf('\n@compute', Start);
assert.ok(Start >= 0 && End > Start);
const Program = `${Records}
@group(0) @binding(0) var<uniform> Plume: PlumeRecord;
@group(0) @binding(1) var ThermoState: texture_3d<f32>;
@group(0) @binding(2) var LatticeSampler: sampler;
@group(0) @binding(3) var<storage, read_write> Result: array<vec2f>;
${Source.slice(Start, End)}
@compute @workgroup_size(1)
fn main() {
    let Depth = ResolveOpticalDepth(vec3f(3.0, 3.0, 2.0), vec3f(0.0, 0.0, 1.0));
    Result[0] = vec2f(Depth, exp(-Depth * 0.65));
}`;
const Pipeline = Device.createComputePipeline({ layout: 'auto', compute: { module: Device.createShaderModule({ code: Program }), entryPoint: 'main' } });
const U = GPUBufferUsage;
const UniformBytes = new ArrayBuffer(256);
const I = new Int32Array(UniformBytes), F = new Float32Array(UniformBytes);
I.set([4, 4, 4, 0], 8); I.set([8, 8, 8, 0], 12);
F.set([1, 2, 0, 1 / 60], 16);
F.set([1, 0, 3, 0], 40);
const Uniform = Device.createBuffer({ size: 256, usage: U.UNIFORM | U.COPY_DST });
Device.queue.writeBuffer(Uniform, 0, UniformBytes);
const Texture = Device.createTexture({ size: [8, 8, 8], dimension: '3d', format: 'rgba16float', usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST });
const Sampler = Device.createSampler({ magFilter: 'linear', minFilter: 'linear', addressModeU: 'repeat', addressModeV: 'repeat', addressModeW: 'clamp-to-edge' });
const Result = Device.createBuffer({ size: 8, usage: U.STORAGE | U.COPY_SRC });
const Group = Device.createBindGroup({ layout: Pipeline.getBindGroupLayout(0), entries: [
    { binding: 0, resource: { buffer: Uniform } }, { binding: 1, resource: Texture.createView() },
    { binding: 2, resource: Sampler }, { binding: 3, resource: { buffer: Result } }
] });
async function Measure(HalfDensity, HalfSoot) {
    const Data = new Uint16Array(8 * 8 * 8 * 4);
    for (let J = 0; J < 8 * 8 * 8; J++) { Data[J * 4] = HalfDensity; Data[J * 4 + 3] = HalfSoot; }
    Device.queue.writeTexture({ texture: Texture }, Data, { bytesPerRow: 64, rowsPerImage: 8 }, [8, 8, 8]);
    const Staging = Device.createBuffer({ size: 8, usage: U.COPY_DST | U.MAP_READ });
    const Encoder = Device.createCommandEncoder();
    const Compute = Encoder.beginComputePass();
    Compute.setPipeline(Pipeline); Compute.setBindGroup(0, Group); Compute.dispatchWorkgroups(1); Compute.end();
    Encoder.copyBufferToBuffer(Result, 0, Staging, 0, 8);
    Device.queue.submit([Encoder.finish()]);
    await Staging.mapAsync(GPUMapMode.READ);
    const Output = [...new Float32Array(Staging.getMappedRange().slice(0))];
    Staging.unmap(); Staging.destroy();
    return Output;
}
const Cases = [
    { Name: 'clear', Density: 0, Soot: 0, Depth: 0 },
    { Name: 'smoke', Density: 0x3c00, Soot: 0, Depth: 4.8 },
    { Name: 'double smoke', Density: 0x4000, Soot: 0, Depth: 9.6 },
    { Name: 'soot', Density: 0, Soot: 0x3c00, Depth: 14.4 }
];
for (const Case of Cases) {
    const [Depth, Transmittance] = await Measure(Case.Density, Case.Soot);
    assert.ok(Math.abs(Depth - Case.Depth) < 1e-5, `${Case.Name} depth ${Depth}`);
    assert.ok(Math.abs(Transmittance - Math.exp(-0.65 * Case.Depth)) < 1e-6, `${Case.Name} transmittance ${Transmittance}`);
    console.log(`${Case.Name}: optical_depth=${Depth.toFixed(6)} sun_transmittance=${Transmittance.toFixed(6)}`);
}
Uniform.destroy(); Texture.destroy(); Result.destroy(); await Device.queue.onSubmittedWorkDone();
const ValidationError = await Device.popErrorScope();
assert.equal(ValidationError, null, ValidationError?.message);
Device.destroy();
console.log('PASS extracted vehicle plume optical-depth function on software Vulkan');
