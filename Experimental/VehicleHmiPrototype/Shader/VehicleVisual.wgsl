// Rigid authored tyres, damage visibility and cabin articulation; shared by all raster paths.
struct VehicleRecord {
    Cabin : vec4f, // steering angle, see-through glass, reserved, lamp
    Column : vec4f,
    Axis : vec4f,
    CabinFrame : vec4f,
    CabinShift : vec4f, // tablet centre xyz, tablet-only scale w
    Wheels : array<vec4f, 4>, // x = shredded; yzw reserved
}
struct DeformedVertex { Position : vec3f, Normal : vec3f, }
fn ResolveWheelVertex(Wheel: u32, Family: u32, Position: vec3f, Normal: vec3f) -> DeformedVertex {
    var Result: DeformedVertex;
    Result.Position = Position;
    Result.Normal = Normal;
    // All vertices of a shredded rubber facet collapse to the axle: no rubber or rubber shadow.
    // Rim/hub/brake facets and all occupant steering/spin/suspension transforms stay intact.
    if (Family == TyreFamily && VehicleState.Wheels[Wheel].x > 0.5) { Result.Position = vec3f(0.0); }
    return Result;
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

fn PlaceTablet(Position: vec3f) -> vec3f {
    return VehicleState.CabinShift.xyz + (Position - VehicleState.CabinShift.xyz) * VehicleState.CabinShift.w;
}
// Only the steering assembly articulates. HUD sizing never moves physical cabin geometry.
fn DeformBodyVertex(Family: u32, Position: vec3f, Normal: vec3f) -> DeformedVertex
{
    var Moved: DeformedVertex;
    Moved.Position = Position;
    Moved.Normal = Normal;
    if (Family == TabletHousingFamily || Family == DisplayGlassFamily) { Moved.Position = PlaceTablet(Position); }
    if (Family == SteeringWheelFamily)
    {
        Moved = TurnSteeringWheel(Position, Normal);
    }
    return Moved;
}
