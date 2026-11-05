//============================================================================================================================================
//                                                        BENCHMARKTOOLS.TEST.MJS
//============================================================================================================================================
// 🧩 The benchmark tooling is tested like everything else: its statistics, its verdict (it must say "no measurable difference" when that is the truth), the run order, the
//    summary table, and (on Dawn's null backend, where nothing executes) the whole plumbing from the harness through BenchHooks.mjs to a result file.

import test from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { Median, Summarise, Compare, RenderTable, Percent, MinimumEffect } from "./BenchmarkStatistics.mjs";
import { ParseArguments, RunOrder } from "./RunBenchmark.mjs";
import { Summarise as SummariseDirectory } from "./SummariseBenchmarks.mjs";

const Here = path.dirname(fileURLToPath(import.meta.url));

test("median and the robust spread", () =>
{
    assert.equal(Median([3, 1, 2]), 2); assert.equal(Median([4, 1, 3, 2]), 2.5); assert.ok(Number.isNaN(Median([])));
    const Statistics = Summarise([100, 102, 98, 101, 99, 5000]);       // one slow frame (a GC pause, a noisy neighbour) must not move the median
    assert.ok(Math.abs(Statistics.MedianMs - 100.5) < 1e-9 && Statistics.MaxMs === 5000 && Statistics.MeanMs > 800, "the median ignores the outlier, the mean does not");
    assert.ok(Statistics.SpreadFraction < 0.02, `the spread is the median absolute deviation (${Statistics.SpreadFraction})`);
    assert.equal(Summarise([]).Count, 0);
});

test("verdict: faster, slower, and — when the truth is that — no measurable difference", () =>
{
    assert.equal(Compare([100, 101], [80, 81]).Verdict, "faster");
    assert.equal(Compare([100, 101], [125, 124]).Verdict, "slower");
    assert.equal(Compare([100, 101], [101, 100]).Verdict, "no measurable difference", "an identical configuration is not faster or slower");
    assert.equal(Compare([100, 100], [103, 104]).Verdict, "no measurable difference", "a 3-4 % difference is inside the 5 % band");
    // a noisy machine widens the band: 20 % spread between repeats of the BASELINE means a 25 % gap is not believed
    const Noisy = Compare([100, 120], [85, 86]);
    assert.ok(Noisy.Noise > 0.15 && Noisy.Band >= 2 * Noisy.Noise - 1e-12 && Noisy.Verdict === "no measurable difference", `noise ${Noisy.Noise}, band ${Noisy.Band}, ${Noisy.Verdict}`);
    assert.equal(Compare([100], [70]).Verdict, "faster", "a single repeat has no noise estimate: the fixed band alone decides");
    // a result that only just leaves the band is flagged, not stated flatly (the GitHub run's IES row: -5.2 % against a 5 % band)
    const Borderline = Compare([100, 100.5], [94.6, 94.8]);
    assert.equal(Borderline.Verdict, "faster (marginal)", `margin ${Borderline.Margin}`);
    assert.equal(Compare([100, 100.5], [110, 110.5]).Verdict, "slower", "ten percent over a five percent band is not marginal");
    assert.equal(Compare([100, 100.5], [105.9, 106.0]).Verdict, "slower (marginal)");
    assert.equal(Compare([], [1]).Verdict, "no data");
    assert.equal(MinimumEffect, 0.05);
    assert.equal(Percent(1.1234), "+12.3 %"); assert.equal(Percent(0.8), "−20.0 %");
});

test("the run order alternates and mirrors, so a drift cannot favour one side", () =>
{
    assert.deepEqual(RunOrder(2), ["baseline", "variant", "variant", "baseline"]);
    assert.deepEqual(RunOrder(3), ["baseline", "variant", "variant", "baseline", "baseline", "variant"]);
    for (const Rounds of [1, 2, 4]) { const Order = RunOrder(Rounds); assert.equal(Order.filter((Name) => Name === "baseline").length, Rounds); assert.equal(Order.filter((Name) => Name === "variant").length, Rounds); }
});

test("argument parsing rejects what would produce a meaningless benchmark", () =>
{
    const Good = ParseArguments(["--baseline", "renderer=bvh", "--variant", "renderer=sdf", "--frames", "12"]);
    assert.equal(Good.frames, 12); assert.equal(Good.rounds, 2); assert.equal(Good.label, "renderer=sdf"); assert.equal(Good.warmup, 3);
    assert.throws(() => ParseArguments(["--baseline", "a"]), /required/);
    assert.throws(() => ParseArguments(["--baseline", "a", "--variant", "b", "--frobnicate", "1"]), /unknown/);
    assert.throws(() => ParseArguments(["--baseline", "a", "--variant", "b", "--frames", "4"]), /exceed the warm-up/);
});

test("the summary table carries the honesty note and one row per toggle", () =>
{
    const Result = (Label, A, B) => ({ Label, Baseline: "renderer=bvh", Variant: `renderer=bvh&${Label}`, Machine: { Cpu: "Test CPU", Cores: 4, Node: "v22", SwiftShader: "pkg" }, Comparison: Compare([A, A + 1], [B, B + 1]) });
    const Table = RenderTable([Result("screentraces=1", 1000, 800), Result("lightgrid=0", 1000, 1001)], { Cpu: "Test CPU", Cores: 4, Node: "v22", SwiftShader: "pkg" });
    assert.match(Table, /no GPU/); assert.match(Table, /relative/); assert.match(Table, /Test CPU × 4 cores/);
    assert.match(Table, /\| screentraces=1 \|.*\*\*faster\*\*/); assert.match(Table, /\| lightgrid=0 \|.*\*\*no measurable difference\*\*/);
    assert.equal(Table.split("\n").filter((Line) => /^\| (screentraces|lightgrid)/.test(Line)).length, 2);
    const Directory = fs.mkdtempSync(path.join(os.tmpdir(), "bench-"));
    fs.writeFileSync(path.join(Directory, "bench-a.json"), JSON.stringify(Result("screentraces=1", 1000, 800)));
    fs.writeFileSync(path.join(Directory, "ignored.json"), JSON.stringify({ not: "a result" }));
    assert.match(SummariseDirectory([Directory]), /screentraces=1/);
    assert.match(SummariseDirectory([path.join(Directory, "nothing")]), /No benchmark results/);
});

test("plumbing: harness + BenchHooks + RunBenchmark produce a result file on the null backend", { timeout: 300000 }, () =>
{
    const Out = path.join(fs.mkdtempSync(path.join(os.tmpdir(), "bench-")), "bench-plumbing.json");
    const Run = spawnSync(process.execPath, [path.join(Here, "RunBenchmark.mjs"), "--label", "plumbing", "--baseline", "renderer=bvh", "--variant", "renderer=sdf", "--frames", "6", "--rounds", "1", "--warmup", "2", "--out", Out],
                          { cwd: Here, encoding: "utf8", env: { ...process.env, BACKEND: "null", SIZE: "320x180" }, timeout: 280000 });
    assert.equal(Run.status, 0, (Run.stdout + Run.stderr).slice(-1500));
    const Result = JSON.parse(fs.readFileSync(Out, "utf8"));
    assert.equal(Result.Runs.length, 2); assert.deepEqual(Result.Runs.map((Entry) => Entry.Which), ["baseline", "variant"]);
    for (const Entry of Result.Runs) { assert.ok(Entry.MedianMs >= 0 && Entry.DurationsMs.length === 6, "every executed frame was timed"); }
    assert.ok(["faster", "slower", "no measurable difference", "no data"].includes(Result.Comparison.Verdict));
    assert.match(Result.Machine.SwiftShader, /sparticuz/);
});

// ---- the check-run reporter (Tools/PostCheckRun.mjs) ----
import { FilterTap, Fit, BuildRequest, ParseArguments as ParseReportArguments, MaximumText } from "./PostCheckRun.mjs";

test("reporter: TAP output is cut down to counts, names, failures and printed diagnostics", () =>
{
    const Tap = ["TAP version 13", "# Subtest: a", "    ok 1 - inner", "ok 1 - alpha", "  ---", "  duration_ms: 1.5", "  ...", "# Subtest: b", "not ok 2 - beta", "  ---", "  duration_ms: 2.5",
                 "  error: 'boom'", "  code: 'ERR_ASSERTION'", "  expected: 255", "  actual: 244", "  stack: |-", "    at x", "  ...", "ok 3 - gamma # SKIP", "  # measured 12 of 20736 pixels differ", "1..3", "# tests 3", "# pass 1", "# fail 1", "# skipped 1", "# duration_ms 99"].join("\n");
    const Kept = FilterTap(Tap).split("\n");
    assert.ok(Kept.includes("ok 1 - alpha") && Kept.includes("not ok 2 - beta") && Kept.includes("ok 3 - gamma # SKIP") && Kept.includes("    ok 1 - inner"));
    assert.ok(Kept.includes("  error: 'boom'") && Kept.includes("  expected: 255") && Kept.includes("  actual: 244"), "the failing test's message is kept");
    assert.ok(Kept.includes("# tests 3") && Kept.includes("# fail 1") && Kept.includes("  # measured 12 of 20736 pixels differ"), "counts and printed diagnostics are kept");
    assert.ok(!Kept.some((Line) => /duration_ms: 1\.5|stack:|at x|Subtest/.test(Line)), "passing tests' YAML, stacks and subtest headers are dropped");
    assert.equal(FilterTap("plain text\nno tap"), "", "non-TAP lines are not invented");
});

test("reporter: the request is a valid completed check run and fits the API's limits", () =>
{
    const Long = Array.from({ length: 5000 }, (_, Index) => `line ${Index} with some text`).join("\n");
    assert.ok(Fit(Long).length < MaximumText + 100 && Fit(Long).endsWith("line 4999 with some text") && /characters cut from the start/.test(Fit(Long)), "the end is kept");
    assert.equal(Fit("short"), "short");
    const Request = BuildRequest({ Name: "Report: unit tests", Title: "87 passed, 0 failed", Summary: "Unit tests", Text: "a ``` fence", Conclusion: "success", Sha: "a".repeat(40), Repository: "o/r" });
    assert.equal(Request.Url, "https://api.github.com/repos/o/r/check-runs");
    assert.equal(Request.Body.status, "completed"); assert.equal(Request.Body.conclusion, "success"); assert.equal(Request.Body.head_sha, "a".repeat(40));
    assert.ok(Request.Body.output.text.startsWith("```text\n") && Request.Body.output.text.endsWith("\n```") && !/a ``` fence/.test(Request.Body.output.text), "a fence inside the text cannot close the block");
    assert.ok(JSON.stringify(Request.Body).length < 65535 + 2000);
    assert.throws(() => BuildRequest({ Name: "x", Conclusion: "green", Sha: "s", Repository: "o/r" }), /not a check-run conclusion/);
    assert.throws(() => BuildRequest({ Name: "", Conclusion: "success", Sha: "s", Repository: "o/r" }), /required/);
    const Parsed = ParseReportArguments(["--name", "n", "--title", "t", "--conclusion", "failure", "--filter", "tap", "--dry"]);
    assert.equal(Parsed.name, "n"); assert.equal(Parsed.conclusion, "failure"); assert.equal(Parsed.dry, true);
    assert.throws(() => ParseReportArguments(["--bogus", "1"]), /unknown option/);
});

import { TapTitle } from "./PostCheckRun.mjs";
import { RenderCpuTable } from "./BenchmarkStatistics.mjs";

test("reporter: a TAP title from the counts, raw markdown mode, and the CPU table", () =>
{
    assert.equal(TapTitle("# tests 96\n# pass 87\n# fail 0\n# skipped 9\n"), "87 passed, 0 failed, 9 skipped");
    assert.equal(TapTitle("no tap here"), "");
    const Raw = BuildRequest({ Name: "n", Title: "t", Text: "| a | b |\n|---|---|", Conclusion: "neutral", Sha: "s", Repository: "o/r", Raw: true });
    assert.equal(Raw.Body.output.text, "| a | b |\n|---|---|", "markdown goes through unwrapped");
    assert.equal(ParseReportArguments(["--raw"]).raw, true);
    const Table = RenderCpuTable({ Machine: { Cpu: "Test CPU", Cores: 4, Node: "v22" }, AtlasBuild: { MedianMs: 16.76, MinMs: 14.7, MaxMs: 18.5 }, EmissiveInstances: 62, EmissiveFullRebuild: { MedianMs: 2.7, MinMs: 2.5, MaxMs: 3.6 } });
    assert.match(Table, /Test CPU × 4 cores/); assert.match(Table, /\| AtlasBuild \| 16\.760 \| 14\.700 \| 18\.500 \|/); assert.match(Table, /EmissiveInstances: 62/);
    // the summariser reads a directory holding both kinds of file
    const Directory = fs.mkdtempSync(path.join(os.tmpdir(), "bench-"));
    fs.writeFileSync(path.join(Directory, "cpu-bench.json"), JSON.stringify({ Machine: { Cpu: "Test CPU", Cores: 4, Node: "v22" }, AtlasBuild: { MedianMs: 1, MinMs: 1, MaxMs: 1 }, EmissiveFullRebuild: { MedianMs: 2, MinMs: 2, MaxMs: 2 } }));
    fs.writeFileSync(path.join(Directory, "bench-x.json"), JSON.stringify({ Label: "x", Baseline: "a", Variant: "b", Machine: {}, Comparison: Compare([100, 101], [80, 81]) }));
    const Markdown = SummariseDirectory([Directory]);
    assert.match(Markdown, /### Toggle benchmarks/); assert.match(Markdown, /### CPU microbenchmarks/); assert.match(Markdown, /\*\*faster\*\*/);
});

import { EscapeData, EscapeProperty, Annotation, AnnotationLimit } from "./PostCheckRun.mjs";

test("reporter: annotations are escaped the way the runner expects and cannot be split or forged", () =>
{
    assert.equal(EscapeData("a%b\nc\r\nd"), "a%25b%0Ac%0D%0Ad");
    assert.equal(EscapeProperty("Report: a, b"), "Report%3A a%2C b");
    const Line = Annotation("Report: unit tests: 87 passed, 0 failed", "# pass 87\n# fail 0\n::error::not a command at line start");
    assert.ok(Line.startsWith("::notice title=Report%3A unit tests%3A 87 passed%2C 0 failed::"), Line.slice(0, 80));
    assert.ok(!Line.includes("\n"), "the whole annotation is one line: a newline would end the command and let the text forge another");
    assert.ok(Line.includes("%0A%0A") === false && Line.includes("# pass 87%0A# fail 0%0A::error::not a command"), "newlines are encoded, so a '::error::' inside the text is data, not a command");
    assert.ok(Annotation("t", "x".repeat(100000)).length < AnnotationLimit + 400, "long text is cut to the API's limit, keeping the end");
    assert.equal(Annotation("t", "x", "warning"), "::warning title=t::x");
});
