// Opt-in EXECUTED GPU tests, not Dawn's null command validator.
// RUN_PATH_GPU=1 BACKEND=vulkan node --test Tools/PathTracingGpu.test.mjs
// A real Vulkan adapter (including SwiftShader) and Tools' webgpu dependency are required.
import test from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs/promises";
import { DeviceExchange } from "../Source/DeviceExchange.js";
import { ShaderInterchange } from "../Source/ShaderInterchange.js";
import { SceneStructure, OccupantRole } from "../Source/FacetStructure.js";
import { ConstructMaterialSpecification, EncodeMaterialRecords, CourseMaterial, MarkedFamilyFirst, MarkedFamilyLast,
    MarkedFamilyExtraFirst, MarkedFamilyExtraLast } from "../Source/MaterialSpecification.js";
import { ConstructIdentityTransform, ConstructViewTransform, ConstructPerspectiveTransform, MultiplyTransform, InvertTransform } from "../Source/TransformProjection.js";
import { ReSTIRPathTracer, PathTracerSources } from "../Source/ReSTIRPathTracer.js";
import { ResolvePathSettings } from "../Source/PathTracingSettings.js";

const Enabled = process.env.RUN_PATH_GPU === "1";
const Near = (A,B,E,Name) => assert.ok(Math.abs(A-B)<E,`${Name}: ${A.toFixed(5)} vs ${B.toFixed(5)}, tolerance ${E}`);
const Plane = (Z, Family) => [
    { Family, Corners: [[-1000,-1000,Z],[1000,-1000,Z],[1000,1000,Z]] },
    { Family, Corners: [[-1000,-1000,Z],[1000,1000,Z],[-1000,1000,Z]] }
];
const Root = new URL("../",import.meta.url);

test("executed ReSTIR transport: analytic environments/cavities/glass, GGX replay and SVGF variance", {
    skip: !Enabled, timeout: 600000
}, async () => {
    assert.notEqual(process.env.BACKEND,"null","the null backend DOES NOT execute these tests");
    const { create, globals } = await import("webgpu"); Object.assign(globalThis,globals);
    let Gpu = create([`backend=${process.env.BACKEND || "vulkan"}`]);
    Object.defineProperty(globalThis,"navigator",{ configurable: true, value: { gpu: Gpu } });
    globalThis.fetch = async (Name) => ({ ok: true, text: () => fs.readFile(new URL(String(Name),Root),"utf8") });
    const X = new DeviceExchange(), Device = await X.ConstructDevice();
    const Shaders = new ShaderInterchange(X,"Shader");
    await Shaders.RetrieveSources([...PathTracerSources,"SceneRecords.wgsl","FrameDescriptor.wgsl","PackingCodec.wgsl",
        "SurfaceReflectance.wgsl","SkyProjection.wgsl","EffectRecords.wgsl","VehicleVisual.wgsl","ClusteredLights.wgsl"]);
    const Constants = {};
    for (const Name of ["VehicleGlass","Tyre","SteeringWheel","TabletHousing","DisplayGlass"])
        Constants[Name+"Family"] = { Type: "u32", Value: CourseMaterial[Name] };
    Object.assign(Constants,{
        TyreMarkFamily: { Type: "u32", Value: CourseMaterial.TyreRubber }, RimMarkFamily: { Type: "u32", Value: CourseMaterial.RimScrape },
        MarkedFamilyFirst: { Type: "u32", Value: MarkedFamilyFirst }, MarkedFamilyLast: { Type: "u32", Value: MarkedFamilyLast },
        MarkedFamilyExtraFirst: { Type: "u32", Value: MarkedFamilyExtraFirst }, MarkedFamilyExtraLast: { Type: "u32", Value: MarkedFamilyExtraLast }
    });
    for (const [Name,Value] of Object.entries({ TyreMarkExtentX: 1,TyreMarkExtentY: 1,TyreMarkOriginX: 0,TyreMarkOriginY: 0,TyreMarkPatchWidth: 0.2 }))
        Constants[Name] = { Type: "f32", Value };
    const Module = Shaders.ConstructModule.bind(Shaders);
    Shaders.ConstructModule = (File,C={},Label=File) => Module(File,{...Constants,...C},Label);
    const Scene = new SceneStructure();
    const Set = Scene.ConstructFacetSet("Analytic plane",Plane(0,0),0,0);
    for (let I=0; I<5; ++I) {
        Scene.ConstructOccupant(`Fixture ${I}`,Set,ConstructIdentityTransform(),OccupantRole.Raster);
        Scene.Occupants[I].Inactive = I>0;
    }
    const Materials = ConstructMaterialSpecification();
    const Diffuse = (Albedo,Emission=[0,0,0]) => ({ Albedo, Emission, Roughness: 1, Metalness: 0,
        CoatWeight: 0,CoatRoughness: 1,SpecularLevel: 0,Transmission: 0 });
    Materials[0] = Diffuse([0.7,0.4,0.2]); Materials[1] = Diffuse([0.3,0.5,0.8],[1,0.5,0.25]);
    const Sky = X.ConstructTexture({ size: [1,1],format: "rgba16float",usage: GPUTextureUsage.TEXTURE_BINDING|GPUTextureUsage.COPY_DST });
    const Mark = X.ConstructTexture({ size: [1,1],format: "rgba8unorm",usage: GPUTextureUsage.TEXTURE_BINDING });
    const Depth = X.ConstructTexture({ size: [1,1],format: "depth32float",usage: GPUTextureUsage.TEXTURE_BINDING|GPUTextureUsage.RENDER_ATTACHMENT });
    const Dummy = X.ConstructTexture({ size: [1,1],format: "rgba16float",usage: GPUTextureUsage.TEXTURE_BINDING });
    const H = {
        Exchange: X,Shaders,Scene,Materials,Settings: {...ResolvePathSettings(new URLSearchParams()),
            TemporalTallyLimit: 8,PathBounces: 6,PathSpatialPasses: 3,VarianceKernel: true},
        FrameRecords: X.ConstructRecords(512,GPUBufferUsage.UNIFORM|GPUBufferUsage.COPY_DST),
        MaterialRecords: X.ConstructRecordsFrom(EncodeMaterialRecords(Materials),GPUBufferUsage.STORAGE),
        OccupantRecords: X.ConstructRecordsFrom(Scene.EncodeOccupantRecords(),GPUBufferUsage.STORAGE),
        VehicleRecords: X.ConstructRecords(208,GPUBufferUsage.STORAGE),
        LightRecords: X.ConstructRecords(32*64,GPUBufferUsage.STORAGE),
        Lighting: { Lights: [] }, Marks: { View: Mark.createView() },
        Atmosphere: { SkyView: Sky.createView(),SkySampler: X.ConstructSampler({magFilter: "linear",minFilter: "linear"}) },
        Effects: { Records: X.ConstructRecords(1120,GPUBufferUsage.UNIFORM),Metrics: {Live: 0} },
        Cabin: { Settings: { Brightness: 1,Screens: false,GlassTransmission: 0.72 } }
    };
    const ConstructProgram = Shaders.ConstructComputeProgram.bind(Shaders);
    Shaders.ConstructComputeProgram = async (File, Entry, Layouts) => {
        const Start = performance.now(); const Program = await ConstructProgram(File,Entry,Layouts);
        console.log(`GPU pipeline ${Entry}: ${((performance.now()-Start)/1000).toFixed(2)}s`); return Program;
    };
    const P = new ReSTIRPathTracer(H); await P.ConstructPrograms();
    const W=64, Height=64, Pixels=W*Height;
    P.ConstructTargets([W,Height],[W,Height],{Views: { Attributes: Dummy.createView(),Depth: Depth.createView(),Direct: Dummy.createView() }});
    let Tilted=false;
    const Frame = (Index,Restart,SkyScale) => {
        const Bytes = new ArrayBuffer(512), F = new Float32Array(Bytes), U = new Uint32Array(Bytes);
        const Eye=Tilted ? [0,-1,1] : [0,0,1];
        const View = ConstructViewTransform(Eye,[0,0,0],Tilted ? [0,0,1] : [0,1,0]);
        const VP = MultiplyTransform(ConstructPerspectiveTransform(Math.PI/3,1,0.01),View);
        F.set(VP); F.set(InvertTransform(VP),16); F.set(VP,32); F.set(VP,48); F.set(View,64);
        F.set(Eye,80); U[83]=Index; F.set([0,0,1],84); F[91]=SkyScale;
        F.set([W,Height,1/W,1/Height],92); F.set([W,Height,1/W,1/Height],96);
        U[102]=1; U[103]=1|2|4|256|8192|(Restart?2048:0); F[105]=8; F[106]=8; U[107]=3; F[111]=1; F[114]=1;
        Device.queue.writeBuffer(H.FrameRecords,0,Bytes);
    };
    const ReadRaw = async (Texture=P.RawTexture) => {
        const Read = Device.createBuffer({ size: Pixels*16,usage: GPUBufferUsage.COPY_DST|GPUBufferUsage.MAP_READ });
        const Encoder = Device.createCommandEncoder();
        Encoder.copyTextureToBuffer({texture: Texture},{buffer: Read,bytesPerRow: W*16},[W,Height]);
        Device.queue.submit([Encoder.finish()]); await Read.mapAsync(GPUMapMode.READ);
        const F = new Float32Array(Read.getMappedRange().slice(0)); Read.unmap(); Read.destroy();
        const Sum=[0,0,0];
        for (let I=0; I<Pixels; ++I) for (let C=0; C<3; ++C) {
            assert.ok(Number.isFinite(F[I*4+C]) && F[I*4+C]>=0,"raw radiance must be finite and nonnegative"); Sum[C]+=F[I*4+C]/Pixels;
        }
        ReadRaw.Variance=0;
        for (let I=0;I<Pixels;I++) ReadRaw.Variance+=(F[I*4]-Sum[0])**2/Pixels;
        return Sum;
    };
    const Run = async (Renderer, SkyScale, Frames=6) => {
        H.Settings.Renderer=Renderer;
        console.log(`GPU scene: ${Renderer}, sky scale ${SkyScale}, ${H.Settings.PathBounces} vertices`);
        const Sum=[0,0,0];
        for (let I=0; I<Frames; ++I) {
            Frame(I,I===0,SkyScale); P.UploadConfiguration();
            const Encoder = Device.createCommandEncoder(); P.Acceleration.RecordRefit(Encoder); P.RecordTrace(Encoder,I&1);
            Device.queue.submit([Encoder.finish()]);
            const Mean = await ReadRaw(); Mean.forEach((V,C) => Sum[C]+=V/Frames);
        }
        return Sum;
    };
    try {
        Device.queue.writeTexture({texture: Sky},Uint16Array.of(0x3c00,0x3c00,0x3c00,0x3c00),{bytesPerRow: 8},[1,1]);
        const EnvPT = await Run("reference",1), EnvRIS = await Run("restir",1);
        for (let C=0; C<3; ++C) {
            Near(EnvPT[C],Materials[0].Albedo[C],0.035,`Lambert reference channel ${C}`);
            Near(EnvRIS[C],Materials[0].Albedo[C],0.045,`Lambert ReSTIR channel ${C}`);
            Near(EnvPT[C],EnvRIS[C],0.045,`Lambert PT/ReSTIR channel ${C}`);
        }
        // Exercise the actual short-history SVGF/à-trous path, not just its
        // command layout. Previously the zero first-frame variance rejected all
        // noisy neighbours and left the output just as noisy as raw radiance.
        const RawMean=await ReadRaw(), RawVariance=ReadRaw.Variance;
        const Reconstruction=Device.createCommandEncoder(); P.RecordReconstruction(Reconstruction,1);
        Device.queue.submit([Reconstruction.finish()]);
        const FilteredMean=await ReadRaw(P.Textures.Wavelets[1]), FilteredVariance=ReadRaw.Variance;
        assert.ok(FilteredVariance<RawVariance*0.85,`SVGF variance ${FilteredVariance} must improve on raw ${RawVariance}`);
        for(let C=0;C<3;C++) Near(FilteredMean[C],RawMean[C],0.05,`filtered mean channel ${C}`);
        Tilted=true;
        const TiltedMean=await Run("restir",1), TiltedRaw=await ReadRaw(), TiltedVariance=ReadRaw.Variance;
        const TiltedReconstruction=Device.createCommandEncoder(); P.RecordReconstruction(TiltedReconstruction,1);
        Device.queue.submit([TiltedReconstruction.finish()]);
        const TiltedFiltered=await ReadRaw(P.Textures.Wavelets[1]), TiltedFilteredVariance=ReadRaw.Variance;
        assert.ok(TiltedFilteredVariance<TiltedVariance*0.15,"a receding flat plane must not be mistaken for a depth discontinuity");
        for(let C=0;C<3;C++) {
            Near(TiltedMean[C],Materials[0].Albedo[C],0.05,`tilted Lambert channel ${C}`);
            Near(TiltedFiltered[C],TiltedRaw[C],0.05,`tilted filtered mean channel ${C}`);
        }
        Tilted=false;
        Scene.FacetSets[0].Facets=[...Plane(0,0),...Plane(2,1)];
        Scene.Occupants[0].RayGeometryVersion=1; H.Settings.PathBounces=16;
        P.UpdateScene(1);
        const CavityPT=await Run("reference",0), CavityRIS=await Run("restir",0);
        for (let C=0; C<3; ++C) {
            const A=Materials[0].Albedo[C], B=Materials[1].Albedo[C], E=Materials[1].Emission[C];
            const Expected=A*E/(1-A*B);
            Near(CavityPT[C],Expected,0.06,`16-vertex analytic cavity reference channel ${C}`);
            Near(CavityRIS[C],Expected,0.07,`16-vertex analytic cavity ReSTIR channel ${C}`);
            Near(CavityPT[C],CavityRIS[C],0.07,`cavity PT/ReSTIR channel ${C}`);
        }
        // A glossy/coated SECOND vertex exercises view-dependent suffix replay,
        // not just a Lambert colour cached at that point.
        const BaseDiffuse=Materials[0], BaseEmitter=Materials[1];
        Materials[0]={...BaseDiffuse,Metalness: 0.85,Roughness: 0.24,CoatWeight: 0.7,CoatRoughness: 0.12,SpecularLevel: 0.04};
        Materials[1]={...BaseEmitter,Metalness: 0.45,Roughness: 0.63,CoatWeight: 0.4,CoatRoughness: 0.3,SpecularLevel: 0.04};
        Device.queue.writeBuffer(H.MaterialRecords,0,EncodeMaterialRecords(Materials));
        const GlossyPT=await Run("reference",0), GlossyRIS=await Run("restir",0);
        for(let C=0;C<3;C++) {
            assert.ok(GlossyPT[C]>0 && GlossyPT[C]<3 && GlossyRIS[C]>0 && GlossyRIS[C]<3,"bounded coated-cavity mean");
            Near(GlossyPT[C],GlossyRIS[C],0.07*Math.max(1,GlossyPT[C]),`GGX/coated suffix PT/ReSTIR channel ${C}`);
        }
        // Pure coloured thin-sheet transmission over the Lambert plane. With no
        // reflective/diffuse glass lobe, both the illumination AND camera path
        // cross it once: outgoing RGB must be albedo * tint^2 under white sky.
        Materials[0]=BaseDiffuse;
        Materials[1]={...Diffuse([0.2,0.5,0.9]),Transmission: 1};
        Device.queue.writeBuffer(H.MaterialRecords,0,EncodeMaterialRecords(Materials));
        Scene.FacetSets[0].Facets=[...Plane(0,0),...Plane(0.5,1)];
        Scene.Occupants[0].RayGeometryVersion=2; P.UpdateScene(3);
        const GlassPT=await Run("reference",1), GlassRIS=await Run("restir",1);
        for(let C=0;C<3;C++) {
            const Tint=0.88+0.12*Materials[1].Albedo[C], Expected=Materials[0].Albedo[C]*Tint*Tint;
            Near(GlassPT[C],Expected,0.01,`analytic transmitted reference channel ${C}`);
            Near(GlassRIS[C],Expected,0.01,`analytic transmitted ReSTIR channel ${C}`);
        }
        assert.equal(X.ReportedErrors.length,0);
        console.log("GPU energy checks:",JSON.stringify({EnvPT,EnvRIS,CavityPT,CavityRIS,GlossyPT,GlossyRIS,GlassPT,GlassRIS,
            RawVariance,FilteredVariance,TiltedVariance,TiltedFilteredVariance}));
    } finally {
        Device.destroy(); delete globalThis.navigator; Gpu=null;
    }
});
