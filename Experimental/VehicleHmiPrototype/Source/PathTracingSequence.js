//============================================================================================================================================
//                                                         PATHTRACINGSEQUENCE.JS
//============================================================================================================================================
// 🧩 The ray-traced lighting path: ReSTIR DI (direct light from every light kind) and ReSTIR PT (the indirect path
//    tracer with the hybrid shift) over the triangle BVH, resolved into the three demodulated signals the variance
//    integrators reconstruct.

// 📝 Order per frame: ① ResampleDirectTemporal (primary surface → guide/position, light candidates, temporal reuse);
//    ② ResampleDirectSpatial (spatial reuse, final shadow ray → DirectDiffuse irradiance, DirectSpecular radiance);
//    ③ ResamplePathTemporal (one path per pixel, temporal reuse by shift); ④ ResamplePathSpatial (spatial reuse by
//    shift, resolve → Indirect irradiance, Specular radiance = path specular + direct specular).
//    Reservoirs and guides alternate between two slots by frame parity; each pass's intermediate reservoir is single.
//    Group 1 of every program is the scene trace group (TraceRecords.wgsl) the TraceStructure maintains.

const DirectStride = 32;   // [B] DirectRecord
const PathStride   = 96;   // [B] PathRecord

export class PathTracingSequence
{
    constructor(Exchange, Shaders, Trace)
    {
        this.Exchange = Exchange;
        this.Shaders  = Shaders;
        this.Trace    = Trace;
    }

    async ConstructPrograms(FrameRecords, MaterialRecords, OccupantRecords, Atmosphere)
    {
        this.FrameRecords = FrameRecords;
        const Compute = GPUShaderStage.COMPUTE;
        this.SceneLayout = this.Exchange.ConstructDescriptorLayout(
            ["Uniform", "Uniform", "StorageRead", "StorageRead", "StorageRead", "StorageRead", "StorageRead", "Texture2D", "Sampler"], Compute, "Scene trace");
        this.SceneGroup = this.Exchange.ConstructDescriptorGroup(this.SceneLayout,
            [this.Trace.TraceRecords, MaterialRecords, this.Trace.NodeRecords, this.Trace.TriangleRecords, this.Trace.InstanceRecords,
             this.Trace.LightTable, OccupantRecords, Atmosphere.SkyView, Atmosphere.SkySampler]);
        this.DirectTemporalLayout = this.Exchange.ConstructDescriptorLayout(
            ["Uniform", "Texture2D", "TextureDepth2D", "Texture2D", "Texture2D", "Texture2DUnfiltered", "StorageTexture2D:rgba16float",
             "StorageTexture2D:rgba32float", "StorageRead", "Storage"], Compute, "Direct reservoir temporal");
        this.DirectSpatialLayout = this.Exchange.ConstructDescriptorLayout(
            ["Uniform", "Texture2D", "Texture2DUnfiltered", "StorageRead", "Storage", "StorageTexture2D:rgba16float", "StorageTexture2D:rgba16float"],
            Compute, "Direct reservoir spatial");
        this.PathTemporalLayout = this.Exchange.ConstructDescriptorLayout(
            ["Uniform", "Texture2D", "Texture2DUnfiltered", "Texture2D", "Texture2D", "Texture2DUnfiltered", "StorageRead", "Storage"], Compute, "Path temporal");
        this.PathSpatialLayout = this.Exchange.ConstructDescriptorLayout(
            ["Uniform", "Texture2D", "Texture2DUnfiltered", "StorageRead", "Storage", "Texture2D", "StorageTexture2D:rgba16float", "StorageTexture2D:rgba16float"],
            Compute, "Path spatial");
        this.DirectTemporalProgram = await this.Shaders.ConstructComputeProgram("DirectReservoirTemporal.wgsl", "ResampleDirectTemporal",
                                                                                [this.DirectTemporalLayout, this.SceneLayout]);
        this.DirectSpatialProgram  = await this.Shaders.ConstructComputeProgram("DirectReservoirSpatial.wgsl", "ResampleDirectSpatial",
                                                                                [this.DirectSpatialLayout, this.SceneLayout]);
        this.PathTemporalProgram   = await this.Shaders.ConstructComputeProgram("PathTraceTemporal.wgsl", "ResamplePathTemporal",
                                                                                [this.PathTemporalLayout, this.SceneLayout]);
        this.PathSpatialProgram    = await this.Shaders.ConstructComputeProgram("PathTraceSpatial.wgsl", "ResamplePathSpatial",
                                                                                [this.PathSpatialLayout, this.SceneLayout]);
    }

    ConstructTargets(TraceExtent, Surface)
    {
        for (const Resource of this.Resources || [])
            Resource.destroy();
        const [Width, Height] = TraceExtent;
        const Usage = GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING;
        const Construct = (Label, Format = "rgba16float") => this.Exchange.ConstructTexture({ label: Label, size: TraceExtent, format: Format, usage: Usage });
        const Guides = [Construct("Path guide A"), Construct("Path guide B")];
        const Positions = [Construct("Path positions A", "rgba32float"), Construct("Path positions B", "rgba32float")];
        const DirectDiffuse = Construct("Direct diffuse irradiance");
        const DirectSpecular = Construct("Direct specular radiance");
        const Indirect = Construct("Indirect irradiance");
        const Specular = Construct("Specular radiance");
        const BufferUsage = GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST;
        const Pixels = Width * Height;
        const DirectTemporal = this.Exchange.ConstructRecords(Pixels * DirectStride, BufferUsage, "Direct reservoirs temporal");
        const DirectFinal = [0, 1].map((Slot) => this.Exchange.ConstructRecords(Pixels * DirectStride, BufferUsage, `Direct reservoirs ${Slot}`));
        const PathTemporal = this.Exchange.ConstructRecords(Pixels * PathStride, BufferUsage, "Path reservoirs temporal");
        const PathFinal = [0, 1].map((Slot) => this.Exchange.ConstructRecords(Pixels * PathStride, BufferUsage, `Path reservoirs ${Slot}`));
        this.Resources = [...Guides, ...Positions, DirectDiffuse, DirectSpecular, Indirect, Specular, DirectTemporal, ...DirectFinal, PathTemporal, ...PathFinal];
        this.TraceExtent = TraceExtent;
        const View = (Texture) => Texture.createView();
        this.Views = { Guides: Guides.map(View), Positions: Positions.map(View), DirectDiffuse: View(DirectDiffuse), DirectSpecular: View(DirectSpecular),
                       Indirect: View(Indirect), Specular: View(Specular) };
        this.DirectTemporalGroups = [0, 1].map((Parity) => this.Exchange.ConstructDescriptorGroup(this.DirectTemporalLayout,
            [this.FrameRecords, Surface.Views.Attributes, Surface.Views.Depth, Surface.Views.Motion, this.Views.Guides[1 - Parity],
             this.Views.Positions[1 - Parity], this.Views.Guides[Parity], this.Views.Positions[Parity], DirectFinal[1 - Parity], DirectTemporal]));
        this.DirectSpatialGroups = [0, 1].map((Parity) => this.Exchange.ConstructDescriptorGroup(this.DirectSpatialLayout,
            [this.FrameRecords, this.Views.Guides[Parity], this.Views.Positions[Parity], DirectTemporal, DirectFinal[Parity],
             this.Views.DirectDiffuse, this.Views.DirectSpecular]));
        this.PathTemporalGroups = [0, 1].map((Parity) => this.Exchange.ConstructDescriptorGroup(this.PathTemporalLayout,
            [this.FrameRecords, this.Views.Guides[Parity], this.Views.Positions[Parity], Surface.Views.Motion, this.Views.Guides[1 - Parity],
             this.Views.Positions[1 - Parity], PathFinal[1 - Parity], PathTemporal]));
        this.PathSpatialGroups = [0, 1].map((Parity) => this.Exchange.ConstructDescriptorGroup(this.PathSpatialLayout,
            [this.FrameRecords, this.Views.Guides[Parity], this.Views.Positions[Parity], PathTemporal, PathFinal[Parity],
             this.Views.DirectSpecular, this.Views.Indirect, this.Views.Specular]));
    }

    RecordDirect(Encoder, Parity, TimestampWrites)
    {
        const [Width, Height] = this.TraceExtent;
        const Pass = Encoder.beginComputePass({ label: "ReSTIR DI", timestampWrites: TimestampWrites });
        Pass.setBindGroup(1, this.SceneGroup);
        Pass.setPipeline(this.DirectTemporalProgram);
        Pass.setBindGroup(0, this.DirectTemporalGroups[Parity]);
        Pass.dispatchWorkgroups(Math.ceil(Width / 8), Math.ceil(Height / 8));
        Pass.setPipeline(this.DirectSpatialProgram);
        Pass.setBindGroup(0, this.DirectSpatialGroups[Parity]);
        Pass.dispatchWorkgroups(Math.ceil(Width / 8), Math.ceil(Height / 8));
        Pass.end();
    }

    RecordPaths(Encoder, Parity, TimestampWrites)
    {
        const [Width, Height] = this.TraceExtent;
        const Pass = Encoder.beginComputePass({ label: "ReSTIR PT", timestampWrites: TimestampWrites });
        Pass.setBindGroup(1, this.SceneGroup);
        Pass.setPipeline(this.PathTemporalProgram);
        Pass.setBindGroup(0, this.PathTemporalGroups[Parity]);
        Pass.dispatchWorkgroups(Math.ceil(Width / 8), Math.ceil(Height / 8));
        Pass.setPipeline(this.PathSpatialProgram);
        Pass.setBindGroup(0, this.PathSpatialGroups[Parity]);
        Pass.dispatchWorkgroups(Math.ceil(Width / 8), Math.ceil(Height / 8));
        Pass.end();
    }
}
