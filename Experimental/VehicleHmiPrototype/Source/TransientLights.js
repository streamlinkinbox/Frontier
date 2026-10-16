// Eight reusable light slots, never one light/shadow per particle. Same record feeds
// screen direct lighting and surface-card direct lighting (and hence indirect GI).
import { ResolveSparkRadiance, ResolveSparkGain } from './SparkSequence.js';
export const TransientLightCapacity = 8;
export const TransientLightFloats = TransientLightCapacity * 8;
const Luma = RGB => RGB[0] * 0.2126 + RGB[1] * 0.7152 + RGB[2] * 0.0722;
const LinearHex = Hex => [1,3,5].map(Start => {
    const C = parseInt(Hex.slice(Start, Start + 2), 16) / 255;
    return C <= 0.04045 ? C / 12.92 : ((C + 0.055) / 1.055) ** 2.4;
});
export function BuildTransientLights(Sparks, Effects) {
    const Lights = [], Bins = new Map();
    if (Sparks.Settings.Enabled) for (const Spark of Sparks.Particles) {
        const RGB = ResolveSparkRadiance(Spark.Kelvin).map(C => C * Sparks.Settings.Brightness * Spark.Size * Spark.Size * ResolveSparkGain(Spark) * 0.012);
        const Energy = Luma(RGB);
        if (!(Energy > 0)) continue;
        const Key = Spark.Position.map(Value => Math.floor(Value / 1.5)).join(',');
        const Bin = Bins.get(Key) || { Position: [0,0,0], Colour: [0,0,0], Energy: 0, Reach: 4 };
        Bin.Energy += Energy;
        for (let Axis = 0; Axis < 3; ++Axis) { Bin.Position[Axis] += Spark.Position[Axis] * Energy; Bin.Colour[Axis] += RGB[Axis]; }
        Bins.set(Key, Bin);
    }
    for (const Bin of [...Bins.values()].sort((A,B) => B.Energy - A.Energy).slice(0,4)) {
        Bin.Position = Bin.Position.map(V => V / Bin.Energy);
        Bin.Colour = Bin.Colour.map(C => C * Math.min(1, 20 / Bin.Energy));
        Lights.push(Bin);
    }
    const S = Effects.Settings;
    for (const Emitter of Effects.Emitters) {
        if (!Emitter || Emitter.Kind === 4 || Effects.Clock < Emitter.Start || Effects.Clock >= Emitter.End) continue;
        const Age = Effects.Clock - Emitter.Start;
        const Duration = Emitter.End - Emitter.Start;
        // Smooth rise and fade; reduced-light proxy follows the bulk of released/arriving cubes.
        const Envelope = Math.min(1, Age / 0.12) * Math.min(1, (Duration - Age) / 0.65)
            * Math.exp(-Math.max(0, Age - ((S.HoldOffset || 0) + (S.Wave || 1))) / 0.8);
        const Power = Math.min(35, Math.max(0, S.Intensity * (S.Faces ? S.GlowStrength : 6))) * Envelope;
        if (!(Power > 0)) continue;
        const C = Emitter.Centre, M = Emitter.Transform;
        const Drift = Emitter.Kind % 2 === 0 ? (1 - Math.exp(-S.Drag * Age)) / Math.max(S.Drag, 0.001) : 0;
        const Position = [0,1,2].map(Axis => M[12 + Axis] + M[Axis] * C[0] + M[4 + Axis] * C[1] + M[8 + Axis] * C[2] + Emitter.Velocity[Axis] * Drift);
        const Top = Emitter.Kind < 2 ? (Effects.Bounds?.Maximum?.[2] ?? 1.35) : (Emitter.Extent?.[2] ?? 0.35);
        Position[2] += Top + 0.15; // keep the proxy outside the emitting shell to avoid self-occlusion
        // Face-colour mode uses the configured vehicle coat, not an unrelated cyan light.
        const RGB = S.Faces ? (Effects.LightAlbedo || [0.45,0.65,0.5]) : LinearHex(S.Glow);
        Lights.push({ Position, Colour: RGB.map(Channel => Channel * Power), Energy: Power, Reach: 9 });
    }
    const Selected = Lights.sort((A,B) => B.Energy - A.Energy).slice(0, TransientLightCapacity);
    const Data = new Float32Array(TransientLightFloats);
    // Whole-pool energy ceiling; HDR emissive surfaces remain independent.
    const Budget = Selected.reduce((Sum, Light) => Sum + Luma(Light.Colour), 0);
    const Scale = Math.min(1, 100 / Math.max(Budget, 0.001));
    Selected.forEach((Light, Index) => Data.set([...Light.Position, Light.Reach, ...Light.Colour.map(C => C * Scale), 0], Index * 8));
    return Data;
}
