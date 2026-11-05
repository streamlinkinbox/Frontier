//============================================================================================================================================
//                                                           GARAGEHOOKS.MJS
//============================================================================================================================================
// 🧩 Drives the Garage's ReSTIR view from a script under the headless harness and writes out what it rendered.
//
//    HOOKS=GarageHooks.mjs GARAGE_PLAN=scan GARAGE_OUT=out node HeadlessHarness.mjs .. 8 "?capture=1&quality=low&lobby=1&garage=192x120"
//
//    The view is forced on (no tablet is open in the harness) and its own clocks are stepped by the plan, not by the harness's 16.7 ms frames:
//    a scan sweep is 4.6 s, a software rasteriser draws a handful of frames. GARAGE_OUT is a directory; each dump is <plan>-<name>.png, the
//    preview texture exactly as the tablet's well would show it. @napi-rs/canvas (a devDependency of Tools) gives the HUD a real 2D canvas.
//
//    Plans: orbit   rotate, zoom, pick — the camera only orbits
//           paint   a drop at a picked point of the body: the ripple's frames
//           scan    the X-ray scan across its sweep: transparency, ghost, wireframe, curtain, particles, HUD

import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";

const Plan = process.env.GARAGE_PLAN || "orbit";
const Out  = process.env.GARAGE_OUT || "";
const SweepStep  = Number(process.env.GARAGE_STEP || 1.0);   // [s] of scan clock between the plan's pictures
// 📝 Paint ripple seconds the plan adds before each picture (frames 3, 4, ...): fine at first, where the wave crosses the car, then to its end.
//    The host adds up to 0.05 s of its own on every frame; each picture's caption reads the clock it was drawn with.
const RippleSteps = process.env.GARAGE_STEP ? [Number(process.env.GARAGE_STEP)] : [0.05, 0.1, 0.1, 0.15, 0.2, 0.3, 0.6, 1.0];
let Canvas = null;
try { Canvas = await import("@napi-rs/canvas"); } catch { console.log("no @napi-rs/canvas: the HUD's text is not drawn"); }

// 📝 The preview texture as RGBA8 rows (the exchange's canvas format may be BGRA).
export async function ReadPreview(Host)
{
    const Garage = Host.Garage;
    const Device = Host.Exchange.Device;
    const [Width, Height] = Garage.Extent;
    const Stride = Math.ceil(Width * 4 / 256) * 256;
    const Records = Device.createBuffer({ size: Stride * Height, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
    const Encoder = Device.createCommandEncoder();
    Encoder.copyTextureToBuffer({ texture: Garage.PreviewTexture }, { buffer: Records, bytesPerRow: Stride }, [Width, Height]);
    Device.queue.submit([Encoder.finish()]);
    await Records.mapAsync(GPUMapMode.READ);
    const Source = new Uint8Array(Records.getMappedRange());
    const Pixels = new Uint8Array(Width * Height * 4);
    const Swap = Host.Exchange.CanvasFormat.startsWith("bgra");
    for (let Row = 0; Row < Height; ++Row)
        for (let Column = 0; Column < Width; ++Column)
        {
            const From = Row * Stride + Column * 4, To = (Row * Width + Column) * 4;
            Pixels[To]     = Source[From + (Swap ? 2 : 0)];
            Pixels[To + 1] = Source[From + 1];
            Pixels[To + 2] = Source[From + (Swap ? 0 : 2)];
            Pixels[To + 3] = 255;
        }
    Records.unmap();
    Records.destroy();
    return { Width, Height, Pixels };
}

async function Dump(Host, Name)
{
    await Host.Exchange.Device.queue.onSubmittedWorkDone();
    const { Width, Height, Pixels } = await ReadPreview(Host);
    let Sum = 0, Bright = 0;
    for (let Index = 0; Index < Pixels.length; Index += 4)
    {
        const Luma = Pixels[Index] + Pixels[Index + 1] + Pixels[Index + 2];
        Sum += Luma;
        if (Luma > 60) Bright++;
    }
    const Drawn = { Ripple: Host.Garage.Metrics.Ripple, Scan: Host.Garage.Metrics.ScanDrawn };
    console.log(`garage ${Name}: ${Width}x${Height} mean ${(Sum / (Width * Height * 3)).toFixed(1)} lit ${(100 * Bright / (Width * Height)).toFixed(1)} %`
              + ` ripple ${Drawn.Ripple.Elapsed.toFixed(3)} s radius ${Drawn.Ripple.Radius.toFixed(2)} m, scan ${Drawn.Scan.Seconds.toFixed(2)} s front ${Drawn.Scan.Front.toFixed(2)} m`);
    if (Out)
    {
        fs.mkdirSync(Out, { recursive: true });
        fs.writeFileSync(path.join(Out, `${Plan}-${Name}.png`), globalThis.EncodePng(Pixels, Width, Height));
        fs.writeFileSync(path.join(Out, `${Plan}-${Name}.json`), JSON.stringify({ Name, Width, Height, ...Drawn }));
    }
    return Pixels;
}

// 📝 The scan clock takes steps of at most 0.05 s (the reference's own clamp), so a long advance is many of them.
function StepScan(Garage, Seconds)
{
    for (let Remaining = Seconds; Remaining > 1.0e-9; Remaining -= 0.05)
        Garage.Scan.Advance(Math.min(0.05, Remaining));
}

function StepRipple(Appearance, Seconds)
{
    for (let Remaining = Seconds; Remaining > 1.0e-9; Remaining -= 0.05)
        Appearance.Advance(Math.min(0.05, Remaining));
}

const Dumps = {};
export default {
    async BeforeFrame(Host, Frame)
    {
        const Garage = Host.Garage;
        if (Frame === 0)
        {
            Garage.Forced = true;
            if (Canvas)
            {
                Garage.Hud.CreateCanvas = (Width, Height) => Canvas.createCanvas(Width, Height);
                Garage.Hud.Canvas = null;
                Garage.Hud.Resize(Garage.Extent[0], Garage.Extent[1]);
            }
        }
        if (Plan === "orbit")
        {
            if (Frame === 2) { Garage.Rotate(90, 0); Garage.Zoom(120); }
            if (Frame >= 2 && Frame < 6) Garage.Orbit.Advance(0.25);
            if (Frame === 3) Dumps.Before = await Dump(Host, "start");
        }
        else if (Plan === "paint")
        {
            const Appearance = Host.Cabin.Appearance;
            if (Frame === 2)
            {
                Dumps.Before = await Dump(Host, "ripple-00-before");   // the last picture drawn: the car as it opens, before the drop
                const Pick = Garage.Pick(0.5, 0.55);
                assert.ok(Pick, "the picture's centre picks the car");
                Appearance.Paint({ Name: "Command Red", Hex: "#8a1a1d", Metallic: true }, "gloss", Pick.Point);
                console.log("paint drop at", Pick.Point.map((Value) => Value.toFixed(2)).join(", "), "occupant", Pick.Occupant);
            }
            if (Frame >= 3 && Frame - 3 < RippleSteps.length) StepRipple(Appearance, RippleSteps[Frame - 3]);
            if (Frame >= 3) Dumps[`Ripple${Frame}`] = await Dump(Host, `ripple-${String(Frame - 2).padStart(2, "0")}`);
        }
        else if (Plan === "scan")
        {
            if (Frame === 1) Garage.AssignScan(true);
            if (Frame >= 1) StepScan(Garage, Frame === 1 ? 0.2 : SweepStep);
            if (Frame >= 2) Dumps[`Scan${Frame}`] = await Dump(Host, `sweep-${String(Frame).padStart(2, "0")}`);
        }
    },
    async Report(Host)
    {
        const Garage = Host.Garage;
        assert.ok(Garage.Metrics.Recorded > 0, "the Garage view recorded frames");
        console.log(`Garage view: ${Garage.Metrics.Recorded} frames, ${Garage.Metrics.HudUploads} HUD uploads, ${Garage.EdgeCount} wire segments, ${Garage.SparkleCount} particles`);
    }
};
