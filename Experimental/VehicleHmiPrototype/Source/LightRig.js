// Authored local-light catalogue and automotive switchgear. This is the sole
// producer of point/spot/IES records; no light is inferred from a glowing mesh.
// Fixture emission and direct intensity change together, while the sun/sky stay
// in the atmosphere pass. +X is vehicle forward, +Y left, +Z up.
import { ResolveLevelPlacements } from "./LevelStructure.js";
import { CourseMaterial, EncodeMaterialRecords } from "./MaterialSpecification.js";
import { PhotometricProfiles as P } from "./Photometry.js";
import { TransformPoint, TransformDirection, NormalizeVector } from "./TransformProjection.js";
import { ResolveCoMAuthoredZ } from "./VehicleSpecification.js";
import { ResolveSparkRadiance, ResolveSparkGain } from "./SparkSequence.js";

export const LightModes = Object.freeze(["Off", "Low", "High", "Fog"]);
export function CycleLightMode(Current) { return LightModes[(LightModes.indexOf(Current) + 1) % LightModes.length]; }

const Luminance = ([R, G, B]) => R * 0.2126 + G * 0.7152 + B * 0.0722;
const Clamp = (Value, Low, High) => Number.isFinite(Value) ? Math.max(Low, Math.min(High, Value)) : Low;
const Radians = Math.PI / 180;
const Rebase = ResolveCoMAuthoredZ(); // the archive, fixtures and optical axes share the body frame

function Point(Position, Reach, Colour, Kind = "Effect")
{
    return { Position, Reach, Colour, Direction: [0, 0, -1], Up: [1, 0, 0],
             Inner: -1, Outer: -1, Profile: P.Isotropic, Kind, Dynamic: Kind !== "Scene" };
}

function Spot(Position, Direction, Up, Reach, Colour, Profile, OuterDegrees, Kind = "Scene")
{
    return { Position, Direction: NormalizeVector(Direction), Up: NormalizeVector(Up), Reach, Colour,
             Inner: Math.cos(Math.max(0, OuterDegrees - 8) * Radians), Outer: Math.cos(OuterDegrees * Radians),
             Profile, Kind, Dynamic: Kind !== "Scene" };
}

function RotateYaw([X, Y, Z], Yaw)
{
    return [X * Math.cos(Yaw) - Y * Math.sin(Yaw), X * Math.sin(Yaw) + Y * Math.cos(Yaw), Z];
}

export function ConstructSceneLights()
{
    const Lights = [];
    for (const Fixture of ResolveLevelPlacements())
    {
        const [X, Y, Z] = Fixture.Position;
        const Down = [0, 0, -1];
        if (Fixture.Shape === "PoleHead")
        {
            const Toward = RotateYaw([0.17, 0, -0.985], Fixture.Yaw);
            Lights.push(Spot([X, Y, Z - 0.09], Toward, RotateYaw([1, 0, 0.17], Fixture.Yaw),
                             42, [1850, 1420, 890], P.Roadway, 78));
        }
        else if (Fixture.Shape === "LampStrip")
        {
            for (const Along of [-9.6, -4.8, 0, 4.8, 9.6])
                Lights.push(Spot([X + Along, Y, Z - 0.13], Down, [1, 0, 0], 17, [235, 255, 290], P.Area, 76));
        }
        else if (Fixture.Shape === "TunnelLamp")
        {
            const Toward = Y > -48 ? -1 : 1;
            Lights.push(Spot([X, Y + 0.10 * Toward, Z - 0.06], [0, 0.34 * Toward, -0.94], [1, 0, 0],
                             13, [95, 114, 138], P.Area, 75));
        }
        else if (Fixture.Shape === "GantryLamp")
        {
            for (const Along of [-8, -4, 0, 4, 8])
                Lights.push(Spot([X + Along, Y, Z - 0.08], Down, [1, 0, 0], 14,
                                 Fixture.Material === CourseMaterial.GateLampRed ? [150, 32, 20] : [48, 160, 70], P.Area, 74));
        }
    }
    return Lights;
}

function HexLinear(Hex)
{
    if (!/^#[\da-f]{6}$/i.test(Hex || "")) return [0.04, 0.65, 1.0];
    return [1, 3, 5].map((At) => {
        const C = parseInt(Hex.slice(At, At + 2), 16) / 255;
        return C <= 0.04045 ? C / 12.92 : ((C + 0.055) / 1.055) ** 2.4;
    });
}

// Many tiny spark particles become a handful of spatially pooled light sources;
// derez cells get a separate bounded pool. Unlike the old fixed eight frame
// slots, these share the same clustered lighting/GI path as every other lamp.
export function ConstructEffectLights(Sparks, Effects)
{
    const Bins = new Map();
    if (Sparks?.Settings?.Enabled)
        for (const Spark of Sparks.Particles || [])
        {
            const Gain = Clamp(Sparks.Settings.Brightness, 0, 10) * Clamp(Spark.Size, 0, 5) ** 2
                       * Clamp(ResolveSparkGain(Spark), 0, 10) * 0.012;
            const RGB = ResolveSparkRadiance(Spark.Kelvin).map((C) => C * Gain);
            const Weight = Luminance(RGB);
            if (!(Weight > 0.001)) continue;
            const Key = Spark.Position.map((V) => Math.floor(V / 1.8)).join(":");
            const Bin = Bins.get(Key) || { Sum: [0, 0, 0], RGB: [0, 0, 0], Weight: 0 };
            Bin.Weight += Weight;
            for (let I = 0; I < 3; I++) { Bin.Sum[I] += Spark.Position[I] * Weight; Bin.RGB[I] += RGB[I]; }
            Bins.set(Key, Bin);
        }
    const Candidates = [...Bins.values()].sort((A, B) => B.Weight - A.Weight).slice(0, 5).map((Bin) =>
        Point(Bin.Sum.map((V) => V / Bin.Weight), 4.5, Bin.RGB, "Spark"));
    if (Effects?.Settings)
    {
        const S = Effects.Settings;
        for (const Emitter of Effects.Emitters || [])
        {
            if (!Emitter || Emitter.Kind === 4 || Effects.Clock < Emitter.Start || Effects.Clock >= Emitter.End) continue;
            const Age = Effects.Clock - Emitter.Start, Remaining = Emitter.End - Effects.Clock;
            const Envelope = Clamp(Age / 0.12, 0, 1) * Clamp(Remaining / 0.55, 0, 1);
            const Strength = Clamp(S.Intensity, 0, 8) * Clamp(S.Faces ? S.GlowStrength : 6, 0, 12) * Envelope;
            if (!(Strength > 0.001)) continue;
            const M = Emitter.Transform, Centre = Emitter.Centre;
            const Drag = Math.max(0.001, S.Drag || 0.4);
            const Flight = Emitter.Kind % 2 === 0 ? (1 - Math.exp(-Drag * Age)) / Drag : 0;
            const Position = [0, 1, 2].map((I) => M[12 + I] + M[I] * Centre[0] + M[4 + I] * Centre[1]
                + M[8 + I] * Centre[2] + (Emitter.Velocity?.[I] || 0) * Flight);
            Position[2] += 0.5;
            const Colour = S.Faces ? (Effects.LightAlbedo || [0.3, 0.5, 0.35]) : HexLinear(S.Glow);
            Candidates.push(Point(Position, 9, Colour.map((C) => C * Strength), "Derez"));
        }
    }
    Candidates.sort((A, B) => Luminance(B.Colour) - Luminance(A.Colour));
    const Selected = Candidates.slice(0, 8);
    const Total = Selected.reduce((Sum, Light) => Sum + Luminance(Light.Colour), 0);
    const Scale = Math.min(1, 100 / Math.max(Total, 0.001));
    return Selected.map((Light) => ({ ...Light, Colour: Light.Colour.map((C) => C * Scale) }));
}

export class LightRig
{
    constructor(Materials, MaterialRecords, Exchange, Settings)
    {
        this.Materials = Materials;
        this.MaterialRecords = MaterialRecords;
        this.Exchange = Exchange;
        this.Settings = Settings;
        this.SceneLights = ConstructSceneLights();
        this.Lights = [];
        this.DynamicLights = [];
        this.LastKey = "";
    }

    SceneActive()
    {
        const S = this.Settings;
        return S.SceneLightsEnabled && (!S.AutoSceneLights || S.SunElevation <= 5);
    }

    // Changing a fixture's emission recaptures cards (handled by the host), so
    // its emissive mesh, first GI bounce and direct source never disagree.
    SynchroniseMaterials()
    {
        const S = this.Settings, Mode = S.LampMode;
        const Scene = this.SceneActive() ? Clamp(S.SceneLightPower, 0, 3) : 0;
        const Scale = (Colour, Power) => Colour.map((C) => C * Power);
        const Low = Mode === "Low", High = Mode === "High", Fog = Mode === "Fog";
        const Front = High ? Scale([37, 43, 49], S.HighBeamPower)
                    : Low ? Scale([19, 22, 25], S.LowBeamPower)
                    : Fog ? [2.0, 2.3, 2.7] : [0, 0, 0];
        const Roof = High && S.RoofEnabled ? Clamp(S.RoofBeamPower, 0, 3) : 0;
        const Values = [
            [CourseMaterial.Headlight, Front],
            [CourseMaterial.Taillight, Mode === "Off" ? [0, 0, 0] : [3.6, 0.08, 0.10]],
            [CourseMaterial.FogLens, Fog ? Scale([13, 12, 9.5], S.FogBeamPower) : [0, 0, 0]],
            [CourseMaterial.RoofLED, Scale([27, 32, 38], Roof)],
            [CourseMaterial.RoofLens, Scale([0.6, 0.8, 1.0], Roof)],
            [CourseMaterial.LampWarm, Scale([14, 10.2, 6], Scene)],
            [CourseMaterial.LampDiffuser, Scale([2.5, 1.9, 1.15], Scene)],
            [CourseMaterial.LampCool, Scale([7, 8.4, 10], Scene)],
            [CourseMaterial.GateLamp, Scale([1.2, 9, 2.4], Scene)],
            [CourseMaterial.GateLampRed, Scale([9, 1, 0.6], Scene)]
        ];
        let Changed = false;
        for (const [Family, Emission] of Values)
        {
            const Material = this.Materials[Family];
            if (Emission.some((Value, I) => Math.abs(Material.Emission[I] - Value) > 1e-5))
            {
                Material.Emission = Emission;
                this.Exchange.Device.queue.writeBuffer(this.MaterialRecords, Family * 48, EncodeMaterialRecords([Material]));
                Changed = true;
            }
        }
        return Changed;
    }

    // LightingKey excludes transforms and sparks: those move every frame and
    // use nearby card priority, not a full radiosity-history reset.
    QueryLightingKey()
    {
        const S = this.Settings;
        return [S.LampMode, this.SceneActive(), S.SceneLightPower, S.LowBeamPower, S.HighBeamPower,
                S.FogBeamPower, S.RoofEnabled, S.RoofBeamPower, S.RoofProfile, S.BeamAim,
                S.RoofAim, S.EmissionScale].join("|");
    }

    Resolve(VehicleTransform, Sparks, Effects, Smoke, VehicleHidden = false)
    {
        const S = this.Settings, Mode = S.LampMode;
        const LightPower = this.SceneActive() ? Clamp(S.SceneLightPower, 0, 3) : 0;
        const Static = LightPower > 0 ? this.SceneLights.map((Light) =>
            ({ ...Light, Colour: Light.Colour.map((C) => C * LightPower) })) : [];
        const Vehicle = [];
        if (!VehicleHidden && Mode !== "Off")
        {
            const BodyPoint = (Local) => TransformPoint(VehicleTransform, Local);
            const BodyAxis = (Local) => NormalizeVector(TransformDirection(VehicleTransform, Local));
            const Aim = Clamp(S.BeamAim, -10, 4) * Radians;
            const Forward = BodyAxis([Math.cos(Aim), 0, Math.sin(Aim)]);
            const Up = BodyAxis([-Math.sin(Aim), 0, Math.cos(Aim)]);
            for (const Side of [-1, 1])
            {
                const Head = BodyPoint([3.20, Side * 0.94, 0.49 - Rebase]);
                if (Mode === "Low") Vehicle.push(Spot(Head, Forward, Up, 72,
                    [980, 1070, 1210].map((C) => C * S.LowBeamPower), P.LowBeam, 45, "Vehicle"));
                if (Mode === "High") Vehicle.push(Spot(Head, Forward, Up, 126,
                    [2480, 2620, 2850].map((C) => C * S.HighBeamPower), P.HighBeam, 35, "Vehicle"));
                if (Mode === "Fog") Vehicle.push(Spot(BodyPoint([3.23, Side * 0.89, 0.27 - Rebase]),
                    BodyAxis([0.9994, 0, -0.035]), Up, 40,
                    [500, 440, 330].map((C) => C * S.FogBeamPower), P.FogBeam, 66, "Vehicle"));
            }
            if (Mode === "High" && S.RoofEnabled)
            {
                const RoofAngle = Clamp(S.RoofAim, -16, 8) * Radians;
                const Axis = BodyAxis([Math.cos(RoofAngle), 0, Math.sin(RoofAngle)]);
                const RoofUp = BodyAxis([-Math.sin(RoofAngle), 0, Math.cos(RoofAngle)]);
                for (const Y of [-0.42, 0, 0.42])
                    Vehicle.push(Spot(BodyPoint([0.66, Y, 1.56 - Rebase]), Axis, RoofUp, 90,
                        [1000, 1150, 1310].map((C) => C * S.RoofBeamPower),
                        S.RoofProfile === "Spot" ? P.RoofSpot : P.RoofFlood,
                        S.RoofProfile === "Spot" ? 35 : 63, "Roof"));
            }
        }
        const Fire = S.SmokeEnabled && Smoke?.Sources?.Lights
            ? Smoke.Sources.Lights.slice(0, 4).map((Light) => Point(Light.Position, 14, Light.Radiance, "Fire")) : [];
        const EffectsLights = ConstructEffectLights(Sparks, Effects);
        this.DynamicLights = [...Vehicle, ...Fire, ...EffectsLights].filter((Light) => Luminance(Light.Colour) > 0.001);
        // Vehicle light wins in a pathological overfull cluster, then the
        // nearby street fixtures, then one-frame effects. Stable across frames.
        this.Lights = [...Vehicle, ...Static, ...Fire, ...EffectsLights].filter((Light) => Luminance(Light.Colour) > 0.001);
        return this.Lights;
    }
}
