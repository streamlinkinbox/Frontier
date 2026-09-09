//============================================================================================================================================
//                                                    RAYGENERATION.SLANG
//============================================================================================================================================
// 🧩 The single statement of how a Vulkan image pixel becomes a world-space primary ray. Shared by every
//    compute path that ray-casts straight into a storage image (ReSTIR viewport, future previews, proofs).
//
// Coordinate chain (CLAUDE.md §7):
//
//    World   — right-handed, +X right (east), +Y forward (north), +Z up (zenith).      X × Y = Z
//    View    — orthonormal basis supplied by CameraProjection: Right, Up, Forward.     Right = Forward × WorldUp,
//              Up = Right × Forward. Both are world-space unit vectors.
//    Image   — Vulkan storage-image / framebuffer space: origin top-left, x → right, y → DOWN,
//              pixel (0,0) is the top-left texel. There is no clip space and no viewport transform on this
//              path, so the ONLY Vulkan convention that applies is the downward y axis.
//
//    The pinhole mapping therefore is:
//        screenX = (2u − 1) · tan(fov/2) · aspect        u = (x + jitter) / width     → −1 left … +1 right
//        screenY = (1 − 2v) · tan(fov/2)                 v = (y + jitter) / height    → +1 top  … −1 bottom
//        dir     = normalize(Forward + screenX · Right + screenY · Up)
//
//    "(1 − 2v)" IS the Vulkan Y-inversion: image row 0 (top) must look along +Up. Do not flip it again
//    downstream and do not apply a projection-matrix style Y-flip; that would turn the image upside down.
//
// Included by the viewport shader via #include; requires GL_GOOGLE_include_directive (glslc) or slangc.
//------------------------------------------------------------------------------------------------------------------------

#ifndef FRONTIER_CAMERA_RAY_GENERATION
#define FRONTIER_CAMERA_RAY_GENERATION

struct CameraBasis
{
    vec3  Origin;           // [m]  world-space eye position
    vec3  Forward;          // [-]  unit, world-space
    vec3  Right;            // [-]  unit, world-space
    vec3  Up;               // [-]  unit, world-space
    float TanHalfFov;       // [-]  tan(vertical FoV / 2)
    float Aspect;           // [-]  width / height
};

// 🧩 Pixel + sub-pixel jitter (0..1) → normalised world-space ray direction.
vec3 GeneratePrimaryDirection(CameraBasis Camera, uvec2 Pixel, vec2 Jitter, uvec2 Extent)
{
    float u = (float(Pixel.x) + Jitter.x) / float(Extent.x);
    float v = (float(Pixel.y) + Jitter.y) / float(Extent.y);

    float screenX = (2.0 * u - 1.0) * Camera.Aspect * Camera.TanHalfFov;   // −1 left … +1 right
    float screenY = (1.0 - 2.0 * v) * Camera.TanHalfFov;                   // +1 top  … −1 bottom  (Vulkan y-down)

    return normalize(Camera.Forward + screenX * Camera.Right + screenY * Camera.Up);
}

#endif // FRONTIER_CAMERA_RAY_GENERATION
