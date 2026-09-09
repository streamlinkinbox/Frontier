//============================================================================================================================================
// 📦 Engine/Shaders/SurfaceResolve.slang — Visibility id → primary surface (thin G-buffer) + debug views
//============================================================================================================================================
// Per pixel: read the visibility id, fetch the triangle's three world-space vertices, intersect the pixel's camera ray
//    with THAT triangle (Möller–Trumbore against a known plane — exact, no derivative bookkeeping; the ray uses the same
//    jitter the raster used, so the hit lands on the rasterised sample) and write the surface the path kernel needs.
//    The kernel then starts at step 3 (direct lighting) and never traces a primary ray again.
//
// Outputs (GENERAL layout storage images, render-region sized at the top-left):
//    SurfaceImage  rgba32f  xyz = world position, w = uintBitsToFloat(visibility id)  (0xFFFFFFFF = background)
//    NormalImage   rgba16f  xyz = geometric normal facing the camera, w = 0
//    OutputImage   rgba8    written ONLY when Control.z (debug view) ≠ 0 — the kernel is skipped by the CPU in that case
// NOTE: GLSL source lowered by glslc -fshader-stage=compute.

#version 460
#extension GL_GOOGLE_include_directive : require
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "Shaders/SceneRecords.slang"
#include "Shaders/RayGeneration.slang"

layout(std430, set = 0, binding = 1) readonly buffer InstanceExtent { InstanceRecord Instances[]; };
layout(std430, set = 0, binding = 2) readonly buffer ClusterExtent  { ClusterRecord  Clusters[];  };
layout(std430, set = 0, binding = 3) readonly buffer VertexExtent   { VertexRecord   Vertices[];  };
layout(std430, set = 0, binding = 4) readonly buffer IndexExtent    { uint           Indices[];   };
layout(std430, set = 0, binding = 5) readonly buffer MaterialExtent { MaterialRecord Materials[]; };

layout(set = 0, binding = 6)          uniform usampler2D VisibilityImage;
layout(set = 0, binding = 7)          uniform sampler2D  MotionImage;
layout(set = 0, binding = 8)          uniform sampler2D  DepthImage;
layout(set = 0, binding = 9)          uniform sampler2D  HiZImage;
layout(set = 0, binding = 10, rgba32f) uniform writeonly image2D SurfaceImage;
layout(set = 0, binding = 11, rgba16f) uniform writeonly image2D NormalImage;
layout(set = 0, binding = 12, rgba8)   uniform writeonly image2D OutputImage;

// R6 row 3: prev-frame reservoirs for the M / W / Age debug views — the same buffer the kernel reads as
//    binding 16 (written per frame by VisibilityExchange::RecordFrame), so the views show the last completed
//    kernel frame. The kernel is skipped in debug views, so Curr is never fresher than this by design.
struct GpuReservoirView
{
    vec4  Sample;    // xyz = light sample point, w = WeightSum
    uvec4 Counts;    // x = M, y = SelectedLight, z = Visible, w = Age
    vec4  UvDepth;   // xy = SelectedUv, z = W, w = view depth at the build pixel [m]
    vec4  Normal;    // xyz = geometric normal at the build pixel, w = stride guard (ViewportWidth)
};
layout(std430, set = 0, binding = 13) readonly buffer ReservoirExtent { GpuReservoirView Reservoirs[]; };

// Debug views — mirror DebugViewCategory in Engine/DeviceExchange/VisibilityExchange.h
const uint kDebugOff        = 0u;
const uint kDebugDepth      = 1u;
const uint kDebugVisibility = 2u;
const uint kDebugMotion     = 3u;
const uint kDebugCluster    = 4u;
const uint kDebugHiZ        = 5u;   // level = Control.y & 15 is NOT used; HiZ view shows level 3 (8×8 px footprints)
const uint kDebugAlbedo     = 6u;
const uint kDebugNormal     = 7u;
const uint kDebugRoughness  = 8u;   // R4b: MaterialRecord.Albedo.w (constant part; textured roughness is not resolved here)
const uint kDebugMetalness  = 9u;   // R4b: MaterialRecord.Emissive.w
const uint kDebugShadingNormal = 10u;   // R4b: interpolated vertex normal (⚠️ without the normal map — that needs the slab + texture table)
const uint kDebugReservoirM   = 11u;  // R6 row 3: reservoir M (sample count) — white ramp, saturates at 256
const uint kDebugReservoirW   = 12u;  // R6 row 3: reservoir W (unbiased weight) — 1−exp(−W·k) heat ramp
const uint kDebugReservoirAge = 13u;  // R6 row 3: reservoir Age (frames since a fresh sample won) — ramp, saturates at 16

vec3 HashColour(uint V)
{
    V ^= V >> 16; V *= 0x7feb352du; V ^= V >> 15; V *= 0x846ca68bu; V ^= V >> 16;
    return vec3(float(V & 255u), float((V >> 8) & 255u), float((V >> 16) & 255u)) / 255.0 * 0.8 + 0.2;
}

void main()
{
    uvec2 Pixel = gl_GlobalInvocationID.xy;
    if (Pixel.x >= Extent.x || Pixel.y >= Extent.y) return;

    uint  Visibility = texelFetch(VisibilityImage, ivec2(Pixel), 0).r;
    vec3  Position   = vec3(0.0);
    vec3  Normal     = vec3(0.0, 0.0, 1.0);
    uint  Material   = 0u;
    uint  ClusterId  = 0u;
    vec3  ShadingNormal = vec3(0.0, 0.0, 1.0);

    if (Visibility != kVisibilityInvalid)
    {
        uint           InstanceIndex = VisibilityInstance(Visibility);
        uint           Primitive     = VisibilityPrimitive(Visibility);
        InstanceRecord Instance      = Instances[InstanceIndex];
        uint           Base          = Instance.FirstIndex + Primitive * 3u;
        vec3 A = (Instance.World * vec4(Vertices[Indices[Base + 0u] + Instance.VertexOffset].Position.xyz, 1.0)).xyz;
        vec3 B = (Instance.World * vec4(Vertices[Indices[Base + 1u] + Instance.VertexOffset].Position.xyz, 1.0)).xyz;
        vec3 C = (Instance.World * vec4(Vertices[Indices[Base + 2u] + Instance.VertexOffset].Position.xyz, 1.0)).xyz;
        Material  = Instance.MaterialIndex;
        ClusterId = Instance.ClusterOffset + Primitive / 128u;

        CameraBasis Camera;
        Camera.Origin = CameraOrigin.xyz; Camera.Forward = CameraForward.xyz; Camera.Right = CameraRight.xyz; Camera.Up = CameraUp.xyz;
        Camera.TanHalfFov = CameraForward.w; Camera.Aspect = CameraRight.w;
        vec3 Direction = GeneratePrimaryDirection(Camera, Pixel, Jitter.zw, Extent.xy);

        vec3  Edge1 = B - A, Edge2 = C - A;
        vec3  Cross = cross(Edge1, Edge2);
        Normal = normalize(Cross);
        float Denominator = dot(Normal, Direction);
        float T = abs(Denominator) > 1e-8 ? dot(Normal, A - CameraOrigin.xyz) / Denominator : 0.0;
        Position = CameraOrigin.xyz + Direction * max(T, 0.0);
        if (dot(Normal, -Direction) < 0.0) Normal = -Normal;    // face the camera, as the kernel did with its primary hit

        if (Control.z == kDebugShadingNormal)
        {
            // Barycentrics of the hit against the same triangle → interpolated vertex normal (linear part of World; flipped to the camera side).
            vec3  E2 = Position - A;
            float D00 = dot(Edge1, Edge1), D01 = dot(Edge1, Edge2), D11 = dot(Edge2, Edge2), D20 = dot(E2, Edge1), D21 = dot(E2, Edge2);
            float Den = D00 * D11 - D01 * D01;
            float V = abs(Den) > 1e-20 ? (D11 * D20 - D01 * D21) / Den : 0.0;
            float W = abs(Den) > 1e-20 ? (D00 * D21 - D01 * D20) / Den : 0.0;
            vec3 N = (1.0 - V - W) * Vertices[Indices[Base + 0u] + Instance.VertexOffset].Normal.xyz
                   + V * Vertices[Indices[Base + 1u] + Instance.VertexOffset].Normal.xyz
                   + W * Vertices[Indices[Base + 2u] + Instance.VertexOffset].Normal.xyz;
            N = mat3(Instance.World) * N;
            ShadingNormal = dot(N, N) > 1e-12 ? normalize(N) : Normal;
            if (dot(ShadingNormal, Normal) < 0.0) ShadingNormal = -ShadingNormal;
        }
    }

    imageStore(SurfaceImage, ivec2(Pixel), vec4(Position, uintBitsToFloat(Visibility)));
    imageStore(NormalImage,  ivec2(Pixel), vec4(Normal, 0.0));

    uint View = Control.z;
    if (View == kDebugOff) return;

    vec3 Colour = vec3(0.0);
    bool Hit = Visibility != kVisibilityInvalid;
    if (View == kDebugDepth)
    {
        // Reverse-Z device depth → linear view depth in metres, mapped with 1 − exp(−z / 10 m) so a room and a hall both read.
        float Device = texelFetch(DepthImage, ivec2(Pixel), 0).r;
        float Linear = Device > 0.0 ? CameraOrigin.w / Device : 0.0;
        Colour = Hit ? vec3(1.0 - exp(-Linear * 0.1)) : vec3(0.0);
    }
    else if (View == kDebugVisibility) Colour = Hit ? HashColour(Visibility) : vec3(0.0);
    else if (View == kDebugCluster)    Colour = Hit ? HashColour(ClusterId * 2654435761u) : vec3(0.0);
    else if (View == kDebugMotion)
    {
        vec2 Motion = texelFetch(MotionImage, ivec2(Pixel), 0).rg * vec2(Extent.xy);   // pixels
        Colour = Hit ? vec3(0.5 + clamp(Motion * 0.05, -0.5, 0.5), 0.5) : vec3(0.0);   // ±10 px → full red/green swing
    }
    else if (View == kDebugHiZ)
    {
        int   Level  = min(3, int(Extent.z) - 1);
        float Device = texelFetch(HiZImage, ivec2(Pixel) >> Level, Level).r;
        float Linear = Device > 0.0 ? CameraOrigin.w / Device : 0.0;
        Colour = Device > 0.0 ? vec3(1.0 - exp(-Linear * 0.1)) : vec3(0.0);
    }
    else if (View == kDebugAlbedo) Colour = Hit ? Materials[Material].Albedo.xyz : vec3(0.0);
    else if (View == kDebugNormal) Colour = Hit ? Normal * 0.5 + 0.5 : vec3(0.0);
    else if (View == kDebugRoughness)     Colour = Hit ? vec3(Materials[Material].Albedo.w)   : vec3(0.0);
    else if (View == kDebugMetalness)     Colour = Hit ? vec3(Materials[Material].Emissive.w) : vec3(0.0);
    else if (View == kDebugShadingNormal) Colour = Hit ? ShadingNormal * 0.5 + 0.5 : vec3(0.0);
    else if (View == kDebugReservoirM || View == kDebugReservoirW || View == kDebugReservoirAge)
    {
        // R6 row 3: last completed kernel frame's reservoir at this pixel (unvalidated here — the M/W/Age views
        //    are diagnostics of the reuse state itself, including stale entries).
        GpuReservoirView R = Reservoirs[Pixel.y * Extent.x + Pixel.x];
        if (View == kDebugReservoirM)         Colour = vec3(clamp(float(R.Counts.x) / 256.0, 0.0, 1.0));
        else if (View == kDebugReservoirAge)  Colour = vec3(clamp(float(R.Counts.w) / 16.0, 0.0, 1.0));
        else                                  Colour = vec3(1.0 - exp(-R.UvDepth.z * 0.25));
    }

    imageStore(OutputImage, ivec2(Pixel), vec4(Colour, 1.0));
}
