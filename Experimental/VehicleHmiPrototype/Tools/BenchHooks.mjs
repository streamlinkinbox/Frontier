//============================================================================================================================================
//                                                            BENCHHOOKS.MJS
//============================================================================================================================================
// 🧩 A HeadlessHarness hook that times every executed frame and prints one line:  BENCH {json}
//
//    HOOKS=BenchHooks.mjs BENCH_QUERY="renderer=bvh" node RunOnSoftwareGpu.mjs node HeadlessHarness.mjs .. 12 "?capture=1&quality=low&renderer=bvh"
//
// 📝 What the number is. The harness awaits BeforeFrame before it hands the host a frame callback, and this hook first waits for the queue to drain, so no frame is ever in flight
//    when the next begins: the time between two BeforeFrame calls is one whole frame, the host's CPU encoding plus the adapter's execution of it. On a software adapter
//    (SwiftShader, which is what a GitHub runner has) that is the cost of the WGSL on CPU cores: a fair measure of RELATIVE ALU / branch work between two configurations,
//    NOT of what a GPU would do (no occupancy, no memory hierarchy, no fixed-function rasteriser). The first BENCH_WARMUP frames are reported but left out of the statistics:
//    they carry the first relighting passes and lazy allocations.

import { Summarise } from "./BenchmarkStatistics.mjs";

const Stamps = [];
let Last = -1;

export default {
    async BeforeFrame(Host, Frame)
    {
        if (!Host.Ready) return;
        await Host.Exchange.Device.queue.onSubmittedWorkDone();
        if (Frame === Last) return;                 // a callback the host skipped
        Last = Frame;
        Stamps.push({ Frame, Time: performance.now() });
    },

    async Report(Host)
    {
        await Host.Exchange.Device.queue.onSubmittedWorkDone();
        const End = performance.now();
        const Durations = Stamps.map((Stamp, Index) => (Index + 1 < Stamps.length ? Stamps[Index + 1].Time : End) - Stamp.Time);
        const Warmup = Number(process.env.BENCH_WARMUP || 3);
        const Statistics = Summarise(Durations.slice(Warmup));
        console.log("BENCH " + JSON.stringify({ Query: process.env.BENCH_QUERY || "", Frames: Durations.length, Warmup, ...Statistics, DurationsMs: Durations.map((Value) => Math.round(Value)) }));
    }
};
