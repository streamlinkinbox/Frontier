// Exact shared VehicleVisual deformation, then bottom-up BLAS and TLAS refit.
//! include "SceneRecords.wgsl"
//! include "VehicleVisual.wgsl"
struct RefitConfiguration {
    TriangleOffset: u32, InstanceOffset: u32, DomainOffset: u32, First: u32,
    Count: u32, Kind: u32, Pad0: u32, Pad1: u32,
}
@group(0) @binding(0) var<uniform> Refit: RefitConfiguration;
@group(0) @binding(1) var<storage, read_write> Geometry: array<u32>;
@group(0) @binding(2) var<storage, read> BaseTriangles: array<u32>;
@group(0) @binding(3) var<storage, read> Occupants: array<OccupantRecord>;
@group(0) @binding(4) var<storage, read> VehicleState: VehicleRecord;
fn ReadVector(Word: u32) -> vec3f { return bitcast<vec3f>(vec3u(Geometry[Word], Geometry[Word+1u], Geometry[Word+2u])); }
fn WriteVector(Word: u32, V: vec3f) {
    Geometry[Word] = bitcast<u32>(V.x); Geometry[Word+1u] = bitcast<u32>(V.y); Geometry[Word+2u] = bitcast<u32>(V.z);
}
fn BaseVector(Word: u32) -> vec3f { return bitcast<vec3f>(vec3u(BaseTriangles[Word], BaseTriangles[Word+1u], BaseTriangles[Word+2u])); }
@compute @workgroup_size(64)
fn DeformPathTriangles(@builtin(global_invocation_id) Id: vec3u) {
    if (Id.x >= Refit.Count) { return; }
    let Source = Id.x * 24u;
    let Word = Refit.TriangleOffset + Source;
    let Family = BaseTriangles[Source+3u];
    let Owner = BaseTriangles[Source+7u];
    for (var C = 0u; C < 3u; C++) {
        let P = BaseVector(Source+C*4u); let N = BaseVector(Source+12u+C*4u);
        var Moved = DeformedVertex(P, N);
        if (Owner == 0u) { Moved = DeformBodyVertex(Family, P, N); }
        else { Moved = ResolveWheelVertex(Owner-1u, Family, P, N); }
        WriteVector(Word+C*4u, Moved.Position); WriteVector(Word+12u+C*4u, Moved.Normal);
    }
}
@compute @workgroup_size(64)
fn RefitPathBounds(@builtin(global_invocation_id) Id: vec3u) {
    if (Id.x >= Refit.Count) { return; }
    let Index = Geometry[Refit.First+Id.x]; let Word = Index * 8u;
    let First = Geometry[Word+3u]; let Count = Geometry[Word+7u];
    var Low = vec3f(1.0e30); var High = vec3f(-1.0e30);
    if (Refit.Kind == 2u) {
        let Left = Index+1u;
        let Right = select(Geometry[Left*8u+3u], Left+1u, Geometry[Left*8u+7u] > 0u);
        Low = min(ReadVector(Left*8u), ReadVector(Right*8u));
        High = max(ReadVector(Left*8u+4u), ReadVector(Right*8u+4u));
    } else if (Refit.Kind == 0u) {
        for (var I = 0u; I < Count; I++) {
            let T = Refit.TriangleOffset + (First+I)*24u;
            for (var C = 0u; C < 3u; C++) { let P = ReadVector(T+C*4u); Low = min(Low,P); High = max(High,P); }
        }
    } else {
        for (var I = 0u; I < Count; I++) {
            let Instance = Geometry[Refit.DomainOffset+(First+I)*2u];
            let Root = Geometry[Refit.InstanceOffset+Instance*8u];
            let A = ReadVector(Root*8u); let B = ReadVector(Root*8u+4u);
            let Transform = Occupants[Instance].LocalToWorld;
            for (var C = 0u; C < 8u; C++) {
                let P = vec3f(select(A.x,B.x,(C&1u)!=0u), select(A.y,B.y,(C&2u)!=0u), select(A.z,B.z,(C&4u)!=0u));
                let W = (Transform*vec4f(P,1.0)).xyz; Low = min(Low,W); High = max(High,W);
            }
        }
    }
    // Round outward. Planar/zero-thickness triangles must retain nonempty boxes.
    let Epsilon = max(vec3f(1.0e-6), max(abs(Low),abs(High))*2.0e-7);
    WriteVector(Word,Low-Epsilon); WriteVector(Word+4u,High+Epsilon);
}
