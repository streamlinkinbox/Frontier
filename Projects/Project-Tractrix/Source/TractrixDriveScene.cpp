//============================================================================================================================================
// 📦 Projects/Project-Tractrix/Source/TractrixDriveScene.cpp — builds the `--scene drive` drivable level (see header)
//============================================================================================================================================
// See TractrixDriveScene.h. Layout (camera behind the car at −X looking +X, Z up, metres):
//
//        pad        120 m checker/grid square, centred on the spawn (0,0,0); the car rests on it at frame zero
//        ramp       a wedge 9 m ahead (+X), 6 m run rising 1.35 m — the jump
//        bumps      three rounded ridges behind the spawn (−X) for the suspension / soft-tyre sweep
//        cones      a slalom line down the +X lane
//        vehicle    the real ControlVehicle shell (ControlVehicleMesh.inl) in System-B flake clearcoat, four procedural wheels
//
// Built once in world space and exported through SceneCodec::Encode to Content/Scenes/DriveScene.gltf — the same discipline
//    ShowroomStructure / ShaderBallStructure follow. The chassis + 4 wheels each get their own material so the codec emits one
//    InstanceRecord per part; the vehicle-instance sequence rewrites those World rows from the physics telemetry every frame.

#include "TractrixDriveScene.h"
#include "ControlVehicleMesh.inl"

#include "../../../Engine/ContentInterchange/SceneCodec.h"
#include "../../../Engine/DeviceExchange/OrientationClassifier.h"

#include <cmath>
#include <cstring>

namespace Frontier {
namespace Tractrix {

namespace {

constexpr float kPi = 3.14159265358979f;

MaterialDescriptor MakeMaterial(const char* Name)
{
    MaterialDescriptor D;
    D.Name = Name;
    D.Slabs.emplace_back();
    return D;
}

void AssignColour(float* Target, float R, float G, float B)
{
    Target[0] = R; Target[1] = G; Target[2] = B;
}

// Material ordinals — the order they are pushed in Construct(). Static scenery first, then the dynamic vehicle parts
//    LAST so QueryFirstDynamicInstance() names a contiguous [chassis, FL, FR, RL, RR] block.
enum : uint32_t
{
    MaterialCheckerLight = 0u,
    MaterialCheckerDark  = 1u,
    MaterialSurround     = 2u,
    MaterialRamp         = 3u,
    MaterialBump         = 4u,
    MaterialConeBody     = 5u,
    MaterialConeStripe   = 6u,
    MaterialBodyPaint    = 7u,   // ← first dynamic (chassis)
    MaterialTyreFL       = 8u,
    MaterialTyreFR       = 9u,
    MaterialTyreRL       = 10u,
    MaterialTyreRR       = 11u,
    MaterialHub          = 12u,
    kMaterialCount       = 13u
};

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                       SPAN SCOPE
//------------------------------------------------------------------------------------------------------------------------

TractrixDriveScene::SpanScope::~SpanScope() noexcept
{
    if (Spans == nullptr || Triangles == nullptr || Span >= Spans->size()) return;
    TriangleSpanRecord& S = (*Spans)[Span];
    const uint32_t Now = static_cast<uint32_t>(Triangles->size());
    S.TriangleCount = Now >= S.FirstTriangle ? Now - S.FirstTriangle : 0u;
}

TractrixDriveScene::SpanScope TractrixDriveScene::OpenSpan(const char* Name, bool Dynamic) noexcept
{
    TriangleSpanRecord S;
    S.FirstTriangle = static_cast<uint32_t>(Triangles.size());
    if (Name != nullptr) S.Name = Name;
    S.Dynamic = Dynamic;
    Spans.push_back(std::move(S));
    SpanScope Scope;
    Scope.Spans     = &Spans;
    Scope.Triangles = &Triangles;
    Scope.Span      = static_cast<uint32_t>(Spans.size()) - 1u;
    return Scope;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   PLACEMENT QUERIES
//------------------------------------------------------------------------------------------------------------------------

Vector3 TractrixDriveScene::QueryChassisSpawn() const noexcept
{
    return ChassisSpawn;
}

Vector3 TractrixDriveScene::QueryWheelRestOrigin(uint32_t Wheel) const noexcept
{
    const auto& H = kWheelHub[Wheel & 3u];
    return Vector3{ ChassisSpawn.x + H[0], ChassisSpawn.y + H[1], ChassisSpawn.z + H[2] };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        CONSTRUCT
//------------------------------------------------------------------------------------------------------------------------

void TractrixDriveScene::Construct() noexcept
{
    Construct(Layout{});
}

void TractrixDriveScene::Construct(const Layout& InLayout) noexcept
{
    Triangles.clear();
    CornerNormals.clear();
    Materials.clear();
    Spans.clear();
    LayoutData = InLayout;

    // Wheels rest on the pad (z = 0): lift the body-local shell by (tyre radius − hub height).
    ChassisSpawn = Vector3{ 0.0f, 0.0f, kTyreRadius - kHubZ };

    // --- Materials (order must match the enum above) -----------------------------------------------------------------
    Materials.reserve(kMaterialCount);
    {
        MaterialDescriptor D = MakeMaterial("drive_checker_light");
        AssignColour(D.Slabs[0].BaseColor, 0.60f, 0.61f, 0.63f);
        D.Slabs[0].SpecularRoughness = 0.55f;
        Materials.push_back(D);                                    // 0

        D = MakeMaterial("drive_checker_dark");
        AssignColour(D.Slabs[0].BaseColor, 0.10f, 0.105f, 0.12f);
        D.Slabs[0].SpecularRoughness = 0.60f;
        Materials.push_back(D);                                    // 1

        D = MakeMaterial("drive_surround");
        AssignColour(D.Slabs[0].BaseColor, 0.16f, 0.17f, 0.18f);
        D.Slabs[0].SpecularRoughness = 0.85f;
        Materials.push_back(D);                                    // 2

        D = MakeMaterial("drive_ramp");
        AssignColour(D.Slabs[0].BaseColor, 0.42f, 0.42f, 0.44f);
        D.Slabs[0].SpecularRoughness = 0.70f;
        Materials.push_back(D);                                    // 3

        D = MakeMaterial("drive_bump");
        AssignColour(D.Slabs[0].BaseColor, 0.72f, 0.54f, 0.06f);
        D.Slabs[0].SpecularRoughness = 0.55f;
        Materials.push_back(D);                                    // 4

        D = MakeMaterial("drive_cone_body");
        AssignColour(D.Slabs[0].BaseColor, 0.86f, 0.30f, 0.05f);
        D.Slabs[0].SpecularRoughness = 0.45f;
        Materials.push_back(D);                                    // 5

        D = MakeMaterial("drive_cone_stripe");
        AssignColour(D.Slabs[0].BaseColor, 0.90f, 0.90f, 0.92f);
        D.Slabs[0].SpecularRoughness = 0.40f;
        Materials.push_back(D);                                    // 6

        // System-B automotive flake clearcoat (see Docs/AutomotiveFlakePaint.md): deep candy-blue pigment,
        //    a metallic-flake population, and a separate low-roughness GGX clearcoat over the top.
        D = MakeMaterial("tractrix_bodypaint");
        {
            MaterialSlabDescriptor& S = D.Slabs[0];
            AssignColour(S.BaseColor, 0.020f, 0.045f, 0.24f);        // candy-blue pigment
            S.BaseMetalness            = 0.0f;
            S.SpecularRoughness        = 0.32f;
            S.SlateAutomotiveProfile   = 1.0f;                    // 1..5 = coated finite-flake families
            S.SlateAutomotiveSweep     = 0.0f;
            S.SlateGlintDensity        = 6.0f;                    // metallic-flake population (0 = off)
            S.SlateGlintUvScale        = 1.0f;
            S.CoatWeight               = 1.0f;                    // clearcoat over the flakes
            AssignColour(S.CoatColor, 1.0f, 1.0f, 1.0f);
            S.CoatRoughness            = 0.06f;
            S.CoatIor                  = 1.5f;
        }
        Materials.push_back(D);                                    // 7  chassis (first dynamic)
        ChassisMaterial = MaterialBodyPaint;

        for (uint32_t W = 0u; W < 4u; ++W)                         // 8..11  one tyre material per wheel → one instance each
        {
            char Name[32];
            std::snprintf(Name, sizeof(Name), "tractrix_tyre_%u", W);
            D = MakeMaterial(Name);
            AssignColour(D.Slabs[0].BaseColor, 0.028f, 0.028f, 0.032f);
            D.Slabs[0].SpecularRoughness = 0.85f;
            Materials.push_back(D);
            WheelMaterial[W] = MaterialTyreFL + W;
        }

        D = MakeMaterial("tractrix_hub");                         // 12  shared hub trim (baked into each wheel span)
        AssignColour(D.Slabs[0].BaseColor, 0.78f, 0.79f, 0.82f);
        D.Slabs[0].BaseMetalness     = 1.0f;
        D.Slabs[0].SpecularRoughness = 0.14f;
        Materials.push_back(D);                                    // 12
    }

    // --- Static scenery ----------------------------------------------------------------------------------------------
    {
        const float H = LayoutData.PadHalfExtent;
        const float C = LayoutData.CheckerCell > 0.1f ? LayoutData.CheckerCell : 4.0f;
        const int   N = static_cast<int>((2.0f * H) / C);         // cells per side

        // A large plain surround under everything (so the world is not a hole beyond the checker).
        {
            const auto Surround = OpenSpan("Ground");
            const float G = H * 4.0f;
            AppendQuad(Vector3{ -G, -G, -0.02f }, Vector3{ G, -G, -0.02f },
                       Vector3{ G, G, -0.02f },   Vector3{ -G, G, -0.02f }, MaterialSurround, 0.05f);
        }
        // The checker pad on top — alternating light/dark squares give a visible speed/grid reference.
        {
            const auto Checker = OpenSpan("Checker Pad");
            for (int i = 0; i < N; ++i)
                for (int j = 0; j < N; ++j)
                {
                    const float x0 = -H + i * C, x1 = x0 + C;
                    const float y0 = -H + j * C, y1 = y0 + C;
                    const uint32_t M = ((i + j) & 1) ? MaterialCheckerDark : MaterialCheckerLight;
                    AppendQuad(Vector3{ x0, y0, 0.0f }, Vector3{ x1, y0, 0.0f },
                               Vector3{ x1, y1, 0.0f }, Vector3{ x0, y1, 0.0f }, M, 1.0f);
                }
        }
        // Ramp ahead of the spawn.
        {
            const auto Ramp = OpenSpan("Ramp");
            AppendWedge(LayoutData.RampNearX, LayoutData.RampNearX + LayoutData.RampRunX,
                        LayoutData.RampRise, LayoutData.RampHalfWidth, MaterialRamp);
        }
        // Speed bumps behind the spawn.
        for (uint32_t B = 0u; B < LayoutData.BumpCount; ++B)
        {
            char Name[24];
            std::snprintf(Name, sizeof(Name), "Speed Bump %u", B + 1u);
            const auto Bump = OpenSpan(Name);
            const float x = LayoutData.BumpFirstX + static_cast<float>(B) * LayoutData.BumpSpacingX;
            AppendBump(x, LayoutData.BumpHalfWidthY, LayoutData.BumpHeight, MaterialBump);
        }
        // Slalom cones down the +X lane, alternating sides.
        for (uint32_t K = 0u; K < LayoutData.ConeCount; ++K)
        {
            char Name[16];
            std::snprintf(Name, sizeof(Name), "Cone %u", K + 1u);
            const auto Cone = OpenSpan(Name);
            const float x = 18.0f + static_cast<float>(K) * 5.0f;
            const float y = (K & 1) ? -2.4f : 2.4f;
            AppendCone(Vector3{ x, y, 0.0f }, 0.22f, 0.55f, MaterialConeBody, 16u);
        }
    }

    // --- Vehicle (dynamic) — chassis shell, then four wheels, appended LAST -------------------------------------------
    {
        const auto BodySpan = OpenSpan("ControlVehicle", /*Dynamic*/ true);
        AppendBodyShell(ChassisSpawn, MaterialBodyPaint);
    }
    for (uint32_t W = 0u; W < 4u; ++W)
    {
        static const char* kWheelName[4] = { "Wheel FL", "Wheel FR", "Wheel RL", "Wheel RR" };
        const auto WheelSpan = OpenSpan(kWheelName[W], /*Dynamic*/ true);
        AppendWheel(QueryWheelRestOrigin(W), kTyreRadius, kTyreHalfWidth, WheelMaterial[W], MaterialHub, 24u);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   PRIMITIVE APPENDS
//------------------------------------------------------------------------------------------------------------------------

void TractrixDriveScene::AppendTriangle(const Vector3 P[3], const Vector3 N[3], const float Uv[3][2], uint32_t Material) noexcept
{
    TriangleIndex T{};
    T.VertexAlphaX = P[0].x; T.VertexAlphaY = P[0].y; T.VertexAlphaZ = P[0].z;
    T.VertexBetaX  = P[1].x; T.VertexBetaY  = P[1].y; T.VertexBetaZ  = P[1].z;
    T.VertexGammaX = P[2].x; T.VertexGammaY = P[2].y; T.VertexGammaZ = P[2].z;
    std::memcpy(&T.MaterialSlot, &Material, sizeof(Material));
    T.TextureAlphaU = Uv[0][0]; T.TextureAlphaV = Uv[0][1];
    T.TextureBetaU  = Uv[1][0]; T.TextureBetaV  = Uv[1][1];
    T.TextureGammaU = Uv[2][0]; T.TextureGammaV = Uv[2][1];
    Triangles.push_back(T);
    CornerNormals.push_back(N[0]); CornerNormals.push_back(N[1]); CornerNormals.push_back(N[2]);
}

void TractrixDriveScene::AppendQuad(const Vector3& A, const Vector3& B, const Vector3& C, const Vector3& D,
                                    uint32_t Material, float UvScale) noexcept
{
    const Vector3 Cross = OrientationClassifier::CrossProduct(B - A, C - A);
    const float   Len   = Cross.Length();
    const Vector3 N     = Len > 0.0f ? Cross / Len : Vector3{ 0.0f, 0.0f, 1.0f };
    const Vector3 Ns[3] = { N, N, N };
    const float   SizeU = (B - A).Length() * UvScale, SizeV = (D - A).Length() * UvScale;
    const Vector3 P0[3] = { A, B, C }; const float U0[3][2] = { { 0.0f, 0.0f }, { SizeU, 0.0f }, { SizeU, SizeV } };
    const Vector3 P1[3] = { A, C, D }; const float U1[3][2] = { { 0.0f, 0.0f }, { SizeU, SizeV }, { 0.0f, SizeV } };
    AppendTriangle(P0, Ns, U0, Material);
    AppendTriangle(P1, Ns, U1, Material);
}

void TractrixDriveScene::AppendBox(const Vector3& Minimum, const Vector3& Maximum, uint32_t Material) noexcept
{
    const float X0 = Minimum.x, Y0 = Minimum.y, Z0 = Minimum.z;
    const float X1 = Maximum.x, Y1 = Maximum.y, Z1 = Maximum.z;
    AppendQuad(Vector3{ X0, Y0, Z1 }, Vector3{ X1, Y0, Z1 }, Vector3{ X1, Y1, Z1 }, Vector3{ X0, Y1, Z1 }, Material, 1.0f);   // +Z
    AppendQuad(Vector3{ X0, Y1, Z0 }, Vector3{ X1, Y1, Z0 }, Vector3{ X1, Y0, Z0 }, Vector3{ X0, Y0, Z0 }, Material, 1.0f);   // −Z
    AppendQuad(Vector3{ X0, Y0, Z0 }, Vector3{ X1, Y0, Z0 }, Vector3{ X1, Y0, Z1 }, Vector3{ X0, Y0, Z1 }, Material, 1.0f);   // −Y
    AppendQuad(Vector3{ X1, Y1, Z0 }, Vector3{ X0, Y1, Z0 }, Vector3{ X0, Y1, Z1 }, Vector3{ X1, Y1, Z1 }, Material, 1.0f);   // +Y
    AppendQuad(Vector3{ X0, Y1, Z0 }, Vector3{ X0, Y0, Z0 }, Vector3{ X0, Y0, Z1 }, Vector3{ X0, Y1, Z1 }, Material, 1.0f);   // −X
    AppendQuad(Vector3{ X1, Y0, Z0 }, Vector3{ X1, Y1, Z0 }, Vector3{ X1, Y1, Z1 }, Vector3{ X1, Y0, Z1 }, Material, 1.0f);   // +X
}

// A jump ramp: a right-triangular prism whose crest is the far edge (+X). Sloped top, vertical far face, two side walls.
void TractrixDriveScene::AppendWedge(float NearX, float FarX, float Rise, float HalfWidth, uint32_t Material) noexcept
{
    const float y0 = -HalfWidth, y1 = HalfWidth;
    const Vector3 Nl{ NearX, y0, 0.0f }, Nr{ NearX, y1, 0.0f };   // toe on the ground
    const Vector3 Fl{ FarX,  y0, Rise }, Fr{ FarX,  y1, Rise };  // crest
    const Vector3 Gl{ FarX,  y0, 0.0f }, Gr{ FarX,  y1, 0.0f };  // crest base
    AppendQuad(Nl, Nr, Fr, Fl, Material, 1.0f);   // sloped driving surface
    AppendQuad(Fl, Fr, Gr, Gl, Material, 1.0f);   // vertical far face (crest → ground)
    // Two triangular side walls.
    { const Vector3 P[3] = { Nl, Fl, Gl }; const Vector3 N{ 0.0f, -1.0f, 0.0f }; const Vector3 Ns[3] = { N, N, N };
      const float Uv[3][2] = { {0,0},{1,1},{1,0} }; AppendTriangle(P, Ns, Uv, Material); }
    { const Vector3 P[3] = { Nr, Gr, Fr }; const Vector3 N{ 0.0f, 1.0f, 0.0f }; const Vector3 Ns[3] = { N, N, N };
      const float Uv[3][2] = { {0,0},{1,0},{1,1} }; AppendTriangle(P, Ns, Uv, Material); }
}

// A rounded speed bump: a low half-cylinder ridge whose axis runs along Y, spanning ±HalfWidthY.
void TractrixDriveScene::AppendBump(float CentreX, float HalfWidthY, float Height, uint32_t Material) noexcept
{
    const uint32_t Arc = 10u;
    const float R = (Height * Height + 0.55f * 0.55f) / (2.0f * Height);   // circle through (±0.55,0) and (0,Height)
    const float ZC = Height - R;                                            // arc centre below ground
    const float Span = 0.55f;                                               // half-length of the bump in X
    const float A0 = std::asin(-Span / R), A1 = std::asin(Span / R);
    for (uint32_t s = 0u; s < Arc; ++s)
    {
        const float t0 = A0 + (A1 - A0) * (static_cast<float>(s)      / Arc);
        const float t1 = A0 + (A1 - A0) * (static_cast<float>(s + 1u) / Arc);
        const float x0 = CentreX + R * std::sin(t0), z0 = ZC + R * std::cos(t0);
        const float x1 = CentreX + R * std::sin(t1), z1 = ZC + R * std::cos(t1);
        AppendQuad(Vector3{ x0, -HalfWidthY, z0 }, Vector3{ x1, -HalfWidthY, z1 },
                   Vector3{ x1,  HalfWidthY, z1 }, Vector3{ x0,  HalfWidthY, z0 }, Material, 1.0f);
    }
}

void TractrixDriveScene::AppendCone(const Vector3& BaseCentre, float Radius, float Height, uint32_t Material, uint32_t Segments) noexcept
{
    const Vector3 Apex = Vector3{ BaseCentre.x, BaseCentre.y, BaseCentre.z + Height };
    for (uint32_t s = 0u; s < Segments; ++s)
    {
        const float a0 = (2.0f * kPi * s)          / Segments;
        const float a1 = (2.0f * kPi * (s + 1u))   / Segments;
        const Vector3 B0{ BaseCentre.x + Radius * std::cos(a0), BaseCentre.y + Radius * std::sin(a0), BaseCentre.z };
        const Vector3 B1{ BaseCentre.x + Radius * std::cos(a1), BaseCentre.y + Radius * std::sin(a1), BaseCentre.z };
        // Side.
        { const Vector3 P[3] = { B0, B1, Apex };
          Vector3 N = OrientationClassifier::CrossProduct(B1 - B0, Apex - B0); const float L = N.Length();
          N = L > 0.0f ? N / L : Vector3{ 0,0,1 }; const Vector3 Ns[3] = { N, N, N };
          const float Uv[3][2] = { {0,0},{1,0},{0.5f,1} }; AppendTriangle(P, Ns, Uv, Material); }
        // Base (facing −Z).
        { const Vector3 P[3] = { B1, B0, BaseCentre }; const Vector3 N{ 0,0,-1 }; const Vector3 Ns[3] = { N, N, N };
          const float Uv[3][2] = { {0,0},{1,0},{0.5f,0.5f} }; AppendTriangle(P, Ns, Uv, Material); }
    }
}

// A wheel: the tyre is a cylinder about the local +Y (spin) axis; a small hub disc caps each face.
void TractrixDriveScene::AppendWheel(const Vector3& Centre, float Radius, float HalfWidth,
                                     uint32_t TyreMaterial, uint32_t HubMaterial, uint32_t Segments) noexcept
{
    const float yL = Centre.y - HalfWidth, yR = Centre.y + HalfWidth;
    const float HubR = Radius * 0.45f;
    for (uint32_t s = 0u; s < Segments; ++s)
    {
        const float a0 = (2.0f * kPi * s)        / Segments;
        const float a1 = (2.0f * kPi * (s + 1u)) / Segments;
        const float c0 = std::cos(a0), s0 = std::sin(a0), c1 = std::cos(a1), s1 = std::sin(a1);
        // Tread ring (outward normals in the X-Z plane).
        const Vector3 O0{ Centre.x + Radius * c0, 0.0f, Centre.z + Radius * s0 };
        const Vector3 O1{ Centre.x + Radius * c1, 0.0f, Centre.z + Radius * s1 };
        const Vector3 A{ O0.x, yL, O0.z }, B{ O1.x, yL, O1.z }, Cc{ O1.x, yR, O1.z }, Dd{ O0.x, yR, O0.z };
        AppendQuad(A, B, Cc, Dd, TyreMaterial, 1.0f);
        // Hub discs on both faces.
        const Vector3 hub0{ Centre.x + HubR * c0, 0.0f, Centre.z + HubR * s0 };
        const Vector3 hub1{ Centre.x + HubR * c1, 0.0f, Centre.z + HubR * s1 };
        { const Vector3 P[3] = { Vector3{ Centre.x, yR, Centre.z }, Vector3{ hub0.x, yR, hub0.z }, Vector3{ hub1.x, yR, hub1.z } };
          const Vector3 N{ 0,1,0 }; const Vector3 Ns[3] = { N, N, N }; const float Uv[3][2] = { {0.5f,0.5f},{0,0},{1,0} };
          AppendTriangle(P, Ns, Uv, HubMaterial); }
        { const Vector3 P[3] = { Vector3{ Centre.x, yL, Centre.z }, Vector3{ hub1.x, yL, hub1.z }, Vector3{ hub0.x, yL, hub0.z } };
          const Vector3 N{ 0,-1,0 }; const Vector3 Ns[3] = { N, N, N }; const float Uv[3][2] = { {0.5f,0.5f},{1,0},{0,0} };
          AppendTriangle(P, Ns, Uv, HubMaterial); }
        // Tyre sidewalls (ring between hub and tread on each face).
        { const Vector3 P[3] = { Vector3{ hub0.x, yR, hub0.z }, Vector3{ O0.x, yR, O0.z }, Vector3{ O1.x, yR, O1.z } };
          const Vector3 N{ 0,1,0 }; const Vector3 Ns[3] = { N, N, N }; const float Uv[3][2] = { {0,0},{1,0},{1,1} };
          AppendTriangle(P, Ns, Uv, TyreMaterial);
          const Vector3 P2[3] = { Vector3{ hub0.x, yR, hub0.z }, Vector3{ O1.x, yR, O1.z }, Vector3{ hub1.x, yR, hub1.z } };
          AppendTriangle(P2, Ns, Uv, TyreMaterial); }
        { const Vector3 P[3] = { Vector3{ hub0.x, yL, hub0.z }, Vector3{ O1.x, yL, O1.z }, Vector3{ O0.x, yL, O0.z } };
          const Vector3 N{ 0,-1,0 }; const Vector3 Ns[3] = { N, N, N }; const float Uv[3][2] = { {0,0},{1,1},{1,0} };
          AppendTriangle(P, Ns, Uv, TyreMaterial);
          const Vector3 P2[3] = { Vector3{ hub0.x, yL, hub0.z }, Vector3{ hub1.x, yL, hub1.z }, Vector3{ O1.x, yL, O1.z } };
          AppendTriangle(P2, Ns, Uv, TyreMaterial); }
    }
}

// The real ControlVehicle shell (ControlVehicleMesh.inl), translated by Offset, smooth-shaded (per-vertex averaged
//    normals), one material → one instance the vehicle-instance sequence drives.
void TractrixDriveScene::AppendBodyShell(const Vector3& Offset, uint32_t Material) noexcept
{
    using namespace ControlVehicleMesh;
    const uint32_t V = kVertexCount;
    const uint32_t Tn = kTriangleCount;

    std::vector<Vector3> Normal(V, Vector3{ 0.0f, 0.0f, 0.0f });
    auto Position = [&](uint32_t i) -> Vector3
    {
        return Vector3{ kPositions[i * 3u + 0u], kPositions[i * 3u + 1u], kPositions[i * 3u + 2u] };
    };
    for (uint32_t t = 0u; t < Tn; ++t)
    {
        const uint32_t a = kTriangles[t * 3u + 0u], b = kTriangles[t * 3u + 1u], c = kTriangles[t * 3u + 2u];
        const Vector3 Fn = OrientationClassifier::CrossProduct(Position(b) - Position(a), Position(c) - Position(a));
        Normal[a] = Normal[a] + Fn; Normal[b] = Normal[b] + Fn; Normal[c] = Normal[c] + Fn;
    }
    for (uint32_t i = 0u; i < V; ++i)
    {
        const float L = Normal[i].Length();
        Normal[i] = L > 1e-6f ? Normal[i] / L : Vector3{ 0.0f, 0.0f, 1.0f };
    }
    for (uint32_t t = 0u; t < Tn; ++t)
    {
        const uint32_t a = kTriangles[t * 3u + 0u], b = kTriangles[t * 3u + 1u], c = kTriangles[t * 3u + 2u];
        const Vector3 P[3] = { Position(a) + Offset, Position(b) + Offset, Position(c) + Offset };
        const Vector3 N[3] = { Normal[a], Normal[b], Normal[c] };
        const float   Uv[3][2] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f } };
        AppendTriangle(P, N, Uv, Material);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         EXPORT
//------------------------------------------------------------------------------------------------------------------------

bool TractrixDriveScene::Export(const std::string& Path, std::string* Error) const noexcept
{
    SceneEncodeConfiguration Configuration;
    Configuration.Name           = "DriveScene";
    Configuration.CornerNormals  = &CornerNormals;
    Configuration.WriteTexcoords = true;
    Configuration.Spans          = &Spans;
    return SceneCodec::Encode(Path, Triangles, Materials, Error, Configuration);
}

} // namespace Tractrix
} // namespace Frontier
