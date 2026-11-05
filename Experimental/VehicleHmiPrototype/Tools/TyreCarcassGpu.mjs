//============================================================================================================================================
//                                                          TYRECARCASSGPU.MJS
//============================================================================================================================================
// 🧩 Runs the XPBD tyre carcass on a real (software) GPU and prints load–deflection results as JSON. Used by TyreCarcass.test.mjs:
//        node RunOnSoftwareGpu.mjs node TyreCarcassGpu.mjs '{"Cases":[{"Deflection":0.01,"Gauge":220000}]}'
// 📝 Each case presses ONE tyre onto a flat road (Y = 0) with its hub held at OuterRadius − Deflection, optionally rolling, and reads the frame-mean load back.

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { create, globals } from "webgpu";

Object.assign(globalThis, globals);
const Root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const Gpu = create([`backend=${process.env.BACKEND || "vulkan"}`]);
Object.defineProperty(globalThis, "navigator", { value: { gpu: Gpu, userAgent: "node" }, configurable: true });
globalThis.fetch = async (Location) =>
{
    const File = path.join(Root, String(Location).split("?")[0]);
    if (!fs.existsSync(File)) return { ok: false, status: 404, text: async () => "", arrayBuffer: async () => new ArrayBuffer(0) };
    const Bytes = fs.readFileSync(File);
    return { ok: true, status: 200, text: async () => Bytes.toString("utf8"), arrayBuffer: async () => Bytes.buffer.slice(Bytes.byteOffset, Bytes.byteOffset + Bytes.byteLength) };
};
const { DeviceExchange } = await import(pathToFileURL(path.join(Root, "Source/DeviceExchange.js")).href);
const { ShaderInterchange } = await import(pathToFileURL(path.join(Root, "Source/ShaderInterchange.js")).href);
const { TyreCarcassSolver, ResolveCarcassPose, ParseTyreLattice } = await import(pathToFileURL(path.join(Root, "Source/TyreCarcassSolver.js")).href);

const Request = JSON.parse(process.argv[2] || '{"Cases":[{"Deflection":0.01,"Gauge":220000}]}');
const Exchange = new DeviceExchange();
await Exchange.ConstructDevice();
const Shaders = new ShaderInterchange(Exchange, "Shader");
const Lattice = new Uint8Array(fs.readFileSync(path.join(Root, "Archive/TyreLattice.bin")));
const { TreadThickness } = await import(pathToFileURL(path.join(Root, "Source/TyreCarcassSolver.js")).href);
const Outer = ParseTyreLattice(Lattice).OuterRadius + TreadThickness;   // the rolling radius: lattice outer radius plus the tread the contact stands outside it
const Sleep = (Milliseconds) => new Promise((Resolve) => setTimeout(Resolve, Milliseconds));

async function RunCase(Case)
{
    const Frames = Case.Frames || 60, Substeps = Case.Substeps || 32, Speed = Case.Speed || 0.0, Dt = 1 / 60;
    const Solver = new TyreCarcassSolver(Exchange, Shaders, 1, Substeps);
    const HubY = Outer - Case.Deflection;
    const Pose = (X, Spin) => ResolveCarcassPose([X, 0, HubY], 0, Spin, [Speed, 0, 0], Speed / (Outer - 0.012), 0);
    await Solver.ConstructResources(Lattice, [{ ...Pose(0, 0) }]);
    let X = 0, Spin = 0;
    const Series = [];
    for (let Frame = 0; Frame < Frames; ++Frame)
    {
        X += Speed * Dt; Spin += Speed / (Outer - 0.012) * Dt;
        const Encoder = Exchange.Device.createCommandEncoder();
        Solver.Record(Encoder, [{ End: Pose(X, Spin), Ground: 0.0, Gauge: Case.Gauge }], Dt);
        Exchange.Device.queue.submit([Encoder.finish()]);
        Solver.Harvest();
        await Exchange.Device.queue.onSubmittedWorkDone();
        await Sleep(2);
        if (Frame % 10 === 9) Series.push(Solver.Readings[0].Load);
    }
    await Sleep(200);
    const Reading = { ...Solver.Readings[0] };
    return { Case, Reading, Series };
}

const Results = [];
for (const Case of Request.Cases) Results.push(await RunCase(Case));
console.log("RESULT " + JSON.stringify({ Outer, Results }));
process.exit(0);
