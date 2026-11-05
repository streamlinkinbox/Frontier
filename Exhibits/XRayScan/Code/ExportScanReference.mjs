//============================================================================================================================================
//                                                       EXPORTSCANREFERENCE.MJS
//============================================================================================================================================
// 🧩 Runs the HTML prototype's own X-ray scan modules (Experimental/VehicleHmiPrototype/Source: ScanSequence.js, VehicleEdges.js and the scan
//    parts of GarageView.js) and writes what they produce as the TOML the C++ port is checked against (XRayScanValidation).
//
//        node Exhibits/XRayScan/Code/ExportScanReference.mjs [output.toml]       (default Exhibits/XRayScan/Results/ScanReference.toml)
//
//    Everything in the file is a number the JavaScript computed: the C++ never reads a value that the exporter chose except as an input.

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const Here    = path.dirname(fileURLToPath(import.meta.url));
const Root    = path.resolve(Here, "../../..");
const Source  = path.join(Root, "Experimental/VehicleHmiPrototype");
const Output  = process.argv[2] ?? path.join(Root, "Exhibits/XRayScan/Results/ScanReference.toml");

// 📝 The prototype reads its archives with fetch and its page address; both are answered from the checkout.
globalThis.location = { search: "?lobby=1", href: "http://localhost/?lobby=1" };
globalThis.fetch = async (Location) =>
{
    const Bytes = fs.readFileSync(path.join(Source, String(Location).split("?")[0]));
    return { ok: true, status: 200, arrayBuffer: async () => Bytes.buffer.slice(Bytes.byteOffset, Bytes.byteOffset + Bytes.byteLength), text: async () => Bytes.toString("utf8") };
};

const { ScanSequence, ScanSpecification, ConvertLinearToSrgb, ResolveAcesFilmic, ResolveDisplayColour, EaseInOutCubic, DampValue } = await import(`${Source}/Source/ScanSequence.js`);
const { ConvertHexToLinear } = await import(`${Source}/Source/PaintRipple.js`);
const { ExtractFeatureEdges, ConstructScanEdges } = await import(`${Source}/Source/VehicleEdges.js`);
const { ConstructSparkleCloud, EncodeOccupantMask, ResolveScanPlane, GarageView } = await import(`${Source}/Source/GarageView.js`);
const { CourseMaterial } = await import(`${Source}/Source/MaterialSpecification.js`);
const { SceneStructure } = await import(`${Source}/Source/FacetStructure.js`);

//------------------------------------------------------------------------------------------------------------------------
//                                                        TOML WRITER
//------------------------------------------------------------------------------------------------------------------------

// 📝 A float the way TOML spells it: the shortest text that reads back as the same double, always with a point or an exponent.
function Float(Value)
{
    if (!Number.isFinite(Value))
    {
        throw new Error("a non-finite number in the reference");
    }
    if (Object.is(Value, -0))
    {
        return "-0.0";
    }
    const Text = String(Value);
    return /[.eE]/.test(Text) ? Text : `${Text}.0`;
}
const List  = (Values) => `[${Values.map(Float).join(", ")}]`;
const Ints  = (Values) => `[${Values.join(", ")}]`;
const Flags = (Values) => `[${Values.map((Value) => (Value ? "true" : "false")).join(", ")}]`;
const Lines = [];
const Emit  = (Text = "") => Lines.push(Text);

//------------------------------------------------------------------------------------------------------------------------
//                                                        SPECIFICATION
//------------------------------------------------------------------------------------------------------------------------

Emit("# 🧩 Written by Exhibits/XRayScan/Code/ExportScanReference.mjs from Experimental/VehicleHmiPrototype. Do not edit by hand.");
Emit();
Emit("[Specification]");
for (const Name of ["Cycle", "Sweep", "Hold", "FrontStart", "FrontEnd", "FrontIdle", "RampRate", "RampSettle", "StepLimit", "SolidFloor", "EdgeOpacity", "EdgeMargin",
                    "CurtainIdle", "CurtainHeight", "ParticleCentre", "ParticleSize", "ParticleSpeed", "ParticleOpacity"])
{
    Emit(`${Name} = ${Float(ScanSpecification[Name])}`);
}
Emit(`CurtainOpacity = ${List(ScanSpecification.CurtainOpacity)}`);
Emit(`ParticleBox = ${List(ScanSpecification.ParticleBox)}`);
Emit(`ParticleCount = ${ScanSpecification.ParticleCount}`);
for (const Name of ["GhostColour", "Accent", "EdgeColour", "ParticleColour"])
{
    Emit(`${Name} = "${ScanSpecification[Name]}"`);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         COLOUR TABLES
//------------------------------------------------------------------------------------------------------------------------

Emit();
Emit("# The four colours of the scan: hex → linear → ACES filmic → sRGB, as the profile encodes them.");
for (const Hex of [ScanSpecification.GhostColour, ScanSpecification.Accent, ScanSpecification.EdgeColour, ScanSpecification.ParticleColour, "#000000", "#ffffff", "#1a2b3c", "#fe0102"])
{
    Emit("[[Colour]]");
    Emit(`Hex = "${Hex}"`);
    Emit(`Linear = ${List(ConvertHexToLinear(Hex))}`);
    Emit(`Display = ${List(ResolveDisplayColour(Hex))}`);
}
Emit();
Emit("# Scalar conversions at fixed arguments.");
const Probe = [0.0, 0.0001, 0.0031308, 0.004, 0.05, 0.2, 0.5, 0.735, 1.0, 1.7, 4.0];
Emit("[Conversions]");
Emit(`Argument = ${List(Probe)}`);
Emit(`Srgb = ${List(Probe.map(ConvertLinearToSrgb))}`);
Emit(`Ease = ${List(Probe.map((Value) => EaseInOutCubic(Math.min(Value, 1.0))))}`);
Emit(`Damp = ${List(Probe.map((Value) => DampValue(Value, 1.0, 3.0, 0.05)))}`);
Emit(`AcesInput = ${List([0.02, 0.2, 0.9, 0.0, 1.5, 0.3, 0.6, 0.01, 0.4, 2.0, 0.05, 0.8])}`);
const AcesInput = [[0.02, 0.2, 0.9], [0.0, 1.5, 0.3], [0.6, 0.01, 0.4], [2.0, 0.05, 0.8]];
Emit(`AcesOutput = ${List(AcesInput.flatMap((Linear) => ResolveAcesFilmic(Linear)))}`);

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE CLOCK, SAMPLED
//------------------------------------------------------------------------------------------------------------------------

// 📝 The script, as inputs the C++ replays: on at 0 s, 1/60 s steps; three long steps (past the 0.05 s limit) at 3 s; off at 9 s; on again at 12.5 s.
//    Assign: 1 on, 0 off, -1 leaves the toggle as it was. Capture names a stage and a HUD clock for a profile taken after the segment.
const Sixtieth = 1.0 / 60.0;
const Rotated = (() =>
{
    const Angle = 0.6;
    return { Origin: [12.5, -3.25, 0.37], Forward: [Math.cos(Angle), Math.sin(Angle), 0.0], Right: [Math.sin(Angle), -Math.cos(Angle), 0.0], Up: [0.0, 0.0, 1.0] };
})();
const Identity = { Origin: [0.0, 0.0, 0.0], Forward: [1.0, 0.0, 0.0], Right: [0.0, -1.0, 0.0], Up: [0.0, 0.0, 1.0] };
const Stages = { Identity, Rotated };
const Script = [
    { Assign: 1, Steps: 180, Raw: Sixtieth },
    { Assign: -1, Steps: 3, Raw: 0.2, Capture: ["Identity", 0.0] },
    { Assign: -1, Steps: 177, Raw: Sixtieth, Capture: ["Rotated", 0.37] },
    { Assign: -1, Steps: 120, Raw: Sixtieth, Capture: ["Rotated", 1.25] },
    { Assign: 0, Steps: 90, Raw: Sixtieth, Capture: ["Identity", 2.0] },
    { Assign: -1, Steps: 90, Raw: Sixtieth },
    { Assign: 1, Steps: 60, Raw: Sixtieth, Capture: ["Rotated", 3.0] },
    { Assign: -1, Steps: 400, Raw: Sixtieth, Capture: ["Rotated", 4.5] },
    { Assign: -1, Steps: 120, Raw: Sixtieth, Capture: ["Identity", 5.0] }
];
const Scan = new ScanSequence();
const Sparkles = ConstructSparkleCloud();
const Sequence = { Index: [], Time: [], Ramp: [], SweepSeconds: [], Front: [], Amount: [], CycleAmount: [], Sweeping: [], Transparent: [], Clock: [], Curtain0: [], Curtain1: [], EdgeOpacity: [], Visible: [] };
const Profiles = [];
let Elapsed = 0.0;
let StepIndex = 0;
for (const Segment of Script)
{
    if (Segment.Assign >= 0)
    {
        Scan.Assign(Segment.Assign === 1);
    }
    for (let Index = 0; Index < Segment.Steps; ++Index, ++StepIndex)
    {
        Scan.Advance(Segment.Raw);
        Elapsed += Segment.Raw;
        // 📝 Every eighth step and the first and last of a segment are kept: the C++ steps through all of them and compares at these.
        if (StepIndex % 8 === 0 || Index === 0 || Index === Segment.Steps - 1)
        {
            const Curtain = Scan.ResolveCurtain();
            Sequence.Index.push(StepIndex);
            Sequence.Time.push(Elapsed);
            Sequence.Ramp.push(Scan.Ramp);
            Sequence.SweepSeconds.push(Scan.SweepSeconds);
            Sequence.Front.push(Scan.Front);
            Sequence.Amount.push(Scan.Amount);
            Sequence.CycleAmount.push(Scan.CycleAmount);
            Sequence.Sweeping.push(Scan.Sweeping);
            Sequence.Transparent.push(Scan.Transparent);
            Sequence.Clock.push(Scan.Clock);
            Sequence.Curtain0.push(Curtain[0]);
            Sequence.Curtain1.push(Curtain[1]);
            Sequence.EdgeOpacity.push(Scan.ResolveEdgeOpacity());
            Sequence.Visible.push(Scan.Visible);
        }
    }
    if (Segment.Capture)
    {
        // 📝 EncodeScanRecord is a method of the Garage view: it runs here on the three members it reads.
        const [StageName, HudClock] = Segment.Capture;
        const Stage = Stages[StageName];
        const Host = { Scan, HudClock, SparkleCount: Sparkles.length / 4, Exchange: { Device: { queue: { writeBuffer() {} } } }, ScanRecords: null };
        GarageView.prototype.EncodeScanRecord.call(Host, Stage, null);
        Profiles.push({ StageName, Step: StepIndex - 1, Values: Array.from(Host.ScanRecordValues), Plane: ResolveScanPlane(Stage, Scan.Front), Amount: Scan.Amount });
    }
}
Emit();
Emit("# The clock's script (inputs) and the state after sampled steps (what the JavaScript computed).");
for (const Segment of Script)
{
    Emit("[[Segment]]");
    Emit(`Assign = ${Segment.Assign}`);
    Emit(`Steps = ${Segment.Steps}`);
    Emit(`Raw = ${Float(Segment.Raw)}`);
}
Emit("[Sequence]");
Emit(`Index = ${Ints(Sequence.Index)}`);
for (const Name of ["Time", "Ramp", "SweepSeconds", "Front", "Amount", "CycleAmount", "Clock", "Curtain0", "Curtain1", "EdgeOpacity"])
{
    Emit(`${Name} = ${List(Sequence[Name])}`);
}
for (const Name of ["Sweeping", "Transparent", "Visible"])
{
    Emit(`${Name} = ${Flags(Sequence[Name])}`);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  PROFILES AND VIEW RECORDS
//------------------------------------------------------------------------------------------------------------------------

Emit();
Emit("# EncodeScanRecord (GarageView.js) after the segments that name a capture: the 48 floats of the layer profile, as the shaders read them, and the");
Emit("# front's plane and fade (the scan fields of FrameRecord) for the same stage.");
for (const Profile of Profiles)
{
    Emit("[[Profile]]");
    Emit(`Stage = "${Profile.StageName}"`);
    Emit(`Step = ${Profile.Step}`);
    Emit(`HudLineTop = ${Float(Profile.Values[43])}`);
    Emit(`Values = ${List(Profile.Values)}`);
    Emit(`ScanPlane = ${List(Profile.Plane)}`);
    Emit(`Amount = ${Float(Profile.Amount)}`);
}
Emit();
Emit("[Stage.Identity]");
Emit(`Origin = ${List(Identity.Origin)}\nForward = ${List(Identity.Forward)}\nRight = ${List(Identity.Right)}\nUp = ${List(Identity.Up)}`);
Emit("[Stage.Rotated]");
Emit(`Origin = ${List(Rotated.Origin)}\nForward = ${List(Rotated.Forward)}\nRight = ${List(Rotated.Right)}\nUp = ${List(Rotated.Up)}`);
Emit();
Emit("# EncodeOccupantMask (GarageView.js).");
const MaskCases = [[0, 1, 2, 3, 4], [31, 32, 33, 63, 64, 95, 96, 127], [127, 128, 255, 17], []];
for (const Case of MaskCases)
{
    Emit("[[Mask]]");
    Emit(`Indices = ${Ints(Case)}`);
    Emit(`Words = ${Ints(Array.from(EncodeOccupantMask(Case)))}`);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        SPARKLES
//------------------------------------------------------------------------------------------------------------------------

Emit();
Emit("# ConstructSparkleCloud (GarageView.js): 130 points, four floats each (the Float32Array's own values).");
Emit("[Sparkles]");
Emit(`Count = ${Sparkles.length / 4}`);
Emit(`Values = ${List(Array.from(Sparkles))}`);

//------------------------------------------------------------------------------------------------------------------------
//                                                     SHAPES AND THEIR EDGES
//------------------------------------------------------------------------------------------------------------------------

const Triangle = (...Corners) => ({ Corners });
const Shapes = [];
const A = [0, 0, 0], B = [1, 0, 0], C = [0, 1, 0];
Shapes.push({ Name: "Fold", Threshold: 14, Facets: [Triangle(A, B, C), Triangle(C, B, [1, 1, 1])] });
Shapes.push({ Name: "Flat", Threshold: 14, Facets: [Triangle(A, B, C), Triangle(C, B, [1, 1, 0])] });
Shapes.push({ Name: "Gentle", Threshold: 14, Facets: [Triangle(A, B, C), Triangle(C, B, [1, 1, 0.1])] });
Shapes.push({ Name: "GentleWide", Threshold: 5, Facets: [Triangle(A, B, C), Triangle(C, B, [1, 1, 0.1])] });
Shapes.push({ Name: "Welded", Threshold: 14, Facets: [Triangle(A, B, C), Triangle([0.00002, 0.99999, 0], [0.99998, 0.00003, 0], [1, 1, 0])] });
Shapes.push({ Name: "Apart", Threshold: 14, Facets: [Triangle(A, B, C), Triangle([0.0002, 0.9999, 0], [0.9998, 0.0003, 0], [1, 1, 0])] });
Shapes.push({ Name: "Collapsed", Threshold: 14, Facets: [Triangle(A, [0.00001, 0, 0], C), Triangle(A, B, C)] });
Shapes.push({ Name: "Back", Threshold: 14, Facets: [Triangle(A, B, C), Triangle(A, C, B)] });
// 📝 Four triangles on one edge (a non-manifold fin): the JavaScript's Map leaves a consumed edge in place as null, and a third triangle meets it.
Shapes.push({ Name: "Fin", Threshold: 14, Facets: [Triangle(A, B, C), Triangle(B, A, [0, 0, 1]), Triangle(A, B, [0, -1, 0]), Triangle(B, A, [0, 0, -1])] });
// 📝 Welding ties: coordinates whose four-decimal rounding sits exactly on a half, positive and negative.
Shapes.push({ Name: "Ties", Threshold: 14, Facets: [Triangle([0.00005, 0, 0], [1.00015, 0, 0], [0, 1.00025, 0]), Triangle([-0.00005, 0, 0], [0, 1.00025, 0], [-1.00015, -0.00035, 0.5])] });
// 📝 A tie decides a weld: 0.99995 × 10^4 is exactly 9999.5, which Math.round sends up to 10000 (the vertex the neighbour has) and a round-half-away or
//    a round-half-down would not; for the negative −0.99995 × 10^4 = −9999.5 it sends toward +∞, to −9999.
Shapes.push({ Name: "TieWeld", Threshold: 14, Facets: [Triangle([0, 0, 0], [1, 0, 0], [0, 1, 0]), Triangle([0, 0.99995, 0], [1, 0, 0], [1, 1, 0])] });
Shapes.push({ Name: "TieWeldNegative", Threshold: 14, Facets: [Triangle([0, 0, 0], [-1, 0, 0], [0, -0.9999, 0]), Triangle([0, -0.99995, 0], [-1, 0, 0], [-1, -1, 0])] });
// 📝 A unit cube (12 right-angle creases), a lat-long sphere (about 30° between neighbours) and a capped cylinder, at the two thresholds.
function Cube()
{
    const P = (X, Y, Z) => [X, Y, Z];
    const Quads = [[P(0, 0, 0), P(1, 0, 0), P(1, 1, 0), P(0, 1, 0)], [P(0, 0, 1), P(0, 1, 1), P(1, 1, 1), P(1, 0, 1)], [P(0, 0, 0), P(0, 0, 1), P(1, 0, 1), P(1, 0, 0)],
                   [P(1, 0, 0), P(1, 0, 1), P(1, 1, 1), P(1, 1, 0)], [P(1, 1, 0), P(1, 1, 1), P(0, 1, 1), P(0, 1, 0)], [P(0, 1, 0), P(0, 1, 1), P(0, 0, 1), P(0, 0, 0)]];
    return Quads.flatMap(([Q0, Q1, Q2, Q3]) => [Triangle(Q0, Q1, Q2), Triangle(Q0, Q2, Q3)]);
}
function Sphere(Segments, Rings)
{
    const Point = (Ring, Segment) =>
    {
        const Polar = Math.PI * Ring / Rings, Azimuth = 2.0 * Math.PI * Segment / Segments;
        return [0.5 * Math.sin(Polar) * Math.cos(Azimuth), 0.5 * Math.sin(Polar) * Math.sin(Azimuth), 0.5 * Math.cos(Polar)];
    };
    const Facets = [];
    for (let Ring = 0; Ring < Rings; ++Ring)
    {
        for (let Segment = 0; Segment < Segments; ++Segment)
        {
            const P00 = Point(Ring, Segment), P01 = Point(Ring, Segment + 1), P10 = Point(Ring + 1, Segment), P11 = Point(Ring + 1, Segment + 1);
            if (Ring > 0) Facets.push(Triangle(P00, P10, P01));
            if (Ring < Rings - 1) Facets.push(Triangle(P01, P10, P11));
        }
    }
    return Facets;
}
function Cylinder(Segments)
{
    const Facets = [];
    const Rim = (Segment, Height) => [0.5 * Math.cos(2.0 * Math.PI * Segment / Segments), 0.5 * Math.sin(2.0 * Math.PI * Segment / Segments), Height];
    for (let Segment = 0; Segment < Segments; ++Segment)
    {
        Facets.push(Triangle(Rim(Segment, 0), Rim(Segment + 1, 0), Rim(Segment + 1, 1)), Triangle(Rim(Segment, 0), Rim(Segment + 1, 1), Rim(Segment, 1)));
        Facets.push(Triangle([0, 0, 1], Rim(Segment, 1), Rim(Segment + 1, 1)));
    }
    return Facets;
}
for (const Threshold of [14, 28])
{
    Shapes.push({ Name: `Cube${Threshold}`, Threshold, Facets: Cube() });
    Shapes.push({ Name: `Sphere${Threshold}`, Threshold, Facets: Sphere(12, 8) });
    Shapes.push({ Name: `Cylinder${Threshold}`, Threshold, Facets: Cylinder(24) });
}
Emit();
Emit("# ExtractFeatureEdges (VehicleEdges.js) on shapes whose facets are listed here: Corners holds 9 numbers per facet, Segments 6 per segment.");
for (const Shape of Shapes)
{
    const Segments = ExtractFeatureEdges(Shape.Facets, Shape.Threshold);
    Emit("[[Shape]]");
    Emit(`Name = "${Shape.Name}"`);
    Emit(`Threshold = ${Float(Shape.Threshold)}`);
    Emit(`Corners = [${Shape.Facets.map((Facet) => List(Facet.Corners.flat())).join(", ")}]`);
    Emit(`SegmentCount = ${Segments.length / 6}`);
    Emit(`Segments = ${List(Segments)}`);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE CAR'S OWN WIREFRAME
//------------------------------------------------------------------------------------------------------------------------

// 📝 FNV-1a over the bytes of the records: one number that changes if any coordinate or tag of any of the segments does.
function Fnv1a64(Bytes)
{
    let Hash = 0xcbf29ce484222325n;
    for (const Byte of Bytes)
    {
        Hash ^= BigInt(Byte);
        Hash = (Hash * 0x100000001b3n) & 0xffffffffffffffffn;
    }
    return Hash;
}

// 📝 The engine's scene is the FSP1 facet space the illumination port loads (SceneStructure::LoadFacetSpace): the hero car's facets in doubles, as
//    the demo that exported it held them. Only what the scan reads is decoded: the sets, and the first five occupants (the body and its wheels).
function ReadFacetSpace(Bytes)
{
    const View = new DataView(Bytes.buffer, Bytes.byteOffset, Bytes.byteLength);
    let Cursor = 0;
    const U32 = () => { const Value = View.getUint32(Cursor, true); Cursor += 4; return Value; };
    const I32 = () => { const Value = View.getInt32(Cursor, true); Cursor += 4; return Value; };
    const F64 = () => { const Value = View.getFloat64(Cursor, true); Cursor += 8; return Value; };
    const V3  = () => [F64(), F64(), F64()];
    const Text = () =>
    {
        const Length = U32();
        const Value = Buffer.from(Bytes.buffer, Bytes.byteOffset + Cursor, Length).toString("utf8");
        Cursor += Length + ((4 - (Length % 4)) % 4);
        return Value;
    };
    if (Buffer.from(Bytes.buffer, Bytes.byteOffset, 4).toString("latin1") !== "FSP1") throw new Error("not an FSP1 facet space");
    Cursor = 4;
    if (U32() !== 1) throw new Error("unsupported FSP1 version");
    const SetCount = U32(), OccupantCount = U32();
    const Body = I32(), Wheels = [I32(), I32(), I32(), I32()];
    for (let Skipped = 0; Skipped < 6; ++Skipped) I32();    // ground occupant, ground tiles, four strips
    for (let Skipped = 0; Skipped < 8; ++Skipped) U32();    // distance and stress counts, the cone range
    const FacetSets = [];
    for (let Set = 0; Set < SetCount; ++Set)
    {
        Text(); const FacetCount = U32();
        F64(); F64(); V3(); V3(); U32();
        const Flags = U32();
        const Facets = [];
        for (let Facet = 0; Facet < FacetCount; ++Facet)
        {
            const Corners = [V3(), V3(), V3()];
            const Family = U32();
            U32();                                           // occupant offset
            if (Flags & 2) { U32(); V3(); V3(); V3(); }      // authored normals
            Facets.push({ Corners, Family });
        }
        FacetSets.push({ Facets });
    }
    const Occupants = [];
    for (let Occupant = 0; Occupant < Math.min(OccupantCount, 5); ++Occupant)
    {
        Text();
        const FacetSet = U32();
        U32(); I32();                                        // participation, material override
        for (let Element = 0; Element < 32; ++Element) F64();
        V3(); V3(); F64(); U32(); V3(); V3(); U32(); I32(); I32();
        Occupants.push({ FacetSet });
    }
    return { VehicleOccupants: { Body, Wheels }, Occupants, FacetSets };
}

function Describe(Name, Scene)
{
    const Edges = ConstructScanEdges(Scene);
    const Words = new Uint32Array(Edges.Records.buffer);
    const Counts = new Map();
    for (let Index = 0; Index < Edges.Count; ++Index)
    {
        Counts.set(Words[Index * 8 + 3], (Counts.get(Words[Index * 8 + 3]) ?? 0) + 1);
    }
    Emit();
    Emit(`[${Name}]`);
    Emit(`Body = ${Scene.VehicleOccupants.Body}`);
    Emit(`Wheels = ${Ints(Scene.VehicleOccupants.Wheels)}`);
    Emit(`BodyFacetSet = ${Scene.Occupants[Scene.VehicleOccupants.Body].FacetSet}`);
    Emit(`WheelFacetSets = ${Ints(Scene.VehicleOccupants.Wheels.map((Wheel) => Scene.Occupants[Wheel].FacetSet))}`);
    Emit(`BodyFacetCount = ${Scene.FacetSets[Scene.Occupants[Scene.VehicleOccupants.Body].FacetSet].Facets.length}`);
    Emit(`WheelFacetCount = ${Scene.FacetSets[Scene.Occupants[Scene.VehicleOccupants.Wheels[0]].FacetSet].Facets.length}`);
    Emit(`SegmentCount = ${Edges.Count}`);
    Emit(`RecordHash = "${Fnv1a64(new Uint8Array(Edges.Records.buffer)).toString(16).padStart(16, "0")}"`);
    for (const [Tag, Count] of [...Counts.entries()].sort((Left, Right) => Left[0] - Right[0]))
    {
        Emit(`[[${Name}.Tag]]`);
        Emit(`Tag = ${Tag}`);
        Emit(`Count = ${Count}`);
    }
    Emit(`[${Name}.First]`);
    Emit(`Records = ${List(Array.from(Edges.Records.slice(0, 8 * 4)).map((Value, Index) => (Index % 8 === 3 ? 0 : Value)))}`);
    Emit(`Tags = ${Ints(Array.from({ length: 4 }, (_, Index) => Words[Index * 8 + 3]))}`);
    Emit(`[${Name}.Last]`);
    Emit(`Records = ${List(Array.from(Edges.Records.slice((Edges.Count - 4) * 8)).map((Value, Index) => (Index % 8 === 3 ? 0 : Value)))}`);
    Emit(`Tags = ${Ints(Array.from({ length: 4 }, (_, Index) => Words[(Edges.Count - 4 + Index) * 8 + 3]))}`);
    return Edges.Count;
}

// 📝 The engine's scene: what the C++ validation loads and must reproduce segment for segment.
Emit();
Emit("# ConstructScanEdges on the engine's hero car (EngineContent/EnvironmentArchives/ProvingGround/ProvingGround.facetspace, decoded here from its FSP1 bytes):");
Emit("# segments per tag (occupant | family << 8) and a hash of the record bytes.");
const EngineScene = ReadFacetSpace(fs.readFileSync(path.join(Root, "EngineContent/EnvironmentArchives/ProvingGround/ProvingGround.facetspace")));
const EngineSegments = Describe("Car", EngineScene);
Emit(`[Car.Families]`);
Emit(`Values = ${Ints([CourseMaterial.BodyCoat, CourseMaterial.VehiclePlastic, CourseMaterial.Tyre, CourseMaterial.Headlight, CourseMaterial.Taillight])}`);

// 📝 The prototype's own scene (the 'Archive' lobby scene: 4,414 body facets with its cabin): documented, not asserted by the C++, whose scene is
//    the engine's (a different capture of the same car: see the README).
const Scene = new SceneStructure();
await Scene.ConstructScene("Archive", 0);
const DemoSegments = Describe("Demo", Scene);

fs.mkdirSync(path.dirname(Output), { recursive: true });
fs.writeFileSync(Output, `${Lines.join("\n")}\n`);
console.log(`wrote ${Output}: ${StepIndex} clock steps (${Sequence.Index.length} sampled), ${Profiles.length} profiles, ${Shapes.length} shapes, ${EngineSegments} segments on the engine's car, ${DemoSegments} on the prototype's`);
