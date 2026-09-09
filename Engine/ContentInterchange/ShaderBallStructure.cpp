//============================================================================================================================================
//                                                    SHADERBALLSTRUCTURE.CPP
//============================================================================================================================================
// See ShaderBallStructure.h. Layout (top view, camera at −Y looking +Y):
//
//        row 3 (Y=+2.4)  emission · alpha card · haze 0.3 · haze 0.8 · EON r=0 · EON r=1
//        row 2 (Y=+1.2)  coat 0.0 · coat r0 · coat r0.3 · fuzz 0.5 · fuzz 1.0 · film 0.3µm
//        row 1 (Y= 0.0)  Au · Ag · Cu · Al · Fe(F82 tint) · film on Au
//        row 0 (Y=−1.2)  dielectric roughness 0.0 · 0.2 · 0.4 · 0.6 · 0.8 · 1.0
//                        X = −3, −1.8, −0.6, +0.6, +1.8, +3     spheres r = 0.45 m resting on Z = 0
//
//    The 6×6 m plane is a mid-grey EON diffuse; the 2×2 m luminaire sits at Z = 4 (120 nit → same order as Cornell's 32
//    at 4× the area). Materials are OpenPBR slabs (MaterialSlabDescriptor spec defaults unless set below).

#include "ShaderBallStructure.h"
#include "SceneCodec.h"
#include "../DeviceExchange/OrientationClassifier.h"
#include <cmath>
#include <cstring>

namespace Frontier {

namespace {

constexpr float kPi = 3.14159265358979f;

MaterialDescriptor MakeMaterial(const char* Name)
{
    MaterialDescriptor D; D.Name = Name; D.Slabs.emplace_back(); return D;
}

void SetColor(float* Target, float R, float G, float B) { Target[0] = R; Target[1] = G; Target[2] = B; }

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                        CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

void ShaderBallStructure::Construct() noexcept
{
    Triangles.clear(); CornerNormals.clear(); Materials.clear();

    // ── Materials (index = order below) ──────────────────────────────────────────────────────────────────────────────
    {   // 0 floor
        MaterialDescriptor D = MakeMaterial("floor");
        SetColor(D.Slabs[0].BaseColor, 0.45f, 0.45f, 0.45f); D.Slabs[0].SpecularWeight = 0.0f; D.Slabs[0].SpecularRoughness = 1.0f;
        Materials.push_back(D);
    }
    // 1–6 dielectric roughness ramp (base 0.6 0.1 0.1)
    for (int I = 0; I < 6; ++I)
    {
        MaterialDescriptor D = MakeMaterial(("dielectric_r" + std::to_string(I * 2)).c_str());
        SetColor(D.Slabs[0].BaseColor, 0.6f, 0.1f, 0.1f); D.Slabs[0].SpecularRoughness = 0.2f * static_cast<float>(I);
        Materials.push_back(D);
    }
    // 7–12 metals: base colour = F0, specular colour = F82 tint (Gulbrandsen-style fits, linear Rec.709)
    {
        struct Metal { const char* Name; float F0[3]; float F82[3]; float Roughness; };
        const Metal Metals[6] = {
            { "gold",     { 1.000f, 0.766f, 0.336f }, { 1.00f, 0.96f, 0.82f }, 0.25f },
            { "silver",   { 0.972f, 0.960f, 0.915f }, { 1.00f, 1.00f, 1.00f }, 0.15f },
            { "copper",   { 0.955f, 0.638f, 0.538f }, { 1.00f, 0.95f, 0.90f }, 0.30f },
            { "aluminium",{ 0.913f, 0.922f, 0.924f }, { 1.00f, 1.00f, 1.00f }, 0.35f },
            { "iron_f82", { 0.560f, 0.570f, 0.580f }, { 0.50f, 0.50f, 0.50f }, 0.40f },   // strong F82 tint: grazing edge darkens
            { "gold_film",{ 1.000f, 0.766f, 0.336f }, { 1.00f, 0.96f, 0.82f }, 0.10f } };
        for (const Metal& M : Metals)
        {
            MaterialDescriptor D = MakeMaterial(M.Name);
            SetColor(D.Slabs[0].BaseColor, M.F0[0], M.F0[1], M.F0[2]); SetColor(D.Slabs[0].SpecularColor, M.F82[0], M.F82[1], M.F82[2]);
            D.Slabs[0].BaseMetalness = 1.0f; D.Slabs[0].SpecularRoughness = M.Roughness;
            if (std::strcmp(M.Name, "gold_film") == 0) { D.Slabs[0].ThinFilmWeight = 1.0f; D.Slabs[0].ThinFilmThickness = 0.35f; D.Slabs[0].ThinFilmIor = 1.5f; }
            Materials.push_back(D);
        }
    }
    // 13–18 coat / fuzz / film on dielectric
    {
        MaterialDescriptor D = MakeMaterial("blue_no_coat");
        SetColor(D.Slabs[0].BaseColor, 0.05f, 0.15f, 0.6f); D.Slabs[0].SpecularRoughness = 0.5f; Materials.push_back(D);
        D = MakeMaterial("blue_coat_r0");
        SetColor(D.Slabs[0].BaseColor, 0.05f, 0.15f, 0.6f); D.Slabs[0].SpecularRoughness = 0.5f; D.Slabs[0].CoatWeight = 1.0f; Materials.push_back(D);
        D = MakeMaterial("blue_coat_r03");
        SetColor(D.Slabs[0].BaseColor, 0.05f, 0.15f, 0.6f); D.Slabs[0].SpecularRoughness = 0.5f; D.Slabs[0].CoatWeight = 1.0f; D.Slabs[0].CoatRoughness = 0.3f; Materials.push_back(D);
        D = MakeMaterial("velvet_fuzz_05");
        SetColor(D.Slabs[0].BaseColor, 0.35f, 0.02f, 0.08f); D.Slabs[0].SpecularWeight = 0.2f; D.Slabs[0].FuzzWeight = 0.5f; D.Slabs[0].FuzzRoughness = 0.5f; Materials.push_back(D);
        D = MakeMaterial("velvet_fuzz_10");
        SetColor(D.Slabs[0].BaseColor, 0.35f, 0.02f, 0.08f); D.Slabs[0].SpecularWeight = 0.2f; D.Slabs[0].FuzzWeight = 1.0f; D.Slabs[0].FuzzRoughness = 0.8f; SetColor(D.Slabs[0].FuzzColor, 1.0f, 0.9f, 0.9f); Materials.push_back(D);
        D = MakeMaterial("soap_film_03");
        SetColor(D.Slabs[0].BaseColor, 0.02f, 0.02f, 0.02f); D.Slabs[0].SpecularRoughness = 0.05f; D.Slabs[0].ThinFilmWeight = 1.0f; D.Slabs[0].ThinFilmThickness = 0.3f; Materials.push_back(D);
    }
    // 19–24 emission / alpha card / haziness / EON
    {
        MaterialDescriptor D = MakeMaterial("emitter_sphere");
        SetColor(D.Slabs[0].BaseColor, 0.0f, 0.0f, 0.0f); D.Slabs[0].SpecularWeight = 0.0f; D.Slabs[0].EmissionLuminance = 8.0f; SetColor(D.Slabs[0].EmissionColor, 1.0f, 0.6f, 0.3f); Materials.push_back(D);
        D = MakeMaterial("alpha_card");   // ⚠️ no texture: geometry_opacity 0.3 < cutoff ⇒ fully cut out; proves the mask path without content
        SetColor(D.Slabs[0].BaseColor, 0.2f, 0.7f, 0.2f); D.Slabs[0].GeometryOpacity = 0.3f; D.Flags = MaterialFlagAlphaMask | MaterialFlagDoubleSided; D.AlphaCutoff = 0.5f; Materials.push_back(D);
        D = MakeMaterial("haze_03");
        SetColor(D.Slabs[0].BaseColor, 0.1f, 0.1f, 0.1f); D.Slabs[0].SpecularRoughness = 0.1f; D.Slabs[0].SlateHazinessWeight = 0.3f; D.Slabs[0].SlateHazinessRoughness = 0.6f; Materials.push_back(D);
        D = MakeMaterial("haze_08");
        SetColor(D.Slabs[0].BaseColor, 0.1f, 0.1f, 0.1f); D.Slabs[0].SpecularRoughness = 0.1f; D.Slabs[0].SlateHazinessWeight = 0.8f; D.Slabs[0].SlateHazinessRoughness = 0.8f; Materials.push_back(D);
        D = MakeMaterial("eon_r0");
        SetColor(D.Slabs[0].BaseColor, 0.8f, 0.7f, 0.5f); D.Slabs[0].SpecularWeight = 0.0f; D.Slabs[0].BaseDiffuseRoughness = 0.0f; Materials.push_back(D);
        D = MakeMaterial("eon_r1");
        SetColor(D.Slabs[0].BaseColor, 0.8f, 0.7f, 0.5f); D.Slabs[0].SpecularWeight = 0.0f; D.Slabs[0].BaseDiffuseRoughness = 1.0f; Materials.push_back(D);
    }
    {   // 25 luminaire (last)
        MaterialDescriptor D = MakeMaterial("luminaire");
        SetColor(D.Slabs[0].BaseColor, 1.0f, 1.0f, 1.0f); D.Slabs[0].SpecularWeight = 0.0f; D.Slabs[0].EmissionLuminance = 120.0f; Materials.push_back(D);
    }

    // ── Geometry ─────────────────────────────────────────────────────────────────────────────────────────────────────
    AppendQuad(Vector3{ -4.0f, -3.0f, 0.0f }, Vector3{ 4.0f, -3.0f, 0.0f }, Vector3{ 4.0f, 4.0f, 0.0f }, Vector3{ -4.0f, 4.0f, 0.0f }, 0u, 0.25f);

    constexpr float Radius = 0.45f;
    uint32_t Material = 1u;
    for (int Row = 0; Row < 4; ++Row)
        for (int Column = 0; Column < 6; ++Column, ++Material)
        {
            const Vector3 Centre{ -3.0f + 1.2f * static_cast<float>(Column), -1.2f + 1.2f * static_cast<float>(Row), Radius };
            if (Material == 20u)   // alpha card: a vertical 0.9 m quad facing the camera instead of a sphere
            {
                AppendQuad(Vector3{ Centre.x - 0.45f, Centre.y, 0.0f }, Vector3{ Centre.x + 0.45f, Centre.y, 0.0f },
                           Vector3{ Centre.x + 0.45f, Centre.y, 0.9f }, Vector3{ Centre.x - 0.45f, Centre.y, 0.9f }, Material, 1.0f);
                continue;
            }
            AppendSphere(Centre, Radius, Material, 24u, 48u);
        }

    // Luminaire: 2×2 m at Z = 4 facing down (−Z) — LAST.
    AppendQuad(Vector3{ -1.0f, 1.6f, 4.0f }, Vector3{ 1.0f, 1.6f, 4.0f }, Vector3{ 1.0f, -0.4f, 4.0f }, Vector3{ -1.0f, -0.4f, 4.0f }, 25u, 1.0f);
}

void ShaderBallStructure::AppendTriangle(const Vector3 P[3], const Vector3 N[3], const float Uv[3][2], uint32_t Material) noexcept
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

void ShaderBallStructure::AppendQuad(const Vector3& A, const Vector3& B, const Vector3& C, const Vector3& D, uint32_t Material, float UvScale) noexcept
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

void ShaderBallStructure::AppendSphere(const Vector3& Centre, float Radius, uint32_t Material, uint32_t Rings, uint32_t Segments) noexcept
{
    // UV sphere, poles on ±Z, CCW outward winding, u = longitude / 2π, v = latitude from the north pole.
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
            Vector3 P00, P01, P10, P11, N00, N01, N10, N11; float U00[2], U01[2], U10[2], U11[2];
            Point(Ring,      Segment,      P00, N00, U00);
            Point(Ring,      Segment + 1u, P01, N01, U01);
            Point(Ring + 1u, Segment,      P10, N10, U10);
            Point(Ring + 1u, Segment + 1u, P11, N11, U11);
            if (Ring != 0u)            { const Vector3 P[3] = { P00, P10, P01 }; const Vector3 N[3] = { N00, N10, N01 }; const float Uv[3][2] = { { U00[0], U00[1] }, { U10[0], U10[1] }, { U01[0], U01[1] } }; AppendTriangle(P, N, Uv, Material); }
            if (Ring + 1u != Rings)    { const Vector3 P[3] = { P01, P10, P11 }; const Vector3 N[3] = { N01, N10, N11 }; const float Uv[3][2] = { { U01[0], U01[1] }, { U10[0], U10[1] }, { U11[0], U11[1] } }; AppendTriangle(P, N, Uv, Material); }
        }
}

bool ShaderBallStructure::Export(const std::string& Path, std::string* Error) const noexcept
{
    SceneEncodeConfiguration Configuration;
    Configuration.Name = "ShaderBall"; Configuration.CornerNormals = &CornerNormals; Configuration.WriteTexcoords = true;
    return SceneCodec::Encode(Path, Triangles, Materials, Error, Configuration);
}

} // namespace Frontier
