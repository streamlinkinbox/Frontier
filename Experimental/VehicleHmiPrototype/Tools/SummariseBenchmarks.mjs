//============================================================================================================================================
//                                                       SUMMARISEBENCHMARKS.MJS
//============================================================================================================================================
// 🧩 Turns the bench-*.json files the matrix jobs wrote (Tools/RunBenchmark.mjs) into one markdown table.
//
//    node SummariseBenchmarks.mjs <directory or files...> [--out summary.md]        (appends to $GITHUB_STEP_SUMMARY too, when set)

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { RenderTable, RenderCpuTable } from "./BenchmarkStatistics.mjs";

// ➡️ The files the matrix jobs wrote: toggle results (Comparison + Label) and the CPU microbenchmark (cpu-bench.json).
export function CollectFiles(Inputs)
{
    const Files = [];
    for (const Input of Inputs)
    {
        if (fs.existsSync(Input) && fs.statSync(Input).isDirectory())
            for (const Entry of fs.readdirSync(Input, { recursive: true })) { if (String(Entry).endsWith(".json") && /bench[-.]/.test(String(Entry))) Files.push(path.join(Input, String(Entry))); }
        else if (fs.existsSync(Input)) Files.push(Input);
    }
    return Files.sort().map((File) => ({ File, Result: JSON.parse(fs.readFileSync(File, "utf8")) }));
}

export const CollectResults = (Inputs) => CollectFiles(Inputs).map(({ Result }) => Result).filter((Result) => Result.Comparison && Result.Label);

export function Summarise(Inputs)
{
    const Files = CollectFiles(Inputs);
    const Results = Files.map(({ Result }) => Result).filter((Result) => Result.Comparison && Result.Label);
    const Cpu = Files.map(({ Result }) => Result).find((Result) => Result.AtlasBuild && Result.EmissiveFullRebuild);
    if (!Results.length && !Cpu) return "_No benchmark results were found._";
    return [Results.length ? "### Toggle benchmarks (software rasteriser)\n\n" + RenderTable(Results, Results[0].Machine) : "", Cpu ? "### CPU microbenchmarks\n\n" + RenderCpuTable(Cpu) : ""].filter(Boolean).join("\n\n");
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url))
{
    const Arguments = process.argv.slice(2);
    const OutIndex = Arguments.indexOf("--out");
    const Out = OutIndex >= 0 ? Arguments.splice(OutIndex, 2)[1] : "";
    const Markdown = Summarise(Arguments.length ? Arguments : ["."]);
    console.log(Markdown);
    if (Out) fs.writeFileSync(Out, Markdown + "\n");
    if (process.env.GITHUB_STEP_SUMMARY) fs.appendFileSync(process.env.GITHUB_STEP_SUMMARY, "## Benchmarks\n\n" + Markdown + "\n");
}
