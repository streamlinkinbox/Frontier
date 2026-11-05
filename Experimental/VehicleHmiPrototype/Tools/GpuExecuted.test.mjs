//============================================================================================================================================
//                                                         GPUEXECUTED.TEST.MJS
//============================================================================================================================================
// 🧩 Executed-shader checks: the real app runs headless on a SwiftShader Vulkan device (Tools/RunOnSoftwareGpu.mjs), so every WGSL program
//    really executes, and the captured frames are compared. Opt-in (minutes, needs registry.npmjs.org once for SwiftShader):
//
//        RUN_GPU=1 npm run test:gpu                       (SIZE, HARNESS_TIMEOUT and GPU_FRAMES override the defaults; GPU_ARTIFACT_DIR keeps the frames and logs;
//                                                          GPU_JOBS=1 renders one frame at a time: each render needs about 0.7 GB)
//
// 📝 Frames are deterministic: the harness clock advances one 60 Hz step per executed frame and the TAA jitter and every random sequence
//    follow the frame index, so two runs with the same query and frame count produce the same image up to a tiny noise floor (about 40 of
//    20736 pixels, one 8-bit step). That lets a test assert EQUALITY to that floor (the light grid is an exact cull) as well as bounds.

import test from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import zlib from "node:zlib";
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";

const Here = path.dirname(fileURLToPath(import.meta.url));
const Enabled = process.env.RUN_GPU === "1";
const Frames = Number(process.env.GPU_FRAMES || 16);   // past frame 15, where the old single surround quad used to win the depth test
const Scratch = fs.mkdtempSync(path.join(os.tmpdir(), "gpu-executed-"));

export function DecodePng(File)
{
    const Data = fs.readFileSync(File);
    let Offset = 8, Width = 0, Height = 0;
    const Parts = [];
    while (Offset < Data.length)
    {
        const Length = Data.readUInt32BE(Offset), Type = Data.toString("latin1", Offset + 4, Offset + 8);
        if (Type === "IHDR") { Width = Data.readUInt32BE(Offset + 8); Height = Data.readUInt32BE(Offset + 12); }
        if (Type === "IDAT") Parts.push(Data.subarray(Offset + 8, Offset + 8 + Length));
        Offset += 12 + Length;
    }
    const Raw = zlib.inflateSync(Buffer.concat(Parts)), Stride = Width * 4 + 1, Rgba = new Uint8Array(Width * Height * 4);
    for (let Row = 0; Row < Height; ++Row)
    {
        assert.equal(Raw[Row * Stride], 0, "the harness writes unfiltered PNG rows");
        Rgba.set(Raw.subarray(Row * Stride + 1, (Row + 1) * Stride), Row * Width * 4);
    }
    return { Width, Height, Rgba };
}

export function Compare(A, B)
{
    assert.equal(A.Width, B.Width); assert.equal(A.Height, B.Height);
    let Sum = 0, Max = 0, Different = 0;
    for (let Index = 0; Index < A.Rgba.length; Index += 4)
    {
        let Pixel = 0;
        for (let C = 0; C < 3; ++C) { const D = Math.abs(A.Rgba[Index + C] - B.Rgba[Index + C]); Sum += D; Pixel = Math.max(Pixel, D); }
        Max = Math.max(Max, Pixel);
        if (Pixel > 0) Different++;
    }
    return { MeanAbs: Sum / (A.Width * A.Height * 3), Max, Different, Pixels: A.Width * A.Height };
}

// ➡️ How many pixels differ by at least N steps (the largest channel difference of the pixel).
export function Histogram(A, B)
{
    assert.equal(A.Width, B.Width); assert.equal(A.Height, B.Height);
    const Counts = new Array(256).fill(0);
    for (let Index = 0; Index < A.Rgba.length; Index += 4)
    {
        let Pixel = 0;
        for (let C = 0; C < 3; ++C) Pixel = Math.max(Pixel, Math.abs(A.Rgba[Index + C] - B.Rgba[Index + C]));
        Counts[Pixel]++;
    }
    return { Counts, AtLeast: (Steps) => Counts.slice(Steps).reduce((Sum, Value) => Sum + Value, 0) };
}

export function Region(Image, X0, Y0, X1, Y1)
{
    const Sum = [0, 0, 0]; let Count = 0;
    for (let Y = Math.floor(Image.Height * Y0); Y < Math.floor(Image.Height * Y1); ++Y)
        for (let X = Math.floor(Image.Width * X0); X < Math.floor(Image.Width * X1); ++X, ++Count)
            for (let C = 0; C < 3; ++C) Sum[C] += Image.Rgba[(Y * Image.Width + X) * 4 + C];
    return Sum.map((V) => V / Count);
}

// 📝 At most GPU_JOBS renders run at once (default 2; a headless render of this app takes 0.7 GB, so GPU_JOBS=1 on a machine with 4 GB or less). A render killed by the OOM killer
//    dies with no output and exit code 1 (RunOnSoftwareGpu passes on a missing status as 1), which looks exactly like a failing test.
let Running = 0; const Waiting = [];
const Acquire = async () => { if (Running >= Number(process.env.GPU_JOBS || 2)) await new Promise((Resolve) => Waiting.push(Resolve)); Running++; };
const Release = () => { Running--; Waiting.shift()?.(); };

async function Render(Name, Query, FrameCount = Frames, Extra = {})
{
    const Out = path.join(Scratch, `${Name}.png`);
    await Acquire();
    return new Promise((Resolve, Reject) =>
    {
        const Child = spawn(process.execPath, [path.join(Here, "RunOnSoftwareGpu.mjs"), process.execPath, path.join(Here, "HeadlessHarness.mjs"), "..", String(FrameCount),
                            `?capture=1&quality=low&${Query}`], {
            cwd: Here, env: { ...process.env, SIZE: process.env.SIZE || "320x180", OUT: Out, HARNESS_TIMEOUT: process.env.HARNESS_TIMEOUT || "1500000", ...Extra }
        });
        let Log = "";
        Child.stdout.on("data", (Chunk) => { Log += Chunk; });
        Child.stderr.on("data", (Chunk) => { Log += Chunk; });
        Child.on("exit", (Code) =>
        {
            Release();
            // 📝 GPU_ARTIFACT_DIR: keep every rendered frame and its log (CI uploads the directory, so a failed comparison can be looked at)
            if (process.env.GPU_ARTIFACT_DIR) { fs.mkdirSync(process.env.GPU_ARTIFACT_DIR, { recursive: true }); fs.writeFileSync(path.join(process.env.GPU_ARTIFACT_DIR, `${Name}.log`), Log); if (fs.existsSync(Out)) fs.copyFileSync(Out, path.join(process.env.GPU_ARTIFACT_DIR, `${Name}.png`)); }
            (Code === 0 && fs.existsSync(Out)) ? Resolve({ Name, Out, Log, Image: DecodePng(Out) }) : Reject(new Error(`${Name} exited ${Code}\n${Log.slice(-1500)}`));
        });
    });
}

const Pair = (A, B) => Promise.all([A, B]);
const Cache = new Map();
const Once = (Name, Query, FrameCount, Extra) => { if (!Cache.has(Name)) Cache.set(Name, Render(Name, Query, FrameCount, Extra)); return Cache.get(Name); };

const GreyAsphalt = (Rgb, Label) =>
{
    const [R, G, B] = Rgb;
    assert.ok(G < R * 1.25 && G < B * 1.35 && R > 20, `${Label}: the ground must read as grey tarmac, not grass (R ${R.toFixed(0)} G ${G.toFixed(0)} B ${B.toFixed(0)})`);
};

test("executed: SDF frame is lit, finite and the tarmac stays tarmac through the old z-fight frames", { skip: !Enabled, timeout: 1800000 }, async (T) =>
{
    const Off = await Once("sdf-grid", "renderer=sdf");
    assert.match(Off.Log, /0 errors, 0 warnings/);
    T.diagnostic(`SDF frame: ground-left RGB ${Region(Off.Image, 0.02, 0.8, 0.25, 0.97).map((V) => V.toFixed(0))}, sky RGB ${Region(Off.Image, 0.1, 0.02, 0.9, 0.12).map((V) => V.toFixed(0))}`);
    GreyAsphalt(Region(Off.Image, 0.02, 0.8, 0.25, 0.97), "ground-left");
    GreyAsphalt(Region(Off.Image, 0.75, 0.8, 0.98, 0.97), "ground-right");
    const Sky = Region(Off.Image, 0.1, 0.02, 0.9, 0.12);
    assert.ok(Sky[2] > Sky[0] && Sky[2] > 120, `the sky is blue (${Sky.map((V) => V.toFixed(0))})`);
});

test("executed: the world light grid is an exact cull at night, and the same comparison catches a grid that is not", { skip: !Enabled, timeout: 1800000 }, async (T) =>
{
    // 📝 Why night, analytic tyres, and a deliberately broken third frame. (1) In daylight the sun dominates and a grid that dropped half of every light's reach moves only ~1 % of the pixels by
    //    a few steps (measured: 989 pixels, max 7, against a clean floor of 82 and 3), so a daylight comparison barely has teeth. At night the lamps and beams ARE the picture. (2) The XPBD
    //    tyre readbacks arrive at frame-to-frame different moments on a slow software adapter, which varies the car's pose and ripples through reprojection at thin silhouettes: two
    //    IDENTICAL night runs differ by mean 0.100 (max 103) with them and 0.011 (max 12) without. The tyres have nothing to do with the grid, so they are analytic here. (3) A comparison
    //    that cannot fail proves nothing, so a third frame renders the grid with every reach halved (?gridreach=0.5, the diagnostic) and the test requires THAT to be caught.
    //    Measured (SwiftShader, 320x180, 16 frames): clean grid vs all lights: mean abs 0.005-0.012, at most 20 pixels moved by 4 or more steps, none by 16;
    //    halved reach vs all lights: mean abs 0.521, 693 pixels by 4 or more, 312 by 8 or more, 72 by 16 or more.
    const Base = "renderer=sdf&preset=Night&tyres=analytic";
    const [On, Off, Broken] = await Promise.all([Once("grid-on", Base), Once("grid-off", `${Base}&lightgrid=0`), Once("grid-broken", `${Base}&gridreach=0.5`)]);
    const Exact = Compare(On.Image, Off.Image), ExactSteps = Histogram(On.Image, Off.Image);
    const Wrong = Compare(Broken.Image, Off.Image), WrongSteps = Histogram(Broken.Image, Off.Image);
    T.diagnostic(`night, light grid vs every light tested: ${Exact.Different} of ${Exact.Pixels} pixels differ, max ${Exact.Max}, mean abs ${Exact.MeanAbs.toFixed(4)}; pixels moved by >= 4 / 8 / 16 steps: ${ExactSteps.AtLeast(4)} / ${ExactSteps.AtLeast(8)} / ${ExactSteps.AtLeast(16)}`);
    T.diagnostic(`night, grid with HALVED reach vs every light tested (must be caught): ${Wrong.Different} of ${Wrong.Pixels} pixels differ, max ${Wrong.Max}, mean abs ${Wrong.MeanAbs.toFixed(4)}; pixels moved by >= 4 / 8 / 16 steps: ${WrongSteps.AtLeast(4)} / ${WrongSteps.AtLeast(8)} / ${WrongSteps.AtLeast(16)}`);
    for (const Run of [On, Off, Broken]) assert.match(Run.Log, /0 errors, 0 warnings/);
    // the grid is exact: clean runs sit at the noise floor (thresholds 2.5-3x the worst clean measurement)
    assert.ok(Exact.MeanAbs <= 0.03, `grid on vs all lights: mean abs ${Exact.MeanAbs.toFixed(4)} (clean floor 0.012)`);
    assert.ok(ExactSteps.AtLeast(4) <= 0.003 * Exact.Pixels, `grid on vs all lights: ${ExactSteps.AtLeast(4)} pixels moved by 4 or more steps (at most ${Math.floor(0.003 * Exact.Pixels)})`);
    assert.ok(ExactSteps.AtLeast(16) <= 5 && Exact.Max <= 24, `grid on vs all lights: ${ExactSteps.AtLeast(16)} pixels by 16 or more, largest change ${Exact.Max}`);
    // and the comparison has teeth: a grid that drops half of every reach is far outside that floor
    assert.ok(Wrong.MeanAbs >= 0.15 && Wrong.MeanAbs >= 10 * Math.max(Exact.MeanAbs, 0.005), `the broken grid must be caught: mean abs ${Wrong.MeanAbs.toFixed(4)} against ${Exact.MeanAbs.toFixed(4)} for the clean one`);
    assert.ok(WrongSteps.AtLeast(8) >= 100 && WrongSteps.AtLeast(8) >= 10 * Math.max(ExactSteps.AtLeast(8), 5), `the broken grid moves ${WrongSteps.AtLeast(8)} pixels by 8 or more steps (the clean one ${ExactSteps.AtLeast(8)})`);
});

test("executed: screen traces are sane, finite and change the SDF image", { skip: !Enabled, timeout: 1800000 }, async (T) =>
{
    const [Off, On] = await Pair(Once("sdf-grid", "renderer=sdf"), Once("sdf-screen", "renderer=sdf&screentraces=1"));
    assert.match(On.Log, /0 errors, 0 warnings/);
    GreyAsphalt(Region(On.Image, 0.02, 0.8, 0.25, 0.97), "screen traces: ground-left");
    const Difference = Compare(Off.Image, On.Image);
    T.diagnostic(`SDF screen traces on vs off: ${Difference.Different} of ${Difference.Pixels} pixels differ, max ${Difference.Max}, mean abs ${Difference.MeanAbs.toFixed(3)} / 255`);
    assert.ok(Difference.Different > 0, "enabling the screen traces must change some pixels (otherwise the gate does nothing)");
    assert.ok(Difference.MeanAbs < 6, `screen traces must stay close to the distance-field reference (mean abs ${Difference.MeanAbs.toFixed(2)} / 255)`);
});

test("executed: BVH path tracer + screen traces + light grid render a sane, tarmac-grey frame", { skip: !Enabled, timeout: 1800000 }, async (T) =>
{
    const [Bvh, BvhScreen] = await Pair(Once("bvh", "renderer=bvh"), Once("bvh-screen", "renderer=bvh&screentraces=1"));
    for (const Run of [Bvh, BvhScreen])
    {
        assert.match(Run.Log, /0 errors, 0 warnings/);
        GreyAsphalt(Region(Run.Image, 0.02, 0.8, 0.25, 0.97), `${Run.Name}: ground-left`);
    }
    const Difference = Compare(Bvh.Image, BvhScreen.Image);
    T.diagnostic(`BVH screen traces on vs off: ${Difference.Different} of ${Difference.Pixels} pixels differ, max ${Difference.Max}, mean abs ${Difference.MeanAbs.toFixed(3)} / 255`);
    assert.ok(Difference.Different > 0, "enabling the screen traces must change some BVH pixels");
    assert.ok(Difference.MeanAbs < 10, `BVH with screen traces must stay close to the pure BVH path tracer (mean abs ${Difference.MeanAbs.toFixed(2)} / 255)`);
});

process.on("exit", () => { try { fs.rmSync(Scratch, { recursive: true, force: true }); } catch { /* best effort */ } });

test("executed: emissive clusters on vs off at night: both lit and sane, and the partition neither adds nor removes energy", { skip: !Enabled, timeout: 1800000 }, async (T) =>
{
    // 📝 What this can and cannot show. At night the lamps and the tail strip are lit, but the analytic street lamps still dominate, so the structure moves few pixels: this is a smoke
    //    and BIAS test (the partition of unity must not change the frame's mean light anywhere), not a variance test. The estimator itself is verified where it can be: against the analytic
    //    irradiance in EmissiveLights.test.mjs, and the shader's sampler against its CPU twin over 3000 queries. The state hook proves each run really had the structure on or off.
    const Hooks = { HOOKS: path.join(Here, "EmissiveStateHooks.mjs") };
    const [On, Off] = await Pair(Once("night-emissive", "renderer=bvh&preset=Night", Frames, Hooks), Once("night-noemissive", "renderer=bvh&preset=Night&emissive=0", Frames, Hooks));
    const State = (Run) => JSON.parse(Run.Log.split("\n").find((Line) => Line.startsWith("EMISSIVESTATE ")).slice(14));
    assert.ok(State(On).Active && State(On).Instances >= 50 && State(On).Triangles >= 90, `emissive ON really ran the structure: ${JSON.stringify(State(On))}`);
    assert.ok(!State(Off).Active && !State(Off).Setting, `emissive OFF did not: ${JSON.stringify(State(Off))}`);
    for (const Run of [On, Off]) assert.match(Run.Log, /0 errors, 0 warnings/);
    const Night = Region(On.Image, 0.1, 0.02, 0.9, 0.12);
    assert.ok(Night[2] > Night[0] && Night[2] < 140, `a night sky is dark blue (${Night.map((V) => V.toFixed(0))})`);
    const Mean = (Image) => { let Sum = 0; for (let Index = 0; Index < Image.Rgba.length; Index += 4) Sum += 0.2126 * Image.Rgba[Index] + 0.7152 * Image.Rgba[Index + 1] + 0.0722 * Image.Rgba[Index + 2]; return Sum / (Image.Rgba.length / 4); };
    assert.ok(Mean(On.Image) > 25 && Mean(Off.Image) > 25, "both nights are lit by the lamps, not black");
    assert.ok(Math.abs(Mean(On.Image) - Mean(Off.Image)) < 0.03 * Mean(Off.Image), `the frame's mean light agrees to 3 %: ${Mean(On.Image).toFixed(2)} vs ${Mean(Off.Image).toFixed(2)}`);
    // measured at 16 frames: every region's mean differs by at most 1.0 of 255, with mixed signs; 4 is four times that and still catches a 5 % energy error in a region of luminance 80
    for (const [Name, Box] of Object.entries({ sky: [0.1, 0.02, 0.9, 0.12], ground: [0.02, 0.8, 0.98, 0.97], car: [0.35, 0.55, 0.65, 0.85], ahead: [0.4, 0.3, 0.6, 0.5], left: [0, 0.3, 0.3, 0.7], right: [0.7, 0.3, 1, 0.7] }))
    {
        const A = Region(On.Image, ...Box), B = Region(Off.Image, ...Box);
        for (let C = 0; C < 3; ++C) assert.ok(Math.abs(A[C] - B[C]) < 4, `${Name} channel ${C}: ${A[C].toFixed(1)} with the structure, ${B[C].toFixed(1)} without`);
    }
    const Difference = Compare(On.Image, Off.Image);
    T.diagnostic(`night, emissive clusters on vs off: mean luminance ${Mean(On.Image).toFixed(2)} vs ${Mean(Off.Image).toFixed(2)}; ${Difference.Different} of ${Difference.Pixels} pixels differ, mean abs ${Difference.MeanAbs.toFixed(3)} / 255; structure: ${JSON.stringify(State(On))}`);
    assert.ok(Difference.MeanAbs < 4, `the frames stay close (mean abs ${Difference.MeanAbs.toFixed(2)} / 255)`);
});
