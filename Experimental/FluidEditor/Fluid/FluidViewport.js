/*====================================================================================================================================
                                                      FLUIDVIEWPORT.JS
====================================================================================================================================*/
// 🧩 The WebGPU FLIP/PIC free-surface viewport driven by the node graph

import
{
    ClearAccumulationKernel, ScatterKernel, NormaliseKernel,
    DivergenceKernel, PressureJacobiKernel, ProjectKernel, GatherKernel
} from './FluidKernels.js';

import
{
    DensityClearKernel, DensitySplatKernel, DensityResolveKernel, SurfaceRenderKernel
} from './FluidRenderKernels.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                      VIEWPORT STATE
//------------------------------------------------------------------------------------------------------------------------

// 📝 One record rather than scattered module globals, so the reset path has a single thing to rebuild
//    and no stale field can survive a resolution change.
const Viewport =
{
    Device:  null,
    Context: null,
    Canvas:  null,
    Format:  null,

    GridExtent:    64,
    ParticleTally: 0,
    ParticleCeiling: 0,

    Specification: null,
    ShadeMode:     0,
    InspectMode:   0,
    CameraMode:    '3D',

    OverlayBounds:    false,
    OverlayCollider:  false,

    Environment: { SunElevation: 20, SunAzimuth: 45, Turbidity: 10, FogDensity: 0.005 },

    Orbit:    { Yaw: 0.6, Pitch: 0.35, Distance: 9.0 },
    Running:  false,
    StepOnce: false,

    ElapsedSeconds: 0,
    FrameMilliseconds: 0,
    SubstepTally: 2,

    Resources: null,
    Pipelines: null,
    FrameHandle: 0,
    AcquiredCondition: false
};

// Domain extent in metres. Reset rebuilds against the specification, but a value is needed before the
// first feed lands or the first frame divides by zero.
let DomainExtent = 4.0;

//------------------------------------------------------------------------------------------------------------------------
//                                                      INITIALIZATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Resolves to an outcome record rather than rejecting. A missing adapter is an ordinary condition on
//    a prototype page — the graph half stays fully usable, and the caller reveals a notice instead of
//    the page dying at module scope with a blank viewport and a console trace nobody reads.
export async function InitializeFluidViewport(TargetCanvas)
{
    if (!navigator.gpu)
    {
        return { AcquiredCondition: false,
                 Diagnostic: 'navigator.gpu is absent. This browser has no WebGPU support — try Chrome '
                           + 'or Edge 113+, or enable the flag at chrome://flags/#enable-unsafe-webgpu.' };
    }

    let Adapter = null;
    try
    {
        Adapter = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
    }
    catch (AcquisitionFault)
    {
        return { AcquiredCondition: false, Diagnostic: `requestAdapter() threw: ${AcquisitionFault.message}` };
    }

    if (!Adapter)
    {
        return { AcquiredCondition: false,
                 Diagnostic: 'requestAdapter() returned null. WebGPU is present but no usable adapter was '
                           + 'surfaced — check chrome://gpu for a driver blocklist entry.' };
    }

    let Device = null;
    try
    {
        Device = await Adapter.requestDevice();
    }
    catch (ProvisionFault)
    {
        return { AcquiredCondition: false, Diagnostic: `requestDevice() rejected: ${ProvisionFault.message}` };
    }

    const Context = TargetCanvas.getContext('webgpu');
    if (!Context)
    {
        return { AcquiredCondition: false, Diagnostic: "getContext('webgpu') returned null." };
    }

    Viewport.Device  = Device;
    Viewport.Context = Context;
    Viewport.Canvas  = TargetCanvas;
    Viewport.Format  = navigator.gpu.getPreferredCanvasFormat();

    Context.configure({ device: Device, format: Viewport.Format, alphaMode: 'opaque' });

    // 🔴 A device lost after acquisition is silent otherwise — the loop keeps scheduling frames against a
    //    dead device and the canvas simply freezes on its last image, which reads as a hung simulation.
    Device.lost.then((Loss) =>
    {
        Viewport.AcquiredCondition = false;
        Viewport.Running = false;
        console.error(`[FluidViewport] device lost — ${Loss.reason}: ${Loss.message}`);
    });

    ComposePipelines();
    AllocateResources(Viewport.GridExtent);
    AttachCameraControls(TargetCanvas);
    CalibrateSurfaceExtent();

    Viewport.AcquiredCondition = true;
    Viewport.FrameHandle = requestAnimationFrame(AdvanceFrame);

    return { AcquiredCondition: true, Diagnostic: '' };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       PIPELINES
//------------------------------------------------------------------------------------------------------------------------

function ComposeComputePipeline(Device, Source, Label)
{
    return Device.createComputePipeline({
        label:   Label,
        layout:  'auto',
        compute: { module: Device.createShaderModule({ code: Source, label: `${Label}Module` }),
                   entryPoint: 'main' }
    });
}

function ComposePipelines()
{
    const Device = Viewport.Device;

    Viewport.Pipelines =
    {
        ClearAccumulation: ComposeComputePipeline(Device, ClearAccumulationKernel, 'ClearAccumulation'),
        Scatter:           ComposeComputePipeline(Device, ScatterKernel,           'Scatter'),
        Normalise:         ComposeComputePipeline(Device, NormaliseKernel,         'Normalise'),
        Divergence:        ComposeComputePipeline(Device, DivergenceKernel,        'Divergence'),
        PressureJacobi:    ComposeComputePipeline(Device, PressureJacobiKernel,    'PressureJacobi'),
        Project:           ComposeComputePipeline(Device, ProjectKernel,           'Project'),
        Gather:            ComposeComputePipeline(Device, GatherKernel,            'Gather'),

        DensityClear:   ComposeComputePipeline(Device, DensityClearKernel,   'DensityClear'),
        DensitySplat:   ComposeComputePipeline(Device, DensitySplatKernel,   'DensitySplat'),
        DensityResolve: ComposeComputePipeline(Device, DensityResolveKernel, 'DensityResolve'),

        Surface: Device.createRenderPipeline({
            label:  'SurfaceRender',
            layout: 'auto',
            vertex: {
                module: Device.createShaderModule({ code: SurfaceRenderKernel, label: 'SurfaceModule' }),
                entryPoint: 'VertexStage'
            },
            fragment: {
                module: Device.createShaderModule({ code: SurfaceRenderKernel, label: 'SurfaceModuleFragment' }),
                entryPoint: 'FragmentStage',
                targets: [{ format: Viewport.Format }]
            },
            primitive: { topology: 'triangle-list' }
        })
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       RESOURCES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Particle count is tied to the grid so the fill density stays constant as the grid changes — eight
//    particles per cell over an assumed quarter-full domain is the usual FLIP seeding rate. Without
//    this coupling, raising the resolution would thin the liquid out until the surface broke up.
function ResolveParticleCeiling(GridExtent)
{
    return Math.min(Math.floor(GridExtent * GridExtent * GridExtent * 0.25 * 8), 1_200_000);
}

function AllocateResources(GridExtent)
{
    const Device = Viewport.Device;
    if (Viewport.Resources) ReleaseResources();

    const CellTally = GridExtent * GridExtent * GridExtent;
    const ParticleCeiling = ResolveParticleCeiling(GridExtent);

    const Storage = GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST;

    Viewport.Resources =
    {
        CellTally,
        ParticleCeiling,
        PressureIndex: 0,

        // 8 floats per particle: position+alive, velocity+foam.
        Particles: Device.createBuffer({
            label: 'Particles', size: ParticleCeiling * 8 * 4,
            usage: Storage | GPUBufferUsage.COPY_SRC }),

        MomentumAccumulator: Device.createBuffer({
            label: 'MomentumAccumulator', size: CellTally * 3 * 4, usage: Storage }),
        WeightAccumulator: Device.createBuffer({
            label: 'WeightAccumulator', size: CellTally * 4, usage: Storage }),

        Velocity: Device.createBuffer({
            label: 'Velocity', size: CellTally * 4 * 4, usage: Storage }),
        VelocityOriginal: Device.createBuffer({
            label: 'VelocityOriginal', size: CellTally * 4 * 4, usage: Storage }),

        CellKind: Device.createBuffer({
            label: 'CellKind', size: CellTally * 4, usage: Storage }),
        Divergence: Device.createBuffer({
            label: 'Divergence', size: CellTally * 4, usage: Storage }),

        PressurePrimary: Device.createBuffer({
            label: 'PressurePrimary', size: CellTally * 4, usage: Storage }),
        PressureSecondary: Device.createBuffer({
            label: 'PressureSecondary', size: CellTally * 4, usage: Storage }),

        DensityAccumulator: Device.createBuffer({
            label: 'DensityAccumulator', size: CellTally * 2 * 4, usage: Storage }),
        DensityField: Device.createBuffer({
            label: 'DensityField', size: CellTally * 2 * 4, usage: Storage }),

        Colliders: Device.createBuffer({
            label: 'Colliders', size: Math.max(1, 16) * 16 * 4, usage: Storage }),

        SolverProfile: Device.createBuffer({
            label: 'SolverProfile', size: 128,
            usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST }),
        SurfaceProfile: Device.createBuffer({
            label: 'SurfaceProfile', size: 32,
            usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST }),
        CellTallyUniform: Device.createBuffer({
            label: 'CellTallyUniform', size: 16,
            usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST }),
        RenderProfile: Device.createBuffer({
            label: 'RenderProfile', size: 176,
            usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST })
    };

    Device.queue.writeBuffer(Viewport.Resources.CellTallyUniform, 0,
        new Uint32Array([CellTally, 0, 0, 0]));

    Viewport.GridExtent = GridExtent;
    Viewport.ParticleCeiling = ParticleCeiling;

    ComposeBindGroups();
    SeedParticles();
}

function ReleaseResources()
{
    for (const Held of Object.values(Viewport.Resources))
    {
        if (Held && typeof Held.destroy === 'function') Held.destroy();
    }
    Viewport.Resources = null;
}

function ComposeBindGroups()
{
    const Device = Viewport.Device;
    const Held = Viewport.Resources;
    const Pipelines = Viewport.Pipelines;

    const Entry = (Binding, Buffer) => ({ binding: Binding, resource: { buffer: Buffer } });

    Held.Bindings =
    {
        ClearAccumulation: Device.createBindGroup({
            layout: Pipelines.ClearAccumulation.getBindGroupLayout(0),
            entries: [Entry(0, Held.MomentumAccumulator), Entry(1, Held.WeightAccumulator),
                      Entry(2, Held.CellKind), Entry(3, Held.SolverProfile)] }),

        Scatter: Device.createBindGroup({
            layout: Pipelines.Scatter.getBindGroupLayout(0),
            entries: [Entry(0, Held.Particles), Entry(1, Held.MomentumAccumulator),
                      Entry(2, Held.WeightAccumulator), Entry(3, Held.CellKind),
                      Entry(4, Held.SolverProfile)] }),

        Normalise: Device.createBindGroup({
            layout: Pipelines.Normalise.getBindGroupLayout(0),
            entries: [Entry(0, Held.MomentumAccumulator), Entry(1, Held.WeightAccumulator),
                      Entry(2, Held.Velocity), Entry(3, Held.VelocityOriginal),
                      Entry(4, Held.CellKind), Entry(5, Held.Colliders),
                      Entry(6, Held.SolverProfile)] }),

        Divergence: Device.createBindGroup({
            layout: Pipelines.Divergence.getBindGroupLayout(0),
            entries: [Entry(0, Held.Velocity), Entry(1, Held.CellKind),
                      Entry(2, Held.Divergence), Entry(3, Held.SolverProfile)] }),

        // 📝 Two Jacobi bind groups, swapped each iteration. Ping-pong is not optional here: a single
        //    buffer read and written in the same dispatch is a race, and the residual would depend on
        //    workgroup scheduling rather than on the iteration count.
        PressureForward: Device.createBindGroup({
            layout: Pipelines.PressureJacobi.getBindGroupLayout(0),
            entries: [Entry(0, Held.PressurePrimary), Entry(1, Held.PressureSecondary),
                      Entry(2, Held.Divergence), Entry(3, Held.CellKind),
                      Entry(4, Held.SolverProfile)] }),
        PressureReverse: Device.createBindGroup({
            layout: Pipelines.PressureJacobi.getBindGroupLayout(0),
            entries: [Entry(0, Held.PressureSecondary), Entry(1, Held.PressurePrimary),
                      Entry(2, Held.Divergence), Entry(3, Held.CellKind),
                      Entry(4, Held.SolverProfile)] }),

        ProjectPrimary: Device.createBindGroup({
            layout: Pipelines.Project.getBindGroupLayout(0),
            entries: [Entry(0, Held.Velocity), Entry(1, Held.PressurePrimary),
                      Entry(2, Held.CellKind), Entry(3, Held.SolverProfile)] }),
        ProjectSecondary: Device.createBindGroup({
            layout: Pipelines.Project.getBindGroupLayout(0),
            entries: [Entry(0, Held.Velocity), Entry(1, Held.PressureSecondary),
                      Entry(2, Held.CellKind), Entry(3, Held.SolverProfile)] }),

        Gather: Device.createBindGroup({
            layout: Pipelines.Gather.getBindGroupLayout(0),
            entries: [Entry(0, Held.Particles), Entry(1, Held.Velocity),
                      Entry(2, Held.VelocityOriginal), Entry(3, Held.Colliders),
                      Entry(4, Held.SolverProfile)] }),

        DensityClear: Device.createBindGroup({
            layout: Pipelines.DensityClear.getBindGroupLayout(0),
            entries: [Entry(0, Held.DensityAccumulator), Entry(1, Held.CellTallyUniform)] }),

        DensitySplat: Device.createBindGroup({
            layout: Pipelines.DensitySplat.getBindGroupLayout(0),
            entries: [Entry(0, Held.Particles), Entry(1, Held.DensityAccumulator),
                      Entry(2, Held.SurfaceProfile)] }),

        DensityResolve: Device.createBindGroup({
            layout: Pipelines.DensityResolve.getBindGroupLayout(0),
            entries: [Entry(0, Held.DensityAccumulator), Entry(1, Held.DensityField),
                      Entry(2, Held.CellTallyUniform)] }),

        SurfacePrimary: Device.createBindGroup({
            layout: Pipelines.Surface.getBindGroupLayout(0),
            entries: [Entry(0, Held.DensityField), Entry(1, Held.Velocity),
                      Entry(2, Held.PressurePrimary), Entry(3, Held.Divergence),
                      Entry(4, Held.CellKind), Entry(5, Held.RenderProfile)] }),
        SurfaceSecondary: Device.createBindGroup({
            layout: Pipelines.Surface.getBindGroupLayout(0),
            entries: [Entry(0, Held.DensityField), Entry(1, Held.Velocity),
                      Entry(2, Held.PressureSecondary), Entry(3, Held.Divergence),
                      Entry(4, Held.CellKind), Entry(5, Held.RenderProfile)] })
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PARTICLE SEEDING
//------------------------------------------------------------------------------------------------------------------------

// 📝 Seeding runs on the host: it happens once per reset, not per frame, and the emitter shapes are far
//    easier to keep correct in one place than spread across a kernel variant per source kind.
function SeedParticles()
{
    const Held = Viewport.Resources;
    if (!Held) return;

    const Source = Viewport.Specification && Viewport.Specification.SourceSpecification;
    const Ceiling = Held.ParticleCeiling;
    const Staging = new Float32Array(Ceiling * 8);

    // No emitter wired — an empty domain, reported honestly rather than filled with a default body.
    if (!Source)
    {
        Viewport.ParticleTally = 0;
        Viewport.Device.queue.writeBuffer(Held.Particles, 0, Staging);
        return;
    }

    const Extent = DomainExtent;
    let Written = 0;

    // Deterministic jitter — a seeded LCG rather than Math.random, so a reset reproduces exactly and a
    // suspected solver fault cannot be a different initial condition in disguise.
    let RandomState = 0x2f6e2b1 >>> 0;
    const NextUnit = () =>
    {
        RandomState = (RandomState * 1664525 + 1013904223) >>> 0;
        return RandomState / 4294967296;
    };

    const Deposit = (X, Y, Z, VelocityX, VelocityY, VelocityZ) =>
    {
        if (Written >= Ceiling) return;
        const Base = Written * 8;
        Staging[Base + 0] = X;
        Staging[Base + 1] = Y;
        Staging[Base + 2] = Z;
        Staging[Base + 3] = 1.0;          // Alive
        Staging[Base + 4] = VelocityX;
        Staging[Base + 5] = VelocityY;
        Staging[Base + 6] = VelocityZ;
        Staging[Base + 7] = 0.0;          // Foam
        Written++;
    };

    // Particles per axis over a unit-fraction box, chosen to hit roughly the target fill rate.
    const FillBox = (OriginFraction, ExtentFraction, VelocityVector) =>
    {
        const Spacing = Extent / (Viewport.GridExtent * 2);

        const Lower = OriginFraction.map((Value) => Value * Extent);
        const Size  = ExtentFraction.map((Value) => Value * Extent);

        const StepsX = Math.max(1, Math.floor(Size[0] / Spacing));
        const StepsY = Math.max(1, Math.floor(Size[1] / Spacing));
        const StepsZ = Math.max(1, Math.floor(Size[2] / Spacing));

        for (let IndexZ = 0; IndexZ < StepsZ; IndexZ++)
        {
            for (let IndexY = 0; IndexY < StepsY; IndexY++)
            {
                for (let IndexX = 0; IndexX < StepsX; IndexX++)
                {
                    if (Written >= Ceiling) return;

                    // Jitter within the cell breaks the lattice; an unjittered grid of particles produces
                    // visible axis-aligned banding in the reconstructed surface.
                    Deposit(
                        Lower[0] + (IndexX + NextUnit()) * Spacing,
                        Lower[1] + (IndexY + NextUnit()) * Spacing,
                        Lower[2] + (IndexZ + NextUnit()) * Spacing,
                        VelocityVector[0], VelocityVector[1], VelocityVector[2]);
                }
            }
        }
    };

    switch (Source.Kind)
    {
        case 'dambreak':
            FillBox(Source.Origin, Source.Extent.map((Value) => Value * Source.Fill), [0, 0, 0]);
            break;

        case 'pool':
            FillBox([0.02, 0.02, 0.02], [0.96, Math.max(Source.Level, 0.02), 0.96], [0, 0, 0]);
            break;

        case 'drop':
        {
            const Centre = Source.Centre.map((Value) => Value * Extent);
            const Radius = Source.Radius * Extent;
            const Spacing = Extent / (Viewport.GridExtent * 2);
            const Reach = Math.ceil(Radius / Spacing);

            for (let IndexZ = -Reach; IndexZ <= Reach; IndexZ++)
            for (let IndexY = -Reach; IndexY <= Reach; IndexY++)
            for (let IndexX = -Reach; IndexX <= Reach; IndexX++)
            {
                const OffsetX = IndexX * Spacing, OffsetY = IndexY * Spacing, OffsetZ = IndexZ * Spacing;
                if (OffsetX * OffsetX + OffsetY * OffsetY + OffsetZ * OffsetZ > Radius * Radius) continue;

                Deposit(Centre[0] + OffsetX + NextUnit() * Spacing * 0.5,
                        Centre[1] + OffsetY + NextUnit() * Spacing * 0.5,
                        Centre[2] + OffsetZ + NextUnit() * Spacing * 0.5,
                        0, -Source.Speed, 0);
            }
            break;
        }

        case 'inflow':
            // 📝 A jet is a continuous emitter, but this seeding runs once. It is approximated as a
            //    charged column along the jet axis — the stream is present and moving from frame one,
            //    without a per-frame emission path the rest of the solver does not yet carry.
            FillBox(
                [Math.max(Source.Origin[0] - Source.Radius, 0.0),
                 Math.max(Source.Origin[1] - Source.Radius, 0.0),
                 Math.max(Source.Origin[2] - Source.Radius, 0.0)],
                [Source.Radius * 2, Source.Radius * 2, Source.Radius * 2],
                Source.Direction.map((Value) => Value * Source.Speed));
            break;

        case 'volume':
        default:
            FillBox([0.05, 0.05, 0.05], [0.9 * Source.Fill, 0.5 * Source.Fill, 0.9 * Source.Fill], [0, 0, 0]);
            break;
    }

    Viewport.ParticleTally = Written;
    Viewport.Device.queue.writeBuffer(Held.Particles, 0, Staging);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    UNIFORM UPLOAD
//------------------------------------------------------------------------------------------------------------------------

function ResolveForceTerms()
{
    const Terms = {
        Gravity:            [0, -9.81, 0],
        WindDirection:      [1, 0, 0],
        WindStrength:       0,
        VortexCentre:       [0.5, 0.5, 0.5],
        VortexAxis:         [0, 1, 0],
        VortexStrength:     0,
        TurbulenceStrength: 0,
        TurbulenceScale:    2,
        FieldSeed:          0
    };

    const Specification = Viewport.Specification;
    if (!Specification) return Terms;

    // Gravity starts at zero and accumulates, or an explicit gravity node would stack on top of the
    // implicit one the feed inserts and the liquid would fall at twice the rate.
    let GravityAccumulated = [0, 0, 0];
    let GravityPresent = false;

    for (const Force of Specification.Forces || [])
    {
        const Scale = Force.Scale === undefined ? 1 : Force.Scale;

        switch (Force.Kind)
        {
            case 'gravity':
                GravityPresent = true;
                GravityAccumulated = GravityAccumulated.map(
                    (Value, Axis) => Value + Force.Acceleration[Axis] * Scale);
                break;
            case 'wind':
                Terms.WindDirection = Force.Direction;
                Terms.WindStrength += Force.Strength * Scale;
                break;
            case 'vortex':
                Terms.VortexCentre   = Force.Centre.map((Value) => Value * DomainExtent);
                Terms.VortexAxis     = Force.Axis;
                Terms.VortexStrength += Force.Strength * Scale;
                break;
            case 'turbulence':
                Terms.TurbulenceStrength += Force.Strength * Scale;
                Terms.TurbulenceScale     = Force.FieldScale;
                Terms.FieldSeed           = Force.FieldSeed;
                break;
        }
    }

    if (GravityPresent) Terms.Gravity = GravityAccumulated;
    return Terms;
}

function UploadSolverProfile(TimeStep)
{
    const Held = Viewport.Resources;
    const Specification = Viewport.Specification;
    const Extent = Viewport.GridExtent;

    const Forces = ResolveForceTerms();

    // 📝 std140-ish layout. Mirrors SolverProfile in the kernel preamble field for field — a mismatch
    //    here binds without complaint and every dial reads as garbage, so the two move together.
    const Scalars = new ArrayBuffer(128);
    const Floats  = new Float32Array(Scalars);
    const Unsigned = new Uint32Array(Scalars);

    Unsigned[0] = Extent; Unsigned[1] = Extent; Unsigned[2] = Extent;
    Unsigned[3] = Viewport.ParticleTally;

    Floats[4] = DomainExtent;
    Floats[5] = DomainExtent / Extent;
    Floats[6] = TimeStep;
    Floats[7] = Specification ? Specification.PicFlipBlend : 0.95;

    Floats[8] = Forces.Gravity[0]; Floats[9] = Forces.Gravity[1]; Floats[10] = Forces.Gravity[2];
    Floats[11] = Specification ? Specification.Viscosity : 0.0;

    Floats[12] = Forces.WindDirection[0]; Floats[13] = Forces.WindDirection[1];
    Floats[14] = Forces.WindDirection[2]; Floats[15] = Forces.WindStrength;

    Floats[16] = Forces.VortexCentre[0]; Floats[17] = Forces.VortexCentre[1];
    Floats[18] = Forces.VortexCentre[2]; Floats[19] = Forces.VortexStrength;

    Floats[20] = Forces.VortexAxis[0]; Floats[21] = Forces.VortexAxis[1];
    Floats[22] = Forces.VortexAxis[2]; Floats[23] = Forces.TurbulenceStrength;

    Floats[24] = Forces.TurbulenceScale;
    Floats[25] = Viewport.ElapsedSeconds;
    Unsigned[26] = ResolveColliderTally();
    Floats[27] = Forces.FieldSeed;

    Viewport.Device.queue.writeBuffer(Held.SolverProfile, 0, Scalars);
}

function ResolveColliderTally()
{
    const Specification = Viewport.Specification;
    if (!Specification || !Specification.Colliders) return 0;
    return Math.min(Specification.Colliders.length, 16);
}

function UploadColliders()
{
    const Held = Viewport.Resources;
    const Specification = Viewport.Specification;

    const Records = new Float32Array(16 * 16);
    const Unsigned = new Uint32Array(Records.buffer);

    const Colliders = (Specification && Specification.Colliders) || [];
    const ShapeCode = { box: 0, sphere: 1, terrain: 2, paddle: 3 };

    for (let Index = 0; Index < Math.min(Colliders.length, 16); Index++)
    {
        const Collider = Colliders[Index];
        const Base = Index * 16;

        Unsigned[Base + 0] = ShapeCode[Collider.Shape] === undefined ? 0 : ShapeCode[Collider.Shape];
        Records[Base + 1] = Collider.Friction || 0;
        Records[Base + 2] = Collider.Rate || 0;
        Records[Base + 3] = (Collider.Radius || 0) * DomainExtent;

        const Centre = Collider.Centre || Collider.Origin || [0.5, 0.5, 0.5];
        Records[Base + 4] = Centre[0] * DomainExtent;
        Records[Base + 5] = Centre[1] * DomainExtent;
        Records[Base + 6] = Centre[2] * DomainExtent;

        const Extent = Collider.Extent || [0.1, 0.1, 0.1];
        Records[Base + 8]  = Extent[0] * DomainExtent;
        Records[Base + 9]  = Extent[1] * DomainExtent;
        Records[Base + 10] = Extent[2] * DomainExtent;

        // A terrain bed reuses Radius as its ridge amplitude and Rate as its phase.
        if (Collider.Shape === 'terrain')
        {
            Records[Base + 3] = (Collider.Roughness || 0.5) * DomainExtent * 0.1;
            Records[Base + 5] = (Collider.Height || 0.12) * DomainExtent;
            Records[Base + 2] = Collider.FieldSeed || 0;
        }
    }

    Viewport.Device.queue.writeBuffer(Held.Colliders, 0, Records);
}

function UploadSurfaceProfile()
{
    const Held = Viewport.Resources;
    const Specification = Viewport.Specification;
    const Extent = Viewport.GridExtent;

    const Scalars = new ArrayBuffer(32);
    const Floats = new Float32Array(Scalars);
    const Unsigned = new Uint32Array(Scalars);

    Unsigned[0] = Extent; Unsigned[1] = Extent; Unsigned[2] = Extent;
    Unsigned[3] = Viewport.ParticleTally;
    Floats[4] = DomainExtent / Extent;
    Floats[5] = Math.max(Specification ? Specification.SurfaceRadius : 1.0, 0.5);

    Viewport.Device.queue.writeBuffer(Held.SurfaceProfile, 0, Scalars);
}

function UploadRenderProfile()
{
    const Held = Viewport.Resources;
    const Specification = Viewport.Specification;

    const Canvas = Viewport.Canvas;
    const AspectRatio = Canvas.width / Math.max(Canvas.height, 1);

    const Camera = ResolveCameraBasis();
    const Sun = ResolveSunDirection();

    const Scalars = new ArrayBuffer(176);
    const Floats = new Float32Array(Scalars);
    const Unsigned = new Uint32Array(Scalars);

    Floats[0] = Camera.Origin[0]; Floats[1] = Camera.Origin[1]; Floats[2] = Camera.Origin[2];
    Floats[3] = DomainExtent;

    Floats[4] = Camera.Forward[0]; Floats[5] = Camera.Forward[1]; Floats[6] = Camera.Forward[2];

    // 📝 The isosurface threshold. Derived from the splat radius, because a wider splat raises the
    //    density everywhere — a fixed threshold would make the surface inflate as Radius rises.
    const SplatRadius = Math.max(Specification ? Specification.SurfaceRadius : 1.0, 0.5);
    Floats[7] = 0.5 * SplatRadius * SplatRadius;

    Floats[8] = Camera.Right[0]; Floats[9] = Camera.Right[1]; Floats[10] = Camera.Right[2];
    Floats[11] = Math.max(Specification ? Specification.SurfaceSmoothing : 0.5, 0.15);

    Floats[12] = Camera.Up[0]; Floats[13] = Camera.Up[1]; Floats[14] = Camera.Up[2];
    Floats[15] = AspectRatio;

    const Absorption = (Specification && Specification.Absorption) || [0.35, 0.04, 0.02];
    Floats[16] = Absorption[0]; Floats[17] = Absorption[1]; Floats[18] = Absorption[2];
    Floats[19] = Specification ? Specification.AbsorptionDepth : 1.2;

    Floats[20] = Sun[0]; Floats[21] = Sun[1]; Floats[22] = Sun[2];
    Floats[23] = Math.max(Specification ? Specification.SurfaceRoughness : 0.06, 0.01);

    Unsigned[24] = Viewport.GridExtent;
    Unsigned[25] = Viewport.GridExtent;
    Unsigned[26] = Viewport.GridExtent;
    Unsigned[27] = Viewport.InspectMode;

    Unsigned[28] = Viewport.ShadeMode;
    Unsigned[29] = Viewport.OverlayBounds ? 1 : 0;
    Unsigned[30] = Viewport.OverlayCollider ? 1 : 0;
    Floats[31] = Viewport.Environment.FogDensity;

    Floats[32] = Viewport.Environment.Turbidity;
    Floats[33] = Viewport.ElapsedSeconds;

    Viewport.Device.queue.writeBuffer(Held.RenderProfile, 0, Scalars);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        CAMERA
//------------------------------------------------------------------------------------------------------------------------

function ResolveCameraBasis()
{
    const Centre = [DomainExtent * 0.5, DomainExtent * 0.5, DomainExtent * 0.5];

    if (Viewport.CameraMode === '2D')
    {
        // 📝 A side elevation, not a plan view. For a liquid the interesting axis is the one gravity
        //    acts along — a top-down orthographic hides the whole free surface.
        const Distance = Viewport.Orbit.Distance;
        return {
            Origin:  [Centre[0], Centre[1], Centre[2] + Distance],
            Forward: [0, 0, -1],
            Right:   [1, 0, 0],
            Up:      [0, 1, 0]
        };
    }

    const { Yaw, Pitch, Distance } = Viewport.Orbit;

    const Origin = [
        Centre[0] + Distance * Math.cos(Pitch) * Math.sin(Yaw),
        Centre[1] + Distance * Math.sin(Pitch),
        Centre[2] + Distance * Math.cos(Pitch) * Math.cos(Yaw)
    ];

    const Forward = Normalize([Centre[0] - Origin[0], Centre[1] - Origin[1], Centre[2] - Origin[2]]);
    const Right = Normalize(Cross(Forward, [0, 1, 0]));
    const Up = Cross(Right, Forward);

    return { Origin, Forward, Right, Up };
}

// ⚠️ Y-up. The elevation dial is the Y component — a Z-up reading of this puts the sun on the horizon
//    and the water goes flat, with nothing in the render to say why.
function ResolveSunDirection()
{
    const Elevation = Viewport.Environment.SunElevation * Math.PI / 180;
    const Azimuth   = Viewport.Environment.SunAzimuth * Math.PI / 180;

    return Normalize([
        Math.cos(Elevation) * Math.sin(Azimuth),
        Math.sin(Elevation),
        Math.cos(Elevation) * Math.cos(Azimuth)
    ]);
}

function Normalize(Vector)
{
    const Length = Math.hypot(Vector[0], Vector[1], Vector[2]) || 1;
    return [Vector[0] / Length, Vector[1] / Length, Vector[2] / Length];
}

function Cross(Left, Right)
{
    return [
        Left[1] * Right[2] - Left[2] * Right[1],
        Left[2] * Right[0] - Left[0] * Right[2],
        Left[0] * Right[1] - Left[1] * Right[0]
    ];
}

function AttachCameraControls(TargetCanvas)
{
    let Dragging = false;
    let LastX = 0, LastY = 0;

    TargetCanvas.addEventListener('pointerdown', (Gesture) =>
    {
        Dragging = true;
        LastX = Gesture.clientX;
        LastY = Gesture.clientY;
        TargetCanvas.setPointerCapture(Gesture.pointerId);
    });

    TargetCanvas.addEventListener('pointermove', (Gesture) =>
    {
        if (!Dragging) return;

        Viewport.Orbit.Yaw   -= (Gesture.clientX - LastX) * 0.008;
        Viewport.Orbit.Pitch += (Gesture.clientY - LastY) * 0.008;

        // Clamped short of the poles — at exactly ±90° the up vector is parallel to forward and the
        // basis collapses, which shows as the view snapping to a random roll.
        const Ceiling = Math.PI * 0.49;
        Viewport.Orbit.Pitch = Math.max(-Ceiling, Math.min(Ceiling, Viewport.Orbit.Pitch));

        LastX = Gesture.clientX;
        LastY = Gesture.clientY;
    });

    const Release = (Gesture) =>
    {
        Dragging = false;
        if (TargetCanvas.hasPointerCapture(Gesture.pointerId))
        {
            TargetCanvas.releasePointerCapture(Gesture.pointerId);
        }
    };
    TargetCanvas.addEventListener('pointerup', Release);
    TargetCanvas.addEventListener('pointercancel', Release);

    TargetCanvas.addEventListener('wheel', (Gesture) =>
    {
        Gesture.preventDefault();
        Viewport.Orbit.Distance = Math.max(
            DomainExtent * 0.35,
            Math.min(DomainExtent * 6, Viewport.Orbit.Distance * (1 + Math.sign(Gesture.deltaY) * 0.09)));
    }, { passive: false });
}

// Match the drawing buffer to the CSS box. Returns whether it changed, so the caller can skip the
// reconfigure when nothing moved.
function CalibrateSurfaceExtent()
{
    const Canvas = Viewport.Canvas;
    const Ratio = Math.min(window.devicePixelRatio || 1, 2);

    const Width  = Math.max(1, Math.floor(Canvas.clientWidth * Ratio));
    const Height = Math.max(1, Math.floor(Canvas.clientHeight * Ratio));

    if (Canvas.width === Width && Canvas.height === Height) return false;

    Canvas.width = Width;
    Canvas.height = Height;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       FRAME LOOP
//------------------------------------------------------------------------------------------------------------------------

let LastTimestamp = 0;

function AdvanceFrame(Timestamp)
{
    Viewport.FrameHandle = requestAnimationFrame(AdvanceFrame);
    if (!Viewport.AcquiredCondition || !Viewport.Resources) return;

    const FrameStart = performance.now();

    CalibrateSurfaceExtent();

    const Elapsed = LastTimestamp ? (Timestamp - LastTimestamp) / 1000 : 1 / 60;
    LastTimestamp = Timestamp;

    const Advancing = Viewport.Running || Viewport.StepOnce;
    const Simulable = Viewport.Specification && Viewport.Specification.SimulableCondition
                   && Viewport.ParticleTally > 0;

    if (Advancing && Simulable)
    {
        AdvanceSimulation(Elapsed);
        Viewport.StepOnce = false;
    }

    RenderSurface();

    // 📝 Measured on the host, so this is wall-clock frame cost including the queue submit, not a GPU
    //    timestamp. Smoothed, or the readout flickers too fast to read.
    const Measured = performance.now() - FrameStart;
    Viewport.FrameMilliseconds = Viewport.FrameMilliseconds * 0.9 + Measured * 0.1;
}

function AdvanceSimulation(Elapsed)
{
    const Device = Viewport.Device;
    const Held = Viewport.Resources;
    const Pipelines = Viewport.Pipelines;
    const Specification = Viewport.Specification;

    const Substeps = Math.max(1, Specification.Substeps || 1);
    Viewport.SubstepTally = Substeps;

    // 📝 The step is clamped to the specification rather than tracking real elapsed time. A frame hitch
    //    would otherwise hand the solver a huge dt, particles would tunnel through the walls in one
    //    advection, and the sim would explode — visibly, and blamed on the solver rather than the hitch.
    const NominalStep = Specification.TimeStep || 1 / 60;
    const StepLength = Math.min(NominalStep, Math.max(Elapsed, 1e-4)) / Substeps;

    UploadColliders();
    UploadSurfaceProfile();

    const CellTally = Held.CellTally;
    const CellGroups = Math.ceil(CellTally / 64);
    const ParticleGroups = Math.ceil(Math.max(Viewport.ParticleTally, 1) / 64);

    for (let Substep = 0; Substep < Substeps; Substep++)
    {
        Viewport.ElapsedSeconds += StepLength;
        UploadSolverProfile(StepLength);

        const Encoder = Device.createCommandEncoder({ label: `Substep${Substep}` });
        const Pass = Encoder.beginComputePass();

        // ---- P2G ----
        Pass.setPipeline(Pipelines.ClearAccumulation);
        Pass.setBindGroup(0, Held.Bindings.ClearAccumulation);
        Pass.dispatchWorkgroups(CellGroups);

        Pass.setPipeline(Pipelines.Scatter);
        Pass.setBindGroup(0, Held.Bindings.Scatter);
        Pass.dispatchWorkgroups(ParticleGroups);

        Pass.setPipeline(Pipelines.Normalise);
        Pass.setBindGroup(0, Held.Bindings.Normalise);
        Pass.dispatchWorkgroups(CellGroups);

        // ---- Pressure projection ----
        Pass.setPipeline(Pipelines.Divergence);
        Pass.setBindGroup(0, Held.Bindings.Divergence);
        Pass.dispatchWorkgroups(CellGroups);

        const Iterations = Math.max(1, Specification.PressureIterations || 40);
        Pass.setPipeline(Pipelines.PressureJacobi);
        for (let Iteration = 0; Iteration < Iterations; Iteration++)
        {
            Pass.setBindGroup(0, (Held.PressureIndex + Iteration) % 2 === 0
                ? Held.Bindings.PressureForward
                : Held.Bindings.PressureReverse);
            Pass.dispatchWorkgroups(CellGroups);
        }

        // Keep the final buffer for the next substep and the pressure inspection view.
        Held.PressureIndex = (Held.PressureIndex + Iterations) % 2;

        Pass.setPipeline(Pipelines.Project);
        Pass.setBindGroup(0, Held.PressureIndex === 1
            ? Held.Bindings.ProjectSecondary
            : Held.Bindings.ProjectPrimary);
        Pass.dispatchWorkgroups(CellGroups);

        // The inspection view needs only the final substep's post-projection divergence.
        // Earlier substeps will overwrite this buffer before the next pressure solve.
        if (Substep === Substeps - 1)
        {
            Pass.setPipeline(Pipelines.Divergence);
            Pass.setBindGroup(0, Held.Bindings.Divergence);
            Pass.dispatchWorkgroups(CellGroups);
        }

        // ---- G2P + advection ----
        Pass.setPipeline(Pipelines.Gather);
        Pass.setBindGroup(0, Held.Bindings.Gather);
        Pass.dispatchWorkgroups(ParticleGroups);

        Pass.end();
        Device.queue.submit([Encoder.finish()]);
    }

    // ---- Reconstruct the surface from the settled particle set ----
    const SurfaceEncoder = Device.createCommandEncoder({ label: 'SurfaceReconstruction' });
    const SurfacePass = SurfaceEncoder.beginComputePass();

    SurfacePass.setPipeline(Pipelines.DensityClear);
    SurfacePass.setBindGroup(0, Held.Bindings.DensityClear);
    SurfacePass.dispatchWorkgroups(CellGroups);

    SurfacePass.setPipeline(Pipelines.DensitySplat);
    SurfacePass.setBindGroup(0, Held.Bindings.DensitySplat);
    SurfacePass.dispatchWorkgroups(ParticleGroups);

    SurfacePass.setPipeline(Pipelines.DensityResolve);
    SurfacePass.setBindGroup(0, Held.Bindings.DensityResolve);
    SurfacePass.dispatchWorkgroups(CellGroups);

    SurfacePass.end();
    Device.queue.submit([SurfaceEncoder.finish()]);
}

function RenderSurface()
{
    const Device = Viewport.Device;
    const Held = Viewport.Resources;

    UploadRenderProfile();

    const Encoder = Device.createCommandEncoder({ label: 'SurfaceRender' });
    const Pass = Encoder.beginRenderPass({
        colorAttachments: [{
            view: Viewport.Context.getCurrentTexture().createView(),
            clearValue: { r: 0.04, g: 0.05, b: 0.07, a: 1 },
            loadOp: 'clear',
            storeOp: 'store'
        }]
    });

    Pass.setPipeline(Viewport.Pipelines.Surface);
    Pass.setBindGroup(0, Held.PressureIndex === 1
        ? Held.Bindings.SurfaceSecondary
        : Held.Bindings.SurfacePrimary);
    Pass.draw(3);
    Pass.end();

    Device.queue.submit([Encoder.finish()]);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE FEED CONTRACT
//------------------------------------------------------------------------------------------------------------------------

// 📝 The graph hands over a whole specification each time anything changes. Reseeding is deliberately
//    NOT unconditional: dragging a shading dial would otherwise restart the sim on every pointer move,
//    which reads as the viewport being broken. Only a change to the source or the domain reseeds.
export function ReconfigureFluid(Specification)
{
    const Previous = Viewport.Specification;
    Viewport.Specification = Specification;

    if (!Specification) return;

    const PreviousSource = Previous && Previous.SourceSpecification;
    const CurrentSource  = Specification.SourceSpecification;

    const SourceChanged = JSON.stringify(PreviousSource || null) !== JSON.stringify(CurrentSource || null);
    const ExtentChanged = Specification.DomainExtent !== undefined
                       && Specification.DomainExtent !== DomainExtent;

    if (ExtentChanged) DomainExtent = Specification.DomainExtent;

    if (!Viewport.AcquiredCondition || !Viewport.Resources) return;

    if (SourceChanged || ExtentChanged)
    {
        SeedParticles();
        ClearFields();
        Viewport.Orbit.Distance = Math.min(Math.max(Viewport.Orbit.Distance, DomainExtent * 0.35),
                                           DomainExtent * 6);
    }

    UploadColliders();
    UploadSurfaceProfile();
}

export function ReconfigureShading(Mode)
{
    const Codes = { refractive: 0, opaque: 1, thickness: 2, points: 3 };
    Viewport.ShadeMode = Codes[Mode] === undefined ? 0 : Codes[Mode];
}

export function ReconfigureInspection(Mode)
{
    const Codes = { surface: 0, particles: 1, velocity: 2, occupancy: 3, pressure: 4, divergence: 5 };
    Viewport.InspectMode = Codes[Mode] === undefined ? 0 : Codes[Mode];
}

export function ReconfigureOverlay(Overlay)
{
    if (Overlay.DomainBounds !== undefined)     Viewport.OverlayBounds   = !!Overlay.DomainBounds;
    if (Overlay.ColliderOutlines !== undefined) Viewport.OverlayCollider = !!Overlay.ColliderOutlines;
}

export function ReconfigureCameraMode(Mode)
{
    Viewport.CameraMode = Mode === '2D' ? '2D' : '3D';
}

// 📝 A grid change reallocates every buffer, so it necessarily restarts the sim. Guarded against a
//    no-op change, because reallocating on an unchanged value would restart the sim for nothing.
export function ReconfigureResolution(GridExtent)
{
    const Requested = Math.max(16, Math.min(256, GridExtent | 0));
    if (Requested === Viewport.GridExtent) return;

    if (!Viewport.AcquiredCondition)
    {
        Viewport.GridExtent = Requested;
        return;
    }

    AllocateResources(Requested);
    Viewport.ElapsedSeconds = 0;
}

export function ReconfigureEnvironment(Dials)
{
    for (const [Dial, Magnitude] of Object.entries(Dials))
    {
        if (Viewport.Environment[Dial] !== undefined) Viewport.Environment[Dial] = Magnitude;
    }
}

export function RegulateTransport(Directive)
{
    switch (Directive)
    {
        case 'run':  Viewport.Running = true;  break;
        case 'halt': Viewport.Running = false; break;

        case 'step':
            Viewport.Running = false;
            Viewport.StepOnce = true;
            break;

        case 'reset':
            Viewport.ElapsedSeconds = 0;
            Viewport.FrameMilliseconds = 0;
            if (Viewport.AcquiredCondition && Viewport.Resources)
            {
                SeedParticles();
                ClearFields();
            }
            break;
    }
}

// 📝 Reports what the solver actually holds. Read from the viewport's own state rather than recomputed
//    from the specification, so a feed that failed to land shows here as stale figures instead of the
//    panel confidently describing a simulation that is not running.
export function ResolveSolverTally()
{
    if (!Viewport.AcquiredCondition) return null;

    return {
        GridExtent:        Viewport.GridExtent,
        ParticleTally:     Viewport.ParticleTally,
        SubstepTally:      Viewport.SubstepTally,
        FrameMilliseconds: Viewport.FrameMilliseconds
    };
}

// Zero every grid field. Without this a reset leaves the previous run's pressure and velocity in place
// and the first step of the new run is kicked by the old solution.
function ClearFields()
{
    const Device = Viewport.Device;
    const Held = Viewport.Resources;
    const CellTally = Held.CellTally;

    const Blank = new Float32Array(CellTally * 4);
    Device.queue.writeBuffer(Held.Velocity, 0, Blank);
    Device.queue.writeBuffer(Held.VelocityOriginal, 0, Blank);

    const Scalar = new Float32Array(CellTally);
    Device.queue.writeBuffer(Held.PressurePrimary, 0, Scalar);
    Device.queue.writeBuffer(Held.PressureSecondary, 0, Scalar);
    Held.PressureIndex = 0;
    Device.queue.writeBuffer(Held.Divergence, 0, Scalar);
    Device.queue.writeBuffer(Held.CellKind, 0, new Uint32Array(CellTally));

    Device.queue.writeBuffer(Held.DensityField, 0, new Float32Array(CellTally * 2));
}
