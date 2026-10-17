import { RayTracingScene } from "./RayTracingScene.js";
import { MaxSceneLights } from "./LightingSystem.js";
import { PathPixelBytes, PathConfigurationBytes, RendererModes, DescribeRayTracing } from "./PathTracingSettings.js";

export const PathTracerSources = ["RoadMarks.wgsl", "PathRecords.wgsl", "PathScene.wgsl", "PathRefit.wgsl",
    "PathTransport.wgsl", "PathResampling.wgsl", "PathDenoise.wgsl", "PathWavelet.wgsl", "PathResolve.wgsl"];

export class ReSTIRPathTracer
{
    constructor(Host)
    {
        this.Host = Host; this.Exchange = Host.Exchange; this.Shaders = Host.Shaders;
    }

    async ConstructPrograms()
    {
        const H = this.Host, X = this.Exchange, Compute = GPUShaderStage.COMPUTE;
        this.Configuration = X.ConstructRecords(PathConfigurationBytes, GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, "Path tracer configuration");
        this.Acceleration = new RayTracingScene(X, this.Shaders, H.Scene, H.Materials, H.OccupantRecords, H.VehicleRecords);
        await this.Acceleration.ConstructPrograms();
        this.Acceleration.Update(0, true);
        this.SceneLayout = X.ConstructDescriptorLayout(["StorageRead", "StorageRead", "StorageRead", "StorageRead",
            "Texture2D", "Sampler", "Texture2D", "Uniform", "StorageRead", "StorageRead"], Compute, "Path tracer scene");
        this.TraceLayout = X.ConstructDescriptorLayout(["Uniform", "Uniform", "StorageRead", "Storage", "StorageTexture2D:rgba32float"],
            Compute, "Path reservoirs");
        this.GenerateProgram = await this.Shaders.ConstructComputeProgram("PathResampling.wgsl", "GeneratePathReservoirs", [this.TraceLayout, this.SceneLayout]);
        this.SpatialProgram = await this.Shaders.ConstructComputeProgram("PathResampling.wgsl", "SpatialPathReservoirs", [this.TraceLayout, this.SceneLayout]);
        this.TemporalLayout = X.ConstructDescriptorLayout(["Uniform", "Uniform", "StorageRead", "StorageRead", "Texture2DUnfiltered",
            "Texture2DUnfiltered", "Texture2DUnfiltered", "StorageTexture2D:rgba32float", "StorageTexture2D:rgba32float"], Compute, "Path SVGF temporal");
        this.WaveletLayout = X.ConstructDescriptorLayout(["Uniform", "Uniform", "StorageRead", "Texture2DUnfiltered", "Texture2DUnfiltered",
            "Uniform", "StorageTexture2D:rgba32float"], Compute, "Path SVGF wavelet");
        this.ResolveLayout = X.ConstructDescriptorLayout(["Uniform", "Uniform", "StorageRead", "Texture2DUnfiltered", "Texture2DUnfiltered",
            "Texture2D", "TextureDepth2D", "Texture2D", "StorageRead", "StorageTexture2D:rgba16float"], Compute, "Path screen reconstruction");
        this.TemporalProgram = await this.Shaders.ConstructComputeProgram("PathDenoise.wgsl", "IntegratePathHistory", [this.TemporalLayout]);
        this.WaveletProgram = await this.Shaders.ConstructComputeProgram("PathWavelet.wgsl", "FilterPathWavelet", [this.WaveletLayout]);
        this.ResolveProgram = await this.Shaders.ConstructComputeProgram("PathResolve.wgsl", "ResolvePathScreen", [this.ResolveLayout]);
        this.FilterSteps = [1, 2, 4, 8, 16].map((Step) => X.ConstructRecordsFrom(Uint32Array.of(Step, 0, 0, 0), GPUBufferUsage.UNIFORM, `Path wavelet step ${Step}`));
    }

    ConstructTargets(TraceExtent, ScreenExtent, Surface)
    {
        for (const Resource of this.Resources || []) Resource.destroy();
        this.TraceExtent = TraceExtent; this.ScreenExtent = ScreenExtent; this.Surface = Surface;
        const X = this.Exchange, Pixels = TraceExtent[0] * TraceExtent[1];
        const Usage = GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_SRC;
        const Texture = (Name, Extent = TraceExtent, Format = "rgba32float") => X.ConstructTexture({ label: Name, size: Extent, format: Format, usage: Usage });
        this.Histories = [0, 1].map((P) => X.ConstructRecords(Pixels * PathPixelBytes,
            GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC, `Spatial path history ${P}`));
        this.Scratch = X.ConstructRecords(Pixels * PathPixelBytes, GPUBufferUsage.STORAGE, "Initial/temporal path reservoirs");
        this.RawTexture = Texture("Unclamped raw path radiance");
        const Integrated = Texture("Integrated path radiance");
        const Moments = [Texture("Path moments A"), Texture("Path moments B")];
        const Retained = [Texture("Path SVGF feedback A"), Texture("Path SVGF feedback B")];
        const Wavelets = [Texture("Path wavelet A"), Texture("Path wavelet B")];
        const Screen = Texture("Ray-traced screen radiance", ScreenExtent, "rgba16float");
        this.Resources = [...this.Histories, this.Scratch, this.RawTexture, Integrated, ...Moments, ...Retained, ...Wavelets, Screen];
        this.Textures = { Integrated, Moments, Retained, Wavelets, Screen };
        this.Views = { Raw: this.RawTexture.createView(), Integrated: Integrated.createView(),
            Moments: Moments.map((T) => T.createView()), Retained: Retained.map((T) => T.createView()),
            Wavelets: Wavelets.map((T) => T.createView()), Screen: Screen.createView() };
        this.BuildSceneGroups();
        const H = this.Host, V = this.Views, Frame = H.FrameRecords, Config = this.Configuration;
        this.TemporalGroups = [0, 1].map((P) => X.ConstructDescriptorGroup(this.TemporalLayout,
            [Frame, Config, this.Histories[P], this.Histories[1 - P], V.Raw, V.Retained[1 - P], V.Moments[1 - P], V.Integrated, V.Moments[P]]));
        this.WaveletGroups = [0, 1].map((P) => {
            let Source = V.Integrated;
            return this.FilterSteps.map((Step, I) => {
                const Target = I === 0 ? V.Retained[P] : V.Wavelets[(I - 1) & 1];
                const Group = X.ConstructDescriptorGroup(this.WaveletLayout, [Frame, Config, this.Histories[P], Source, V.Moments[P], Step, Target]);
                Source = Target; return Group;
            });
        });
        this.ResolveGroups = [0, 1].map((P) => [V.Raw, V.Retained[P], ...V.Wavelets].map((Signal) =>
            X.ConstructDescriptorGroup(this.ResolveLayout, [Frame, Config, this.Histories[P], Signal, V.Raw,
                Surface.Views.Attributes, Surface.Views.Depth, Surface.Views.Direct, H.MaterialRecords, V.Screen])));
    }

    BuildSceneGroups()
    {
        const X = this.Exchange, H = this.Host;
        this.SceneGroup = X.ConstructDescriptorGroup(this.SceneLayout, [this.Acceleration.Geometry, H.OccupantRecords,
            H.MaterialRecords, H.LightRecords, H.Atmosphere.SkyView, H.Atmosphere.SkySampler, H.Marks.View, H.Effects.Records, H.VehicleRecords, this.Acceleration.PreviousTriangles]);
        this.GenerateGroups = [0, 1].map((P) => X.ConstructDescriptorGroup(this.TraceLayout,
            [H.FrameRecords, this.Configuration, this.Histories[1 - P], this.Scratch, this.Views.Raw]));
        this.SpatialGroups = [0, 1].map((P) => X.ConstructDescriptorGroup(this.TraceLayout,
            [H.FrameRecords, this.Configuration, this.Scratch, this.Histories[P], this.Views.Raw]));
        this.BoundGeneration = this.Acceleration.Generation;
    }

    UpdateScene(Frame)
    {
        const Changed = this.Acceleration.Update(Frame);
        if (this.Views && this.BoundGeneration !== this.Acceleration.Generation) this.BuildSceneGroups();
        return Changed;
    }

    UploadConfiguration()
    {
        const H = this.Host, S = H.Settings, D = this.Acceleration.Data;
        const Bytes = new ArrayBuffer(PathConfigurationBytes), U = new Uint32Array(Bytes), F = new Float32Array(Bytes);
        U.set([D.TriangleOffset, D.InstanceOffset, D.DomainOffset, D.EmitterOffset]);
        U.set([D.TlasRoot, D.TlasEnd, D.Domain.length, D.Emitters.length], 4);
        U.set([S.PathBounces, S.PathCandidates, Math.min(H.Lighting.Lights.length, MaxSceneLights), S.PathSeed], 8);
        F.set([S.PathRRStart, S.PathUniformProposal, S.PathHistoryWeight, S.PathSpatialPasses], 12);
        U.set([D.DomainTriangleCount, this.Acceleration.Generation, S.Renderer === RendererModes.Reference ? 1 : 0, H.Effects.Metrics.Live > 0 ? 1 : 0], 16);
        F.set([H.Cabin.Settings.Brightness, H.Cabin.Settings.Screens ? 1 : 0, H.Cabin.Settings.GlassTransmission, 0], 20);
        F.set(H.PathJitterDelta || [0,0], 24);
        this.Exchange.Device.queue.writeBuffer(this.Configuration, 0, Bytes);
    }

    RecordTrace(Encoder, Parity, TimestampWrites)
    {
        const [W, H] = this.TraceExtent;
        const Pass = Encoder.beginComputePass({ label: "Triangle ReSTIR GI path tracing", timestampWrites: TimestampWrites });
        Pass.setBindGroup(1, this.SceneGroup);
        Pass.setPipeline(this.GenerateProgram); Pass.setBindGroup(0, this.GenerateGroups[Parity]);
        Pass.dispatchWorkgroups(Math.ceil(W / 8), Math.ceil(H / 8));
        Pass.setPipeline(this.SpatialProgram); Pass.setBindGroup(0, this.SpatialGroups[Parity]);
        Pass.dispatchWorkgroups(Math.ceil(W / 8), Math.ceil(H / 8));
        Pass.end();
    }

    RecordReconstruction(Encoder, Parity, TimestampWrites)
    {
        const [W, H] = this.TraceExtent, S = this.Host.Settings;
        const Denoise = S.VarianceKernel && S.Renderer !== RendererModes.Reference;
        let Projection = 0;
        const Pass = Encoder.beginComputePass({ label: "Path SVGF and screen reconstruction", timestampWrites: TimestampWrites });
        if (Denoise) {
            Pass.setPipeline(this.TemporalProgram); Pass.setBindGroup(0, this.TemporalGroups[Parity]);
            Pass.dispatchWorkgroups(Math.ceil(W / 8), Math.ceil(H / 8));
            Pass.setPipeline(this.WaveletProgram);
            const Count = Math.max(1, Math.min(5, S.PathSpatialPasses));
            for (let I = 0; I < Count; ++I) {
                Pass.setBindGroup(0, this.WaveletGroups[Parity][I]); Pass.dispatchWorkgroups(Math.ceil(W / 8), Math.ceil(H / 8));
            }
            Projection = Count === 1 ? 1 : 2 + ((Count - 2) & 1);
        }
        Pass.setPipeline(this.ResolveProgram); Pass.setBindGroup(0, this.ResolveGroups[Parity][Projection]);
        Pass.dispatchWorkgroups(Math.ceil(this.ScreenExtent[0] / 8), Math.ceil(this.ScreenExtent[1] / 8));
        Pass.end();
    }

    Describe()
    {
        return [DescribeRayTracing(this.Exchange), ...this.Acceleration.Describe(),
            `${this.Host.Settings.PathBounces} path vertices · ${this.Host.Settings.PathCandidates} fresh candidate(s)/pixel/frame`,
            `144-byte reservoirs + primary guides · ${this.TraceExtent?.join("×") || "–"} trace pixels`,
            "RGB transport and luminance moments: float32; denoising can be disabled."];
    }
}
