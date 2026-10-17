//! include "FrameDescriptor.wgsl"
// Byte layouts are also asserted in PathTracing.test.mjs.
struct PathConfiguration {
    Geometry: vec4u, // triangle, instance, domain, emitter offsets, in u32 words
    Scene: vec4u,    // TLAS root/end, active instance count, emitter count
    Counts: vec4u,   // max vertices, fresh candidates, finite light count, seed
    Options: vec4f,  // RR first vertex, defensive proposal fraction, temporal alpha, wavelet iterations
    Domain: vec4u,   // total instanced triangles, geometry generation, reference flag, unused
    Display: vec4f,  // screen brightness, screens live, glass transmission, unused
    JitterDelta: vec4f, // xy current-minus-previous screen-space camera jitter, pixels
    Pad1: vec4f,
}
@group(0) @binding(1) var<uniform> Path: PathConfiguration;
const PathInvalid: u32 = 0xFFFFFFFFu;
const PathTargetFloor: f32 = 1.0e-12;
struct PathSurface {
    Position: vec3f, Depth: f32,
    Normal: vec3f, PriorDepth: f32,
    LocalPosition: vec3f, Family: u32,
    View: vec3f, Instance: u32,
    Motion: vec2f, Triangle: u32, Hit: u32,
}
// Canonical sample domain: (instance, triangle, uniform-area barycentrics, random
// suffix seed), OR an environment direction. A seed replays the ENTIRE remaining
// path at the new outgoing direction, including non-Lambertian second vertices.
struct PathReservoir {
    Triangle: u32, Instance: u32, Seed: u32, M: f32,
    Barycentric: vec2f, W: f32, Target: f32,
}
struct PathPixel {
    Surface: PathSurface, Reservoir: PathReservoir,
    Direct: vec4f, Contribution: vec4f,
}
fn PathHash(Value: u32) -> u32 {
    var X = Value; X ^= X >> 16u; X *= 0x7feb352du; X ^= X >> 15u; X *= 0x846ca68bu; return X ^ (X >> 16u);
}
fn PathRandom(State: ptr<function,u32>) -> f32 {
    (*State) = (*State)*747796405u+2891336453u;
    let Word = (((*State) >> (((*State) >> 28u)+4u)) ^ (*State))*277803737u;
    // 24 bits, strictly below 1.0 even after f32 rounding.
    return f32(((Word >> 22u)^Word) >> 8u)*(1.0/16777216.0);
}
fn PathRandom2(State: ptr<function,u32>) -> vec2f { let X = PathRandom(State); return vec2f(X,PathRandom(State)); }
fn PathUniformSphere(U: vec2f) -> vec3f {
    let Z = 1.0-2.0*U.x; let R = sqrt(max(0.0,1.0-Z*Z)); let Phi = 2.0*Pi*U.y;
    return vec3f(R*cos(Phi),R*sin(Phi),Z);
}
fn PathScreenPoint(Clip: vec4f) -> vec2f { return vec2f(Clip.x/Clip.w*0.5+0.5,0.5-Clip.y/Clip.w*0.5)*Frame.ScreenExtent; }
// A trace pixel samples screen (pixel*scale + 0.5), NOT (pixel+0.5)*scale.
// The inverse map therefore has no extra -0.5 trace-pixel offset.
fn PathPriorTracePixel(Screen: vec2u, Motion: vec2f) -> vec2f {
    return (vec2f(Screen)-Motion)/f32(Frame.TraceScale);
}
fn PathFinite(X: f32) -> bool { return abs(X) < 3.0e37; }
fn PathSafeColour(C: vec3f) -> vec3f {
    if (!all(C == C) || any(abs(C) > vec3f(3.0e37))) { return vec3f(0.0); }
    return max(C,vec3f(0.0));
}
