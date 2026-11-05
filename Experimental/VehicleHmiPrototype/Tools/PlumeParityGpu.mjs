//============================================================================================================================================
//                                                          PLUMEPARITYGPU.MJS
//============================================================================================================================================
// 🧩 Runs the demo's own smoke-and-fire plume (SmokeSequence over PlumeSpace, the five Plume*.wgsl programs) on a real (software) GPU through Dawn,
//    drives it with the plume exhibit's burnout script, and writes what the C++ port must reproduce: the step records, the lattices after the last
//    transport step, the light lattice, the pressure, curl and divergence regions, the coverage box and the march of the last refresh.
//
//        node RunOnSoftwareGpu.mjs node PlumeParityGpu.mjs <output folder> [grade low|medium|high] [refreshes] [trace WxH] [screen WxH]
//
//    Exhibits/PlumeSimulation/Code/RunPlumeParity.sh runs this and then PlumeSimulationCapture's parity mode against the folder it writes.
//
// 📝 The scene the plume normally shares with the illumination port is stood in by the smallest resources the programs bind: a scene distance field
//    of zero cascades (the analytic ground plane at z = 0 answers every sample and no occupant is ever indexed), no opaque surface (depth 0), a
//    shadow with no cascade (the sun is always seen) and a one-texel sky. Those exist identically on the C++ side, so the plume's own arithmetic
//    is what is compared.

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { create, globals } from "webgpu";

Object.assign(globalThis, globals);
const Here = path.dirname(fileURLToPath(import.meta.url));
const Root = path.resolve(Here, "..");
const Gpu  = create([`backend=${process.env.BACKEND || "vulkan"}`]);
Object.defineProperty(globalThis, "navigator", { value: { gpu: Gpu, userAgent: "node" }, configurable: true });
globalThis.fetch = async (Location) =>
{
    const File = path.join(Root, String(Location).split("?")[0]);
    if (!fs.existsSync(File)) return { ok: false, status: 404, text: async () => "", arrayBuffer: async () => new ArrayBuffer(0) };
    const Bytes = fs.readFileSync(File);
    return { ok: true, status: 200, text: async () => Bytes.toString("utf8"), arrayBuffer: async () => Bytes.buffer.slice(Bytes.byteOffset, Bytes.byteOffset + Bytes.byteLength) };
};

const Import = (Relative) => import(pathToFileURL(path.resolve(Root, Relative)).href);
const { DeviceExchange }    = await Import("Source/DeviceExchange.js");
const { ShaderInterchange } = await Import("Source/ShaderInterchange.js");
const { SmokeSequence }     = await Import("Source/SmokeSequence.js");
const { ConstructViewTransform, ConstructPerspectiveTransform, MultiplyTransform, InvertTransform } = await Import("Source/TransformProjection.js");
const { Fnv1a64, Mulberry32, BurnoutInputs, LightDirection } = await Import("../../Exhibits/PlumeSimulation/Code/PlumeScenario.mjs");

const Output    = path.resolve(process.argv[2] || "plume-parity");
const GradeName = process.argv[3] || "low";
const Refreshes = Number(process.argv[4] || 70);
const Parse     = (Text, Fallback) => (Text || Fallback).split("x").map(Number);
const Trace     = Parse(process.argv[5], "128x80");
const Screen    = Parse(process.argv[6], "256x160");
fs.mkdirSync(Output, { recursive: true });

//--------------------------------------------------------------------------------------------------------------------------------------------
//                                                            THE DEVICE
//--------------------------------------------------------------------------------------------------------------------------------------------

const Exchange = new DeviceExchange();
await Exchange.ConstructDevice();
const Device = Exchange.Device;
// 📝 The lattices and regions are read back: the probe adds the copy-source usage the demo's own resources do not ask for.
const InheritedTexture = Exchange.ConstructTexture.bind(Exchange);
Exchange.ConstructTexture = (Descriptor) => InheritedTexture({ ...Descriptor, usage: Descriptor.usage | GPUTextureUsage.COPY_SRC });
const InheritedRecords = Exchange.ConstructRecords.bind(Exchange);
Exchange.ConstructRecords = (ByteExtent, Usage, Label = "") => InheritedRecords(ByteExtent, Usage | GPUBufferUsage.COPY_SRC, Label);

const Shaders = new ShaderInterchange(Exchange, "Shader");
await Shaders.RetrieveSources(fs.readdirSync(path.join(Root, "Shader")).filter((Name) => Name.endsWith(".wgsl")));
const InheritedModule = Shaders.ConstructModule.bind(Shaders);
Shaders.ConstructModule = (FileName, Constants = {}, Label = FileName) => InheritedModule(FileName, { MaximumTraversalSteps: { Type: "u32", Value: 96 }, ...Constants }, Label);

// ① the scene distance field's group: zero cascades, the ground plane at z = 0
const Uniform = GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, Storage = GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST;
const DistanceRecords = Exchange.ConstructRecords(160, Uniform, "Distance record");
const ClipViews = [0, 1, 2, 3].map((Level) => Exchange.ConstructTexture({ label: `Clip ${Level}`, size: [1, 1, 1], dimension: "3d", format: "rgba16float",
                                                                          usage: GPUTextureUsage.TEXTURE_BINDING }).createView({ dimension: "3d" }));
const CascadeSampler = Exchange.ConstructSampler({ magFilter: "linear", minFilter: "linear", addressModeU: "repeat", addressModeV: "repeat", addressModeW: "repeat" });
const EpochRecords   = Exchange.ConstructRecords(16, Storage, "Brick stamps");
const TraversalLayout = Exchange.ConstructDescriptorLayout(["Uniform", "Texture3D", "Texture3D", "Texture3D", "Texture3D", "Sampler", "StorageRead"],
                                                           GPUShaderStage.COMPUTE | GPUShaderStage.FRAGMENT, "Distance traversal");
const DistanceGroup = Exchange.ConstructDescriptorGroup(TraversalLayout, [DistanceRecords, ...ClipViews, CascadeSampler, EpochRecords]);
const OccupantRecords = Exchange.ConstructRecords(336 * 4, Storage, "Occupants");

// ② what the march reads beside the plume: the frame record, a surface with no depth, a shadow with no cascade, a one-texel sky
const FrameRecords = Exchange.ConstructRecords(1072, Uniform, "Frame record");
const DepthTexture = Exchange.ConstructTexture({ label: "Surface depth", size: Screen, format: "depth32float", usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.RENDER_ATTACHMENT });
const ShadowTexture = Exchange.ConstructTexture({ label: "Shadow depth", size: [1, 1, 1], format: "depth32float", usage: GPUTextureUsage.TEXTURE_BINDING });
const SkyTexture = Exchange.ConstructTexture({ label: "Sky view", size: [1, 1], format: "rgba8unorm", usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST });
Device.queue.writeTexture({ texture: SkyTexture }, new Uint8Array([115, 158, 230, 255]), { bytesPerRow: 4 }, [1, 1]);
const Surface    = { Views: { Depth: DepthTexture.createView({ aspect: "depth-only" }) } };
const Shadow     = { LayeredView: ShadowTexture.createView({ dimension: "2d-array", aspect: "depth-only" }),
                     ComparisonSampler: Exchange.ConstructSampler({ magFilter: "linear", minFilter: "linear", compare: "less" }),
                     ReceiverRecords: Exchange.ConstructRecords(224, Uniform, "Cascade receiver") };
const Atmosphere = { SkyView: SkyTexture.createView(), SkySampler: Exchange.ConstructSampler({ magFilter: "linear", minFilter: "linear" }) };

// ③ the plume itself
const Smoke = new SmokeSequence(Exchange, Shaders, GradeName);
await Smoke.ConstructPrograms(FrameRecords, TraversalLayout, Atmosphere, OccupantRecords, Shadow);
Smoke.ConstructTargets(Trace, FrameRecords, Surface, Shadow, Atmosphere);

//--------------------------------------------------------------------------------------------------------------------------------------------
//                                                            THE RUN
//--------------------------------------------------------------------------------------------------------------------------------------------

const Noise = Mulberry32(0x51ED270B);
const Steps = [];
for (let Step = 0; Step < Refreshes; ++Step)
{
    const Inputs = BurnoutInputs(Step, Array.from({ length: 8 }, () => Noise()));
    Smoke.Advance(Inputs.Seconds, { Enabled: Inputs.Enabled, Vehicle: Inputs.Vehicle, Light: { Direction: LightDirection }, Gas: Inputs.Gas,
                                    Readings: Inputs.Readings, Thermal: Inputs.Thermal, Emissions: Inputs.Emissions });
    if (Smoke.StepPending)
    {
        Steps.push({ Refresh: Step, Record: Fnv1a64(new Uint8Array(Smoke.RecordBytes)), Sources: Smoke.Sources.Count,
                     SourceHash: Fnv1a64(new Uint8Array(Smoke.Sources.Records.buffer, 0, Smoke.Sources.Count * 12 * 4)) });
        const Encoder = Device.createCommandEncoder();
        Smoke.RecordTransport(Encoder, DistanceGroup);
        Device.queue.submit([Encoder.finish()]);
    }
}
await Device.queue.onSubmittedWorkDone();

// 📝 The camera for the march: from the side, a little behind the car, looking at the plume's foot. Every field the march reads; the rest stay zero.
const Car     = BurnoutInputs(Refreshes - 1, new Array(8).fill(0.5)).Vehicle.Position;
const Eye     = [Car[0] - 3.0, -12.0, 3.2], Target = [Car[0] - 4.0, 0.0, 1.4];
const ViewT   = ConstructViewTransform(Eye, Target);
const Projection = ConstructPerspectiveTransform(0.62, Screen[0] / Screen[1], 0.1);
const ViewProjection = MultiplyTransform(Projection, ViewT);
const FrameBytes = new ArrayBuffer(1072);
const Scalars = new Float32Array(FrameBytes), Words = new Uint32Array(FrameBytes);
const SunDirection = (() => { const L = Math.hypot(...LightDirection); return LightDirection.map((C) => C / L); })();
Scalars.set(ViewProjection, 0);
Scalars.set(InvertTransform(ViewProjection), 16);
Scalars.set(ViewProjection, 32);
Scalars.set(ViewProjection, 48);
Scalars.set(ViewT, 64);
Scalars.set(Eye, 80);
Words[83] = Refreshes;
Scalars.set(SunDirection, 84);
Scalars[87] = Smoke.ElapsedSeconds;
Scalars.set([3.0, 2.9, 2.7], 88);
Scalars[91] = 1.0;
Scalars.set(Screen, 92);
Scalars.set(Screen.map((Extent) => 1 / Extent), 94);
Scalars.set(Trace, 96);
Scalars.set(Trace.map((Extent) => 1 / Extent), 98);
Words[102] = Math.round(Screen[0] / Trace[0]);
Device.queue.writeBuffer(FrameRecords, 0, FrameBytes);

// the march of the last refresh, over the lattices the last transport step left
{
    const Encoder = Device.createCommandEncoder();
    Smoke.RecordMarch(Encoder);
    Device.queue.submit([Encoder.finish()]);
}
await Device.queue.onSubmittedWorkDone();

//--------------------------------------------------------------------------------------------------------------------------------------------
//                                                           THE READBACK
//--------------------------------------------------------------------------------------------------------------------------------------------

async function ReadTexture(Texture, Extent, TexelBytes)
{
    const RowBytes = Extent[0] * TexelBytes, Aligned = Math.ceil(RowBytes / 256) * 256, Depth = Extent[2] || 1;
    const Staging = Device.createBuffer({ size: Aligned * Extent[1] * Depth, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
    const Encoder = Device.createCommandEncoder();
    Encoder.copyTextureToBuffer({ texture: Texture }, { buffer: Staging, bytesPerRow: Aligned, rowsPerImage: Extent[1] }, [Extent[0], Extent[1], Depth]);
    Device.queue.submit([Encoder.finish()]);
    await Staging.mapAsync(GPUMapMode.READ);
    const Padded = new Uint8Array(Staging.getMappedRange().slice(0));
    Staging.unmap();
    Staging.destroy();
    const Packed = new Uint8Array(RowBytes * Extent[1] * Depth);
    for (let Row = 0; Row < Extent[1] * Depth; ++Row) Packed.set(Padded.subarray(Row * Aligned, Row * Aligned + RowBytes), Row * RowBytes);
    return Packed;
}

async function ReadRecords(Records, ByteExtent)
{
    const Staging = Device.createBuffer({ size: ByteExtent, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
    const Encoder = Device.createCommandEncoder();
    Encoder.copyBufferToBuffer(Records, 0, Staging, 0, ByteExtent);
    Device.queue.submit([Encoder.finish()]);
    await Staging.mapAsync(GPUMapMode.READ);
    const Bytes = new Uint8Array(Staging.getMappedRange().slice(0));
    Staging.unmap();
    Staging.destroy();
    return Bytes;
}

const State = Smoke.State;
const Coarse = Smoke.VelocityExtent, Fine = Smoke.ThermoExtent;
const CoarseCells = Coarse[0] * Coarse[1] * Coarse[2];
const Captures = [
    { Name: "Velocity",   Bytes: await ReadTexture(Smoke.VelocityTextures[State], Coarse, 8), Extent: Coarse, Kind: "rgba16f" },
    { Name: "Thermo",     Bytes: await ReadTexture(Smoke.ThermoTextures[State], Fine, 8), Extent: Fine, Kind: "rgba16f" },
    { Name: "Light",      Bytes: await ReadTexture(Smoke.LightTexture, Coarse, 8), Extent: Coarse, Kind: "rgba16f" },
    { Name: "Curl",       Bytes: await ReadRecords(Smoke.CurlRecords, CoarseCells * 16), Extent: [CoarseCells * 4], Kind: "f32" },
    { Name: "Divergence", Bytes: await ReadRecords(Smoke.DivergenceRecords, CoarseCells * 4), Extent: [CoarseCells], Kind: "f32" },
    { Name: "PressureA",  Bytes: await ReadRecords(Smoke.PressureRecords[0], CoarseCells * 4), Extent: [CoarseCells], Kind: "f32" },
    { Name: "PressureB",  Bytes: await ReadRecords(Smoke.PressureRecords[1], CoarseCells * 4), Extent: [CoarseCells], Kind: "f32" },
    { Name: "Coverage",   Bytes: await ReadRecords(Smoke.CoverageRecords, 32), Extent: [8], Kind: "i32" },
    { Name: "March",      Bytes: await ReadTexture(Smoke.MarchTexture, Trace, 8), Extent: Trace, Kind: "rgba16f" }
];

const Lines = [];
Lines.push("# Written by Experimental/VehicleHmiPrototype/Tools/PlumeParityGpu.mjs: what the demo's plume (Dawn on a software GPU) leaves on the device.");
Lines.push("[Run]");
Lines.push(`Grade = "${GradeName}"`);
Lines.push(`Refreshes = ${Refreshes}`);
Lines.push(`TraceExtent = [${Trace.join(", ")}]`);
Lines.push(`ScreenExtent = [${Screen.join(", ")}]`);
Lines.push(`StepsTaken = ${Smoke.StepsTaken}`);
Lines.push(`State = ${State}`);
Lines.push(`Resident = ${Smoke.Resident}`);
Lines.push(`ElapsedSeconds = ${Smoke.ElapsedSeconds}`);
Lines.push(`Origin = [${Smoke.Origin.join(", ")}]`);
Lines.push(`RefreshHash = "${Fnv1a64(new Uint8Array(FrameBytes))}"`);
Lines.push(`FireLightHash = "${Fnv1a64(new Uint8Array(Smoke.FireLightBytes))}"`);
Lines.push(`StepRecordHashes = [${Steps.map((Entry) => `"${Entry.Record}"`).join(", ")}]`);
Lines.push(`StepSourceHashes = [${Steps.map((Entry) => `"${Entry.SourceHash}"`).join(", ")}]`);
Lines.push(`StepSources = [${Steps.map((Entry) => Entry.Sources).join(", ")}]`);
Lines.push(`StepRefreshes = [${Steps.map((Entry) => Entry.Refresh).join(", ")}]`);
fs.writeFileSync(path.join(Output, "Refresh.bin"), new Uint8Array(FrameBytes));
for (const Capture of Captures)
{
    fs.writeFileSync(path.join(Output, `${Capture.Name}.bin`), Capture.Bytes);
    Lines.push("");
    Lines.push(`[${Capture.Name}]`);
    Lines.push(`Kind = "${Capture.Kind}"`);
    Lines.push(`Extent = [${Capture.Extent.join(", ")}]`);
    Lines.push(`Bytes = ${Capture.Bytes.length}`);
    Lines.push(`Hash = "${Fnv1a64(Capture.Bytes)}"`);
}
fs.writeFileSync(path.join(Output, "Reference.toml"), Lines.join("\n") + "\n");
console.log(`PlumeParityGpu: ${Steps.length} steps in ${Refreshes} refreshes on ${Exchange.QueryAdapterDescription ? Exchange.QueryAdapterDescription() : "the device"}; ${Captures.length} captures in ${Output}`);
for (const Capture of Captures) console.log(`  ${Capture.Name.padEnd(11)} ${String(Capture.Bytes.length).padStart(9)} B  ${Fnv1a64(Capture.Bytes)}`);
console.log(`  uncaptured device errors: ${Exchange.ReportedErrors ? Exchange.ReportedErrors.length : "n/a"}`);
process.exit(0);
