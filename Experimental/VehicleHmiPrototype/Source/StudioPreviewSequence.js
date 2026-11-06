//============================================================================================================================================
//                                                        STUDIOPREVIEWSEQUENCE.JS
//============================================================================================================================================
// 🧩 Full BVH ReSTIR GI + Multi-Bounce Ray-Traced Reflection pipeline for the cockpit HMI's Garage configurator viewport,
//    rendering the exact Lobby photography studio (cyclorama cove, ceiling trusses, emissive overhead softbox + clustered
//    studio floods, display fleet, number plates, and live ControlVehicle) into CabinSequence.PreviewTexture.

import { SceneStructure, OccupantStride, OccupantRole } from "./FacetStructure.js";
import { CourseMaterial } from "./MaterialSpecification.js";
import { LobbyShapes, ResolveLobbyPlacements, EmitLobbyGroundFacets, LobbyVehiclePlacements } from "./LobbyStructure.js";
import { LobbyDrivers, EmitLobbyPlateFacets } from "./LobbyDrivers.js";
import { ClampStudioEye } from "./LobbyCameraMotion.js";
import { VehicleGeometry, ResolveSuspensionMountLocal, ControlVehicleSockets } from "./VehicleSpecification.js";
import { ConstructIdentityTransform, ConstructRigidTransform, ConstructViewTransform, ConstructPerspectiveTransform,
         MultiplyTransform, InvertTransform, ProjectRadicalInverse, TransformPoint } from "./TransformProjection.js";
import { LightClusterSpace } from "./LightClusterSpace.js";
import { LightingSystem, AutomotiveMode, LightType } from "./LightingSystem.js";
import { EmissiveLightSpace } from "./EmissiveLightSpace.js";
import { RayTracingSpace } from "./RayTracingSpace.js";
import { AtmosphereIntegrator } from "./AtmosphereIntegrator.js";
import { SurfaceProjection } from "./SurfaceProjection.js";
import { NearestDepthSpace } from "./NearestDepthSpace.js";
import { ResamplingSequence } from "./ResamplingSequence.js";
import { VarianceIntegrator } from "./VarianceIntegrator.js";
import { DisplayProjection } from "./DisplayProjection.js";

const FrameRecordExtent = 528;   // [B] FrameRecord in SceneRecords.wgsl

const StudioFeatureBits = {
    Indirect: 1, TemporalReuse: 2, SpatialReuse: 4, VisibilityReuse: 8, VarianceKernel: 16, Reflection: 32,
    CardRadiosity: 64, Antialias: 128, DirectShadow: 256, AutoExposure: 512, RadianceRefresh: 1024,
    TemporalRestart: 2048, RayTracing: 8192, RayTracedShadows: 16384, RussianRoulette: 32768, FireflyFilter: 65536,
    MultiBounceNEE: 131072, RayTracedReflections: 262144
};

const StudioExtendedBits = { LightGrid: 8, EmissiveLights: 16, IesProfiles: 32 };

export function ResolveStudioExtent(GradeName = "high")
{
    if (GradeName === "low")
        return [256, 160];
    if (GradeName === "medium")
        return [768, 480];
    return [960, 600];
}

export function ResolveStudioBudget(GradeName = "high")
{
    const Grade = ["low", "medium", "high"].includes(GradeName) ? GradeName : "high";
    return Object.freeze({
        MaxBounces: Grade === "high" ? 3 : (Grade === "medium" ? 2 : 1),
        InitialCandidates: Grade === "low" ? 1 : 2,
        ReflectionBounces: Grade === "high" ? 2 : 1
    });
}

export function ConstructStudioLighting()
{
    const Lighting = new LightingSystem();
    Lighting.StudioLights = [{
        Type: LightType.Floodlight, Position: [0.0, 0.0, 5.85], Direction: [0.0, 0.0, -1.0],
        Colour: [0.96, 0.98, 1.0], Intensity: 950.0, Reach: 38.0,
        Params: [Math.cos(18.0 * Math.PI / 180.0), Math.cos(72.0 * Math.PI / 180.0), 0.0, 1.0]
    }];
    for (const [X, Y] of [[12.0, 0.0], [-12.0, 0.0], [0.0, 12.0], [0.0, -12.0]])
    {
        const Length = Math.hypot(X, Y, 2.2);
        Lighting.StudioLights.push({
            Type: LightType.Floodlight, Position: [X * 0.5, Y * 0.5, 5.2], Direction: [X / Length, Y / Length, -2.2 / Length],
            Colour: [0.94, 0.96, 1.0], Intensity: 430.0, Reach: 44.0,
            Params: [Math.cos(22.0 * Math.PI / 180.0), Math.cos(72.0 * Math.PI / 180.0), 0.0, 1.0]
        });
    }
    Lighting.SetVehicleMode(AutomotiveMode.Off);
    return Lighting;
}

function ConstructStudioShaderAdapter(Shaders)
{
    const RasterConstants = {
        StudioPreviewPass: { Type: "u32", Value: 1 }
    };
    return {
        ConstructModule:         (FileName, Constants = {}, Label = FileName) =>
                                     Shaders.ConstructModule(FileName, Constants, Label),
        ConstructComputeProgram: (FileName, EntryPoint, Layouts, Constants = {}) =>
                                     Shaders.ConstructComputeProgram(FileName, EntryPoint, Layouts, Constants),
        ConstructRasterProgram:  (FileName, Descriptor, Layouts, Constants = {}) =>
                                     Shaders.ConstructRasterProgram(FileName, Descriptor, Layouts, { ...RasterConstants, ...Constants })
    };
}

function ResolvePlacementTransform(Placement)
{
    const Half = 0.5 * (Placement.Yaw || 0.0);
    return ConstructRigidTransform(Placement.Position, [0.0, 0.0, Math.sin(Half), Math.cos(Half)]);
}

function TransformWorldPoseToStudio(WorldTransform, Origin, CosNegHeading, SinNegHeading)
{
    const Out = new Float32Array(16);
    for (let Column = 0; Column < 3; ++Column)
    {
        const Base = Column * 4;
        const X = WorldTransform[Base + 0], Y = WorldTransform[Base + 1], Z = WorldTransform[Base + 2];
        Out[Base + 0] = CosNegHeading * X - SinNegHeading * Y;
        Out[Base + 1] = SinNegHeading * X + CosNegHeading * Y;
        Out[Base + 2] = Z;
        Out[Base + 3] = 0.0;
    }
    const Dx = WorldTransform[12] - Origin[0];
    const Dy = WorldTransform[13] - Origin[1];
    const Dz = WorldTransform[14] - Origin[2];
    Out[12] = CosNegHeading * Dx - SinNegHeading * Dy;
    Out[13] = SinNegHeading * Dx + CosNegHeading * Dy;
    Out[14] = VehicleGeometry.CoMHeight + Dz;
    Out[15] = 1.0;
    return Out;
}

export class StudioPreviewSequence
{
    constructor(Exchange, Shaders)
    {
        this.Exchange           = Exchange;
        this.BaseShaders        = Shaders;
        this.FrameIndex         = 0;
        this.PriorValid         = false;
        this.PreviousUnjittered = null;
        this.SkyBaked           = false;
        this.EmissiveUploaded   = false;
        this.OwnsScene          = false;
    }

    ConstructStandaloneStudioScene(HostScene)
    {
        const Scene = new SceneStructure();
        const SourceBodyFacets = HostScene.FacetSets[HostScene.VehicleBodySet].Facets.map((Facet) =>
            Facet.Family === CourseMaterial.Headlight ? { ...Facet, Family: CourseMaterial.StudioHeadlight } : Facet);
        const LeftSetIndex  = HostScene.Occupants[HostScene.VehicleOccupants.Wheels[0]].FacetSet;
        const RightSetIndex = HostScene.Occupants[HostScene.VehicleOccupants.Wheels[1]].FacetSet;
        const SourceLeft    = HostScene.FacetSets[LeftSetIndex].Facets;
        const SourceRight   = HostScene.FacetSets[RightSetIndex].Facets;

        const BodySet       = Scene.ConstructFacetSet("Studio ControlVehicle body", SourceBodyFacets, 0.05, 40.0);
        Scene.VehicleBodySet = BodySet;
        const WheelLeftSet  = Scene.ConstructFacetSet("Studio ControlVehicle wheel (left)", SourceLeft, 0.025, 40.0);
        const WheelRightSet = Scene.ConstructFacetSet("Studio ControlVehicle wheel (right)", SourceRight, 0.025, 40.0);
        const GroundSet     = Scene.ConstructFacetSet("Lobby studio floor", EmitLobbyGroundFacets(), 0.0, 0.0);
        const ShapeSets     = {};
        for (const [Key, Shape] of Object.entries(LobbyShapes))
            ShapeSets[Key] = Scene.ConstructFacetSet(Key, Shape.Emit(), Shape.Voxel, Shape.Cards);

        const VehicleRoles = OccupantRole.Raster | OccupantRole.Distance | OccupantRole.Cards | OccupantRole.Dynamic;
        const StaticRoles  = OccupantRole.Raster | OccupantRole.Distance | OccupantRole.Cards;
        const RestBody     = ConstructRigidTransform([0.0, 0.0, VehicleGeometry.CoMHeight], [0.0, 0.0, 0.0, 1.0]);
        Scene.VehicleOccupants.Body = Scene.ConstructOccupant("ControlVehicle", BodySet, RestBody, VehicleRoles);
        const StaticStrutLength = ControlVehicleSockets.SuspensionMount[0][2] - ControlVehicleSockets.AxleMount[0][2];
        const WheelNames = ["Wheel FL", "Wheel FR", "Wheel RL", "Wheel RR"];
        for (let Wheel = 0; Wheel < 4; ++Wheel)
        {
            const Set   = (Wheel & 1) ? WheelRightSet : WheelLeftSet;
            const Mount = ResolveSuspensionMountLocal(Wheel);
            const Hub   = TransformPoint(RestBody, [Mount[0], Mount[1], Mount[2] - StaticStrutLength]);
            Scene.VehicleOccupants.Wheels[Wheel] = Scene.ConstructOccupant(WheelNames[Wheel], Set,
                ConstructRigidTransform(Hub, [0.0, 0.0, 0.0, 1.0]), VehicleRoles);
        }
        Scene.DynamicCount = Scene.Occupants.length;

        // Lobby display fleet + front/rear Rank number plates (matching Lobby.html)
        const Paints = [CourseMaterial.DisplayPaintA, CourseMaterial.DisplayPaintB, CourseMaterial.DisplayPaintC, CourseMaterial.DisplayPaintD];
        const Lights = [CourseMaterial.DisplayLightA, CourseMaterial.DisplayLightB, CourseMaterial.DisplayLightC, CourseMaterial.DisplayLightD];
        const Remap  = (SetIndex, From, To) => Scene.FacetSets[SetIndex].Facets.map((Facet) =>
            Facet.Family === From ? { ...Facet, Family: To } : Facet);
        const GuestBodySets  = Paints.map((Paint, Index) =>
            Scene.ConstructFacetSet(`Studio display body ${Index}`, Remap(BodySet, CourseMaterial.BodyCoat, Paint), 0.05, 40.0));
        const GuestWheelSets = Lights.map((Light, Index) => [
            Scene.ConstructFacetSet(`Studio display wheel ${Index} (left)`, Remap(WheelLeftSet, CourseMaterial.Hub, Light), 0.025, 40.0),
            Scene.ConstructFacetSet(`Studio display wheel ${Index} (right)`, Remap(WheelRightSet, CourseMaterial.Hub, Light), 0.025, 40.0)]);
        const PlateRoles = OccupantRole.Raster | OccupantRole.Distance | OccupantRole.Fallback;
        Scene.PlateOccupants = [];
        const HeroPlateSet = Scene.ConstructFacetSet("Studio hero plates", EmitLobbyPlateFacets(LobbyDrivers[0]), 0.01, 0.0);
        Scene.PlateOccupants.push(Scene.ConstructOccupant("Hero plates", HeroPlateSet, RestBody, PlateRoles | OccupantRole.Dynamic));
        Scene.DisplayOccupants = [];
        LobbyVehiclePlacements.forEach((Placement, Car) =>
        {
            const Orientation   = [0.0, 0.0, Math.sin(0.5 * Placement.Yaw), Math.cos(0.5 * Placement.Yaw)];
            const BodyTransform = ConstructRigidTransform([Placement.Position[0], Placement.Position[1], VehicleGeometry.CoMHeight], Orientation);
            Scene.DisplayOccupants.push(Scene.ConstructOccupant(Placement.Name, GuestBodySets[Car], BodyTransform, StaticRoles));
            const GuestPlateSet = Scene.ConstructFacetSet(`Studio display plates ${Car}`, EmitLobbyPlateFacets(LobbyDrivers[Car + 1]), 0.01, 0.0);
            Scene.PlateOccupants.push(Scene.ConstructOccupant(`${Placement.Name} plates`, GuestPlateSet, BodyTransform, PlateRoles));
            for (let Wheel = 0; Wheel < 4; ++Wheel)
            {
                const Mount = ResolveSuspensionMountLocal(Wheel);
                const Hub   = TransformPoint(BodyTransform, [Mount[0], Mount[1], Mount[2] - StaticStrutLength]);
                Scene.DisplayOccupants.push(Scene.ConstructOccupant(`${Placement.Name} wheel ${Wheel}`, GuestWheelSets[Car][Wheel & 1],
                    ConstructRigidTransform(Hub, Orientation), StaticRoles));
            }
        });

        const RasterOnly = [];
        for (const Placement of ResolveLobbyPlacements())
        {
            if (LobbyShapes[Placement.Shape].Distance === false)
            {
                RasterOnly.push(Placement);
                continue;
            }
            const Index = Scene.ConstructOccupant(Placement.Name, ShapeSets[Placement.Shape], ResolvePlacementTransform(Placement), StaticRoles,
                                                  Placement.Material);
            Scene.Occupants[Index].Level = Placement.Shape;
        }
        Scene.BaseDistanceCount = Scene.Occupants.length;
        Scene.DistanceCount     = Scene.Occupants.length;
        Scene.GroundOccupant    = Scene.ConstructOccupant("Lobby studio ground", GroundSet, ConstructIdentityTransform(), OccupantRole.Raster);
        for (const Placement of RasterOnly)
        {
            const Index = Scene.ConstructOccupant(Placement.Name, ShapeSets[Placement.Shape], ResolvePlacementTransform(Placement),
                                                  OccupantRole.Raster, Placement.Material);
            Scene.Occupants[Index].Level = Placement.Shape;
        }
        Scene.ConstructDrawRuns();
        return Scene;
    }

    async ConstructResources({
        Lobby, GradeName, Scene, VertexRecords, OccupantRecords, MaterialRecords, Materials, EffectRecords = null,
        VehicleRecords, Shadow, Atmosphere, Distance, Cards, RayTracing, SamplingGroup, TyreMarks, PhotometryAtlas,
        OverlayLayout, OverlayRecords, GlassVertexCount, TabletView, ClusterView, MirrorArrayView, LinearSampler
    })
    {
        const Exchange = this.Exchange;
        const Device   = Exchange.Device;
        this.Lobby     = !!Lobby;
        this.GradeName = GradeName;
        this.Materials = Materials;
        this.Distance  = Distance;
        this.Extent    = ResolveStudioExtent(GradeName);
        this.TraceScale  = 2;
        this.TraceExtent = [this.Extent[0] >> 1, this.Extent[1] >> 1];

        if (this.Lobby && RayTracing && SamplingGroup)
        {
            // Lobby.html already holds the exact studio scene, TLAS/BLAS, studio lights, emissive clusters and surface cards.
            this.OwnsScene        = false;
            this.Scene            = Scene;
            this.VertexRecords    = VertexRecords;
            this.OccupantRecords  = OccupantRecords;
            this.RayTracing       = RayTracing;
            this.Atmosphere       = Atmosphere;
            this.SamplingGroup    = SamplingGroup;
            this.UseCardRadiosity = true;
            this.SkyBaked         = true;
        }
        else
        {
            this.OwnsScene       = true;
            this.Scene           = this.ConstructStandaloneStudioScene(Scene);
            this.VertexRecords   = Exchange.ConstructRecordsFrom(this.Scene.EncodeVertexRecords(), GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST,
                                                                 "Studio facet vertices");
            this.OccupantRecords = Exchange.ConstructRecords(this.Scene.Occupants.length * OccupantStride,
                                                             GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST, "Studio occupant records");
            this.LightClusters   = new LightClusterSpace(Exchange);
            if (PhotometryAtlas)
                this.LightClusters.AssignPhotometry(Device, PhotometryAtlas);
            this.Lighting        = ConstructStudioLighting();
            this.LightClusters.Update(Device, this.Lighting.Update(ConstructIdentityTransform(), []));

            this.RayTracing = new RayTracingSpace(Exchange);
            this.RayTracing.ConstructResources(this.Scene, MaterialRecords, this.LightClusters.Records);
            this.Emissive   = new EmissiveLightSpace();
            this.Emissive.Construct(this.Scene, Materials, this.RayTracing);
            if (this.Emissive.Update(this.Scene, Materials, 1.6))
            {
                this.LightClusters.AssignEmissive(Device, this.Emissive, true);
                this.EmissiveUploaded = true;
                this.Emissive.MembershipChanged = false;
            }
            Device.queue.writeBuffer(this.OccupantRecords, 0, this.Scene.EncodeOccupantRecords(null));

            this.Atmosphere = new AtmosphereIntegrator(Exchange, this.BaseShaders);
            await this.Atmosphere.ConstructResources();
            this.SamplingGroup    = Cards.ConstructSamplingGroup(this.OccupantRecords, this.Atmosphere.SkyView, this.Atmosphere.SkySampler);
            this.UseCardRadiosity = false;
            this.SkyBaked         = false;
        }

        this.StudioLight   = this.Atmosphere.ResolveLight(72.0, 145.0, 0.7);
        this.StudioShaders = ConstructStudioShaderAdapter(this.BaseShaders);
        this.FrameRecords  = Exchange.ConstructRecords(FrameRecordExtent, GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, "Studio frame record");
        this.ZeroEffects   = Exchange.ConstructRecords(EffectRecords ? EffectRecords.size : 1120, GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, "Studio effect record");
        this.ZeroCascades  = Exchange.ConstructRecords(Shadow?.ReceiverRecords ? Shadow.ReceiverRecords.size : 224, GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, "Studio shadow receiver");

        const [Width, Height] = this.Extent;
        this.Surface = new SurfaceProjection(Exchange, this.StudioShaders);
        await this.Surface.ConstructPrograms(this.FrameRecords, this.OccupantRecords, MaterialRecords, Distance.TraversalLayout,
                                             Cards.SamplingLayout, this.RayTracing.TraversalLayout, TyreMarks,
                                             { Records: this.ZeroEffects }, VehicleRecords);
        this.Surface.Effects = null;
        this.Surface.ConstructTargets(Width, Height, {
            LayeredView: Shadow.LayeredView, ComparisonSampler: Shadow.ComparisonSampler, ReceiverRecords: this.ZeroCascades
        }, this.Atmosphere);

        this.NearestDepth = new NearestDepthSpace(Exchange, this.StudioShaders);
        await this.NearestDepth.ConstructPrograms();
        this.NearestDepth.ConstructTargets(this.Extent, this.Surface);

        this.Resampling = new ResamplingSequence(Exchange, this.StudioShaders);
        await this.Resampling.ConstructPrograms(this.FrameRecords, MaterialRecords, Distance.TraversalLayout, Cards.SamplingLayout,
                                                this.NearestDepth.TraversalLayout);
        this.Resampling.ConstructTargets(this.TraceExtent, this.Surface);

        this.Diffuse = new VarianceIntegrator(Exchange, this.StudioShaders, "Studio diffuse",
                                              { MinimumWeight: 0.08, TallyLimit: 24, WaveletIterations: 3, ColourSigma: 4.0 });
        await this.Diffuse.ConstructPrograms(this.FrameRecords);
        this.Diffuse.ConstructTargets(this.TraceExtent, this.Extent, this.Surface,
                                      [this.Resampling.Views.Irradiance, this.Resampling.Views.Irradiance], this.Resampling.Views.Guides);

        this.Glossy = new VarianceIntegrator(Exchange, this.StudioShaders, "Studio glossy",
                                             { MinimumWeight: 0.12, TallyLimit: 12, WaveletIterations: 2, ColourSigma: 3.0 });
        await this.Glossy.ConstructPrograms(this.FrameRecords);
        this.Glossy.ConstructTargets(this.TraceExtent, this.Extent, this.Surface,
                                     [this.Resampling.Views.Reflection, this.Resampling.Views.Reflection], this.Resampling.Views.Guides);

        this.Display = new DisplayProjection(Exchange, this.StudioShaders);
        await this.Display.ConstructPrograms(this.FrameRecords, MaterialRecords, Distance.TraversalLayout, Cards.SamplingLayout,
                                             this.OccupantRecords, this.RayTracing.TraversalLayout, "rgba8unorm-srgb");
        this.Display.ConstructTargets(this.Extent, this.Surface, this.Resampling, this.Diffuse, this.Glossy, Cards);
        this.NearestDepth.ConstructTraversalGroups(this.Surface, this.Display, this.RayTracing);

        // See-through hero glazing (KindGlass) drawn over Recombined radiance before TAA, matching Lobby.html.
        this.OverlayRecords   = OverlayRecords;
        this.GlassVertexCount = GlassVertexCount;
        this.GlassUniform     = Exchange.ConstructRecords(80, GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, "Studio glass overlay");
        const IdleView        = this.Display.EmptySmoke.createView();
        this.GlassGroup       = Exchange.ConstructDescriptorGroup(OverlayLayout, [
            this.FrameRecords, this.OccupantRecords, this.GlassUniform, this.ZeroEffects, VehicleRecords,
            TabletView, ClusterView, IdleView, MirrorArrayView, LinearSampler,
            this.Atmosphere.SkyView, this.Atmosphere.SkySampler, this.Display.AdaptationRecords
        ]);
        this.GlassProgram     = await this.StudioShaders.ConstructRasterProgram("CabinOverlay.wgsl", {
            VertexEntry: "OverlayVertex", FragmentEntry: "OverlayFragment",
            VertexLayouts: [{ arrayStride: 40, attributes: [
                { shaderLocation: 0, offset: 0,  format: "float32x3" },
                { shaderLocation: 1, offset: 12, format: "uint32" },
                { shaderLocation: 2, offset: 16, format: "float32x3" },
                { shaderLocation: 3, offset: 28, format: "float32" },
                { shaderLocation: 4, offset: 32, format: "float32x2" }] }],
            Primitive: { topology: "triangle-list", cullMode: "none" },
            DepthStencil: { format: "depth32float", depthWriteEnabled: false, depthCompare: "greater" },
            Targets: [{ format: "rgba16float", blend: { color: { srcFactor: "one", dstFactor: "one-minus-src-alpha", operation: "add" },
                                                        alpha: { srcFactor: "zero", dstFactor: "one", operation: "add" } } }]
        }, [OverlayLayout]);
    }

    SynchroniseVehicle(HostScene, Vehicle)
    {
        if (!this.OwnsScene || !HostScene?.Occupants?.length)
            return;
        const Device    = this.Exchange.Device;
        const WorldBody = HostScene.Occupants[0].Transform;
        const Heading   = Math.atan2(WorldBody[1], WorldBody[0]);
        const CosNeg    = Math.cos(-Heading), SinNeg = Math.sin(-Heading);
        const Origin    = [WorldBody[12], WorldBody[13], WorldBody[14]];

        const StudioBody = TransformWorldPoseToStudio(WorldBody, Origin, CosNeg, SinNeg);
        this.Scene.AssignOccupantTransform(0, StudioBody);
        for (let Wheel = 0; Wheel < 4; ++Wheel)
        {
            const WorldWheel  = HostScene.Occupants[1 + Wheel].Transform;
            const StudioWheel = TransformWorldPoseToStudio(WorldWheel, Origin, CosNeg, SinNeg);
            this.Scene.AssignOccupantTransform(1 + Wheel, StudioWheel);
        }
        const HeroPlate = this.Scene.PlateOccupants?.[0];
        if (HeroPlate !== undefined)
            this.Scene.AssignOccupantTransform(HeroPlate, StudioBody);

        if (this.FrameIndex === 0)
        {
            for (let Index = 0; Index < 5; ++Index)
                this.Scene.RetainOccupantTransform(Index);
            if (HeroPlate !== undefined)
                this.Scene.RetainOccupantTransform(HeroPlate);
        }

        this.RayTracing.UpdateTlas(this.Scene, Vehicle);
        if (this.Emissive && this.Emissive.Update(this.Scene, this.Materials, 1.6))
        {
            this.LightClusters.AssignEmissive(Device, this.Emissive, !this.EmissiveUploaded);
            this.EmissiveUploaded = true;
            if (this.Emissive.MembershipChanged)
            {
                Device.queue.writeBuffer(this.OccupantRecords, 0, this.Scene.EncodeOccupantRecords(null));
                this.Emissive.MembershipChanged = false;
                return;
            }
        }
        const DynamicUploadCount = HeroPlate !== undefined ? HeroPlate + 1 : 5;
        Device.queue.writeBuffer(this.OccupantRecords, 0, this.Scene.EncodeOccupantRecords(null, DynamicUploadCount));
    }

    EncodeStudioFrame(Eye, Target, FovRadians, Near, Appearance, Clock)
    {
        const [Width, Height] = this.Extent;
        const Aspect   = Width / Height;
        const Sequence = (this.FrameIndex % 16) + 1;
        const JitterX  = (ProjectRadicalInverse(Sequence, 2) - 0.5) * 2.0 / Width;
        const JitterY  = (ProjectRadicalInverse(Sequence, 3) - 0.5) * 2.0 / Height;
        const View     = ConstructViewTransform(Eye, Target, [0.0, 0.0, 1.0]);
        const Proj     = ConstructPerspectiveTransform(FovRadians, Aspect, Near, JitterX, JitterY);
        const Unjittered = MultiplyTransform(ConstructPerspectiveTransform(FovRadians, Aspect, Near), View);
        const ViewProj   = MultiplyTransform(Proj, View);
        const Previous   = this.PreviousUnjittered || Unjittered;
        this.PreviousUnjittered = Unjittered;

        // Standard-Z [0..1] inverse view-projection for CPU PickVehicle(StudioScene, PreviewInverse, U, V)
        const Far = 80.0, F = 1.0 / Math.tan(0.5 * FovRadians);
        const ForwardProj = new Float32Array([
            F / Aspect, 0, 0, 0,
            0, F, 0, 0,
            0, 0, Far / (Near - Far), -1,
            0, 0, Near * Far / (Near - Far), 0
        ]);
        const ForwardInverse = InvertTransform(MultiplyTransform(ForwardProj, View));

        const Bytes    = new ArrayBuffer(FrameRecordExtent);
        const Scalars  = new Float32Array(Bytes);
        const Integers = new Uint32Array(Bytes);
        Scalars.set(ViewProj, 0);
        Scalars.set(InvertTransform(ViewProj), 16);
        Scalars.set(Unjittered, 32);
        Scalars.set(Previous, 48);
        Scalars.set(View, 64);
        Scalars.set(Eye, 80);
        Integers[83] = this.FrameIndex >>> 0;
        Scalars.set(this.StudioLight.Direction, 84);
        Scalars[87]  = Clock;
        Scalars.set(this.StudioLight.Illuminance, 88);
        Scalars[91]  = 0.30;   // Lobby.html SkyScale
        Scalars.set([Width, Height, 1.0 / Width, 1.0 / Height], 92);
        Scalars.set([this.TraceExtent[0], this.TraceExtent[1], 1.0 / this.TraceExtent[0], 1.0 / this.TraceExtent[1]], 96);
        Scalars.set([JitterX, JitterY], 100);
        Integers[102] = this.TraceScale;

        let Features = StudioFeatureBits.Indirect | StudioFeatureBits.SpatialReuse | StudioFeatureBits.VisibilityReuse
                     | StudioFeatureBits.VarianceKernel | StudioFeatureBits.Reflection | StudioFeatureBits.Antialias
                     | StudioFeatureBits.DirectShadow | StudioFeatureBits.AutoExposure | StudioFeatureBits.RadianceRefresh
                     | StudioFeatureBits.RayTracing | StudioFeatureBits.RayTracedShadows | StudioFeatureBits.RussianRoulette
                     | StudioFeatureBits.FireflyFilter | StudioFeatureBits.MultiBounceNEE | StudioFeatureBits.RayTracedReflections;
        if (this.UseCardRadiosity)
            Features |= StudioFeatureBits.CardRadiosity;
        if (this.PriorValid)
            Features |= StudioFeatureBits.TemporalReuse;
        else
            Features |= StudioFeatureBits.TemporalRestart;

        const { MaxBounces, InitialCandidates, ReflectionBounces } = ResolveStudioBudget(this.GradeName);
        Features |= (MaxBounces << 20) | (InitialCandidates << 24) | (ReflectionBounces << 28);

        const Extended = StudioExtendedBits.LightGrid | StudioExtendedBits.EmissiveLights | StudioExtendedBits.IesProfiles;
        Integers[103] = Features >>> 0;
        Integers[129] = Extended >>> 0;
        Integers[104] = 0;      // InspectionFinal
        Scalars[105]  = 20.0;   // TemporalTallyLimit
        Scalars[106]  = 20.0;   // SpatialRadius
        Integers[107] = 5;      // SpatialCount
        Scalars.set(this.StudioLight.NightSky, 108);
        Scalars[111]  = 1.6;    // Lobby.html EmissionScale
        Scalars[112]  = 0.6;
        Scalars[113]  = 0.0;    // ExposureCompensation
        Scalars[114]  = 1.0;    // IndirectScale
        const RoughByte  = Math.round(0.80 * 255.0);
        const ScaleShort = Math.round(1.0 * 1000.0) + 1;
        Integers[115]    = (1 | (RoughByte << 8) | (ScaleShort << 16)) >>> 0;
        Scalars.set([0.0, 0.0, 0.5], 116);
        Integers[119] = 0;

        // Carry Garage inspection stage (ViewMode, ScanX, HoloAmount) in unused FireLightPosition while FireLightIntensity = 0.0.
        const ViewMode   = Appearance?.View || 0;
        const ScanX      = Appearance?.Scan ? Appearance.Scan.ScanX : 3.25;
        const HoloAmount = Appearance?.Scan ? Appearance.Scan.HoloAmount : 0.0;
        Scalars.set([ViewMode, ScanX, HoloAmount], 120);
        Scalars[123] = 0.0;   // FireLightIntensity == 0 keeps FireIllumination inert
        Scalars.set([0.0, 0.0, 0.0], 124);
        Scalars[127] = 14.0;
        Scalars[128] = Near;

        this.Exchange.Device.queue.writeBuffer(this.FrameRecords, 0, Bytes);
        return { Unjittered, ForwardInverse };
    }

    Record(Encoder, TargetView, { HostScene, Vehicle, Yaw, Pitch, Appearance, GlassTransmission, SeeThroughGlass, Clock })
    {
        if (!this.SkyBaked)
        {
            this.Atmosphere.RecordSkyView(Encoder, 0.7);
            this.SkyBaked = true;
        }
        this.SynchroniseVehicle(HostScene, Vehicle);

        const Centre   = [0.0, 0.0, 0.46];
        const Azimuth  = Math.PI + Yaw;
        const Distance = 6.5;
        const RawEye   = [
            Centre[0] + Distance * Math.cos(Pitch) * Math.cos(Azimuth),
            Centre[1] + Distance * Math.cos(Pitch) * Math.sin(Azimuth),
            Centre[2] + Distance * Math.sin(Pitch)
        ];
        const Eye        = ClampStudioEye(RawEye);
        const FovRadians = 48.0 * Math.PI / 180.0;
        const Near       = 0.1;
        const { Unjittered, ForwardInverse } = this.EncodeStudioFrame(Eye, Centre, FovRadians, Near, Appearance, Clock);

        const Parity        = this.FrameIndex & 1;
        const DistanceGroup = this.Distance.TraversalGroup;
        const TraceGroup    = this.NearestDepth.TraversalGroups[Parity];

        this.Surface.RecordSurface(Encoder, this.Scene, this.VertexRecords);
        this.Surface.RecordDirect(Encoder, DistanceGroup, this.SamplingGroup, this.RayTracing.TraversalGroup);
        this.Resampling.RecordResampling(Encoder, Parity, DistanceGroup, this.SamplingGroup, TraceGroup);
        this.Diffuse.Record(Encoder, Parity, true);
        this.Resampling.RecordReflection(Encoder, DistanceGroup, this.SamplingGroup, TraceGroup);
        this.Glossy.Record(Encoder, Parity, true);

        const GlassOverlay = SeeThroughGlass && this.GlassProgram && this.GlassVertexCount > 0 ? (OverlayEncoder, RecombinedView) =>
        {
            const Scan = Appearance?.Scan;
            const GlassRecord = new Float32Array([
                0.0, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0,
                GlassTransmission ?? 0.72, 0.82, 0.0, 0.0,
                Appearance?.View || 0, Scan ? Scan.HoloAmount : 0.0, Scan ? Scan.ScanX : 3.25, Clock
            ]);
            this.Exchange.Device.queue.writeBuffer(this.GlassUniform, 0, GlassRecord);
            const Pass = OverlayEncoder.beginRenderPass({
                label: "Studio glass overlay",
                colorAttachments: [{ view: RecombinedView, loadOp: "load", storeOp: "store" }],
                depthStencilAttachment: { view: this.Surface.Views.Depth, depthReadOnly: true }
            });
            Pass.setPipeline(this.GlassProgram);
            Pass.setBindGroup(0, this.GlassGroup);
            Pass.setVertexBuffer(0, this.OverlayRecords);
            Pass.draw(this.GlassVertexCount);
            Pass.end();
        } : null;

        this.Display.RecordResolve(Encoder, Parity, undefined, null, GlassOverlay, null);
        this.Display.RecordPresent(Encoder, Parity, TargetView, DistanceGroup);

        this.PriorValid = true;
        this.FrameIndex++;
        return { Eye, Unjittered, ForwardInverse, DepthView: this.Surface.Views.Depth };
    }
}
