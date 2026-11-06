//============================================================================================================================================
//                                                            LOBBYSTRUCTURE.JS
//============================================================================================================================================
// 🧩 The Lobby: a seamless automotive photography studio for the vehicle configurator — circular cyclorama cove,
//    suspended luminous softbox, dark industrial ceiling and a polished turntable floor. Geometry only; no UI.

import { CourseMaterial } from "./MaterialSpecification.js";
import { EmitQuad, EmitBoxFacets } from "./CourseStructure.js";

// 📝 Requested by Lobby.html (its path) or by ?lobby=1 on any entry page (the headless harness passes it that way).
export function ResolveLobbyRequested()
{
    try
    {
        const Location = globalThis.location || {};
        if (new URLSearchParams(Location.search || "").get("lobby") === "1")
            return true;
        return String(Location.pathname || Location.href || "").includes("Lobby.html");
    }
    catch { return false; }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

// 📝 The cyclorama: a floor disc that sweeps, through a quarter cove, into a cylindrical wall — one seamless
//    white surface around the car, as in a photography cove. Wound so every facet normal points inward.
function EmitCycloramaFacets()
{
    const Facets = [];
    const Segments = 96;
    const Profile = [];
    // Cove: quadratic Bézier from the floor (r 9 m, z 0) through the control point at the wall base to vertical.
    for (let Step = 0; Step <= 8; ++Step)
    {
        const T = Step / 8, U = 1.0 - T;
        Profile.push([U * U * 17.0 + 2.0 * U * T * 20.0 + T * T * 20.0,
                      U * U * 0.0 + 2.0 * U * T * 0.0 + T * T * 3.4]);
    }
    Profile.push([20.0, 5.2], [20.0, 7.4]);
    const Point = (Angle, Radius, Z) => [Radius * Math.cos(Angle), Radius * Math.sin(Angle), Z];
    for (let Segment = 0; Segment < Segments; ++Segment)
    {
        const A0 = 2.0 * Math.PI * Segment / Segments, A1 = 2.0 * Math.PI * (Segment + 1) / Segments;
        for (let Step = 0; Step + 1 < Profile.length; ++Step)
        {
            const [R0, Z0] = Profile[Step], [R1, Z1] = Profile[Step + 1];
            // Inward winding: bottom at a0 → top at a0 → top at a1 → bottom at a1.
            EmitQuad(Facets, Point(A0, R0, Z0), Point(A0, R1, Z1), Point(A1, R1, Z1), Point(A1, R0, Z0), CourseMaterial.StudioWall);
        }
    }
    return Facets;
}

// 📝 The overhead softbox: a black-framed luminous panel. Only its underside emits (the clustered-light and
//    emissive-surface samplers read that family's emission); every other face is the dark housing.
function EmitSoftboxFacets()
{
    return EmitBoxFacets(24.0, 11.0, 0.22, CourseMaterial.StudioFrame,
        [CourseMaterial.StudioFrame, CourseMaterial.StudioSoftbox, CourseMaterial.StudioFrame,
         CourseMaterial.StudioFrame, CourseMaterial.StudioFrame, CourseMaterial.StudioFrame]);
}

export const LobbyShapes = Object.freeze({
    Cyclorama:  { Emit: () => EmitCycloramaFacets(), Voxel: 0.30, Cards: 9 },
    Softbox:    { Emit: () => EmitSoftboxFacets(), Voxel: 0.12, Cards: 6 },
    Ceiling:    { Emit: () => EmitBoxFacets(46.0, 46.0, 0.2, CourseMaterial.StudioFrame), Voxel: 0.35, Cards: 2 },
    TrussBeam:  { Emit: () => EmitBoxFacets(38.0, 0.28, 0.28, CourseMaterial.StudioFrame), Voxel: 0.3, Cards: 2 },
    TrussCross: { Emit: () => EmitBoxFacets(0.18, 38.0, 0.18, CourseMaterial.StudioFrame), Voxel: 0.3, Cards: 2 },
    Cable:      { Emit: () => EmitBoxFacets(0.035, 0.035, 1.6, CourseMaterial.StudioFrame), Voxel: 0.1, Cards: 0, Distance: false }
});

// 📝 The display fleet: the drivable hero at the centre, four parked cars nose-in around it — five cars in the
//    one studio, all of them lit by the same single softbox. Yaw in radians, cars facing +X at yaw 0.
export const LobbyVehiclePlacements = Object.freeze([
    Object.freeze({ Name: "Display car north-east", Position: [ 7.42,  7.42], Yaw: -2.01 }),
    Object.freeze({ Name: "Display car north-west", Position: [-7.42,  7.42], Yaw: -0.44 }),
    Object.freeze({ Name: "Display car south-west", Position: [-7.42, -7.42], Yaw:  0.44 }),
    Object.freeze({ Name: "Display car south-east", Position: [ 7.42, -7.42], Yaw:  2.01 })
]);

export function ResolveLobbyPlacements()
{
    const List = [];
    const Place = (Shape, Name, X, Y, Z = 0.0, Yaw = 0.0) =>
        List.push({ Shape, Name, Position: [X, Y, Z], Yaw, Material: -1, Collider: false });

    Place("Cyclorama", "Studio cyclorama", 0.0, 0.0, 0.0);
    Place("Ceiling", "Studio ceiling", 0.0, 0.0, 7.7);
    for (const Y of [-14.0, -7.0, 0.0, 7.0, 14.0])
        Place("TrussBeam", `Ceiling truss ${Y}`, 0.0, Y, 7.35);
    for (const X of [-14.0, 0.0, 14.0])
        Place("TrussCross", `Ceiling cross truss ${X}`, X, 0.0, 7.18);
    Place("Softbox", "Overhead softbox", 0.0, 0.0, 5.95);
    for (const X of [-10.5, 10.5])
        for (const Y of [-4.6, 4.6])
            Place("Cable", `Softbox cable ${X},${Y}`, X, Y, 6.10);
    return List;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       GROUND
//------------------------------------------------------------------------------------------------------------------------

// 📝 Dark charcoal surround, a glossy white studio floor disc under the cove, and the turntable disc with its seam.
export function EmitLobbyGroundFacets({ Guests = true } = {})
{
    const Facets = [];
    const Cell = 20.0;
    for (let Row = -3; Row < 8; ++Row)
        for (let Column = -3; Column < 3; ++Column)
        {
            const X0 = Row * Cell, Y0 = Column * Cell;
            EmitQuad(Facets, [X0, Y0, -0.02], [X0 + Cell, Y0, -0.02], [X0 + Cell, Y0 + Cell, -0.02], [X0, Y0 + Cell, -0.02],
                     CourseMaterial.StudioSurround);
        }

    const Segments = 96;
    const Disc = (CentreX, CentreY, Radius, Z, Family) =>
    {
        for (let Segment = 0; Segment < Segments; ++Segment)
        {
            const A0 = 2.0 * Math.PI * Segment / Segments, A1 = 2.0 * Math.PI * (Segment + 1) / Segments;
            Facets.push({ Corners: [[CentreX, CentreY, Z], [CentreX + Radius * Math.cos(A0), CentreY + Radius * Math.sin(A0), Z],
                                    [CentreX + Radius * Math.cos(A1), CentreY + Radius * Math.sin(A1), Z]], Family });
        }
    };
    Disc(0.0, 0.0, 17.6, 0.0, CourseMaterial.StudioFloor);

    // A turntable disc under every car, each with its hairline seam.
    const Turntables = [[0.0, 0.0, 4.65], ...(Guests ? LobbyVehiclePlacements.map((Car) => [...Car.Position, 4.30]) : [])];
    for (const [CentreX, CentreY, Radius] of Turntables)
    {
        Disc(CentreX, CentreY, Radius, 0.006, CourseMaterial.StudioTurntable);
        for (let Segment = 0; Segment < Segments; ++Segment)
        {
            const A0 = 2.0 * Math.PI * Segment / Segments, A1 = 2.0 * Math.PI * (Segment + 1) / Segments;
            const P = (Angle, R) => [CentreX + R * Math.cos(Angle), CentreY + R * Math.sin(Angle), 0.009];
            EmitQuad(Facets, P(A0, Radius), P(A0, Radius + 0.05), P(A1, Radius + 0.05), P(A1, Radius), CourseMaterial.Surround);
        }
    }
    return Facets;
}
