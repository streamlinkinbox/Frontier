//============================================================================================================================================
//                                                            PHOTOMETRY.JS
//============================================================================================================================================
// 🧩 IESNA LM-63 Type C photometry for the clustered lights: parse the files, reconstruct them at the atlas grid, and pack the atlas the shaders read.

// 📝 Provenance. The parser and the first Type C symmetry rules are the Frontier lighting rewrite's (streamlinkinbox/Frontier, branch arena/734ca2e5-frontier, commit f2aa20a6).
//    Four of the seven shipped profiles (Roadway, Area, RoofFlood, RoofSpot) are now REAL manufacturer / test-lab files, verbatim, with their source, commit and licence in
//    Profiles/PROVENANCE.json and Profiles/README.md. The three automotive beams (LowBeam, HighBeam, FogBeam) are still AUTHORED demonstration curves (their headers say so):
//    no measured vehicle headlamp file under a permissive licence turned up. Real files forced three extensions of the f4 parser, all LM-63 conventions: a 90°-270° grid
//    (symmetric about the 90-270 plane), an asymmetric grid that stops before 360° (periodic, the last plane blends back to the first), and a vertical range that ends at 90°
//    (a downlight: no light beyond its last measured angle).
//
// 📝 Convention. Type C: the polar angle is measured FROM the fixture's optical axis (0° on the axis, 180° straight behind), C0 is "up" around the axis and C90 is to its
//    right. Values are peak-normalised, and the light record's intensity and colour supply the scale, so a profile reshapes a light without relighting it.
//
// 📝 Packing. 7 profiles × 37 azimuths (10°) × 91 polar angles (2°) = 23 569 texels, one byte each (the strongest texel of each profile = 255), 23 584 bytes after padding to the vec4u
//    the uniform record needs: small enough to travel inside LightClusterRecords, with the lights and their cell masks, in one binding.

export const PhotometrySize = Object.freeze({ Azimuth: 37, Polar: 91 });                       // 10° × 2°
export const PhotometryProfileFiles = Object.freeze(["Roadway", "Area", "LowBeam", "HighBeam", "FogBeam", "RoofFlood", "RoofSpot"]);   // profile ids 1 to 7; 0 is isotropic
export const PhotometryProfile = Object.freeze({ Isotropic: 0, Roadway: 1, Area: 2, LowBeam: 3, HighBeam: 4, FogBeam: 5, RoofFlood: 6, RoofSpot: 7 });
export const PhotometryTexelsPerProfile = PhotometrySize.Azimuth * PhotometrySize.Polar;
export const PhotometryTexelCount = PhotometryProfileFiles.length * PhotometryTexelsPerProfile;
export const PhotometryVectorCount = Math.ceil(PhotometryTexelCount / 16);                       // vec4u in the uniform record
export const PhotometryBytes = PhotometryVectorCount * 16;

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
    // 📝 The C-plane layouts LM-63 defines (and files in the wild use): one plane = rotationally symmetric; 0-90 = both axes mirrored; 0-180 = mirrored about C0-C180;
    //    90-270 = mirrored about the 90-270 plane; 0-360 (or 0 up to a last plane short of 360, 357.5 and 358.8 are common) = asymmetric, periodic.
    const Layout = Horizontal.length === 1 ? Horizontal[0] === 0
        : Horizontal[0] === 90 ? Horizontal.at(-1) === 270 && Horizontal.every((Angle, Index) => Number.isFinite(Angle) && (Index === 0 || Angle > Horizontal[Index - 1]))
        : ValidAngles(Horizontal, 360) && (Horizontal.at(-1) === 90 || Horizontal.at(-1) === 180 || Horizontal.at(-1) > 180);
    if (!ValidAngles(Vertical, 180) || !Layout || Candela.some((Value) => !Number.isFinite(Value) || Value < 0))
        throw new Error("IES angles must increase from zero (C-plane 0/90/180/360, or 90 to 270); candela must be nonnegative");
    const Peak = Candela.reduce((Best, Value) => Math.max(Best, Value), 0);
    if (!(Peak > 0))
        throw new Error("IES candela grid contains no light");
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

// 📝 The profile's relative intensity (peak = 1) towards (azimuth°, polar°). LM-63 Type C symmetry: a single plane is rotational, 0-90 mirrors both axes, 0-180 mirrors
//    about C180, 90-270 mirrors about the 90-270 plane (φ → 180° − φ), and a grid that reaches 360° is used as it stands. A grid whose last plane is short of 360° is
//    periodic: between that plane and 360° the intensity blends from the last plane back to the first. Beyond the last measured polar angle there is no light (a downlight
//    measured to 90° emits nothing above the horizontal).
export function SampleIes(Profile, Azimuth, Polar)
{
    let Angle = ((Azimuth % 360) + 360) % 360;
    const Count = Profile.Horizontal.length, First = Profile.Horizontal[0], Last = Profile.Horizontal.at(-1);
    if (Polar > Profile.Vertical.at(-1) + 1.0e-9) return 0.0;
    let H0 = 0, H1 = 0, Ht = 0;
    if (Count === 1) { /* rotationally symmetric */ }
    else
    {
        if (First === 90)
        {
            if (Angle < 90 || Angle > 270) Angle = (((180 - Angle) % 360) + 360) % 360;
        }
        else if (Last === 90) Angle = Math.min(Angle % 180, 180 - Angle % 180);
        else if (Last === 180) Angle = Math.min(Angle, 360 - Angle);
        if (First === 0 && Last < 360 && Last > 180 && Angle >= Last) { H0 = Count - 1; H1 = 0; Ht = (Angle - Last) / (360 - Last); }
        else [H0, H1, Ht] = Interpolate(Profile.Horizontal, Angle);
    }
    const [V0, V1, Vt] = Interpolate(Profile.Vertical, Polar);
    const Rows = Profile.Vertical.length;
    const A = Profile.Candela[H0 * Rows + V0] * (1 - Vt) + Profile.Candela[H0 * Rows + V1] * Vt;
    const B = Profile.Candela[H1 * Rows + V0] * (1 - Vt) + Profile.Candela[H1 * Rows + V1] * Vt;
    return (A * (1 - Ht) + B * Ht) / Profile.Peak;
}

// 📝 The packed atlas from the profile texts, in PhotometryProfileFiles order: texel (profile p, azimuth a, polar v) lives at ((p-1)·37 + a)·91 + v, a byte, little-endian
//    inside the u32 the shader loads. Texts: an object { Name: "LM-63 text" }.
//    Each profile is scaled so that the strongest TEXEL is 255. The file's own peak can fall between grid points (the FREGAT roadway luminaire peaks at 69° and 166.8°, off the
//    2° × 10° grid, so its strongest texel is 96 % of that peak): the shader reads texel values as "relative to the profile's peak", so the grid's maximum is the peak it can see.
export function ConstructPhotometricAtlas(Texts)
{
    const Atlas = new Uint8Array(PhotometryBytes);
    PhotometryProfileFiles.forEach((Name, Index) =>
    {
        const Profile = ParseIes(Texts[Name]);
        const Values = new Float64Array(PhotometryTexelsPerProfile);
        let Strongest = 0;
        for (let H = 0; H < PhotometrySize.Azimuth; ++H)
            for (let V = 0; V < PhotometrySize.Polar; ++V)
            {
                const Value = SampleIes(Profile, 360 * H / (PhotometrySize.Azimuth - 1), 180 * V / (PhotometrySize.Polar - 1));
                Values[H * PhotometrySize.Polar + V] = Value;
                if (Value > Strongest) Strongest = Value;
            }
        for (let Texel = 0; Texel < Values.length; ++Texel)
            Atlas[Index * PhotometryTexelsPerProfile + Texel] = Math.max(0, Math.min(255, Math.round(255 * Values[Texel] / Strongest)));
    });
    return Atlas;
}

// 📝 What a decoded atlas texel means, for tests: the bilinear lookup the shader performs, on the CPU.
export function SampleAtlas(Atlas, ProfileId, Azimuth, Polar)
{
    if (ProfileId === 0) return 1.0;
    const Index = ProfileId - 1;
    const A = Math.max(0, Math.min(PhotometrySize.Azimuth - 1.000001, (((Azimuth % 360) + 360) % 360) / 360 * (PhotometrySize.Azimuth - 1)));
    const P = Math.max(0, Math.min(PhotometrySize.Polar - 1.000001, Polar / 180 * (PhotometrySize.Polar - 1)));
    const H0 = Math.floor(A), V0 = Math.floor(P), Ht = A - H0, Vt = P - V0;
    const Texel = (H, V) => Atlas[(Index * PhotometrySize.Azimuth + H) * PhotometrySize.Polar + V] / 255;
    const Low = Texel(H0, V0) * (1 - Vt) + Texel(H0, V0 + 1) * Vt, High = Texel(H0 + 1, V0) * (1 - Vt) + Texel(H0 + 1, V0 + 1) * Vt;
    return Low * (1 - Ht) + High * Ht;
}

export async function RetrievePhotometricAtlas(GetText = async (Name) =>
{
    const Response = await fetch(`Profiles/${Name}.ies`, { cache: "no-cache" });
    if (!Response.ok) throw new Error(`Missing IES profile ${Name}: ${Response.status}`);
    return Response.text();
})
{
    const Texts = {};
    await Promise.all(PhotometryProfileFiles.map(async (Name) => { Texts[Name] = await GetText(Name); }));
    return ConstructPhotometricAtlas(Texts);
}
