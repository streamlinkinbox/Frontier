//============================================================================================================================================
//                                                         PHOTOMETRY.TEST.MJS
//============================================================================================================================================
// 🧩 IES (LM-63 Type C) photometry for the clustered lights: the parser, the packed atlas, the shapes of the shipped profiles, the light-type mapping, and (opt-in,
//    RUN_GPU=1) the shader's lookup executed on a software GPU against the CPU reference.

import test from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import crypto from "node:crypto";
import os from "node:os";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { ParseIes, SampleIes, ConstructPhotometricAtlas, SampleAtlas, PhotometryProfileFiles, PhotometryProfile, PhotometrySize, PhotometryBytes, PhotometryVectorCount, PhotometryTexelCount } from "../Source/Photometry.js";
import { LightingSystem, LightType } from "../Source/LightingSystem.js";

const Here = path.dirname(fileURLToPath(import.meta.url)), Root = path.resolve(Here, "..");
const Texts = Object.fromEntries(PhotometryProfileFiles.map((Name) => [Name, fs.readFileSync(path.join(Root, "Profiles", `${Name}.ies`), "utf8")]));
const Atlas = ConstructPhotometricAtlas(Texts);

const Provenance = JSON.parse(fs.readFileSync(path.join(Root, "Profiles", "PROVENANCE.json"), "utf8"));
const ProfilesReadme = fs.readFileSync(path.join(Root, "Profiles", "README.md"), "utf8");
const Measured = Object.entries(Provenance.profiles).filter(([, Entry]) => Entry.kind === "measured").map(([Name]) => Name);
const Authored = Object.entries(Provenance.profiles).filter(([, Entry]) => Entry.kind === "authored").map(([Name]) => Name);
const PermissiveLicences = ["MIT", "Apache-2.0", "BSD-2-Clause", "BSD-3-Clause", "CC-BY-4.0", "CC0-1.0", "ISC"];

test("every shipped profile is a valid LM-63 Type C table; the manifest says which are real and which are authored", () =>
{
    assert.deepEqual(Object.keys(Provenance.profiles), [...PhotometryProfileFiles], "the manifest lists the seven profiles in atlas order");
    assert.deepEqual([...Measured].sort(), ["Area", "RoofFlood", "RoofSpot", "Roadway"].sort(), "four real files");
    assert.deepEqual([...Authored].sort(), ["FogBeam", "HighBeam", "LowBeam"], "three authored automotive beams");
    for (const [Name, Text] of Object.entries(Texts))
    {
        const Profile = ParseIes(Text);
        assert.ok(Profile.Peak > 0 && Profile.Candela.every((Value) => Value >= 0), Name);
        assert.ok(Profile.Vertical.length >= 2 && Profile.Horizontal.length >= 1, Name);
        if (Authored.includes(Name))
        {
            assert.equal(Profile.Vertical.length, 91, Name); assert.equal(Profile.Horizontal.length, 37, Name);
            assert.match(Text, /authored demonstration profile - not measured photometry/, `${Name} says what it is`);
        }
        else
        {
            assert.doesNotMatch(Text, /authored|demonstration|synthetic|not measured/i, `${Name} is a real file and does not claim otherwise`);
            assert.match(Text, /^\s*\[MANUFAC\]\s*\S/m, `${Name} names its manufacturer`);
            assert.match(Text, /^\s*\[(?:TEST|TESTLAB|LUMINAIRE)\]\s*\S/m, `${Name} names its luminaire or test`);
        }
    }
});

test("provenance: each file is exactly the recorded bytes, and every real file names a permissively licensed source commit", () =>
{
    for (const [Name, Entry] of Object.entries(Provenance.profiles))
    {
        const Bytes = fs.readFileSync(path.join(Root, "Profiles", Entry.file));
        assert.equal(Bytes.length, Entry.bytes, `${Name}: size`);
        assert.equal(crypto.createHash("sha256").update(Bytes).digest("hex"), Entry.sha256, `${Name}: sha256 (a profile was edited or replaced without updating PROVENANCE.json)`);
        assert.ok(ProfilesReadme.includes(Entry.file), `Profiles/README.md mentions ${Entry.file}`);
        if (Entry.kind !== "measured") continue;
        assert.match(Entry.source.repository, /^https:\/\/github\.com\/[^/]+\/[^/]+$/, Name);
        assert.match(Entry.source.commit, /^[0-9a-f]{40}$/, `${Name}: a full commit hash, so the file can be fetched again`);
        assert.ok(Entry.source.path.length > 5, Name);
        assert.ok(PermissiveLicences.includes(Entry.source.repositoryLicence), `${Name}: ${Entry.source.repositoryLicence} is not on the permissive list`);
        assert.ok(ProfilesReadme.includes(Entry.source.commit.slice(0, 7)) && ProfilesReadme.includes(Entry.source.repositoryLicence), `README records the commit and licence of ${Name}`);
        assert.ok(Entry.header.MANUFAC && (Entry.header.LUMINAIRE || Entry.header.LUMCAT), `${Name}: the manifest records the file's own header`);
    }
    assert.match(Provenance.searched.automotive, /No measured vehicle headlamp/);
});

test("the authored automotive profiles regenerate byte for byte from Tools/AuthorLightingProfiles.py and the script never writes the real ones", { skip: spawnSync("python3", ["--version"]).status !== 0 }, () =>
{
    const Directory = fs.mkdtempSync(path.join(os.tmpdir(), "profiles-"));
    const Run = spawnSync("python3", [path.join(Here, "AuthorLightingProfiles.py"), Directory], { encoding: "utf8" });
    assert.equal(Run.status, 0, Run.stderr);
    assert.deepEqual(fs.readdirSync(Directory).map((File) => File.replace(/\.ies$/, "")).sort(), [...Authored].sort(), "only the authored files are generated");
    for (const Name of Authored) assert.ok(fs.readFileSync(path.join(Directory, `${Name}.ies`)).equals(fs.readFileSync(path.join(Root, "Profiles", `${Name}.ies`))), `${Name} regenerates identically`);
});

// A small IES text with the given angles and candela rows (one row per horizontal plane).
const Make = (Vertical, Horizontal, Rows, Extra = {}) => ["IESNA:LM-63-2002", "[TEST] unit test", "[MANUFAC] unit test", "TILT=NONE",
    `1 1000 ${Extra.Multiplier ?? 1} ${Vertical.length} ${Horizontal.length} 1 2 0.1 0.1 0.1`, "1 1 10", Vertical.join(" "), Horizontal.join(" "), ...Rows.map((Row) => Row.join(" "))].join("\n");

test("the parser reads the layouts real files use: 90-270 grids, grids that stop short of 360, downlights measured to 90", () =>
{
    // 90-270: symmetric about the 90-270 plane, so azimuth φ and 180° − φ are the same direction
    const Bilateral = ParseIes(Make([0, 45, 90], [90, 180, 270], [[10, 8, 4], [6, 5, 2], [2, 1, 0.5]]));
    for (const Azimuth of [0, 20, 45, 100, 170]) for (const Polar of [10, 40, 80])
        assert.ok(Math.abs(SampleIes(Bilateral, Azimuth, Polar) - SampleIes(Bilateral, 180 - Azimuth, Polar)) < 1e-12, `mirror about 90-270 at ${Azimuth}, ${Polar}`);
    assert.ok(SampleIes(Bilateral, 90, 0) > SampleIes(Bilateral, 270, 0) * 4, "the two sides of the 90-270 plane's data are not forced equal");
    assert.ok(Math.abs(SampleIes(Bilateral, 0, 0) - SampleIes(Bilateral, 180, 0)) < 1e-12 && Math.abs(SampleIes(Bilateral, 180, 0) - 6 / 10) < 1e-12, "C180 data is used for C0 as well");
    // periodic: planes at 0, 120, 240; between 240 and 360 the intensity runs from plane 240 back to plane 0
    const Periodic = ParseIes(Make([0, 90], [0, 120, 240], [[10, 5], [4, 2], [2, 1]]));
    assert.ok(Math.abs(SampleIes(Periodic, 120, 0) - 0.4) < 1e-12 && Math.abs(SampleIes(Periodic, 240, 0) - 0.2) < 1e-12);
    assert.ok(Math.abs(SampleIes(Periodic, 300, 0) - (0.2 + 0.5 * (1.0 - 0.2))) < 1e-12, "halfway between the last plane and 360 is halfway to plane 0");
    assert.ok(Math.abs(SampleIes(Periodic, 359.999, 0) - 1.0) < 1e-4 && Math.abs(SampleIes(Periodic, 0, 0) - 1.0) < 1e-12, "and it is continuous across 360");
    // a downlight measured to 90°: nothing above the horizontal
    const Down = ParseIes(Make([0, 30, 90], [0], [[10, 6, 1]]));
    assert.ok(SampleIes(Down, 0, 90) > 0 && SampleIes(Down, 77, 90.5) === 0 && SampleIes(Down, 0, 150) === 0, "zero beyond the last measured polar angle");
    assert.ok(Math.abs(SampleIes(Down, 33, 30) - 0.6) < 1e-12, "and rotational symmetry as before");
    // the old layouts keep their meaning
    const Quadrant = ParseIes(Make([0, 90], [0, 90], [[10, 5], [4, 2]]));
    assert.ok(Math.abs(SampleIes(Quadrant, 270, 0) - SampleIes(Quadrant, 90, 0)) < 1e-12 && Math.abs(SampleIes(Quadrant, 180, 0) - SampleIes(Quadrant, 0, 0)) < 1e-12, "0-90 mirrors both axes");
    const Half = ParseIes(Make([0, 90], [0, 180], [[10, 5], [4, 2]]));
    assert.ok(Math.abs(SampleIes(Half, 270, 0) - SampleIes(Half, 90, 0)) < 1e-12 && Math.abs(SampleIes(Half, 180, 0) - 0.4) < 1e-12, "0-180 mirrors about C0-C180");
    // layouts it cannot honour
    assert.throws(() => ParseIes(Make([0, 90], [45, 180, 270], [[1, 1], [1, 1], [1, 1]])), /increase from zero/, "a first plane of 45°");
    assert.throws(() => ParseIes(Make([0, 90], [0, 60, 120], [[1, 1], [1, 1], [1, 1]])), /increase from zero/, "a last plane of 120°");
    assert.throws(() => ParseIes(Make([0, 90], [90, 180, 300], [[1, 1], [1, 1], [1, 1]])), /increase from zero/, "90-300 is not a layout");
    assert.throws(() => ParseIes(Make([90, 180], [0], [[1, 1]])), /increase from zero/, "an uplight-only vertical range is not supported");
    // the multiplier scales candela but not the normalised shape
    assert.ok(Math.abs(ParseIes(Make([0, 90], [0], [[10, 5]], { Multiplier: 3.7 })).Peak - 37) < 1e-9);
});

test("the parser rejects what it cannot honour", () =>
{
    const Good = Texts.LowBeam;
    assert.throws(() => ParseIes(Good.replace("TILT=NONE", "TILT=INCLUDE")), /TILT=NONE/);
    assert.throws(() => ParseIes(Good.split("\n").slice(0, 8).join("\n")), /incomplete/);
    assert.throws(() => ParseIes(Good.replace("1 1000 1 91 37 1 2", "1 1000 1 91 37 2 2")), /Type C/);
    assert.throws(() => ParseIes(Good.replace("0 2 4 6", "0 6 4 2")), /increase from zero/);
    assert.throws(() => ParseIes(Good.replace("748.36765", "-3.0")), /nonnegative/);
});

test("the atlas: sizes agree with the shader's constants and the packing is a faithful 8-bit copy of the files", () =>
{
    assert.equal(PhotometryTexelCount, 7 * 37 * 91); assert.equal(PhotometryVectorCount, 1474); assert.equal(PhotometryBytes, 1474 * 16);
    const Shader = fs.readFileSync(path.join(Root, "Shader", "LightClusterSpace.wgsl"), "utf8");
    assert.match(Shader, /PhotometryAzimuthBins\s*:\s*u32\s*=\s*37u/); assert.match(Shader, /PhotometryPolarBins\s*:\s*u32\s*=\s*91u/);
    assert.match(Shader, /PhotometryTexelsPerProfile\s*:\s*u32\s*=\s*3367u/); assert.match(Shader, /Atlas\s*:\s*array<vec4u, 1474>/);
    PhotometryProfileFiles.forEach((Name, Index) =>
    {
        const Profile = ParseIes(Texts[Name]);
        // the atlas scales each profile so its strongest TEXEL is 255 (a file's own peak can fall between grid points)
        let Strongest = 0;
        for (let H = 0; H < 37; ++H) for (let V = 0; V < 91; ++V) Strongest = Math.max(Strongest, SampleIes(Profile, 360 * H / 36, 180 * V / 90));
        assert.ok(Strongest > 0.9 && Strongest <= 1.0 + 1e-12, `${Name}: the grid sees at least 90 % of the file's peak (${Strongest.toFixed(3)})`);
        let Worst = 0;
        for (let H = 0; H < 37; H += 3)
            for (let V = 0; V < 91; V += 2)
                Worst = Math.max(Worst, Math.abs(SampleIes(Profile, 360 * H / 36, 180 * V / 90) / Strongest - Atlas[(Index * 37 + H) * 91 + V] / 255));
        assert.ok(Worst <= 0.5 / 255 + 1e-9, `${Name}: quantisation error ${Worst}`);
        const Peak = Math.max(...Atlas.subarray(Index * 3367, (Index + 1) * 3367));
        assert.equal(Peak, 255, `${Name} is peak-normalised`);
    });
});

test("the profiles have the shapes their names claim (the real files measure what the slot needs)", () =>
{
    const At = (Name, Azimuth, Polar) => SampleAtlas(Atlas, PhotometryProfile[Name], Azimuth, Polar);
    // low beam: a sharp cutoff above the horizon (C0 is up, polar 6° above the axis) and a spread to the sides
    assert.ok(At("LowBeam", 0, 6) < 0.05 && At("LowBeam", 180, 6) > 0.3, "low beam: dark above its cutoff, lit below it");
    assert.ok(At("LowBeam", 270, 12) > 0.3 && At("LowBeam", 90, 12) > 0.3, "low beam spreads to both sides");
    // high beam: tight and symmetric
    assert.ok(At("HighBeam", 0, 0) > 0.9 && At("HighBeam", 90, 25) < 0.3 && Math.abs(At("HighBeam", 90, 10) - At("HighBeam", 270, 10)) < 0.05, "high beam: tight and symmetric");
    // fog: wider than low beam laterally
    assert.ok(At("FogBeam", 90, 40) > At("LowBeam", 90, 40) + 0.05 || At("FogBeam", 90, 40) > 0.1, "fog beam is wide");
    // flood wider than spot, and each is the beam its file measures: ERCO 60°, 50 % at 30° off axis; the LightLab-tested spot, 50 % at 8°
    assert.ok(At("RoofFlood", 90, 30) > At("RoofSpot", 90, 30) + 0.1, "the flood pods throw wider than the spots");
    assert.ok(Math.abs(At("RoofFlood", 0, 30) - 0.5) < 0.1 && At("RoofFlood", 0, 44) < 0.02, "flood: half power at 30°, nothing beyond 40°");
    assert.ok(At("RoofSpot", 0, 0) > 0.99 && At("RoofSpot", 0, 8) > 0.3 && At("RoofSpot", 0, 8) < 0.7 && At("RoofSpot", 0, 24) < 0.1, "spot: tight, half power near 8°");
    assert.ok(Math.abs(At("RoofSpot", 17, 6) - At("RoofSpot", 250, 6)) < 0.02, "both pods are rotationally symmetric");
    // roadway luminaire (FREGAT, full cut-off): the throw runs along the road (C0 / C180) out to ~70° and stops; across the road it is much shorter; nothing above the horizontal
    assert.ok(At("Roadway", 180, 66) > 0.55 && At("Roadway", 0, 66) > 0.45, "roadway: long throw along the road");
    assert.ok(At("Roadway", 90, 66) < 0.05 && At("Roadway", 270, 66) < 0.05, "roadway: short across the road");
    assert.ok(At("Roadway", 0, 100) < 1e-3 && At("Roadway", 180, 120) < 1e-3 && At("Roadway", 0, 85) < 0.05, "roadway: full cut-off, nothing at or above the horizontal");
    assert.ok(Math.abs(At("Roadway", 90, 30) - At("Roadway", 270, 30)) > 0.15, "roadway: the house side and the street side differ (an asymmetric file)");
    // area: wide, soft, rotationally symmetric, a little light above the horizontal
    assert.ok(At("Area", 0, 30) > 0.8 && Math.abs(At("Area", 0, 60) - 0.5) < 0.1 && Math.abs(At("Area", 77, 50) - At("Area", 211, 50)) < 0.02, "area: 50 % at 60°, rotationally symmetric");
    assert.ok(At("Area", 0, 120) > 0.05 && At("Area", 0, 120) < 0.3, "area: a little light above the horizontal");
    assert.equal(SampleAtlas(Atlas, 0, 12, 34), 1.0, "profile 0 is isotropic");
});

test("every light type the lighting system emits resolves to a photometric profile", () =>
{
    const Shader = fs.readFileSync(path.join(Root, "Shader", "LightClusterSpace.wgsl"), "utf8");
    const Mapping = Object.fromEntries([...Shader.matchAll(/if \(Type == (\d)u\) \{ return (?:select\()?(\d)u(?:, (\d)u, Light\.Params\.z > 0\.5\))?; \}/g)].map((Match) => [Number(Match[1]), Match[3] ? [Number(Match[2]), Number(Match[3])] : [Number(Match[2])]]));
    assert.deepEqual(Mapping, { 1: [1], 2: [3], 3: [4], 4: [5], 5: [6, 7] }, "the shader maps type → profile as documented");
    const Types = new Set();
    const Identity = new Float64Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0.5, 1]);
    // every vehicle mode, the roof pods in both fixture styles, and a street lamp: together they cover every type the system can emit
    for (const Mode of [0, 1, 2, 3])
        for (const Fixture of [0, 1])
        {
            const System = new LightingSystem();
            System.SetVehicleMode(Mode); System.SetRoofFixture(Fixture); System.RoofLightEnabled = true;
            const Data = System.Update(Identity, [{ X: 5, Y: 5, Z: 10.8 }]);
            for (let Index = 0; Index < 32; ++Index) if (Data[Index * 16 + 15] > 0.5) Types.add(Data[Index * 16 + 3] + ":" + Data[Index * 16 + 14]);
        }
    const Seen = new Set([...Types].map((Key) => Number(Key.split(":")[0])));
    assert.ok([1, 2, 3, 4, 5].every((Type) => Seen.has(Type)), `the system emits every light type 1 to 5 (saw ${[...Types]})`);
    assert.ok(Types.has("5:0") && Types.has("5:1"), "both flood variants occur (SlimLED flood, rally spot pods)");
    for (const Type of Seen) assert.ok(Type === 0 || Mapping[Type], `type ${Type} has a profile or is omnidirectional`);
    assert.ok(LightType.StreetLamp === 1 && LightType.Floodlight === 5);
});

test("executed: the shader's profile lookup matches the CPU reference for every light type on a software GPU", { skip: process.env.RUN_GPU !== "1", timeout: 900000 }, () =>
{
    const Run = spawnSync(process.execPath, [path.join(Here, "RunOnSoftwareGpu.mjs"), process.execPath, path.join(Here, "IesParityGpu.mjs")], { cwd: Here, encoding: "utf8", timeout: 800000 });
    assert.equal(Run.status, 0, (Run.stdout + Run.stderr).slice(-1500));
    const Result = JSON.parse(Run.stdout.split("\n").find((Line) => Line.startsWith("RESULT ")).slice(7));
    assert.ok(Result.Finite && Result.NonZero > 200, `lit directions found (${Result.NonZero}) and finite`);
    assert.ok(Result.Worst < 0.02 && Result.Mean < 0.002, `GPU vs CPU: worst ${Result.Worst.toFixed(4)} (${JSON.stringify(Result.WorstCase)}), mean ${Result.Mean.toFixed(5)}`);
});
