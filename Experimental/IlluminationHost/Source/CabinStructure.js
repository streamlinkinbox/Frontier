//============================================================================================================================================
//                                                            CABINSTRUCTURE.JS
//============================================================================================================================================
// 🧩 The ControlVehicle's cabin: dashboard, steering wheel and column, cluster binnacle, centre-stack tablet, seats and three mirrors.

// 📝 The authored shell is an empty tub — floor, walls, glazing — with no dashboard, steering or screen sockets. The
//    placements follow Project-Drive's DriveCabinLayout.h (estimated from the shell: cowl X +1.335 Z +0.735,
//    windscreen top X +0.529 Z +1.322, roof Z +1.35; left-hand drive, the driver 0.42 m left of the centreline, the
//    eye at X −0.20 Z +1.12) with the dashboard those screens need built around them:
//
//    | Fixture           | Where (model frame)                  | What                                                   |
//    |-------------------|--------------------------------------|--------------------------------------------------------|
//    | dashboard         | X 0.885 … 1.36, across ±0.95         | an extruded profile tucked under the cowl              |
//    | steering wheel    | centre X 0.56 Y +0.42 Z 0.78, r 0.18 | rim, three spokes, hub — turns about its column        |
//    | cluster           | X 0.90 Y +0.42 Z 0.835, 0.26 × 0.145 | the TabletPanel hexagon, on the column's binnacle,     |
//    |                   |                                      | read through the wheel's upper opening                 |
//    | tablet            | X 0.84 Y 0 Z 0.60, 0.304 × 0.19      | the TabletPanel launcher (16:10) on the centre stack   |
//    | mirrors           | doors at Y ±1.33, header at X 0.62   | glass aimed so the driver's eye sees down the flanks   |
//
//    Everything is emitted in the car-body (centre-of-mass) frame — the model frame less the 0.11285 m rebase — and
//    appended to the body's facet set, so the cabin moves, casts shadows, derezzes and enters the distance field and
//    cards with the car. Only the steering wheel moves on its own: its facets carry the SteeringWheel family and the
//    vertex stage turns them about the column (TyreCarcass.wgsl TurnSteeringWheel).

import { CourseMaterial } from "./MaterialSpecification.js";
import { ResolveCoMAuthoredZ } from "./VehicleSpecification.js";

const Rebase = ResolveCoMAuthoredZ();

// 📝 Model frame → car-body frame.
function Body(X, Y, Z)
{
    return [X, Y, Z - Rebase];
}

function Add(A, B) { return [A[0] + B[0], A[1] + B[1], A[2] + B[2]]; }
function Subtract(A, B) { return [A[0] - B[0], A[1] - B[1], A[2] - B[2]]; }
function Scale(A, S) { return [A[0] * S, A[1] * S, A[2] * S]; }
function Dot(A, B) { return A[0] * B[0] + A[1] * B[1] + A[2] * B[2]; }
function Cross(A, B) { return [A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0]]; }
function Normalize(A) { const L = Math.hypot(A[0], A[1], A[2]) || 1.0; return [A[0] / L, A[1] / L, A[2] / L]; }
function Combine(Origin, Axes, Weights) { return Add(Origin, Add(Add(Scale(Axes[0], Weights[0]), Scale(Axes[1], Weights[1])), Scale(Axes[2], Weights[2]))); }

// 📝 A triangle whose geometric normal is turned to agree with Expected (closed solids come out wound outward).
function Triangle(Facets, A, B, C, Family, Expected = null, Normals = null)
{
    let Corners = [A, B, C], Corner = Normals;
    if (Expected)
    {
        const Normal = Cross(Subtract(B, A), Subtract(C, A));
        if (Dot(Normal, Expected) < 0.0)
        {
            Corners = [A, C, B];
            if (Normals)
                Corner = [Normals[0], Normals[2], Normals[1]];
        }
    }
    Facets.push(Corner ? { Corners, Normals: Corner, Family } : { Corners, Family });
}

function Quad(Facets, A, B, C, D, Family, Expected = null)
{
    Triangle(Facets, A, B, C, Family, Expected);
    Triangle(Facets, A, C, D, Family, Expected);
}

// 📝 An oriented box: centre, half extents along three orthonormal axes.
function Box(Facets, Centre, Half, Axes, Family)
{
    for (let Axis = 0; Axis < 3; ++Axis)
    {
        for (const Sign of [-1, 1])
        {
            const U = (Axis + 1) % 3, V = (Axis + 2) % 3;
            const Corner = (Su, Sv) =>
            {
                const Weights = [0, 0, 0];
                Weights[Axis] = Sign * Half[Axis];
                Weights[U] = Su * Half[U];
                Weights[V] = Sv * Half[V];
                return Combine(Centre, Axes, Weights);
            };
            Quad(Facets, Corner(-1, -1), Corner(1, -1), Corner(1, 1), Corner(-1, 1), Family, Scale(Axes[Axis], Sign));
        }
    }
}

// 📐 Ear clipping of a simple polygon given as 2D points (counter-clockwise); returns index triples.
function TriangulatePolygon(Points)
{
    const Indices = Points.map((_, Index) => Index);
    const Triangles = [];
    const Area = (A, B, C) => (B[0] - A[0]) * (C[1] - A[1]) - (C[0] - A[0]) * (B[1] - A[1]);
    const Inside = (P, A, B, C) => Area(A, B, P) >= -1.0e-12 && Area(B, C, P) >= -1.0e-12 && Area(C, A, P) >= -1.0e-12;
    let Guard = 0;
    while (Indices.length > 3 && Guard++ < 1000)
    {
        let Clipped = false;
        for (let Index = 0; Index < Indices.length; ++Index)
        {
            const I0 = Indices[(Index + Indices.length - 1) % Indices.length], I1 = Indices[Index], I2 = Indices[(Index + 1) % Indices.length];
            const A = Points[I0], B = Points[I1], C = Points[I2];
            if (Area(A, B, C) <= 1.0e-12)
                continue;
            if (Indices.some((Other) => Other !== I0 && Other !== I1 && Other !== I2 && Inside(Points[Other], A, B, C)))
                continue;
            Triangles.push([I0, I1, I2]);
            Indices.splice(Index, 1);
            Clipped = true;
            break;
        }
        if (!Clipped)
            break;
    }
    if (Indices.length === 3)
        Triangles.push([Indices[0], Indices[1], Indices[2]]);
    return Triangles;
}

// 📝 A profile in the model's x–z plane extruded across Y ∈ [Left, Right] (model frame), caps and sides wound outward.
function Prism(Facets, Profile, Left, Right, Family)
{
    let Points = Profile.slice();
    let Signed = 0.0;
    for (let Index = 0; Index < Points.length; ++Index)
    {
        const A = Points[Index], B = Points[(Index + 1) % Points.length];
        Signed += A[0] * B[1] - B[0] * A[1];
    }
    if (Signed < 0.0)
        Points = Points.reverse();
    const At = (Point, Y) => Body(Point[0], Y, Point[1]);
    for (const [I0, I1, I2] of TriangulatePolygon(Points))
    {
        Triangle(Facets, At(Points[I0], Right), At(Points[I1], Right), At(Points[I2], Right), Family, [0.0, 1.0, 0.0]);
        Triangle(Facets, At(Points[I0], Left), At(Points[I1], Left), At(Points[I2], Left), Family, [0.0, -1.0, 0.0]);
    }
    for (let Index = 0; Index < Points.length; ++Index)
    {
        const A = Points[Index], B = Points[(Index + 1) % Points.length];
        const Outward = [B[1] - A[1], 0.0, -(B[0] - A[0])];
        Quad(Facets, At(A, Left), At(B, Left), At(B, Right), At(A, Right), Family, Outward);
    }
}

// 📝 A smooth tube ring (the steering wheel's rim): centre, the ring's plane axes (U, V), major and minor radii.
function Torus(Facets, Centre, U, V, Major, Minor, Family, Segments = 40, Sides = 10)
{
    const Normal = Cross(U, V);
    const Point = (Segment, Side) =>
    {
        const θ = 2.0 * Math.PI * Segment / Segments, φ = 2.0 * Math.PI * Side / Sides;
        const Radial = Add(Scale(U, Math.cos(θ)), Scale(V, Math.sin(θ)));
        const Direction = Add(Scale(Radial, Math.cos(φ)), Scale(Normal, Math.sin(φ)));
        return { Position: Add(Add(Centre, Scale(Radial, Major)), Scale(Direction, Minor)), Normal: Direction };
    };
    for (let Segment = 0; Segment < Segments; ++Segment)
    {
        for (let Side = 0; Side < Sides; ++Side)
        {
            const A = Point(Segment, Side), B = Point(Segment + 1, Side), C = Point(Segment + 1, Side + 1), D = Point(Segment, Side + 1);
            const Expected = Add(Add(A.Normal, B.Normal), Add(C.Normal, D.Normal));
            Triangle(Facets, A.Position, B.Position, C.Position, Family, Expected, [A.Normal, B.Normal, C.Normal]);
            Triangle(Facets, A.Position, C.Position, D.Position, Family, Expected, [A.Normal, C.Normal, D.Normal]);
        }
    }
}

// 📝 A capped cylinder between two points (smooth sides, flat caps).
function Cylinder(Facets, From, To, Radius, Family, Sides = 16)
{
    const Axis = Normalize(Subtract(To, From));
    const Helper = Math.abs(Axis[2]) < 0.9 ? [0.0, 0.0, 1.0] : [1.0, 0.0, 0.0];
    const U = Normalize(Cross(Axis, Helper)), V = Cross(Axis, U);
    const Ring = (Index) => { const θ = 2.0 * Math.PI * Index / Sides; return Add(Scale(U, Math.cos(θ)), Scale(V, Math.sin(θ))); };
    for (let Index = 0; Index < Sides; ++Index)
    {
        const R0 = Ring(Index), R1 = Ring(Index + 1);
        const A = Add(From, Scale(R0, Radius)), B = Add(From, Scale(R1, Radius)), C = Add(To, Scale(R1, Radius)), D = Add(To, Scale(R0, Radius));
        const Expected = Add(R0, R1);
        Triangle(Facets, A, B, C, Family, Expected, [R0, R1, R1]);
        Triangle(Facets, A, C, D, Family, Expected, [R0, R1, R0]);
        Triangle(Facets, From, B, A, Family, Scale(Axis, -1.0));
        Triangle(Facets, To, D, C, Family, Axis);
    }
}

// 📝 A screen or mirror frame: centre, the viewer's right, up, and the normal toward the viewer (Right × Up).
function Fixture(Centre, Right, Up, Width, Height, Extra = {})
{
    const Normal = Normalize(Cross(Right, Up));
    return { Centre, Right: Normalize(Right), Up: Normalize(Up), Normal, Width, Height, ...Extra };
}

// 📝 Tilt: the top of the screen leaning away from the driver (+X) by Tilt radians (DriveCabinLayout's CabinScreenBasis).
function ScreenFixture(CentreModel, Tilt, Width, Height, Extra = {})
{
    const Up = [Math.sin(Tilt), 0.0, Math.cos(Tilt)];
    return Fixture(Body(...CentreModel), [0.0, -1.0, 0.0], Up, Width, Height, Extra);
}

// 📐 A planar mirror aimed for an eye: the normal halves the eye's incoming ray and the wanted reflected ray.
function MirrorFixture(Name, CentreModel, Eye, Reflected, Width, Height)
{
    const Centre = Body(...CentreModel);
    const Incoming = Normalize(Subtract(Centre, Eye));
    const Normal = Normalize(Subtract(Normalize(Reflected), Incoming));
    const Right = Normalize(Cross([0.0, 0.0, 1.0], Normal));
    const Up = Cross(Normal, Right);
    return { Name, Centre, Right, Up, Normal, Width, Height };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     LAYOUT
//------------------------------------------------------------------------------------------------------------------------

const DriverEyeModel = [-0.20, 0.42, 1.12];
const ColumnAxis = Normalize([-0.894, 0.0, 0.447]);   // toward the driver, rising 26.6°

// 📝 Hip (H-) points on the front cushions: where a seated occupant's eye is carried from when the cockpit is sized.
export const SeatHipPoints = Object.freeze({ Driver: Body(-0.30, 0.42, 0.34), Passenger: Body(-0.30, -0.42, 0.34) });

export const CabinLayout = (() =>
{
    const Eye = Body(...DriverEyeModel);
    const WheelCentre = Body(0.56, 0.42, 0.78);
    const Layout = {
        DriverEye:    { Position: Eye, PitchDown: 0.14, FieldOfView: 70.0 },
        PassengerEye: { Position: Body(-0.22, -0.42, 1.10), PitchDown: 0.12, FieldOfView: 70.0 },
        Steering:     { Pivot: WheelCentre, Axis: ColumnAxis, Radius: 0.18, Ratio: 13.5 },
        Tablet:       ScreenFixture([0.84, 0.0, 0.60], 0.35, 0.304, 0.19, { Luminance: 3.0 }),
        Cluster:      ScreenFixture([0.90, 0.42, 0.835], 0.14, 0.26, 0.145, { Luminance: 3.5, Outline: "hexagon" }),
        Mirrors: [
            MirrorFixture("Left mirror",  [1.02, 1.33, 0.86], Eye, [-1.0, 0.06, -0.03], 0.19, 0.105),
            MirrorFixture("Right mirror", [1.02, -1.33, 0.86], Eye, [-1.0, -0.06, -0.03], 0.19, 0.105),
            MirrorFixture("Rear-view mirror", [0.62, 0.0, 1.20], Eye, [-1.0, 0.0, -0.035], 0.26, 0.075)
        ]
    };
    return Layout;
})();

// 📝 The camera rigs: the chase camera, the free camera (F8) and the interior and inspection rigs fixed to the car
//    (C cycles them). Eye and look in the car-body frame; Pitch [rad] down, Fov [°] vertical.
export const CameraRigs = Object.freeze({
    Chase:     null,
    Cockpit:   { Eye: CabinLayout.DriverEye.Position, Hip: SeatHipPoints.Driver, Pitch: CabinLayout.DriverEye.PitchDown, Fov: 70.0, Interior: true },
    Passenger: { Eye: CabinLayout.PassengerEye.Position, Hip: SeatHipPoints.Passenger, Pitch: CabinLayout.PassengerEye.PitchDown, Fov: 70.0, Interior: true },
    Hood:      { Eye: [1.62, 0.0, 0.70], Pitch: 0.05, Fov: 62.0 },
    "Tyre FL": { Eye: [2.95, 2.05, -0.12], Target: [1.7274, 1.0475, -0.38], Fov: 38.0 },
    "Tyre RL": { Eye: [-0.55, 2.15, -0.15], Target: [-1.6686, 1.0475, -0.38], Fov: 38.0 }
});

//------------------------------------------------------------------------------------------------------------------------
//                                                  COCKPIT SIZE
//------------------------------------------------------------------------------------------------------------------------

// 📝 The cockpit's own scale and placement (the inspector's Cabin · HMI "Cockpit size"). Every fixture of it — the
//    dashboard, console and selector, the column and the wheel, the binnacle and the cluster, the tablet, the seats —
//    scales about Anchor (on the cabin floor at the centreline, under the dashboard's lower edge) and then shifts by
//    Offset. The occupants stay human-sized: each eye moves with its seat's hip point but is not scaled, so from the
//    driver's camera a smaller cockpit really looks smaller (scaling the eye too would leave that view unchanged).
//    The shell, its glazing, the three mirrors and the carpet are the car's and stay put. The facets themselves never
//    change: the vertex stage places them (TyreCarcass.wgsl PlaceCockpit), so the size can be dragged live.
export const CockpitAnchor = Body(0.30, 0.0, 0.14);

export const CockpitDefaults = Object.freeze({ CockpitScale: 1.0, CockpitForward: 0.0, CockpitLateral: 0.0, CockpitHeight: 0.0 });

// 📝 Settings → { Anchor, Scale, Offset } in the car-body frame.
export function ResolveCockpitFrame(Settings = CockpitDefaults)
{
    const Scale = Math.min(2.0, Math.max(0.25, Number.isFinite(Settings.CockpitScale) ? Settings.CockpitScale : 1.0));
    const Offset = [Settings.CockpitForward || 0.0, Settings.CockpitLateral || 0.0, Settings.CockpitHeight || 0.0];
    return { Anchor: CockpitAnchor, Scale, Offset };
}

// 📐 P′ = Anchor + Scale·(P − Anchor) + Offset — the same placement the vertex stage gives the cockpit's facets.
export function PlaceCockpitPoint(Point, Frame)
{
    return Add(Add(Frame.Anchor, Scale(Subtract(Point, Frame.Anchor), Frame.Scale)), Frame.Offset);
}

// 📝 A seated eye: carried by its seat's hip point (Eye + Place(Hip) − Hip), its height above the cushion unscaled.
export function PlaceSeatedEye(Eye, Hip, Frame)
{
    return Add(Eye, Subtract(PlaceCockpitPoint(Hip, Frame), Hip));
}

// 📝 A screen fixture placed with the cockpit: its centre moved, its size scaled, its axes unchanged.
export function PlaceCockpitFixture(Fixture, Frame)
{
    return { ...Fixture, Centre: PlaceCockpitPoint(Fixture.Centre, Frame), Width: Fixture.Width * Frame.Scale, Height: Fixture.Height * Frame.Scale };
}

// 📝 The cluster's hexagon (ClusterSequence's outline), unit points over [−1, 1]².
export const ClusterHexagon = [[-0.62, -1], [0.62, -1], [1, 0], [0.62, 1], [-0.62, 1], [-1, 0]];

//------------------------------------------------------------------------------------------------------------------------
//                                                    FACETS
//------------------------------------------------------------------------------------------------------------------------

export function ConstructCabinFacets()
{
    const Facets = [];
    const F = CourseMaterial;

    // ① dashboard: one profile under the cowl, across the cabin; a lower trim band and the centre stack below it
    Prism(Facets, [[1.36, 0.70], [0.92, 0.745], [0.885, 0.73], [0.90, 0.46], [0.98, 0.30], [1.10, 0.14], [1.36, 0.14]], -0.95, 0.95, F.CabinTrim);
    Prism(Facets, [[0.93, 0.742], [0.915, 0.734], [0.908, 0.70], [0.925, 0.708]], -0.95, 0.95, F.CabinAccent);
    Box(Facets, Body(0.78, 0.0, 0.31), [0.30, 0.13, 0.14], [[1, 0, 0], [0, 1, 0], [0, 0, 1]], F.CabinTrim);       // centre stack foot
    Box(Facets, Body(0.10, 0.0, 0.28), [0.50, 0.11, 0.11], [[1, 0, 0], [0, 1, 0], [0, 0, 1]], F.CabinTrim);       // console
    Box(Facets, Body(0.12, 0.0, 0.395), [0.30, 0.095, 0.006], [[1, 0, 0], [0, 1, 0], [0, 0, 1]], F.CabinAccent);  // console top inlay
    Box(Facets, Body(0.34, 0.0, 0.46), [0.025, 0.018, 0.06], [[1, 0, 0], [0, 1, 0], [0, 0, 1]], F.CabinLeather);  // selector
    Box(Facets, Body(0.34, 0.0, 0.53), [0.035, 0.03, 0.022], [[1, 0, 0], [0, 1, 0], [0, 0, 1]], F.CabinAccent);   // selector knob

    // ② steering column and wheel: the column is static; rim, spokes and hub carry the SteeringWheel family
    const Steering = CabinLayout.Steering;
    const Pivot = Steering.Pivot;
    const Axis = Steering.Axis;
    const Up = Normalize(Cross([0.0, 1.0, 0.0], Axis));   // in the wheel's plane, toward 12 o'clock
    const Across = Cross(Up, Axis);                         // in the wheel's plane, toward 3 o'clock (the driver's right)
    const ColumnBase = Add(Pivot, Scale(Axis, -0.38));
    Cylinder(Facets, ColumnBase, Add(Pivot, Scale(Axis, -0.05)), 0.034, F.CabinTrim);
    Box(Facets, Add(ColumnBase, Scale(Axis, 0.13)), [0.075, 0.055, 0.12], [Across, Up, Axis], F.CabinTrim);       // column shroud
    Torus(Facets, Pivot, Up, Across, Steering.Radius, 0.017, F.SteeringWheel);
    Cylinder(Facets, Add(Pivot, Scale(Axis, -0.035)), Add(Pivot, Scale(Axis, 0.025)), 0.062, F.SteeringWheel, 20);  // hub
    Cylinder(Facets, Add(Pivot, Scale(Axis, 0.025)), Add(Pivot, Scale(Axis, 0.032)), 0.04, F.CabinAccent, 20);      // badge
    for (const Angle of [Math.PI * 0.5, -Math.PI * 0.5, Math.PI])
    {
        // 📝 Spokes at 9, 3 and 6 o'clock, from the hub's edge to the rim.
        const Direction = Add(Scale(Up, Math.cos(Angle)), Scale(Across, Math.sin(Angle)));
        const Middle = Add(Pivot, Scale(Direction, 0.5 * (0.06 + Steering.Radius)));
        const Side = Cross(Axis, Direction);
        Box(Facets, Add(Middle, Scale(Axis, -0.008)), [0.5 * (Steering.Radius - 0.06) + 0.006, Angle === Math.PI ? 0.016 : 0.022, 0.009],
            [Direction, Side, Axis], F.SteeringWheel);
    }

    // ③ the cluster binnacle on the column: a hexagonal housing, the cover glass and a hood
    const Cluster = CabinLayout.Cluster;
    const Hexagon = ClusterHexagon.map(([X, Y]) => Add(Add(Cluster.Centre, Scale(Cluster.Right, X * Cluster.Width * 0.5)), Scale(Cluster.Up, Y * Cluster.Height * 0.5)));
    const Grown = ClusterHexagon.map(([X, Y]) => Add(Add(Cluster.Centre, Scale(Cluster.Right, X * (Cluster.Width * 0.5 + 0.012))),
                                                     Scale(Cluster.Up, Y * (Cluster.Height * 0.5 + 0.012))));
    const Back = Scale(Cluster.Normal, -0.055), Front = Scale(Cluster.Normal, -0.0006);
    for (let Index = 1; Index < 5; ++Index)
    {
        Triangle(Facets, Hexagon[0], Hexagon[Index], Hexagon[Index + 1], F.DisplayGlass, Cluster.Normal);
        Triangle(Facets, Add(Grown[0], Back), Add(Grown[Index], Back), Add(Grown[Index + 1], Back), F.CabinTrim, Scale(Cluster.Normal, -1.0));
    }
    for (let Index = 0; Index < 6; ++Index)
    {
        const A = Grown[Index], B = Grown[(Index + 1) % 6];
        const Outward = Normalize(Subtract(Add(A, B), Scale(Cluster.Centre, 2.0)));
        Quad(Facets, Add(A, Front), Add(B, Front), Add(B, Back), Add(A, Back), F.CabinTrim, Outward);
        // 📝 The bezel ring between the glass and the housing's rim.
        const H0 = Hexagon[Index], H1 = Hexagon[(Index + 1) % 6];
        Quad(Facets, Add(H0, Front), Add(H1, Front), Add(B, Front), Add(A, Front), F.CabinTrim, Cluster.Normal);
    }
    const HoodCentre = Add(Add(Cluster.Centre, Scale(Cluster.Up, Cluster.Height * 0.5 + 0.022)), Scale(Cluster.Normal, -0.01));
    Box(Facets, HoodCentre, [Cluster.Width * 0.5 + 0.03, 0.008, 0.06], [Cluster.Right, Cluster.Up, Cluster.Normal], F.CabinTrim);
    Box(Facets, Add(Cluster.Centre, Add(Scale(Cluster.Normal, -0.05), Scale(Cluster.Up, -Cluster.Height * 0.5 - 0.02))),
        [0.06, 0.03, 0.05], [Cluster.Right, Cluster.Up, Cluster.Normal], F.CabinTrim);                                 // stalk onto the column

    // ④ the tablet on the centre stack: housing (12 mm console, bezel margin) and its cover glass
    const Tablet = CabinLayout.Tablet;
    Box(Facets, Add(Tablet.Centre, Scale(Tablet.Normal, -0.0065)), [Tablet.Width * 0.5 + 0.012, Tablet.Height * 0.5 + 0.012, 0.006],
        [Tablet.Right, Tablet.Up, Tablet.Normal], F.CabinTrim);
    const TabletCorner = (X, Y) => Add(Add(Tablet.Centre, Scale(Tablet.Right, X * Tablet.Width * 0.5)), Scale(Tablet.Up, Y * Tablet.Height * 0.5));
    Quad(Facets, TabletCorner(-1, -1), TabletCorner(1, -1), TabletCorner(1, 1), TabletCorner(-1, 1), F.DisplayGlass, Tablet.Normal);
    Box(Facets, Add(Tablet.Centre, Add(Scale(Tablet.Normal, -0.03), Scale(Tablet.Up, -0.02))), [0.05, 0.04, 0.025],
        [Tablet.Right, Tablet.Up, Tablet.Normal], F.CabinAccent);                                                       // mount

    // ⑤ seats: two front buckets and a rear bench
    for (const Lateral of [0.42, -0.42])
    {
        const Recline = 0.32;
        const BackUp = [-Math.sin(Recline), 0.0, Math.cos(Recline)], BackOut = [Math.cos(Recline), 0.0, Math.sin(Recline)];
        Box(Facets, Body(-0.18, Lateral, 0.27), [0.27, 0.25, 0.07], [[1, 0, 0], [0, 1, 0], [0, 0, 1]], F.SeatFabric);
        Box(Facets, Body(-0.18, Lateral + 0.21, 0.33), [0.26, 0.05, 0.05], [[1, 0, 0], [0, 1, 0], [0, 0, 1]], F.CabinLeather);
        Box(Facets, Body(-0.18, Lateral - 0.21, 0.33), [0.26, 0.05, 0.05], [[1, 0, 0], [0, 1, 0], [0, 0, 1]], F.CabinLeather);
        Box(Facets, Add(Body(-0.54, Lateral, 0.62), [0, 0, 0]), [0.06, 0.25, 0.33], [BackOut, [0, 1, 0], BackUp], F.SeatFabric);
        Box(Facets, Add(Body(-0.52, Lateral + 0.21, 0.62), [0, 0, 0]), [0.08, 0.045, 0.30], [BackOut, [0, 1, 0], BackUp], F.CabinLeather);
        Box(Facets, Add(Body(-0.52, Lateral - 0.21, 0.62), [0, 0, 0]), [0.08, 0.045, 0.30], [BackOut, [0, 1, 0], BackUp], F.CabinLeather);
        Box(Facets, Body(-0.68, Lateral, 1.08), [0.05, 0.13, 0.09], [BackOut, [0, 1, 0], BackUp], F.CabinLeather);    // headrest
    }
    Box(Facets, Body(-1.30, 0.0, 0.27), [0.25, 0.72, 0.08], [[1, 0, 0], [0, 1, 0], [0, 0, 1]], F.SeatFabric);
    Box(Facets, Body(-1.62, 0.0, 0.60), [0.07, 0.72, 0.30], [[Math.cos(0.35), 0, Math.sin(0.35)], [0, 1, 0], [-Math.sin(0.35), 0, Math.cos(0.35)]], F.SeatFabric);
    Box(Facets, Body(0.0, 0.0, 0.055), [1.2, 0.92, 0.012], [[1, 0, 0], [0, 1, 0], [0, 0, 1]], F.CabinCarpet);

    // ⑥ mirrors: door housings on short arms, the header mount of the rear-view mirror, silvered glass
    for (const Mirror of CabinLayout.Mirrors)
    {
        const Header = Mirror.Name.startsWith("Rear");
        const Depth = Header ? 0.018 : 0.05;
        const Housing = Add(Mirror.Centre, Scale(Mirror.Normal, -Depth - 0.0015));
        Box(Facets, Housing, [Mirror.Width * 0.5 + 0.012, Mirror.Height * 0.5 + 0.012, Depth], [Mirror.Right, Mirror.Up, Mirror.Normal],
            Header ? F.MirrorTrim : F.BodyCoat);
        const Corner = (X, Y) => Add(Add(Mirror.Centre, Scale(Mirror.Right, X * Mirror.Width * 0.5)), Scale(Mirror.Up, Y * Mirror.Height * 0.5));
        Quad(Facets, Corner(-1, -1), Corner(1, -1), Corner(1, 1), Corner(-1, 1), F.MirrorGlass, Mirror.Normal);
        if (Header)
            Cylinder(Facets, Add(Mirror.Centre, Scale(Mirror.Normal, -0.03)), Add(Mirror.Centre, [0.03, 0.0, 0.11]), 0.012, F.MirrorTrim, 10);
        else
        {
            const Side = Math.sign(Mirror.Centre[1]);
            Box(Facets, Add(Mirror.Centre, [0.035, -Side * 0.12, -0.05]), [0.06, 0.07, 0.022], [[1, 0, 0], [0, 1, 0], [0, 0, 1]], F.BodyCoat);
        }
    }
    return Facets;
}

// 📝 The glazing of the authored body (VehicleGlass facets), body frame, for the cabin overlay to draw see-through.
export function ExtractGlassFacets(BodyFacets)
{
    return BodyFacets.filter((Facet) => Facet.Family === CourseMaterial.VehicleGlass);
}

export { Body as ProjectModelPoint };
