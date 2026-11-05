//============================================================================================================================================
//                                                          PROFILETLASSKIP.MJS
//============================================================================================================================================
// 🧩 Runs the vehicle HMI demo headless, with the TLAS static skip (Renderer/Patches/0003) off and on, in the level and in the lobby, and writes
//    what the GPU was given in each case.
//
//        node Exhibits/VehicleHmiPrototype/ProfileTlasSkip.mjs          (from the repository root; about a minute, null GPU backend)
//
//    Writes tlas-skip-level.json and tlas-skip-lobby.json next to this file.
//
// 📝 What is compared: every write the host makes to the BVH node buffer is mirrored (Tools/TlasSkipHooks.mjs), and after each frame the mirror's
//    TLAS bytes are hashed. A skip changes nothing when the two runs hash the same on every frame. Each scene runs twice per setting: as it
//    comes, and with the vehicle solver frozen from frame 40 (VehicleSolver.Frozen) so that nothing moves, which is the case the skip is for.
//
// 📝 What the milliseconds are: JavaScript time of UpdateTlas on this machine's main thread. The renderer's cost on a graphics card is the GPU's
//    traversal, which is not measured here and cannot be measured on a software rasteriser.

import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import path from "node:path";
import fs from "node:fs";

const Here   = path.dirname(fileURLToPath(import.meta.url));
const Tools  = path.resolve(Here, "..", "..", "Experimental", "VehicleHmiPrototype", "Tools");
const Frames = Number(process.env.FRAMES || 90);
const Freeze = Math.floor(Frames * 0.45);
const Scenes = [
    { Name: "level", Query: "?capture=1&quality=low" },
    { Name: "lobby", Query: "?capture=1&quality=low&lobby=1" }
];

function Run(Scene, Skip, FreezeAt)
{
    const Result = path.join(Here, `.run-${Scene.Name}-${Skip}-${FreezeAt}.json`);
    const Process = spawnSync(process.execPath, ["HeadlessHarness.mjs", "..", String(Frames), Scene.Query], {
        cwd: Tools, encoding: "utf8", timeout: 300000,
        env: { ...process.env, HOOKS: "TlasSkipHooks.mjs", TLAS_SKIP: String(Skip), TLAS_OUT: Result,
               ...(FreezeAt >= 0 ? { TLAS_FREEZE_AT: String(FreezeAt) } : {}) }
    });
    if (Process.status !== 0 || !fs.existsSync(Result))
    {
        console.error(Process.stdout.slice(-2000), Process.stderr.slice(-2000));
        throw new Error(`${Scene.Name} skip=${Skip} freeze=${FreezeAt} failed`);
    }
    const Parsed = JSON.parse(fs.readFileSync(Result, "utf8"));
    fs.unlinkSync(Result);
    return Parsed;
}

let Failed = false;
for (const Scene of Scenes)
{
    const Summary = { Scene: Scene.Name, Query: Scene.Query, Frames, FreezeAt: Freeze, Settings: {} };
    for (const [Label, FreezeAt] of [["AsItComes", -1], ["VehicleFrozen", Freeze]])
    {
        const Off = Run(Scene, 0, FreezeAt);
        const On  = Run(Scene, 1, FreezeAt);
        let Differing = 0, FirstDifferent = -1;
        for (let Index = 0; Index < Off.Frames.length; ++Index)
        {
            if (Off.Frames[Index].Hash !== On.Frames[Index].Hash || Off.Frames[Index].Nodes !== On.Frames[Index].Nodes)
            {
                ++Differing;
                if (FirstDifferent < 0) FirstDifferent = Index;
            }
        }
        const Last = On.Frames.at(-1);
        Summary.Settings[Label] = {
            FramesCompared: Off.Frames.length, FramesDiffering: Differing, FirstDifferingFrame: FirstDifferent,
            DistinctTlasStates: new Set(Off.Frames.map((Entry) => Entry.Hash)).size,
            UsedTlasNodes: Last.Nodes, TlasBytes: Last.Nodes * 32, ActiveInstances: Last.Instances,
            SkipOff: { Rebuilt: Off.Rebuilt, Skipped: Off.Skipped, TlasWrites: Off.InFrame.TlasWrites, TlasBytesWritten: Off.InFrame.TlasBytesWritten, UpdateTlasMeanMs: Off.InFrame.MeanMs },
            SkipOn:  { Rebuilt: On.Rebuilt, Skipped: On.Skipped, TlasWrites: On.InFrame.TlasWrites, TlasBytesWritten: On.InFrame.TlasBytesWritten, UpdateTlasMeanMs: On.InFrame.MeanMs }
        };
        if (Label === "VehicleFrozen")
            Summary.UpdateTlasCalls = On.Micro;
        if (Differing !== 0)
            Failed = true;
        console.log(`${Scene.Name} / ${Label}: ${Off.Frames.length} frames compared, ${Differing} differ; TLAS ${Last.Nodes * 32} B; skip on skipped ${On.Skipped} of ${On.Skipped + On.Rebuilt}, `
                  + `writes ${On.InFrame.TlasWrites} vs ${Off.InFrame.TlasWrites}; UpdateTlas ${On.InFrame.MeanMs.toFixed(3)} ms vs ${Off.InFrame.MeanMs.toFixed(3)} ms`);
    }
    if (Summary.UpdateTlasCalls && !Summary.UpdateTlasCalls.ByteComparison.Identical)
        Failed = true;
    fs.writeFileSync(path.join(Here, `tlas-skip-${Scene.Name}.json`), JSON.stringify(Summary, null, 2) + "\n");
}
process.exit(Failed ? 1 : 0);
