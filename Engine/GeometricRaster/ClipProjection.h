//============================================================================================================================================
//                                                       CLIPPROJECTION.H
//============================================================================================================================================
// 🧩 World → view → Vulkan clip transforms for the visibility raster and the GPU cull, plus the small 4×4 helpers the
//    scene import needs (TRS composition, point / direction transforms). Header-only; no Vulkan types.
//
// Coordinate conventions (CLAUDE.md §7 + Shaders/RayGeneration.slang):
//    World  : right-handed, +Z up, +Y forward, +X right, metres.
//    View   : x = dot(p − o, Right), y = dot(p − o, Up), z = dot(p − o, Forward)   (z grows INTO the screen, no sign flip).
//    Clip   : x_c = x_v / (aspect·tanHalf),  y_c = −y_v / tanHalf,  z_c = near,  w_c = z_v.
//             → Vulkan NDC has Y pointing DOWN, so view-up is negated once here — the same single flip RayGeneration.slang
//               applies with screenY = (1 − 2v)·tanHalf. Depth is REVERSE-Z with an infinite far plane: z_ndc = near / z_v,
//               so the near plane maps to 1.0 and infinity to 0.0 (depth clear = 0, compare = GREATER).

#pragma once

#include "../DeviceExchange/OrientationClassifier.h"
#include <cmath>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                   MATRIX HELPERS
//------------------------------------------------------------------------------------------------------------------------

// Columns[c][r] — column-major, so Columns[3] is the translation column.
[[nodiscard]] inline Matrix4x4 MultiplyProjection(const Matrix4x4& A, const Matrix4x4& B) noexcept   // A · B
{
    Matrix4x4 R;
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            R.Columns[c][r] = A.Columns[0][r] * B.Columns[c][0] + A.Columns[1][r] * B.Columns[c][1]
                            + A.Columns[2][r] * B.Columns[c][2] + A.Columns[3][r] * B.Columns[c][3];
    return R;
}

[[nodiscard]] inline Vector3 TransformPoint(const Matrix4x4& M, const Vector3& P) noexcept
{
    return Vector3{ M.Columns[0][0] * P.x + M.Columns[1][0] * P.y + M.Columns[2][0] * P.z + M.Columns[3][0],
                    M.Columns[0][1] * P.x + M.Columns[1][1] * P.y + M.Columns[2][1] * P.z + M.Columns[3][1],
                    M.Columns[0][2] * P.x + M.Columns[1][2] * P.y + M.Columns[2][2] * P.z + M.Columns[3][2] };
}

[[nodiscard]] inline Vector3 TransformDirection(const Matrix4x4& M, const Vector3& D) noexcept
{
    return Vector3{ M.Columns[0][0] * D.x + M.Columns[1][0] * D.y + M.Columns[2][0] * D.z,
                    M.Columns[0][1] * D.x + M.Columns[1][1] * D.y + M.Columns[2][1] * D.z,
                    M.Columns[0][2] * D.x + M.Columns[1][2] * D.y + M.Columns[2][2] * D.z };
}

// Largest axis scale of the upper 3×3 — bounding-sphere radii are scaled by this (conservative for non-uniform scale).
[[nodiscard]] inline float QueryMaximumScale(const Matrix4x4& M) noexcept
{
    float Best = 0.0f;
    for (int c = 0; c < 3; ++c)
    {
        const float L = std::sqrt(M.Columns[c][0] * M.Columns[c][0] + M.Columns[c][1] * M.Columns[c][1] + M.Columns[c][2] * M.Columns[c][2]);
        if (L > Best) Best = L;
    }
    return Best;
}

// Column-major 16-float array (glTF node matrix order) → Matrix4x4.
[[nodiscard]] inline Matrix4x4 ProjectionFromColumns(const float* Sixteen) noexcept
{
    Matrix4x4 M;
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            M.Columns[c][r] = Sixteen[c * 4 + r];
    return M;
}

// glTF is right-handed Y-up; the engine is right-handed Z-up. (x, y, z)_gltf → (x, −z, y)_engine, and back.
[[nodiscard]] inline Matrix4x4 ConstructGltfToWorldProjection() noexcept
{
    Matrix4x4 M;
    M.Columns[0][0] = 1.0f; M.Columns[1][1] = 0.0f; M.Columns[2][2] = 0.0f;
    M.Columns[1][2] = 1.0f;     // gltf +Y → world +Z
    M.Columns[2][1] = -1.0f;    // gltf +Z → world −Y
    return M;
}

[[nodiscard]] inline Vector3 WorldToGltf(const Vector3& P) noexcept { return Vector3{ P.x, P.z, -P.y }; }
[[nodiscard]] inline Vector3 GltfToWorld(const Vector3& P) noexcept { return Vector3{ P.x, -P.z, P.y }; }

//------------------------------------------------------------------------------------------------------------------------
//                                                CAMERA CLIP PROJECTION
//------------------------------------------------------------------------------------------------------------------------

struct CameraClipConfiguration
{
    Vector3 Origin;             // [m]  eye position
    Vector3 Forward;            // [-]  unit forward
    Vector3 Right;              // [-]  unit right
    Vector3 Up;                 // [-]  unit up
    float   TanHalfFieldOfView; // [-]  tan(vertical FoV / 2)
    float   AspectRatio;        // [-]  width / height
    float   NearDistance;       // [m]  near plane (reverse-Z maps it to depth 1.0)
};

// View matrix: rows are the camera basis, translation = −basis·origin.
[[nodiscard]] inline Matrix4x4 ConstructViewProjection(const CameraClipConfiguration& C) noexcept
{
    Matrix4x4 V;
    V.Columns[0][0] = C.Right.x;   V.Columns[1][0] = C.Right.y;   V.Columns[2][0] = C.Right.z;
    V.Columns[0][1] = C.Up.x;      V.Columns[1][1] = C.Up.y;      V.Columns[2][1] = C.Up.z;
    V.Columns[0][2] = C.Forward.x; V.Columns[1][2] = C.Forward.y; V.Columns[2][2] = C.Forward.z;
    V.Columns[3][0] = -(C.Right.x   * C.Origin.x + C.Right.y   * C.Origin.y + C.Right.z   * C.Origin.z);
    V.Columns[3][1] = -(C.Up.x      * C.Origin.x + C.Up.y      * C.Origin.y + C.Up.z      * C.Origin.z);
    V.Columns[3][2] = -(C.Forward.x * C.Origin.x + C.Forward.y * C.Origin.y + C.Forward.z * C.Origin.z);
    V.Columns[3][3] = 1.0f;
    return V;
}

// Reverse-Z infinite perspective with the Vulkan Y flip folded in (see file header).
[[nodiscard]] inline Matrix4x4 ConstructClipProjection(const CameraClipConfiguration& C) noexcept
{
    Matrix4x4 P;
    P.Columns[0][0] = 1.0f / (C.AspectRatio * C.TanHalfFieldOfView);
    P.Columns[1][1] = -1.0f / C.TanHalfFieldOfView;
    P.Columns[2][2] = 0.0f;
    P.Columns[3][2] = C.NearDistance;   // z_c = near
    P.Columns[2][3] = 1.0f;             // w_c = z_v
    P.Columns[3][3] = 0.0f;
    return P;
}

[[nodiscard]] inline Matrix4x4 ConstructViewClipProjection(const CameraClipConfiguration& C) noexcept
{
    return MultiplyProjection(ConstructClipProjection(C), ConstructViewProjection(C));
}

} // namespace Frontier
