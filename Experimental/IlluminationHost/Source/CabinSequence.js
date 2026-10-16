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
//    The Garage application shows the car. The prototype rendered a three.js model into 640 × 400 and read it back;
//    here the exhibit's own facets are drawn by ForwardRaster.wgsl's studio mode into a texture the overlay composites
//    into the preview well (no read-back, the same car — popped tyres, derez and all), orbiting on its turntable.
//
//    Mirrors: the driver's eye reflected in each mirror's plane, through an off-axis frustum fitted to its glass
//    (Kooima's generalised perspective projection), its near plane on the glass. One mirror is re-rendered per frame
//    while the mirrors can be seen (an interior camera, or the chase camera within 14 m), so each refreshes at 20 Hz.

import { CabinLayout, ClusterHexagon, ExtractGlassFacets, ResolveCockpitFrame, PlaceSeatedEye, PlaceCockpitFixture, SeatHipPoints } from "./CabinStructure.js";
import { CourseMaterial, VehiclePaintPalette, VehicleWheelPalette, ResolveVehiclePaintMaterial, ResolveVehicleWheelMaterial } from "./MaterialSpecification.js";
import { VertexStride } from "./FacetStructure.js";
import { TransformPoint, TransformDirection, MultiplyTransform, ConstructViewTransform, SubtractVector, AddVector, ScaleVector,
         DotVector, CrossVector, NormalizeVector, LengthVector } from "./TransformProjection.js";

const OverlayFloats  = 10;                  // position xyz, kind (u32), normal xyz, layer, uv
const ViewStride     = 256;                 // [B] dynamic-offset alignment
const ForwardViewBytes = 224;               // [B] 56 floats in ForwardView
const OverlayBytes   = 128;                 // [B] 32 floats in OverlayRecord
const MirrorExtent   = [352, 192];          // [px]
const PreviewExtent  = [1024, 640];         // [px] the 640 × 400 well at the panel's 1.6×
const TabletPixels   = [1920, 1200];        // [px] 1200 × 750 at 1.6×
const ClusterPixels  = [1500, 838];         // [px] the hexagon's canvas: 1000 × 559 logical at 1.5× (ClusterSequence)
const KindGlass = 0, KindTablet = 1, KindCluster = 2, KindMirror = 3;

export const CabinSettingsDefaults = Object.freeze({
    SeeThroughGlass:          true,    // glazing see-through (cabin visible from outside, the world from inside)
    Screens:                  true,    // HMI tablet and cluster live
    Mirrors:                  1,       // 0 off, 1 when visible, 2 always
    Brightness:               1.0,     // [-] screen brightness
    GlassTransmission:        0.72,    // [-] at normal incidence
    ApplyScanToRealVehicle:   true,    // [-] X-Ray Scan / Ghost-Wireframe applies to the real 3D car as well as the HMI preview
    WireframeWeight:          0.78     // [-] facet wireframe edge strength in Ghost / X-Ray mode
});

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
        this.Preview  = { External: true, Loaded: true, Facets: 0, Spin: true, Dirty: true, Yaw: 0.6, Pitch: 0.2,
                          Orbit: (Yaw, Pitch) => { this.Preview.Yaw += Yaw; this.Preview.Pitch = Math.max(-0.1, Math.min(0.9, this.Preview.Pitch + Pitch)); } };
        this.Metrics  = { TabletUploads: 0, ClusterUploads: 0, MirrorPasses: 0, PreviewPasses: 0, Hmi: "starting" };
        this.LiveRevision = -1;
        this.PumpedPressure = null;

        // 📝 Interactive Vehicle HMI Configurator state (Paint, Finish, Wheels, X-Ray Scan / Ghost-Wireframe & 3D Ripple).
        this.Config = {
            PaintId:      "lime",
            Finish:       "gloss",
            WheelId:      "cobalt",
            Scan:         false,
            GhostHold:    false,
            Tab:          "paint"
        };
        this.CurrentPaintMaterial = ResolveVehiclePaintMaterial(this.Config.PaintId, this.Config.Finish);
        this.CurrentWheelMaterial = ResolveVehicleWheelMaterial(this.Config.WheelId);
        this.ScanX       = 3.2;
        this.HoloAmount  = 0.0;
        this.PaintRipple = { Active: false, Center: [2.65, 0.0, 0.25], Radius: 0.0,
                             PreviousAlbedo: [...this.CurrentPaintMaterial.Albedo],
                             PreviousSurface: [this.CurrentPaintMaterial.Roughness, this.CurrentPaintMaterial.Metalness,
                                               this.CurrentPaintMaterial.CoatWeight, this.CurrentPaintMaterial.CoatRoughness] };
        this.RimRipple   = { Active: false, Center: [1.73, 1.05, -0.10], Radius: 0.0,
                             PreviousAlbedo: [...this.CurrentWheelMaterial.Albedo] };
        this.OnMaterialChange = null;
        this.OnConfigChange   = null;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                  BRING-UP
    //--------------------------------------------------------------------------------------------------------------------

    async ConstructResources({ FrameRecords, OccupantRecords, MaterialRecords, EffectRecords, VehicleRecords, Shadow, Atmosphere, Scene })
    {
        const Exchange = this.Exchange;
        const Device = Exchange.Device;
        this.Scene = Scene;
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
        this.OverlayUniform = Exchange.ConstructRecords(OverlayBytes, GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, "Cabin overlay record");

        // ② textures: the two canvases, the preview, the mirrors
        const Texture = (Label, Size, Format, Usage, Layers = 1) => Exchange.ConstructTexture({ label: Label, size: [Size[0], Size[1], Layers], format: Format, usage: Usage });
        const CanvasUsage = GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST | GPUTextureUsage.RENDER_ATTACHMENT;
        this.TabletTexture  = Texture("Cabin tablet", TabletPixels, "rgba8unorm-srgb", CanvasUsage);
        this.ClusterTexture = Texture("Cabin cluster", ClusterPixels, "rgba8unorm-srgb", CanvasUsage);
        this.PreviewTexture = Texture("Garage preview", PreviewExtent, "rgba8unorm-srgb", GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.RENDER_ATTACHMENT);
        this.PreviewDepth   = Texture("Garage preview depth", PreviewExtent, "depth32float", GPUTextureUsage.RENDER_ATTACHMENT);
        this.MirrorTexture  = Texture("Mirror views", MirrorExtent, "rgba16float", GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.RENDER_ATTACHMENT, 3);
        this.MirrorDepth    = Texture("Mirror depth", MirrorExtent, "depth32float", GPUTextureUsage.RENDER_ATTACHMENT);
        this.MirrorLayers   = [0, 1, 2].map((Layer) => this.MirrorTexture.createView({ dimension: "2d", baseArrayLayer: Layer, arrayLayerCount: 1 }));
        this.LinearSampler  = Exchange.ConstructSampler({ magFilter: "linear", minFilter: "linear", mipmapFilter: "linear" });
        this.ClearCanvasTextures();

        // ③ the overlay pass (glazing, tablet, cluster, mirrors + vehicle hologram/wireframe/ripple overlay)
        const Stages = GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT;
        const PremultipliedBlend = {
            color: { srcFactor: "one", dstFactor: "one-minus-src-alpha", operation: "add" },
            alpha: { srcFactor: "zero", dstFactor: "one", operation: "add" }
        };
        const VehicleVertexLayout = [{ arrayStride: VertexStride, attributes: [
            { shaderLocation: 0, offset: 0,  format: "float32x3" },
            { shaderLocation: 1, offset: 12, format: "uint32" },
            { shaderLocation: 2, offset: 16, format: "float32x3" },
            { shaderLocation: 3, offset: 28, format: "uint32" }] }];
        this.OverlayLayout = Exchange.ConstructDescriptorLayout(["Uniform", "StorageRead", "Uniform", "Uniform", "StorageRead", "Texture2D", "Texture2D",
                                                                 "Texture2D", "Texture2DLayered", "Sampler", "Texture2D", "Sampler"], Stages, "Cabin overlay");
        this.OverlayGroup = Exchange.ConstructDescriptorGroup(this.OverlayLayout,
            [FrameRecords, OccupantRecords, this.OverlayUniform, EffectRecords, VehicleRecords, this.TabletTexture.createView(), this.ClusterTexture.createView(),
             this.PreviewTexture.createView(), this.MirrorTexture.createView({ dimension: "2d-array" }), this.LinearSampler, Atmosphere.SkyView, Atmosphere.SkySampler]);
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
            Targets: [{ format: "rgba16float", blend: PremultipliedBlend }]
        }, [this.OverlayLayout]);
        this.VehicleHoloProgram = await this.Shaders.ConstructRasterProgram("CabinOverlay.wgsl", {
            VertexEntry: "VehicleHoloVertex", FragmentEntry: "VehicleHoloFragment",
            VertexLayouts: VehicleVertexLayout,
            Primitive: { topology: "triangle-list", cullMode: "none" },
            DepthStencil: { format: "depth32float", depthWriteEnabled: false, depthCompare: "greater" },
            Targets: [{ format: "rgba16float", blend: PremultipliedBlend }]
        }, [this.OverlayLayout]);

        // ④ the forward views: mirrors (radiance) and the studio preview (display-referred, solid + holographic pass)
        this.ViewRecords = Exchange.ConstructRecords(ViewStride * 5, GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, "Cabin views");
        this.ForwardLayout = Exchange.ConstructDescriptorLayout(["Uniform", "UniformDynamic", "StorageRead", "StorageRead", "StorageRead", "Uniform",
                                                                 "TextureDepth2DLayered", "SamplerComparison", "Uniform", "Texture2D", "Sampler"], Stages, "Forward views");
        this.ForwardGroup = Exchange.ConstructDescriptorGroup(this.ForwardLayout,
            [FrameRecords, { buffer: this.ViewRecords, offset: 0, size: ForwardViewBytes }, OccupantRecords, MaterialRecords, VehicleRecords, EffectRecords,
             Shadow.LayeredView, Shadow.ComparisonSampler, Shadow.ReceiverRecords, Atmosphere.SkyView, Atmosphere.SkySampler]);
        const ForwardDescriptor = (Format, Blended = false) => ({
            VertexEntry: "ForwardVertex", FragmentEntry: "ForwardFragment",
            VertexLayouts: VehicleVertexLayout,
            Primitive: { topology: "triangle-list", cullMode: "none" },
            DepthStencil: { format: "depth32float", depthWriteEnabled: !Blended, depthCompare: Blended ? "less-equal" : "less" },
            Targets: [{ format: Format, ...(Blended ? { blend: PremultipliedBlend } : {}) }]
        });
        this.MirrorProgram      = await this.Shaders.ConstructRasterProgram("ForwardRaster.wgsl", ForwardDescriptor("rgba16float", false), [this.ForwardLayout]);
        this.PreviewProgram     = await this.Shaders.ConstructRasterProgram("ForwardRaster.wgsl", ForwardDescriptor("rgba8unorm-srgb", false), [this.ForwardLayout]);
        this.PreviewHoloProgram = await this.Shaders.ConstructRasterProgram("ForwardRaster.wgsl", ForwardDescriptor("rgba8unorm-srgb", true), [this.ForwardLayout]);

        // ⑤ the HMI itself, when this host can draw 2D canvases (the headless harness cannot)
        this.ConstructHmi().catch((Failure) => { this.Metrics.Hmi = `unavailable: ${Failure && Failure.message ? Failure.message : Failure}`; });
    }

    ClearCanvasTextures()
    {
        const Device = this.Exchange.Device;
        const Encoder = Device.createCommandEncoder({ label: "Cabin clear" });
        for (const [Texture, Colour] of [[this.TabletTexture, [0.004, 0.006, 0.007, 1]], [this.ClusterTexture, [0, 0, 0, 0]], [this.PreviewTexture, [0.01, 0.02, 0.022, 1]]])
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
        this.Screen.Configurator = this;
        this.Screen.PresentAspect(this.Layout.Tablet.Width / this.Layout.Tablet.Height);
        this.Cluster = new ClusterSequence(ClusterCanvas);
        this.Cluster.Demonstration = false;
        this.Metrics.Hmi = "live";
        if (this.PendingPressure !== undefined)
            this.AssignPressure(this.PendingPressure);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                         VEHICLE CONFIGURATOR & X-RAY SCAN
    //--------------------------------------------------------------------------------------------------------------------

    ResolveWellDropPoint(WellU, WellV)
    {
        const Nx = (Math.max(0.0, Math.min(1.0, WellU)) - 0.5) * 2.0;
        const Ny = (0.5 - Math.max(0.0, Math.min(1.0, WellV))) * 2.0;
        const Yaw = Math.PI + this.Preview.Yaw;
        const Pitch = this.Preview.Pitch;
        const Back = [Math.cos(Pitch) * Math.cos(Yaw), Math.cos(Pitch) * Math.sin(Yaw), Math.sin(Pitch)];
        const Right = [-Math.sin(Yaw), Math.cos(Yaw), 0.0];
        const Up = CrossVector(Back, Right);
        const Point = AddVector(AddVector([0.0, 0.0, 0.15], ScaleVector(Right, Nx * 2.2)),
                                AddVector(ScaleVector(Up, Ny * 1.25), ScaleVector(Back, 0.85)));
        return [
            Math.max(-2.75, Math.min(2.75, Point[0])),
            Math.max(-1.15, Math.min(1.15, Point[1])),
            Math.max(-0.35, Math.min(1.15, Point[2]))
        ];
    }

    ApplyPaint(PaintId, Finish = this.Config.Finish, DropPointLocal = null)
    {
        if (!VehiclePaintPalette.some((Item) => Item.Id === PaintId))
            return;
        const Previous = this.CurrentPaintMaterial;
        this.Config.PaintId = PaintId;
        this.Config.Finish  = Finish === "matte" ? "matte" : "gloss";
        const Next = ResolveVehiclePaintMaterial(this.Config.PaintId, this.Config.Finish);
        this.CurrentPaintMaterial = Next;
        this.PaintRipple = {
            Active:          true,
            Center:          DropPointLocal ? DropPointLocal.slice(0, 3) : [2.65, 0.0, 0.25],
            Radius:          0.0,
            PreviousAlbedo:  Previous.Albedo.slice(),
            PreviousSurface: [Previous.Roughness, Previous.Metalness, Previous.CoatWeight, Previous.CoatRoughness]
        };
        if (this.Screen)
            this.Screen.Dirty = true;
        if (this.OnMaterialChange)
            this.OnMaterialChange("paint", Next, this.Config);
        if (this.OnConfigChange)
            this.OnConfigChange(this.Config);
    }

    ApplyFinish(Finish)
    {
        const NextFinish = Finish === "matte" ? "matte" : "gloss";
        if (this.Config.Finish === NextFinish)
            return;
        this.ApplyPaint(this.Config.PaintId, NextFinish, [0.0, 0.0, 0.55]);
    }

    ApplyWheel(WheelId, DropPointLocal = null)
    {
        if (!VehicleWheelPalette.some((Item) => Item.Id === WheelId))
            return;
        const Previous = this.CurrentWheelMaterial;
        this.Config.WheelId = WheelId;
        const Next = ResolveVehicleWheelMaterial(this.Config.WheelId);
        this.CurrentWheelMaterial = Next;
        this.RimRipple = {
            Active:         true,
            Center:         DropPointLocal ? DropPointLocal.slice(0, 3) : [1.73, 1.05, -0.10],
            Radius:         0.0,
            PreviousAlbedo: Previous.Albedo.slice()
        };
        if (this.Screen)
            this.Screen.Dirty = true;
        if (this.OnMaterialChange)
            this.OnMaterialChange("wheel", Next, this.Config);
        if (this.OnConfigChange)
            this.OnConfigChange(this.Config);
    }

    ToggleScan(Force = undefined)
    {
        const Next = Force !== undefined ? !!Force : !this.Config.Scan;
        this.Config.Scan = Next;
        if (Next && this.ScanX < -2.5 && !this.Config.GhostHold)
            this.ScanX = 3.15;
        if (!Next)
            this.Config.GhostHold = false;
        if (this.Screen)
            this.Screen.Dirty = true;
        if (this.OnConfigChange)
            this.OnConfigChange(this.Config);
    }

    ToggleGhostHold(Force = undefined)
    {
        const Next = Force !== undefined ? !!Force : !this.Config.GhostHold;
        this.Config.GhostHold = Next;
        if (Next)
        {
            this.Config.Scan = true;
            this.ScanX = -3.15;
            this.HoloAmount = 1.0;
        }
        else if (this.Config.Scan)
        {
            this.Config.Scan = false;
        }
        if (this.Screen)
            this.Screen.Dirty = true;
        if (this.OnConfigChange)
            this.OnConfigChange(this.Config);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                    FRAME
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 Live: { Speed [km/h], Revolutions, Gear, Temperatures [°C ×4], Pressures [kPa ×4], Tread [×4], States [×4] }.
    //    Returns a pressure [kPa] when the Tyres application's slider asks the tyres to be pumped.
    Advance(Seconds, Live)
    {
        const ClampedSeconds = Math.min(Math.max(Seconds, 0.0), 0.1);
        this.Clock += ClampedSeconds;
        if (this.Preview.Spin)
            this.Preview.Yaw += 0.35 * ClampedSeconds;

        // 📝 Advance X-Ray Scan wave (front +3.15 m to rear -3.15 m) and hologram envelope.
        if (this.Config.GhostHold)
        {
            this.ScanX = -3.15;
            this.HoloAmount = Math.min(1.0, this.HoloAmount + ClampedSeconds * 4.5);
        }
        else if (this.Config.Scan)
        {
            this.HoloAmount = Math.min(1.0, this.HoloAmount + ClampedSeconds * 3.8);
            this.ScanX = Math.max(-3.15, this.ScanX - ClampedSeconds * 4.8);
        }
        else
        {
            this.ScanX = Math.min(3.2, this.ScanX + ClampedSeconds * 6.5);
            if (this.ScanX > 2.85)
                this.HoloAmount = Math.max(0.0, this.HoloAmount - ClampedSeconds * 4.2);
        }

        // 📝 Advance expanding 3D spherical material ripple waves.
        if (this.PaintRipple.Active)
        {
            this.PaintRipple.Radius += ClampedSeconds * 8.2;
            if (this.PaintRipple.Radius > 7.2)
                this.PaintRipple.Active = false;
        }
        if (this.RimRipple.Active)
        {
            this.RimRipple.Radius += ClampedSeconds * 7.8;
            if (this.RimRipple.Radius > 7.0)
                this.RimRipple.Active = false;
        }

        if (!this.Screen || !this.Settings.Screens)
            return null;
        const Device = this.Exchange.Device;
        const Screen = this.Screen;
        Screen.AdvanceClock();
        // 📝 The live applications redraw at 5 Hz; the rest of the panel only when touched (or while X-Ray HUD animates).
        const Revision = Math.floor(this.Clock * 5.0);
        if (Revision !== this.LiveRevision)
        {
            this.LiveRevision = Revision;
            Screen.Live = Live;
            if (Screen.Application === "telemetry" || Screen.Application === "tyres")
                Screen.Dirty = true;
        }
        if (Screen.Application === "garage" && (this.HoloAmount > 0.001 || Screen.DragMaterial))
            Screen.Dirty = true;
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

    WriteView(Slot, ViewProjection, Camera, Mode, Lights = null, PassMode = 0.0, AllowHolo = true)
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
        const Holo = AllowHolo ? this.HoloAmount : 0.0;
        Scalars.set([this.ScanX, Holo, this.Settings.WireframeWeight ?? 0.78, PassMode], 32);
        Scalars.set([...this.PaintRipple.Center, this.PaintRipple.Radius], 36);
        Scalars.set([...this.PaintRipple.PreviousAlbedo, this.PaintRipple.Active ? 1.0 : 0.0], 40);
        Scalars.set(this.PaintRipple.PreviousSurface, 44);
        Scalars.set([...this.RimRipple.Center, this.RimRipple.Radius], 48);
        Scalars.set([...this.RimRipple.PreviousAlbedo, this.RimRipple.Active ? 1.0 : 0.0], 52);
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
        this.WriteView(Index, ViewProjection, Virtual, 0.0, null, 0.0, false);
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

    // 📝 The Garage's studio: the car alone on its turntable, its own camera and three lights.
    //    When X-Ray Scan / Ghost-Wireframe is active, Pass 1 draws the solid unscanned vehicle plus the internal
    //    cockpit, hubs and brake assemblies, and Pass 2 overlays the translucent holographic ghost and facet wireframe.
    RecordPreview(Encoder, Scene, VertexRecords, BodyTransform)
    {
        const Centre = TransformPoint(BodyTransform, [0.0, 0.0, 0.15]);
        const Heading = Math.atan2(BodyTransform[1], BodyTransform[0]);
        const Yaw = Heading + Math.PI + this.Preview.Yaw, Pitch = this.Preview.Pitch, Distance = 8.2;
        const Eye = [Centre[0] + Distance * Math.cos(Pitch) * Math.cos(Yaw), Centre[1] + Distance * Math.cos(Pitch) * Math.sin(Yaw),
                     Centre[2] + Distance * Math.sin(Pitch)];
        const View = ConstructViewTransform(Eye, Centre);
        const Aspect = PreviewExtent[0] / PreviewExtent[1], Fov = 30.0 * Math.PI / 180.0, Near = 0.1, Far = 60.0;
        const F = 1.0 / Math.tan(0.5 * Fov);
        const Projection = new Float32Array([F / Aspect, 0, 0, 0, 0, F, 0, 0, 0, 0, Far / (Near - Far), -1, 0, 0, Near * Far / (Near - Far), 0]);
        const Back = NormalizeVector(SubtractVector(Eye, Centre));
        const Side = NormalizeVector(CrossVector([0.0, 0.0, 1.0], Back));
        const Key = NormalizeVector(AddVector(AddVector(ScaleVector(Side, 0.55), [0.0, 0.0, 0.8]), ScaleVector(Back, 0.45)));
        const Fill = NormalizeVector(AddVector(AddVector(ScaleVector(Side, -0.8), [0.0, 0.0, 0.3]), ScaleVector(Back, 0.5)));
        const Rim = NormalizeVector(AddVector(ScaleVector(Back, -0.9), [0.0, 0.0, 0.55]));
        const ViewProjection = MultiplyTransform(Projection, View);
        const Lights = [[...Key, 3.2], [...Fill, 1.0], [...Rim, 2.4]];
        this.WriteView(3, ViewProjection, Eye, 1.0, Lights, 1.0, true);
        if (this.HoloAmount > 0.001)
            this.WriteView(4, ViewProjection, Eye, 1.0, Lights, 2.0, true);

        const Pass = Encoder.beginRenderPass({
            label: "Garage preview",
            colorAttachments: [{ view: this.PreviewTexture.createView(), clearValue: [0.012, 0.022, 0.025, 1], loadOp: "clear", storeOp: "store" }],
            depthStencilAttachment: { view: this.PreviewDepth.createView(), depthClearValue: 1.0, depthLoadOp: "clear", depthStoreOp: "discard" }
        });
        Pass.setPipeline(this.PreviewProgram);
        Pass.setBindGroup(0, this.ForwardGroup, [3 * ViewStride]);
        Pass.setVertexBuffer(0, VertexRecords);
        for (const Run of Scene.DrawRuns)
        {
            if (Run.First >= 5)
                continue;
            const Set = Scene.FacetSets[Run.FacetSet];
            Pass.draw(Set.VertexCount, Run.Count, Set.FirstVertex, Run.First);
        }
        if (this.HoloAmount > 0.001 && this.PreviewHoloProgram)
        {
            Pass.setPipeline(this.PreviewHoloProgram);
            Pass.setBindGroup(0, this.ForwardGroup, [4 * ViewStride]);
            for (const Run of Scene.DrawRuns)
            {
                if (Run.First >= 5)
                    continue;
                const Set = Scene.FacetSets[Run.FacetSet];
                Pass.draw(Set.VertexCount, Run.Count, Set.FirstVertex, Run.First);
            }
        }
        Pass.end();
        this.Metrics.PreviewPasses++;
    }

    // 📝 Over the recombined radiance, before the antialiasing; the surface depth read only.
    RecordOverlay(Encoder, TargetView, DepthView, VertexRecords = null)
    {
        if (!this.OverlayProgram)
            return;
        const Screen = this.Screen;
        const Garage = !!(Screen && this.Settings.Screens && Screen.Application === "garage");
        const PanelWidth = Screen ? Screen.PanelWidth : 1200, PanelHeight = Screen ? Screen.PanelHeight : 750;
        const OffsetAcross = Screen ? Screen.OffsetAcross : 0, OffsetDown = Screen ? Screen.OffsetDown : 0;
        const Well = [(78 + OffsetAcross) / PanelWidth, (248 + OffsetDown) / PanelHeight, (718 + OffsetAcross) / PanelWidth, (648 + OffsetDown) / PanelHeight];
        const Live = !!Screen && this.Settings.Screens;
        const Record = new Float32Array([
            this.Layout.Tablet.Luminance, Live ? this.Settings.Brightness : 0.0, Garage ? 1.0 : 0.0, 0.0,
            ...Well,
            22.0 / PanelWidth, 22.0 / PanelHeight, Live ? this.Layout.Cluster.Luminance : 0.0, 0.0,
            this.Settings.GlassTransmission, 0.82, this.MirrorsRendered > 0 ? 1.0 : 0.0, 0.0,
            ...this.PaintRipple.Center, this.PaintRipple.Radius,
            ...this.PaintRipple.PreviousAlbedo, this.PaintRipple.Active ? 1.0 : 0.0,
            ...this.RimRipple.Center, this.RimRipple.Radius,
            ...this.RimRipple.PreviousAlbedo, this.RimRipple.Active ? 1.0 : 0.0]);
        this.Exchange.Device.queue.writeBuffer(this.OverlayUniform, 0, Record);
        const Pass = Encoder.beginRenderPass({
            label: "Cabin overlay",
            colorAttachments: [{ view: TargetView, loadOp: "load", storeOp: "store" }],
            depthStencilAttachment: { view: DepthView, depthReadOnly: true }
        });
        const DrawVehicleHolo = VertexRecords && this.Scene && this.VehicleHoloProgram
            && !this.Scene.Occupants[0].Inactive
            && ((this.HoloAmount > 0.001 && this.Settings.ApplyScanToRealVehicle !== false) || this.PaintRipple.Active || this.RimRipple.Active);
        if (DrawVehicleHolo)
        {
            Pass.setPipeline(this.VehicleHoloProgram);
            Pass.setBindGroup(0, this.OverlayGroup);
            Pass.setVertexBuffer(0, VertexRecords);
            for (const Run of this.Scene.DrawRuns)
            {
                if (Run.First >= 5)
                    continue;
                const Set = this.Scene.FacetSets[Run.FacetSet];
                Pass.draw(Set.VertexCount, Run.Count, Set.FirstVertex, Run.First);
            }
        }
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
        const Tablet = PlaceCockpitFixture(this.Layout.Tablet, this.Cockpit);
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
