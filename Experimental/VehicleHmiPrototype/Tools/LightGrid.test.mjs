//============================================================================================================================================
//                                                           LIGHTGRID.TEST.MJS
//============================================================================================================================================
// 🧩 The world light grid's one promise: a light is never missing from a cell its reach touches, so culling by the grid cannot change a picture (the executed tests check that on
//    rendered frames; this checks the structure itself, and that the ?gridreach= diagnostic really does break the promise, which is what lets those frames prove they would notice).

import test from "node:test";
import assert from "node:assert/strict";
import { LightClusterSpace, LightClusterBounds } from "../Source/LightClusterSpace.js";
import { MaxSceneLights, LightRecordFloats } from "../Source/LightingSystem.js";

globalThis.GPUBufferUsage ??= { STORAGE: 1, UNIFORM: 2, COPY_DST: 4 };      // the constructor names buffer usages; no GPU is involved here

const Resolution = LightClusterBounds.Resolution, CellCount = Resolution[0] * Resolution[1] * Resolution[2];
const Written = [];
const Device = { queue: { writeBuffer: (...Arguments) => Written.push(Arguments.length) } };

function Build(ReachScale, Seed = 7)
{
    let State = Seed >>> 0; const Rand = () => (State = (State * 1664525 + 1013904223) >>> 0) / 4294967296;
    const Space = new LightClusterSpace({ ConstructRecords: () => ({}) });
    Space.ReachScale = ReachScale;
    const Data = new Float32Array(MaxSceneLights * LightRecordFloats), Lights = [];
    for (let Index = 0; Index < MaxSceneLights; ++Index)
    {
        const O = Index * LightRecordFloats, Active = Index % 5 !== 3;     // some inactive lights: they must never appear in a mask
        const Centre = [0, 1, 2].map((Axis) => LightClusterBounds.Minimum[Axis] - 15 + Rand() * (LightClusterBounds.Extent[Axis] + 30)), Reach = 3 + 40 * Rand();
        Data.set([Centre[0], Centre[1], Centre[2], Index % 6], O); Data[O + 7] = Reach; Data[O + 15] = Active ? 1 : 0;
        Lights.push({ Centre, Reach, Active });
    }
    Space.Update(Device, Data);
    return { Space, Lights, Rand };
}

const CellOf = (Point) => Point.map((Value, Axis) => Math.min(Resolution[Axis] - 1, Math.max(0, Math.floor((Value - LightClusterBounds.Minimum[Axis]) / (LightClusterBounds.Extent[Axis] / Resolution[Axis])))));

test("the grid never drops a light from a cell it reaches (no false negatives), and never lists an inactive one", () =>
{
    const { Space, Lights, Rand } = Build(1.0);
    let Checked = 0, Reached = 0, Surplus = 0;
    for (let Probe = 0; Probe < 60000; ++Probe)
    {
        const Point = [0, 1, 2].map((Axis) => LightClusterBounds.Minimum[Axis] + Rand() * LightClusterBounds.Extent[Axis]);
        const [X, Y, Z] = CellOf(Point), Mask = Space.Masks[X + Resolution[0] * (Y + Resolution[1] * Z)];
        Lights.forEach((Light, Index) =>
        {
            const Distance = Math.hypot(Point[0] - Light.Centre[0], Point[1] - Light.Centre[1], Point[2] - Light.Centre[2]);
            const Listed = (Mask >>> Index) & 1;
            if (Light.Active && Distance < Light.Reach) { Reached++; assert.equal(Listed, 1, `light ${Index} reaches (${Point.map((V) => V.toFixed(1))}) but is missing from its cell`); }
            if (!Light.Active) assert.equal(Listed, 0, `inactive light ${Index} is listed`);
            if (Light.Active && Distance >= Light.Reach && Listed) Surplus++;
            Checked++;
        });
    }
    assert.ok(Reached > 20000, `the probes exercised ${Reached} light/point pairs inside a reach`);
    assert.ok(Surplus > 0, "the cull is conservative (a cell lists lights that reach only part of it), not exact per point: the shader's own reach test makes it exact");
    assert.ok(Checked === 60000 * MaxSceneLights);
});

test("the cull is a real cull: a light is listed in far fewer than all cells", () =>
{
    const { Space } = Build(1.0);
    let Listed = 0;
    for (let Cell = 0; Cell < CellCount; ++Cell) for (let Bit = 0; Bit < 32; ++Bit) Listed += (Space.Masks[Cell] >>> Bit) & 1;
    const Fraction = Listed / (CellCount * 25);       // 25 of the 32 lights are active
    assert.ok(Fraction > 0.02 && Fraction < 0.8, `each active light is listed in ${(100 * Fraction).toFixed(0)} % of the cells`);
});

test("diagnostic: ?gridreach below 1 DOES drop lights from cells they reach, so a test that compares the grid with the all-lights frame can fail", () =>
{
    const { Space, Lights, Rand } = Build(0.5);
    let Missing = 0, Reached = 0;
    for (let Probe = 0; Probe < 60000; ++Probe)
    {
        const Point = [0, 1, 2].map((Axis) => LightClusterBounds.Minimum[Axis] + Rand() * LightClusterBounds.Extent[Axis]);
        const [X, Y, Z] = CellOf(Point), Mask = Space.Masks[X + Resolution[0] * (Y + Resolution[1] * Z)];
        Lights.forEach((Light, Index) =>
        {
            if (!Light.Active || Math.hypot(Point[0] - Light.Centre[0], Point[1] - Light.Centre[1], Point[2] - Light.Centre[2]) >= Light.Reach) return;
            Reached++; if (!((Mask >>> Index) & 1)) Missing++;
        });
    }
    assert.ok(Missing > 0.05 * Reached, `half the reach drops ${Missing} of ${Reached} reaching light/point pairs`);
});

test("the host wires ?gridreach to the cull and clamps it", async () =>
{
    const { readFileSync } = await import("node:fs");
    const Host = readFileSync(new URL("../Source/IlluminationHost.js", import.meta.url), "utf8");
    assert.match(Host, /LightGridReach: Math\.max\(0\.05, Math\.min\(1\.0, Number\(Query\.get\("gridreach"\) \|\| 1\)\)\)/);
    assert.match(Host, /this\.LightClusters\.ReachScale = this\.Settings\.LightGridReach;/);
});
