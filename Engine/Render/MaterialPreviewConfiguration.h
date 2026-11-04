/*==========================================================================================================================================
                                                   MATERIALPREVIEWCONFIGURATION.H
==========================================================================================================================================*/
// 🧩 Bound interactive material inspection independently of authored materials and atlas thumbnail recipes.

#pragma once
#include <algorithm>
#include <cmath>
#include <array>
#include <cstdint>
#include <numbers>

namespace Frontier
{
enum class MaterialPreviewShape : uint32_t
{
    ShaderBall,
    Sphere,
    Plane,
    Cube,
    Cloth
};
struct MaterialPreviewConfiguration
{
    MaterialPreviewShape Shape    = MaterialPreviewShape::ShaderBall;
    // 📝 Index into the scanned EnvironmentArchives catalogue (baked equirect studio maps,
    // plus any user-imported equirect PNGs); hosts clamp it to the live catalogue size.
    uint32_t Environment          = 0;
    float    Zoom                 = 1;
    float    Exposure             = 1;
    // 📝 Scales environment radiance and the key light together; exposure stays display-side.
    float    LightIntensity       = 1;
    float                      PanX        = 0;
    float                      PanY        = 0;
    float                      Yaw         = 0;
    float                      Pitch       = 0;
    bool operator==(const MaterialPreviewConfiguration &) const = default;
};
inline bool ValidateMaterialPreviewConfiguration(const MaterialPreviewConfiguration &Configuration)
{
    return uint32_t(Configuration.Shape) <= 4 && Configuration.Environment <= 63 &&
        std::isfinite(Configuration.Zoom) && Configuration.Zoom >= .5f && Configuration.Zoom <= 5 &&
        std::isfinite(Configuration.Exposure) && Configuration.Exposure >= .25f && Configuration.Exposure <= 4 &&
        std::isfinite(Configuration.LightIntensity) && Configuration.LightIntensity >= 0 && Configuration.LightIntensity <= 4 &&
        std::isfinite(Configuration.PanX) && std::abs(Configuration.PanX) <= 2 &&
        std::isfinite(Configuration.PanY) && std::abs(Configuration.PanY) <= 2 &&
        std::isfinite(Configuration.Yaw) && std::abs(Configuration.Yaw) <= 3.141593f &&
        std::isfinite(Configuration.Pitch) && std::abs(Configuration.Pitch) <= 1.45f;
}

struct MaterialPreviewProjection
{
    std::array<float,16> ViewFromSurface;
    std::array<float,16> ClipFromSurface;
    // 📝 The orbit basis ConstructOrbitProjection/ConstructMaterialPreviewProjection already compute
    // internally to build ViewFromSurface, exposed directly so a caller using its own CPU projection (e.g.
    // BakingWorkspacePanel::ConstructViewport3D's draw-list rasterizer) can orbit around exactly the same
    // eye/basis as the Vulkan view matrix, instead of re-deriving (or worse, re-inventing) the same orbit
    // trigonometry a second time.
    std::array<float,3> Eye{}, Forward{}, Right{}, Up{};
};

inline void ResetMaterialPreviewView(MaterialPreviewConfiguration &Configuration)
{
    Configuration.Zoom = 1;
    Configuration.PanX = Configuration.PanY = Configuration.Yaw = Configuration.Pitch = 0;
}

// 📝 The actual orbit-camera math (LookAt-style orbit, Blender/three.js convention: yaw spins about
// screen-vertical world-up through the pivot, pitch tips about the fixed screen-right axis with no roll),
// factored out of ConstructMaterialPreviewProjection so every native workspace that frames a 3D subject -
// not just the Material library's five fixed preview shapes - shares one real implementation instead of a
// parallel hand-rolled camera. Radius/Pivot/FrameRatio are the only per-subject inputs: Material's five
// shapes pass their measured bounding constants (see below); a workspace framing arbitrary geometry (e.g.
// Baking's imported/demo meshes) instead passes its own live-computed bounding radius and pivot.
inline MaterialPreviewProjection ConstructOrbitProjection(float Yaw, float Pitch, float Zoom, float PanX, float PanY,
    float Radius, float Pivot[3], float FrameRatio, float Aspect = 1, float FovDegrees = 34) noexcept
{
    const float θ = Yaw;
    const float Cosθ = std::cos(θ), Sinθ = std::sin(θ);
    MaterialPreviewProjection Projection;
    float R[9]{1, 0, 0, 0, 1, 0, 0, 0, 1};
    const float FovY = FovDegrees * std::numbers::pi_v<float> / 180;
    constexpr float BaseElevation = .1908f;
    const float Focal = Radius * FrameRatio;
    const float Distance = std::max(Focal / Zoom, Radius + .2f);
    const float Elevation = std::clamp(Pitch + BaseElevation, -1.45f, 1.45f);
    const float CosE = std::cos(Elevation), SinE = std::sin(Elevation);
    const float EyeDir[3]{-CosE * Sinθ, SinE, CosE * Cosθ};
    const float PanScale = Distance * std::tan(FovY / 2);
    float Right[3]{EyeDir[2], 0, -EyeDir[0]};
    const float RightLength = std::hypot(Right[0], Right[2]);
    Right[0] /= RightLength; Right[2] /= RightLength;
    const float Up[3]{EyeDir[1] * Right[2], EyeDir[2] * Right[0] - EyeDir[0] * Right[2], -EyeDir[1] * Right[0]};
    // 📝 Pan walks the pivot against camera-right but with camera-up: clip Y is flipped, so both
    // apples-to-apples NDC offsets land exactly on (PanX, PanY) like the orthographic rig.
    const float Px = Pivot[0] - (Right[0] * PanX - Up[0] * PanY) * PanScale;
    const float Py = Pivot[1] - (Right[1] * PanX - Up[1] * PanY) * PanScale;
    const float Pz = Pivot[2] - (Right[2] * PanX - Up[2] * PanY) * PanScale;
    const float Ex = Px + EyeDir[0] * Distance, Ey = Py + EyeDir[1] * Distance, Ez = Pz + EyeDir[2] * Distance;
    const float Fwd[3]{-EyeDir[0], -EyeDir[1], -EyeDir[2]};
    R[0] = Right[0]; R[1] = Up[0]; R[2] = -Fwd[0];
    R[3] = Right[1]; R[4] = Up[1]; R[5] = -Fwd[1];
    R[6] = Right[2]; R[7] = Up[2]; R[8] = -Fwd[2];
    Projection.ViewFromSurface = {R[0], R[1], R[2], 0, R[3], R[4], R[5], 0, R[6], R[7], R[8], 0,
        -(R[0] * Ex + R[3] * Ey + R[6] * Ez), -(R[1] * Ex + R[4] * Ey + R[7] * Ez),
        -(R[2] * Ex + R[5] * Ey + R[8] * Ez), 1};
    Projection.Eye = {Ex, Ey, Ez};
    Projection.Forward = {Fwd[0], Fwd[1], Fwd[2]};
    Projection.Right = {Right[0], Right[1], Right[2]};
    Projection.Up = {Up[0], Up[1], Up[2]};
    // 📝 Vulkan clip (Y-down, depth 0..1); the shader divides w, so tiles and the preview share this form.
    const float SafeAspect = Aspect > 0 && std::isfinite(Aspect) ? Aspect : 1;
    constexpr float Near = .1f, Far = 20;
    const float F = 1 / std::tan(FovY / 2);
    const float P[16]{F / SafeAspect, 0, 0, 0, 0, -F, 0, 0, 0, 0, Far / (Near - Far), -1, 0, 0,
        Near * Far / (Near - Far), 0};
    const auto &V = Projection.ViewFromSurface;
    for (size_t Column = 0; Column < 4; ++Column)
        for (size_t Row = 0; Row < 4; ++Row)
        {
            float Sum = 0;
            for (size_t Axis = 0; Axis < 4; ++Axis) Sum += P[Axis * 4 + Row] * V[Column * 4 + Axis];
            Projection.ClipFromSurface[Column * 4 + Row] = Sum;
        }
    return Projection;
}

inline MaterialPreviewProjection ConstructMaterialPreviewProjection(
    const MaterialPreviewConfiguration &Configuration, float Aspect = 1)
{
    const uint32_t Shape = std::min(uint32_t(Configuration.Shape), 4u);
    // 📝 Alloy capture rig: 34° perspective, each specimen framed the way the HTML capture frames its
    // r=1.35 ball at distance 5.8, viewed slightly from above (camera (0,1.1,5.7) looks down ~11°).
    // Pivots/radii are measured bounding volumes of the inspection geometry. The 16-unit-per-shape pivot
    // offset keeps every shape's own GPU-baked atlas tile isolated when all five are rendered into one wide
    // scene for thumbnail baking (see MaterialLibraryRendering.cpp) - irrelevant to a single live preview,
    // which only ever reads back the one shape's tile.
    constexpr float FrameRatio = 5.8f / 1.35f;
    constexpr float PivotTable[5][3]{{0, -.002f, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, -.041f, 0}};
    constexpr float RadiusTable[5]{1.085f, .85f, 1.131f, 1.074f, .959f};
    float Pivot[3]{16.f * float(Shape) + PivotTable[Shape][0], PivotTable[Shape][1], PivotTable[Shape][2]};
    return ConstructOrbitProjection(Configuration.Yaw, Configuration.Pitch, Configuration.Zoom, Configuration.PanX,
        Configuration.PanY, RadiusTable[Shape], Pivot, FrameRatio, Aspect);
}


// 📝 World-locked preview lighting: a direction authored in the default pose's view frame is
// carried through the world frame into the orbiting view frame, so the studio stays fixed while
// the camera moves (HTML OrbitControls behaviour) and highlights sweep across the surface
// instead of staying glued to the screen. Exact identity at the default configuration.
inline std::array<float, 3> ConstructMaterialPreviewLightDirection(
    const MaterialPreviewConfiguration &Configuration, std::array<float, 3> ReferenceDirection)
{
    const auto &Current = ConstructMaterialPreviewProjection(Configuration).ViewFromSurface;
    const auto &Reference = ConstructMaterialPreviewProjection(MaterialPreviewConfiguration{}).ViewFromSurface;
    const auto Apply = [](const std::array<float, 16> &Matrix, std::array<float, 3> Direction, bool Transposed)
    {
        std::array<float, 3> Result{};
        for (size_t Row = 0; Row < 3; ++Row)
            for (size_t Column = 0; Column < 3; ++Column)
                Result[Row] += Matrix[Transposed ? Row * 4 + Column : Column * 4 + Row] * Direction[Column];
        return Result;
    };
    return Apply(Current, Apply(Reference, ReferenceDirection, true), false);
}

// 📝 World up expressed in the current view frame (second basis column of the view rotation),
// so the sky gradient stays fixed to the world while the camera pitches and yaws.
inline std::array<float, 3> ConstructMaterialPreviewEnvironmentUp(
    const MaterialPreviewConfiguration &Configuration)
{
    const auto &View = ConstructMaterialPreviewProjection(Configuration).ViewFromSurface;
    return {View[4], View[5], View[6]};
}

}
