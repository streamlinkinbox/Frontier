import {ClusteredGeometry} from './ClusteredGeometry.js';
import {VirtualizedGeometry} from './VirtualizedGeometry.js';
//============================================================================================================================================
//                                                           SURFACEPROJECTION.JS
//============================================================================================================================================
// 🧩 Primary visibility (depth, attributes, motion) and the direct sun radiance resolved over it, sized to the screen.

import { VertexStride } from "./FacetStructure.js";

export class SurfaceProjection
{
    constructor(Exchange, Shaders)
    {
        this.Exchange = Exchange;
        this.Shaders  = Shaders;
        this.Extent   = [0, 0];
    }

    // 📝 TyreMarks: the TyreMarkDepot whose deposit the raster reads for the road families.
    // 📝 Effects (EffectSequence) shares this pass: its record masks the car here, and its cubes draw after the runs.
    // 📝 VehicleRecords: the VehicleRecord the vertex stage deforms the tyres and turns the steering wheel by.
    async ConstructPrograms(FrameRecords, OccupantRecords, MaterialRecords, DistanceLayout, SamplingLayout, RayTracingLayout, TyreMarks, Effects, VehicleRecords)
    {
        this.FrameRecords    = FrameRecords;
        this.OccupantRecords = OccupantRecords;
        this.MaterialRecords = MaterialRecords;
        this.Effects         = Effects;
        this.RasterLayout = this.Exchange.ConstructDescriptorLayout(["Uniform", "StorageRead", "Texture2D", "Sampler", "Uniform", "StorageRead"],
                                                                    GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, "Surface raster");
        this.RasterGroup = this.Exchange.ConstructDescriptorGroup(this.RasterLayout, [FrameRecords, OccupantRecords, TyreMarks.View, TyreMarks.Sampler,
                                                                                      Effects.Records, VehicleRecords]);
        this.RasterProgram = await this.Shaders.ConstructRasterProgram("SurfaceRaster.wgsl", {
            VertexEntry: "SurfaceVertex", FragmentEntry: "SurfaceFragment",
            VertexLayouts: [{ arrayStride: VertexStride, attributes: [
                { shaderLocation: 0, offset: 0,  format: "float32x3" },
                { shaderLocation: 1, offset: 12, format: "uint32" },
                { shaderLocation: 2, offset: 16, format: "float32x3" },
                { shaderLocation: 3, offset: 28, format: "uint32" }] },{arrayStride:4,stepMode:"instance",attributes:[{shaderLocation:4,offset:0,format:"uint32"}]}],
            Primitive: { topology: "triangle-list", cullMode: "none" },
            DepthStencil: { format: "depth32float", depthWriteEnabled: true, depthCompare: "greater" },
            Targets: [{ format: "rgba16float" }, { format: "rg16float" }]
        }, [this.RasterLayout]);
        this.Clusters=new ClusteredGeometry(this.Exchange,this.Shaders);await this.Clusters.ConstructPrograms(FrameRecords,OccupantRecords);
        this.Virtualized=new VirtualizedGeometry(this.Exchange,this.Shaders);await this.Virtualized.ConstructPrograms(FrameRecords,OccupantRecords);
        this.DirectLayout = this.Exchange.ConstructDescriptorLayout(
            ["Uniform", "StorageRead", "Texture2D", "TextureDepth2D", "TextureDepth2DLayered", "SamplerComparison", "Uniform", "Texture2D",
             "Sampler", "StorageTexture2D:rgba16float", "StorageRead", "Uniform", "Texture2D", "Sampler"], GPUShaderStage.COMPUTE, "Direct radiance");
        this.DirectProgram = await this.Shaders.ConstructComputeProgram("DirectRadiance.wgsl", "IntegrateDirectRadiance",
                                                                        [this.DirectLayout, DistanceLayout, SamplingLayout, RayTracingLayout]);
    }

    ConstructTargets(Width, Height, Shadow, Atmosphere, LightRecords = null)
    {
        this.Extent = [Width, Height];
        if (LightRecords) this.LightRecords = LightRecords;
        for (const Texture of [this.DepthTexture, this.AttributeTexture, this.MotionTexture, this.DirectTexture])
            if (Texture) Texture.destroy();
        const Attachment = GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING;
        this.DepthTexture     = this.Exchange.ConstructTexture({ label: "Surface depth", size: [Width, Height], format: "depth32float", usage: Attachment });
        this.AttributeTexture = this.Exchange.ConstructTexture({ label: "Surface attributes", size: [Width, Height], format: "rgba16float", usage: Attachment });
        this.MotionTexture    = this.Exchange.ConstructTexture({ label: "Surface motion", size: [Width, Height], format: "rg16float", usage: Attachment });
        this.DirectTexture    = this.Exchange.ConstructTexture({ label: "Direct radiance", size: [Width, Height], format: "rgba16float",
                                                                 usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING });
        this.Views = {
            Depth:      this.DepthTexture.createView(),
            Attributes: this.AttributeTexture.createView(),
            Motion:     this.MotionTexture.createView(),
            Direct:     this.DirectTexture.createView()
        };
        this.Virtualized?.ConstructTargets(Width, Height);
        this.DirectGroup = this.Exchange.ConstructDescriptorGroup(this.DirectLayout,
            [this.FrameRecords, this.MaterialRecords, this.Views.Attributes, this.Views.Depth, Shadow.LayeredView, Shadow.ComparisonSampler,
             Shadow.ReceiverRecords, Atmosphere.SkyView, Atmosphere.SkySampler, this.Views.Direct, this.LightRecords,this.Clouds.Shadows.Params,this.Clouds.Shadows.View,this.Clouds.Shadows.Sampler]);
    }

    RecordSurface(Encoder, Scene, VertexRecords, TimestampWrites,ClusterQuery)
    {
        this.Clusters.Prepare(Scene);
        // 📝 The virtualized takeover must never take the frame loop down with it: any failure inside
        //    Prepare (bind-group construction, hierarchy build, …) disables it and the lossless-cluster
        //    path renders the frame instead. (An uncaught throw here previously aborted AdvanceFrame
        //    before submit, freezing the last presented image on screen.)
        let Takeover = null;
        try { Takeover = this.Virtualized?.Prepare(Scene) ?? null; }
        catch (Error) { console.error("[VirtualizedGeometry] Prepare failed; virtualized path disabled:", Error); if (this.Virtualized) this.Virtualized.Enabled = false; }
        const VirtualActive = Boolean(Takeover?.Records);
        if(this.ClusteredEnabled!==false)this.Clusters.RecordCull(Encoder,ClusterQuery);
        if (VirtualActive) this.Virtualized.RecordCullPhase1(Encoder);
        const BeginPass = (Label, Clear) => Encoder.beginRenderPass({
            label: Label,
            colorAttachments: [
                { view: this.Views.Attributes, clearValue: [0, 0, 0, 0], loadOp: Clear ? "clear" : "load", storeOp: "store" },
                { view: this.Views.Motion, clearValue: [0, 0, 0, 0], loadOp: Clear ? "clear" : "load", storeOp: "store" }],
            depthStencilAttachment: { view: this.Views.Depth, depthClearValue: 0.0, depthLoadOp: Clear ? "clear" : "load", depthStoreOp: "store" },
            timestampWrites: Clear ? TimestampWrites : undefined
        });
        // 📝 Draws the lossless-cluster fallback runs/clusters plus (for the virtualized path) one indirect phase.
        const DrawRuns = (Pass, Phase) =>
        {
            Pass.setPipeline(this.RasterProgram);
            Pass.setBindGroup(0, this.RasterGroup);
            Pass.setVertexBuffer(0, VertexRecords);
            if(Phase === 2){ if (VirtualActive) this.Virtualized.RecordDraws(Pass, this.Clusters.Instances, 2); return; }
            if(this.ClusteredEnabled!==false)
            {
                Pass.setIndexBuffer(this.Clusters.Indices,'uint32');
                for(const D of this.Clusters.Draws)
                {
                    if(D.Run){Pass.setVertexBuffer(0, VertexRecords);Pass.setVertexBuffer(1,this.Clusters.Instances);const R=D.Run,S=Scene.FacetSets[R.FacetSet];Pass.draw(S.VertexCount,R.Count,S.FirstVertex,R.First);}
                    else if(!VirtualActive || !this.Virtualized.OwnsOccupant(D.Occupant)){Pass.setVertexBuffer(0, VertexRecords);Pass.setVertexBuffer(1,this.Clusters.Instances,D.Occupant*4,4);Pass.drawIndexedIndirect(this.Clusters.Commands,D.Cluster*20);}
                }
            }
            else {Pass.setVertexBuffer(1,this.Clusters.Instances);for(const Run of Scene.DrawRuns){const Set=Scene.FacetSets[Run.FacetSet];Pass.draw(Set.VertexCount,Run.Count,Set.FirstVertex,Run.First);}}
            if (VirtualActive && Phase) this.Virtualized.RecordDraws(Pass, this.Clusters.Instances, Phase);
        };
        if (!VirtualActive)
        {
            const Pass = BeginPass("Surface projection", true);
            DrawRuns(Pass, 0);
            if (this.Effects) this.Effects.Record(Pass);
            Pass.end();
            return;
        }
        // 📝 Two-phase occlusion: phase-1 cut draws (tested against last frame's Hi-Z), rebuild the pyramid
        //    from their depth, then phase-2 draws add clusters that were occluded last frame but are visible now.
        const Phase1 = BeginPass("Surface projection (virtualized phase 1)", true);
        DrawRuns(Phase1, 1);
        Phase1.end();
        this.Virtualized.RecordHiZ(Encoder, this.Views.Depth);
        this.Virtualized.RecordCullPhase2(Encoder);
        const Phase2 = BeginPass("Surface projection (virtualized phase 2)", false);
        DrawRuns(Phase2, 2);
        if (this.Effects) this.Effects.Record(Phase2);
        Phase2.end();
        // 📝 Phase 3: visibility buffer + deferred material resolve + two-layer refraction gather, from the
        //    same indirect commands (results in Virtualized.ResolveView for the display chain).
        this.Virtualized.RecordVisibilityResolve(Encoder, this.RasterGroup, this.Clusters.Instances);
    }

    RecordDirect(Encoder, DistanceGroup, SamplingGroup, RayTracingGroup, TimestampWrites)
    {
        const Pass = Encoder.beginComputePass({ label: "Direct radiance", timestampWrites: TimestampWrites });
        Pass.setPipeline(this.DirectProgram);
        Pass.setBindGroup(0, this.DirectGroup);
        Pass.setBindGroup(1, DistanceGroup);
        Pass.setBindGroup(2, SamplingGroup);
        Pass.setBindGroup(3, RayTracingGroup);
        Pass.dispatchWorkgroups(Math.ceil(this.Extent[0] / 8), Math.ceil(this.Extent[1] / 8));
        Pass.end();
    }
}
