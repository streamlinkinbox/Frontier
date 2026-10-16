// LM-63 Type C photometry. The authored .ies files can be replaced by measured
// TILT=NONE fixtures without changing the renderer. Values are normalised to the
// peak candela; the light record supplies the intensity and colour separately.
export const PhotometrySize = Object.freeze({ Azimuth: 73, Polar: 91 }); // 5° × 2°
export const PhotometricProfiles = Object.freeze({
    Isotropic: 0, Roadway: 1, Area: 2, LowBeam: 3, HighBeam: 4,
    FogBeam: 5, RoofFlood: 6, RoofSpot: 7
});
const Files = ["Roadway", "Area", "LowBeam", "HighBeam", "FogBeam", "RoofFlood", "RoofSpot"];
const Numbers = /[-+]?(?:\d*\.\d+|\d+\.?\d*)(?:[eE][-+]?\d+)?/g;

export function ParseIes(Text)
{
    const Lines = Text.replace(/\r/g, "").split("\n");
    const Tilt = Lines.findIndex((Line) => /^TILT\s*=/.test(Line.trim().toUpperCase()));
    if (Tilt < 0 || Lines[Tilt].trim().toUpperCase() !== "TILT=NONE")
        throw new Error("IES requires LM-63 TILT=NONE (external tilt tables are not supported)");
    const Data = (Lines.slice(Tilt + 1).join(" ").match(Numbers) || []).map(Number);
    if (Data.length < 13)
        throw new Error("IES photometric header is incomplete");
    const [Lamps, , Multiplier, VerticalCount, HorizontalCount, Kind, Units] = Data;
    if (!Number.isInteger(Lamps) || Lamps < 1 || !Number.isFinite(Multiplier) || Multiplier <= 0
        || !Number.isInteger(VerticalCount) || VerticalCount < 2 || VerticalCount > 181
        || !Number.isInteger(HorizontalCount) || HorizontalCount < 1 || HorizontalCount > 361
        || Kind !== 1 || ![1, 2].includes(Units))
        throw new Error("IES needs a finite Type C photometric grid with valid lamp counts and units");
    const Needed = 13 + VerticalCount + HorizontalCount + VerticalCount * HorizontalCount;
    if (Data.length < Needed)
        throw new Error(`IES candela grid is incomplete (${Data.length} numbers, need ${Needed})`);
    const Vertical = Data.slice(13, 13 + VerticalCount);
    const Horizontal = Data.slice(13 + VerticalCount, 13 + VerticalCount + HorizontalCount);
    const Candela = Data.slice(13 + VerticalCount + HorizontalCount, Needed).map((Value) => Value * Multiplier);
    const ValidAngles = (Angles, End) => Angles[0] === 0 && Angles.at(-1) <= End
        && Angles.every((Angle, Index) => Number.isFinite(Angle) && (Index === 0 || Angle > Angles[Index - 1]));
    if (!ValidAngles(Vertical, 180) || !ValidAngles(Horizontal, 360)
        || ![0, 90, 180, 360].includes(Horizontal.at(-1))
        || Candela.some((Value) => !Number.isFinite(Value) || Value < 0))
        throw new Error("IES angles must increase from zero (C-plane 0/90/180/360); candela must be nonnegative");
    const Peak = Candela.reduce((Best, Value) => Math.max(Best, Value), 0);
    if (!(Peak > 0)) throw new Error("IES candela grid contains no light");
    return { Vertical, Horizontal, Candela, Peak };
}

function Interpolate(Angles, Value)
{
    const Clamped = Math.max(Angles[0], Math.min(Angles.at(-1), Value));
    let Upper = 1;
    while (Upper < Angles.length - 1 && Angles[Upper] < Clamped) Upper++;
    const Lower = Upper - 1;
    return [Lower, Upper, (Clamped - Angles[Lower]) / (Angles[Upper] - Angles[Lower])];
}

export function SampleIes(Profile, Azimuth, Polar)
{
    let Angle = ((Azimuth % 360) + 360) % 360;
    const Last = Profile.Horizontal.at(-1);
    // LM-63 Type C symmetry: a single plane is rotational, 90° mirrors both
    // axes, 180° mirrors about C180, and a 360° grid is used unmodified.
    if (Last === 0) Angle = 0;
    else if (Last === 90) Angle = Math.min(Angle % 180, 180 - Angle % 180);
    else if (Last === 180) Angle = Math.min(Angle, 360 - Angle);
    const [H0, H1, Ht] = Last === 0 ? [0, 0, 0] : Interpolate(Profile.Horizontal, Angle);
    const [V0, V1, Vt] = Interpolate(Profile.Vertical, Polar);
    const Count = Profile.Vertical.length;
    const A = Profile.Candela[H0 * Count + V0] * (1 - Vt) + Profile.Candela[H0 * Count + V1] * Vt;
    const B = Profile.Candela[H1 * Count + V0] * (1 - Vt) + Profile.Candela[H1 * Count + V1] * Vt;
    return (A * (1 - Ht) + B * Ht) / Profile.Peak;
}

export async function RetrievePhotometricAtlas(GetText = async (Name) =>
{
    const Response = await fetch(`Profiles/${Name}.ies`, { cache: "no-cache" });
    if (!Response.ok) throw new Error(`Missing IES profile ${Name}: ${Response.status}`);
    return Response.text();
})
{
    const { Azimuth, Polar } = PhotometrySize;
    const Stride = Azimuth * Polar;
    const Atlas = new Float32Array((Files.length + 1) * Stride);
    Atlas.fill(1, 0, Stride); // point sources do not use an angular profile
    await Promise.all(Files.map(async (Name, Index) =>
    {
        const Profile = ParseIes(await GetText(Name));
        for (let H = 0; H < Azimuth; H++)
            for (let V = 0; V < Polar; V++)
                Atlas[(Index + 1) * Stride + H * Polar + V] = SampleIes(Profile, 360 * H / (Azimuth - 1), 180 * V / (Polar - 1));
    }));
    return Atlas;
}
