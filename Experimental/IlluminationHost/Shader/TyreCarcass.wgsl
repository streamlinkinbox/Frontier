//============================================================================================================================================
//                                                            TYRECARCASS.WGSL
//============================================================================================================================================
// 🧩 The soft tyre and the cabin's moving parts in the vertex stage: belt deflection, bulge, crease, shift; the steering wheel; the cockpit.

// 📝 TyreCarcassSolver.js integrates a 96-node belt per wheel in the wheel's own frame. Here a tyre vertex, hub-local
//    with the axle along +Y, finds its belt angle φ in the x–z plane and moves inward by the belt's deflection there
//    times the share of the sidewall height it sits at — the bead stays on the rim (share 0), the tread takes all of
//    it (share 1), and every rim vertex, being inside the rim radius, is left alone. The sidewalls bow out of both
//    faces as an inextensible wall shortened by the deflection does, √(3·h·w/8), less while the air still carries
//    the load, and no further than the 0.36·h such an arc can reach; the bulge is local to the squashed belt, so a
//    flat tyre bellies out at the road and nowhere else. The belt shifts sideways round the patch under cornering.
//    A shredded tyre's rubber collapses onto the rim barrel — the strip of it a rim keeps.
//
//    The normal is the deformed surface's own: the vertex and two points 8 mm along its tangent plane (about the
//    mesh's own spacing, so a fold finer than the facets is not resolved into a normal they cannot carry) are
//    displaced alike and the displaced tangents crossed, so the flattened patch faces the road, a bulging sidewall
//    lights as it bends and an undeformed tyre keeps the archive's smoothed normal exactly.
//
//    Cabin.z exaggerates the deformation for inspection (the inspector's "Deformation ×") without lifting the tyre
//    off the road or sinking it in: the patch stays where the road puts it (the floor w ≥ R − d / cos θ under the
//    hub, Contact.x = d) and only what the road does not fix — the roll-in and roll-out, the standing wave, the
//    bulge, the belt's lateral shift — grows.
//
//    The same record carries the steering wheel's angle and column (the cabin's facets of that family turn about it),
//    whether the glazing is see-through, and the cockpit's own scale and offset (CabinStructure.js PlaceCockpitPoint).
//    Every including shader declares
//
//        var<storage, read> VehicleState : VehicleRecord;

struct TyreCarcass
{
    Shape      : vec4f,               // x tread radius [m], y rim radius [m], z half width [m], w shredded (1) or not (0)
    Pose       : vec4f,               // x road angle in the wheel frame [rad], y lateral belt shift [m], z its angular spread [rad],
                                      // w sidewall bulge gain [-] (1 empty, about ¼ at the reference pressure)
    Contact    : vec4f,               // x hub height over the road [m] (0 airborne), y z unused, w pressure over reference [-]
    Deflection : array<vec4f, 24>,    // [m] radial belt deflection per node, inward positive
}

struct VehicleRecord
{
    Cabin      : vec4f,               // x steering-wheel angle [rad], y glazing see-through (1), z deformation exaggeration [×], w lamp
    Column     : vec4f,               // xyz steering-column pivot, car-body frame [m]; w ScanX [m]
    Axis       : vec4f,               // xyz steering-column axis, car-body frame, toward the driver; w HoloAmount [0..1]
    CabinFrame : vec4f,               // xyz the cockpit's scaling anchor, car-body frame [m]; w its scale [×]
    CabinShift : vec4f,               // xyz the cockpit's offset after scaling, car-body frame [m]; w WireframeWeight [0..1]
    Carcasses  : array<TyreCarcass, 4>,
}

struct DeformedVertex
{
    Position : vec3f,
    Normal   : vec3f,
}

const CarcassNodeCount : i32 = 96;

fn ReadCarcassNode(Wheel: u32, Node: i32) -> f32
{
    let Wrapped = u32(((Node % CarcassNodeCount) + CarcassNodeCount) % CarcassNodeCount);
    return VehicleState.Carcasses[Wheel].Deflection[Wrapped / 4u][Wrapped % 4u];
}

// 📐 Catmull–Rom through the four nodes about φ: the belt's deflection there.
fn SampleCarcass(Wheel: u32, φ: f32) -> f32
{
    let U = fract(φ / 6.28318530718) * f32(CarcassNodeCount);
    let Base = i32(floor(U));
    let T = U - floor(U);
    let P0 = ReadCarcassNode(Wheel, Base - 1);
    let P1 = ReadCarcassNode(Wheel, Base);
    let P2 = ReadCarcassNode(Wheel, Base + 1);
    let P3 = ReadCarcassNode(Wheel, Base + 2);
    let A = -P0 + 3.0 * P1 - 3.0 * P2 + P3;
    let B = 2.0 * P0 - 5.0 * P1 + 4.0 * P2 - P3;
    let C = -P0 + P2;
    return 0.5 * (2.0 * P1 + C * T + B * T * T + A * T * T * T);
}

// 📐 Where the carcass puts one hub-local point of an intact tyre (axle along +Y).
fn DisplaceTyre(Wheel: u32, Position: vec3f) -> vec3f
{
    let Shape = VehicleState.Carcasses[Wheel].Shape;
    let Radial = length(Position.xz);
    let Height = max(Shape.x - Shape.y, 1.0e-4);
    let Share = clamp((Radial - Shape.y) / Height, 0.0, 1.0);
    if (Share <= 0.0)
    {
        return Position;
    }
    let Pose = VehicleState.Carcasses[Wheel].Pose;
    let Contact = VehicleState.Carcasses[Wheel].Contact;
    let Exaggeration = max(VehicleState.Cabin.z, 1.0);
    let φ = atan2(Position.z, Position.x);
    let FromRoad = atan2(sin(φ - Pose.x), cos(φ - Pose.x));
    // 📝 The road's floor at this angle: the belt is pushed in at least this far, so the exaggerated belt is held to it
    //    — the patch keeps its contact — and only the deflection beyond it is scaled.
    var Floor = 0.0;
    if (Contact.x > 0.0 && abs(FromRoad) < 1.2)
    {
        Floor = max(Shape.x - Contact.x / cos(FromRoad), 0.0);
    }
    let Solved = SampleCarcass(Wheel, φ);
    let Belt = select(Solved * Exaggeration, Floor + max(Solved - Floor, 0.0) * Exaggeration, Floor > 0.0);
    // 📐 The sidewall of height h shortened by w bows out by about √(3·h·w/8) (an inextensible arc's sagitta) — the
    //    arc's own reach saturates near 0.36·h however far it is squashed — times the share the air does not carry.
    let Depth = Pose.w * min(sqrt(0.375 * Height * max(Belt, 0.0)), 0.36 * Height);
    let Profile = sin(3.14159265359 * Share);
    let Side = clamp(Position.y / max(Shape.z, 1.0e-3), -1.0, 1.0);
    let Shift = Pose.y * Exaggeration * exp(-(FromRoad * FromRoad) / max(Pose.z * Pose.z, 1.0e-4)) * Share;
    let Reach = max(Radial - Belt * Share, Shape.y);
    return vec3f(cos(φ) * Reach, Position.y + Depth * Profile * Side + Shift, sin(φ) * Reach);
}

// 📝 Shredded: what is left of the rubber is a ragged strip hugging the rim barrel.
fn ShredTyre(Wheel: u32, Position: vec3f, Normal: vec3f) -> DeformedVertex
{
    let Shape = VehicleState.Carcasses[Wheel].Shape;
    let Share = clamp((length(Position.xz) - Shape.y) / max(Shape.x - Shape.y, 1.0e-4), 0.0, 1.0);
    let φ = atan2(Position.z, Position.x);
    let Ragged = 0.004 * sin(23.0 * φ + 1.7) + 0.003 * sin(41.0 * φ);
    let Remnant = Shape.y + 0.008 + max(Ragged, -0.006) * Share;
    var Deformed: DeformedVertex;
    Deformed.Position = vec3f(cos(φ) * Remnant, clamp(Position.y, -0.13, 0.13), sin(φ) * Remnant);
    Deformed.Normal = normalize(mix(vec3f(cos(φ), 0.0, sin(φ)), Normal, 0.35));
    return Deformed;
}

// 📝 Position only, for the shadow pass.
fn DeformTyrePosition(Wheel: u32, Position: vec3f) -> vec3f
{
    let Shape = VehicleState.Carcasses[Wheel].Shape;
    if (length(Position.xz) <= Shape.y)
    {
        return Position;
    }
    if (Shape.w > 0.5)
    {
        return ShredTyre(Wheel, Position, vec3f(1.0, 0.0, 0.0)).Position;
    }
    return DisplaceTyre(Wheel, Position);
}

fn DeformTyre(Wheel: u32, Position: vec3f, Normal: vec3f) -> DeformedVertex
{
    var Deformed: DeformedVertex;
    Deformed.Position = Position;
    Deformed.Normal = Normal;
    let Shape = VehicleState.Carcasses[Wheel].Shape;
    if (length(Position.xz) <= Shape.y)
    {
        return Deformed;   // the rim, the hub and the bead seat are rigid
    }
    if (Shape.w > 0.5)
    {
        return ShredTyre(Wheel, Position, Normal);
    }
    // 📐 Finite differences along the tangent plane: T1 × T2 = N, so the crossed displaced tangents keep the
    //    surface's orientation, and undeformed they return N itself.
    let Helper = select(vec3f(0.0, 1.0, 0.0), vec3f(1.0, 0.0, 0.0), abs(Normal.y) > 0.9);
    let T1 = normalize(cross(Helper, Normal));
    let T2 = cross(Normal, T1);
    let Step = 0.008;
    let P0 = DisplaceTyre(Wheel, Position);
    let P1 = DisplaceTyre(Wheel, Position + T1 * Step);
    let P2 = DisplaceTyre(Wheel, Position + T2 * Step);
    let Turned = cross(P1 - P0, P2 - P0);
    Deformed.Position = P0;
    Deformed.Normal = select(Normal, normalize(Turned), dot(Turned, Turned) > 1.0e-16);
    return Deformed;
}

// 📐 Rodrigues: Vector turned by Angle about the unit Axis.
fn TurnAboutAxis(Vector: vec3f, Axis: vec3f, Angle: f32) -> vec3f
{
    let C = cos(Angle);
    let S = sin(Angle);
    return Vector * C + cross(Axis, Vector) * S + Axis * dot(Axis, Vector) * (1.0 - C);
}

// 📝 A steering-wheel facet (car-body frame) turned about the column by the wheel's angle.
fn TurnSteeringWheel(Position: vec3f, Normal: vec3f) -> DeformedVertex
{
    var Turned: DeformedVertex;
    let Axis = normalize(VehicleState.Axis.xyz);
    let Pivot = VehicleState.Column.xyz;
    Turned.Position = Pivot + TurnAboutAxis(Position - Pivot, Axis, VehicleState.Cabin.x);
    Turned.Normal = TurnAboutAxis(Normal, Axis, VehicleState.Cabin.x);
    return Turned;
}

// 📝 The cockpit's fixtures — dashboard, column and wheel, binnacle, tablet, seats (CabinLeather … DisplayGlass and
//    SeatFabric) — scale about the anchor and shift with it; the shell, its glazing, the mirrors and the carpet stay.
fn IsCockpitFamily(Family: u32) -> bool
{
    return (Family >= CockpitFamilyFirst && Family <= CockpitFamilyLast) || Family == SeatFabricFamily;
}

fn PlaceCockpit(Position: vec3f) -> vec3f
{
    let Anchor = VehicleState.CabinFrame.xyz;
    return Anchor + (Position - Anchor) * VehicleState.CabinFrame.w + VehicleState.CabinShift.xyz;
}

// 📝 A body vertex's own motion (car-body frame): the steering wheel turns about its column, then the cockpit's
//    fixtures take its scale and offset. A uniform scale leaves normals as they are.
fn DeformBodyVertex(Family: u32, Position: vec3f, Normal: vec3f) -> DeformedVertex
{
    var Moved: DeformedVertex;
    Moved.Position = Position;
    Moved.Normal = Normal;
    if (Family == SteeringWheelFamily)
    {
        Moved = TurnSteeringWheel(Position, Normal);
    }
    if (IsCockpitFamily(Family))
    {
        Moved.Position = PlaceCockpit(Moved.Position);
    }
    return Moved;
}
