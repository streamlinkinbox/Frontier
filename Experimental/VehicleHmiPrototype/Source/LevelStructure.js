//============================================================================================================================================
//                                                            LEVELSTRUCTURE.JS
//============================================================================================================================================
// 🧩 The Frontier proving grounds: shapes, placements, breakable props and the painted ground — declared here, built by FacetStructure.

// 📝 The layout (metres; +X east down the main straight, +Y left/north, +Z up). One large flat tarmac pad,
//    216 x 120 m (x -60..156, y -60..60) in a grass surround, ringed by a concrete boundary barrier so the
//    car cannot leave the grounds. No timed drills or exercises - a free training ground:
//
//        y +60  boundary barrier ------------------------------------------------------------------
//              |  perimeter loop (painted)     GARAGE / hangar       parking bays    east apron:   |
//        y  +6 |= kerb = START > FRONTIER = main straight = junction (traffic light) =   slalom,     |
//              |= kerb =                                                  breakable lane: crates,    |
//              |   cones - SLOW       lit TUNNEL (south edge, traffic light at portal)   panes       |
//        y -60  boundary barrier ------------------------------------------------------------------
//            x -60                                          x +60                                  x +156
//
//    Free-play furniture only: knockable cones, breakable props, lamps, traffic lights, tyre/barrier
//    protection on the fast corners. T (or the Level page) rezzes cones and props back; R returns the
//    car to the start grid. Pad size is unchanged - it is baked into the ground lattice, card and
//    tyre-mark constants (FacetStructure.GroundLayout).
//
import { CourseMaterial } from "./MaterialSpecification.js";
import { CourseConstants, KerbConstants, SpawnPoint, EmitQuad, EmitBoxFacets, EmitPanelFacets, EmitPrismFacets,
         EmitExtrusionFacets, EmitConeFacets, EmitRampFacets, EmitTyreBarrierFacets, EmitCornerBoxFacets } from "./CourseStructure.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                      SHAPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Jersey barrier cross-section (y, z), counter-clockwise: 60 cm foot, 16 cm crown, 80 cm tall.
const JerseyProfile = [[-0.30, 0.0], [0.30, 0.0], [0.30, 0.08], [0.12, 0.30], [0.08, 0.80], [-0.08, 0.80], [-0.12, 0.30], [-0.30, 0.08]];

// 📝 Ground signs: { Text, Position [x, y] (board centre), Yaw [deg] (0: the board runs along x, read from +-y) }.
const GroundSigns = [
    { Text: "GARAGE",   Position: [34.0, 27.6],   Yaw: 0.0 },
    { Text: "TUNNEL",   Position: [-10.0, -38.6], Yaw: 0.0 },
    { Text: "TRAINING", Position: [108.0, 54.6],  Yaw: 0.0 },
    { Text: "FRONTIER", Position: [61.6, 30.0],   Yaw: 90.0 }
];
const SignPixel = 0.16;   // [m] letter pixel on the boards

function ResolveSignTexts()
{
    return [...new Set(GroundSigns.map((Sign) => Sign.Text))];
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
    // Boundary: one long low wall per pad side (rastered only, but a collider - the vehicle hull reads the
    // obstacle boxes directly, so the car is fenced in without four huge distance volumes).
    BoundaryWall: { Emit: () => EmitBoxFacets(216.0, 0.6, 1.0, CourseMaterial.BarrierConcrete), Voxel: 0.5, Cards: 0, Distance: false },
    BoundarySide: { Emit: () => EmitBoxFacets(120.0, 0.6, 1.0, CourseMaterial.BarrierConcrete), Voxel: 0.5, Cards: 0, Distance: false },
    // Traffic light: post, head and three discs (only the lit disc is emissive; the others are dark glass).
    TrafficPost:  { Emit: () => EmitBoxFacets(0.18, 0.18, 3.4, CourseMaterial.SteelDark), Voxel: 0.1, Cards: 0, Distance: false },
    TrafficHead:  { Emit: () => EmitBoxFacets(0.35, 0.5, 1.1, CourseMaterial.SteelDark), Voxel: 0.08, Cards: 6 },
    TrafficRed:   { Emit: () => EmitBoxFacets(0.06, 0.3, 0.3, CourseMaterial.GateLampRed), Voxel: 0.04, Cards: 8 },
    TrafficAmber: { Emit: () => EmitBoxFacets(0.06, 0.3, 0.3, CourseMaterial.LampWarm), Voxel: 0.04, Cards: 8 },
    TrafficGreen: { Emit: () => EmitBoxFacets(0.06, 0.3, 0.3, CourseMaterial.GateLamp), Voxel: 0.04, Cards: 8 },
    TrafficOff:   { Emit: () => EmitBoxFacets(0.06, 0.3, 0.3, CourseMaterial.SteelDark), Voxel: 0.04, Cards: 4 },
    TyreStack:    { Emit: () => EmitPrismFacets(0.33, 1.2, 12, CourseMaterial.Tyre), Voxel: 0.06, Cards: 16 },
    Pole:         { Emit: () => EmitBoxFacets(0.3, 0.3, 11.0, CourseMaterial.SteelDark), Voxel: 0.25, Cards: 0, Distance: false },
    PoleHead:     { Emit: () => EmitBoxFacets(1.8, 0.6, 0.3, CourseMaterial.LampWarm), Voxel: 0.05, Cards: 16 },
    FramePost:    { Emit: () => EmitBoxFacets(0.12, 0.12, 2.3, CourseMaterial.SteelDark), Voxel: 0.1, Cards: 0, Distance: false },
    // Tyre barrier sections (fast corners), the start gantry and the ground signs (rastered boards).
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
                                "Tower", "Jersey", "TyreStack", "Pole", "FramePost", "TyreBarrier", "GantryPost", "SignPost",
                                "BoundaryWall", "BoundarySide", "TrafficPost", "TrafficHead"]);

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

    // ③ perimeter boundary: one wall per side, inset 1.2 m from the pad edge, so the car stays on the
    //    grounds. Tyre barriers guard the two fast east corners where the loop turns.
    Place("BoundaryWall", "Boundary north", 48.0, 58.8);
    Place("BoundaryWall", "Boundary south", 48.0, -58.8);
    Place("BoundarySide", "Boundary west", -58.8, 0.0, 0.0, 90.0 * Degrees);
    Place("BoundarySide", "Boundary east", 154.8, 0.0, 0.0, 90.0 * Degrees);
    for (let Section = 0; Section < 3; ++Section)
    {
        Place("TyreBarrier", `East corner tyres N${Section}`, 148.0, 52.0 - 4.1 * Section, 0.0, 90.0 * Degrees);
        Place("TyreBarrier", `East corner tyres S${Section}`, 148.0, -52.0 + 4.1 * Section, 0.0, 90.0 * Degrees);
    }

    // ④ tunnel along the south edge, with lamp strips on both walls (unchanged feature)
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

    // ⑤ traffic lights: one at the junction where the main straight meets the east apron, one at the
    //    tunnel's west portal. Green is lit; red/amber discs are present but dark (a static, decorative
    //    signal - cycling it would need a per-frame emissive rewrite; see README).
    const TrafficLight = (Name, X, Y, FacingDegrees) =>
    {
        const Yaw = FacingDegrees * Degrees;
        Place("TrafficPost", `${Name} post`, X, Y, 0.0, Yaw);
        Place("TrafficHead", `${Name} head`, X, Y, 3.4, Yaw);
        // discs sit on the head's west face (toward eastbound traffic approaching it)
        Place("TrafficOff",   `${Name} red`,   X - 0.19, Y, 4.22, Yaw);
        Place("TrafficOff",   `${Name} amber`, X - 0.19, Y, 3.90, Yaw);
        Place("TrafficGreen", `${Name} green`, X - 0.19, Y, 3.58, Yaw);
    };
    TrafficLight("Junction light", 62.0, -8.2, 0.0);
    TrafficLight("Tunnel light", -38.0, -42.6, 0.0);

    // ⑥ start gantry over the main straight (posts, beam, lamp strip)
    Place("GantryPost", "Start gantry post N", -34.0, 6.8);
    Place("GantryPost", "Start gantry post S", -34.0, -6.8);
    Place("GantryBeam", "Start gantry", -34.0, 0.0, 5.0, 90.0 * Degrees);
    Place("GantryLamp", "Start gantry lamps", -34.0, 0.0, 4.88, 90.0 * Degrees, CourseMaterial.GateLamp);

    // ⑦ jersey barriers: split the straight from the breakable lane, and line the tunnel approach
    for (let Barrier = 0; Barrier < 10; ++Barrier)
        Place("Jersey", `Apron barrier ${Barrier}`, 70.0 + 3.0 * Barrier, -16.0);
    for (let Barrier = 0; Barrier < 6; ++Barrier)
        Place("Jersey", `Tunnel barrier ${Barrier}`, -34.0 + 3.0 * Barrier, -40.8);

    // ⑧ ground signs on their posts
    for (const Sign of GroundSigns)
    {
        const [Width] = ResolveSignExtent(Sign.Text);
        const Yaw = Sign.Yaw * Degrees;
        const Along = [Math.cos(Yaw), Math.sin(Yaw)];
        Place(`Sign ${Sign.Text}`, `Sign ${Sign.Text}`, Sign.Position[0], Sign.Position[1], 2.3, Yaw);
        for (const Side of [-1, 1])
            Place("SignPost", `Sign ${Sign.Text} post ${Side < 0 ? "a" : "b"}`, Sign.Position[0] + Side * Along[0] * (0.5 * Width - 0.35),
                  Sign.Position[1] + Side * Along[1] * (0.5 * Width - 0.35));
    }

    // ⑨ light poles (rastered) with their lamp heads (in the field: they light the night) - along the
    //    straight, the loop and the apron, none inside a driving lane.
    const Poles = [[-45.0, 10.0, -90.0], [-15.0, 10.0, -90.0], [15.0, 10.0, -90.0], [45.0, 10.0, -90.0],
                   [-30.0, -10.0, 90.0], [0.0, -10.0, 90.0], [30.0, -10.0, 90.0], [-48.0, 30.0, 0.0],
                   [-12.0, 44.0, 180.0], [-56.0, -24.0, 0.0], [20.0, -38.0, 90.0], [8.0, 30.0, 0.0],
                   [70.0, 48.0, 180.0], [100.0, 48.0, 180.0], [130.0, 48.0, 180.0], [70.0, -48.0, 0.0],
                   [100.0, -48.0, 0.0], [130.0, -48.0, 0.0], [152.0, 0.0, -90.0], [108.0, 0.0, 90.0]];
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

// 📝 Free-play cone layouts (no drills/groups to score - Group only names the set T rezzes back together).
export function ResolveConePlacements()
{
    const List = [];
    const Cone = (Group, X, Y, Yaw = 0.0) => List.push({ Name: `Cone ${Group} ${List.filter((Item) => Item.Group === Group).length}`,
                                                          Position: [X, Y], Yaw, Group });
    // start grid pair on the main straight
    Cone("straight", -34.15, 6.7); Cone("straight", -34.15, -6.7);
    // garage entrance pair
    Cone("garage", 30.0, 28.6); Cone("garage", 38.0, 28.6);
    // free slalom line on the east apron (drive it either way, no timing)
    for (let Index = 0; Index < 8; ++Index)
        Cone("slalom", 96.0 + 7.0 * Index, 22.0);
    // a ring to circle on the apron
    for (let Index = 0; Index < 12; ++Index)
    {
        const Angle = 2.0 * Math.PI * Index / 12;
        Cone("ring", 122.0 + 10.0 * Math.cos(Angle), -22.0 + 10.0 * Math.sin(Angle), Angle);
    }
    // parking box outline, north apron
    for (const [X, Y] of [[64, 38], [70, 38], [76, 38], [64, 44], [76, 44], [64, 50], [70, 50], [76, 50]])
        Cone("parking", X, Y);
    // tunnel guide cones at both portals
    for (const X of [-40.0, 8.0]) for (const Y of [-43.4, -52.6]) Cone("tunnel", X, Y);
    return List;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 BREAKABLE PROPS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Free-play breakables: every prop is a closed convex solid with a Fracture-editor recipe.
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
    // Breakable lane on the south apron (drive in from the junction, heading east) ...
    for (const Y of [-24.1, -23.0, -21.9])
        Add(`Wooden crate ${List.length + 1}`, Crate, "wood", 74.0, Y, 0.12 * (Y + 23.0), { DensityScale: 0.32, Capacity: 32 });
    Add("Glass pane", Pane, "glass", 84.0, -23.0, 90.0 * Degrees, { Capacity: 48 });
    Add("Concrete pillar A", Pillar, "concrete", 94.0, -24.6, 0.0, { Capacity: 40 });
    Add("Concrete pillar B", Pillar, "concrete", 94.0, -21.4, 0.0, { Capacity: 40 });
    Add("Tempered pane", Tempered, "tempered", 104.0, -23.0, 90.0 * Degrees, { Capacity: 48 });
    Add("ABS barrier A", Barrier, "plastic", 114.0, -24.0, 90.0 * Degrees, { Capacity: 24 });
    Add("ABS barrier B", Barrier, "plastic", 114.0, -22.0, 90.0 * Degrees, { Capacity: 24 });
    Add("Stone wall", Wall, "rock", 124.0, -23.0, 90.0 * Degrees, { Capacity: 40 });
    // ... and a small crate cluster by the garage for low-speed nudging
    Add("Garage crate 1", Crate, "wood", 28.0, 24.0, 0.3, { DensityScale: 0.32, Capacity: 32 });
    Add("Garage crate 2", Crate, "wood", 29.2, 24.4, -0.2, { DensityScale: 0.32, Capacity: 32 });
    Add("Concrete block", Block, "concrete", 28.6, 22.6, 90.0 * Degrees, { Capacity: 40 });
    return List;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   GROUND PAINT
//------------------------------------------------------------------------------------------------------------------------

// 📝 Zones of the ground lattice (4 m cells, classified by centre): later entries win; Fresh zones never patch.
const GroundZones = [
    { Minimum: [16.0, 24.0],   Maximum: [52.0, 48.0],   Family: CourseMaterial.ApronConcrete },   // garage floor and forecourt
    { Minimum: [60.0, -58.0],  Maximum: [156.0, 58.0],  Family: CourseMaterial.Asphalt },         // east apron, fresh
    { Minimum: [60.0, -28.0],  Maximum: [132.0, -18.0], Family: CourseMaterial.ApronConcrete }    // breakable lane
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

    // 1) main straight: edge lines, centre dashes, chequered start strip, three painted grid slots
    for (const Y of [5.8, -5.8]) Paint(-52.0, Y - 0.1, 58.0, Y + 0.1, White);
    for (let X = -30.0; X < 52.0; X += 6.0) Paint(X, -0.075, X + 3.0, 0.075, White);
    for (let Square = 0; Square < 20; ++Square)
        for (let Rank = 0; Rank < 2; ++Rank)
            if ((Square + Rank) % 2 === 0)
                Paint(-34.3 + 0.3 * Rank, -5.8 + 0.58 * Square, -34.0 + 0.3 * Rank, -5.8 + 0.58 * (Square + 1), White);
    for (const SlotY of [-3.6, 0.0, 3.6]) PaintOutline(Paint, -44.0, SlotY - 1.4, -38.0, SlotY + 1.4, 0.14, Yellow);
    PaintText(Paint, "FRONTIER", [-20.0, 5.2], [1.0, 0.0], [0.0, -1.0], 0.2, 0.5, 0.33, White);
    Text("TRAINING GROUND", [-2.0, -4.6], ReadEast, 0.22, 0.4, 0.3, Yellow);

    // 2) perimeter loop: a painted rectangle inset 10 m from the boundary - edge lines + dashed centreline
    for (const Y of [50.0, -50.0]) { Paint(-50.0, Y - 0.09, 146.0, Y + 0.09, White); }
    for (const X of [-50.0, 146.0]) { Paint(X - 0.09, -50.0, X + 0.09, 50.0, White); }
    for (let X = -46.0; X < 146.0; X += 8.0) { Paint(X, 44.94, X + 4.0, 45.06, Yellow); Paint(X, -45.06, X + 4.0, -44.94, Yellow); }
    for (let Y = -42.0; Y < 44.0; Y += 8.0) { Paint(-44.06, Y, -43.94, Y + 4.0, Yellow); Paint(139.94, Y, 140.06, Y + 4.0, Yellow); }

    // 3) garage bays and the forecourt line
    for (const X of [24.0, 30.0, 36.0, 42.0, 48.0]) Paint(X - 0.08, 32.0, X + 0.08, 42.0, White);
    Paint(18.0, 29.4, 50.0, 29.6, Yellow);
    Text("GARAGE", [34.0, 26.8], ReadEast, 0.24, 0.42, 0.3, White);

    // 4) tunnel: approach lines, SLOW on the way in, TUNNEL over the portal lane
    Paint(-38.0, -44.6, 10.0, -44.4, White);
    Paint(-38.0, -51.6, 10.0, -51.4, White);
    Text("SLOW", [-30.0, -48.0], ReadEast, 0.3, 0.5, 0.4, Yellow);
    Text("TUNNEL", [-14.0, -40.8], ReadEast, 0.24, 0.42, 0.3, White);

    // 5) junction: stop line + crosswalk stripes where the straight meets the apron
    Paint(59.8, -5.8, 60.0, 5.8, White);
    for (let Stripe = 0; Stripe < 6; ++Stripe) Paint(61.0 + 0.7 * Stripe, -5.4, 61.4 + 0.7 * Stripe, 5.4, White);

    // 6) east apron: two painted rings to circle, dashed slalom lane edges, parking bays, the name
    PaintRing(Paint, Facets, 122.0, -22.0, 10.0, 0.28, 72, 1.0, White);
    PaintRing(Paint, Facets, 122.0, -22.0, 14.0, 0.2, 72, 0.5, Yellow);
    for (const Y of [18.4, 25.6]) for (let X = 92.0; X < 152.0; X += 6.0) Paint(X, Y - 0.06, X + 3.0, Y + 0.06, White);
    for (let Bay = 0; Bay < 5; ++Bay) PaintOutline(Paint, 62.0 + 7.0 * Bay, 38.0, 68.0 + 7.0 * Bay, 50.0, 0.14, White);
    Text("APRON", [108.0, 8.0], ReadEast, 0.4, 0.7, 0.55, Blue);

    // 7) breakable lane edges (south apron)
    Paint(68.0, -18.4, 132.0, -18.2, Yellow);
    Paint(68.0, -27.8, 132.0, -27.6, Yellow);
    Text("BREAKABLES", [100.0, -29.6], ReadEast, 0.2, 0.36, 0.28, Yellow);

    // 8) run-off chevrons at the east end of the straight
    for (let Hatch = 0; Hatch < 5; ++Hatch) PaintSlanted(Facets, 132.0, -4.0 + 2.0 * Hatch, 3.0, 0.35, Yellow);
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
//                                                    FREE ZONES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Teleports (no drills, no timers): the editor drops the car at each feature of the training ground.
export const LevelZones = Object.freeze([
    { Name: "Start grid", Position: [-40.0, 0.0],   Yaw: 0.0,   Note: "Main straight, facing east" },
    { Name: "Garage",     Position: [34.0, 20.0],   Yaw: 90.0,  Note: "Inside the hangar, facing out" },
    { Name: "Tunnel",     Position: [-36.0, -48.0], Yaw: 0.0,   Note: "Lit tunnel, south edge" },
    { Name: "Junction",   Position: [52.0, 0.0],    Yaw: 0.0,   Note: "Traffic light into the apron" },
    { Name: "East apron", Position: [108.0, 0.0],   Yaw: 0.0,   Note: "Slalom, rings, parking, breakables" },
    { Name: "Loop north", Position: [-20.0, 45.0],  Yaw: 0.0,   Note: "Perimeter loop, north leg" }
]);

//------------------------------------------------------------------------------------------------------------------------
//                                                    CLEAR ZONES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Rectangles [x₀, y₀, x₁, y₁] the stress-test scatter keeps clear: the drivable features and every structure.
export const LevelClearZones = Object.freeze([
    [-60.0, -9.0, 62.0, 9.0],      // main straight and junction
    [16.0, 22.0, 54.0, 50.0],      // garage and forecourt
    [-38.0, -56.0, 12.0, -40.0],   // tunnel and approaches
    [60.0, -58.0, 156.0, 58.0]     // east apron (loop, slalom, rings, breakable lane)
]);

export { SpawnPoint };
