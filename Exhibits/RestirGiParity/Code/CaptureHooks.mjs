//============================================================================================================================================
//                                                           CAPTUREHOOKS.MJS
//============================================================================================================================================
// 🧩 HOOKS module for the demo's Tools/HeadlessHarness.mjs: records the reference run the C++ port is compared against.
//
// 📝 Recorded, all under CAPTURE_OUT:
//    ① WriteLog.bin   — every queue write and every mapped-at-creation upload, in order, tagged with the refresh index
//                        (-1 during bring-up) and the destination's label: the exact host → device input stream;
//    ② Commands.txt   — every compute/raster unit: unit label, program label, dispatch extents and draw arguments;
//    ③ Scene.facetspace — the scene topology (facet sets, occupants) as the host held it before the first refresh;
//    ④ <Refresh>_<Label>.raw + Targets.json — raw texel contents of the named targets after the listed refreshes;
//    ⑤ a deterministic clock: performance.now() advances by exactly one 60 Hz step per refresh, like the harness.
//
//    CAPTURE_OUT=dir CAPTURE_FRAMES=0,1,3 CAPTURE_TEXTURES="Label A,Label B" HOOKS=CaptureHooks.mjs node HeadlessHarness.mjs ..

import fs from "node:fs";
import path from "node:path";

const OutFolder = process.env.CAPTURE_OUT || "capture";
fs.mkdirSync(OutFolder, { recursive: true });
const DumpFrames = new Set((process.env.CAPTURE_FRAMES || "").split(",").filter((Text) => Text.length > 0).map(Number));
const DumpLabels = (process.env.CAPTURE_TEXTURES || "").split(",").map((Text) => Text.trim()).filter((Text) => Text.length > 0);
const DumpBringUp = (process.env.CAPTURE_BRINGUP || "").split(",").map((Text) => Text.trim()).filter((Text) => Text.length > 0);

let CurrentFrame = -1;
const Textures = new Map();
const Buffers = new Map();
const Pending = [];
const TargetIndex = [];

//------------------------------------------------------------------------------------------------------------------------
//                                                   DETERMINISTIC CLOCK
//------------------------------------------------------------------------------------------------------------------------

const RealNow = performance.now.bind(performance);
let FakeNow = null;
performance.now = () => (FakeNow === null ? RealNow() : FakeNow);
const ClockOrigin = RealNow();

//------------------------------------------------------------------------------------------------------------------------
//                                                   WRITE LOG
//------------------------------------------------------------------------------------------------------------------------

const WriteLog = fs.openSync(path.join(OutFolder, "WriteLog.bin"), "w");
const Commands = fs.openSync(path.join(OutFolder, "Commands.txt"), "w");

function ToBytes(Data, DataOffset = 0, Size = undefined)
{
    if (Data instanceof ArrayBuffer)
        return new Uint8Array(Data, DataOffset, Size === undefined ? Data.byteLength - DataOffset : Size);
    if (ArrayBuffer.isView(Data))
    {
        const Element = Data.BYTES_PER_ELEMENT || 1;
        const Start = Data.byteOffset + DataOffset * Element;
        const Length = Size === undefined ? Data.byteLength - DataOffset * Element : Size * Element;
        return new Uint8Array(Data.buffer, Start, Length);
    }
    throw new Error("unsupported write source");
}

// 📝 Record: i32 refresh, u8 kind (0 region write, 1 region create, 2 texture write), u16 label length, label,
//    u64 offset (region) or 4×u32 origin+layer (texture), u32 byte count, bytes.
function LogWrite(Kind, Label, Offset, Bytes, Extra = null)
{
    const LabelBytes = Buffer.from(Label || "", "utf8");
    const Header = Buffer.alloc(4 + 1 + 2 + LabelBytes.length + 8 + 16 + 4);
    let Cursor = 0;
    Header.writeInt32LE(CurrentFrame, Cursor); Cursor += 4;
    Header.writeUInt8(Kind, Cursor); Cursor += 1;
    Header.writeUInt16LE(LabelBytes.length, Cursor); Cursor += 2;
    LabelBytes.copy(Header, Cursor); Cursor += LabelBytes.length;
    Header.writeBigUInt64LE(BigInt(Offset), Cursor); Cursor += 8;
    const Words = Extra || [0, 0, 0, 0];
    for (let Index = 0; Index < 4; ++Index) { Header.writeUInt32LE(Words[Index] >>> 0, Cursor); Cursor += 4; }
    Header.writeUInt32LE(Bytes.byteLength, Cursor); Cursor += 4;
    fs.writeSync(WriteLog, Header);
    fs.writeSync(WriteLog, Buffer.from(Bytes.buffer, Bytes.byteOffset, Bytes.byteLength));
}

function LogCommand(Text)
{
    fs.writeSync(Commands, `F${CurrentFrame} ${Text}\n`);
}

const Queue = globalThis.GPUQueue.prototype;
const OriginalWriteBuffer = Queue.writeBuffer;
Queue.writeBuffer = function (Target, Offset, Data, DataOffset, Size)
{
    LogWrite(0, Target.label, Offset, ToBytes(Data, DataOffset || 0, Size));
    return OriginalWriteBuffer.call(this, Target, Offset, Data, DataOffset, Size);
};
const OriginalWriteTexture = Queue.writeTexture;
Queue.writeTexture = function (Destination, Data, Layout, Size)
{
    const Extent = Array.isArray(Size) ? Size : [Size.width, Size.height || 1, Size.depthOrArrayLayers || 1];
    const Origin = Destination.origin ? (Array.isArray(Destination.origin) ? Destination.origin
                                                                         : [Destination.origin.x || 0, Destination.origin.y || 0, Destination.origin.z || 0]) : [0, 0, 0];
    const Bytes = ToBytes(Data, 0, undefined).subarray(Layout.offset || 0);
    LogWrite(2, Destination.texture.label, Layout.bytesPerRow || 0, Bytes, [Origin[0], Origin[1], Origin[2], (Extent[0] | (Extent[1] << 16)) >>> 0]);
    return OriginalWriteTexture.call(this, Destination, Data, Layout, Size);
};

const DeviceProto = globalThis.GPUDevice.prototype;
const OriginalCreateBuffer = DeviceProto.createBuffer;
DeviceProto.createBuffer = function (Descriptor)
{
    // 📝 Readback needs COPY_SRC; it is added to every buffer that is not a mapping buffer (no effect on rendering).
    if (!(Descriptor.usage & (GPUBufferUsage.MAP_READ | GPUBufferUsage.MAP_WRITE)))
        Descriptor = { ...Descriptor, usage: Descriptor.usage | GPUBufferUsage.COPY_SRC };
    const Created = OriginalCreateBuffer.call(this, Descriptor);
    Buffers.set(Descriptor.label, Created);
    if (Descriptor.mappedAtCreation)
    {
        const Ranges = [];
        const Map = Created.getMappedRange.bind(Created);
        const Unmap = Created.unmap.bind(Created);
        Created.getMappedRange = (...Arguments) => { const Range = Map(...Arguments); Ranges.push([Arguments[0] || 0, Range]); return Range; };
        Created.unmap = () =>
        {
            for (const [Offset, Range] of Ranges)
                LogWrite(1, Descriptor.label, Offset, new Uint8Array(Range).slice());
            Unmap();
        };
    }
    return Created;
};
const OriginalCreateTexture = DeviceProto.createTexture;
DeviceProto.createTexture = function (Descriptor)
{
    Descriptor = { ...Descriptor, usage: Descriptor.usage | GPUTextureUsage.COPY_SRC };
    const Created = OriginalCreateTexture.call(this, Descriptor);
    Textures.set(Descriptor.label, { Texture: Created, Descriptor });
    return Created;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   COMMAND LOG
//------------------------------------------------------------------------------------------------------------------------

const EncoderProto = globalThis.GPUCommandEncoder.prototype;
const OriginalBeginCompute = EncoderProto.beginComputePass;
EncoderProto.beginComputePass = function (Descriptor = {})
{
    LogCommand(`compute "${Descriptor.label || ""}"`);
    return OriginalBeginCompute.call(this, Descriptor);
};
const OriginalBeginRender = EncoderProto.beginRenderPass;
EncoderProto.beginRenderPass = function (Descriptor = {})
{
    const Colors = (Descriptor.colorAttachments || []).map((Attachment) => Attachment ? Attachment.loadOp : "-").join(",");
    LogCommand(`raster "${Descriptor.label || ""}" colors=${Colors} depth=${Descriptor.depthStencilAttachment ? Descriptor.depthStencilAttachment.depthLoadOp : "-"}`);
    return OriginalBeginRender.call(this, Descriptor);
};
const OriginalCopyBuffer = EncoderProto.copyBufferToBuffer;
EncoderProto.copyBufferToBuffer = function (Source, SourceOffset, Target, TargetOffset, Size)
{
    LogCommand(`copy "${Source.label}" ${SourceOffset} -> "${Target.label}" ${TargetOffset} ${Size}`);
    return OriginalCopyBuffer.call(this, Source, SourceOffset, Target, TargetOffset, Size);
};
const ComputeProto = globalThis.GPUComputePassEncoder.prototype;
const OriginalComputePipeline = ComputeProto.setPipeline;
ComputeProto.setPipeline = function (Pipeline) { LogCommand(`  program "${Pipeline.label}"`); return OriginalComputePipeline.call(this, Pipeline); };
const OriginalDispatch = ComputeProto.dispatchWorkgroups;
ComputeProto.dispatchWorkgroups = function (X, Y = 1, Z = 1) { LogCommand(`  dispatch ${X} ${Y} ${Z}`); return OriginalDispatch.call(this, X, Y, Z); };
const RenderProto = globalThis.GPURenderPassEncoder.prototype;
const OriginalRenderPipeline = RenderProto.setPipeline;
RenderProto.setPipeline = function (Pipeline) { LogCommand(`  program "${Pipeline.label}"`); return OriginalRenderPipeline.call(this, Pipeline); };
const OriginalDraw = RenderProto.draw;
RenderProto.draw = function (VertexCount, InstanceCount = 1, FirstVertex = 0, FirstInstance = 0)
{
    LogCommand(`  draw ${VertexCount} ${InstanceCount} ${FirstVertex} ${FirstInstance}`);
    return OriginalDraw.call(this, VertexCount, InstanceCount, FirstVertex, FirstInstance);
};
const OriginalIndirect = RenderProto.drawIndexedIndirect;
RenderProto.drawIndexedIndirect = function (Region, Offset)
{
    LogCommand(`  draw-indexed-indirect "${Region.label}" ${Offset}`);
    return OriginalIndirect.call(this, Region, Offset);
};
const OriginalVertex = RenderProto.setVertexBuffer;
RenderProto.setVertexBuffer = function (Slot, Region, Offset = 0, Size)
{
    LogCommand(`  vertex ${Slot} "${Region ? Region.label : ""}" ${Offset}`);
    return OriginalVertex.call(this, Slot, Region, Offset, Size);
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   TARGET READBACK
//------------------------------------------------------------------------------------------------------------------------

const TexelBytes = { "rgba16float": 8, "rg16float": 4, "r16float": 2, "r32float": 4, "rgba32float": 16, "rgba8unorm": 4,
                     "rgba8unorm-srgb": 4, "r8unorm": 1, "depth32float": 4, "r32uint": 4 };

function QueueReadback(Device, Label, Tag)
{
    const Entry = Textures.get(Label);
    if (!Entry)
    {
        console.log(`[capture] no texture labelled '${Label}'`);
        return;
    }
    const Descriptor = Entry.Descriptor;
    const Size = Array.isArray(Descriptor.size) ? Descriptor.size : [Descriptor.size.width, Descriptor.size.height || 1, Descriptor.size.depthOrArrayLayers || 1];
    const [Width, Height] = Size;
    const Layers = Size[2] || 1;
    const Texel = TexelBytes[Descriptor.format];
    const Row = Width * Texel;
    const Stride = Math.ceil(Row / 256) * 256;
    const Region = OriginalCreateBuffer.call(Device, { label: "capture readback", size: Stride * Height * Layers, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
    const Encoder = Device.createCommandEncoder({ label: "capture" });
    Encoder.copyTextureToBuffer({ texture: Entry.Texture, aspect: Descriptor.format === "depth32float" ? "depth-only" : "all" },
                                { buffer: Region, bytesPerRow: Stride, rowsPerImage: Height }, [Width, Height, Layers]);
    Device.queue.submit([Encoder.finish()]);
    const Name = `${Tag}_${Label.replace(/[^A-Za-z0-9]+/g, "_")}.raw`;
    Pending.push(Region.mapAsync(GPUMapMode.READ).then(() =>
    {
        const Mapped = new Uint8Array(Region.getMappedRange());
        const Packed = Buffer.alloc(Row * Height * Layers);
        for (let Layer = 0; Layer < Layers; ++Layer)
            for (let Line = 0; Line < Height; ++Line)
                Packed.set(Mapped.subarray((Layer * Height + Line) * Stride, (Layer * Height + Line) * Stride + Row), (Layer * Height + Line) * Row);
        fs.writeFileSync(path.join(OutFolder, Name), Packed);
        Region.unmap();
        Region.destroy();
        TargetIndex.push({ File: Name, Label, Tag, Format: Descriptor.format, Width, Height, Layers, Dimension: Descriptor.dimension || "2d" });
    }));
}

function QueueRegionReadback(Device, Label, Tag)
{
    const Source = Buffers.get(Label);
    if (!Source)
        return;
    const Size = Source.size;
    const Region = OriginalCreateBuffer.call(Device, { label: "capture readback", size: Size, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
    const Encoder = Device.createCommandEncoder({ label: "capture" });
    OriginalCopyBuffer.call(Encoder, Source, 0, Region, 0, Size);
    Device.queue.submit([Encoder.finish()]);
    const Name = `${Tag}_${Label.replace(/[^A-Za-z0-9]+/g, "_")}.bin`;
    Pending.push(Region.mapAsync(GPUMapMode.READ).then(() =>
    {
        fs.writeFileSync(path.join(OutFolder, Name), Buffer.from(Region.getMappedRange().slice(0)));
        Region.unmap();
        Region.destroy();
        TargetIndex.push({ File: Name, Label, Tag, Format: "region", Width: Size, Height: 1, Layers: 1 });
    }));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   SCENE TOPOLOGY
//------------------------------------------------------------------------------------------------------------------------

// 📝 Little-endian container 'FSP1'. Every scalar the host computes with is a JS number, so positions, transforms and
//    bounds are written as float64: the C++ port starts from exactly the values the demo started from.
function WriteFacetSpace(Scene, File)
{
    const Parts = [];
    const U32 = (Value) => { const B = Buffer.alloc(4); B.writeUInt32LE(Value >>> 0); Parts.push(B); };
    const I32 = (Value) => { const B = Buffer.alloc(4); B.writeInt32LE(Value | 0); Parts.push(B); };
    const F64 = (Value) => { const B = Buffer.alloc(8); B.writeDoubleLE(Number(Value)); Parts.push(B); };
    const Text = (Value) => { const B = Buffer.from(Value || "", "utf8"); U32(B.length); Parts.push(B); const Pad = (4 - (B.length % 4)) % 4; if (Pad) Parts.push(Buffer.alloc(Pad)); };
    Parts.push(Buffer.from("FSP1"));
    U32(1);
    U32(Scene.FacetSets.length);
    U32(Scene.Occupants.length);
    I32(Scene.VehicleOccupants.Body);
    for (const Wheel of Scene.VehicleOccupants.Wheels) I32(Wheel);
    I32(Scene.GroundOccupant);
    I32(Scene.GroundTiles.First);
    for (const Strip of Scene.GroundTiles.Strips) I32(Strip);
    U32(Scene.DistanceCount);
    U32(Scene.BaseDistanceCount);
    U32(Scene.DynamicCount);
    U32(Scene.VehicleBodySet);
    U32(Scene.StressPool.First);
    U32(Scene.StressPool.Capacity);
    U32(Scene.ConeBlock.First);
    U32(Scene.ConeBlock.Count);
    for (const Set of Scene.FacetSets)
    {
        Text(Set.Name);
        U32(Set.Facets.length);
        F64(Set.DistanceVoxel);
        F64(Set.CardDensity);
        for (let Axis = 0; Axis < 3; ++Axis) F64(Set.LocalMinimum[Axis]);
        for (let Axis = 0; Axis < 3; ++Axis) F64(Set.LocalMaximum[Axis]);
        U32(Set.VertexCapacity || 0);
        const Authored = Set.Facets.some((Facet) => Facet.Normals);
        U32((Set.Composite ? 1 : 0) | (Authored ? 2 : 0));
        for (const Facet of Set.Facets)
        {
            for (const Corner of Facet.Corners) for (let Axis = 0; Axis < 3; ++Axis) F64(Corner[Axis]);
            U32(Facet.Family);
            U32(Facet.OccupantOffset || 0);
            if (Authored)
            {
                U32(Facet.Normals ? 1 : 0);
                for (let Corner = 0; Corner < 3; ++Corner) for (let Axis = 0; Axis < 3; ++Axis) F64(Facet.Normals ? Facet.Normals[Corner][Axis] : 0.0);
            }
        }
    }
    for (const Occupant of Scene.Occupants)
    {
        Text(Occupant.Name);
        U32(Occupant.FacetSet);
        U32(Occupant.Roles);
        I32(Occupant.MaterialOverride);
        for (let Element = 0; Element < 16; ++Element) F64(Occupant.Transform[Element]);
        for (let Element = 0; Element < 16; ++Element) F64(Occupant.PreviousTransform[Element]);
        for (let Axis = 0; Axis < 3; ++Axis) F64(Occupant.CardMinimum[Axis]);
        for (let Axis = 0; Axis < 3; ++Axis) F64(Occupant.CardMaximum[Axis]);
        F64(Occupant.CardDensity);
        const Flags = (Occupant.Inactive ? 1 : 0) | (Occupant.Fragment ? 2 : 0) | (Occupant.Moving ? 4 : 0) | (Occupant.Obstacle ? 8 : 0)
                    | (Occupant.Breakable ? 16 : 0) | (Occupant.LocalMinimum ? 32 : 0) | (Occupant.FallbackAlbedo !== undefined ? 64 : 0)
                    | (Occupant.Cone !== undefined ? 128 : 0) | (Occupant.StressSlot !== undefined ? 256 : 0)
                    | (Occupant.Moving !== undefined ? 512 : 0);
        U32(Flags);
        for (let Axis = 0; Axis < 3; ++Axis) F64(Occupant.LocalMinimum ? Occupant.LocalMinimum[Axis] : 0.0);
        for (let Axis = 0; Axis < 3; ++Axis) F64(Occupant.LocalMaximum ? Occupant.LocalMaximum[Axis] : 0.0);
        U32(Occupant.FallbackAlbedo || 0);
        I32(Occupant.Cone !== undefined ? Occupant.Cone : -1);
        I32(Occupant.StressSlot !== undefined ? Occupant.StressSlot : -1);
    }
    fs.writeFileSync(File, Buffer.concat(Parts));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   HOOKS
//------------------------------------------------------------------------------------------------------------------------

export default {
    BeforeFrame(Host, Frame)
    {
        const Device = Host.Exchange.Device;
        if (Frame === 0)
        {
            // 📝 Effects outside the illumination port (clouds, cloud shadows, lens flare, smoke, sparks, tyre marks) are
            //    switched off for the reference, so the comparison isolates the ported chain.
            Object.assign(Host.Settings, JSON.parse(process.env.CAPTURE_SETTINGS || "{}"));
            // 📝 The camera and light each refresh is encoded from, so the port's host encoders can be checked exactly.
            const Encode = Host.EncodeFrameRecord.bind(Host);
            Host.EncodeFrameRecord = (Camera, Light) =>
            {
                fs.appendFileSync(path.join(OutFolder, "Refreshes.jsonl"), JSON.stringify({ Refresh: Host.FrameIndex, Camera, Light,
                    Focus: Host.Focus, TraceExtent: Host.TraceExtent, ScreenExtent: Host.ScreenExtent, PriorValid: Host.PriorValid,
                    VehicleBody: Host.Vehicle.QueryBodyTransform ? Array.from(Host.Vehicle.QueryBodyTransform()) : null }) + "\n");
                return Encode(Camera, Light);
            };
            WriteFacetSpace(Host.Scene, path.join(OutFolder, "Scene.facetspace"));
            fs.writeFileSync(path.join(OutFolder, "Settings.json"), JSON.stringify({ Settings: Host.Settings, Grade: Host.Grade,
                ScreenExtent: Host.ScreenExtent, TraceExtent: Host.TraceExtent, TraceScale: Host.TraceScaleUsed,
                Constants: Host.Constants, Adapter: Host.AdapterDescription }, null, 1));
            for (const Label of DumpBringUp)
                QueueReadback(Device, Label, "BringUp");
        }
        // 📝 Targets of the refresh that just finished (Frame − 1): its commands were submitted before this hook ran.
        if (DumpFrames.has(Frame - 1))
        {
            for (const Label of DumpLabels)
            {
                if (Label.startsWith("region:"))
                    QueueRegionReadback(Device, Label.slice(7), `F${Frame - 1}`);
                else
                    QueueReadback(Device, Label, `F${Frame - 1}`);
            }
        }
        CurrentFrame = Frame;
        FakeNow = ClockOrigin + (Frame + 1) * 16.6667;
    },
    async Report(Host)
    {
        const Device = Host.Exchange.Device;
        const Final = Host.FrameIndex - 1;
        if (DumpFrames.has(Final))
            for (const Label of DumpLabels)
            {
                if (Label.startsWith("region:"))
                    QueueRegionReadback(Device, Label.slice(7), `F${Final}`);
                else
                    QueueReadback(Device, Label, `F${Final}`);
            }
        await Promise.all(Pending);
        fs.writeFileSync(path.join(OutFolder, "Targets.json"), JSON.stringify(TargetIndex, null, 1));
        fs.closeSync(WriteLog);
        fs.closeSync(Commands);
        console.log(`[capture] ${TargetIndex.length} targets, frame index ${Host.FrameIndex}`);
    }
};
