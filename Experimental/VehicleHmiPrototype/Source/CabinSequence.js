import { PickVehicle } from "./VehiclePicking.js";
import { StudioPreviewSequence, ResolveStudioExtent } from "./StudioPreviewSequence.js";
//============================================================================================================================================
//                                                             CABINSEQUENCE.JS
//============================================================================================================================================
// 🧩 The cabin's live surfaces: the TabletPanel HMI and cluster as textures, the mirrors' reflected views, the Garage's car, the glass.

// 📝 The HMI is the accepted TabletPanel prototype (Experimental/TabletPanel, arena/215a6f39-frontier), copied as is
//    into Source/Cabin: its launcher, twelve applications and five launcher designs draw into one 2D canvas (1200 × 750
//    logical at 1.6×, 16:10), the cluster into another (the hexagon outline, 30 Hz). Here those canvases become GPU
//    textures (copyExternalImageToTexture, only when they have redrawn) and the cabin overlay draws them on the
//    centre-stack tablet and the column binnacle. A click on the tablet is a ray from the camera to the glass, turned
//    into the panel's own coordinates and handed to its hit regions, exactly as the prototype's touch did.
//
//    Live data replaces the prototype's demonstration values: the cluster shows the car's speed, revs, gear and tyre
//    pressures, the Telemetry and Tyres applications the solver's readings, and the Tyres slider pumps the tyres.
//
//    The Garage application shows the car inside the Lobby photography studio, rendered by StudioPreviewSequence's
//    two-level BVH ReSTIR GI + multi-bounce ray-traced reflection pipeline into PreviewTexture (no read-back, live
//    paint & wheel waves, popped tyres, and X-ray/Wireframe/Ghost inspection).
//
//    Mirrors: the driver's eye reflected in each mirror's plane, through an off-axis frustum fitted to its glass
//    (Kooima's generalised perspective projection), its near plane on the glass. One mirror is re-rendered per frame
//    while the mirrors can be seen (an interior camera, or the chase camera within 14 m), so each refreshes at 20 Hz.

import { CabinLayout, ClusterHexagon, ExtractGlassFacets, ResolveCockpitFrame, PlaceSeatedEye, SeatHipPoints, ResolveTabletScale } from "./CabinStructure.js";
import { CourseMaterial } from "./MaterialSpecification.js";
import { VertexStride } from "./FacetStructure.js";
import { TransformPoint, TransformDirection, MultiplyTransform, ConstructViewTransform, SubtractVector, AddVector, ScaleVector,
         DotVector, CrossVector, NormalizeVector, LengthVector, InvertTransform } from "./TransformProjection.js";

const OverlayFloats  = 10;                  // position xyz, kind (u32), normal xyz, layer, uv
const ViewStride     = 256;                 // [B] dynamic-offset alignment
const MirrorExtent   = [352, 192];          // [px]
const PreviewExtent  = [960, 600];          // [px] default high-grade 16:10 ReSTIR GI studio viewport
const TabletPixels   = [1920, 1200];        // [px] 1200 × 750 at 1.6×
const ClusterPixels  = [1500, 838];         // [px] the hexagon's canvas: 1000 × 559 logical at 1.5× (ClusterSequence)
const KindGlass = 0, KindTablet = 1, KindCluster = 2, KindMirror = 3;

export const CabinSettingsDefaults = Object.freeze({
    SeeThroughGlass: true,     // glazing see-through (cabin visible from outside, the world from inside)
    Screens:         true,     // HMI tablet and cluster live
    Mirrors:         1,        // 0 off, 1 when visible, 2 always
    Brightness:      1.0,      // [-] screen brightness
    GlassTransmission: 0.72    // [-] at normal incidence
});

// Keep descriptor count and WGSL binding 12 (display exposure) on one explicit schema.
export const CabinOverlayBindings = Object.freeze(["Uniform", "StorageRead", "Uniform", "Uniform", "StorageRead",
    "Texture2D", "Texture2D", "Texture2D", "Texture2DLayered", "Sampler", "Texture2D", "Sampler", "StorageRead"]);

export class CabinSequence
{
    constructor(Exchange, Shaders)
    {
        this.Exchange = Exchange;
        this.Shaders  = Shaders;
        this.Settings = { ...CabinSettingsDefaults };
        this.Layout   = CabinLayout;
        this.Cockpit  = ResolveCockpitFrame();   // the cockpit's scale and offset (the host refreshes it every frame)
        this.Clock    = 0.0;
        this.MirrorCursor = 0;
        this.MirrorsRendered = 0;
        this.Preview  = { External: true, Loaded: true, Facets: 0, Spin: false, Dirty: true, Yaw: -2.45, Pitch: 0.17,
                          Orbit: (Yaw, Pitch) => { this.Preview.Yaw += Yaw; this.Preview.Pitch = Math.max(0.02, Math.min(0.75, this.Preview.Pitch + Pitch)); } };
        this.Metrics  = { TabletUploads: 0, ClusterUploads: 0, MirrorPasses: 0, PreviewPasses: 0, Hmi: "starting" };
        this.LiveRevision = -1;
        this.PumpedPressure = null;
        this.MaterialCommitted = false;
        this.RequestedApplication = null;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                  BRING-UP
    //--------------------------------------------------------------------------------------------------------------------

    async ConstructResources({
        FrameRecords, OccupantRecords, MaterialRecords, Materials, EffectRecords, VehicleRecords, Shadow, Atmosphere, Scene,
        VertexRecords = null, AdaptationRecords, Lobby = false, GradeName = "high", Distance = null, Cards = null,
        RayTracing = null, SamplingGroup = null, TyreMarks = null, Vehicle = null, PhotometryAtlas = null
    })
    {
        const Exchange = this.Exchange;
        const Device = Exchange.Device;
        this.Scene = Scene;
        this.Vehicle = Vehicle;
        this.PreviewExtent = ResolveStudioExtent(GradeName);
        this.Preview.Pick = (U, V) => PickVehicle(this.Studio ? this.Studio.Scene : Scene, this.PreviewInverse, U, V);
        this.Preview.Facets = Scene.FacetSets[Scene.VehicleBodySet].Facets.length
                            + 4 * Scene.FacetSets[Scene.Occupants[Scene.VehicleOccupants.Wheels[0]].FacetSet].Facets.length;

        // ① overlay geometry, car-body frame
        const Vertices = [];
        const Push = (Position, Kind, Normal, Layer, U, V) => Vertices.push(...Position, Kind, ...Normal, Layer, U, V);
        for (const Facet of ExtractGlassFacets(Scene.FacetSets[Scene.VehicleBodySet].Facets))
        {
            const [A, B, C] = Facet.Corners;
            const Normal = NormalizeVector(CrossVector(SubtractVector(B, A), SubtractVector(C, A)));
            for (const Corner of Facet.Corners)
                Push(Corner, KindGlass, Normal, 0, 0, 0);
        }
        this.GlassVertexCount = Vertices.length / OverlayFloats;
        const Rectangle = (Fixture, Kind, Layer, Flip) =>
        {
            const Corner = (X, Y) => AddVector(AddVector(Fixture.Centre, ScaleVector(Fixture.Right, X * Fixture.Width * 0.5)), ScaleVector(Fixture.Up, Y * Fixture.Height * 0.5));
            const Coordinate = (X, Y) => [Flip ? 0.5 - 0.5 * X : 0.5 + 0.5 * X, 0.5 - 0.5 * Y];
            for (const [X, Y] of [[-1, -1], [1, -1], [1, 1], [-1, -1], [1, 1], [-1, 1]])
                Push(Corner(X, Y), Kind, Fixture.Normal, Layer, ...Coordinate(X, Y));
        };
        Rectangle(this.Layout.Tablet, KindTablet, 0, false);
        const Cluster = this.Layout.Cluster;
        const Hexagon = ClusterHexagon.map(([X, Y]) => ({ X, Y, Position: AddVector(AddVector(Cluster.Centre, ScaleVector(Cluster.Right, X * Cluster.Width * 0.5)),
                                                                                    ScaleVector(Cluster.Up, Y * Cluster.Height * 0.5)) }));
        for (let Index = 1; Index < 5; ++Index)
            for (const Point of [Hexagon[0], Hexagon[Index], Hexagon[Index + 1]])
                Push(Point.Position, KindCluster, Cluster.Normal, 0, 0.5 + 0.5 * Point.X, 0.5 - 0.5 * Point.Y);
        this.Layout.Mirrors.forEach((Mirror, Index) => Rectangle(Mirror, KindMirror, Index, true));
        const Bytes = new ArrayBuffer(Vertices.length * 4);
        const Scalars = new Float32Array(Bytes), Integers = new Uint32Array(Bytes);
        Vertices.forEach((Value, Index) => { if (Index % OverlayFloats === 3) Integers[Index] = Value; else Scalars[Index] = Value; });
        this.OverlayVertexCount = Vertices.length / OverlayFloats;
        this.OverlayRecords = Exchange.ConstructRecordsFrom(Scalars, GPUBufferUsage.VERTEX, "Cabin overlay vertices");
        this.OverlayUniform = Exchange.ConstructRecords(80, GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, "Cabin overlay record");

        // ② textures: the two canvases, the ReSTIR GI studio preview, the mirrors
        const Texture = (Label, Size, Format, Usage, Layers = 1) => Exchange.ConstructTexture({ label: Label, size: [Size[0], Size[1], Layers], format: Format, usage: Usage });
        const CanvasUsage = GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST | GPUTextureUsage.RENDER_ATTACHMENT;
        this.TabletTexture  = Texture("Cabin tablet", TabletPixels, "rgba8unorm-srgb", CanvasUsage);
        this.ClusterTexture = Texture("Cabin cluster", ClusterPixels, "rgba8unorm-srgb", CanvasUsage);
        this.PreviewTexture = Texture("Garage preview", this.PreviewExtent, "rgba8unorm-srgb",
                                      GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC);
        this.MirrorTexture  = Texture("Mirror views", MirrorExtent, "rgba16float", GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.RENDER_ATTACHMENT, 3);
        this.MirrorDepth    = Texture("Mirror depth", MirrorExtent, "depth32float", GPUTextureUsage.RENDER_ATTACHMENT);
        this.MirrorLayers   = [0, 1, 2].map((Layer) => this.MirrorTexture.createView({ dimension: "2d", baseArrayLayer: Layer, arrayLayerCount: 1 }));
        this.LinearSampler  = Exchange.ConstructSampler({ magFilter: "linear", minFilter: "linear", mipmapFilter: "linear" });
        this.ClearCanvasTextures();

        // ③ the overlay pass
        const Stages = GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT;
        this.OverlayLayout = Exchange.ConstructDescriptorLayout(CabinOverlayBindings, Stages, "Cabin overlay");
        const TabletView      = this.TabletTexture.createView();
        const ClusterView     = this.ClusterTexture.createView();
        const MirrorArrayView = this.MirrorTexture.createView({ dimension: "2d-array" });
        this.OverlayGroup = Exchange.ConstructDescriptorGroup(this.OverlayLayout,
            [FrameRecords, OccupantRecords, this.OverlayUniform, EffectRecords, VehicleRecords, TabletView, ClusterView,
             this.PreviewTexture.createView(), MirrorArrayView, this.LinearSampler, Atmosphere.SkyView, Atmosphere.SkySampler, AdaptationRecords]);
        this.OverlayProgram = await this.Shaders.ConstructRasterProgram("CabinOverlay.wgsl", {
            VertexEntry: "OverlayVertex", FragmentEntry: "OverlayFragment",
            VertexLayouts: [{ arrayStride: OverlayFloats * 4, attributes: [
                { shaderLocation: 0, offset: 0,  format: "float32x3" },
                { shaderLocation: 1, offset: 12, format: "uint32" },
                { shaderLocation: 2, offset: 16, format: "float32x3" },
                { shaderLocation: 3, offset: 28, format: "float32" },
                { shaderLocation: 4, offset: 32, format: "float32x2" }] }],
            Primitive: { topology: "triangle-list", cullMode: "none" },
            DepthStencil: { format: "depth32float", depthWriteEnabled: false, depthCompare: "greater" },
            Targets: [{ format: "rgba16float", blend: { color: { srcFactor: "one", dstFactor: "one-minus-src-alpha", operation: "add" },
                                                        alpha: { srcFactor: "zero", dstFactor: "one", operation: "add" } } }]
        }, [this.OverlayLayout]);

        // ③b the ReSTIR GI + ray-traced reflection Lobby studio pipeline for the Garage viewport
        if (Distance && Cards)
        {
            this.Studio = new StudioPreviewSequence(Exchange, this.Shaders);
            await this.Studio.ConstructResources({
                Lobby, GradeName, Scene, VertexRecords, OccupantRecords, MaterialRecords, Materials, EffectRecords,
                VehicleRecords, Shadow, Atmosphere, Distance, Cards, RayTracing, SamplingGroup, TyreMarks, PhotometryAtlas,
                OverlayLayout: this.OverlayLayout, OverlayRecords: this.OverlayRecords, GlassVertexCount: this.GlassVertexCount,
                TabletView, ClusterView, MirrorArrayView, LinearSampler: this.LinearSampler
            });
        }

        // ④ the forward views: mirrors (radiance) and the Garage X-ray / Wireframe / Ghost overlay over ReSTIR GI
        this.ViewRecords = Exchange.ConstructRecords(ViewStride * 4, GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, "Cabin views");
        this.ForwardLayout = Exchange.ConstructDescriptorLayout(["Uniform", "UniformDynamic", "StorageRead", "StorageRead", "StorageRead", "Uniform",
                                                                 "TextureDepth2DLayered", "SamplerComparison", "Uniform", "Texture2D", "Sampler"], Stages, "Forward views");
        this.ForwardGroup = Exchange.ConstructDescriptorGroup(this.ForwardLayout,
            [FrameRecords, { buffer: this.ViewRecords, offset: 0, size: 224 }, OccupantRecords, MaterialRecords, VehicleRecords, EffectRecords,
             Shadow.LayeredView, Shadow.ComparisonSampler, Shadow.ReceiverRecords, Atmosphere.SkyView, Atmosphere.SkySampler]);
        if (this.Studio)
        {
            this.StudioForwardGroup = Exchange.ConstructDescriptorGroup(this.ForwardLayout,
                [this.Studio.FrameRecords, { buffer: this.ViewRecords, offset: 0, size: 224 }, this.Studio.OccupantRecords, MaterialRecords,
                 VehicleRecords, this.Studio.ZeroEffects, Shadow.LayeredView, Shadow.ComparisonSampler, this.Studio.ZeroCascades,
                 this.Studio.Atmosphere.SkyView, this.Studio.Atmosphere.SkySampler]);
        }
        const PremultipliedBlend = {
            color: { srcFactor: "one", dstFactor: "one-minus-src-alpha", operation: "add" },
            alpha: { srcFactor: "one", dstFactor: "one-minus-src-alpha", operation: "add" }
        };
        const ForwardVertexLayouts = [{ arrayStride: VertexStride, attributes: [
            { shaderLocation: 0, offset: 0,  format: "float32x3" },
            { shaderLocation: 1, offset: 12, format: "uint32" },
            { shaderLocation: 2, offset: 16, format: "float32x3" },
            { shaderLocation: 3, offset: 28, format: "uint32" }] }];
        const ForwardDescriptor = (Format, Blend = undefined) => ({
            VertexEntry: "ForwardVertex", FragmentEntry: "ForwardFragment",
            VertexLayouts: ForwardVertexLayouts,
            Primitive: { topology: "triangle-list", cullMode: "none" },
            DepthStencil: { format: "depth32float", depthWriteEnabled: true, depthCompare: "less" },
            Targets: [Blend ? { format: Format, blend: Blend } : { format: Format }]
        });
        this.MirrorProgram = await this.Shaders.ConstructRasterProgram("ForwardRaster.wgsl", ForwardDescriptor("rgba16float"), [this.ForwardLayout]);
        this.PreviewXrayProgram = await this.Shaders.ConstructRasterProgram("ForwardRaster.wgsl", {
            VertexEntry: "ForwardVertex", FragmentEntry: "ForwardXrayFragment",
            VertexLayouts: ForwardVertexLayouts,
            Primitive: { topology: "triangle-list", cullMode: "none" },
            DepthStencil: { format: "depth32float", depthWriteEnabled: false, depthCompare: "greater-equal" },
            Targets: [{ format: "rgba8unorm-srgb", blend: PremultipliedBlend }]
        }, [this.ForwardLayout]);

        // ⑤ the HMI itself, when this host can draw 2D canvases (the headless harness cannot)
        this.ConstructHmi().catch((Failure) => { this.Metrics.Hmi = `unavailable: ${Failure && Failure.message ? Failure.message : Failure}`; });
    }

    ClearCanvasTextures()
    {
        const Device = this.Exchange.Device;
        const Encoder = Device.createCommandEncoder({ label: "Cabin clear" });
        for (const [Texture, Colour] of [[this.TabletTexture, [0.004, 0.006, 0.007, 1]], [this.ClusterTexture, [0, 0, 0, 0]], [this.PreviewTexture, [0, 0, 0, 0]]])
        {
            const Pass = Encoder.beginRenderPass({ colorAttachments: [{ view: Texture.createView(), clearValue: Colour, loadOp: "clear", storeOp: "store" }] });
            Pass.end();
        }
        Device.queue.submit([Encoder.finish()]);
    }

    async ConstructHmi()
    {
        if (typeof document === "undefined" || typeof document.createElement !== "function" || typeof Image === "undefined")
            throw new Error("no 2D canvas in this host");
        const TabletCanvas = document.createElement("canvas");
        const ClusterCanvas = document.createElement("canvas");
        if (!TabletCanvas.getContext || !TabletCanvas.getContext("2d"))
            throw new Error("no 2D canvas in this host");
        const [{ ScreenSequence }, { ClusterSequence }, { Applications }, { ConstructIconImages }] = await Promise.all([
            import("./Cabin/ScreenSequence.js"), import("./Cabin/ClusterSequence.js"), import("./Cabin/AppSpecification.js"), import("./Cabin/IconDepot.js")]);
        if (document.fonts && document.fonts.load)
            await Promise.all([document.fonts.load('300 20px "DM Sans"'), document.fonts.load('400 20px "DM Sans"')]).catch(() => null);
        const Icons = await ConstructIconImages(Applications);
        this.Screen = new ScreenSequence(TabletCanvas, Icons, () => { this.Screen.Dirty = true; });
        this.Screen.Preview = this.Preview;
        this.Screen.Appearance = this.Appearance;
        if (this.Appearance?.Colour)
            this.Screen.FinishColour = this.Appearance.Colour;
        this.Screen.ExternalBrightness = true;
        this.Screen.PresentAspect(this.Layout.Tablet.Width / this.Layout.Tablet.Height);
        if (this.RequestedApplication)
            this.Screen.PresentApplication(this.RequestedApplication);
        this.Cluster = new ClusterSequence(ClusterCanvas);
        this.Cluster.Demonstration = false;
        this.Metrics.Hmi = "live";
        if (this.PendingPressure !== undefined)
            this.AssignPressure(this.PendingPressure);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                    FRAME
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 Live: { Speed [km/h], Revolutions, Gear, Temperatures [°C ×4], Pressures [kPa ×4], Tread [×4], States [×4] }.
    //    Returns a pressure [kPa] when the Tyres application's slider asks the tyres to be pumped.
    Advance(Seconds, Live)
    {
        this.Clock += Seconds;
        if (this.Appearance?.Advance(Seconds))
        {
            this.MaterialCommitted = true;
            if (this.Screen && this.Screen.Application === "garage")
                this.Screen.Dirty = true;
        }
        if (this.Preview.Spin)
            this.Preview.Yaw += 0.35 * Seconds;
        if (!this.Screen || !this.Settings.Screens)
            return null;
        const Device = this.Exchange.Device;
        const Screen = this.Screen;
        Screen.AdvanceClock();
        // 📝 The live applications redraw at 5 Hz; the rest of the panel only when touched.
        const Revision = Math.floor(this.Clock * 5.0);
        if (Revision !== this.LiveRevision)
        {
            this.LiveRevision = Revision;
            Screen.Live = Live;
            if (Screen.Application === "telemetry" || Screen.Application === "tyres")
                Screen.Dirty = true;
        }
        if (Screen.Application === "garage")
            Screen.Dirty = Screen.Dirty || false;
        if (Screen.RasterScreen())
        {
            Device.queue.copyExternalImageToTexture({ source: Screen.Canvas }, { texture: this.TabletTexture },
                                                    [Math.min(Screen.Canvas.width, TabletPixels[0]), Math.min(Screen.Canvas.height, TabletPixels[1])]);
            this.Metrics.TabletUploads++;
        }
        const Cluster = this.Cluster;
        const Speed = Math.round(Math.abs(Live.Speed)), Revolutions = Math.round(Live.Revolutions / 25) * 25;
        const Tyres = Live.Pressures.map((Pressure, Index) => ({ Pressure, Warning: Pressure < 150 || !!Live.States[Index] }));
        const TyreKey = Tyres.map((Tyre) => `${Math.round(Tyre.Pressure)}${Tyre.Warning ? "!" : ""}`).join(",");
        if (Speed !== Cluster.Speed || Math.abs(Revolutions - Cluster.Revolutions) > 40 || Live.Gear !== Cluster.Gear || TyreKey !== this.TyreKey)
        {
            Object.assign(Cluster, { Speed, Revolutions, Gear: Live.Gear, Tyres, Dirty: true });
            this.TyreKey = TyreKey;
        }
        if (Cluster.RasterCluster(this.Clock))
        {
            Device.queue.copyExternalImageToTexture({ source: Cluster.Canvas }, { texture: this.ClusterTexture },
                                                    [Math.min(Cluster.Canvas.width, ClusterPixels[0]), Math.min(Cluster.Canvas.height, ClusterPixels[1])]);
            this.ClusterHeightFraction = Math.min(1.0, Cluster.Canvas.height / ClusterPixels[1]);
            this.ClusterWidthFraction = Math.min(1.0, Cluster.Canvas.width / ClusterPixels[0]);
            this.Metrics.ClusterUploads++;
        }
        // 📝 The Tyres slider: a pressure set on the panel pumps every intact tyre.
        const Asked = Screen.Values.pressure;
        if (this.PumpedPressure === null)
            this.PumpedPressure = Asked;
        if (Asked !== this.PumpedPressure && !Screen.Pointer)
        {
            this.PumpedPressure = Asked;
            return Asked;
        }
        return null;
    }

    // 📝 The host's own pressure edits show on the panel's slider.
    AssignPressure(Kilopascals)
    {
        this.PendingPressure = Kilopascals;
        if (!this.Screen)
            return;
        const Clamped = Math.max(140, Math.min(240, Math.round(Kilopascals)));
        this.Screen.Values.pressure = Clamped;
        this.PumpedPressure = Clamped;
        this.Screen.Dirty = true;
    }

    // 📝 Mirrors and the studio preview. BodyTransform: the car's local → world; Visible: whether mirrors can be seen.
    RecordViews(Encoder, Scene, VertexRecords, BodyTransform, MirrorsVisible)
    {
        if (!this.MirrorProgram)
            return;
        const Mode = this.Settings.Mirrors;
        const Render = Mode === 2 || (Mode === 1 && MirrorsVisible);
        if (Render)
        {
            // 📝 All three on the first frame they are needed, then one per frame.
            const Mirrors = this.MirrorsRendered === 0 ? [0, 1, 2] : [this.MirrorCursor];
            this.MirrorCursor = (this.MirrorCursor + 1) % 3;
            for (const Index of Mirrors)
                this.RecordMirror(Encoder, Scene, VertexRecords, BodyTransform, Index);
            this.MirrorsRendered++;
        }
        else
            this.MirrorsRendered = 0;
        if (this.Screen && this.Settings.Screens && this.Screen.Application === "garage")
            this.RecordPreview(Encoder, Scene, VertexRecords, BodyTransform);
    }

    WriteView(Slot, ViewProjection, Camera, Mode, Lights = null)
    {
        const Scalars = new Float32Array(56);
        Scalars.set(ViewProjection, 0);
        Scalars.set([...Camera, Mode], 16);
        if (Lights)
        {
            Scalars.set(Lights[0], 20);
            Scalars.set(Lights[1], 24);
            Scalars.set(Lights[2], 28);
        }
        const Appearance = this.Appearance;
        if (Appearance)
        {
            const Ripple = Appearance.Ripple;
            if (Ripple?.Active)
            {
                const Metric = Ripple.ModeCode === 1 ? Ripple.SweepFront : Ripple.Radius;
                Scalars.set([Ripple.Centre[0], Ripple.Centre[1], Ripple.Centre[2], Metric], 32);
                Scalars.set([Ripple.NextColour[0], Ripple.NextColour[1], Ripple.NextColour[2], Ripple.ModeCode], 36);
                Scalars.set([Ripple.Roughness, Ripple.Metalness, Ripple.CoatWeight, Appearance.WheelRipple?.Active ? 1.0 : 0.0], 40);
            }
            else if (Appearance.WheelRipple?.Active)
            {
                Scalars[43] = 1.0;
            }
            if (Appearance.WheelRipple?.Active)
            {
                const Wheel = Appearance.WheelRipple;
                Scalars.set([Wheel.NextColour[0], Wheel.NextColour[1], Wheel.NextColour[2], Wheel.Radius], 44);
                Scalars.set([Wheel.Centre[0], Wheel.Centre[1], Wheel.Centre[2], Wheel.Roughness], 48);
            }
            const Scan = Appearance.Scan;
            Scalars.set([Appearance.View || 0, Scan ? Scan.ScanX : 3.25, Scan ? Scan.HoloAmount : 0.0, this.Clock], 52);
        }
        this.Exchange.Device.queue.writeBuffer(this.ViewRecords, Slot * ViewStride, Scalars);
    }

    // 📐 Kooima's generalised perspective: the eye, the screen's lower-left, lower-right and upper-left corners; the
    //    near plane on the screen itself; WebGPU depth [0, 1], standard (not reversed).
    static ResolveOffAxis(Eye, LowerLeft, LowerRight, UpperLeft, Far)
    {
        const Right = NormalizeVector(SubtractVector(LowerRight, LowerLeft));
        const Up = NormalizeVector(SubtractVector(UpperLeft, LowerLeft));
        const Toward = NormalizeVector(CrossVector(Right, Up));
        const A = SubtractVector(LowerLeft, Eye), B = SubtractVector(LowerRight, Eye), C = SubtractVector(UpperLeft, Eye);
        const Distance = -DotVector(A, Toward);
        // 📝 The near plane a centimetre past the screen (the mirror's glass and housing stay behind it).
        const Near = Math.max(Distance + 0.01, 1.0e-3);
        const Left = DotVector(Right, A) * Near / Distance, RightEdge = DotVector(Right, B) * Near / Distance;
        const Bottom = DotVector(Up, A) * Near / Distance, Top = DotVector(Up, C) * Near / Distance;
        const Projection = new Float32Array([
            2.0 * Near / (RightEdge - Left), 0, 0, 0,
            0, 2.0 * Near / (Top - Bottom), 0, 0,
            (RightEdge + Left) / (RightEdge - Left), (Top + Bottom) / (Top - Bottom), Far / (Near - Far), -1,
            0, 0, Near * Far / (Near - Far), 0]);
        const View = new Float32Array([
            Right[0], Up[0], Toward[0], 0,
            Right[1], Up[1], Toward[1], 0,
            Right[2], Up[2], Toward[2], 0,
            -DotVector(Right, Eye), -DotVector(Up, Eye), -DotVector(Toward, Eye), 1]);
        return MultiplyTransform(Projection, View);
    }

    RecordMirror(Encoder, Scene, VertexRecords, BodyTransform, Index)
    {
        const Mirror = this.Layout.Mirrors[Index];
        const World = (Point) => TransformPoint(BodyTransform, Point);
        const Direction = (Vector) => NormalizeVector(TransformDirection(BodyTransform, Vector));
        const Centre = World(Mirror.Centre), Right = Direction(Mirror.Right), Up = Direction(Mirror.Up), Normal = Direction(Mirror.Normal);
        // 📝 The mirrors are the shell's; the eye is the seated driver's, so it rides with the seat as the cockpit is sized.
        const Eye = World(PlaceSeatedEye(this.Layout.DriverEye.Position, SeatHipPoints.Driver, this.Cockpit));
        // 📝 The driver's eye reflected in the glass's plane; the glass seen from there, mirrored (its right is −Right).
        const Virtual = SubtractVector(Eye, ScaleVector(Normal, 2.0 * DotVector(SubtractVector(Eye, Centre), Normal)));
        const W = 0.5 * Mirror.Width, H = 0.5 * Mirror.Height;
        const Corner = (X, Y) => AddVector(AddVector(Centre, ScaleVector(Right, X * W)), ScaleVector(Up, Y * H));
        const ViewProjection = CabinSequence.ResolveOffAxis(Virtual, Corner(1, -1), Corner(-1, -1), Corner(1, 1), 600.0);
        this.WriteView(Index, ViewProjection, Virtual, 0.0);
        const Pass = Encoder.beginRenderPass({
            label: `Mirror ${Index}`,
            colorAttachments: [{ view: this.MirrorLayers[Index], clearValue: [0.3, 0.4, 0.55, 1], loadOp: "clear", storeOp: "store" }],
            depthStencilAttachment: { view: this.MirrorDepth.createView(), depthClearValue: 1.0, depthLoadOp: "clear", depthStoreOp: "discard" }
        });
        Pass.setPipeline(this.MirrorProgram);
        Pass.setBindGroup(0, this.ForwardGroup, [Index * ViewStride]);
        Pass.setVertexBuffer(0, VertexRecords);
        for (const Run of Scene.DrawRuns)
        {
            const Set = Scene.FacetSets[Run.FacetSet];
            Pass.draw(Set.VertexCount, Run.Count, Set.FirstVertex, Run.First);
        }
        Pass.end();
        this.Metrics.MirrorPasses++;
    }

    // 📝 The Garage's studio: full BVH ReSTIR GI + ray-traced reflection Lobby studio pass + X-ray inspection overlay when active.
    RecordPreview(Encoder, Scene, VertexRecords, BodyTransform)
    {
        if (!this.Studio)
            return;
        const PreviewView = this.PreviewTexture.createView();
        const Frame = this.Studio.Record(Encoder, PreviewView, {
            HostScene: Scene, Vehicle: this.Vehicle, Yaw: this.Preview.Yaw, Pitch: this.Preview.Pitch,
            Appearance: this.Appearance, GlassTransmission: this.Settings.GlassTransmission,
            SeeThroughGlass: this.Settings.SeeThroughGlass, Clock: this.Clock
        });
        this.PreviewInverse = Frame.ForwardInverse;

        const HoloAmount = this.Appearance?.Scan?.HoloAmount ?? 0.0;
        if (this.PreviewXrayProgram && this.StudioForwardGroup && (HoloAmount > 0.004 || (this.Appearance?.View || 0) > 0))
        {
            this.WriteView(3, Frame.Unjittered, Frame.Eye, 1.0, [[0, 0, 1, 0], [0, 0, 1, 0], [0, 0, 1, 0]]);
            const StudioScene = this.Studio.Scene;
            const StudioVertices = this.Studio.OwnsScene ? this.Studio.VertexRecords : VertexRecords;
            const HeroPlate = StudioScene.PlateOccupants?.[0];
            const IsHeroVehicleRun = (Run) => Run.First < 5 || (HeroPlate !== undefined && Run.First === HeroPlate && Run.Count === 1);
            const XrayPass = Encoder.beginRenderPass({
                label: "Garage preview X-ray",
                colorAttachments: [{ view: PreviewView, loadOp: "load", storeOp: "store" }],
                depthStencilAttachment: { view: Frame.DepthView, depthReadOnly: true }
            });
            XrayPass.setPipeline(this.PreviewXrayProgram);
            XrayPass.setBindGroup(0, this.StudioForwardGroup, [3 * ViewStride]);
            XrayPass.setVertexBuffer(0, StudioVertices);
            for (const Run of StudioScene.DrawRuns)
            {
                if (!IsHeroVehicleRun(Run))
                    continue;
                const Set = StudioScene.FacetSets[Run.FacetSet];
                XrayPass.draw(Set.VertexCount, Run.Count, Set.FirstVertex, Run.First);
            }
            XrayPass.end();
        }
        this.Metrics.PreviewPasses++;
    }

    // 📝 Over the recombined radiance, before the antialiasing; the surface depth read only.
    RecordOverlay(Encoder, TargetView, DepthView)
    {
        if (!this.OverlayProgram)
            return;
        const Screen = this.Screen;
        const Garage = !!(Screen && this.Settings.Screens && Screen.Application === "garage");
        const PanelWidth = Screen ? Screen.PanelWidth : 1200, PanelHeight = Screen ? Screen.PanelHeight : 750;
        const OffsetAcross = Screen ? Screen.OffsetAcross : 0, OffsetDown = Screen ? Screen.OffsetDown : 0;
        const Well = [(78 + OffsetAcross) / PanelWidth, (248 + OffsetDown) / PanelHeight, (718 + OffsetAcross) / PanelWidth, (648 + OffsetDown) / PanelHeight];
        const Live = !!Screen && this.Settings.Screens;
        const Drag = Screen?.DragMaterial && Screen.Pointer;
        const PointerU = Drag ? (Drag.PreviousAcross + OffsetAcross) / PanelWidth : 0;
        const PointerV = Drag ? (Drag.PreviousDown + OffsetDown) / PanelHeight : 0;
        const Brightness = ResolveScreenBrightness(this.Settings.Brightness, Screen?.Values.brightness ?? 100, Live);
        const Appearance = this.Appearance;
        const Scan = Appearance?.Scan;
        const Record = new Float32Array([
            this.Layout.Tablet.Luminance, Brightness, Garage ? 1.0 : 0.0, Drag ? 1 : 0,
            ...Well,
            22.0 / PanelWidth, 22.0 / PanelHeight, Live ? this.Layout.Cluster.Luminance : 0.0, PointerU,
            this.Settings.GlassTransmission, 0.82, this.MirrorsRendered > 0 ? 1.0 : 0.0, PointerV,
            Appearance?.View || 0, Scan ? Scan.HoloAmount : 0.0, Scan ? Scan.ScanX : 3.25, this.Clock]);
        this.Exchange.Device.queue.writeBuffer(this.OverlayUniform, 0, Record);
        const Pass = Encoder.beginRenderPass({
            label: "Cabin overlay",
            colorAttachments: [{ view: TargetView, loadOp: "load", storeOp: "store" }],
            depthStencilAttachment: { view: DepthView, depthReadOnly: true }
        });
        Pass.setPipeline(this.OverlayProgram);
        Pass.setBindGroup(0, this.OverlayGroup);
        Pass.setVertexBuffer(0, this.OverlayRecords);
        Pass.draw(this.OverlayVertexCount);
        Pass.end();
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                    TOUCH
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 A world ray against the tablet's glass: the panel fraction (Across, Down ∈ [0, 1]) or null.
    QueryTabletHit(Origin, Direction, BodyTransform)
    {
        const Tablet = { ...this.Layout.Tablet, Width: this.Layout.Tablet.Width * ResolveTabletScale(this.TabletScale),
            Height: this.Layout.Tablet.Height * ResolveTabletScale(this.TabletScale) };
        const Centre = TransformPoint(BodyTransform, Tablet.Centre);
        const Normal = NormalizeVector(TransformDirection(BodyTransform, Tablet.Normal));
        const Right = NormalizeVector(TransformDirection(BodyTransform, Tablet.Right));
        const Up = NormalizeVector(TransformDirection(BodyTransform, Tablet.Up));
        const Facing = DotVector(Direction, Normal);
        if (Math.abs(Facing) < 1.0e-4)
            return null;
        const Travel = DotVector(SubtractVector(Centre, Origin), Normal) / Facing;
        if (Travel <= 0.0 || Travel > 4.0)
            return null;
        const Hit = AddVector(Origin, ScaleVector(Direction, Travel));
        const Local = SubtractVector(Hit, Centre);
        const Across = 0.5 + DotVector(Local, Right) / Tablet.Width, Down = 0.5 - DotVector(Local, Up) / Tablet.Height;
        return Across >= 0.0 && Across <= 1.0 && Down >= 0.0 && Down <= 1.0 ? { Across, Down } : null;
    }

    // 📝 Phase: "hover" | "down" | "move" | "up" | "cancel"; the fractions go through the panel's own mapping.
    ForwardPointer(Phase, Across, Down)
    {
        if (!this.Screen)
            return false;
        const Point = this.Screen.QueryPanelPoint(Across, Down);
        return this.Screen.ForwardPointer(Phase, Point.Across, Point.Down);
    }

    QueryReadout()
    {
        const Screen = this.Screen;
        return [`HMI ${this.Metrics.Hmi}${Screen ? ` · ${Screen.Design}${Screen.Application ? ` › ${Screen.Application}` : ""}` : ""}`
                + ` · tablet uploads ${this.Metrics.TabletUploads} · cluster ${this.Metrics.ClusterUploads}`,
                `mirror passes ${this.Metrics.MirrorPasses} · garage previews ${this.Metrics.PreviewPasses} · glass ${this.Settings.SeeThroughGlass ? "see-through" : "opaque"}`];
    }
}

export { KindGlass, KindTablet, KindCluster, KindMirror, CourseMaterial };

export function ResolveScreenBrightness(Inspector = 1, PanelPercent = 100, Enabled = true) {
    const Clamp = V => Number.isFinite(V) ? Math.max(0, Math.min(1,V)) : 1;
    return Enabled ? Clamp(Inspector) * Clamp(PanelPercent / 100) : 0;
}
