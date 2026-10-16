//============================================================================================================================================
//                                                            LEVELSTRUCTURE.JS
//============================================================================================================================================
// 🧩 The Frontier proving grounds: shapes, placements, breakable props and the painted ground — declared here, built by FacetStructure.

// 📝 The layout (metres; +X down the main straight, +Y left, +Z up):
//
//        y +60 ┌── container yard ─────────────────────────────┬──── TRAINING ARENA (east wing) ──────────────────────┐
//              │  skidpad: 14 m ring,      hangar 28 × 16 m    │ ▲ ▲ ▲     ▲          ▲   bowling   ┌ spin box ┐        │
//              │  32 cones                                     │ launch   slalom    moose  15 pins  │ 18×18 m  │        │
//        y  +6 │═ kerb ═ START ▸ FRONTIER ═ JUMP ═ ramp ═ gravel│ 3 lanes  8 cones   ISO     ·        └──────────┘        │
//        y  −6 │═ kerb ═                                       ║ brake    11 m      3888-2           ◯ figure-8 drift ◯  │
//              │  tower   burnout box    SMASH alley (13 props)│ line,    pitch     lane                 two 12 m rings   │
//        y −60 └── lit tunnel ────────────────────────────────┴──────────────────────────────────────────────────────┘
//            x −60                                          x +60                                                x +156
//
//    The launch strip runs 62 m from the start line to the brake gantry, then 22–32 m of braking into a stop box.
//    The east wing is the training arena — Apex Legends' Firing Range, for driving: every station is a drill the
//    training sequence times and scores (TrainingSequence.js), every cone is a rigid body that can be knocked flying
//    and is rezzed back into place when its drill restarts.
//
//    Every shape is authored with its long side on local x and turned into place, so the occupant distance space —
//    a stack of volumes as wide and deep as its widest and deepest member — stays shallow; the tall, thin light poles
//    and the sign boards are rastered only (the pole heads are in the field and carded, so they light the night).
//    Large structures take coarse volumes and sparse cards: the global field's finest voxel is 12.5 cm anyway.

import { CourseMaterial } from "./MaterialSpecification.js";
import { CourseConstants, KerbConstants, SpawnPoint, EmitQuad, EmitBoxFacets, EmitPanelFacets, EmitPrismFacets,
         EmitExtrusionFacets, EmitConeFacets, EmitRampFacets, EmitTyreBarrierFacets, EmitCornerBoxFacets } from "./CourseStructure.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                      SHAPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Jersey barrier cross-section (y, z), counter-clockwise: 60 cm foot, 16 cm crown, 80 cm tall.
const JerseyProfile = [[-0.30, 0.0], [0.30, 0.0], [0.30, 0.08], [0.12, 0.30], [0.08, 0.80], [-0.08, 0.80], [-0.12, 0.30], [-0.30, 0.08]];

// 📝 Station signs: { Text, Position [x, y] (board centre), Yaw [°] (0: the board runs along x, read from ±y) }.
const ArenaSigns = [
    { Text: "LAUNCH",  Position: [76.0, -58.6],   Yaw: 0.0 },
    { Text: "SLALOM",  Position: [100.0, -58.6],  Yaw: 0.0 },
    { Text: "MOOSE",   Position: [120.0, -58.6],  Yaw: 0.0 },
    { Text: "DRIFT",   Position: [142.0, -59.0],  Yaw: 0.0 },
    { Text: "SPIN",    Position: [142.0, 50.5],   Yaw: 0.0 },
    { Text: "BOWLING", Position: [119.0, 57.6],   Yaw: 0.0 },
    { Text: "SKIDPAD", Position: [-30.0, 48.6],   Yaw: 0.0 },
    { Text: "SMASH",   Position: [-10.0, -38.0],  Yaw: 90.0 },
    { Text: "FRONTIER RANGE", Position: [61.6, 30.0], Yaw: 90.0 }
];
const SignPixel = 0.16;   // [m] letter pixel on the boards

function ResolveSignTexts()
{
    return [...new Set(ArenaSigns.map((Sign) => Sign.Text))];
}

function ResolveSignExtent(Text)
{
    return [Text.length * 6.0 * SignPixel - SignPixel + 4.0 * SignPixel, 11.0 * SignPixel];
}

// 📝 Key → { Emit, Voxel [m] (distance volume), Cards [texel/m], Distance (false: rastered only) }.
export const LevelShapes = Object.freeze({
    Kerb:         { Emit: () => EmitBoxFacets(KerbConstants.SegmentLength, KerbConstants.Depth, KerbConstants.Height, CourseMaterial.KerbRed), Voxel: 0.04, Cards: 16 },
    Ramp:         { Emit: () => EmitRampFacets(), Voxel: 0.12, Cards: 20 },
    Cone:         { Emit: () => EmitConeFacets(), Voxel: 0.02, Cards: 24 },
    HangarPanel:  { Emit: () => EmitPanelFacets(4.0, 0.4, 8.0, CourseMaterial.Cladding, CourseMaterial.WallChalk), Voxel: 0.28, Cards: 6 },
    HangarAccent: { Emit: () => EmitPanelFacets(4.0, 0.4, 8.0, CourseMaterial.Cladding, CourseMaterial.WallCrimson), Voxel: 0.28, Cards: 6 },
    HangarRoof:   { Emit: () => EmitBoxFacets(28.8, 2.0, 0.3, CourseMaterial.Cladding), Voxel: 0.24, Cards: 4 },
    LampStrip:    { Emit: () => EmitBoxFacets(24.0, 0.25, 0.12, CourseMaterial.LampCool), Voxel: 0.1, Cards: 4 },
    Cabinet:      { Emit: () => EmitBoxFacets(2.0, 0.6, 1.8, CourseMaterial.ContainerRed), Voxel: 0.08, Cards: 12 },
    Container40:  { Emit: () => EmitBoxFacets(12.2, 2.44, 2.6, CourseMaterial.ContainerOrange), Voxel: 0.12, Cards: 5 },
    Container20:  { Emit: () => EmitBoxFacets(6.1, 2.44, 2.6, CourseMaterial.ContainerBlue), Voxel: 0.12, Cards: 6 },
    TunnelWall:   { Emit: () => EmitBoxFacets(4.0, 0.5, 4.5, CourseMaterial.TunnelConcrete), Voxel: 0.15, Cards: 10 },
    TunnelRoof:   { Emit: () => EmitBoxFacets(9.0, 4.0, 0.5, CourseMaterial.TunnelConcrete), Voxel: 0.12, Cards: 8 },
    TunnelLamp:   { Emit: () => EmitBoxFacets(3.0, 0.12, 0.16, CourseMaterial.LampCool), Voxel: 0.04, Cards: 16 },
    Tower:        { Emit: () => EmitBoxFacets(5.0, 5.0, 9.0, CourseMaterial.BarrierConcrete), Voxel: 0.3, Cards: 6 },
    TowerCabin:   { Emit: () => EmitBoxFacets(6.5, 6.5, 2.6, CourseMaterial.Glass), Voxel: 0.15, Cards: 8 },
    TowerRoof:    { Emit: () => EmitBoxFacets(7.5, 7.5, 0.4, CourseMaterial.SteelDark), Voxel: 0.15, Cards: 6 },
    Jersey:       { Emit: () => EmitExtrusionFacets(JerseyProfile, 2.0, CourseMaterial.BarrierConcrete), Voxel: 0.05, Cards: 14 },
    TyreStack:    { Emit: () => EmitPrismFacets(0.33, 1.2, 12, CourseMaterial.Tyre), Voxel: 0.06, Cards: 16 },
    Pole:         { Emit: () => EmitBoxFacets(0.3, 0.3, 11.0, CourseMaterial.SteelDark), Voxel: 0.25, Cards: 0, Distance: false },
    PoleHead:     { Emit: () => EmitBoxFacets(1.8, 0.6, 0.3, CourseMaterial.LampWarm), Voxel: 0.05, Cards: 16 },
    FramePost:    { Emit: () => EmitBoxFacets(0.12, 0.12, 2.3, CourseMaterial.SteelDark), Voxel: 0.1, Cards: 0, Distance: false },
    // 📝 The training arena: tyre barrier sections, the launch gantries (rastered posts, a beam and a lamp strip in
    //    the field) and the station signs (rastered: double-sided boards with raised, faintly lit letters).
    TyreBarrier:  { Emit: () => EmitTyreBarrierFacets(6, 0.68, 0.33, 1.2), Voxel: 0.06, Cards: 12 },
    GantryPost:   { Emit: () => EmitBoxFacets(0.3, 0.3, 5.6, CourseMaterial.SteelDark), Voxel: 0.2, Cards: 0, Distance: false },
    GantryBeam:   { Emit: () => EmitBoxFacets(21.6, 0.5, 0.6, CourseMaterial.SteelDark), Voxel: 0.12, Cards: 4 },
    GantryLamp:   { Emit: () => EmitBoxFacets(20.0, 0.22, 0.10, CourseMaterial.GateLamp), Voxel: 0.1, Cards: 4 },
    SignPost:     { Emit: () => EmitBoxFacets(0.12, 0.12, 3.8, CourseMaterial.SteelDark), Voxel: 0.1, Cards: 0, Distance: false },
    ...Object.fromEntries(ResolveSignTexts().map((Text) =>
        [`Sign ${Text}`, { Emit: () => EmitSignFacets(Text), Voxel: 0.1, Cards: 0, Distance: false }]))
});

// 📝 Shapes the car collides with (as world boxes); roofs, lamps and kerbs are not colliders.
const ColliderShapes = new Set(["HangarPanel", "HangarAccent", "Cabinet", "Container40", "Container20", "TunnelWall",
                                "Tower", "Jersey", "TyreStack", "Pole", "FramePost", "TyreBarrier", "GantryPost", "SignPost"]);

//------------------------------------------------------------------------------------------------------------------------
//                                                    PLACEMENTS
//------------------------------------------------------------------------------------------------------------------------

const Degrees = Math.PI / 180.0;

// 📝 { Shape, Name, Position [x, y, z of the bottom centre], Yaw [rad], Material (override or −1), Collider }.
//    Cones are not placed here: they are rigid bodies (ResolveConePlacements) with an occupant block of their own.
export function ResolveLevelPlacements()
{
    const List = [];
    const Place = (Shape, Name, X, Y, Z = 0.0, Yaw = 0.0, Material = -1) =>
        List.push({ Shape, Name, Position: [X, Y, Z], Yaw, Material, Collider: ColliderShapes.has(Shape) });

    // ① kerbs: red and white segments along both edges of the main straight
    KerbConstants.Strips.forEach(([X0, X1, Y0, Y1], Strip) =>
    {
        const Segments = Math.round((X1 - X0) / KerbConstants.SegmentLength);
        for (let Segment = 0; Segment < Segments; ++Segment)
            Place("Kerb", `Kerb ${Strip ? "south" : "north"} ${Segment}`, X0 + (Segment + 0.5) * KerbConstants.SegmentLength, 0.5 * (Y0 + Y1), 0.0, 0.0,
                  (Segment & 1) ? CourseMaterial.PaintWhite : CourseMaterial.KerbRed);
    });
    Place("Ramp", "Jump ramp", 0.0, 0.0);

    // ② the hangar: crimson back wall, chalk side walls, slatted roof with lamp strips, cabinets and tyres inside
    for (let Panel = 0; Panel < 7; ++Panel)
        Place("HangarAccent", `Hangar back wall ${Panel}`, 22.0 + 4.0 * Panel, 46.2);
    for (let Panel = 0; Panel < 4; ++Panel)
    {
        Place("HangarPanel", `Hangar west wall ${Panel}`, 19.8, 32.0 + 4.0 * Panel, 0.0, 90.0 * Degrees);
        Place("HangarPanel", `Hangar east wall ${Panel}`, 48.2, 32.0 + 4.0 * Panel, 0.0, -90.0 * Degrees);
    }
    for (let Slat = 0; Slat < 6; ++Slat)
    {
        Place("HangarRoof", `Hangar roof ${Slat}`, 34.0, 31.0 + 3.0 * Slat, 8.0);
        Place("LampStrip", `Hangar lamp ${Slat}`, 34.0, 31.0 + 3.0 * Slat, 7.82);
    }
    for (const X of [24.0, 27.0, 41.0, 44.0])
        Place("Cabinet", `Tool cabinet ${X}`, X, 45.3);
    for (const [X, Y] of [[46.6, 43.8], [46.6, 42.9], [45.8, 43.6]])
        Place("TyreStack", "Hangar tyres", X, Y);

    // ③ container yard along the north edge, some two high
    const Colours = [CourseMaterial.ContainerOrange, CourseMaterial.ContainerBlue, CourseMaterial.ContainerGreen,
                     CourseMaterial.ContainerWhite, CourseMaterial.ContainerRed];
    [-50.0, -37.0, -24.0, -11.0, 2.0].forEach((X, Index) => Place("Container40", `Container ${Index}`, X, 55.0, 0.0, 0.0, Colours[Index]));
    Place("Container40", "Container stacked 0", -43.5, 55.0, 2.6, 0.0, Colours[2]);
    Place("Container40", "Container stacked 1", -17.5, 55.0, 2.6, 0.0, Colours[0]);
    Place("Container20", "Container short 0", 13.0, 55.0, 0.0, 0.0, Colours[3]);
    Place("Container20", "Container short 1", 13.0, 55.0, 2.6, 0.0, Colours[1]);
    Place("Container20", "Container short 2", 52.0, 52.0, 0.0, 90.0 * Degrees, Colours[4]);

    // ④ tunnel along the south edge, with lamp strips on both walls
    for (let Segment = 0; Segment < 8; ++Segment)
    {
        const X = -26.0 + 4.0 * Segment;
        Place("TunnelWall", `Tunnel north wall ${Segment}`, X, -43.75, 0.0, 0.0, CourseMaterial.WallCobalt);
        Place("TunnelWall", `Tunnel south wall ${Segment}`, X, -52.25, 0.0, 0.0, CourseMaterial.WallChalk);
        Place("TunnelRoof", `Tunnel roof ${Segment}`, X, -48.0, 4.5, 90.0 * Degrees);
        if (Segment % 2 === 0)
        {
            Place("TunnelLamp", `Tunnel lamp N${Segment}`, X, -44.06, 3.6);
            Place("TunnelLamp", `Tunnel lamp S${Segment}`, X, -51.94, 3.6);
        }
    }

    // ⑤ control tower by the start
    Place("Tower", "Control tower", -54.0, -11.0);
    Place("TowerCabin", "Control tower cabin", -54.0, -11.0, 9.0);
    Place("TowerRoof", "Control tower roof", -54.0, -11.0, 11.6);

    // ⑥ smash alley barriers, burnout tyre wall, the run-off tyre wall past the gravel
    for (let Barrier = 0; Barrier < 19; ++Barrier)
    {
        const X = -6.0 + 3.0 * Barrier;
        Place("Jersey", `Alley barrier N${Barrier}`, X, -19.6);
        Place("Jersey", `Alley barrier S${Barrier}`, X, -34.4);
    }
    for (let Stack = 0; Stack < 14; ++Stack)
        Place("TyreStack", `Burnout tyre wall ${Stack}`, -49.0 + 0.7 * Stack, -32.6);
    for (let Stack = 0; Stack < 18; ++Stack)
        Place("TyreStack", `Run-off tyre wall ${Stack}`, 58.6, -6.0 + 0.7 * Stack);

    // ⑦ frame posts either side of each glass pane in the alley
    for (const X of [6.0, 22.0])
    {
        Place("FramePost", `Pane post ${X}a`, X, -25.1);
        Place("FramePost", `Pane post ${X}b`, X, -28.9);
    }

    // ⑧ the training arena: launch gantries over the start and the brake line, the tyre barrier the launch lanes run
    //    into, the station signs on their posts
    for (const [Y, Lamp, Name] of [[-50.0, CourseMaterial.GateLamp, "start"], [12.0, CourseMaterial.GateLampRed, "brake"]])
    {
        Place("GantryPost", `Launch ${Name} gantry post W`, 65.6, Y);
        Place("GantryPost", `Launch ${Name} gantry post E`, 86.4, Y);
        Place("GantryBeam", `Launch ${Name} gantry`, 76.0, Y, 5.0);
        Place("GantryLamp", `Launch ${Name} lamps`, 76.0, Y, 4.88, 0.0, Lamp);
    }
    for (let Section = 0; Section < 5; ++Section)
        Place("TyreBarrier", `Launch tyre barrier ${Section}`, 67.8 + 4.1 * Section, 57.6);
    for (const Sign of ArenaSigns)
    {
        const [Width] = ResolveSignExtent(Sign.Text);
        const Yaw = Sign.Yaw * Degrees;
        const Along = [Math.cos(Yaw), Math.sin(Yaw)];
        Place(`Sign ${Sign.Text}`, `Sign ${Sign.Text}`, Sign.Position[0], Sign.Position[1], 2.3, Yaw);
        for (const Side of [-1, 1])
            Place("SignPost", `Sign ${Sign.Text} post ${Side < 0 ? "a" : "b"}`, Sign.Position[0] + Side * Along[0] * (0.5 * Width - 0.35),
                  Sign.Position[1] + Side * Along[1] * (0.5 * Width - 0.35));
    }

    // ⑨ light poles (rastered) with their lamp heads (in the field: they light the night)
    const Poles = [[-45.0, 10.0, -90.0], [-15.0, 10.0, -90.0], [15.0, 10.0, -90.0], [45.0, 10.0, -90.0],
                   [-30.0, -10.0, 90.0], [0.0, -10.0, 90.0], [30.0, -10.0, 90.0], [-48.0, 30.0, 0.0],
                   [-12.0, 44.0, 180.0], [-56.0, -24.0, 0.0], [20.0, -38.0, 90.0], [8.0, 30.0, 0.0],
                   [90.0, -36.0, 180.0], [90.0, 8.0, 180.0], [90.0, 46.0, 180.0], [110.0, -36.0, 0.0], [110.0, 8.0, 0.0],
                   [128.0, 30.0, 0.0], [154.0, -32.0, 180.0], [128.0, -4.0, -45.0]];
    Poles.forEach(([X, Y, Facing], Index) =>
    {
        Place("Pole", `Light pole ${Index}`, X, Y);
        const Angle = Facing * Degrees;
        Place("PoleHead", `Light pole head ${Index}`, X + 0.7 * Math.cos(Angle), Y + 0.7 * Math.sin(Angle), 10.8, Angle);
    });
    return List;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONES
//------------------------------------------------------------------------------------------------------------------------

// 📝 The ISO 3888-2 double lane change for a 1.9 m car: section 1 (12 m long, 1.1·W + 0.25 wide), a 13.5 m gap,
//    section 3 (11 m, W + 1, offset so its left edge is 1 m past section 1's right edge), a 12.5 m gap, section 5
//    (12 m, min(1.3·W + 0.25, 3)). Cones every three metres or so down both sides of each gated section.
export const MooseLayout = Object.freeze({
    EntryX: 120.0, EntryY: -42.0,
    Sections: Object.freeze([
        Object.freeze({ Y0: -42.0, Y1: -30.0, Centre: 120.0,   Width: 2.45 }),
        Object.freeze({ Y0: -16.5, Y1: -5.5,  Centre: 123.725, Width: 3.0 }),
        Object.freeze({ Y0: 7.0,   Y1: 19.0,  Centre: 120.0,   Width: 2.85 })
    ])
});

export const SpinBox    = Object.freeze({ Minimum: [133.0, 27.0], Maximum: [151.0, 45.0] });
export const DriftRings = Object.freeze({ Centres: Object.freeze([[142.0, -44.0], [142.0, -20.0]]), Radius: 12.0 });
export const SkidpadRing = Object.freeze({ Centre: [-30.0, 30.0], Radius: 14.0 });
export const BowlingPins = Object.freeze({ Head: [119.0, 46.0], Rows: 5, Pitch: 0.95 });

// 📝 { Name, Position [x, y], Yaw [rad], Group } — Group names the drill a cone belongs to, so a drill can count the
//    cones it disturbed and rez them back into place when it restarts.
export function ResolveConePlacements()
{
    const List = [];
    const Cone = (Group, X, Y, Yaw = 0.0) => List.push({ Name: `Cone ${Group} ${List.filter((Item) => Item.Group === Group).length}`,
                                                          Position: [X, Y], Yaw, Group });
    // ① skidpad: 24 round the outside of the ring, 8 marking the inside
    for (let Index = 0; Index < 24; ++Index)
    {
        const Angle = 2.0 * Math.PI * Index / 24;
        Cone("skidpad", SkidpadRing.Centre[0] + 15.2 * Math.cos(Angle), SkidpadRing.Centre[1] + 15.2 * Math.sin(Angle), Angle);
    }
    for (let Index = 0; Index < 8; ++Index)
    {
        const Angle = 2.0 * Math.PI * (Index + 0.5) / 8;
        Cone("skidpad", SkidpadRing.Centre[0] + 8.2 * Math.cos(Angle), SkidpadRing.Centre[1] + 8.2 * Math.sin(Angle), Angle);
    }
    // ② the start line and the ramp funnel
    Cone("straight", -34.15, 6.7);
    Cone("straight", -34.15, -6.7);
    for (const X of [15.0, 19.0])
        for (const Y of [-3.7, 3.7])
            Cone("jump", X, Y);
    // ③ burnout box corners
    for (const [X, Y] of [[-49.4, -29.4], [-32.6, -29.4], [-49.4, -18.6], [-32.6, -18.6]])
        Cone("burnout", X, Y);
    // ④ launch: the far edge of each lane's stop box
    for (const X of [70.0, 76.0, 82.0])
        for (const Offset of [-2.1, 2.1])
            Cone("launch", X + Offset, 44.6);
    // ⑤ slalom: eight cones on one line, 11 m apart, gate cones at both ends
    for (let Index = 0; Index < 8; ++Index)
        Cone("slalom", 100.0, -30.0 + 11.0 * Index);
    for (const Y of [-42.0, 55.0])
        for (const X of [94.0, 106.0])
            Cone("slalom", X, Y);
    // ⑥ moose test: both sides of each gated section
    for (const Section of MooseLayout.Sections)
    {
        const Count = 5;
        for (let Index = 0; Index < Count; ++Index)
        {
            const Y = Section.Y0 + (Section.Y1 - Section.Y0) * Index / (Count - 1);
            Cone("moose", Section.Centre - 0.5 * Section.Width, Y);
            Cone("moose", Section.Centre + 0.5 * Section.Width, Y);
        }
    }
    // ⑦ spin box: the perimeter every 4.5 m, a 9 m entry gap in the south side
    const [BX0, BY0] = SpinBox.Minimum, [BX1, BY1] = SpinBox.Maximum;
    for (let Step = 0; Step <= 4; ++Step)
    {
        const X = BX0 + (BX1 - BX0) * Step / 4, Y = BY0 + (BY1 - BY0) * Step / 4;
        if (Step !== 2)
            Cone("spin", X, BY0);
        Cone("spin", X, BY1);
        if (Step > 0 && Step < 4)
        {
            Cone("spin", BX0, Y);
            Cone("spin", BX1, Y);
        }
    }
    // ⑧ figure-8: a three-cone pylon at each ring's centre, markers outside each ring away from the crossing
    DriftRings.Centres.forEach(([CX, CY], Ring) =>
    {
        for (let Index = 0; Index < 3; ++Index)
        {
            const Angle = 2.0 * Math.PI * Index / 3 + 0.5 * Math.PI;
            Cone("drift", CX + 0.45 * Math.cos(Angle), CY + 0.45 * Math.sin(Angle));
        }
        for (let Index = 0; Index < 8; ++Index)
        {
            const Angle = 2.0 * Math.PI * Index / 8;
            const TowardCrossing = Ring === 0 ? Math.sin(Angle) > 0.5 : Math.sin(Angle) < -0.5;
            if (!TowardCrossing)
                Cone("drift", CX + 13.6 * Math.cos(Angle), CY + 13.6 * Math.sin(Angle), Angle);
        }
    });
    // ⑨ cone bowling: fifteen pins in a triangle at the end of a run-up lane
    for (let Row = 0; Row < BowlingPins.Rows; ++Row)
        for (let Pin = 0; Pin <= Row; ++Pin)
            Cone("bowling", BowlingPins.Head[0] + (Pin - 0.5 * Row) * BowlingPins.Pitch, BowlingPins.Head[1] + Row * 0.86 * BowlingPins.Pitch);
    return List;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 BREAKABLE PROPS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The smash alley, approached from its west end: every prop is a closed convex solid with a Fracture-editor recipe.
//    Position is the bottom centre; Capacity reserves that many fragment occupants and its share of vertex room.
//    DensityScale < 1 makes a crate hollow rather than a cube of solid pine.
export function ResolveBreakables()
{
    const Approach = [-1.0, 0.0, 0.0];
    const List = [];
    const Add = (Name, Shape, Material, X, Y, Yaw, Options = {}) => List.push({
        Name, Shape, Material, Position: [X, Y, 0.0], Yaw, Approach, Capacity: Options.Capacity || 40,
        DensityScale: Options.DensityScale || 1.0, Mode: Options.Mode || "dynamic", Energy: Options.Energy || 6000, Seed: Options.Seed
    });
    const Crate = { Kind: "box", Extent: [0.9, 0.9, 0.9] };
    const Pane = { Kind: "box", Extent: [3.4, 0.05, 2.0] };
    const Tempered = { Kind: "box", Extent: [3.4, 0.04, 2.0] };
    const Pillar = { Kind: "prism", Radius: 0.36, Height: 1.8, Sides: 8 };
    const Barrier = { Kind: "box", Extent: [1.8, 0.5, 0.8] };
    const Wall = { Kind: "box", Extent: [3.6, 0.5, 1.2] };
    const Block = { Kind: "box", Extent: [1.2, 0.6, 0.8] };
    for (const Y of [-28.1, -27.0, -25.9])
        Add(`Wooden crate ${List.length + 1}`, Crate, "wood", -2.0, Y, 0.12 * (Y + 27.0), { DensityScale: 0.32, Capacity: 32 });
    Add("Glass pane", Pane, "glass", 6.0, -27.0, 90.0 * Degrees, { Capacity: 48 });
    Add("Concrete pillar A", Pillar, "concrete", 14.0, -28.6, 0.0, { Capacity: 40 });
    Add("Concrete pillar B", Pillar, "concrete", 14.0, -25.4, 0.0, { Capacity: 40 });
    Add("Tempered pane", Tempered, "tempered", 22.0, -27.0, 90.0 * Degrees, { Capacity: 48 });
    Add("ABS barrier A", Barrier, "plastic", 30.0, -28.0, 90.0 * Degrees, { Capacity: 24 });
    Add("ABS barrier B", Barrier, "plastic", 30.0, -26.0, 90.0 * Degrees, { Capacity: 24 });
    Add("Stone wall", Wall, "rock", 38.0, -27.0, 90.0 * Degrees, { Capacity: 40 });
    Add("Wooden crate 4", Crate, "wood", 46.0, -28.2, 0.3, { DensityScale: 0.32, Capacity: 32 });
    Add("Wooden crate 5", Crate, "wood", 46.0, -25.8, -0.2, { DensityScale: 0.32, Capacity: 32 });
    Add("Concrete block", Block, "concrete", 46.5, -27.0, 90.0 * Degrees, { Capacity: 40 });
    return List;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   GROUND PAINT
//------------------------------------------------------------------------------------------------------------------------

// 📝 Zones of the ground lattice (4 m cells, classified by centre): later entries win; Fresh zones never patch.
const GroundZones = [
    { Minimum: [-52.0, -32.0], Maximum: [-28.0, -16.0], Family: CourseMaterial.ApronConcrete },   // burnout apron
    { Minimum: [16.0, 24.0],   Maximum: [52.0, 48.0],   Family: CourseMaterial.ApronConcrete },   // hangar floor and forecourt
    { Minimum: [-60.0, 48.0],  Maximum: [24.0, 60.0],   Family: CourseMaterial.ApronConcrete },   // container yard
    { Minimum: [-60.0, -16.0], Maximum: [-48.0, -8.0],  Family: CourseMaterial.ApronConcrete },   // tower apron
    { Minimum: [52.0, -8.0],   Maximum: [60.0, 8.0],    Family: CourseMaterial.Gravel },          // run-off gravel
    { Minimum: [64.0, -60.0],  Maximum: [88.0, 60.0],   Family: CourseMaterial.Asphalt },         // launch lanes, fresh
    { Minimum: [112.0, -46.0], Maximum: [128.0, 22.0],  Family: CourseMaterial.ApronConcrete },   // moose test lane
    { Minimum: [112.0, 24.0],  Maximum: [126.0, 56.0],  Family: CourseMaterial.ApronConcrete }    // bowling lane
];

// 📝 5 × 7 block letters and digits, top row first.
const Glyphs = {
    A: ["01110", "10001", "10001", "11111", "10001", "10001", "10001"],
    B: ["11110", "10001", "10001", "11110", "10001", "10001", "11110"],
    C: ["01110", "10001", "10000", "10000", "10000", "10001", "01110"],
    D: ["11110", "10001", "10001", "10001", "10001", "10001", "11110"],
    E: ["11111", "10000", "10000", "11110", "10000", "10000", "11111"],
    F: ["11111", "10000", "10000", "11110", "10000", "10000", "10000"],
    G: ["01110", "10001", "10000", "10111", "10001", "10001", "01111"],
    H: ["10001", "10001", "10001", "11111", "10001", "10001", "10001"],
    I: ["01110", "00100", "00100", "00100", "00100", "00100", "01110"],
    J: ["00111", "00010", "00010", "00010", "00010", "10010", "01100"],
    K: ["10001", "10010", "10100", "11000", "10100", "10010", "10001"],
    L: ["10000", "10000", "10000", "10000", "10000", "10000", "11111"],
    M: ["10001", "11011", "10101", "10101", "10001", "10001", "10001"],
    N: ["10001", "11001", "10101", "10011", "10001", "10001", "10001"],
    O: ["01110", "10001", "10001", "10001", "10001", "10001", "01110"],
    P: ["11110", "10001", "10001", "11110", "10000", "10000", "10000"],
    Q: ["01110", "10001", "10001", "10001", "10101", "10010", "01101"],
    R: ["11110", "10001", "10001", "11110", "10100", "10010", "10001"],
    S: ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
    T: ["11111", "00100", "00100", "00100", "00100", "00100", "00100"],
    U: ["10001", "10001", "10001", "10001", "10001", "10001", "01110"],
    V: ["10001", "10001", "10001", "10001", "10001", "01010", "00100"],
    W: ["10001", "10001", "10001", "10101", "10101", "10101", "01010"],
    X: ["10001", "10001", "01010", "00100", "01010", "10001", "10001"],
    Y: ["10001", "10001", "01010", "00100", "00100", "00100", "00100"],
    Z: ["11111", "00001", "00010", "00100", "01000", "10000", "11111"],
    0: ["01110", "10001", "10011", "10101", "11001", "10001", "01110"],
    1: ["00100", "01100", "00100", "00100", "00100", "00100", "01110"],
    2: ["01110", "10001", "00001", "00010", "00100", "01000", "11111"],
    3: ["11111", "00010", "00100", "00010", "00001", "10001", "01110"],
    4: ["00010", "00110", "01010", "10010", "11111", "00010", "00010"],
    5: ["11111", "10000", "11110", "00001", "00001", "10001", "01110"],
    6: ["00110", "01000", "10000", "11110", "10001", "10001", "01110"],
    7: ["11111", "00001", "00010", "00100", "01000", "01000", "01000"],
    8: ["01110", "10001", "10001", "01110", "10001", "10001", "01110"],
    9: ["01110", "10001", "10001", "01111", "00001", "00010", "01100"],
    "-": ["00000", "00000", "00000", "11111", "00000", "00000", "00000"],
    ".": ["00000", "00000", "00000", "00000", "00000", "01100", "01100"]
};

// 📝 Calls Visit(Column0, Column1, Row) for every horizontal run of lit pixels in a glyph.
function VisitGlyphRuns(Letter, Visit)
{
    const Glyph = Glyphs[Letter];
    if (!Glyph)
        return;
    Glyph.forEach((Row, RowIndex) =>
    {
        let Start = -1;
        for (let Column = 0; Column <= 5; ++Column)
        {
            const Lit = Column < 5 && Row[Column] === "1";
            if (Lit && Start < 0)
                Start = Column;
            if (!Lit && Start >= 0)
            {
                Visit(Start, Column, RowIndex);
                Start = -1;
            }
        }
    });
}

// 📝 A station sign, local to the bottom centre of its board: the board runs along x, 10 cm thick, and carries the
//    text raised 2 cm on both faces — reading left to right from either side.
function EmitSignFacets(Text)
{
    const Pixel = SignPixel, Thickness = 0.10, Raise = 0.02;
    const [Width, Height] = ResolveSignExtent(Text);
    const Facets = EmitBoxFacets(Width, Thickness, Height, CourseMaterial.SignBoard);
    const TextWidth = Text.length * 6.0 * Pixel - Pixel;
    for (const Side of [-1, 1])
    {
        // 📝 Seen from −y the reading direction is +x; seen from +y it is −x.
        const Reading = Side < 0 ? 1.0 : -1.0;
        const Face = Side * 0.5 * Thickness;
        let Cursor = -0.5 * TextWidth;
        for (const Letter of Text)
        {
            VisitGlyphRuns(Letter, (Column0, Column1, Row) =>
            {
                const A = Reading * (Cursor + Column0 * Pixel), B = Reading * (Cursor + Column1 * Pixel);
                const Top = Height - 2.0 * Pixel - Row * Pixel;
                const Low = [Math.min(A, B), Math.min(Face, Face + Side * Raise), Top - Pixel];
                const High = [Math.max(A, B), Math.max(Face, Face + Side * Raise), Top];
                Facets.push(...EmitCornerBoxFacets(Low, High, CourseMaterial.SignLetter));
            });
            Cursor += 6.0 * Pixel;
        }
    }
    return Facets;
}

const PaintLift = 0.004;   // [m] paint sits just above the surface it is painted on

// 📝 The ground occupant's facets: grass surround, the asphalt pad in 4 m cells (one in seven a patch repair), the
//    zones, then the paint — lines, the start strip, the drift rings, the stations' markings and their names, each
//    a thin quad.
export function EmitLevelGroundFacets()
{
    const C = CourseConstants;
    const Facets = [];
    const Outer = 240.0;
    const CentreX = 0.5 * (C.PadMinimum[0] + C.PadMaximum[0]);
    EmitQuad(Facets, [CentreX - Outer, -Outer, C.SurroundHeight], [CentreX + Outer, -Outer, C.SurroundHeight],
             [CentreX + Outer, Outer, C.SurroundHeight], [CentreX - Outer, Outer, C.SurroundHeight], CourseMaterial.Grass);
    const CellsX = Math.round((C.PadMaximum[0] - C.PadMinimum[0]) / C.GroundCell);
    const CellsY = Math.round((C.PadMaximum[1] - C.PadMinimum[1]) / C.GroundCell);
    for (let Row = 0; Row < CellsX; ++Row)
        for (let Column = 0; Column < CellsY; ++Column)
        {
            const X0 = C.PadMinimum[0] + Row * C.GroundCell, X1 = X0 + C.GroundCell;
            const Y0 = C.PadMinimum[1] + Column * C.GroundCell, Y1 = Y0 + C.GroundCell;
            const Centre = [0.5 * (X0 + X1), 0.5 * (Y0 + Y1)];
            const Hash = Math.abs(Math.sin(Row * 12.9898 + Column * 78.233) * 43758.5453) % 1.0;
            let Family = Hash < 0.14 ? CourseMaterial.AsphaltPatch : CourseMaterial.Asphalt;
            for (const Zone of GroundZones)
                if (Centre[0] > Zone.Minimum[0] && Centre[0] < Zone.Maximum[0] && Centre[1] > Zone.Minimum[1] && Centre[1] < Zone.Maximum[1])
                    Family = Zone.Family;
            EmitQuad(Facets, [X0, Y0, 0.0], [X1, Y0, 0.0], [X1, Y1, 0.0], [X0, Y1, 0.0], Family);
        }
    const Paint = (X0, Y0, X1, Y1, Family) =>
        EmitQuad(Facets, [Math.min(X0, X1), Math.min(Y0, Y1), PaintLift], [Math.max(X0, X1), Math.min(Y0, Y1), PaintLift],
                 [Math.max(X0, X1), Math.max(Y0, Y1), PaintLift], [Math.min(X0, X1), Math.max(Y0, Y1), PaintLift], Family);
    const White = CourseMaterial.PaintWhite, Yellow = CourseMaterial.PaintYellow;
    const Green = CourseMaterial.PaintGreen, Blue = CourseMaterial.PaintBlue;
    // 📝 Text read driving +x (Up +x, Right −y), +y (Up +y, Right +x) or −y (Up −y, Right −x).
    const ReadEast = [[1.0, 0.0], [0.0, -1.0]], ReadNorth = [[0.0, 1.0], [1.0, 0.0]], ReadSouth = [[0.0, -1.0], [-1.0, 0.0]];
    const Text = (Words, Centre, Reading, PixelWidth, PixelLength, Gap, Family) =>
    {
        const [Up, Right] = Reading;
        const Across = Words.length * (5.0 * PixelWidth + Gap) - Gap, Along = 7.0 * PixelLength;
        const Origin = [Centre[0] - Right[0] * 0.5 * Across + Up[0] * 0.5 * Along, Centre[1] - Right[1] * 0.5 * Across + Up[1] * 0.5 * Along];
        PaintText(Paint, Words, Origin, Up, Right, PixelWidth, PixelLength, Gap, Family);
    };

    // ① main straight: edge lines (broken by the ramp), centre dashes, start strip, grid box
    for (const Y of [5.8, -5.8])
    {
        Paint(-52.0, Y - 0.1, 20.0, Y + 0.1, White);
        Paint(30.0, Y - 0.1, 52.0, Y + 0.1, White);
    }
    for (let X = -30.0; X < 10.0; X += 6.0)
        Paint(X, -0.075, X + 3.0, 0.075, White);
    for (let Square = 0; Square < 20; ++Square)
        for (let Rank = 0; Rank < 2; ++Rank)
            if ((Square + Rank) % 2 === 0)
                Paint(-34.3 + 0.3 * Rank, -5.8 + 0.58 * Square, -34.0 + 0.3 * Rank, -5.8 + 0.58 * (Square + 1), White);
    PaintOutline(Paint, -42.6, -1.3, -37.4, 1.3, 0.12, Yellow);
    PaintText(Paint, "FRONTIER", [-23.0, 5.2], [1.0, 0.0], [0.0, -1.0], 0.2, 0.5, 0.33, White);

    // ② the jump: its name before the ramp, distance lines past the lip every 5 m
    Text("JUMP", [15.2, 0.0], ReadEast, 0.24, 0.42, 0.3, Yellow);
    for (const [Metres, Label] of [[5.0, "5"], [10.0, "10"], [15.0, "15"], [20.0, "20"]])
    {
        const X = 28.0 + Metres;
        Paint(X - 0.06, -5.6, X + 0.06, 5.6, White);
        Text(Label, [X - 1.2, 4.3], ReadEast, 0.14, 0.24, 0.16, White);
    }

    // ③ skidpad: the 14 m drift ring in white, a dashed 9 m inner guide in yellow
    PaintRing(Paint, Facets, SkidpadRing.Centre[0], SkidpadRing.Centre[1], SkidpadRing.Radius, 0.25, 72, 1.0, White);
    PaintRing(Paint, Facets, SkidpadRing.Centre[0], SkidpadRing.Centre[1], 9.0, 0.18, 36, 0.5, Yellow);

    // ④ hangar bays and the forecourt line
    for (const X of [24.0, 30.0, 36.0, 42.0, 48.0])
        Paint(X - 0.08, 32.0, X + 0.08, 42.0, White);
    Paint(18.0, 29.4, 50.0, 29.6, Yellow);

    // ⑤ smash alley lane lines, the entry hatching and its name
    Paint(-8.0, -20.7, 52.0, -20.5, Yellow);
    Paint(-8.0, -33.5, 52.0, -33.3, Yellow);
    for (let Hatch = 0; Hatch < 6; ++Hatch)
        PaintSlanted(Facets, -12.0 + 0.0, -32.5 + 2.0 * Hatch, 3.0, 0.35, Yellow);
    Text("SMASH", [-5.8, -27.0], ReadEast, 0.25, 0.4, 0.375, White);

    // ⑥ burnout box: outline and the word, read driving in from the straight (heading −y)
    PaintOutline(Paint, -49.0, -29.0, -33.0, -19.0, 0.25, Yellow);
    PaintText(Paint, "BURNOUT", [-35.9, -24.6], [0.0, -1.0], [-1.0, 0.0], 0.24, 0.5, 0.3, Yellow);

    // ⑦ tunnel approach lines
    Paint(-36.0, -44.6, 10.0, -44.4, White);
    Paint(-36.0, -51.6, 10.0, -51.4, White);

    // ⑧ launch: three 6 m lanes, the green start line, 60 ft ticks, the brake line, a stop box per lane and the
    //    braking distance every 10 m past the brake line
    for (const X of [67.0, 73.0, 79.0, 85.0])
    {
        if (X === 67.0 || X === 85.0)
            Paint(X - 0.08, -56.0, X + 0.08, 50.0, White);
        else
            for (let Y = -50.0; Y < 50.0; Y += 4.0)
                Paint(X - 0.06, Y, X + 0.06, Y + 2.0, White);
        Paint(X - 0.5, -31.75, X + 0.5, -31.6, Yellow);
    }
    Paint(67.0, -50.25, 85.0, -49.75, Green);
    Paint(67.0, 11.75, 85.0, 12.25, Yellow);
    for (const X of [70.0, 76.0, 82.0])
        PaintOutline(Paint, X - 2.2, 34.0, X + 2.2, 44.0, 0.15, Green);
    for (const Metres of [10, 20, 30, 40])
    {
        const Y = 12.0 + Metres;
        Paint(85.2, Y - 0.06, 86.2, Y + 0.06, White);
        Text(String(Metres), [87.4, Y], ReadNorth, 0.14, 0.24, 0.16, White);
    }
    Text("LAUNCH", [76.0, -55.2], ReadNorth, 0.36, 0.56, 0.5, White);
    Text("BRAKE", [76.0, 15.0], ReadNorth, 0.22, 0.34, 0.3, Yellow);

    // ⑨ slalom: dashed lane edges, start and finish lines, the name
    for (const X of [93.0, 107.0])
        for (let Y = -46.0; Y < 58.0; Y += 4.0)
            Paint(X - 0.06, Y, X + 0.06, Y + 2.0, White);
    Paint(93.0, -42.2, 107.0, -41.8, Green);
    for (let Square = 0; Square < 24; ++Square)
        for (let Rank = 0; Rank < 2; ++Rank)
            if ((Square + Rank) % 2 === 0)
                Paint(93.0 + 0.5833 * Square, 54.8 + 0.3 * Rank, 93.0 + 0.5833 * (Square + 1), 55.1 + 0.3 * Rank, White);
    Text("SLALOM", [100.0, -55.0], ReadNorth, 0.3, 0.56, 0.45, White);

    // ⑩ moose test: the approach lane, entry and exit lines, the name
    for (const X of [118.775, 121.225])
        for (let Y = -58.0; Y < -42.0; Y += 3.0)
            Paint(X - 0.06, Y, X + 0.06, Y + 1.5, White);
    Paint(118.7, -42.2, 121.3, -41.8, Green);
    Paint(118.0, 18.8, 122.0, 19.2, White);
    Text("MOOSE", [120.0, -51.0], ReadNorth, 0.3, 0.56, 0.45, White);

    // ⑪ cone bowling: the run-up lane, the foul line, the name
    for (const X of [114.5, 123.5])
        for (let Y = 24.0; Y < 44.0; Y += 4.0)
            Paint(X - 0.06, Y, X + 0.06, Y + 2.0, White);
    Paint(114.5, 39.85, 123.5, 40.15, Yellow);
    Text("BOWLING", [119.0, 28.0], ReadNorth, 0.18, 0.34, 0.24, White);

    // ⑫ spin box: the outline, a dashed circle inside, the name
    PaintOutline(Paint, SpinBox.Minimum[0], SpinBox.Minimum[1], SpinBox.Maximum[0], SpinBox.Maximum[1], 0.2, Yellow);
    PaintRing(Paint, Facets, 0.5 * (SpinBox.Minimum[0] + SpinBox.Maximum[0]), 0.5 * (SpinBox.Minimum[1] + SpinBox.Maximum[1]), 6.0, 0.16, 40, 0.5, White);
    Text("SPIN", [142.0, 23.2], ReadNorth, 0.3, 0.56, 0.45, White);

    // ⑬ figure-8: two blue rings touching at the crossing, the name read driving south into it
    for (const [CX, CY] of DriftRings.Centres)
        PaintRing(Paint, Facets, CX, CY, DriftRings.Radius, 0.3, 80, 1.0, Blue);
    Text("DRIFT", [142.0, -4.4], ReadSouth, 0.3, 0.56, 0.45, Blue);

    // ⑭ the arena's edge
    Paint(60.5, -59.2, 155.5, -59.0, White);
    Paint(60.5, 59.0, 155.5, 59.2, White);
    Paint(155.3, -59.2, 155.5, 59.2, White);
    return Facets;
}

function PaintOutline(Paint, X0, Y0, X1, Y1, Width, Family)
{
    Paint(X0, Y0, X1, Y0 + Width, Family);
    Paint(X0, Y1 - Width, X1, Y1, Family);
    Paint(X0, Y0 + Width, X0 + Width, Y1 - Width, Family);
    Paint(X1 - Width, Y0 + Width, X1, Y1 - Width, Family);
}

// 📝 A ring of Segments quads; Duty < 1 leaves gaps (a dashed ring).
function PaintRing(Paint, Facets, CentreX, CentreY, Radius, Width, Segments, Duty, Family)
{
    for (let Segment = 0; Segment < Segments; ++Segment)
    {
        const A0 = 2.0 * Math.PI * Segment / Segments, A1 = 2.0 * Math.PI * (Segment + Duty) / Segments;
        const Inner = Radius - 0.5 * Width, Outer = Radius + 0.5 * Width;
        const P = (Angle, R) => [CentreX + R * Math.cos(Angle), CentreY + R * Math.sin(Angle), PaintLift];
        EmitQuad(Facets, P(A0, Inner), P(A0, Outer), P(A1, Outer), P(A1, Inner), Family);
    }
}

function PaintSlanted(Facets, X, Y, Length, Width, Family)
{
    const D = Length / Math.SQRT2, W = Width / Math.SQRT2;
    EmitQuad(Facets, [X, Y, PaintLift], [X + W, Y - W, PaintLift], [X + W + D, Y - W + D, PaintLift], [X + D, Y + D, PaintLift], Family);
}

// 📝 Block letters on the ground: Origin is the top-left corner as the approaching driver reads it, Up the direction
//    of the letters' tops (away from the driver), Right the reading direction. Each row's runs become one quad.
function PaintText(Paint, Text, Origin, Up, Right, PixelWidth, PixelLength, Gap, Family)
{
    let Cursor = 0.0;
    for (const Letter of Text)
    {
        VisitGlyphRuns(Letter, (Start, End, RowIndex) =>
        {
            const Across0 = Cursor + Start * PixelWidth, Across1 = Cursor + End * PixelWidth;
            const Down0 = RowIndex * PixelLength, Down1 = (RowIndex + 1) * PixelLength;
            const Corner = (Across, Down) => [Origin[0] + Right[0] * Across - Up[0] * Down, Origin[1] + Right[1] * Across - Up[1] * Down];
            const A = Corner(Across0, Down0), B = Corner(Across1, Down1);
            Paint(A[0], A[1], B[0], B[1], Family);
        });
        Cursor += 5 * PixelWidth + Gap;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 DRILLS AND ZONES
//------------------------------------------------------------------------------------------------------------------------

// 📝 The training drills TrainingSequence runs. A gate is a line the car's centre crosses heading +y (or +x for
//    East): { Axis ("x" | "y"), At, From, To }. Teleport places the car at the drill's start.
export const TrainingDrills = Object.freeze([
    { Id: "launch", Name: "Launch & brake", Glyph: "Gauge", Note: "62 m sprint · trap · stop in the box",
      Teleport: { Position: [76.0, -54.5], Yaw: 90.0 },
      Start: { Axis: "y", At: -50.0, From: 67.0, To: 85.0 }, SixtyFoot: -31.71, Brake: 12.0, StopBox: [34.0, 44.0],
      Lanes: [70.0, 76.0, 82.0], LaneHalf: 2.2, Groups: ["launch"] },
    { Id: "slalom", Name: "Slalom", Glyph: "Waves", Note: "8 cones, 11 m · +1 s per cone",
      Teleport: { Position: [100.0, -53.0], Yaw: 90.0 },
      Start: { Axis: "y", At: -42.0, From: 93.0, To: 107.0 }, Finish: { Axis: "y", At: 55.0, From: 93.0, To: 107.0 },
      Groups: ["slalom"] },
    { Id: "moose", Name: "Moose test", Glyph: "Route", Note: "ISO 3888-2 double lane change",
      Teleport: { Position: [120.0, -58.0], Yaw: 90.0 },
      Start: { Axis: "y", At: -42.0, From: 118.0, To: 122.0 }, Finish: { Axis: "y", At: 19.0, From: 116.0, To: 126.0 },
      Groups: ["moose"] },
    { Id: "bowling", Name: "Cone bowling", Glyph: "Target", Note: "15 pins · strike them all",
      Teleport: { Position: [119.0, 25.0], Yaw: 90.0 },
      Start: { Axis: "y", At: 40.0, From: 114.5, To: 123.5 }, Groups: ["bowling"] },
    { Id: "spin", Name: "Spin box", Glyph: "RotateCcw", Note: "360s inside the box, cones untouched",
      Teleport: { Position: [142.0, 21.0], Yaw: 90.0 }, Box: SpinBox, Groups: ["spin"] },
    { Id: "drift", Name: "Figure-8 drift", Glyph: "Wind", Note: "Angle × speed × time, chained",
      Teleport: { Position: [131.0, -32.0], Yaw: 0.0 }, Zone: { Minimum: [126.0, -60.0], Maximum: [158.0, -6.0] }, Groups: ["drift"] },
    { Id: "skidpad", Name: "Skidpad", Glyph: "CircleDot", Note: "Hold the 14 m ring",
      Teleport: { Position: [-30.0, 18.5], Yaw: 0.0 }, Zone: { Minimum: [-47.0, 13.0], Maximum: [-13.0, 47.0] }, Groups: ["skidpad"] },
    { Id: "jump", Name: "Jump", Glyph: "TrendingUp", Note: "Airtime and distance past the lip",
      Teleport: { Position: [-20.0, 0.0], Yaw: 0.0 }, Lip: 28.0, Groups: ["jump"] },
    { Id: "smash", Name: "Smash alley", Glyph: "Hammer", Note: "13 props · impact energy",
      Teleport: { Position: [-16.0, -27.0], Yaw: 0.0 }, Zone: { Minimum: [-12.0, -34.4], Maximum: [54.0, -19.6] }, Groups: [] },
    { Id: "burnout", Name: "Burnout box", Glyph: "Flame", Note: "Hold W + S",
      Teleport: { Position: [-41.0, -21.0], Yaw: -90.0 }, Zone: { Minimum: [-49.0, -29.0], Maximum: [-33.0, -19.0] }, Groups: ["burnout"] }
]);

// 📝 Places R can't reach: the editor drops the car at the start of each feature.
export const ArenaZones = Object.freeze([
    { Name: "Start grid",  Position: [-40.0, 0.0],   Yaw: 0.0,   Note: "Main straight, ramp at 22 m" },
    ...TrainingDrills.map((Drill) => ({ Name: Drill.Name, Position: Drill.Teleport.Position, Yaw: Drill.Teleport.Yaw, Note: Drill.Note })),
    { Name: "Hangar",      Position: [34.0, 20.0],   Yaw: 90.0,  Note: "Bounce light, sky slats" },
    { Name: "Tunnel",      Position: [-36.0, -48.0], Yaw: 0.0,   Note: "Lamps in the dark" }
]);

//------------------------------------------------------------------------------------------------------------------------
//                                                    CLEAR ZONES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Rectangles [x₀, y₀, x₁, y₁] the stress-test scatter keeps clear: the drivable features and every structure.
export const LevelClearZones = Object.freeze([
    [-60.0, -9.0, 60.0, 9.0],      // main straight, kerbs, ramp landing, gravel
    [-47.0, 13.0, -13.0, 50.0],    // skidpad
    [16.0, 22.0, 54.0, 50.0],      // hangar and forecourt
    [-62.0, 50.0, 26.0, 60.0],     // container yard
    [50.0, 48.0, 56.0, 56.0],
    [-12.0, -40.0, 54.0, -17.0],   // smash alley
    [-54.0, -35.0, -26.0, -14.0],  // burnout box
    [-38.0, -56.0, 12.0, -40.0],   // tunnel and approaches
    [-60.0, -16.0, -47.0, -6.0],   // control tower
    [56.0, -9.0, 62.0, 9.0],       // run-off tyre wall
    [60.0, 22.0, 63.0, 38.0],      // arena sign
    [62.0, -61.0, 157.0, 61.0]     // the training arena
]);

export { SpawnPoint };
