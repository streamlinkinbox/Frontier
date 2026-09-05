//============================================================================================================================================
//                                                     SHOWROOMSTRUCTURE.CPP
//============================================================================================================================================
// See ShowroomStructure.h. Layout (camera at −Y looking +Y, Z up, metres):
//
//        room      4.0 wide (X ±2.0) · 5.0 deep (Y −2.0 … +3.0) · 3.0 tall (Z 0 … 3.0), open face at −Y
//        walls     left red, right green (Cornell's, so colour bleed reads the same), rear / floor / ceiling white
//        inlay     deep blue floor rectangle + amber strip along the rear base — saturated neighbours for the panel
//        plinth    0.9 × 0.5 × 0.35 dark dielectric, centred under the panel
//        chrome    r = 0.34 sphere on the plinth (metalness 1, roughness 0.08) — reflects the panel back at the eye
//        pillar    0.34 × 0.34 × 1.5 matte column, left rear
//        copper    r = 0.28 rough copper sphere on a short stand, right rear
//        luminaire 1.2 × 1.0 ceiling panel (Cornell's ~32 nit) + a dimmer 0.8 × 0.3 rear strip for rim separation
//
// The interface panel hangs at (0, 1.55, 1.32), tilted ≈ 12° toward the eye — in front of the rear wall, above the
//    chrome sphere, so its own light is visible both directly and in reflection.

#include "ShowroomStructure.h"

#include "../../../Engine/ContentInterchange/SceneCodec.h"
#include "../../../Engine/DeviceExchange/OrientationClassifier.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace Frontier {
namespace ProjectZero {

namespace {

constexpr float kPi = 3.14159265358979f;

MaterialDescriptor MakeMaterial(const char* Name)
{
    MaterialDescriptor D;
    D.Name = Name;
    D.Slabs.emplace_back();
    return D;
}

void SetColour(float* Target, float R, float G, float B)
{
    Target[0] = R; Target[1] = G; Target[2] = B;
}

// Material ordinals — the order they are pushed below.
enum : uint32_t
{
    MaterialWhite     = 0u,
    MaterialRed       = 1u,
    MaterialGreen     = 2u,
    MaterialInlay     = 3u,
    MaterialAmber     = 4u,
    MaterialPlinth    = 5u,
    MaterialChrome    = 6u,
    MaterialPillar    = 7u,
    MaterialCopper    = 8u,
    MaterialLuminaire = 9u,
    MaterialRimLight  = 10u
};

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

Vector3 ShowroomStructure::QueryDropOrigin(uint32_t Ordinal) noexcept
{
    // A loose 3-wide grid dropped onto open FLOOR, in front of the plinth and clear of the chrome sphere.
    //
    //    ⚠️ Do not move these back over the plinth. An earlier layout centred the grid above it, and every body
    //    landed exactly on the plinth's edge, got squeezed sideways and rolled out through the room's open −Y
    //    face — the proof caught them at −814 m. Spheres balanced on a box edge are the least stable contact a
    //    solver can be handed; a flat floor well inside the walls is the honest way to demonstrate settling.
    //
    //    Plinth occupies x ∈ [−0.45, 0.45], y ∈ [1.30, 1.80]. This grid sits at y ∈ [−0.85, 0.95], safely clear.
    //
    //    Laid out as a 3 × 4 grid in the FLOOR PLANE with only a slight height stagger, rather than a 4-high
    //    stack. Stacking them made all twelve land on the same spot as a pile, and the mutual contacts shoved
    //    bodies into the walls hard enough to embed them — the proof caught that too. Spread horizontally, each
    //    body gets its own patch of floor and the pile never forms.
    const uint32_t Column = Ordinal % 3u;          // x: three lanes, 0.62 m apart (radius 0.16 → ample)
    const uint32_t Row    = Ordinal / 3u;          // y: four ranks,  0.60 m apart
    return Vector3{ -0.62f + 0.62f * static_cast<float>(Column),
                    -0.85f + 0.60f * static_cast<float>(Row),
                     0.85f + 0.14f * static_cast<float>(Ordinal % 2u) };
}

void ShowroomStructure::Construct(uint32_t DropBodyCount) noexcept
{
    Triangles.clear();
    CornerNormals.clear();
    Materials.clear();
    FirstDropMaterial = 0u;
    DropCount         = 0u;

    // ── Materials ────────────────────────────────────────────────────────────────────────────────────────────────
    {
        MaterialDescriptor D = MakeMaterial("showroom_white");           // 0 — Cornell's neutral
        SetColour(D.Slabs[0].BaseColor, 0.73f, 0.73f, 0.73f);
        D.Slabs[0].SpecularWeight = 0.0f;
        Materials.push_back(D);

        D = MakeMaterial("showroom_red");                                 // 1
        SetColour(D.Slabs[0].BaseColor, 0.65f, 0.05f, 0.05f);
        D.Slabs[0].SpecularWeight = 0.0f;
        Materials.push_back(D);

        D = MakeMaterial("showroom_green");                               // 2
        SetColour(D.Slabs[0].BaseColor, 0.12f, 0.45f, 0.15f);
        D.Slabs[0].SpecularWeight = 0.0f;
        Materials.push_back(D);

        D = MakeMaterial("floor_inlay_blue");                             // 3 — saturated, semi-gloss
        SetColour(D.Slabs[0].BaseColor, 0.04f, 0.09f, 0.42f);
        D.Slabs[0].SpecularRoughness = 0.22f;
        Materials.push_back(D);

        D = MakeMaterial("accent_amber");                                 // 4
        SetColour(D.Slabs[0].BaseColor, 0.72f, 0.34f, 0.03f);
        D.Slabs[0].SpecularRoughness = 0.35f;
        Materials.push_back(D);

        D = MakeMaterial("plinth_dark");                                  // 5
        SetColour(D.Slabs[0].BaseColor, 0.035f, 0.037f, 0.042f);
        D.Slabs[0].SpecularRoughness = 0.28f;
        Materials.push_back(D);

        D = MakeMaterial("chrome");                                       // 6 — mirrors the panel
        SetColour(D.Slabs[0].BaseColor, 0.92f, 0.93f, 0.95f);
        D.Slabs[0].BaseMetalness     = 1.0f;
        D.Slabs[0].SpecularRoughness = 0.08f;
        Materials.push_back(D);

        D = MakeMaterial("pillar_matte");                                 // 7
        SetColour(D.Slabs[0].BaseColor, 0.52f, 0.50f, 0.47f);
        D.Slabs[0].SpecularWeight        = 0.15f;
        D.Slabs[0].BaseDiffuseRoughness  = 0.8f;
        Materials.push_back(D);

        D = MakeMaterial("copper_rough");                                 // 8
        SetColour(D.Slabs[0].BaseColor, 0.95f, 0.64f, 0.54f);
        D.Slabs[0].BaseMetalness     = 1.0f;
        D.Slabs[0].SpecularRoughness = 0.38f;
        Materials.push_back(D);

        D = MakeMaterial("luminaire");                                    // 9 — Cornell's ceiling panel
        SetColour(D.Slabs[0].BaseColor, 1.0f, 1.0f, 1.0f);
        D.Slabs[0].SpecularWeight    = 0.0f;
        D.Slabs[0].EmissionLuminance = 32.0f;
        Materials.push_back(D);

        D = MakeMaterial("luminaire_rim");                                // 10 — dimmer rear strip
        SetColour(D.Slabs[0].BaseColor, 0.85f, 0.90f, 1.0f);
        D.Slabs[0].SpecularWeight    = 0.0f;
        D.Slabs[0].EmissionLuminance = 9.0f;
        Materials.push_back(D);
    }

    // ── Room shell. Winding faces inward; the −Y face is left open so the camera can look in. ────────────────────
    constexpr float MinX = -2.0f, MaxX = 2.0f;
    constexpr float MinY = -2.0f, MaxY = 3.0f;
    constexpr float MinZ =  0.0f, MaxZ = 3.0f;

    // Floor (+Z up)
    AppendQuad(Vector3{ MinX, MinY, MinZ }, Vector3{ MaxX, MinY, MinZ },
               Vector3{ MaxX, MaxY, MinZ }, Vector3{ MinX, MaxY, MinZ }, MaterialWhite, 0.25f);
    // Ceiling (−Z down)
    AppendQuad(Vector3{ MinX, MaxY, MaxZ }, Vector3{ MaxX, MaxY, MaxZ },
               Vector3{ MaxX, MinY, MaxZ }, Vector3{ MinX, MinY, MaxZ }, MaterialWhite, 0.25f);
    // Rear wall (facing −Y)
    AppendQuad(Vector3{ MinX, MaxY, MinZ }, Vector3{ MaxX, MaxY, MinZ },
               Vector3{ MaxX, MaxY, MaxZ }, Vector3{ MinX, MaxY, MaxZ }, MaterialWhite, 0.25f);
    // Left wall, red (facing +X)
    AppendQuad(Vector3{ MinX, MinY, MinZ }, Vector3{ MinX, MaxY, MinZ },
               Vector3{ MinX, MaxY, MaxZ }, Vector3{ MinX, MinY, MaxZ }, MaterialRed, 0.25f);
    // Right wall, green (facing −X)
    AppendQuad(Vector3{ MaxX, MaxY, MinZ }, Vector3{ MaxX, MinY, MinZ },
               Vector3{ MaxX, MinY, MaxZ }, Vector3{ MaxX, MaxY, MaxZ }, MaterialGreen, 0.25f);

    // ── Floor inlay and rear accent strip, lifted a millimetre to avoid coplanar fighting ────────────────────────
    AppendQuad(Vector3{ -1.25f, 0.10f, 0.001f }, Vector3{ 1.25f, 0.10f, 0.001f },
               Vector3{ 1.25f, 2.60f, 0.001f }, Vector3{ -1.25f, 2.60f, 0.001f }, MaterialInlay, 0.5f);
    AppendQuad(Vector3{ MinX, MaxY - 0.001f, 0.0f }, Vector3{ MaxX, MaxY - 0.001f, 0.0f },
               Vector3{ MaxX, MaxY - 0.001f, 0.06f }, Vector3{ MinX, MaxY - 0.001f, 0.06f }, MaterialAmber, 1.0f);

    // ── Furniture ────────────────────────────────────────────────────────────────────────────────────────────────
    AppendBox(Vector3{ -0.45f, 1.30f, 0.0f },  Vector3{ 0.45f, 1.80f, 0.35f }, MaterialPlinth);   // plinth
    AppendSphere(Vector3{ 0.0f, 1.55f, 0.69f }, 0.34f, MaterialChrome, 28u, 56u);                  // chrome sphere
    AppendBox(Vector3{ -1.75f, 2.25f, 0.0f },  Vector3{ -1.41f, 2.59f, 1.50f }, MaterialPillar);  // matte pillar
    AppendBox(Vector3{ 1.20f, 2.10f, 0.0f },   Vector3{ 1.72f, 2.62f, 0.30f }, MaterialPlinth);   // copper stand
    AppendSphere(Vector3{ 1.46f, 2.36f, 0.58f }, 0.28f, MaterialCopper, 24u, 48u);                 // copper sphere

    // ── Drop bodies, one material each so the codec gives each its own instance ──────────────────────────────────
    // The renderer moves INSTANCES, so two bodies sharing a material would share an instance and could never be
    //    moved apart. Appended before the luminaires to preserve the "emissive quads are last" convention.
    if (DropBodyCount > 0u)
    {
        FirstDropMaterial = static_cast<uint32_t>(Materials.size());
        DropCount         = DropBodyCount;
        for (uint32_t Body = 0u; Body < DropBodyCount; ++Body)
        {
            char Name[32];
            std::snprintf(Name, sizeof(Name), "drop_body_%02u", Body);
            MaterialDescriptor D = MakeMaterial(Name);
            // Cycle three saturated hues so individual bodies stay distinguishable while they tumble.
            switch (Body % 3u)
            {
                case 0u:  SetColour(D.Slabs[0].BaseColor, 0.82f, 0.24f, 0.20f); break;   // red
                case 1u:  SetColour(D.Slabs[0].BaseColor, 0.22f, 0.55f, 0.85f); break;   // blue
                default:  SetColour(D.Slabs[0].BaseColor, 0.92f, 0.72f, 0.18f); break;   // amber
            }
            D.Slabs[0].SpecularWeight = 0.35f;
            D.Slabs[0].SpecularRoughness = 0.35f;
            Materials.push_back(D);

            // Geometry is emitted at the REST pose. Physics then supplies a world matrix relative to it, so the
            //    body must be modelled about its own origin offset — the transform replaces this placement rather
            //    than adding to it, exactly as InstanceMotionSequence assumes.
            AppendSphere(QueryDropOrigin(Body), QueryDropRadius(), FirstDropMaterial + Body, 16u, 32u);
        }
    }

    // ── Luminaires, appended LAST (the convention the Cornell box and shader ball share) ─────────────────────────
    // Ceiling panel, facing down (−Z).
    AppendQuad(Vector3{ -0.60f, 1.00f, MaxZ - 0.002f }, Vector3{ 0.60f, 1.00f, MaxZ - 0.002f },
               Vector3{ 0.60f, 0.00f, MaxZ - 0.002f }, Vector3{ -0.60f, 0.00f, MaxZ - 0.002f }, MaterialLuminaire, 1.0f);
    // Rear rim strip high on the back wall, facing −Y.
    AppendQuad(Vector3{ -0.40f, MaxY - 0.004f, 2.30f }, Vector3{ 0.40f, MaxY - 0.004f, 2.30f },
               Vector3{ 0.40f, MaxY - 0.004f, 2.60f }, Vector3{ -0.40f, MaxY - 0.004f, 2.60f }, MaterialRimLight, 1.0f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    GEOMETRY HELPERS
//------------------------------------------------------------------------------------------------------------------------

void ShowroomStructure::AppendTriangle(const Vector3 P[3], const Vector3 N[3], const float Uv[3][2], uint32_t Material) noexcept
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

void ShowroomStructure::AppendQuad(const Vector3& A, const Vector3& B, const Vector3& C, const Vector3& D,
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

void ShowroomStructure::AppendBox(const Vector3& Minimum, const Vector3& Maximum, uint32_t Material) noexcept
{
    const float X0 = Minimum.x, Y0 = Minimum.y, Z0 = Minimum.z;
    const float X1 = Maximum.x, Y1 = Maximum.y, Z1 = Maximum.z;

    // Outward winding on all six faces.
    AppendQuad(Vector3{ X0, Y0, Z1 }, Vector3{ X1, Y0, Z1 }, Vector3{ X1, Y1, Z1 }, Vector3{ X0, Y1, Z1 }, Material, 1.0f);   // +Z
    AppendQuad(Vector3{ X0, Y1, Z0 }, Vector3{ X1, Y1, Z0 }, Vector3{ X1, Y0, Z0 }, Vector3{ X0, Y0, Z0 }, Material, 1.0f);   // −Z
    AppendQuad(Vector3{ X0, Y0, Z0 }, Vector3{ X1, Y0, Z0 }, Vector3{ X1, Y0, Z1 }, Vector3{ X0, Y0, Z1 }, Material, 1.0f);   // −Y
    AppendQuad(Vector3{ X1, Y1, Z0 }, Vector3{ X0, Y1, Z0 }, Vector3{ X0, Y1, Z1 }, Vector3{ X1, Y1, Z1 }, Material, 1.0f);   // +Y
    AppendQuad(Vector3{ X0, Y1, Z0 }, Vector3{ X0, Y0, Z0 }, Vector3{ X0, Y0, Z1 }, Vector3{ X0, Y1, Z1 }, Material, 1.0f);   // −X
    AppendQuad(Vector3{ X1, Y0, Z0 }, Vector3{ X1, Y1, Z0 }, Vector3{ X1, Y1, Z1 }, Vector3{ X1, Y0, Z1 }, Material, 1.0f);   // +X
}

void ShowroomStructure::AppendSphere(const Vector3& Centre, float Radius, uint32_t Material, uint32_t Rings, uint32_t Segments) noexcept
{
    // UV sphere, poles on ±Z, CCW outward winding — the same construction ShaderBallStructure uses.
    const auto Point = [&](uint32_t Ring, uint32_t Segment, Vector3& P, Vector3& N, float Uv[2])
    {
        const float V     = static_cast<float>(Ring) / static_cast<float>(Rings);
        const float U     = static_cast<float>(Segment) / static_cast<float>(Segments);
        const float Theta = V * kPi, Phi = U * 2.0f * kPi;
        N  = Vector3{ std::sin(Theta) * std::cos(Phi), std::sin(Theta) * std::sin(Phi), std::cos(Theta) };
        P  = Centre + N * Radius;
        Uv[0] = U; Uv[1] = V;
    };

    for (uint32_t Ring = 0u; Ring < Rings; ++Ring)
        for (uint32_t Segment = 0u; Segment < Segments; ++Segment)
        {
            Vector3 P00, P01, P10, P11, N00, N01, N10, N11;
            float   U00[2], U01[2], U10[2], U11[2];
            Point(Ring,      Segment,      P00, N00, U00);
            Point(Ring,      Segment + 1u, P01, N01, U01);
            Point(Ring + 1u, Segment,      P10, N10, U10);
            Point(Ring + 1u, Segment + 1u, P11, N11, U11);

            if (Ring != 0u)
            {
                const Vector3 P[3] = { P00, P10, P01 };
                const Vector3 N[3] = { N00, N10, N01 };
                const float   Uv[3][2] = { { U00[0], U00[1] }, { U10[0], U10[1] }, { U01[0], U01[1] } };
                AppendTriangle(P, N, Uv, Material);
            }
            if (Ring + 1u != Rings)
            {
                const Vector3 P[3] = { P01, P10, P11 };
                const Vector3 N[3] = { N01, N10, N11 };
                const float   Uv[3][2] = { { U01[0], U01[1] }, { U10[0], U10[1] }, { U11[0], U11[1] } };
                AppendTriangle(P, N, Uv, Material);
            }
        }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         EXPORT
//------------------------------------------------------------------------------------------------------------------------

bool ShowroomStructure::Export(const std::string& Path, std::string* Error) const noexcept
{
    SceneEncodeConfiguration Configuration;
    Configuration.Name           = "Showroom";
    Configuration.CornerNormals  = &CornerNormals;
    Configuration.WriteTexcoords = true;
    return SceneCodec::Encode(Path, Triangles, Materials, Error, Configuration);
}

} // namespace ProjectZero
} // namespace Frontier
