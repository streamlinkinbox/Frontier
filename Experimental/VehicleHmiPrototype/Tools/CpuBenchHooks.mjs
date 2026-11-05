//============================================================================================================================================
//                                                          CPUBENCHHOOKS.MJS
//============================================================================================================================================
// 🧩 CPU-side costs of the new structures, measured on the real scene (Dawn's null backend is enough: nothing here touches a shader). Unlike the toggle benchmarks these
//    ARE meaningful on a GPU-less runner: they are JavaScript doing what it does in the browser every frame, so a regression here is a real one (V8 is V8; only the
//    clock speed of the runner differs).
//
//    HOOKS=CpuBenchHooks.mjs node HeadlessHarness.mjs .. 3 "?capture=1"        prints  CPUBENCH {json}

import fs from "node:fs";
import path from "node:path";
import os from "node:os";
import { fileURLToPath } from "node:url";
import { ConstructPhotometricAtlas, PhotometryProfileFiles } from "../Source/Photometry.js";
import { Summarise } from "./BenchmarkStatistics.mjs";

const Here = path.dirname(fileURLToPath(import.meta.url));

// Milliseconds per call: a few warm-up calls for the JIT, then Iterations timed calls.
function Time(Call, Iterations, Warmup = 5)
{
    for (let Index = 0; Index < Warmup; ++Index) Call(Index);
    const Times = [];
    for (let Index = 0; Index < Iterations; ++Index) { const Started = performance.now(); Call(Index + Warmup); Times.push(performance.now() - Started); }
    const { MedianMs, MeanMs, MinMs, MaxMs } = Summarise(Times);
    return { MedianMs, MeanMs, MinMs, MaxMs, Iterations };
}

export default {
    BeforeFrame() {},

    async Report(Host)
    {
        const Result = { Machine: { Cpu: os.cpus()[0]?.model?.trim() ?? "unknown", Cores: os.cpus().length, Node: process.version } };
        // 1. the photometric atlas from the seven real files (a start-up cost: the FREGAT roadway file alone is 560 KB)
        const Texts = Object.fromEntries(PhotometryProfileFiles.map((Name) => [Name, fs.readFileSync(path.join(Here, "..", "Profiles", `${Name}.ies`), "utf8")]));
        Result.AtlasBuild = Time(() => ConstructPhotometricAtlas(Texts), 15, 2);

        // 2. the emissive light structure on the real scene (62 cluster instances, 1024 cells)
        const Emissive = Host.Emissive, Scene = Host.Scene, Materials = Host.Materials;
        Result.EmissiveInstances = Emissive.Count; Result.EmissiveTriangles = Emissive.TriangleCount;
        Result.EmissiveFullRebuild = Time(() => { Emissive.StaticSignature = ""; Emissive.Update(Scene, Materials, 1.0); }, 40);
        const Dynamic = Emissive.Instances.find((Instance) => Instance.Dynamic);
        if (Dynamic)
        {
            // the cost EVERY FRAME while the car drives: its transform changed, so its cluster instances move and the cell lists are re-ranked
            const Transform = Scene.Occupants[Dynamic.Occupant].Transform, Original = Float64Array.from(Transform);
            Result.EmissivePerFrameWhileDriving = Time((Index) => { Transform[12] = Original[12] + 0.01 * Index; Emissive.Update(Scene, Materials, 1.0); }, 60);
            Transform.set(Original); Emissive.Update(Scene, Materials, 1.0);
        }
        Result.EmissiveIdleFrame = Time(() => Emissive.Update(Scene, Materials, 1.0), 300);

        // 3. the analytic lights: the lighting system and the world light grid's per-frame update (what the host does every frame)
        const Device = Host.Exchange.Device;
        Result.LightsPerFrame = Time(() => { const Data = Host.Lighting.Update(Host.Vehicle.QueryBodyTransform(), Host.StreetLampPoles); Host.LightClusters.Update(Device, Data); }, 200);

        console.log("CPUBENCH " + JSON.stringify(Result));
    }
};
