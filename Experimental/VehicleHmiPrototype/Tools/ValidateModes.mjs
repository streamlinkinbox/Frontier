//============================================================================================================================================
//                                                          VALIDATEMODES.MJS
//============================================================================================================================================
// 🧩 Brings the whole app up on Dawn's null backend in every mode a URL can select (it validates every WGSL module, layout, pipeline and command exactly as Chrome would, and
//    executes nothing), and fails if any mode reports a WebGPU or WGSL error. This is the cheap guard for the combinations nobody remembers to try by hand.
//
//    node ValidateModes.mjs [frames]          (default 8 frames per mode)
//    npm run validate:modes

import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const Here = path.dirname(fileURLToPath(import.meta.url));

// [name, query]. Every toggle and every renderer name, alone and in the combinations that change which shaders and records are live.
export const Modes = Object.freeze([
    ["default (BVH path tracer)",          ""],
    ["renderer=bvh",                       "renderer=bvh"],
    ["renderer=sdf",                       "renderer=sdf"],
    ["old name renderer=rtx",              "renderer=rtx"],
    ["old name renderer=gtx",              "renderer=gtx"],
    ["screen traces, BVH",                 "screentraces=1"],
    ["screen traces, SDF",                 "renderer=sdf&screentraces=1"],
    ["light grid off",                     "lightgrid=0"],
    ["IES profiles off",                   "ies=0"],
    ["emissive clusters off",              "emissive=0"],
    ["everything new off",                 "screentraces=0&lightgrid=0&ies=0&emissive=0&tyres=analytic"],
    ["everything new on",                  "screentraces=1&lightgrid=1&ies=1&emissive=1&tyres=xpbd"],
    ["analytic tyres",                     "tyres=analytic"],
    ["XPBD tyres, 8 substeps",             "tyresubsteps=8"],
    ["XPBD tyres, 16 substeps",            "tyresubsteps=16"],
    ["night",                              "preset=Night"],
    ["night, emissive off",                "preset=Night&emissive=0"],
    ["low quality",                        "quality=low"],
    ["BVH, one GI and one reflection bounce", "bounces=1&reflbounces=1"],
    ["BVH, four bounces, four candidates", "bounces=4&reflbounces=4&candidates=4"]
]);

export function RunMode(Query, Frames = 8)
{
    const Started = performance.now();
    const Run = spawnSync(process.execPath, [path.join(Here, "HeadlessHarness.mjs"), "..", String(Frames), `?capture=1${Query ? "&" + Query : ""}`], {
        cwd: Here, encoding: "utf8", maxBuffer: 1 << 26, timeout: 600000, env: { ...process.env, BACKEND: "null", HOOKS: "" }
    });
    const Output = (Run.stdout || "") + (Run.stderr || "");
    const Summary = Output.split("\n").find((Line) => /=== harness:/.test(Line)) || "(no harness summary)";
    return { Ok: Run.status === 0 && / 0 errors, 0 warnings/.test(Summary), Summary: Summary.replace(/^=== harness: /, ""), Seconds: (performance.now() - Started) / 1000, Tail: Output.slice(-1200) };
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url))
{
    const Frames = Number(process.argv[2] || 8);
    let Failed = 0;
    for (const [Name, Query] of Modes)
    {
        const Result = RunMode(Query, Frames);
        console.log(`${Result.Ok ? "ok  " : "FAIL"}  ${Name.padEnd(42)} ?${Query || "(none)"}  →  ${Result.Summary}`);
        if (!Result.Ok) { Failed++; console.log(Result.Tail); }
    }
    console.log(`\n${Modes.length - Failed} of ${Modes.length} modes validated`);
    process.exit(Failed ? 1 : 0);
}
