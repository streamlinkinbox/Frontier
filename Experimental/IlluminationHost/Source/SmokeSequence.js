//============================================================================================================================================
//                                                             SMOKESEQUENCE.JS
//============================================================================================================================================
// 🧩 Drift smoke and tyre fire: an on-demand toroidal cell lattice that follows the car, ticked at 60 Hz on the device, marched per frame.

// 📝 The shape of Frontier's SmokeSequence (one on-demand vehicle domain, world-axis-aligned, scrolled in whole cells
//    with guard bands, contents kept in world space, quiet transport and a declared fade after the sources stop,
//    explicit retirement) and of its two-level gas (a solved coarse flow carrying fine scalars), rebuilt for WebGPU:
//
//      ① everything runs on the device — no CPU lattice, no per-tick field upload, no read-back;
//      ② both lattices are toroidal: scrolling rewrites only the cells the window newly covers (C++ copies them all);
//      ③ the fine lattice works in 8³ bricks and skips every brick with nothing in or flowing into it;
//      ④ the pressure solve is warm-started from the previous tick, so the fixed sweep count converges further;
//      ⑤ light is resolved once per occupied cell per tick (sun through the scene distance field and the smoke, sky,
//         bounce, fire) instead of a shadow march per sample per pixel, and the march skips empty bricks;
//      ⑥ the composite goes after temporal antialiasing into its own target, so no history is ever contaminated;
//      ⑦ with no smoke resident, not one pass is recorded.
//
//    Obstacles are the ONE scene distance field — walls, tunnel, stress objects and the vehicle, whose cells move with
//    it — so smoke rolls around the car and pours out of the tunnel without a single extra collision shape.

const TickSeconds     = 1.0 / 60.0;   // [s]     fixed tick, CoarseGasField.h's CoarseStepInterval
const TickCeiling     = 4;            // [-]     ticks per frame; a stalled tab drops its debt instead of spiralling
const PressureSweepsDefault = 16;     // [-]     even, so the warm-started pressure always ends in buffer A
const RecordStride    = 1024;         // [B]     one tick record per dynamic-offset slot
const RecordExtent    = 832;          // [B]     SmokeRecord
const ViewExtent      = 192;          // [B]     SmokeView
const FineCell        = 0.25;         // [m]
const BrickEdge       = 8;            // [cell]
const Anchor          = [0, 0, -2];   // [cell]  SmokeAnchor: the ground lies two cells into the first brick layer
const GuardBricks     = 3;            // [brick] the car stays this far inside the window; nearer, the window recentres
const FadeCeiling     = 12.0;         // [s]     SmokeSpecification::FadeCeiling — the plume's declared lifetime
const VehicleOccupants = 5;           // [-]     body and four wheels lead the occupant list
const SummaryBias     = 1048576;      // [brick]

// 📝 CoarseGasSettings losses, declared per second and converted per tick, plus the fine-lattice rates.
export const SmokeTransportSettings = Object.freeze({
    BuoyancyLift:       2.4,     // [m/s²/K]
    SmokeLossPerSecond: 0.22,    // [1/s]  fraction of density dissipated in one second
    CoolingPerSecond:   0.55,
    DampingPerSecond:   0.08,
    FlameLossPerSecond: 0.96,    // exp(−3.2 s⁻¹): flames stay at the tyre; Frontier's fine pass used exp(−1.8 s⁻¹),
                                 //   which at 25 cm lets the advected provenance light the whole plume
    SootLossPerSecond:  0.20,
    CurlStrength:       0.40,    // [m/s]
    CurlScale:          1.1,     // [1/m]
    SpeedLimit:         14.0,    // [m/s]
    MomentumRate:       9.0      // [1/s]  how fast a source drags the air toward its carrier velocity
});

// 📝 Every authored gas quantity the Smoke + Fire editor exposes: default, range, step, unit and label. The defaults are
//    the calibrated values above (the C++ losses, the exhibit's appearance); the sequence reads the live object each
//    tick and each frame, so a slider acts on the very next tick — nothing is rebuilt.
export const GasParameterCatalogue = Object.freeze({
    // source — multipliers on the tyre model's own rates, which stay the physical port
    Emission:         { Default: 1.0,  Minimum: 0.0,  Maximum: 4.0,  Step: 0.05, Unit: "×",      Label: "Emission rate" },
    Heat:             { Default: 1.0,  Minimum: 0.0,  Maximum: 4.0,  Step: 0.05, Unit: "×",      Label: "Temperature" },
    Expansion:        { Default: 1.0,  Minimum: 0.0,  Maximum: 3.0,  Step: 0.05, Unit: "×",      Label: "Volume expansion" },
    Soot:             { Default: 1.0,  Minimum: 0.0,  Maximum: 4.0,  Step: 0.05, Unit: "×",      Label: "Soot yield" },
    Momentum:         { Default: 9.0,  Minimum: 0.0,  Maximum: 24.0, Step: 0.5,  Unit: "1/s",    Label: "Carrier drag" },
    TreadEjection:    { Default: 1.0,  Minimum: 0.0,  Maximum: 3.0,  Step: 0.05, Unit: "×",      Label: "Tread ejection" },
    // simulation
    Buoyancy:         { Default: 2.4,  Minimum: 0.0,  Maximum: 8.0,  Step: 0.1,  Unit: "m/s²·K", Label: "Buoyancy" },
    Dissipation:      { Default: 0.10, Minimum: 0.0,  Maximum: 0.9,  Step: 0.01, Unit: "1/s",    Label: "Dissipation" },
    Cooling:          { Default: 0.55, Minimum: 0.0,  Maximum: 0.95, Step: 0.01, Unit: "1/s",    Label: "Cooling" },
    Vorticity:        { Default: 3.0,  Minimum: 0.0,  Maximum: 6.0,  Step: 0.1,  Unit: "ε",      Label: "Vorticity confinement" },
    Turbulence:       { Default: 0.60, Minimum: 0.0,  Maximum: 2.0,  Step: 0.02, Unit: "m/s",    Label: "Turbulence" },
    TurbulenceScale:  { Default: 3.7,  Minimum: 0.2,  Maximum: 4.0,  Step: 0.05, Unit: "1/m",    Label: "Turbulence frequency" },
    WindSpeed:        { Default: 0.8,  Minimum: 0.0,  Maximum: 12.0, Step: 0.1,  Unit: "m/s",    Label: "Wind speed" },
    WindHeading:      { Default: 30.0, Minimum: 0.0,  Maximum: 360.0, Step: 1.0, Unit: "°",      Label: "Wind heading" },
    TimeScale:        { Default: 1.0,  Minimum: 0.25, Maximum: 2.0,  Step: 0.05, Unit: "×",      Label: "Time scale" },
    PressureSweeps:   { Default: PressureSweepsDefault, Minimum: 4, Maximum: 40, Step: 2, Unit: "",  Label: "Pressure iterations" },
    // appearance
    Density:          { Default: 1.0,  Minimum: 0.2,  Maximum: 3.0,  Step: 0.05, Unit: "×",      Label: "Density" },
    Albedo:           { Default: 0.88, Minimum: 0.2,  Maximum: 0.98, Step: 0.01, Unit: "",       Label: "Albedo" },
    Phase:            { Default: 0.38, Minimum: 0.0,  Maximum: 0.9,  Step: 0.01, Unit: "g",      Label: "Forward scattering" },
    MultiScatter:     { Default: 0.75, Minimum: 0.0,  Maximum: 1.5,  Step: 0.05, Unit: "",       Label: "Multiple scattering" },
    Ambient:          { Default: 1.0,  Minimum: 0.0,  Maximum: 3.0,  Step: 0.05, Unit: "×",      Label: "Ambient light" },
    Detail:           { Default: 0.70, Minimum: 0.0,  Maximum: 1.0,  Step: 0.02, Unit: "",       Label: "Detail" },
    DetailScale:      { Default: 2.2,  Minimum: 0.2,  Maximum: 4.0,  Step: 0.05, Unit: "1/m",    Label: "Detail frequency" },
    Erosion:          { Default: 1.0,  Minimum: 0.0,  Maximum: 1.5,  Step: 0.05, Unit: "",       Label: "Edge erosion" },
    Warp:             { Default: 0.0,  Minimum: 0.0,  Maximum: 1.0,  Step: 0.05, Unit: "",       Label: "Billow warp" },
    Powder:           { Default: 0.0,  Minimum: 0.0,  Maximum: 1.0,  Step: 0.05, Unit: "",       Label: "Powder" },
    // fire
    FlameIntensity:   { Default: 1.0,  Minimum: 0.0,  Maximum: 4.0,  Step: 0.05, Unit: "×",      Label: "Flame intensity" },
    FlameTemperature: { Default: 1.0,  Minimum: 0.7,  Maximum: 1.6,  Step: 0.01, Unit: "×",      Label: "Flame temperature" },
    FlameCore:        { Default: 1800, Minimum: 1300, Maximum: 2400, Step: 10,   Unit: "K",      Label: "Flame core" },
    FlameTip:         { Default: 1150, Minimum: 900,  Maximum: 1600, Step: 10,   Unit: "K",      Label: "Flame tip" },
    FlameFlicker:     { Default: 0.6,  Minimum: 0.0,  Maximum: 1.0,  Step: 0.02, Unit: "",       Label: "Flicker" },
    FlameLoss:        { Default: 0.96, Minimum: 0.5,  Maximum: 0.995, Step: 0.005, Unit: "1/s",  Label: "Flame decay" },
    FireLight:        { Default: 1.0,  Minimum: 0.0,  Maximum: 4.0,  Step: 0.05, Unit: "×",      Label: "Fire light" }
});

export function ConstructGasParameters()
{
    const Parameters = {};
    for (const [Key, Entry] of Object.entries(GasParameterCatalogue))
        Parameters[Key] = Entry.Default;
    return Parameters;
}

export const SmokeGrades = Object.freeze({
    high:   { Extent: [96, 96, 48], MarchSteps: 112 },
    medium: { Extent: [96, 96, 40], MarchSteps: 88 },
    low:    { Extent: [80, 80, 32], MarchSteps: 64 }
});

function RemainingAfter(LossPerSecond, Seconds)
{
    return Math.pow(Math.max(0.0, 1.0 - LossPerSecond), Seconds);
}

export class SmokeSequence
{
    constructor(Exchange, Shaders, GradeName)
    {
        this.Exchange     = Exchange;
        this.Shaders      = Shaders;
        const Grade       = SmokeGrades[GradeName] || SmokeGrades.high;
        this.Extent       = Grade.Extent.slice();
        this.MarchSteps   = Grade.MarchSteps;
        this.BrickExtent  = this.Extent.map((Cells) => Cells / BrickEdge);
        this.BrickCount   = this.BrickExtent[0] * this.BrickExtent[1] * this.BrickExtent[2];
        this.CoarseExtent = this.Extent.map((Cells) => Cells / 2);
        this.CoarseCount  = this.CoarseExtent[0] * this.CoarseExtent[1] * this.CoarseExtent[2];
        this.Acquired     = false;
        this.Resident     = false;
        this.FineOrigin   = [0, 0, Anchor[2]];
        this.PriorFineOrigin = [0, 0, Anchor[2]];
        this.VelocityParity = 0;   // [-] coarse buffer holding the flow state
        this.FineParity   = 0;     // [-] fine texture holding the latest state
        this.Accumulator  = 0.0;
        this.Clock        = 0.0;
        this.QuietSeconds = 0.0;
        this.TicksPending = 0;
        this.WindowStarts = [];
        this.Metrics      = { Ticks: 0, Acquisitions: 0, Relocations: 0, Retirements: 0, Sources: 0, Burning: 0 };
        this.FireLight    = { Position: [0.0, 0.0, 0.0], Intensity: 0.0, Colour: [1.0, 0.52, 0.18] };
        this.Gas          = ConstructGasParameters();
        this.PressureSweeps = PressureSweepsDefault;
        this.RecordBytes  = new ArrayBuffer(RecordStride * TickCeiling);
        this.RecordFloats = new Float32Array(this.RecordBytes);
        this.RecordInts   = new Int32Array(this.RecordBytes);
        this.RecordUints  = new Uint32Array(this.RecordBytes);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                  PROGRAMS
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 Programs compile at bring-up (cheap); lattice storage is acquired on the first smoke demand.
    async ConstructPrograms(FrameRecords, DistanceLayout, Atmosphere)
    {
        this.FrameRecords = FrameRecords;
        this.Atmosphere   = Atmosphere;
        const Compute = GPUShaderStage.COMPUTE;
        const Layout = (Codes, Label) => this.Exchange.ConstructDescriptorLayout(Codes, Compute, Label);
        this.CommonLayout    = Layout(["UniformDynamic", "Storage", "Sampler", "Texture2D", "Sampler"], "Smoke tick");
        this.VelocityLayout  = Layout(["StorageRead", "Storage"], "Smoke flow");
        this.PressureLayout  = Layout(["Storage", "Storage", "Storage"], "Smoke pressure");
        this.FineLayout      = Layout(["Texture3D", "StorageTexture3D:rgba16float", "Storage", "Storage"], "Smoke fine");
        this.LightLayout     = Layout(["Texture3D", "StorageTexture3D:rgba16float", "StorageRead"], "Smoke light");
        this.MarchLayout     = Layout(["Uniform", "Uniform", "Texture3D", "Texture3D", "Texture3D", "Sampler", "StorageRead", "StorageRead",
                                       "TextureDepth2D", "StorageTexture2D:rgba16float", "StorageTexture2D:r32float"], "Smoke march");
        this.CompositeLayout = Layout(["Uniform", "Uniform", "Texture2D", "Texture2D", "Texture2DUnfiltered", "TextureDepth2D",
                                       "StorageTexture2D:rgba16float"], "Smoke composite");
        const Flow = [this.CommonLayout, DistanceLayout, this.VelocityLayout];
        const Program = (File, Entry, Layouts) => this.Shaders.ConstructComputeProgram(File, Entry, Layouts);
        [this.ResetProgram, this.ObstacleProgram, this.ForcesProgram, this.AdvectCoarseProgram,
         this.DivergenceProgram, this.RelaxProgram, this.ProjectProgram, this.AdvectFineProgram, this.LightProgram,
         this.MarchProgram, this.CompositeProgram] = await Promise.all([
            Program("SmokeAdvection.wgsl", "ResetSummary", [...Flow, this.FineLayout]),
            Program("SmokeTransport.wgsl", "ResolveObstacle", Flow),
            Program("SmokeTransport.wgsl", "IntegrateCoarseForces", Flow),
            Program("SmokeTransport.wgsl", "AdvectCoarse", Flow),
            Program("SmokePressure.wgsl", "ResolveDivergence", [...Flow, this.PressureLayout]),
            Program("SmokePressure.wgsl", "RelaxPressure", [...Flow, this.PressureLayout]),
            Program("SmokePressure.wgsl", "ProjectCoarse", [...Flow, this.PressureLayout]),
            Program("SmokeAdvection.wgsl", "AdvectFine", [...Flow, this.FineLayout]),
            Program("SmokeIllumination.wgsl", "IntegrateLight", [...Flow, this.LightLayout]),
            Program("SmokeMarch.wgsl", "MarchSmoke", [this.MarchLayout]),
            Program("SmokeComposite.wgsl", "ComposeSmoke", [this.CompositeLayout])
        ]);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                 ACQUISITION
    //--------------------------------------------------------------------------------------------------------------------

    AcquireResources()
    {
        if (this.Acquired)
            return;
        const Exchange = this.Exchange;
        // 📝 COPY_SRC lets a diagnostic read the lattice back; it costs nothing on the device.
        const Storage = GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST | GPUBufferUsage.COPY_SRC;
        this.RecordBuffer   = Exchange.ConstructRecords(RecordStride * TickCeiling, GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, "Smoke ticks");
        this.ViewBuffer     = Exchange.ConstructRecords(ViewExtent, GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, "Smoke view");
        this.ObstacleBuffer = Exchange.ConstructRecords(this.CoarseCount * 16, Storage, "Smoke obstacles");
        this.FlagBuffer     = Exchange.ConstructRecords(this.BrickCount * 8, Storage, "Smoke brick flags");
        this.SummaryBuffer  = Exchange.ConstructRecords(32, Storage, "Smoke summary");
        this.VelocityBuffers = [0, 1].map((Index) => Exchange.ConstructRecords(this.CoarseCount * 16, Storage, `Smoke flow ${"AB"[Index]}`));
        this.PressureBuffers = [0, 1].map((Index) => Exchange.ConstructRecords(this.CoarseCount * 4, Storage, `Smoke pressure ${"AB"[Index]}`));
        this.DivergenceBuffer = Exchange.ConstructRecords(this.CoarseCount * 4, Storage, "Smoke divergence");
        const Volume = (Label, Size) => Exchange.ConstructTexture({ label: Label, size: Size, dimension: "3d", format: "rgba16float",
                                                                   usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING
                                                                        | GPUTextureUsage.COPY_SRC });
        this.FineTextures = [Volume("Smoke fine A", this.Extent), Volume("Smoke fine B", this.Extent)];
        this.FineViews = this.FineTextures.map((Texture) => Texture.createView({ dimension: "3d" }));
        this.LightTexture = Volume("Smoke light", this.CoarseExtent);
        this.LightView = this.LightTexture.createView({ dimension: "3d" });
        this.NoiseTexture = Exchange.ConstructTexture({ label: "Smoke detail noise", size: [32, 32, 32], dimension: "3d", format: "rgba8unorm",
                                                        usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST });
        Exchange.Device.queue.writeTexture({ texture: this.NoiseTexture }, ConstructDetailNoise(32), { bytesPerRow: 32 * 4, rowsPerImage: 32 }, [32, 32, 32]);
        this.NoiseView = this.NoiseTexture.createView({ dimension: "3d" });
        this.RepeatSampler = Exchange.ConstructSampler({ magFilter: "linear", minFilter: "linear",
                                                         addressModeU: "repeat", addressModeV: "repeat", addressModeW: "repeat" });

        this.CommonGroup = Exchange.ConstructDescriptorGroup(this.CommonLayout,
            [{ buffer: this.RecordBuffer, offset: 0, size: RecordExtent }, this.ObstacleBuffer, this.RepeatSampler,
             this.Atmosphere.SkyView, this.Atmosphere.SkySampler]);
        // 📝 VelocityGroups[i] reads buffer i and writes the other; see RecordTransport for which pass takes which.
        this.VelocityGroups = [0, 1].map((Index) => Exchange.ConstructDescriptorGroup(this.VelocityLayout,
            [this.VelocityBuffers[Index], this.VelocityBuffers[1 - Index]]));
        this.PressureGroups = [0, 1].map((Index) => Exchange.ConstructDescriptorGroup(this.PressureLayout,
            [this.PressureBuffers[Index], this.PressureBuffers[1 - Index], this.DivergenceBuffer]));
        this.FineGroups = [0, 1].map((Index) => Exchange.ConstructDescriptorGroup(this.FineLayout,
            [this.FineViews[Index], this.FineViews[1 - Index], this.FlagBuffer, this.SummaryBuffer]));
        this.LightGroups = [0, 1].map((Index) => Exchange.ConstructDescriptorGroup(this.LightLayout,
            [this.FineViews[Index], this.LightView, this.FlagBuffer]));
        this.Acquired = true;
        if (this.PendingTargets)
            this.ConstructScreenTargets(...this.PendingTargets);
    }

    // 📝 The march runs at 1/MarchScale of the screen per axis; the composed target is screen sized.
    ConstructScreenTargets(ScreenExtent, Surface, Display, MarchScale)
    {
        this.PendingTargets = [ScreenExtent, Surface, Display, MarchScale];
        this.Display = Display;
        if (!this.Acquired)
            return;
        for (const Texture of this.ScreenTextures || [])
            Texture.destroy();
        const Exchange = this.Exchange;
        this.MarchScale = MarchScale;
        this.MarchExtent = [Math.ceil(ScreenExtent[0] / MarchScale), Math.ceil(ScreenExtent[1] / MarchScale)];
        const Usage = GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING;
        const MarchTexture = Exchange.ConstructTexture({ label: "Smoke march", size: this.MarchExtent, format: "rgba16float", usage: Usage });
        const DepthTexture = Exchange.ConstructTexture({ label: "Smoke march depth", size: this.MarchExtent, format: "r32float", usage: Usage });
        const ComposedTexture = Exchange.ConstructTexture({ label: "Smoke composed", size: ScreenExtent, format: "rgba16float",
                                                            usage: Usage | GPUTextureUsage.COPY_SRC });
        this.ScreenTextures = [MarchTexture, DepthTexture, ComposedTexture];
        const MarchView = MarchTexture.createView(), DepthView = DepthTexture.createView();
        this.ComposedView = ComposedTexture.createView();
        this.MarchGroups = [0, 1].map((Parity) => Exchange.ConstructDescriptorGroup(this.MarchLayout,
            [this.FrameRecords, this.ViewBuffer, this.FineViews[Parity], this.LightView, this.NoiseView, this.RepeatSampler,
             this.FlagBuffer, this.SummaryBuffer, Surface.Views.Depth, MarchView, DepthView]));
        this.CompositeGroups = [0, 1].map((Parity) => Exchange.ConstructDescriptorGroup(this.CompositeLayout,
            [this.FrameRecords, this.ViewBuffer, Display.Views.Resolved[Parity], MarchView, DepthView, Surface.Views.Depth, this.ComposedView]));
        Display.AssignComposed(this.ComposedView, ComposedTexture);
        this.ScreenExtent = ScreenExtent;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                              HOST-SIDE SCHEDULE
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 Context: { Enabled, Emissions (TyreThermalSolver.ResolveEmissions), Vehicle, Light, SkyScale, Density }.
    //    Decides the ticks owed this frame, the window placement and the swept sources of each tick, and encodes them.
    Advance(Δτ, Context)
    {
        this.TicksPending = 0;
        if (Context.Gas)
            this.Gas = Context.Gas;
        this.PressureSweeps = Math.max(2, 2 * Math.round(this.Gas.PressureSweeps / 2));
        this.Clock += Δτ * this.Gas.TimeScale;
        const Emissions = Context.Enabled ? Context.Emissions : Context.Emissions.map((Emission) => ({ ...Emission, Active: false }));
        const Demand = Emissions.some((Emission) => Emission.Active);
        this.ResolveFireLight(Emissions);
        if (!this.Resident)
        {
            if (!Demand)
                return;
            this.AcquireResources();
            this.Resident = true;
            this.Metrics.Acquisitions++;
            this.ResolvePlacement(Context.Vehicle.Position, Context.Vehicle.LinearVelocity, true);
            // 📝 A fresh domain has no history: its prior window is placed far away, so nothing old is ever read.
            this.PriorFineOrigin = this.FineOrigin.map((Cell, Axis) => Cell + 4 * this.Extent[Axis]);
            this.QuietSeconds = 0.0;
            this.Accumulator = TickSeconds;
            this.WindowStarts = Emissions.map((Emission) => Emission.Position.slice());
        }
        this.Accumulator += Δτ;
        let Count = 0;
        while (this.Accumulator >= TickSeconds && Count < TickCeiling)
        {
            this.Accumulator -= TickSeconds;
            Count++;
        }
        if (this.Accumulator >= TickSeconds)
            this.Accumulator = 0.0;
        if (Count === 0)
            return;
        const Settings = SmokeTransportSettings;
        let SourceTotal = 0;
        for (let Tick = 0; Tick < Count; ++Tick)
        {
            const Sources = [];
            Emissions.forEach((Emission, Wheel) =>
            {
                const Start = this.WindowStarts[Wheel] || Emission.Position;
                const From = Interpolate(Start, Emission.Position, Tick / Count);
                const To = Interpolate(Start, Emission.Position, (Tick + 1) / Count);
                if (!Emission.Active)
                    return;
                Sources.push({ ...Emission, From, To });
                if (Emission.Plume)
                {
                    const Lift = Emission.Plume.Offset;
                    Sources.push({ ...Emission, ...Emission.Plume, FlameRate: 0.0,
                                   From: [From[0] + Lift[0], From[1] + Lift[1], From[2] + Lift[2]],
                                   To: [To[0] + Lift[0], To[1] + Lift[1], To[2] + Lift[2]] });
                }
            });
            SourceTotal = Math.max(SourceTotal, Sources.length);
            const PreviousQuiet = this.QuietSeconds;
            this.QuietSeconds = Sources.length > 0 ? 0.0 : this.QuietSeconds + TickSeconds;
            // 📝 SmokeSequence.h: once the sources stop, the declared lifetime fades the contents continuously to zero.
            const Fade = Sources.length > 0 ? 1.0
                       : Math.max(0.0, FadeCeiling - this.QuietSeconds) / Math.max(TickSeconds, FadeCeiling - PreviousQuiet);
            if (Sources.length > 0 && this.ResolvePlacement(Context.Vehicle.Position, Context.Vehicle.LinearVelocity, false))
                this.Metrics.Relocations++;
            this.EncodeTick(Tick, Sources, Fade, Context, Settings);
            this.PriorFineOrigin = this.FineOrigin.slice();
        }
        this.WindowStarts = Emissions.map((Emission) => Emission.Position.slice());
        this.Exchange.Device.queue.writeBuffer(this.RecordBuffer, 0, this.RecordBytes, 0, Count * RecordStride);
        this.TicksPending = Count;
        this.Metrics.Ticks += Count;
        this.Metrics.Sources = SourceTotal;
        this.Metrics.Burning = Emissions.filter((Emission) => Emission.Burning).length;
        this.RetireAfterRecording = !Demand && this.QuietSeconds >= FadeCeiling;
    }

    // 📝 SmokeSpace.h ResolveSmokeShift(): the window moves in whole bricks only when the car leaves its protected
    //    interior; it then recentres a little behind the car, where the trail is. Height never scrolls.
    ResolvePlacement(Position, Velocity, Force)
    {
        const BrickWorld = BrickEdge * FineCell;
        const OriginBrick = [(this.FineOrigin[0] - Anchor[0]) / BrickEdge, (this.FineOrigin[1] - Anchor[1]) / BrickEdge];
        let Inside = true;
        for (let Axis = 0; Axis < 2; ++Axis)
        {
            const Local = Position[Axis] / BrickWorld - OriginBrick[Axis];
            if (Local < GuardBricks || Local > this.BrickExtent[Axis] - GuardBricks)
                Inside = false;
        }
        if (Inside && !Force)
            return false;
        const Speed = Math.hypot(Velocity[0], Velocity[1]);
        const Lag = Speed > 0.5 ? Math.min(5.0, 0.3 * Speed) / Speed : 0.0;
        const Next = [0, 1].map((Axis) =>
            (Math.floor((Position[Axis] - Velocity[Axis] * Lag) / BrickWorld) - this.BrickExtent[Axis] / 2) * BrickEdge + Anchor[Axis]);
        const Moved = Next[0] !== this.FineOrigin[0] || Next[1] !== this.FineOrigin[1];
        this.FineOrigin = [Next[0], Next[1], Anchor[2]];
        return Moved;
    }

    // 📝 A burning tyre is a flickering point light: for the scene (frame record, direct and card lighting) one light
    //    at the power-weighted centre of the fires, for the smoke one light per burning tyre.
    ResolveFireLight(Emissions)
    {
        let Power = 0.0;
        const Centre = [0.0, 0.0, 0.0];
        this.Fires = [];
        const Flicker = 0.78 + 0.12 * Math.sin(this.Clock * 17.0) + 0.07 * Math.sin(this.Clock * 31.0 + 1.3) + 0.05 * Math.sin(this.Clock * 7.3);
        for (const Emission of Emissions)
        {
            if (!Emission.Burning)
                continue;
            const Intensity = Emission.CombustionPower * 6.0e-5 * Flicker * this.Gas.FireLight;
            const Position = [Emission.Hub[0], Emission.Hub[1], Emission.Hub[2] + 0.3];
            this.Fires.push([...Position, Intensity]);
            for (let Axis = 0; Axis < 3; ++Axis)
                Centre[Axis] += Position[Axis] * Intensity;
            Power += Intensity;
        }
        this.FireLight.Intensity = Power;
        if (Power > 0.0)
            this.FireLight.Position = Centre.map((Component) => Component / Power);
    }

    EncodeTick(Slot, Sources, Fade, Context, Settings)
    {
        const Base = Slot * RecordStride / 4;
        const F = this.RecordFloats, I = this.RecordInts, U = this.RecordUints;
        F.fill(0.0, Base, Base + RecordExtent / 4);
        I.set(this.FineOrigin, Base + 0);
        U[Base + 3] = Math.min(8, Sources.length);
        I.set(this.PriorFineOrigin, Base + 4);
        U[Base + 7] = VehicleOccupants;
        U.set(this.Extent, Base + 8);
        U[Base + 11] = this.BrickCount;
        U.set(this.BrickExtent, Base + 12);
        // 📝 Parities flip once per recorded tick, so tick Slot of this frame writes the texture its predecessor read.
        U[Base + 15] = 1 - (this.FineParity ^ (Slot & 1));
        // 📝 The time scale stretches the simulated step; the lattice still ticks at 60 Hz of wall time.
        const Gas = this.Gas;
        const Step = TickSeconds * Gas.TimeScale;
        F[Base + 16] = FineCell;
        F[Base + 17] = 2.0 * FineCell;
        F[Base + 18] = Step;
        F[Base + 19] = this.Clock + Slot * Step;
        F[Base + 20] = RemainingAfter(Gas.Dissipation, Step);
        F[Base + 21] = RemainingAfter(Gas.Cooling, Step);
        F[Base + 22] = RemainingAfter(Settings.DampingPerSecond, Step);
        F[Base + 23] = RemainingAfter(Gas.FlameLoss, Step);
        F[Base + 24] = RemainingAfter(Settings.SootLossPerSecond, Step);
        F[Base + 25] = Fade;
        F[Base + 26] = Gas.Buoyancy;
        F[Base + 27] = Settings.SpeedLimit;
        F[Base + 28] = Gas.Turbulence;
        F[Base + 29] = Gas.TurbulenceScale;
        F[Base + 30] = RemainingAfter(0.9, Step);
        F[Base + 31] = SmokeExtinction * Gas.Density;
        const Minimum = [Infinity, Infinity, Infinity], Maximum = [-Infinity, -Infinity, -Infinity];
        Sources.slice(0, 8).forEach((Source, Index) =>
        {
            const Offset = Base + 80 + Index * 16;
            const Length = Math.hypot(Source.To[0] - Source.From[0], Source.To[1] - Source.From[1], Source.To[2] - Source.From[2]);
            // 📝 Emission is conserved along the swept path: a longer path spreads the same tick's smoke thinner.
            const Spread = Math.min(1.0, (2.0 * Source.Radius) / Math.max(1.0e-4, Length));
            F.set(Source.From, Offset);
            F[Offset + 3] = Source.Radius;
            F.set(Source.To, Offset + 4);
            F[Offset + 7] = Source.SmokeRate * Spread * SmokeYield * Gas.Emission;
            const Tread = Source.Tread || [0.0, 0.0, 0.0];
            F.set([Source.Velocity[0] + Tread[0] * Gas.TreadEjection, Source.Velocity[1] + Tread[1] * Gas.TreadEjection, Source.Velocity[2]], Offset + 8);
            F[Offset + 11] = Source.HeatRate * Spread * Gas.Heat;
            F[Offset + 12] = Source.FlameRate * Spread;
            F[Offset + 13] = Source.SootRate * Spread * SmokeYield * Gas.Soot;
            F[Offset + 14] = Gas.Momentum;
            F[Offset + 15] = (Source.Expansion || 0.0) * Spread * Gas.Expansion;
            const Reach = 3.5 * Math.max(Source.Radius, 0.75 * 2.0 * FineCell);
            for (let Axis = 0; Axis < 3; ++Axis)
            {
                Minimum[Axis] = Math.min(Minimum[Axis], Math.min(Source.From[Axis], Source.To[Axis]) - Reach);
                Maximum[Axis] = Math.max(Maximum[Axis], Math.max(Source.From[Axis], Source.To[Axis]) + Reach);
            }
        });
        if (Sources.length > 0)
        {
            F.set(Minimum, Base + 32);
            F.set(Maximum, Base + 36);
        }
        U[Base + 35] = Math.min(4, this.Fires.length);
        F[Base + 39] = Context.SkyScale;
        F.set(Context.Vehicle.Position, Base + 40);
        F.set(Context.Vehicle.LinearVelocity, Base + 44);
        F.set(Context.Vehicle.AngularVelocity, Base + 48);
        F.set(Context.Light.Direction, Base + 52);
        F.set(Context.Light.Illuminance, Base + 56);
        // 📝 The four spare scalars of the record: confinement, then the wind and how fast open air takes it up.
        const Heading = Gas.WindHeading * Math.PI / 180.0;
        F[Base + 43] = Gas.Vorticity;
        F[Base + 47] = Gas.WindSpeed > 0.01 ? 0.6 : 0.0;
        F[Base + 51] = Gas.WindSpeed * Math.cos(Heading);
        F[Base + 55] = Gas.WindSpeed * Math.sin(Heading);
        this.Fires.slice(0, 4).forEach((Fire, Index) =>
        {
            F.set(Fire.slice(0, 3), Base + 60 + Index * 4);
            F[Base + 63 + Index * 4] = Fire[3] / (4.0 * Math.PI);
        });
        F.set(this.FireLight.Colour, Base + 76);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                  RECORDING
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 Per tick, with the flow state in buffer p and the latest fine state in texture f:
    //    forces in place on p (VelocityGroups[1−p] writes p) → advect p→1−p (VelocityGroups[p]) → divergence of 1−p
    //    (VelocityGroups[1−p] reads it) → sweeps A↔B → project in place on 1−p (VelocityGroups[p] writes it) → fine
    //    advection f→1−f through 1−p (VelocityGroups[1−p]) → light from 1−f.
    RecordTransport(Encoder, DistanceGroup, TimestampWrites)
    {
        if (!this.Resident || this.TicksPending === 0)
            return;
        const Coarse = this.CoarseExtent.map((Cells) => Math.ceil(Cells / 4));
        const Bricks = this.BrickExtent;
        const Pass = Encoder.beginComputePass({ label: "Smoke transport", timestampWrites: TimestampWrites });
        Pass.setBindGroup(1, DistanceGroup);
        for (let Tick = 0; Tick < this.TicksPending; ++Tick)
        {
            const Flow = this.VelocityParity, Latest = this.FineParity;
            Pass.setBindGroup(0, this.CommonGroup, [Tick * RecordStride]);
            Pass.setBindGroup(2, this.VelocityGroups[1 - Flow]);
            Pass.setBindGroup(3, this.FineGroups[Latest]);
            Pass.setPipeline(this.ResetProgram);
            Pass.dispatchWorkgroups(1);
            Pass.setPipeline(this.ObstacleProgram);
            Pass.dispatchWorkgroups(...Coarse);
            Pass.setPipeline(this.ForcesProgram);
            Pass.dispatchWorkgroups(...Coarse);
            Pass.setBindGroup(2, this.VelocityGroups[Flow]);
            Pass.setPipeline(this.AdvectCoarseProgram);
            Pass.dispatchWorkgroups(...Coarse);
            Pass.setBindGroup(2, this.VelocityGroups[1 - Flow]);
            Pass.setBindGroup(3, this.PressureGroups[0]);
            Pass.setPipeline(this.DivergenceProgram);
            Pass.dispatchWorkgroups(...Coarse);
            Pass.setPipeline(this.RelaxProgram);
            for (let Sweep = 0; Sweep < this.PressureSweeps; ++Sweep)
            {
                Pass.setBindGroup(3, this.PressureGroups[Sweep & 1]);
                Pass.dispatchWorkgroups(...Coarse);
            }
            Pass.setBindGroup(2, this.VelocityGroups[Flow]);
            Pass.setBindGroup(3, this.PressureGroups[0]);
            Pass.setPipeline(this.ProjectProgram);
            Pass.dispatchWorkgroups(...Coarse);
            Pass.setBindGroup(2, this.VelocityGroups[1 - Flow]);
            Pass.setBindGroup(3, this.FineGroups[Latest]);
            Pass.setPipeline(this.AdvectFineProgram);
            Pass.dispatchWorkgroups(...Bricks);
            Pass.setBindGroup(3, this.LightGroups[1 - Latest]);
            Pass.setPipeline(this.LightProgram);
            Pass.dispatchWorkgroups(...Bricks);
            this.VelocityParity = 1 - Flow;
            this.FineParity = 1 - Latest;
        }
        Pass.end();
        this.TicksPending = 0;
        if (this.RetireAfterRecording)
        {
            this.Resident = false;
            this.RetireAfterRecording = false;
            this.Metrics.Retirements++;
        }
    }

    // 📝 March and composite; returns false (and records nothing) unless smoke is resident and the targets exist.
    RecordComposition(Encoder, Parity, View, TimestampWrites)
    {
        if (!this.Resident || !this.MarchGroups || View.Inspection !== 0)
            return false;
        const Bytes = new ArrayBuffer(ViewExtent);
        const F = new Float32Array(Bytes), I = new Int32Array(Bytes), U = new Uint32Array(Bytes);
        I.set(this.FineOrigin, 0);
        U[3] = this.FineParity;
        U.set(this.Extent, 4);
        U[7] = this.BrickCount;
        U.set(this.BrickExtent, 8);
        U[11] = this.MarchSteps;
        const Gas = this.Gas;
        const Heading = Gas.WindHeading * Math.PI / 180.0;
        F[12] = FineCell;
        F[13] = 2.0 * FineCell;
        F[14] = this.Clock;
        F[15] = SmokeExtinction * Gas.Density;
        F.set([Gas.Albedo, Gas.Albedo, Math.min(0.99, Gas.Albedo * 1.02)], 16);
        F[19] = Gas.Phase;
        F.set([0.07, 0.065, 0.06], 20);
        F[23] = 95.0 * Gas.FlameIntensity;
        F[24] = Gas.DetailScale;
        F[25] = Gas.Detail;
        F[26] = 0.16;
        F[27] = 0.16;
        F.set(this.MarchExtent, 28);
        U[30] = this.MarchScale;
        U[31] = 1;
        F.set([Gas.WindSpeed * Math.cos(Heading), Gas.WindSpeed * Math.sin(Heading), 0.0], 32);
        F[35] = Gas.MultiScatter;
        F[36] = Gas.Erosion;
        F[37] = 0.35;
        F[38] = Gas.Ambient;
        F[39] = Gas.Warp;
        F[40] = Gas.FlameFlicker;
        F[41] = Gas.FlameTemperature;
        F[42] = 1.0;
        F[43] = Gas.Powder;
        F[44] = Gas.FlameTip;
        F[45] = Gas.FlameCore;
        F[46] = 2.0;
        F[47] = 0.55;
        this.Exchange.Device.queue.writeBuffer(this.ViewBuffer, 0, Bytes);
        const Pass = Encoder.beginComputePass({ label: "Smoke march and composite", timestampWrites: TimestampWrites });
        Pass.setPipeline(this.MarchProgram);
        Pass.setBindGroup(0, this.MarchGroups[this.FineParity]);
        Pass.dispatchWorkgroups(Math.ceil(this.MarchExtent[0] / 8), Math.ceil(this.MarchExtent[1] / 8));
        Pass.setPipeline(this.CompositeProgram);
        Pass.setBindGroup(0, this.CompositeGroups[Parity]);
        Pass.dispatchWorkgroups(Math.ceil(this.ScreenExtent[0] / 8), Math.ceil(this.ScreenExtent[1] / 8));
        Pass.end();
        return true;
    }

    // 📝 Drops the plume at once (the next demand acquires a fresh window that reads no history).
    Clear()
    {
        if (this.Resident)
            this.Metrics.Retirements++;
        this.Resident = false;
        this.TicksPending = 0;
    }

    QueryReadout(Thermal)
    {
        const Wheels = ["FL", "FR", "RL", "RR"];
        const Lines = [Thermal.Wheels.map((Wheel, Index) =>
            `${Wheels[Index]} ${(Wheel.Surface - 273.15).toFixed(0).padStart(4)}°C${Wheel.Ignited ? "🔥" : "  "}`).join(" ")];
        Lines.push(Thermal.Wheels.map((Wheel, Index) =>
            `${Wheels[Index]} ${(Wheel.GaugePressure / 1000.0).toFixed(0).padStart(4)} kPa`).join(" "));
        const Window = this.FineOrigin.map((Cell) => (Cell * FineCell).toFixed(0)).join(", ");
        Lines.push(`lattice: ${this.Resident ? "resident" : "idle"} · ${this.Extent.join("×")} cells of ${FineCell * 100} cm (${this.BrickCount} bricks)`);
        Lines.push(`window min: (${Window}) m · ticks ${this.Metrics.Ticks} · relocations ${this.Metrics.Relocations}`);
        Lines.push(`acquisitions ${this.Metrics.Acquisitions} · retirements ${this.Metrics.Retirements} · sources ${this.Metrics.Sources}`);
        return Lines;
    }
}

// 📝 Calibration against Frontier's Drive lighting (DensityExtinction 6 /m). Its sources are floored at 0.6 m in a
//    0.75 m lattice; these are tyre-sized in a 0.25 m one, so the same authored rates yield proportionally less
//    smoke — SmokeYield restores the plume the authored rates describe.
const SmokeExtinction = 6.0;   // [1/m] per unit density at density scale 1 (the detail's erosion thins the edges further)
const SmokeYield      = 2.5;   // [-]   authored density rate to lattice density

function Interpolate(From, To, Fraction)
{
    return [From[0] + (To[0] - From[0]) * Fraction, From[1] + (To[1] - From[1]) * Fraction, From[2] + (To[2] - From[2]) * Fraction];
}

// 📝 A 32³ tileable four-channel detail volume (the sibling Plume's): three octaves of periodic value noise, the first two
//    blended with inverted cellular (Worley) noise for billows, and a fourth value octave for the flame's tongues.
//    Periodic on every axis, so the repeat sampler shows no seam. Built once on acquisition.
function ConstructDetailNoise(Edge)
{
    const Hash = (X, Y, Z, Period, Seed) =>
    {
        let Word = ((((X % Period) + Period) % Period) * 73856093) ^ ((((Y % Period) + Period) % Period) * 19349663)
                 ^ ((((Z % Period) + Period) % Period) * 83492791) ^ Math.imul(Seed, 2654435761);
        Word = Math.imul(Word ^ (Word >>> 15), 0x2C1B3C6D);
        Word = Math.imul(Word ^ (Word >>> 12), 0x297A2D39);
        return ((Word ^ (Word >>> 15)) >>> 0) / 4294967296.0;
    };
    const Smooth = (Fraction) => Fraction * Fraction * (3.0 - 2.0 * Fraction);
    const Value = (X, Y, Z, Period, Seed) =>
    {
        const Scale = Period / Edge;
        const U = X * Scale, V = Y * Scale, W = Z * Scale;
        const I = Math.floor(U), J = Math.floor(V), K = Math.floor(W);
        const A = Smooth(U - I), B = Smooth(V - J), C = Smooth(W - K);
        let Sum = 0.0;
        for (let Corner = 0; Corner < 8; ++Corner)
        {
            const Dx = Corner & 1, Dy = (Corner >> 1) & 1, Dz = (Corner >> 2) & 1;
            Sum += Hash(I + Dx, J + Dy, K + Dz, Period, Seed) * (Dx ? A : 1 - A) * (Dy ? B : 1 - B) * (Dz ? C : 1 - C);
        }
        return Sum;
    };
    const Worley = (X, Y, Z, Period, Seed) =>
    {
        const Scale = Period / Edge;
        const U = X * Scale, V = Y * Scale, W = Z * Scale;
        const I = Math.floor(U), J = Math.floor(V), K = Math.floor(W);
        let Nearest = 9.0;
        for (let Dz = -1; Dz <= 1; ++Dz)
            for (let Dy = -1; Dy <= 1; ++Dy)
                for (let Dx = -1; Dx <= 1; ++Dx)
                {
                    const Cx = I + Dx, Cy = J + Dy, Cz = K + Dz;
                    const Px = Cx + Hash(Cx, Cy, Cz, Period, Seed), Py = Cy + Hash(Cx, Cy, Cz, Period, Seed + 1), Pz = Cz + Hash(Cx, Cy, Cz, Period, Seed + 2);
                    Nearest = Math.min(Nearest, (Px - U) ** 2 + (Py - V) ** 2 + (Pz - W) ** 2);
                }
        return Math.max(0.0, 1.0 - Math.sqrt(Nearest));
    };
    const Bytes = new Uint8Array(Edge * Edge * Edge * 4);
    for (let Z = 0; Z < Edge; ++Z)
        for (let Y = 0; Y < Edge; ++Y)
            for (let X = 0; X < Edge; ++X)
            {
                const Offset = ((Z * Edge + Y) * Edge + X) * 4;
                Bytes[Offset]     = Math.round(255 * (0.55 * Value(X, Y, Z, 4, 1) + 0.45 * Worley(X, Y, Z, 4, 11)));
                Bytes[Offset + 1] = Math.round(255 * (0.6 * Value(X, Y, Z, 8, 2) + 0.4 * Worley(X, Y, Z, 8, 21)));
                Bytes[Offset + 2] = Math.round(255 * Value(X, Y, Z, 16, 3));
                Bytes[Offset + 3] = Math.round(255 * Value(X, Y, Z, 8, 4));
            }
    return Bytes;
}

