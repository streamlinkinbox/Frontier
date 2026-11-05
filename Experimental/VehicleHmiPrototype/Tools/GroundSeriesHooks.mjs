import { ReadTexture, Summarise, EnableTextureReadback } from "./GpuProbe.mjs";
await EnableTextureReadback();
const Rows = []; let Last = -1, Wrapped = false; const Cameras = [];
export default {
    async BeforeFrame(Host, Frame) {
        if (!Wrapped) {
            Wrapped = true; const Original = Host.EncodeFrameRecord.bind(Host);
            Host.EncodeFrameRecord = (Camera, Light) => { Cameras.push({ Position: [...Camera.Position], Near: Camera.Near, Frame: Host.FrameIndex }); return Original(Camera, Light); };
        }
        if (Frame < 1 || Frame === Last) return; Last = Frame;
        const Device = Host.Exchange.Device;
        await Device.queue.onSubmittedWorkDone();
        const [W, H] = Host.ScreenExtent;
        const GL = [Math.floor(W * 0.02), Math.floor(H * 0.80), Math.floor(W * 0.22), Math.floor(H * 0.97)];
        const Att = Summarise(await ReadTexture(Device, Host.Surface.AttributeTexture, W, H, "rgba16float"), W, H, GL);
        const Cam = Cameras.filter((C) => C.Frame === Frame - 1).pop();
        const G = Host.Scene.Occupants[Host.Scene.GroundOccupant];
        Rows.push(`after frame ${String(Frame - 1).padStart(2)}: family ${Att.Mean[2].toFixed(0)} | camera ${Cam ? Cam.Position.map((V) => V.toFixed(4)).join(",") : "?"} near ${Cam ? Cam.Near : "?"} | ground inactive ${G.Inactive} roles ${G.Roles} tz ${G.Transform[14]}`);
    },
    async Report() { console.log(Rows.join("\n")); }
};
