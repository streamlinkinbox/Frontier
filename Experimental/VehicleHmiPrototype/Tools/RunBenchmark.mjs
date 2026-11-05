//============================================================================================================================================
//                                                           RUNBENCHMARK.MJS
//============================================================================================================================================
// 🧩 One toggle, measured. Runs the whole exhibit headless on a software adapter with the BASELINE query and with the VARIANT query, interleaved A B B A (so a slow
//    start or a thermal drift cannot favour one), reads the per-frame times Tools/BenchHooks.mjs prints, and writes the comparison as JSON.
//
//    node RunBenchmark.mjs --label "screen traces" --baseline "renderer=bvh" --variant "renderer=bvh&screentraces=1" [--frames 10] [--rounds 2] [--size 320x180] [--out bench.json]
//
//    BACKEND=null node RunBenchmark.mjs ...      exercises the plumbing on Dawn's null backend (nothing executes; the numbers mean nothing)
//
// 📝 GitHub runners have no GPU: the numbers are the WGSL running on CPU cores (SwiftShader), a measure of relative shader work. See BenchmarkStatistics.mjs for how a verdict is
//    reached and Tools/BenchHooks.mjs for exactly what is timed.

import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { FetchSoftwareGpu, SoftwareGpuPackage } from "./RunOnSoftwareGpu.mjs";
import { Compare, Median } from "./BenchmarkStatistics.mjs";

const Here = path.dirname(fileURLToPath(import.meta.url));

export function ParseArguments(Argv)
{
    const Options = { frames: 10, rounds: 2, size: "320x180", warmup: 3, out: "", label: "", baseline: "", variant: "" };
    for (let Index = 0; Index < Argv.length; Index += 2)
    {
        const Key = Argv[Index].replace(/^--/, "");
        if (!(Key in Options) || Argv[Index + 1] === undefined) throw new Error(`unknown or incomplete option ${Argv[Index]}`);
        Options[Key] = typeof Options[Key] === "number" ? Number(Argv[Index + 1]) : Argv[Index + 1];
    }
    if (!Options.baseline || !Options.variant) throw new Error("--baseline and --variant (query strings) are required");
    if (!Options.label) Options.label = Options.variant;
    if (Options.frames <= Options.warmup + 1) throw new Error("--frames must exceed the warm-up frames by at least two");
    return Options;
}

// ➡️ The order of the runs: baseline and variant alternate and the pairs mirror (A B B A A B ...).
export function RunOrder(Rounds)
{
    const Order = [];
    for (let Round = 0; Round < Rounds; ++Round) Order.push(...(Round % 2 === 0 ? ["baseline", "variant"] : ["variant", "baseline"]));
    return Order;
}

function RunOnce(Query, Options, Environment)
{
    const Started = performance.now();
    const Run = spawnSync(process.execPath, [path.join(Here, "HeadlessHarness.mjs"), "..", String(Options.frames), `?capture=1&quality=low&${Query}`], {
        cwd: Here, encoding: "utf8", maxBuffer: 1 << 26, timeout: 1800000,
        env: { ...process.env, ...Environment, HOOKS: path.join(Here, "BenchHooks.mjs"), BENCH_QUERY: Query, BENCH_WARMUP: String(Options.warmup), SIZE: Options.size, HARNESS_TIMEOUT: "1500000" }
    });
    const Output = (Run.stdout || "") + (Run.stderr || "");
    const Line = Output.split("\n").find((Text) => Text.startsWith("BENCH "));
    if (Run.status !== 0 || !Line || !/ 0 errors, /.test(Output))
        throw new Error(`benchmark run failed for "${Query}" (exit ${Run.status}):\n${Output.slice(-1500)}`);
    return { ...JSON.parse(Line.slice(6)), WallSeconds: (performance.now() - Started) / 1000 };
}

export function Machine()
{
    return { Cpu: os.cpus()[0]?.model?.trim() ?? "unknown", Cores: os.cpus().length, MemoryGiB: Math.round(os.totalmem() / 2 ** 30), Node: process.version, Platform: `${os.platform()} ${os.arch()}`,
             SwiftShader: SoftwareGpuPackage, Backend: process.env.BACKEND || "vulkan (SwiftShader)" };
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url))
{
    const Options = ParseArguments(process.argv.slice(2));
    const Environment = process.env.BACKEND === "null" ? { BACKEND: "null" } : FetchSoftwareGpu();
    const Runs = [];
    for (const Which of RunOrder(Options.rounds))
    {
        const Result = RunOnce(Which === "baseline" ? Options.baseline : Options.variant, Options, Environment);
        Runs.push({ Which, MedianMs: Result.MedianMs, MeanMs: Result.MeanMs, SpreadFraction: Result.SpreadFraction, DurationsMs: Result.DurationsMs, WallSeconds: Result.WallSeconds });
        console.error(`  ${Which.padEnd(8)} ${Result.MedianMs.toFixed(0).padStart(7)} ms/frame (frame-to-frame spread ${(100 * Result.SpreadFraction).toFixed(1)} %), run took ${Result.WallSeconds.toFixed(0)} s`);
    }
    const Comparison = Compare(Runs.filter((Run) => Run.Which === "baseline").map((Run) => Run.MedianMs), Runs.filter((Run) => Run.Which === "variant").map((Run) => Run.MedianMs));
    const Result = { Label: Options.label, Baseline: Options.baseline, Variant: Options.variant, Frames: Options.frames, Warmup: Options.warmup, Size: Options.size, Rounds: Options.rounds,
                     Machine: Machine(), Runs, Comparison, MedianOfAll: Median(Runs.map((Run) => Run.MedianMs)) };
    if (Options.out) fs.writeFileSync(Options.out, JSON.stringify(Result, null, 1));
    console.log(`${Options.label}: variant ${(100 * (Comparison.Ratio - 1)).toFixed(1)} % vs baseline (${Comparison.BaselineMs.toFixed(0)} → ${Comparison.VariantMs.toFixed(0)} ms/frame, noise ±${(100 * Comparison.Noise).toFixed(1)} %): ${Comparison.Verdict}`);
}
