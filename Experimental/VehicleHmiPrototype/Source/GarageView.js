//============================================================================================================================================
//                                                             GARAGEVIEW.JS
//============================================================================================================================================
// 🧩 The Garage's car, rendered LIVE by the demo's own ReSTIR renderer through the configurator's orbit camera.
//
//    This is a second view of the same world, not a second picture of it: the very scene, BVH, materials, cards, light and vehicle records the
//    studio camera renders from, with its own camera, its own surface, resampling, variance and display targets and its own temporal history.
//    The studio's other cars are hidden from it (raster and rays alike, ViewWorld.wgsl), so it shows one vehicle; the paint ripple and the X-ray
//    scan are fields of its frame record, so the surface shading, the traced hits and the reflections all see them.
//
//    Over the tone-mapped picture it draws what the reference draws over its canvas (ScanOverlay.wgsl): the ghost shell, wireframe, curtain,
//    particles, the HUD canvas's tags and frame, the stage's vignette. The result is a texture the cabin overlay composites into the tablet's
//    preview well.

import { SurfaceProjection } from "./SurfaceProjection.js";
import { NearestDepthSpace } from "./NearestDepthSpace.js";
import { ResamplingSequence } from "./ResamplingSequence.js";
import { VarianceIntegrator } from "./VarianceIntegrator.js";
import { DisplayProjection } from "./DisplayProjection.js";
import { ScanSequence, ScanSpecification, ResolveDisplayColour } from "./ScanSequence.js";
import { ConvertHexToLinear } from "./PaintRipple.js";
import { ConstructScanEdges } from "./VehicleEdges.js";
import { ScanHud, ScanTags, ProjectToStage, ResolveScanLineTop } from "./ScanHud.js";
import { GarageOrbit, ResolveVehicleFrame } from "./GarageOrbit.js";
import { PickVehicle } from "./VehiclePicking.js";
import { ClampStudioEye } from "./LobbyCameraMotion.js";
import { VehicleGeometry } from "./VehicleSpecification.js";
import { VertexStride } from "./FacetStructure.js";
import { InvertTransform, AddVector, ScaleVector, DotVector } from "./TransformProjection.js";

export const GarageExtent = Object.freeze([640, 400]);   // [px] the reference's own stage size, the preview well at 1×
const FrameRecordExtent   = 640;                         // [B]  FrameRecord in SceneRecords.wgsl
const ScanRecordExtent    = 192;                         // [B]  ScanRecord in ScanOverlay.wgsl
const SparkleSeed         = 0x9e3779b9;
const ReferenceNose       = Object.freeze([2.75, 1.25]);   // [m]  Truck.tsx's default ripple centre: along the car, height in the reference's world
const ReferenceGround     = 0.62;                          // [m]  its ground lies this far below that world's origin

//------------------------------------------------------------------------------------------------------------------------
//                                                        DRAW RUNS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The scene's draw runs restricted to the occupants Selected accepts: a run is a facet set drawn for consecutive occupants, so a selection
//    that skips some of them splits it.
export function SelectDrawRuns(Runs, Selected)
{
    const Result = [];
    for (const Run of Runs)
    {
        let Start = -1;
        for (let Instance = 0; Instance <= Run.Count; ++Instance)
        {
            const Accepted = Instance < Run.Count && Selected(Run.First + Instance);
            if (Accepted && Start < 0)
            {
                Start = Instance;
            }
            else if (!Accepted && Start >= 0)
            {
                Result.push({ FacetSet: Run.FacetSet, First: Run.First + Start, Count: Instance - Start });
                Start = -1;
            }
        }
    }
    return Result;
}

// 📝 The front's plane in world space: the car's forward axis and the offset that puts the front Front metres along the car from its middle, so
//    dot(P, xyz) + w is the distance past the front and the sweep follows the car wherever it stands.
export function ResolveScanPlane(Stage, Front)
{
    return [Stage.Forward[0], Stage.Forward[1], Stage.Forward[2], -DotVector(Stage.Origin, Stage.Forward) - Front];
}

// 📝 128 occupant bits in four words, the layout of FrameRecord.HiddenOccupants and ScannedOccupants.
export function EncodeOccupantMask(Indices)
{
    const Words = new Uint32Array(4);
    for (const Index of Indices)
    {
        if (Index >= 0 && Index < 128)
        {
            Words[Index >> 5] |= (1 << (Index & 31)) >>> 0;
        }
    }
    return Words;
}

// 📝 drei's Sparkles: count points uniform in a box (randFloatSpread per axis), the group lifted to the box's height; a seeded generator so a
//    session and a test see the same cloud. Per point: along, up (above the ground), right; one spare.
export function ConstructSparkleCloud(Count = ScanSpecification.ParticleCount, Box = ScanSpecification.ParticleBox,
                                      Centre = ScanSpecification.ParticleCentre)
{
    let State = SparkleSeed >>> 0;
    const Next = () =>
    {
        State = (State + 0x6d2b79f5) >>> 0;
        let T = State;
        T = Math.imul(T ^ (T >>> 15), T | 1);
        T ^= T + Math.imul(T ^ (T >>> 7), T | 61);
        return ((T ^ (T >>> 14)) >>> 0) / 4294967296.0;
    };
    const Records = new Float32Array(Count * 4);
    for (let Index = 0; Index < Count; ++Index)
    {
        Records[Index * 4]     = Box[0] * (0.5 - Next());
        Records[Index * 4 + 1] = Centre + Box[1] * (0.5 - Next());
        Records[Index * 4 + 2] = Box[2] * (0.5 - Next());
    }
    return Records;
}

function ConstructDefaultCanvas(Width, Height)
{
    if (typeof document === "undefined" || typeof document.createElement !== "function")
    {
        throw new Error("no 2D canvas in this host");
    }
    const Canvas = document.createElement("canvas");
    Canvas.width = Width;
    Canvas.height = Height;
    return Canvas;
}

const AdditiveBlend = { color: { srcFactor: "src-alpha", dstFactor: "one", operation: "add" },
                        alpha: { srcFactor: "zero", dstFactor: "one", operation: "add" } };
const OverBlend     = { color: { srcFactor: "src-alpha", dstFactor: "one-minus-src-alpha", operation: "add" },
                        alpha: { srcFactor: "zero", dstFactor: "one", operation: "add" } };
// 📝 CSS mix-blend-screen of a layer with alpha: the shader premultiplies, the blend adds src · (1 − dst) — 1 − (1 − dst)(1 − src).
const ScreenBlend   = { color: { srcFactor: "one", dstFactor: "one-minus-src", operation: "add" },
                        alpha: { srcFactor: "zero", dstFactor: "one", operation: "add" } };

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE RUNTIME
//------------------------------------------------------------------------------------------------------------------------

export class GarageView
{
    // Host: the IlluminationHost, whose resources this view shares. Options: Extent, CreateCanvas.
    constructor(Host, Options = {})
    {
        this.Host     = Host;
        this.Exchange = Host.Exchange;
        this.Extent   = (Options.Extent || GarageExtent).slice();
        this.Orbit    = new GarageOrbit();
        this.Scan     = new ScanSequence();
        this.Hud      = new ScanHud(Options.CreateCanvas || ConstructDefaultCanvas);
        this.Tick     = 0;
        this.Restart  = true;
        this.Active   = false;    // recorded last frame
        this.Forced   = false;    // a script drives the view without the tablet open
        this.PreviousUnjittered = null;
        this.PickInverse = null;
        this.HudClock = 0.0;
        this.Facets   = 0;
        this.Metrics  = { Recorded: 0, Ghost: 0, HudUploads: 0 };
        this.External = true;
        this.Loaded   = true;
        const Scene = Host.Scene;
        const Hidden = [...Scene.DisplayOccupants, ...(Scene.PlateOccupants || []).slice(1)];
        const Scanned = [Scene.VehicleOccupants.Body, ...Scene.VehicleOccupants.Wheels, ...(Scene.PlateOccupants || []).slice(0, 1)];
        this.HiddenOccupants  = new Set(Hidden);
        this.ScannedOccupants = new Set(Scanned);
        this.HiddenMask  = EncodeOccupantMask(Hidden);
        this.ScannedMask = EncodeOccupantMask(Scanned);
        this.Facets = Scene.FacetSets[Scene.VehicleBodySet].Facets.length
                    + 4 * Scene.FacetSets[Scene.Occupants[Scene.VehicleOccupants.Wheels[0]].FacetSet].Facets.length;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                  BRING-UP
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 The stack the studio camera runs, declared over the same layouts, so the programs it asks for are the ones already built (ShaderInterchange).
    async ConstructPrograms()
    {
        const Host = this.Host, Exchange = this.Exchange, Shaders = Host.Shaders;
        this.FrameRecords = Exchange.ConstructRecords(FrameRecordExtent, GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, "Garage frame record");
        this.ScanRecords  = Exchange.ConstructRecords(ScanRecordExtent, GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, "Scan record");
        this.Surface = new SurfaceProjection(Exchange, Shaders);
        await this.Surface.ConstructPrograms(this.FrameRecords, Host.OccupantRecords, Host.MaterialRecords, Host.Distance.TraversalLayout,
                                             Host.Cards.SamplingLayout, Host.RayTracing.TraversalLayout, Host.Marks, Host.Effects, Host.VehicleRecords);
        // 📝 The effect cubes (derez, sparks of the studio) belong to the studio's picture; this view draws the car and the room.
        this.Surface.Effects = null;
        this.NearestDepth = new NearestDepthSpace(Exchange, Shaders);
        await this.NearestDepth.ConstructPrograms();
        this.Resampling = new ResamplingSequence(Exchange, Shaders);
        await this.Resampling.ConstructPrograms(this.FrameRecords, Host.MaterialRecords, Host.Distance.TraversalLayout, Host.Cards.SamplingLayout,
                                                this.NearestDepth.TraversalLayout);
        this.Diffuse = new VarianceIntegrator(Exchange, Shaders, "Diffuse", { MinimumWeight: 0.08, TallyLimit: 24, WaveletIterations: 3, ColourSigma: 4.0 });
        await this.Diffuse.ConstructPrograms(this.FrameRecords);
        this.Glossy = new VarianceIntegrator(Exchange, Shaders, "Glossy", { MinimumWeight: 0.12, TallyLimit: 12, WaveletIterations: 2, ColourSigma: 3.0 });
        await this.Glossy.ConstructPrograms(this.FrameRecords);
        this.Display = new DisplayProjection(Exchange, Shaders);
        await this.Display.ConstructPrograms(this.FrameRecords, Host.MaterialRecords, Host.Distance.TraversalLayout, Host.Cards.SamplingLayout,
                                             Host.OccupantRecords, Host.RayTracing.TraversalLayout);
        await this.ConstructOverlayPrograms();
        this.ConstructScanResources();
    }

    async ConstructOverlayPrograms()
    {
        const Exchange = this.Exchange, Shaders = this.Host.Shaders;
        const Stages = GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT;
        // 📝 The surface raster's own six bindings first, so the ghost reads the occupants and the vehicle record the solid car does, then the scan, the
        //    wireframe and particle records, the HUD, and the picture's depth the layers are occluded by.
        this.OverlayLayout = Exchange.ConstructDescriptorLayout(
            ["Uniform", "StorageRead", "Texture2D", "Sampler", "Uniform", "StorageRead", "Uniform", "StorageRead", "StorageRead", "Texture2D", "Sampler", "TextureDepth2D"],
            Stages, "Scan overlay");
        const Facets = [{ arrayStride: VertexStride, attributes: [
            { shaderLocation: 0, offset: 0,  format: "float32x3" },
            { shaderLocation: 1, offset: 12, format: "uint32" },
            { shaderLocation: 2, offset: 16, format: "float32x3" },
            { shaderLocation: 3, offset: 28, format: "uint32" }] }];
        const Declare = (Vertex, Fragment, Blend, VertexLayouts = []) => Shaders.ConstructRasterProgram("ScanOverlay.wgsl", {
            VertexEntry: Vertex, FragmentEntry: Fragment, VertexLayouts,
            Primitive: { topology: "triangle-list", cullMode: "none" },
            Targets: [{ format: Exchange.CanvasFormat, blend: Blend }]
        }, [this.OverlayLayout]);
        const Programs = await Promise.all([
            Declare("GhostVertex", "GhostFragment", AdditiveBlend, Facets),
            Declare("EdgeVertex", "EdgeFragment", OverBlend),
            Declare("CurtainVertex", "CurtainFragment", AdditiveBlend),
            Declare("SparkleVertex", "SparkleFragment", OverBlend),
            Declare("VeilVertex", "TagsFragment", OverBlend),
            Declare("VeilVertex", "VignetteFragment", OverBlend),
            Declare("VeilVertex", "TintFragment", ScreenBlend),
            Declare("VeilVertex", "MatrixFragment", OverBlend),
            Declare("VeilVertex", "FrameFragment", OverBlend)]);
        [this.GhostProgram, this.EdgeProgram, this.CurtainProgram, this.SparkleProgram, this.TagsProgram, this.VignetteProgram, this.TintProgram,
         this.MatrixProgram, this.FrameProgram] = Programs;
        this.HudSampler = Exchange.ConstructSampler({ magFilter: "linear", minFilter: "linear" });
    }

    // 📝 The wireframe's segments and the particle cloud: built once, they do not change with the car's pose (the shaders move them with the car).
    ConstructScanResources()
    {
        const Exchange = this.Exchange;
        const Edges = ConstructScanEdges(this.Host.Scene);
        this.EdgeCount = Edges.Count;
        this.EdgeRecords = Exchange.ConstructRecordsFrom(Edges.Count > 0 ? Edges.Records : new Float32Array(8), GPUBufferUsage.STORAGE, "Scan edges");
        const Cloud = ConstructSparkleCloud();
        this.SparkleCount = Cloud.length / 4;
        this.SparkleRecords = Exchange.ConstructRecordsFrom(Cloud, GPUBufferUsage.STORAGE, "Scan particles");
    }

    // 📝 Sized by the stage, and by the host's trace scale as the studio's targets are.
    ConstructTargets()
    {
        const Host = this.Host, Exchange = this.Exchange;
        const [Width, Height] = this.Extent;
        const Scale = Host.Settings.TraceScale;
        this.TraceScaleUsed = Scale;
        this.TraceExtent = [Math.ceil(Width / Scale), Math.ceil(Height / Scale)];
        this.Surface.ConstructTargets(Width, Height, Host.Shadow, Host.Atmosphere);
        this.NearestDepth.ConstructTargets(this.Extent, this.Surface);
        this.Resampling.ConstructTargets(this.TraceExtent, this.Surface);
        this.Diffuse.ConstructTargets(this.TraceExtent, this.Extent, this.Surface, [this.Resampling.Views.Irradiance, this.Resampling.Views.Irradiance],
                                      this.Resampling.Views.Guides);
        this.Glossy.ConstructTargets(this.TraceExtent, this.Extent, this.Surface, [this.Resampling.Views.Reflection, this.Resampling.Views.Reflection],
                                     this.Resampling.Views.Guides);
        this.Display.ConstructTargets(this.Extent, this.Surface, this.Resampling, this.Diffuse, this.Glossy, Host.Cards);
        this.NearestDepth.ConstructTraversalGroups(this.Surface, this.Display, Host.RayTracing);

        const Format = Exchange.CanvasFormat;
        this.PreviewTexture?.destroy();
        this.PreviewTexture = Exchange.ConstructTexture({
            label: "Garage preview", size: [Width, Height], format: Format, viewFormats: [`${Format}-srgb`],
            usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_SRC });
        // 📝 Written through the sRGB view (the present stage hands over linear light), drawn over and sampled through the encoded one.
        this.PresentView = this.PreviewTexture.createView({ format: `${Format}-srgb` });
        this.OverlayView = this.PreviewTexture.createView({ format: Format });
        this.SampleView  = this.PreviewTexture.createView({ format: `${Format}-srgb` });
        this.HudTexture?.destroy();
        this.HudTexture = Exchange.ConstructTexture({
            label: "Scan HUD", size: [Width, Height * 2], format: "rgba8unorm",
            usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST | GPUTextureUsage.RENDER_ATTACHMENT });
        this.HudView = this.HudTexture.createView();
        this.Hud.Resize(Width, Height);
        this.OverlayGroup = Exchange.ConstructDescriptorGroup(this.OverlayLayout,
            [this.FrameRecords, Host.OccupantRecords, Host.Marks.View, Host.Marks.Sampler, Host.Effects.Records, Host.VehicleRecords,
             this.ScanRecords, this.EdgeRecords, this.SparkleRecords, this.HudView, this.HudSampler, this.Surface.Views.Depth]);
        this.Restart = true;
        this.Host.Cabin?.AssignPreview(this.SampleView, this);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                    THE STAGE'S INPUT
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 The tablet's drag, in the stage's own pixels; OrbitControls turns 2π per stage height.
    Rotate(PixelsAcross, PixelsDown)
    {
        this.Orbit.Rotate(PixelsAcross, PixelsDown);
    }

    Zoom(DeltaY)
    {
        this.Orbit.Zoom(DeltaY);
    }

    get Spin()
    {
        return this.Orbit.Spin;
    }

    set Spin(Value)
    {
        this.Orbit.Spin = Value;
    }

    // 📝 The scan toggle and the X key: a switch, as the store's scan flag is.
    AssignScan(Enabled)
    {
        if (Enabled && !this.Scan.Enabled)
        {
            this.HudClock = 0.0;
        }
        this.Scan.Assign(Enabled);
    }

    // 📝 A stage point (U, V ∈ [0, 1], V down) against the car the last picture showed: the part under it, with the world point hit.
    Pick(U, V)
    {
        return this.PickInverse ? PickVehicle(this.Host.Scene, this.PickInverse, U, V) : null;
    }

    // 📝 Truck.tsx's apply request: a paint ripple begins where the drop's ray meets the car (U, V: the drop on the stage), else at the nose —
    //    (2.75, 1.25, 0) of the reference's world, whose ground lies 0.62 m below its origin, so 1.87 m above the car's own.
    ResolveDropCentre(U = null, V = null)
    {
        const Hit = U === null || V === null ? null : this.Pick(U, V);
        if (Hit)
        {
            return Hit.Point;
        }
        const Frame = this.Stage || ResolveVehicleFrame(this.Host.Vehicle.QueryBodyTransform(), VehicleGeometry.CoMHeight);
        return AddVector(AddVector(Frame.Origin, ScaleVector(Frame.Forward, ReferenceNose[0])), ScaleVector(Frame.Up, ReferenceNose[1] + ReferenceGround));
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                       FRAME
    //--------------------------------------------------------------------------------------------------------------------

    Advance(Seconds)
    {
        this.Orbit.Advance(Seconds);
        this.Scan.Advance(Seconds);
        this.HudClock += Seconds;
        if (this.Surface && this.TraceScaleUsed !== this.Host.Settings.TraceScale)
        {
            this.ConstructTargets();
        }
    }

    ResolveActive()
    {
        const Cabin = this.Host.Cabin;
        return this.Forced || Boolean(Cabin?.Screen && Cabin.Settings.Screens && Cabin.Screen.Application === "garage");
    }

    // 📝 The stage's frame in the world: the camera, the car's own axes and the ground it stands on.
    ResolveStage()
    {
        const Host = this.Host;
        const Frame = ResolveVehicleFrame(Host.Vehicle.QueryBodyTransform(), VehicleGeometry.CoMHeight);
        const Camera = this.Orbit.ResolveCamera(Frame, this.Extent[0] / this.Extent[1]);
        if (Host.Lobby)
        {
            Camera.Position = ClampStudioEye(Camera.Position);
        }
        return { Frame, Camera };
    }

    // 📝 Records this stage's picture into the preview texture. Returns false when it is not showing.
    Record(Encoder, Light)
    {
        const Host = this.Host;
        const Active = this.ResolveActive();
        if (!Active)
        {
            this.Active = false;
            return false;
        }
        if (!this.Active)
        {
            // 📝 Opened again: the history belongs to a picture that is gone. Exposure starts where the studio's has settled.
            this.Restart = true;
            Encoder.copyBufferToBuffer(Host.Display.AdaptationRecords, 0, this.Display.AdaptationRecords, 0, 16);
        }
        this.Active = true;
        const { Frame, Camera } = this.ResolveStage();
        const Scan = this.Scan;
        const Plane = ResolveScanPlane(Frame, Scan.Front);
        const Composed = Host.ComposeViewRecord({
            Main: false, ScreenExtent: this.Extent, TraceExtent: this.TraceExtent, TraceScaleUsed: this.TraceScaleUsed, Tick: this.Tick,
            PreviousUnjittered: this.PreviousUnjittered, Restart: this.Restart,
            Hidden: this.HiddenMask, Scanned: this.ScannedMask, ScanPlane: Plane, ScanState: [Scan.Amount, 0.0, 0.0, 0.0]
        }, Camera, Light);
        this.PreviousUnjittered = Composed.Unjittered;
        // 📝 Picks use the unjittered projection, the one the picture's geometry is exact in.
        this.PickInverse = InvertTransform(Composed.Unjittered);
        this.Camera = Camera;
        this.Stage = Frame;
        Host.Exchange.Device.queue.writeBuffer(this.FrameRecords, 0, Composed.Bytes);
        this.EncodeScanRecord(Frame, Composed.Unjittered);

        const Parity = this.Tick & 1;
        const Settings = Host.Settings;
        const Scene = Host.Scene;
        this.Surface.RecordSurface(Encoder, Scene, Host.VertexRecords, undefined, SelectDrawRuns(Scene.DrawRuns, (Index) => !this.HiddenOccupants.has(Index)));
        this.Surface.RecordDirect(Encoder, Host.Distance.TraversalGroup, Host.SamplingGroup, Host.RayTracing.TraversalGroup);
        const TraceGroup = this.NearestDepth.TraversalGroups[Parity];
        if (Settings.Indirect)
        {
            this.Resampling.RecordResampling(Encoder, Parity, Host.Distance.TraversalGroup, Host.SamplingGroup, TraceGroup);
            this.Diffuse.Record(Encoder, Parity, Settings.VarianceKernel);
        }
        if (Settings.Reflection)
        {
            this.Resampling.RecordReflection(Encoder, Host.Distance.TraversalGroup, Host.SamplingGroup, TraceGroup);
            this.Glossy.Record(Encoder, Parity, Settings.VarianceKernel);
        }
        // 📝 The car's glazing is the cabin overlay's: its glass draws over the radiance before the antialiasing, here with this view's record.
        const Glass = (Target, View) => Host.Cabin.RecordGlass(Target, View, this.Surface.Views.Depth, this.FrameRecords, this.Display.AdaptationRecords);
        this.Display.RecordResolve(Encoder, Parity, undefined, null, Glass, null);
        this.Display.RecordPresent(Encoder, Parity, this.PresentView, Host.Distance.TraversalGroup);
        this.RecordOverlay(Encoder, Scene, Frame, Composed.Unjittered);
        this.Restart = false;
        this.Tick++;
        this.Metrics.Recorded++;
        // 📝 What this picture was drawn with, for the evidence's captions: the ripple's clock and radius, the scan's clock and front.
        const Ripple = Host.Cabin.Appearance.Ripple;
        this.Metrics.Ripple = { Elapsed: Ripple.Active ? Ripple.Elapsed : 0.0, Radius: Ripple.Active ? Ripple.Reached : 0.0 };
        this.Metrics.ScanDrawn = { Seconds: Scan.SweepSeconds, Front: Scan.Front, Amount: Scan.Amount };
        return true;
    }

    // 📝 ScanRecord (ScanOverlay.wgsl): the reference's uniforms for the layers, in the car's frame.
    EncodeScanRecord(Frame, Unjittered)
    {
        const Specification = ScanSpecification;
        const Scan = this.Scan;
        const Curtain = Scan.ResolveCurtain();
        // 📝 The truck group bobs about its ground by 12 mm at 0.8 rad/s; its world-height lines ride with it.
        const Bob = 0.012 * Math.sin(0.8 * Scan.Clock);
        const Record = new Float32Array(ScanRecordExtent / 4);
        Record.set([...ConvertHexToLinear(Specification.GhostColour), 1.0], 0);
        Record.set([...ConvertHexToLinear(Specification.Accent), 1.0], 4);
        Record.set([...ResolveDisplayColour(Specification.EdgeColour), Scan.ResolveEdgeOpacity()], 8);
        Record.set([...ResolveDisplayColour(Specification.Accent), 1.0], 12);
        Record.set([Curtain[0], Curtain[1], 0.0, 0.0], 16);
        Record.set([...ResolveDisplayColour(Specification.ParticleColour), Specification.ParticleOpacity], 20);
        Record.set([...Frame.Origin, -0.62 + Bob], 24);
        Record.set([...Frame.Forward, 0.0], 28);
        Record.set([...Frame.Right, 0.0], 32);
        Record.set([...Frame.Up, 0.0], 36);
        Record.set([Scan.Front, Scan.Clock, 1.0, ResolveScanLineTop(this.HudClock)], 40);
        Record.set([Specification.ParticleSize, Specification.ParticleSpeed, this.SparkleCount, Scan.Enabled ? 1.0 : 0.0], 44);
        this.Exchange.Device.queue.writeBuffer(this.ScanRecords, 0, Record);
        this.ScanRecordValues = Record;
    }

    // 📝 Draws the HUD canvas when the scan is on: the tags at their anchors' projection, the frame and captions.
    UpdateHud(Unjittered)
    {
        if (!this.Scan.Enabled || !this.Hud.Capable)
        {
            return false;
        }
        const [Width, Height] = this.Extent;
        const Frame = this.Stage;
        const Projected = ScanTags.map((Tag) =>
        {
            const World = AddVector(AddVector(AddVector(Frame.Origin, ScaleVector(Frame.Forward, Tag.Anchor[0])), ScaleVector(Frame.Right, Tag.Anchor[1])),
                                    ScaleVector(Frame.Up, Tag.Anchor[2]));
            return ProjectToStage(Unjittered, World, Width, Height);
        });
        if (!this.Hud.Draw(this.HudClock, Projected))
        {
            return false;
        }
        const Device = this.Exchange.Device;
        const Canvas = this.Hud.Canvas;
        const Browser = (typeof HTMLCanvasElement !== "undefined" && Canvas instanceof HTMLCanvasElement)
                     || (typeof OffscreenCanvas !== "undefined" && Canvas instanceof OffscreenCanvas);
        if (Browser)
        {
            Device.queue.copyExternalImageToTexture({ source: Canvas }, { texture: this.HudTexture }, [Width, Height * 2]);
        }
        else
        {
            const Pixels = this.Hud.Context.getImageData(0, 0, Width, Height * 2);
            Device.queue.writeTexture({ texture: this.HudTexture }, Pixels.data, { bytesPerRow: Width * 4 }, [Width, Height * 2]);
        }
        this.Metrics.HudUploads++;
        return true;
    }

    // 📝 Over the tone-mapped picture, in the order the page stacks them: ghost, wireframe, curtain, particles, tags, vignette, then the scan HUD.
    RecordOverlay(Encoder, Scene, Frame, Unjittered)
    {
        const Scan = this.Scan;
        const Showing = Scan.Visible;
        const HudDrawn = this.UpdateHud(Unjittered);
        const Pass = Encoder.beginRenderPass({
            label: "Scan overlay",
            colorAttachments: [{ view: this.OverlayView, loadOp: "load", storeOp: "store" }]
        });
        Pass.setBindGroup(0, this.OverlayGroup);
        if (Showing)
        {
            if (Scan.Amount > 0.0)
            {
                Pass.setPipeline(this.GhostProgram);
                Pass.setVertexBuffer(0, this.Host.VertexRecords);
                for (const Run of SelectDrawRuns(Scene.DrawRuns, (Index) => this.ScannedOccupants.has(Index)))
                {
                    const Set = Scene.FacetSets[Run.FacetSet];
                    Pass.draw(Set.VertexCount, Run.Count, Set.FirstVertex, Run.First);
                    this.Metrics.Ghost++;
                }
                if (this.EdgeCount > 0)
                {
                    Pass.setPipeline(this.EdgeProgram);
                    Pass.draw(6, this.EdgeCount);
                }
            }
            if (Scan.Ramp > 0.0)
            {
                Pass.setPipeline(this.CurtainProgram);
                Pass.draw(6, 2);
            }
        }
        if (Scan.Enabled)
        {
            Pass.setPipeline(this.SparkleProgram);
            Pass.draw(6, this.SparkleCount);
            if (HudDrawn)
            {
                Pass.setPipeline(this.TagsProgram);
                Pass.draw(3);
            }
        }
        Pass.setPipeline(this.VignetteProgram);
        Pass.draw(3);
        if (Scan.Enabled)
        {
            Pass.setPipeline(this.TintProgram);
            Pass.draw(3);
            Pass.setPipeline(this.MatrixProgram);
            Pass.draw(3);
            if (HudDrawn)
            {
                Pass.setPipeline(this.FrameProgram);
                Pass.draw(3);
            }
        }
        Pass.end();
    }
}
