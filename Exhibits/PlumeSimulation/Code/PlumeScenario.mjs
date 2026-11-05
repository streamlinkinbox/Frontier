//============================================================================================================================================
//                                                          PLUMESCENARIO.MJS
//============================================================================================================================================
// 🧩 The plume exhibit's scripted inputs: a burnout of 420 refreshes (gas presets, ignition, a burst tyre, a shredded one, editor emitters) as the
//    prototype's own modules take it. ExportPlumeReference.mjs feeds it to the CPU modules, PlumeParityGpu.mjs to the device; the C++ validation
//    repeats the generators (Mulberry32, Tri, Ramp — integer operations and + − × ÷ only) and hashes the inputs to prove it.

import path from "node:path";
import { fileURLToPath } from "node:url";

const Here   = path.dirname(fileURLToPath(import.meta.url));
const Source = path.resolve(Here, "../../../Experimental/VehicleHmiPrototype");
const { ConstructGasParameters } = await import(`${Source}/Source/SmokeSequence.js`);
const { GasPresets } = await import(`${Source}/Source/InspectorPages.js`);

// 📐 FNV-1a, 64 bits, over bytes — written as 16 hex digits.
export function Fnv1a64(Bytes)
{
    let Hash = 0xcbf29ce484222325n;
    for (let Index = 0; Index < Bytes.length; ++Index)
    {
        Hash ^= BigInt(Bytes[Index]);
        Hash = (Hash * 0x100000001b3n) & 0xffffffffffffffffn;
    }
    return Hash.toString(16).padStart(16, "0");
}
//------------------------------------------------------------------------------------------------------------------------
//                                                   THE SCENARIO'S INPUTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 mulberry32 and a triangle wave and a ramp: integer operations and + − × ÷ only, so the C++ repeats them bit for bit.
export function Mulberry32(Seed)
{
    return () =>
    {
        Seed = (Seed + 0x6D2B79F5) | 0;
        let T = Math.imul(Seed ^ (Seed >>> 15), 1 | Seed);
        T = (T + Math.imul(T ^ (T >>> 7), 61 | T)) ^ T;
        return ((T ^ (T >>> 14)) >>> 0) / 4294967296;
    };
}
export const Tri  = (Step, Period) => { const X = (Step % Period) / Period; return X < 0.5 ? 2 * X : 2 - 2 * X; };
export const Ramp = (Step, From, To) => Math.min(1, Math.max(0, (Step - From) / (To - From)));

export const BurnoutSteps   = 420;
export const FadeSteps      = 1000;
export const BurnoutStepDt  = [1 / 60, 1 / 60, 1 / 120, 1 / 60, 1 / 30, 1 / 60, 0.08, 1 / 60];
export const LightDirection = [0.3, 0.2, 0.9327379053088815];

// 📝 The gas at a step: a look preset and overrides on top, applied the way the editor's preset buttons do.
export function GasChoice(Step)
{
    if (Step >= 380) return { Preset: "Crosswind", Overrides: { TimeScale: 2.0, Emission: 0.0, PressureSweeps: 40 } };
    if (Step >= 300) return { Preset: "Crosswind", Overrides: { TimeScale: 0.5 } };
    if (Step >= 200) return { Preset: "Inferno",   Overrides: { PressureSweeps: 13 } };
    if (Step >= 100) return { Preset: "Thick burnout", Overrides: {} };
    return { Preset: "Tyre smoke", Overrides: {} };
}
export function ConstructGas(Choice)
{
    return Object.assign(ConstructGasParameters(), GasPresets[Choice.Preset], Choice.Overrides);
}

export function BurnoutInputs(Step, Noise)
{
    const Seconds = BurnoutStepDt[Step % 8];
    const T = Step / 60;
    const Qx = 0.04 * Tri(Step, 110) - 0.02, Qy = 0.03 * Tri(Step, 70) - 0.015;
    const A = 0.6 * Tri(Step, 200) - 0.3, B = 1.0, Length = Math.sqrt(Qx * Qx + Qy * Qy + A * A + B * B);
    const Position = [6 * T, 0.8 * Tri(Step, 300), 0.55];
    const Velocity = [6 + 2 * Tri(Step, 90), 0.5 * Tri(Step, 45) - 0.25, 0];
    const Vehicle = { Position, LinearVelocity: Velocity, Orientation: [Qx / Length, Qy / Length, A / Length, B / Length], Wheels: [] };
    const Readings = [], Thermal = { Wheels: [] };
    for (let Index = 0; Index < 4; ++Index)
    {
        const Front = Index < 2;
        Vehicle.Wheels.push({ Steer: (Front ? 0.35 : 0.0) * (Tri(Step, 150) - 0.5) * 2, Radius: 0.34 + 0.01 * Index, Spin: 20 + 15 * Tri(Step, 120) + Index });
        const Contact = [Position[0] + (Front ? 1.3 : -1.3), Position[1] + ((Index & 1) ? -0.8 : 0.8), 0.0];
        Readings.push({
            ContactFraction: (Step % 97 < 8 && Index === 1) ? 0.3 : ((Step % 101 < 6 && Index === 2) ? 0.55 : 0.92),
            OnDebris: Step % 131 < 5 && Index === 0,
            Shredded: Index === 3 && Step >= 330,
            SlidingPower: (Front ? 3000 : 22000) * Ramp(Step, 20 + 10 * Index, 80 + 10 * Index) * (1 - Ramp(Step, 360, 400)) * (0.35 + 0.65 * Noise[Index]),
            SlipSpeed: 0.2 + 5.8 * Ramp(Step, 15 + 10 * Index, 70 + 10 * Index) * (0.5 + 0.5 * Noise[4 + Index]),
            ContactPoint: Contact,
            ContactVelocity: [Velocity[0] * 0.9, Velocity[1], 0],
            HubPosition: [Contact[0], Contact[1], 0.34]
        });
        Thermal.Wheels.push({
            Ignited: (Index === 2 && Step >= 150 && Step < 330) || (Index === 0 && Step >= 260 && Step < 300) || (Index === 3 && Step >= 340 && Step < 380),
            CombustionPower: 8000 + 12000 * Noise[Index],
            Surface: 300 + 350 * Ramp(Step, 10, 200),
            Burst: Index === 2 && Step >= 200 && Step < 204
        });
    }
    const Emissions = [0, 1, 2, 3, 4, 5].map((Slot) => ({
        Active: Slot === 4 ? (Step >= 120 && Step < 200) : Slot === 5 ? (Step >= 250 && Step < 300) : true,
        Position: [Position[0] + Slot, 2.0 + 0.1 * Slot, 0.3 * Slot], Radius: Slot === 5 ? 0 : 0.2 + 0.05 * Slot, Velocity: [0.1 * Slot, 0, 1.0 + Slot],
        SmokeRate: 2.0 + Slot, HeatRate: 1.5 * Slot, FlameRate: Slot === 4 ? 3.0 : 0, SootRate: 0.5 * Slot
    }));
    return { Seconds, Vehicle, Readings, Thermal, Emissions, Enabled: !(Step >= 400 && Step < 410), Gas: ConstructGas(GasChoice(Step)) };
}

