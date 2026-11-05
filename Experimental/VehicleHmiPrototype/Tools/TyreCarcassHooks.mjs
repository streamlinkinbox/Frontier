// Reports the XPBD carcass ↔ vehicle coupling of a headless run as JSON lines (consumed by GpuExecuted.test.mjs and by hand).
const Rows = []; let Last = -1;
export default {
    async BeforeFrame(Host, Frame) {
        if (Frame === Last || Frame < 1) return; Last = Frame;
        // 📝 DRIVE=1 scripts a manoeuvre: settle, full throttle, then hard steering with throttle lift, then braking.
        if (process.env.DRIVE === "1")
        {
            const S = Number(process.env.SETTLE) || 20;
            Host.ScriptedInput = Frame < S ? { Throttle: 0, Brake: 0, Steer: 0, Handbrake: false }
                : Frame < S + 40 ? { Throttle: 1, Brake: 0, Steer: 0, Handbrake: false }
                : Frame < S + 70 ? { Throttle: 0.4, Brake: 0, Steer: 0.8, Handbrake: false }
                : { Throttle: 0, Brake: 1, Steer: 0, Handbrake: false };
        }
        if (Frame % (Number(process.env.SERIES_EVERY) || 20) !== 0) return;
        const V = Host.Vehicle, R = Host.TyreCarcass ? Host.TyreCarcass.Readings : [];
        Rows.push({
            Frame: Frame - 1, Z: V.Position[2], Speed: Math.hypot(V.LinearVelocity[0], V.LinearVelocity[1]),
            Wheels: V.Wheels.map((W, I) => ({ Load: W.Load, TyreForce: W.TyreForce, Deflection: W.TyreDeflection, Secant: W.CarcassSecant || 0, Contact: W.Contact,
                                              Reading: R[I] ? { Valid: R[I].Valid, Load: R[I].Load, Deflection: R[I].Deflection, PatchLength: R[I].PatchLength, PatchWidth: R[I].PatchWidth, Frame: R[I].Frame } : null }))
        });
    },
    async Report(Host) {
        console.log("TYRESERIES " + JSON.stringify({ Mass: Host.Vehicle.Wheels.length ? 1300 : 0, Rows, Dispatches: Host.TyreCarcass ? Host.TyreCarcass.Statistics.Dispatches : 0, Readbacks: Host.TyreCarcass ? Host.TyreCarcass.Statistics.Readbacks : 0 }));
    }
};
