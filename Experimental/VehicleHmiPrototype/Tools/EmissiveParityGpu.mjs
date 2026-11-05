//============================================================================================================================================
//                                                          EMISSIVEPARITYGPU.MJS
//============================================================================================================================================
// 🧩 Runs the REAL WGSL (the block between BEGIN/END EMISSIVE SAMPLING in RayTraversal.wgsl, extracted verbatim) on a software GPU for random shading points and random draws, and prints
//    it beside the CPU twin (EmissiveReference.mjs) over the same table bytes: the sampled point, its pdf and radiance, whether a cell is usable, and the exact listed-emitter test.
//    node RunOnSoftwareGpu.mjs node EmissiveParityGpu.mjs

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { create, globals } from "webgpu";

Object.assign(globalThis, globals);
const Root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
Object.defineProperty(globalThis, "navigator", { value: { gpu: create([`backend=${process.env.BACKEND || "vulkan"}`]), userAgent: "node" }, configurable: true });
globalThis.fetch = async (Location) =>
{
    const File = path.join(Root, String(Location).split("?")[0]);
    if (!fs.existsSync(File)) return { ok: false, status: 404, text: async () => "" };
    return { ok: true, status: 200, text: async () => fs.readFileSync(File, "utf8") };
};
const Load = (Name) => import(pathToFileURL(path.join(Root, "Source", Name)).href);
const { DeviceExchange } = await Load("DeviceExchange.js");
const { ShaderInterchange } = await Load("ShaderInterchange.js");
const { LightClusterSpace, LightClusterBounds } = await Load("LightClusterSpace.js");
const { EmissiveLightSpace } = await Load("EmissiveLightSpace.js");
const { SampleEmissiveLight, CellWords, IsListedEmitter } = await import("./EmissiveReference.mjs");

const Exchange = new DeviceExchange();
await Exchange.ConstructDevice();
const Device = Exchange.Device;
const Shaders = new ShaderInterchange(Exchange, "Shader");
await Shaders.RetrieveSources(["FrameDescriptor.wgsl", "SceneRecords.wgsl", "LightClusterSpace.wgsl"]);

// the block, verbatim
const Traversal = fs.readFileSync(path.join(Root, "Shader", "RayTraversal.wgsl"), "utf8");
const Begin = Traversal.indexOf("// BEGIN EMISSIVE SAMPLING"), End = Traversal.indexOf("// END EMISSIVE SAMPLING");
if (Begin < 0 || End < Begin) throw new Error("emissive sampling markers not found in RayTraversal.wgsl");
const Block = Traversal.slice(Begin, End);
Shaders.Sources.set("EmissiveParityProbe.wgsl", `//! include "FrameDescriptor.wgsl"
//! include "LightClusterSpace.wgsl"
@group(3) @binding(3) var<uniform> SceneLights : LightClusterRecords;
@group(1) @binding(0) var<storage, read> Occupants : array<OccupantRecord>;
@group(1) @binding(1) var<storage, read> RayMaterials : array<MaterialRecord>;
${Block}
@group(0) @binding(1) var<storage, read> Queries : array<vec4f>;            // per query: position, four draws, (occupant, BVH triangle)
@group(0) @binding(2) var<storage, read_write> Results : array<vec4f>;      // per query: (valid, pdf, usable, listed), position, normal, radiance
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) Id: vec3u)
{
    let Count = arrayLength(&Queries) / 3u;
    if (Id.x >= Count) { return; }
    let Position = Queries[3u * Id.x].xyz;
    let Draw     = Queries[3u * Id.x + 1u];
    let Member   = Queries[3u * Id.x + 2u];
    let Words    = ResolveEmissiveCell(Position);
    let Sample   = SampleEmissiveLight(Position, Words, Draw);
    let Listed   = IsListedEmitter(Words, u32(Member.x + 0.5), bitcast<u32>(Member.y));
    Results[4u * Id.x]      = vec4f(select(0.0, 1.0, Sample.Valid), Sample.Pdf, select(0.0, 1.0, EmissiveUsable(Words)), select(0.0, 1.0, Listed));
    Results[4u * Id.x + 1u] = vec4f(Sample.Position, 0.0);
    Results[4u * Id.x + 2u] = vec4f(Sample.Normal, 0.0);
    Results[4u * Id.x + 3u] = vec4f(Sample.Radiance, 0.0);
}`);

// a synthetic world: three facet-set kinds of emitters with scale and rotation, 14 of them (more than the eight slots) spread over the pad, a car-like set with several clusters
const Rect = (W, H, Family) => { const A = [-W / 2, -H / 2, 0], B = [W / 2, -H / 2, 0], C = [W / 2, H / 2, 0], D = [-W / 2, H / 2, 0]; return [{ Corners: [A, C, B], Family }, { Corners: [A, D, C], Family }]; };
const Many = []; for (let I = 0; I < 20; ++I) Many.push({ Corners: [[I * 0.4, 0, 0], [I * 0.4 + 0.3, 0, 0], [I * 0.4, 0.2, 0.1 * (I % 3)]], Family: 1 + (I % 2) });   // 20 small triangles: several clusters in one set
const FacetSets = [{ Facets: Rect(0.8, 0.5, 1) }, { Facets: Rect(0.3, 0.3, 2) }, { Facets: Many }];
FacetSets.forEach((Set) => { Set.TriangleSlots = Uint32Array.from(Set.Facets.map((_, Index) => 1000 + Index)); });   // pretend BVH slots (a real run proves the real ones in EmissiveLights.test)
const Materials = [{ Albedo: [0.5, 0.5, 0.5], Emission: [0, 0, 0] }, { Albedo: [0, 0, 0], Emission: [4.0, 3.0, 2.0] }, { Albedo: [0, 0, 0], Emission: [0.5, 6.0, 1.0] }];
let Seed = 4242;
const Random = () => { Seed = (Math.imul(Seed, 1664525) + 1013904223) >>> 0; return Seed / 4294967296; };
const B = LightClusterBounds;
const Rotated = (X, Y, Z, Angle, Scale) => { const C = Math.cos(Angle) * Scale, S = Math.sin(Angle) * Scale; return Float32Array.from([C, S, 0, 0, -S, C, 0, 0, 0, 0, Scale, 0, X, Y, Z, 1]); };
const Occupants = [];
for (let I = 0; I < 24; ++I)
    Occupants.push({ FacetSet: I % 3, Transform: Rotated(B.Minimum[0] + 20 + Random() * 150, B.Minimum[1] + 10 + Random() * 100, 1 + Random() * 8, Random() * 6.28, 0.8 + Random() * 1.5),
                     Roles: 1, Inactive: false, MaterialOverride: -1 });
const Scene = { FacetSets, Occupants };
const Space = new EmissiveLightSpace();
Space.Construct(Scene, Materials);
Space.Update(Scene, Materials, 1.0);

// GPU resources
const Lights = new LightClusterSpace(Exchange);
Lights.Update(Device, new Float32Array(32 * 16));
Lights.AssignEmissive(Device, Space, true);
const OccupantBytes = new ArrayBuffer(Occupants.length * 336);
Occupants.forEach((Occupant, Index) => { new Float32Array(OccupantBytes, Index * 336, 16).set(Occupant.Transform); new Uint32Array(OccupantBytes, Index * 336 + 67 * 4, 1)[0] = Occupant.Roles; });
const MaterialBytes = new Float32Array(Materials.length * 16);
Materials.forEach((Material, Index) => MaterialBytes.set([...Material.Albedo, 0.5, ...Material.Emission, 0], Index * 16));
const OccupantBuffer = Exchange.ConstructRecordsFrom(new Uint8Array(OccupantBytes), GPUBufferUsage.STORAGE, "Occupants");
const MaterialBuffer = Exchange.ConstructRecordsFrom(MaterialBytes, GPUBufferUsage.STORAGE, "Materials");
const FrameBytes = new ArrayBuffer(528);
new Uint32Array(FrameBytes)[103] = 8192;            // FeatureRayTracing
new Float32Array(FrameBytes)[111] = 1.0;            // EmissionScale
new Uint32Array(FrameBytes)[129] = 16;              // ExtendedEmissiveLights
const Frame = Exchange.ConstructRecords(528, GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, "Frame");
Device.queue.writeBuffer(Frame, 0, FrameBytes);

// queries: shading points over the pad (some outside the grid), draws, and membership probes taken from the table (listed and not)
const Queries = [];
const Probes = [];
for (let K = 0; K < 3000; ++K)
{
    const Outside = K % 40 === 0;
    const Position = Outside ? [B.Minimum[0] - 5 - Random() * 20, B.Minimum[1] + Random() * B.Extent[1], Random() * 5]
                             : [B.Minimum[0] + Random() * B.Extent[0], B.Minimum[1] + Random() * B.Extent[1], B.Minimum[2] + Random() * B.Extent[2]];
    const Draw = [Random(), Random(), Random(), Random()];
    const Cluster = Math.floor(Random() * Space.Count), Occupant = Space.ClusterWords[Cluster * 8 + 4], First = Space.ClusterWords[Cluster * 8 + 5];
    const Triangle = Random() < 0.5 ? Space.TriangleWords[(First + Math.floor(Random() * Space.ClusterWords[Cluster * 8 + 6])) * 12 + 3] : 5000 + K;   // half of them are not emitters
    Probes.push({ Position, Draw, Occupant, Triangle });
    const Member = new Float32Array(4); Member[0] = Occupant; new Uint32Array(Member.buffer)[1] = Triangle;
    Queries.push(...Position, 0, ...Draw, ...Member);
}
const QueryBuffer = Exchange.ConstructRecordsFrom(new Float32Array(Queries), GPUBufferUsage.STORAGE, "Queries");
const ResultBuffer = Exchange.ConstructRecords(Probes.length * 64, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC, "Results");
const Layout0 = Device.createBindGroupLayout({ entries: [
    { binding: 0, visibility: GPUShaderStage.COMPUTE, buffer: { type: "uniform" } },
    { binding: 1, visibility: GPUShaderStage.COMPUTE, buffer: { type: "read-only-storage" } },
    { binding: 2, visibility: GPUShaderStage.COMPUTE, buffer: { type: "storage" } }] });
const Layout1 = Device.createBindGroupLayout({ entries: [
    { binding: 0, visibility: GPUShaderStage.COMPUTE, buffer: { type: "read-only-storage" } },
    { binding: 1, visibility: GPUShaderStage.COMPUTE, buffer: { type: "read-only-storage" } }] });
const Empty = Device.createBindGroupLayout({ entries: [] });
const Layout3 = Device.createBindGroupLayout({ entries: [{ binding: 3, visibility: GPUShaderStage.COMPUTE, buffer: { type: "uniform" } }] });
const Program = await Shaders.ConstructComputeProgram("EmissiveParityProbe.wgsl", "main", [Layout0, Layout1, Empty, Layout3]);
const Encoder = Device.createCommandEncoder();
const Pass = Encoder.beginComputePass();
Pass.setPipeline(Program);
Pass.setBindGroup(0, Device.createBindGroup({ layout: Layout0, entries: [{ binding: 0, resource: { buffer: Frame } }, { binding: 1, resource: { buffer: QueryBuffer } }, { binding: 2, resource: { buffer: ResultBuffer } }] }));
Pass.setBindGroup(1, Device.createBindGroup({ layout: Layout1, entries: [{ binding: 0, resource: { buffer: OccupantBuffer } }, { binding: 1, resource: { buffer: MaterialBuffer } }] }));
Pass.setBindGroup(2, Device.createBindGroup({ layout: Empty, entries: [] }));
Pass.setBindGroup(3, Device.createBindGroup({ layout: Layout3, entries: [{ binding: 3, resource: { buffer: Lights.Records } }] }));
Pass.dispatchWorkgroups(Math.ceil(Probes.length / 64));
Pass.end();
const Read = Device.createBuffer({ size: Probes.length * 64, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
Encoder.copyBufferToBuffer(ResultBuffer, 0, Read, 0, Probes.length * 64);
Device.queue.submit([Encoder.finish()]);
await Read.mapAsync(GPUMapMode.READ);
const Gpu = new Float32Array(Read.getMappedRange().slice(0));

let Valid = 0, SameChoice = 0, WorstPosition = 0, WorstPdf = 0, WorstRadiance = 0, UsableMismatch = 0, ListedMismatch = 0, ListedTrue = 0, ListedChecked = 0, Compared = 0;
Probes.forEach((Probe, Index) =>
{
    const Words = CellWords(Space, B, Probe.Position);
    const Expected = Words ? SampleEmissiveLight(Space, Occupants, Materials, 1.0, Probe.Position, Words, Probe.Draw) : { Valid: false };
    const O = Index * 16;
    if ((Gpu[O + 2] > 0.5) !== (Words !== null)) UsableMismatch++;
    const Listed = IsListedEmitter(Space, Words, Probe.Occupant, Probe.Triangle);
    ListedChecked++; if (Listed) ListedTrue++;
    if ((Gpu[O + 3] > 0.5) !== Listed) ListedMismatch++;
    if (Gpu[O] > 0.5) Valid++;
    if (!Expected.Valid && !(Gpu[O] > 0.5)) { Compared++; SameChoice++; return; }
    if (!Expected.Valid || !(Gpu[O] > 0.5)) { Compared++; return; }
    Compared++;
    const PositionError = Math.hypot(Gpu[O + 4] - Expected.Position[0], Gpu[O + 5] - Expected.Position[1], Gpu[O + 6] - Expected.Position[2]);
    if (PositionError < 2.0e-3 * Math.max(1, Math.hypot(...Expected.Position) * 0.01))
    {
        SameChoice++;
        WorstPosition = Math.max(WorstPosition, PositionError);
        WorstPdf = Math.max(WorstPdf, Math.abs(Gpu[O + 1] - Expected.Pdf) / Expected.Pdf);
        WorstRadiance = Math.max(WorstRadiance, Math.abs(Gpu[O + 12] - Expected.Radiance[0]), Math.abs(Gpu[O + 13] - Expected.Radiance[1]), Math.abs(Gpu[O + 14] - Expected.Radiance[2]));
    }
});
console.log("RESULT " + JSON.stringify({ Count: Compared, ValidFraction: Valid / Compared, SameChoice, WorstPosition, WorstPdfRelative: WorstPdf, WorstRadiance, UsableMismatch, ListedMismatch, ListedTrue, ListedChecked,
                                         Clusters: Space.Count, Truncated: Space.Statistics.TruncatedCells }));
process.exit(0);
