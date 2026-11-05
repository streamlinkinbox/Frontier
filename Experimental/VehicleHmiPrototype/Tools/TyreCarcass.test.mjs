//============================================================================================================================================
//                                                         TYRECARCASS.TEST.MJS
//============================================================================================================================================
// 🧩 The XPBD tyre carcass. Pure tests (lattice stream, tiling, kernel bindings, frame mapping, load decode), the vehicle-side coupling law driven
//    by a surrogate carcass with the engine's two-frame latency (CPU only, seconds), and — opt-in with RUN_GPU=1 — the six kernels executing on a
//    software Vulkan device: load versus deflection and pressure, and the whole app with the carcass coupled to the vehicle.

import test from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { ParseTyreLattice, TileTyreLattice, KernelBindings, ResolveCarcassPose, RotateByQuaternion, DecodeAccumulators, MapWorldToCarcass,
         WriteHubRecord, DeclareInflation, TreadThickness, TyreCarcassSolver } from "../Source/TyreCarcassSolver.js";
import { VehicleSolver } from "../Source/VehicleSolver.js";
import { VehicleGeometry } from "../Source/VehicleSpecification.js";

const Here = path.dirname(fileURLToPath(import.meta.url));
const Root = path.resolve(Here, "..");
const Lattice = new Uint8Array(fs.readFileSync(path.join(Root, "Archive/TyreLattice.bin")));
const Enabled = process.env.RUN_GPU === "1";

test("lattice stream: the shipped lattice is this vehicle's tyre and the stream is consumed exactly", () =>
{
    const One = ParseTyreLattice(Lattice);   // throws unless every byte is accounted for
    assert.equal(One.Rings, 16); assert.equal(One.Segments, 64); assert.equal(One.ParticleCount, 1024); assert.equal(One.CordCount, 3712);
    // 📝 The contact stands TreadThickness outside the particles: the rolling radius is the vehicle's tyre radius, the rim is the vehicle's rim.
    assert.ok(Math.abs(One.OuterRadius + TreadThickness - VehicleGeometry.TyreRadius) < 1.0e-6, `rolling radius ${One.OuterRadius + TreadThickness} vs ${VehicleGeometry.TyreRadius}`);
    assert.ok(Math.abs(One.RimRadius - VehicleGeometry.TyreRimRadius) < 1.0e-6);
    assert.equal(One.Colours.reduce((Sum, [, Count]) => Sum + Count, 0), One.CordCount, "the colour runs partition the cords");
    assert.equal(One.LoadFixedScale, 4096.0); assert.equal(One.LengthFixedScale, 1.0e6);
});

test("tiling: tyres are independent copies, cords are offset per tyre and no two cords of a colour share a particle", () =>
{
    const One = ParseTyreLattice(Lattice), T = 4, Tiled = TileTyreLattice(One, T), P = One.ParticleCount;
    assert.equal(Tiled.Particles.byteLength, P * 16 * T);
    const Rests = new Float32Array(Tiled.Rests.buffer);
    for (let Tyre = 0; Tyre < T; ++Tyre) assert.equal(Rests[(Tyre * P + 17) * 4 + 3], Tyre, "each particle carries its tyre index");
    const Words = new Uint32Array(Tiled.Cords.buffer);
    let Total = 0;
    for (const [Offset, Count] of Tiled.Colours)
    {
        const Seen = new Set();
        for (let Cord = Offset; Cord < Offset + Count; ++Cord)
            for (const Particle of [Words[Cord * 4], Words[Cord * 4 + 1]])
            {
                assert.ok(Particle < P * T, "cord endpoints stay inside the fleet");
                assert.ok(!Seen.has(Particle), `colour run at ${Offset}: particle ${Particle} appears twice`);
                Seen.add(Particle);
            }
        Total += Count;
    }
    assert.equal(Total, One.CordCount * T);
    assert.equal(new Set(Array.from({ length: T }, (_, Tyre) => Math.floor(Words[Tiled.Colours[0][0] * 4 + Tyre] / P))).size > 0, true);
});

test("kernel bindings: the declared binding sets are exactly what the generated WGSL uses", () =>
{
    for (const [Name, Declared] of Object.entries(KernelBindings))
    {
        const Source = fs.readFileSync(path.join(Root, "Shader", `${Name}.wgsl`), "utf8");
        const Used = [...new Set([...Source.matchAll(/@binding\((\d+)\) @group\(0\)/g)].map((Match) => Number(Match[1])))].sort((A, B) => A - B);
        assert.deepEqual([...Declared], Used, `${Name}`);
        assert.match(Source, /@compute\s*\n@workgroup_size\(128, 1, 1\)\s*\nfn main\(/, `${Name} keeps the engine's entry point and group extent`);
    }
});

test("frame mapping: the lattice rides the wheel (rest points land where the vehicle's wheel puts them, spin rolls forward)", () =>
{
    // carcass → world is the inverse of (x, y, z) → (x, z, −y): (x, y, z) ↦ (x, −z, y)
    const ToWorld = (P) => [P[0], -P[2], P[1]];
    const Hub = [12.0, -3.0, 0.5], Heading = 0.6;
    const Pose = ResolveCarcassPose(Hub, Heading, 0.0, [0, 0, 0], 0.0, 0.0);
    const Place = (Rest) => { const World = RotateByQuaternion(Pose.q, ...Rest); return ToWorld([Pose.x + World[0], Pose.y + World[1], Pose.z + World[2]]); };
    const Front = Place([0.5, 0, 0]);
    assert.ok(Math.abs(Front[0] - (Hub[0] + 0.5 * Math.cos(Heading))) < 1e-9 && Math.abs(Front[1] - (Hub[1] + 0.5 * Math.sin(Heading))) < 1e-9 && Math.abs(Front[2] - Hub[2]) < 1e-9, "tread front point");
    const Top = Place([0, 0.5, 0]);
    assert.ok(Math.abs(Top[0] - Hub[0]) < 1e-9 && Math.abs(Top[2] - (Hub[2] + 0.5)) < 1e-9, "the lattice's up is the world's up");
    const Axle = Place([0, 0, 0.2]);   // lattice +Z is the wheel's −Y: to the right of a forward-facing wheel
    assert.ok(Math.abs(Axle[0] - (Hub[0] + 0.2 * Math.sin(Heading))) < 1e-9 && Math.abs(Axle[1] - (Hub[1] - 0.2 * Math.cos(Heading))) < 1e-9, "axle direction");
    // a wheel that has rolled forward (positive spin about its +Y) carries the top of the tread forward
    const Rolled = ResolveCarcassPose(Hub, 0.0, 0.5, [0, 0, 0], 0.0, 0.0);
    const World = RotateByQuaternion(Rolled.q, 0, 0.5, 0);
    assert.ok(ToWorld([World[0], World[1], World[2]])[0] > 0.2, "forward spin moves the top of the tyre forward");
    assert.deepEqual(MapWorldToCarcass([1, 2, 3]), [1, 3, -2]);
});

test("hub record and inflation: poses land in the record and the pressure row follows α = V₀ / (γ p_abs)", () =>
{
    const One = ParseTyreLattice(Lattice), Tiled = TileTyreLattice(One, 2);
    const Begin = { x: 1, y: 2, z: 3, q: [0, 0, 0, 1] }, End = { x: 4, y: 5, z: 6, q: [0, 0.1, 0, 0.995], vx: 7, vy: 8, vz: 9, wx: 1, wy: 2, wz: 3 };
    WriteHubRecord(Tiled.Hubs, 1, Begin, End, 0.25);
    const F = new Float32Array(Tiled.Hubs.buffer, 128, 32);
    assert.deepEqual([F[0], F[1], F[2]], [1, 2, 3]); assert.deepEqual([F[7], F[8], F[9]], [4, 5, 6]); assert.deepEqual([F[14], F[15], F[16]], [7, 8, 9]);
    assert.equal(F[23], 0.25);
    DeclareInflation(Tiled.Hubs, Tiled.ShapeRows, One.ShapeRowLimit, 1, 150000.0);
    const Expected = F[20] / (F[29] * (150000.0 + F[30]));
    assert.ok(Math.abs(F[22] - Expected) / Expected < 1e-5 && F[21] === 150000.0, "compliance and gauge");
    const Row = new Float32Array(Tiled.ShapeRows.buffer, One.ShapeRowLimit * 16, 2);
    assert.ok(Math.abs(Row[0] - (F[20] + F[22] * 150000.0)) < 1e-6, "the volume row's target");
});

test("load decode: forces are fixed point, the patch is the extent of the contacts", () =>
{
    const Ints = new Int32Array(16);
    Ints[0] = Math.round(120.5 * 4096); Ints[1] = Math.round(3200.0 * 4096); Ints[2] = Math.round(-45.25 * 4096); Ints[6] = 250;
    Ints[7] = Math.round(-0.11 * 1e6); Ints[8] = Math.round(0.09 * 1e6); Ints[9] = Math.round(-0.06 * 1e6); Ints[10] = Math.round(0.07 * 1e6); Ints[11] = 2000;
    const Decoded = DecodeAccumulators(Ints);
    assert.ok(Math.abs(Decoded.Force[1] - 3200.0) < 1e-3 && Math.abs(Decoded.Force[2] + 45.25) < 1e-3);
    assert.ok(Math.abs(Decoded.PatchLength - 0.20) < 1e-6 && Math.abs(Decoded.PatchWidth - 0.13) < 1e-6 && Math.abs(Decoded.Penetration - 0.002) < 1e-9);
    assert.equal(DecodeAccumulators(new Int32Array(16)).PatchLength, 0.0, "no contacts, no patch");
});

test("readings are only formed where the secant is well conditioned", () =>
{
    const Solver = Object.create(TyreCarcassSolver.prototype);
    Solver.OuterRadius = 0.50855; Solver.Readings = [{}];
    const Accept = (Load, Hub, Contacts = 100, Patch = 0.2) => { Solver.Accept(0, { Force: [0, Load, 0], PatchLength: Patch, PatchWidth: 0.1, Contacts, Penetration: 0 }, Hub, 0.0, 7); return Solver.Readings[0]; };
    const Good = Accept(3200.0, 0.50855 - 0.018);
    assert.ok(Good.Valid && Math.abs(Good.Deflection - 0.018) < 1e-9 && Math.abs(Good.Secant - 3200 / 0.018) < 1e-3);
    assert.ok(!Accept(3200.0, 0.50855 - 0.001).Valid, "a hair of deflection gives no usable secant");
    assert.ok(!Accept(20.0, 0.50855 - 0.018).Valid, "a feather load gives none either");
    assert.ok(!Accept(3200.0, 0.50855 - 0.018, 0).Valid, "no contacts, airborne");
    assert.ok(!Accept(Number.NaN, 0.50855 - 0.018).Valid && Solver.Readings[0].Load === 0.0, "a non-finite load is rejected and zeroed");
});

//------------------------------------------------------------------------------------------------------------------------
//   The vehicle-side coupling, CPU only: a surrogate carcass built from the measured XPBD sweep (220 kPa, 32 substeps) with the
//   two-frame latency of the readback, so the law, its clamps and its smoothing are exercised without a GPU.
//------------------------------------------------------------------------------------------------------------------------

const SweepMillimetres = [0, 1, 2, 4, 6, 8, 10, 14, 20, 30], SweepNewtons = [137, 233, 330, 545, 830, 1101, 1446, 2303, 3573, 6308];
function SurrogateLoad(Deflection, Gauge)
{
    const Millimetres = Deflection * 1000.0;
    let Load = SweepNewtons[SweepNewtons.length - 1] + (Millimetres - 30.0) * 280.0;
    for (let Index = 0; Index + 1 < SweepMillimetres.length; ++Index)
        if (Millimetres <= SweepMillimetres[Index + 1])
        {
            const U = Math.max(0.0, (Millimetres - SweepMillimetres[Index]) / (SweepMillimetres[Index + 1] - SweepMillimetres[Index]));
            Load = SweepNewtons[Index] + U * (SweepNewtons[Index + 1] - SweepNewtons[Index]);
            break;
        }
    return Millimetres <= 0.0 ? 0.0 : Load * Math.pow(Math.max(0.05, Gauge / 220000.0), 1.1);
}

class SurrogateCarcass
{
    constructor(Latency = 2) { this.Readings = [0, 1, 2, 3].map(() => ({ Valid: false, Load: 0, Deflection: 0, Secant: 0, PatchLength: 0, PatchWidth: 0, Frame: -1 })); this.History = [0, 1, 2, 3].map(() => []); this.Latency = Latency; this.Frame = 0; }
    // called once per rendered frame with the vehicle's state: queues the new measurement, publishes the one Latency frames old
    Publish(Vehicle)
    {
        Vehicle.Wheels.forEach((Wheel, Index) =>
        {
            const Queue = this.History[Index];
            Queue.push({ Deflection: Wheel.Contact ? Wheel.TyreDeflection : 0.0, Gauge: Wheel.Pressure });
            if (Queue.length <= this.Latency) return;
            const Measured = Queue.shift(), Load = SurrogateLoad(Measured.Deflection, Measured.Gauge), Reading = this.Readings[Index];
            Object.assign(Reading, { Load, Deflection: Measured.Deflection, Frame: this.Frame, PatchLength: 0.2, PatchWidth: 0.1 });
            Reading.Valid = Load > 50.0 && Measured.Deflection > 0.002;
            Reading.Secant = Reading.Valid ? Load / Measured.Deflection : 0.0;
        });
        this.Frame++;
    }
}

const Idle = { Throttle: 0.0, Brake: 0.0, Steer: 0.0, Handbrake: false };
const Pad = { Position: [-100.0, -34.0, 0.53], Yaw: 0.0 };

function Rig(Carcass)
{
    const Vehicle = new VehicleSolver([]);
    Vehicle.Respawn(Pad.Position, Pad.Yaw);
    return { Vehicle, Carcass };
}

function Run(Rig, Seconds, Command = () => Idle, Corrupt = null)
{
    const Trace = [];
    const Steps = Math.round(Seconds * 120);
    for (let Step = 0; Step < Steps; ++Step)
    {
        if (Rig.Carcass) Rig.Vehicle.AssignCarcass(Rig.Carcass.Readings);
        Rig.Vehicle.Advance(Command(Step / 120), 1 / 120);
        if (Step % 2 === 1 && Rig.Carcass)
        {
            Rig.Carcass.Publish(Rig.Vehicle);
            if (Corrupt) Corrupt(Rig.Carcass, Step);
        }
        Trace.push({ Z: Rig.Vehicle.Position[2], Loads: Rig.Vehicle.Wheels.map((Wheel) => Wheel.Load), Defl: Rig.Vehicle.Wheels.map((Wheel) => Wheel.TyreDeflection), Speed: Math.hypot(...Rig.Vehicle.LinearVelocity) });
    }
    return Trace;
}

const Weight = VehicleGeometry.Mass * 9.81;
const Finite = (Trace) => Trace.every((Row) => Number.isFinite(Row.Z) && Row.Loads.every(Number.isFinite) && Row.Defl.every(Number.isFinite));

test("coupling: a car at rest on the surrogate carcass settles, still, with the four loads summing to its weight", () =>
{
    const Rig1 = Rig(new SurrogateCarcass());
    const Trace = Run(Rig1, 8.0);
    assert.ok(Finite(Trace));
    const Tail = Trace.slice(-240);
    const Zs = Tail.map((Row) => Row.Z), Swing = Math.max(...Zs) - Math.min(...Zs);
    assert.ok(Swing < 0.002, `the body is still (${(Swing * 1000).toFixed(2)} mm peak to peak over the last 2 s)`);
    const Last = Tail[Tail.length - 1], Sum = Last.Loads.reduce((A, B) => A + B, 0);
    assert.ok(Math.abs(Sum / Weight - 1.0) < 0.03, `the corner loads carry the weight (${(Sum / Weight).toFixed(3)})`);
    assert.ok(Last.Defl.every((D) => D > 0.012 && D < 0.026), `plausible static deflection ${Last.Defl.map((D) => (D * 1000).toFixed(1))} mm`);
    assert.ok(Rig1.Vehicle.Wheels.every((Wheel) => Wheel.CarcassSecant > 100000 && Wheel.CarcassSecant < 400000), "the smoothed secant sits in the measured range");
});

test("coupling: without readings (or with the carcass off) the vehicle is the analytic car it always was", () =>
{
    const Analytic = Run(Rig(null), 3.0), Off = Rig(new SurrogateCarcass());
    Off.Vehicle.AssignCarcass(null);
    const Switched = []; for (let Step = 0; Step < 360; ++Step) { Off.Vehicle.AssignCarcass(null); Off.Vehicle.Advance(Idle, 1 / 120); Switched.push(Off.Vehicle.Position[2]); }
    assert.ok(Math.abs(Analytic[Analytic.length - 1].Z - Switched[Switched.length - 1]) < 1e-12, "AssignCarcass(null) is exactly the analytic carcass");
});

test("coupling: pressure matters: a half-flat tyre sinks further than an inflated one on the same law", () =>
{
    const Soft = Rig(new SurrogateCarcass()); Soft.Vehicle.AssignTyrePressures([100000, 100000, 100000, 100000]);
    const Hard = Rig(new SurrogateCarcass());
    const SoftTrace = Run(Soft, 6.0), HardTrace = Run(Hard, 6.0);
    assert.ok(Finite(SoftTrace) && Finite(HardTrace));
    assert.ok(SoftTrace[SoftTrace.length - 1].Defl[0] > HardTrace[HardTrace.length - 1].Defl[0] + 0.004, "lower pressure, larger squeeze");
});

test("coupling: a bad reading can bend the tyre but never break the car (NaN, zero, absurd loads, stale frames)", () =>
{
    const Garbage = (Value) => (Carcass, Step) =>
    {
        if (Step < 360) return;
        Carcass.Readings.forEach((Reading, Index) => { if ((Step / 2 + Index) % 3 === 0) Object.assign(Reading, Value, { Frame: Carcass.Frame }); });
    };
    for (const [Name, Value] of [["NaN", { Valid: true, Load: Number.NaN, Secant: Number.NaN, Deflection: Number.NaN }],
                                 ["absurd", { Valid: true, Load: 5.0e7, Secant: 5.0e9, Deflection: 0.01 }],
                                 ["negative", { Valid: true, Load: -4000, Secant: -1.0e5, Deflection: -0.01 }],
                                 ["zero", { Valid: true, Load: 0, Secant: 0, Deflection: 0 }]])
    {
        const Rig2 = Rig(new SurrogateCarcass());
        const Trace = Run(Rig2, 6.0, () => Idle, Garbage(Value));
        const Z = Trace.map((Row) => Row.Z);
        assert.ok(Z.every(Number.isFinite), `${Name}: finite`);
        assert.ok(Math.min(...Z) > 0.3 && Math.max(...Z) < 0.8, `${Name}: ride height stays in the physical band (${Math.min(...Z).toFixed(3)}..${Math.max(...Z).toFixed(3)})`);
    }
});

test("coupling: acceleration, a slalom and braking on the surrogate carcass stay finite, upright and grounded", () =>
{
    const Rig3 = Rig(new SurrogateCarcass());
    const Trace = Run(Rig3, 14.0, (Time) => Time < 1 ? Idle : Time < 6 ? { Throttle: 1, Brake: 0, Steer: 0, Handbrake: false }
        : Time < 11 ? { Throttle: 0.5, Brake: 0, Steer: Math.sin((Time - 6) * 2.2) * 0.6, Handbrake: false } : { Throttle: 0, Brake: 1, Steer: 0, Handbrake: false });
    assert.ok(Finite(Trace));
    const Fastest = Math.max(...Trace.map((Row) => Row.Speed));
    assert.ok(Fastest > 12.0 && Fastest < 60.0, `it drove (${Fastest.toFixed(1)} m/s)`);
    assert.ok(Trace.every((Row) => Row.Z > 0.35 && Row.Z < 0.75), "never airborne, never sunk");
    assert.ok(Trace[Trace.length - 1].Speed < Fastest * 0.8, "braking worked");
    const Up = Rig3.Vehicle.Orientation; // body z axis must still point up
    const UpZ = 1 - 2 * (Up[0] * Up[0] + Up[1] * Up[1]);
    assert.ok(UpZ > 0.9, "upright");
});

//------------------------------------------------------------------------------------------------------------------------
//   Executed on a software GPU (RUN_GPU=1): the six kernels, then the whole app.
//------------------------------------------------------------------------------------------------------------------------

function OnSoftwareGpu(Arguments, Environment = {})
{
    const Run = spawnSync(process.execPath, [path.join(Here, "RunOnSoftwareGpu.mjs"), ...Arguments], { cwd: Here, encoding: "utf8", timeout: 1700000, maxBuffer: 1 << 26, env: { ...process.env, ...Environment } });
    assert.equal(Run.status, 0, `software GPU run failed:\n${(Run.stdout + Run.stderr).slice(-1500)}`);
    return Run.stdout;
}

test("executed: the carcass's load rises with deflection and with pressure (six kernels, 32 substeps)", { skip: !Enabled, timeout: 1800000 }, () =>
{
    const Cases = [4, 10, 20, 30].map((Millimetres) => ({ Deflection: Millimetres / 1000, Gauge: 220000, Frames: 40 }))
        .concat([100000, 350000].map((Gauge) => ({ Deflection: 0.012, Gauge, Frames: 40 })));
    const Output = OnSoftwareGpu([process.execPath, path.join(Here, "TyreCarcassGpu.mjs"), JSON.stringify({ Cases })]);
    const { Results } = JSON.parse(Output.split("\n").find((Line) => Line.startsWith("RESULT ")).slice(7));
    const Load = (Index) => Results[Index].Reading.Load;
    assert.ok(Results.every((Result) => Result.Reading.Valid && Number.isFinite(Result.Reading.Load) && Result.Reading.Penetration < 0.02), "finite, in contact, no deep penetration");
    assert.ok(Load(0) < Load(1) && Load(1) < Load(2) && Load(2) < Load(3), `monotone in deflection: ${[0, 1, 2, 3].map(Load).map((V) => V.toFixed(0))}`);
    assert.ok(Load(2) > 2800 && Load(2) < 4500, `20 mm at 220 kPa carries a few kN (${Load(2).toFixed(0)} N; measured 3573 N)`);
    assert.ok(Load(5) > 2.0 * Load(4), `load grows with inflation pressure (100 kPa ${Load(4).toFixed(0)} N vs 350 kPa ${Load(5).toFixed(0)} N)`);
    for (const Result of Results) assert.ok(Result.Reading.PatchLength > 0.05 && Result.Reading.PatchLength < 0.8, "a patch of sane length");
});

test("executed: the whole app on the carcass: the four loads sum to the car's weight and the tyres deflect plausibly", { skip: !Enabled, timeout: 3000000 }, () =>
{
    const Output = OnSoftwareGpu([process.execPath, path.join(Here, "HeadlessHarness.mjs"), "..", "90", "?capture=1&quality=low&renderer=sdf&omit=Resampling,Diffuse,Reflection,Smoke,Cabin,Sparks"],
        { SIZE: "160x90", HARNESS_TIMEOUT: "2500000", HOOKS: path.join(Here, "TyreCarcassHooks.mjs"), SERIES_EVERY: "10" });
    const Series = JSON.parse(Output.split("\n").find((Line) => Line.startsWith("TYRESERIES ")).slice(11));
    assert.equal(Series.Dispatches, 416, "13 dispatches × 32 substeps");
    assert.ok(Series.Readbacks > 60, "the loads came back");
    const Last = Series.Rows[Series.Rows.length - 1];
    const Sum = Last.Wheels.reduce((A, W) => A + (W.Reading && W.Reading.Valid ? W.Reading.Load : 0), 0);
    assert.ok(Math.abs(Sum / Weight - 1.0) < 0.1, `Σ XPBD loads / weight = ${(Sum / Weight).toFixed(3)}`);
    assert.ok(Last.Wheels.every((W) => W.Deflection > 0.012 && W.Deflection < 0.026 && W.Secant > 100000), "plausible deflection and secant");
    assert.ok(Last.Z > 0.45 && Last.Z < 0.56, `ride height ${Last.Z.toFixed(3)} m`);
});
