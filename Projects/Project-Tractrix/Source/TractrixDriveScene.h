//============================================================================================================================================
//                                                     TRACTRIXDRIVESCENE.H
//============================================================================================================================================
// 📦 Project-Tractrix's drivable authoring level (`--scene drive`). The exact ShowroomStructure discipline — an analytic
//    world-space triangle soup with per-object spans and OpenPBR materials, exported once through SceneCodec::Encode — but
//    the furniture is a driving course instead of an interior room:
//
//        • a flat CHECKER / grid pad the car spawns on (visible speed reference under the wheels);
//        • a RAMP (wedge) ahead of the spawn for jumps;
//        • a row of rounded SPEED BUMPS to exercise the suspension + soft tyres;
//        • a slalom line of CONES;
//        • the real ControlVehicle BODY shell (ControlVehicleMesh.inl — the authored exterior, not a box) wearing the
//          System-B automotive flake clearcoat, plus four PROCEDURAL wheels (the owner's instruction: never import wheels).
//
//    The chassis and the four wheels are appended each with their OWN material, so the codec gives each its OWN
//    InstanceRecord — the renderer moves instances, not triangles, so the vehicle-instance sequence can drive the chassis
//    pose and spin/steer every wheel independently from the physics telemetry. They are marked Dynamic spans.
//
//    Axis convention is Frontier's and the vehicle's shared frame: +X forward, +Y left, +Z up, metres. No basis change is
//    performed anywhere — ControlVehicleMesh.inl is authored in this frame and the GRIT hub table below is in it too, so
//    render placement and RigidBodySolver placement agree on frame zero (the D4-style rest gate).

#pragma once

#include "../../../Engine/ContentInterchange/MaterialDescriptor.h"
#include "../../../Engine/DeviceExchange/SwapchainExchange.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier {
namespace Tractrix {

class TractrixDriveScene
{
public:
    // Course dimensions (metres). Defaults give a 120 m checker pad, a ramp 8 m ahead and three speed bumps behind.
    struct Layout
    {
        float    PadHalfExtent = 60.0f;   // [m]   half-size of the flat drivable pad
        float    CheckerCell   = 4.0f;    // [m]   checker square edge (the visible speed grid)
        float    RampNearX     = 9.0f;    // [m]   ramp toe (nearest edge ahead of spawn, +X)
        float    RampRunX      = 6.0f;    // [m]   ramp horizontal run
        float    RampRise      = 1.35f;   // [m]   ramp crest height
        float    RampHalfWidth = 3.2f;    // [m]   ramp half-width in Y
        uint32_t BumpCount     = 3u;      // [-]   rounded speed bumps behind the spawn (−X)
        float    BumpFirstX    = -7.0f;   // [m]
        float    BumpSpacingX  = -4.5f;   // [m]   stride between bumps (negative = further behind)
        float    BumpHeight    = 0.11f;   // [m]
        float    BumpHalfWidthY= 4.0f;    // [m]
        uint32_t ConeCount     = 8u;      // [-]   slalom cones along the pad
    };

    // Geometry the physics side must agree with (GRIT ControlVehicle, body-local frame, metres).
    static constexpr float kTyreRadius   = 0.34f;    // [m]
    static constexpr float kTyreHalfWidth= 0.1175f;  // [m]   235 mm section
    static constexpr float kHubZ         = 0.0914f;  // [m]   hub centre height, body-local
    // Wheel hubs, body-local: FL, FR, RL, RR. Wheelbase 3.396 (front +1.7274 / rear −1.6686), track 2.095 (±1.0475).
    static constexpr float kWheelHub[4][3] = {
        {  1.7274f,  1.0475f, kHubZ },   // 0 FL
        {  1.7274f, -1.0475f, kHubZ },   // 1 FR
        { -1.6686f,  1.0475f, kHubZ },   // 2 RL
        { -1.6686f, -1.0475f, kHubZ },   // 3 RR
    };

    void Construct() noexcept;                              // default course
    void Construct(const Layout& InLayout) noexcept;

    // Chassis rest translation: lifts the body-local shell so the wheels rest on the pad (z = 0) at frame zero.
    [[nodiscard]] Vector3 QueryChassisSpawn() const noexcept;
    // Wheel `Wheel` rest centre in world space (chassis spawn + body-local hub).
    [[nodiscard]] Vector3 QueryWheelRestOrigin(uint32_t Wheel) const noexcept;

    // Instance/material ordinals of the dynamic parts, for the vehicle-instance sequence's per-frame World rewrite.
    [[nodiscard]] uint32_t QueryChassisInstance() const noexcept { return ChassisMaterial; }
    [[nodiscard]] uint32_t QueryWheelInstance(uint32_t Wheel) const noexcept { return WheelMaterial[Wheel & 3u]; }
    [[nodiscard]] uint32_t QueryFirstDynamicInstance() const noexcept { return ChassisMaterial; }
    [[nodiscard]] uint32_t QueryDynamicCount() const noexcept { return 5u; }   // chassis + 4 wheels

    // Writes DriveScene.gltf at Path (same codec path ShowroomStructure/ShaderBallStructure follow). Error receives the message.
    [[nodiscard]] bool Export(const std::string& Path, std::string* Error) const noexcept;

    [[nodiscard]] const std::vector<TriangleIndex>&      QueryTriangles()     const noexcept { return Triangles; }
    [[nodiscard]] const std::vector<Vector3>&            QueryCornerNormals() const noexcept { return CornerNormals; }
    [[nodiscard]] const std::vector<MaterialDescriptor>& QueryMaterials()     const noexcept { return Materials; }

    // One record per scene object over Triangles, in append order (ShowroomStructure's self-closing scope).
    struct SpanScope
    {
        std::vector<TriangleSpanRecord>*  Spans     = nullptr;
        const std::vector<TriangleIndex>* Triangles = nullptr;
        uint32_t                          Span      = 0u;
        ~SpanScope() noexcept;
    };
    [[nodiscard]] SpanScope                              OpenSpan(const char* Name, bool Dynamic = false) noexcept;
    [[nodiscard]] const std::vector<TriangleSpanRecord>& QuerySpans() const noexcept { return Spans; }

private:
    void AppendTriangle(const Vector3 P[3], const Vector3 N[3], const float Uv[3][2], uint32_t Material) noexcept;
    void AppendQuad(const Vector3& A, const Vector3& B, const Vector3& C, const Vector3& D, uint32_t Material, float UvScale) noexcept;
    void AppendBox(const Vector3& Minimum, const Vector3& Maximum, uint32_t Material) noexcept;
    void AppendWedge(float NearX, float FarX, float Rise, float HalfWidth, uint32_t Material) noexcept;
    void AppendBump(float CentreX, float HalfWidthY, float Height, uint32_t Material) noexcept;
    void AppendCone(const Vector3& BaseCentre, float Radius, float Height, uint32_t Material, uint32_t Segments) noexcept;
    // Wheel: a tyre cylinder (spin axis +Y) with a hub disc, centred at Centre.
    void AppendWheel(const Vector3& Centre, float Radius, float HalfWidth, uint32_t TyreMaterial, uint32_t HubMaterial, uint32_t Segments) noexcept;
    // The ControlVehicle shell from ControlVehicleMesh.inl, translated by Offset, smooth-shaded, one material.
    void AppendBodyShell(const Vector3& Offset, uint32_t Material) noexcept;

    std::vector<TriangleIndex>      Triangles;
    std::vector<Vector3>            CornerNormals;
    std::vector<MaterialDescriptor> Materials;
    std::vector<TriangleSpanRecord> Spans;

    Layout   LayoutData{};
    Vector3  ChassisSpawn{ 0.0f, 0.0f, 0.0f };
    uint32_t ChassisMaterial  = 0u;      // [idx] material/instance ordinal of the body shell
    uint32_t WheelMaterial[4] = { 0u, 0u, 0u, 0u };
};

} // namespace Tractrix
} // namespace Frontier
