//============================================================================================================================================
//                                                           TLASSKIPHOOKS.MJS
//============================================================================================================================================
// 🧩 Measures the TLAS static skip (Renderer/Patches/0003-tlas-skip-static.patch) under the headless harness, and proves it changes nothing the
//    GPU can see.
//
//    TLAS_SKIP=0|1 TLAS_FREEZE_AT=<frame> TLAS_OUT=<file.json> HOOKS=TlasSkipHooks.mjs node HeadlessHarness.mjs .. 90 "?capture=1&quality=low"
//
//    Every write the host makes to the BVH node buffer is mirrored into a CPU copy, so after each frame the TLAS the GPU holds is known byte for
//    byte; its hash goes into the result. Two runs of the same scene, skip off and skip on, must give the same hash on every frame. The
//    vehicle solver is frozen at TLAS_FREEZE_AT (VehicleSolver.Frozen: held where it is) so that part of the run has nothing moving, which is
//    the case the skip exists for. UpdateTlas is timed in the frames it is called in.

import fs from "node:fs";
import assert from "node:assert/strict";

const Skip      = process.env.TLAS_SKIP !== "0";
const FreezeAt  = process.env.TLAS_FREEZE_AT === undefined ? -1 : Number(process.env.TLAS_FREEZE_AT);
const Out       = process.env.TLAS_OUT || "";
const Loops     = Number(process.env.TLAS_LOOPS || 300);

const Frames = [];
let Mirror = null, MirrorBuffer = null, Writes = 0, WrittenBytes = 0, TimedCalls = 0, TimedMs = 0, Timed = false;

// 📝 Installed when this module is imported, which is before the host exists: the host's first TLAS upload happens while it is being built, and
//    the mirror has to see that one too. Matches the node buffer by its label, so no host object is needed.
const NodeBufferLabel = "RayTracing BVH nodes (TLAS + BLAS)";
const QueueWrite = globalThis.GPUQueue.prototype.writeBuffer;
globalThis.GPUQueue.prototype.writeBuffer = function (Buffer, Offset, Data, DataOffset = 0, Size)
{
    if (Buffer.label === NodeBufferLabel)
    {
        if (MirrorBuffer !== Buffer)
        {
            MirrorBuffer = Buffer;
            Mirror = new Uint8Array(Buffer.size);
        }
        const Bytes = new Uint8Array(Data.buffer ?? Data, (Data.byteOffset ?? 0) + DataOffset, Size ?? (Data.byteLength - DataOffset));
        Mirror.set(Bytes, Offset);
        if (Offset === 0) { ++Writes; WrittenBytes += Bytes.byteLength; }   // the TLAS is the one write that starts at node 0
    }
    return QueueWrite.call(this, Buffer, Offset, Data, DataOffset, Size);
};

// 📐 FNV-1a, 32 bits, over the TLAS bytes.
function HashBytes(Bytes, Count)
{
    let Hash = 0x811c9dc5;
    for (let Index = 0; Index < Count; ++Index)
    {
        Hash = Math.imul(Hash ^ Bytes[Index], 0x01000193) >>> 0;
    }
    return Hash;
}

function Snapshot(Host, Frame)
{
    const Rt = Host.RayTracing;
    const Bytes = Rt.UsedTlasNodes * 32;
    Frames.push({ Frame, Nodes: Rt.UsedTlasNodes, Instances: Rt.ActiveInstances, Hash: HashBytes(Mirror, Bytes),
                  Skipped: Rt.TlasSkipCount, Rebuilt: Rt.TlasRebuildCount, Writes, WrittenBytes });
}

export default {
    BeforeFrame(Host, Frame)
    {
        const Rt = Host.RayTracing;
        if (Frame === 0)
        {
            Rt.TlasSkipEnabled = Skip;
            const Update = Rt.UpdateTlas.bind(Rt);
            Rt.UpdateTlas = (...Arguments) =>
            {
                const Start = performance.now();
                Update(...Arguments);
                TimedMs += performance.now() - Start;
                ++TimedCalls;
            };
            Timed = true;
        }
        if (Frame > 0)
            Snapshot(Host, Frame - 1);
        if (Frame === FreezeAt)
            Host.Vehicle.Frozen = true;
    },

    // 📝 After the run: the last frame, then repeated calls on unchanged inputs — skip on, and forced off — timed, with the rebuilt bytes
    //    compared to what the skip left in place.
    Report(Host)
    {
        const Rt = Host.RayTracing;
        Snapshot(Host, Frames.length);
        const Micro = {};
        if (Skip && Loops > 0)
        {
            const Direct = Object.getPrototypeOf(Rt).UpdateTlas.bind(Rt);
            const Run = (Forced, Count) =>
            {
                Rt.TlasForceRebuild = Forced;
                const Skipped = Rt.TlasSkipCount, Rebuilt = Rt.TlasRebuildCount;
                const Start = performance.now();
                for (let Call = 0; Call < Count; ++Call) Direct(Host.Scene, Host.Vehicle);
                const Total = performance.now() - Start;
                Rt.TlasForceRebuild = false;
                return { Forced, Calls: Count, MeanMs: Total / Count, Skipped: Rt.TlasSkipCount - Skipped, Rebuilt: Rt.TlasRebuildCount - Rebuilt };
            };
            Host.Vehicle.Frozen = true;
            Direct(Host.Scene, Host.Vehicle);
            Run(false, 20); Run(true, 20);                                  // warm-up
            Micro.Cached = Run(false, Loops);
            Micro.Forced = Run(true, Loops);
            const Bytes = Rt.UsedTlasNodes * 32;
            Direct(Host.Scene, Host.Vehicle);
            const Before = Uint8Array.from(new Uint8Array(Rt.AllNodeBytes, 0, Bytes));
            Rt.TlasForceRebuild = true;
            Direct(Host.Scene, Host.Vehicle);
            Rt.TlasForceRebuild = false;
            const After = new Uint8Array(Rt.AllNodeBytes, 0, Bytes);
            let FirstDifferentByte = -1;
            for (let Index = 0; Index < Bytes; ++Index) if (Before[Index] !== After[Index]) { FirstDifferentByte = Index; break; }
            Micro.ByteComparison = { Bytes, Identical: FirstDifferentByte < 0, FirstDifferentByte, UsedTlasNodes: Rt.UsedTlasNodes, ActiveInstances: Rt.ActiveInstances };
            Micro.MeanMsSaved = Micro.Forced.MeanMs - Micro.Cached.MeanMs;
        }
        const Last = Frames.at(-1);
        const Result = { Skip, FreezeAt, FramesRun: Frames.length, InFrame: { Calls: Last.Rebuilt + Last.Skipped, MeanMs: TimedMs / Math.max(1, TimedCalls), TlasWrites: Last.Writes, TlasBytesWritten: Last.WrittenBytes },
                         Skipped: Last.Skipped, Rebuilt: Last.Rebuilt, Frames, Micro };
        if (Out)
        {
            fs.writeFileSync(Out, JSON.stringify(Result));
        }
        console.log(`TLAS skip ${Skip ? "on" : "off"}: ${Frames.length} frames, ${Result.Skipped} skipped, ${Result.Rebuilt} rebuilt, `
                  + `${Last.Nodes} nodes (${Last.Nodes * 32} B), ${Last.Writes} TLAS writes (${Last.WrittenBytes} B), in-frame UpdateTlas ${Result.InFrame.MeanMs.toFixed(3)} ms`);
        assert.ok(Timed);
    }
};
