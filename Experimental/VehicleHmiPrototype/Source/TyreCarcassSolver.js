//============================================================================================================================================
//                                                         TYRECARCASSSOLVER.JS
//============================================================================================================================================
// 🧩 The XPBD tyre carcass on the GPU: one pneumatic tyre is a 16 × 64 particle lattice with cords, a pressure/volume row and a shape block, stepped in
//    substeps by the engine's six compute kernels, with the hub (rim) driven kinematically by the vehicle and the contact load read back.

// 📝 Provenance. The six WGSL kernels (Shader/Tyre*.wgsl) are the Slate engine's own tyre kernels, lowered from Engine/SlateCompute/Compute/TyreSolver/Shader/*.slang
//    to WGSL (SultanAladin/Slate, branch arena/ca517016-slate, commit 8e799250 + 465b55a2). The lattice (Archive/TyreLattice.bin) is that engine's
//    ConstructTyreLattice built for THIS vehicle's tyre (0.50855 m outer radius, 0.3446 m rim radius, 0.3958 m section, 220 kPa), exported by
//    Tools/TyreLattice. The dispatch order, the 256-byte step blocks, the hub record and the load decode follow the engine's TyreDeviceExchange::Record.
//
// 📝 The coupling rule (TyreSolver.h, ContactLoad). The load crosses back two frames late. The vehicle therefore does not take it as a force but as a
//    one-line implicit spring: the load the tyre last reported, the hub height it was measured at, and the secant stiffness the two imply. The solver
//    refreshes that stiffness every frame; the load lags, the stiffness does not. Readings carries exactly those numbers.
//
// 📝 Frames. The solver works in the lattice's own right-handed, Y-up frame (X forward along the tyre's heading, Z along its axle, Y up). The vehicle is Z-up;
//    MapWorldToCarcass sends (x, y, z) to (x, z, -y) — a pure rotation — and the road is the plane Y = ground height, the effective ground the
//    vehicle's own contact model found under the tyre (its tyre-profile search over the course relief), so ramps and kerbs reach the carcass as height.

const SpanNames   = ["Particles", "Motions", "Velocities", "Rests", "Gradients", "Cords", "CordMultipliers", "Hubs", "Accumulators",
                     "ShapeRows", "ShapeSums", "ShapeMultipliers", "ShapeAdvances", "ShapeMatrix"];   // [-] binding i is span i; binding 14 is the step block
const ReadOnlySpans = new Set([3, 5, 7, 9]);                                                          // Rests, Cords, Hubs, ShapeRows
const StepBinding   = 14;
export const StepStride = 256;   // [B] one step block per dispatch, at the uniform offset alignment
export const AccumulatorStride = 16;   // [-] integers per tyre
const HubStride = 128;           // [B] TyreHubRecord

// 📝 The bindings each kernel declares (the generated WGSL keeps only what it uses); tests/TyreCarcass.test.mjs re-derives them from the WGSL text.
export const KernelBindings = Object.freeze({
    TyrePrediction:        Object.freeze([0, 1, 2, 3, 7, 8, 14]),
    TyreCordProjection:    Object.freeze([0, 5, 6, 14]),
    TyreShapeGather:       Object.freeze([0, 4, 7, 10, 14]),
    TyreShapeSolve:        Object.freeze([9, 10, 11, 12, 13, 14]),
    TyreContactProjection: Object.freeze([0, 1, 2, 4, 7, 8, 12, 14]),
    TyreVelocityRecovery:  Object.freeze([0, 1, 2, 7, 14])
});
const KernelOrder = ["TyrePrediction", "TyreCordProjection", "TyreShapeGather", "TyreShapeSolve", "TyreContactProjection", "TyreVelocityRecovery"];

export const TreadThickness = 0.010;      // [m] TyreSpecification::TreadThickness: the contact stands this far outside the particle
export const LoadFixedScale   = 4096.0;   // [1/N]
export const LengthFixedScale = 1.0e6;    // [1/m]

//------------------------------------------------------------------------------------------------------------------------
//                                                   LATTICE STREAM (pure, testable)
//------------------------------------------------------------------------------------------------------------------------

// 📝 The stream: a little-endian u32 JSON length, the JSON header, then the fourteen record arrays in binding order, each as the host holds it.
export function ParseTyreLattice(Bytes)
{
    const View = new DataView(Bytes.buffer, Bytes.byteOffset, Bytes.byteLength);
    const Length = View.getUint32(0, true);
    const Header = JSON.parse(new TextDecoder().decode(Bytes.subarray(4, 4 + Length)));
    let Cursor = 4 + Length;
    const Take = (Count) => { const Out = Bytes.slice(Cursor, Cursor + Count); Cursor += Count; return Out; };
    const P = Header.ParticleCount, C = Header.CordCount, R = Header.ShapeRowLimit;
    Header.Arrays = {
        Particles: Take(P * 16), Motions: Take(P * 16), Velocities: Take(P * 16), Rests: Take(P * 16), Gradients: Take(P * 48),
        Cords: Take(C * 16), CordMultipliers: Take(C * 4), Hubs: Take(HubStride), Accumulators: Take(Header.AccumulatorStride * 4),
        ShapeRows: Take(R * 16), ShapeSums: Take(Header.ShapeSumStride * 4), ShapeMultipliers: Take(R * 4), ShapeAdvances: Take(R * 4),
        ShapeMatrix: Take(Header.ShapeMatrixStride * 4)
    };
    if (Cursor !== Bytes.length)
        throw new Error(`tyre lattice stream length mismatch: ${Cursor} of ${Bytes.length}`);
    return Header;
}

// 📝 Tiles the one exported tyre into Count tyres: particle indices offset per tyre, cord colour runs become colour-major and tyre-minor.
export function TileTyreLattice(One, Count)
{
    const P = One.ParticleCount, C = One.CordCount, T = Count;
    const Repeat = (Source) => { const Out = new Uint8Array(Source.length * T); for (let Tyre = 0; Tyre < T; ++Tyre) Out.set(Source, Tyre * Source.length); return Out; };
    const Rests = Repeat(One.Arrays.Rests), RestView = new Float32Array(Rests.buffer);
    for (let Tyre = 0; Tyre < T; ++Tyre)
        for (let Particle = 0; Particle < P; ++Particle)
            RestView[(Tyre * P + Particle) * 4 + 3] = Tyre;
    const Cords = new Uint8Array(C * 16 * T), CordWords = new Uint32Array(Cords.buffer);
    const Source = new Uint32Array(One.Arrays.Cords.buffer, One.Arrays.Cords.byteOffset, C * 4);
    const Colours = [];
    let Written = 0;
    for (const [Offset, Length] of One.Colours)
    {
        Colours.push([Written, Length * T]);
        for (let Tyre = 0; Tyre < T; ++Tyre)
            for (let Cord = 0; Cord < Length; ++Cord)
            {
                const From = (Offset + Cord) * 4, To = Written * 4;
                CordWords[To] = Source[From] + Tyre * P; CordWords[To + 1] = Source[From + 1] + Tyre * P;
                CordWords[To + 2] = Source[From + 2];    CordWords[To + 3] = Source[From + 3];
                ++Written;
            }
    }
    return {
        Particles: Repeat(One.Arrays.Particles), Motions: Repeat(One.Arrays.Motions), Velocities: Repeat(One.Arrays.Velocities), Rests,
        Gradients: Repeat(One.Arrays.Gradients), Cords, CordMultipliers: Repeat(One.Arrays.CordMultipliers), Hubs: Repeat(One.Arrays.Hubs),
        Accumulators: Repeat(One.Arrays.Accumulators), ShapeRows: Repeat(One.Arrays.ShapeRows), ShapeSums: Repeat(One.Arrays.ShapeSums),
        ShapeMultipliers: Repeat(One.Arrays.ShapeMultipliers), ShapeAdvances: Repeat(One.Arrays.ShapeAdvances), ShapeMatrix: Repeat(One.Arrays.ShapeMatrix),
        Colours
    };
}

export function RotateByQuaternion(Q, X, Y, Z)
{
    const [Qx, Qy, Qz, Qw] = Q;
    const Tx = 2 * (Qy * Z - Qz * Y), Ty = 2 * (Qz * X - Qx * Z), Tz = 2 * (Qx * Y - Qy * X);
    return [X + Qw * Tx + (Qy * Tz - Qz * Ty), Y + Qw * Ty + (Qz * Tx - Qx * Tz), Z + Qw * Tz + (Qx * Ty - Qy * Tx)];
}

export function MultiplyQuaternions(A, B)
{
    return [A[3] * B[0] + A[0] * B[3] + A[1] * B[2] - A[2] * B[1], A[3] * B[1] - A[0] * B[2] + A[1] * B[3] + A[2] * B[0],
            A[3] * B[2] + A[0] * B[1] - A[1] * B[0] + A[2] * B[3], A[3] * B[3] - A[0] * B[0] - A[1] * B[1] - A[2] * B[2]];
}

// 📝 Places one tyre rigidly at a pose on the host copy before upload: particles and their substep-start positions move together, velocities become the rim's.
export function PlaceTyreRigid(Tiled, One, Index, Hub)
{
    const P = One.ParticleCount;
    const Particles = new Float32Array(Tiled.Particles.buffer, Index * P * 16, P * 4), Motions = new Float32Array(Tiled.Motions.buffer, Index * P * 16, P * 4);
    const Velocities = new Float32Array(Tiled.Velocities.buffer, Index * P * 16, P * 4), Rests = new Float32Array(Tiled.Rests.buffer, Index * P * 16, P * 4);
    for (let Particle = 0; Particle < P; ++Particle)
    {
        const World = RotateByQuaternion(Hub.q, Rests[Particle * 4], Rests[Particle * 4 + 1], Rests[Particle * 4 + 2]);
        for (let Axis = 0; Axis < 3; ++Axis)
        {
            Particles[Particle * 4 + Axis] = [Hub.x, Hub.y, Hub.z][Axis] + World[Axis];
            Motions[Particle * 4 + Axis] = Particles[Particle * 4 + Axis];
        }
        Motions[Particle * 4 + 3] = 0;
        Velocities.set([Hub.vx || 0, Hub.vy || 0, Hub.vz || 0, 0], Particle * 4);
    }
}

// 📝 TyreHubRecord: begin pose, end pose, end velocities, then the inflation and ground scalars the construction filled and this leaves alone except for
//    the ground height and the pressure.
export function WriteHubRecord(Hubs, Index, Begin, End, GroundHeight)
{
    const F = new Float32Array(Hubs.buffer, Hubs.byteOffset + Index * HubStride, 32);
    F[0] = Begin.x; F[1] = Begin.y; F[2] = Begin.z; F[3] = Begin.q[0]; F[4] = Begin.q[1]; F[5] = Begin.q[2]; F[6] = Begin.q[3];
    F[7] = End.x;   F[8] = End.y;   F[9] = End.z;   F[10] = End.q[0];  F[11] = End.q[1];  F[12] = End.q[2];  F[13] = End.q[3];
    F[14] = End.vx || 0; F[15] = End.vy || 0; F[16] = End.vz || 0; F[17] = End.wx || 0; F[18] = End.wy || 0; F[19] = End.wz || 0;
    F[23] = GroundHeight;
}

// 📝 DeclareInflation's rule: α = V₀ / (γ p_abs), the volume row's target V₀ + α p₀, written to the hub and the row.
export function DeclareInflation(Hubs, ShapeRows, ShapeRowLimit, Index, GaugePressure)
{
    const Hub = new Float32Array(Hubs.buffer, Hubs.byteOffset + Index * HubStride, 32);
    const Row = new Float32Array(ShapeRows.buffer, ShapeRows.byteOffset + Index * ShapeRowLimit * 16, 4);
    const RestVolume = Hub[20], Gamma = Hub[29], Ambient = Hub[30];
    const Compliance = RestVolume / (Gamma * (GaugePressure + Ambient));
    Hub[21] = GaugePressure; Hub[22] = Compliance;
    Row[0] = RestVolume + Compliance * GaugePressure; Row[1] = Compliance;
}

// 📝 The sixteen integers the contact kernel accumulates per tyre, decoded as DecodeContactLoad does. Forces are frame means, in the lattice frame.
export function DecodeAccumulators(Ints, Offset = 0)
{
    const Load = (Slot) => Ints[Offset + Slot] / LoadFixedScale, Length = (Slot) => Ints[Offset + Slot] / LengthFixedScale;
    const Contacts = Math.max(0, Ints[Offset + 6]);
    return {
        Force: [Load(0), Load(1), Load(2)], Torque: [Load(3), Load(4), Load(5)], Contacts,
        PatchLength: Contacts > 0 ? Length(8) - Length(7) : 0.0, PatchWidth: Contacts > 0 ? Length(10) - Length(9) : 0.0, Penetration: Length(11)
    };
}

// 📝 The vehicle's Z-up world to the carcass's Y-up frame: a rotation of −90° about X, (x, y, z) ↦ (x, z, −y), so handedness is kept.
export const MapWorldToCarcass = (P) => [P[0], P[2], -P[1]];

// 📝 One wheel's hub as the carcass wants it, from the vehicle's own quantities: the hub centre and the road under it (world), the wheel's heading (yaw of its forward
//    axis about +Z), its spin angle and rate about the axle (+Y of the wheel; forward rolling is positive), the hub's velocity, and the body's yaw rate. The lattice spins about its
//    own +Z, which is the wheel's −Y, hence the sign flips; a tyre rolling forward turns at −v/R about +Z, as the engine's own proof drives it.
export function ResolveCarcassPose(Hub, Heading, SpinAngle, Velocity, SpinRate, YawRate)
{
    const Position = MapWorldToCarcass(Hub);
    const Yaw = [0, Math.sin(Heading / 2), 0, Math.cos(Heading / 2)];                    // about the carcass Y: the world yaw about +Z
    const Spin = [0, 0, Math.sin(-SpinAngle / 2), Math.cos(-SpinAngle / 2)];            // about the lattice Z axle
    const Rotation = MultiplyQuaternions(Yaw, Spin);
    const Linear = MapWorldToCarcass(Velocity);
    const Axle = RotateByQuaternion(Yaw, 0, 0, 1);                                       // the lattice axle in the carcass frame
    return { x: Position[0], y: Position[1], z: Position[2], q: Rotation, vx: Linear[0], vy: Linear[1], vz: Linear[2],
             wx: Axle[0] * -SpinRate, wy: YawRate, wz: Axle[2] * -SpinRate };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE SOLVER
//------------------------------------------------------------------------------------------------------------------------

export class TyreCarcassSolver
{
    // 📝 Exchange: DeviceExchange; Shaders: ShaderInterchange; TyreCount: wheels simulated together; Substeps: per frame (32, the engine's own, with its 3× mass scale;
    //    the load is NOT converged below that: at 16 the carcass balloons, a 10 mm squeeze reads 4.96 kN with an 18 cm wide patch where 32 reads 3.46 kN and 7 cm).
    constructor(Exchange, Shaders, TyreCount = 4, Substeps = 32)
    {
        this.Exchange = Exchange; this.Shaders = Shaders;
        this.TyreCount = TyreCount; this.Substeps = Substeps;
        this.FrameIndex = 0;
        this.Readings = Array.from({ length: TyreCount }, () => ({ Valid: false, Load: 0.0, Lateral: 0.0, Longitudinal: 0.0, PatchLength: 0.0, PatchWidth: 0.0,
                                                                   Contacts: 0, Penetration: 0.0, HubHeight: 0.0, Ground: 0.0, Deflection: 0.0, Secant: 0.0, Frame: -1 }));
        this.Statistics = { Dispatches: 0, Readbacks: 0, LastSubmitMilliseconds: 0.0 };
        this.PriorEnd = new Array(TyreCount).fill(null);
        this.GaugeApplied = new Array(TyreCount).fill(NaN);
        this.Ready = false;
    }

    async ConstructResources(LatticeBytes, Hubs = null)
    {
        const Device = this.Exchange.Device;
        this.One = ParseTyreLattice(LatticeBytes);
        this.Tiled = TileTyreLattice(this.One, this.TyreCount);
        // 📝 Initial placement: every tyre rigid at its first pose; the host's first Record re-places them where the vehicle actually stands.
        if (Hubs)
            Hubs.forEach((Hub, Index) => PlaceTyreRigid(this.Tiled, this.One, Index, Hub));
        this.Spans = SpanNames.map((Name) =>
        {
            const Bytes = this.Tiled[Name];
            const Buffer = Device.createBuffer({ label: `Tyre ${Name}`, size: Bytes.byteLength, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST | GPUBufferUsage.COPY_SRC });
            Device.queue.writeBuffer(Buffer, 0, Bytes);
            return Buffer;
        });
        this.HubBytes = this.Tiled.Hubs;
        this.ShapeRowBytes = this.Tiled.ShapeRows;
        this.Colours = this.Tiled.Colours;
        // 📝 The contact stands TreadThickness outside the particles (TyreSpecification), so the lattice is built 10 mm inside the vehicle's tyre radius and the rolling
        //    surface is exactly VehicleGeometry.TyreRadius.
        this.OuterRadius = this.One.OuterRadius + TreadThickness;
        this.Programs = [];
        await this.Shaders.RetrieveSources(KernelOrder.map((Name) => `${Name}.wgsl`));
        for (const Name of KernelOrder)
        {
            const Bindings = KernelBindings[Name];
            const Layout = Device.createBindGroupLayout({ label: `${Name} layout`, entries: Bindings.map((Binding) => ({
                binding: Binding, visibility: GPUShaderStage.COMPUTE,
                buffer: Binding === StepBinding ? { type: "uniform", hasDynamicOffset: true, minBindingSize: 48 } : { type: ReadOnlySpans.has(Binding) ? "read-only-storage" : "storage" } })) });
            const Pipeline = await this.Shaders.ConstructComputeProgram(`${Name}.wgsl`, "main", [Layout]);
            this.Programs.push({ Name, Bindings, Layout, Pipeline, Group: null });
        }
        this.Readback = [0, 1, 2].map(() => ({ Buffer: Device.createBuffer({ label: "Tyre loads", size: this.TyreCount * AccumulatorStride * 4, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST }),
                                                Busy: false, Pending: false, Frame: -1, Hub: new Float32Array(this.TyreCount), Ground: new Float32Array(this.TyreCount) }));
        this.LayoutSteps(1.0 / 60.0);
        this.Ready = true;
    }

    // 📝 The step blocks of one frame: per substep prediction, the cords by colour (no two share a particle), gather, solve, contact, recovery. Rewritten each frame with the
    //    frame's own length so the carcass keeps pace with a render rate that is not sixty hertz.
    LayoutSteps(FrameSeconds)
    {
        const Device = this.Exchange.Device, One = this.One, S = this.Substeps, T = this.TyreCount;
        const PerSubstep = 1 + this.Colours.length + 3 + 1;
        this.Dispatches = [];
        const Blocks = new ArrayBuffer(S * PerSubstep * StepStride);
        const U32 = new Uint32Array(Blocks), F32 = new Float32Array(Blocks);
        const P = One.ParticleCount * T, Groups = (N) => Math.ceil(N / One.GroupExtent);
        let K = 0;
        const Push = (Program, Substep, CordOffset, CordCount, GroupCount) =>
        {
            const O = K * StepStride / 4;
            U32[O] = P; U32[O + 1] = One.Rings; U32[O + 2] = One.Segments; U32[O + 3] = T;
            F32[O + 4] = FrameSeconds / S; U32[O + 5] = Substep; U32[O + 6] = S; U32[O + 7] = 0;
            U32[O + 8] = Substep; U32[O + 9] = CordOffset; U32[O + 10] = CordCount;
            this.Dispatches.push({ Program, Offset: K * StepStride, Groups: GroupCount });
            ++K;
        };
        for (let Substep = 0; Substep < S; ++Substep)
        {
            Push(0, Substep, 0, 0, Groups(P));
            for (const [Offset, Count] of this.Colours) Push(1, Substep, Offset, Count, Groups(Count));
            Push(2, Substep, 0, 0, Groups(P));
            Push(3, Substep, 0, 0, T);
            Push(4, Substep, 0, 0, Groups(P));
            Push(5, Substep, 0, 0, Groups(P));
        }
        if (!this.StepBuffer || this.StepBuffer.size !== Blocks.byteLength)
        {
            if (this.StepBuffer) this.StepBuffer.destroy();
            this.StepBuffer = Device.createBuffer({ label: "Tyre step blocks", size: Blocks.byteLength, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
            for (const Program of this.Programs)
                Program.Group = Device.createBindGroup({ layout: Program.Layout, entries: Program.Bindings.map((Binding) => ({
                    binding: Binding, resource: Binding === StepBinding ? { buffer: this.StepBuffer, size: 48 } : { buffer: this.Spans[Binding] } })) });
        }
        Device.queue.writeBuffer(this.StepBuffer, 0, Blocks);
        this.StepSeconds = FrameSeconds;
        this.Statistics.Dispatches = this.Dispatches.length;
    }

    // 📝 Re-places one tyre rigidly at a pose (a respawn, a teleport, the first frame): uploads its particles, motions and velocities.
    Place(Index, Hub, GaugePressure)
    {
        PlaceTyreRigid(this.Tiled, this.One, Index, Hub);
        const Device = this.Exchange.Device, P = this.One.ParticleCount;
        for (const Name of ["Particles", "Motions", "Velocities"])
            Device.queue.writeBuffer(this.Spans[SpanNames.indexOf(Name)], Index * P * 16, this.Tiled[Name], Index * P * 16, P * 16);
        this.DeclareGauge(Index, GaugePressure, true);
        this.PriorEnd[Index] = null;
        this.Readings[Index].Valid = false;
    }

    DeclareGauge(Index, GaugePressure, Force = false)
    {
        if (!Force && Math.abs(GaugePressure - this.GaugeApplied[Index]) < 200.0) return;
        this.GaugeApplied[Index] = GaugePressure;
        DeclareInflation(this.HubBytes, this.ShapeRowBytes, this.One.ShapeRowLimit, Index, GaugePressure);
        const Device = this.Exchange.Device;
        Device.queue.writeBuffer(this.Spans[SpanNames.indexOf("ShapeRows")], 0, this.ShapeRowBytes);
    }

    // 📝 One frame: writes every hub record (begin = the previous frame's end), then records the substep dispatches and the load copy.
    //    States[i] = { End: pose (ResolveCarcassPose), Ground: height of the road under the hub (carcass Y), Gauge: Pa, Active: bool }.
    Record(Encoder, States, FrameSeconds, TimestampWrites = undefined)
    {
        if (!this.Ready) return false;
        const Device = this.Exchange.Device;
        const Seconds = Math.min(1.0 / 30.0, Math.max(1.0 / 240.0, FrameSeconds));
        if (Math.abs(Seconds - this.StepSeconds) > 1.0e-5) this.LayoutSteps(Seconds);
        States.forEach((State, Index) =>
        {
            if (!State) return;
            const Prior = this.PriorEnd[Index];
            // 📝 A hub that jumped further than a frame of driving can carry it (a respawn, a recovery) has left its carcass behind: place the tyre again.
            const Jumped = Prior && Math.hypot(State.End.x - Prior.x, State.End.y - Prior.y, State.End.z - Prior.z) > 1.5;
            if (!Prior || State.Reset || Jumped) this.Place(Index, State.End, State.Gauge);
            this.DeclareGauge(Index, State.Gauge);
            const Begin = this.PriorEnd[Index] || State.End;
            WriteHubRecord(this.HubBytes, Index, Begin, State.End, State.Ground);
            this.PriorEnd[Index] = State.End;
        });
        Device.queue.writeBuffer(this.Spans[SpanNames.indexOf("Hubs")], 0, this.HubBytes);

        const Pass = Encoder.beginComputePass({ label: "Tyre carcass", timestampWrites: TimestampWrites });
        let Current = -1;
        for (const Dispatch of this.Dispatches)
        {
            const Program = this.Programs[Dispatch.Program];
            if (Current !== Dispatch.Program) { Pass.setPipeline(Program.Pipeline); Current = Dispatch.Program; }
            Pass.setBindGroup(0, Program.Group, [Dispatch.Offset]);
            Pass.dispatchWorkgroups(Dispatch.Groups);
        }
        Pass.end();

        const Slot = this.Readback[this.FrameIndex % 3];
        if (!Slot.Busy)
        {
            Encoder.copyBufferToBuffer(this.Spans[SpanNames.indexOf("Accumulators")], 0, Slot.Buffer, 0, Slot.Buffer.size);
            Slot.Busy = true; Slot.Pending = true; Slot.Frame = this.FrameIndex;
            States.forEach((State, Index) => { Slot.Hub[Index] = State ? State.End.y : 0.0; Slot.Ground[Index] = State ? State.Ground : 0.0; });
        }
        this.FrameIndex++;
        return true;
    }

    // 📝 After the frame's submit: maps the slots whose copy was recorded, decodes them when the device is done, keeps the hub height each was measured at.
    Harvest()
    {
        for (const Slot of this.Readback)
        {
            if (!Slot.Pending) continue;
            Slot.Pending = false;
            Slot.Buffer.mapAsync(GPUMapMode.READ).then(() =>
            {
                const Ints = new Int32Array(Slot.Buffer.getMappedRange().slice(0));
                Slot.Buffer.unmap(); Slot.Busy = false;
                for (let Index = 0; Index < this.TyreCount; ++Index)
                    this.Accept(Index, DecodeAccumulators(Ints, Index * AccumulatorStride), Slot.Hub[Index], Slot.Ground[Index], Slot.Frame);
                this.Statistics.Readbacks++;
            }).catch(() => { Slot.Busy = false; });
        }
    }

    // 📝 Turns a decoded load into the vehicle's coupling numbers. The secant stiffness is only formed where it is well conditioned: a tyre that is in contact
    //    with a real deflection under a real load; elsewhere Valid is false and the vehicle keeps its analytic carcass.
    Accept(Index, Decoded, HubHeight, Ground, Frame)
    {
        const Reading = this.Readings[Index];
        const Deflection = Math.max(0.0, this.OuterRadius + Ground - HubHeight);
        const Load = Decoded.Force[1];
        const Finite = Number.isFinite(Load) && Number.isFinite(Decoded.PatchLength);
        Object.assign(Reading, { Load: Finite ? Load : 0.0, Lateral: Decoded.Force[2], Longitudinal: Decoded.Force[0], PatchLength: Decoded.PatchLength,
                                 PatchWidth: Decoded.PatchWidth, Contacts: Decoded.Contacts, Penetration: Decoded.Penetration, HubHeight: HubHeight, Ground,
                                 Deflection, Frame });
        Reading.Valid = Finite && Decoded.Contacts > 0 && Load > 50.0 && Deflection > 0.002;
        Reading.Secant = Reading.Valid ? Load / Deflection : 0.0;
    }
}
