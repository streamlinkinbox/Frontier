// CPU reference exhibit for the solid-glass medium fix.
//
// This deliberately reuses the shipped ShaderballPreview.cpp and therefore the shipped
// MaterialEvaluation.slang BSDF compiled as C++. It is not a replacement for a GPU ReSTIR
// capture: it is a small, deterministic scene that makes the glass-on-aluminum transport
// visible while the sandbox lacks the Slang/Vulkan toolchain.
#define SHADERBALL_PREVIEW_LIB
#include "../../../Engine/ContentInterchange/ShaderballPreview.cpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{

constexpr float kPi = 3.14159265358979323846f;

void AddSmoothTri(const vec3& A, const vec3& B, const vec3& C, const vec3& Center, int Mat)
{
    size_t Base = g_Tris.size();
    AddTri(A, B, C, Mat);
    Tri& T = g_Tris[Base];
    T.Na = normalize(A - Center);
    T.Nb = normalize(B - Center);
    T.Nc = normalize(C - Center);
}

void AddSphere(const vec3& Center, float Radius, int Mat, int Segments = 40, int Rings = 24)
{
    for (int Y = 0; Y < Rings; ++Y)
    {
        float V0 = static_cast<float>(Y) / Rings;
        float V1 = static_cast<float>(Y + 1) / Rings;
        float P0 = kPi * V0;
        float P1 = kPi * V1;
        for (int X = 0; X < Segments; ++X)
        {
            float U0 = 2.0f * kPi * static_cast<float>(X) / Segments;
            float U1 = 2.0f * kPi * static_cast<float>(X + 1) / Segments;
            auto Point = [&](float P, float U)
            {
                return Center + Radius * vec3(std::sin(P) * std::cos(U),
                                              std::sin(P) * std::sin(U),
                                              std::cos(P));
            };
            vec3 A = Point(P0, U0), B = Point(P0, U1), C = Point(P1, U1), D = Point(P1, U0);
            if (Y != 0) AddSmoothTri(A, B, D, Center, Mat);
            if (Y != Rings - 1) AddSmoothTri(B, C, D, Center, Mat);
        }
    }
}

void AddQuad(const vec3& A, const vec3& B, const vec3& C, const vec3& D, int Mat)
{
    AddTri(A, B, C, Mat);
    AddTri(A, C, D, Mat);
}

void AddVerticalPanel(float Y, float XHalf, float Z0, float Z1, int Mat)
{
    // Winding gives a normal toward the camera at negative Y.
    AddQuad(vec3(-XHalf, Y, Z0), vec3(XHalf, Y, Z0),
            vec3(XHalf, Y, Z1), vec3(-XHalf, Y, Z1), Mat);
}

ShadingRecord Matte(const vec3& Color, float Roughness)
{
    ShadingRecord M = StandardMaterial(Color, Roughness);
    M.SpecularWeight = 0.15f;
    M.SpecularColor = vec3(1.0f);
    M.DiffuseRoughness = 0.8f;
    return M;
}

ShadingRecord RedSolidGlass(float Ior)
{
    ShadingRecord M = StandardMaterial(vec3(0.0f), 0.045f);
    M.SpecularIor = Ior;
    M.TransmissionWeight = 1.0f;
    M.TransmissionColor = vec3(0.72f, 0.012f, 0.018f);
    M.TransmissionDepth = 1.1f;
    M.TransmissionThickness = 1.6f;
    M.Selection = kReflectanceTransmissive;
    return M;
}

ShadingRecord Aluminum()
{
    ShadingRecord M = StandardMaterial(vec3(0.82f, 0.86f, 0.92f), 0.055f);
    M.Metalness = 1.0f;
    M.SpecularWeight = 1.0f;
    M.SpecularColor = vec3(0.92f, 0.95f, 1.0f);
    M.BaseColor = vec3(0.82f, 0.86f, 0.92f);
    M.ThinFilmWeight = 0.0f;
    return M;
}

ShadingRecord Emissive(const vec3& Color)
{
    ShadingRecord M = StandardMaterial(vec3(0.0f), 1.0f);
    M.Emission = Color;
    M.SpecularWeight = 0.0f;
    M.Selection = kReflectanceUnlit;
    return M;
}

void ResetScene(float Ior)
{
    g_Tris.clear();
    g_Nodes.clear();
    g_Order.clear();
    g_Lights.clear();
    for (ShadingRecord& M : g_Mats) M = StandardMaterial(vec3(0.0f), 1.0f);

    g_Mats[0] = RedSolidGlass(Ior); // red solid dielectric; Radiance's medium stack watches slot 0
    g_Mats[1] = Aluminum();         // aluminum reflector
    g_Mats[2] = Matte(vec3(0.11f, 0.13f, 0.17f), 0.72f);
    g_Mats[3] = Matte(vec3(0.70f, 0.72f, 0.76f), 0.55f);
    g_Mats[4] = Emissive(vec3(9.0f, 10.0f, 12.0f));
    g_Mats[5] = Emissive(vec3(8.0f, 0.03f, 0.02f));
    g_SolidBall = true;

    // Large aluminum panel behind the glass. The glass sphere is in front of it, so a camera ray
    // can see both the sphere directly and its reflected image in the metal.
    AddVerticalPanel(2.35f, 3.55f, 0.03f, 4.25f, 1);
    // Dark floor, a side wall, and a narrow light-colored strip make the reflected silhouette readable.
    AddQuad(vec3(-5.0f, -3.0f, 0.0f), vec3(5.0f, -3.0f, 0.0f),
            vec3(5.0f, 3.0f, 0.0f), vec3(-5.0f, 3.0f, 0.0f), 2);
    AddVerticalPanel(2.30f, 3.55f, 0.55f, 0.75f, 3);

    AddSphere(vec3(-1.05f, 0.35f, 1.15f), 0.88f, 0);

    AddSoftbox(vec3(-3.0f, -2.4f, 4.8f), vec3(0.0f, 0.7f, 1.1f) - vec3(-3.0f, -2.4f, 4.8f),
               vec3(0.0f, 0.0f, 1.0f), 1.5f, 1.0f, vec3(16.0f, 16.5f, 18.0f), 4);
    AddSoftbox(vec3(3.0f, -0.5f, 3.4f), vec3(0.0f, 1.0f, 1.0f) - vec3(3.0f, -0.5f, 3.4f),
               vec3(0.0f, 0.0f, 1.0f), 0.75f, 1.2f, vec3(9.0f, 11.0f, 15.0f), 4);
    AddSoftbox(vec3(0.0f, 3.6f, 4.8f), vec3(0.0f, 1.0f, 1.0f) - vec3(0.0f, 3.6f, 4.8f),
               vec3(0.0f, 0.0f, 1.0f), 1.8f, 0.55f, vec3(10.0f, 8.0f, 6.0f), 4);

    g_Order.resize(g_Tris.size());
    for (size_t I = 0; I < g_Tris.size(); ++I) g_Order[I] = static_cast<int>(I);
    g_Nodes.emplace_back();
    BuildBvh(0, 0, static_cast<int>(g_Tris.size()));
}

Camera MakeCamera(float Y, float X, float Z, const vec3& Look, float Aspect)
{
    Camera C;
    C.O = vec3(X, Y, Z);
    C.F = normalize(Look - C.O);
    C.R = normalize(cross(C.F, vec3(0.0f, 0.0f, 1.0f)));
    C.U = normalize(cross(C.R, C.F));
    C.TanHalf = std::tan(18.0f * kPi / 180.0f);
    C.Aspect = Aspect;
    return C;
}

void RenderFilm(const Camera& C, int Width, int Height, int Spp, std::vector<float>& Film, int SeedTag)
{
    Film.assign(static_cast<size_t>(Width) * Height * 3u, 0.0f);
    unsigned Threads = std::thread::hardware_concurrency();
    if (Threads == 0u) Threads = 2u;
    Threads = std::min(Threads, 8u);
    for (int Frame = 0; Frame < Spp; ++Frame)
    {
        std::vector<std::thread> Pool;
        for (unsigned Th = 0u; Th < Threads; ++Th)
            Pool.emplace_back([&, Th]()
            {
                for (int Y = static_cast<int>(Th); Y < Height; Y += static_cast<int>(Threads))
                    for (int X = 0; X < Width; ++X)
                    {
                        uint32_t Seed = (static_cast<uint32_t>(SeedTag) + 1u) * 73856093u
                                      ^ (static_cast<uint32_t>(Frame) + 1u) * 19349663u
                                      ^ (static_cast<uint32_t>(Y * Width + X) + 1u) * 83492791u;
                        Rng R(Seed);
                        float Sx = ((static_cast<float>(X) + R.Next()) / Width * 2.0f - 1.0f) * C.TanHalf * C.Aspect;
                        float Sy = (1.0f - (static_cast<float>(Y) + R.Next()) / Height * 2.0f) * C.TanHalf;
                        vec3 D = normalize(C.F + C.R * Sx + C.U * Sy);
                        vec3 L = Radiance(C.O, D, R);
                        float* P = &Film[(static_cast<size_t>(Y) * Width + X) * 3u];
                        P[0] += L.x; P[1] += L.y; P[2] += L.z;
                    }
            });
        for (auto& T : Pool) T.join();
    }
    float Inv = 1.0f / static_cast<float>(Spp);
    for (float& V : Film) V *= Inv;
}

float AcesEncode(float X)
{
    float Y = (X * (2.51f * X + 0.03f)) / (X * (2.43f * X + 0.59f) + 0.14f);
    return clamp(Y, 0.0f, 1.0f);
}

bool WriteFilm(const char* Path, const std::vector<float>& Film, int Width, int Height, float Exposure = 1.0f)
{
    std::vector<unsigned char> Pixels(static_cast<size_t>(Width) * Height * 3u, 0u);
    for (int Y = 0; Y < Height; ++Y)
        for (int X = 0; X < Width; ++X)
            for (int Ch = 0; Ch < 3; ++Ch)
            {
                float V = AcesEncode(Film[(static_cast<size_t>(Y) * Width + X) * 3u + Ch] * Exposure);
                Pixels[(static_cast<size_t>(Y) * Width + X) * 3u + Ch] =
                    static_cast<unsigned char>(std::pow(V, 1.0f / 2.2f) * 255.0f + 0.5f);
            }
    return PngWriteCounterpart::WritePng(Path, Width, Height, 3, Pixels.data(), Width * 3) != 0;
}

bool WriteComparison(const char* Path, const std::vector<std::vector<float>>& Films,
                     int Width, int Height, int Gap, float Exposure)
{
    int SheetW = static_cast<int>(Films.size()) * Width + (static_cast<int>(Films.size()) - 1) * Gap;
    std::vector<unsigned char> Pixels(static_cast<size_t>(SheetW) * Height * 3u, 8u);
    for (size_t Panel = 0; Panel < Films.size(); ++Panel)
        for (int Y = 0; Y < Height; ++Y)
            for (int X = 0; X < Width; ++X)
                for (int Ch = 0; Ch < 3; ++Ch)
                {
                    float V = AcesEncode(Films[Panel][(static_cast<size_t>(Y) * Width + X) * 3u + Ch] * Exposure);
                    Pixels[(static_cast<size_t>(Y) * SheetW + static_cast<int>(Panel) * (Width + Gap) + X) * 3u + Ch] =
                        static_cast<unsigned char>(std::pow(V, 1.0f / 2.2f) * 255.0f + 0.5f);
                }
    return PngWriteCounterpart::WritePng(Path, SheetW, Height, 3, Pixels.data(), SheetW * 3) != 0;
}

} // namespace

int main(int Argc, char** Argv)
{
    int Size = 384;
    int Spp = 128;
    std::string OutDir = "Exhibits/Gallery/Materials";
    if (Argc > 1) Size = std::atoi(Argv[1]);
    if (Argc > 2) Spp = std::atoi(Argv[2]);
    if (Argc > 3) OutDir = Argv[3];

    Frontier::ShadingTableSet Tables = Frontier::ShadingTableCodec::Bake(1024u);
    g_Tables = &Tables;

    const float Aspect = 1.5f;
    const int W = Size;
    const int H = static_cast<int>(std::round(Size / Aspect));

    // Primary proof: one 3/4 view where the aluminum panel reflects the red solid-glass sphere.
    ResetScene(1.50f);
    Camera Main = MakeCamera(-7.2f, 3.2f, 3.0f, vec3(0.0f, 1.20f, 1.25f), Aspect);
    std::vector<float> MainFilm;
    RenderFilm(Main, W, H, Spp, MainFilm, 11);
    std::string MainPath = OutDir + "/GlassOnAluminum_IOR1p50_CPUReference.png";
    if (!WriteFilm(MainPath.c_str(), MainFilm, W, H, 1.0f)) return 2;

    // Comparison proof: the same scene at three IOR values. The red volume is held fixed, so
    // the change in edge reflection is Fresnel/IOR, not a material-color change.
    std::vector<std::vector<float>> Comparison;
    const float Iors[3] = { 1.10f, 1.50f, 2.40f };
    for (int I = 0; I < 3; ++I)
    {
        ResetScene(Iors[I]);
        Camera C = MakeCamera(-7.2f, 3.2f, 3.0f, vec3(0.0f, 1.20f, 1.25f), Aspect);
        std::vector<float> Film;
        RenderFilm(C, W, H, Spp, Film, 31 + I);
        Comparison.push_back(std::move(Film));
    }
    std::string ComparisonPath = OutDir + "/GlassOnAluminum_FresnelIOR_Comparison.png";
    if (!WriteComparison(ComparisonPath.c_str(), Comparison, W, H, 4, 1.0f)) return 3;

    std::printf("[GlassAluminumProof] wrote %s\n", MainPath.c_str());
    std::printf("[GlassAluminumProof] wrote %s\n", ComparisonPath.c_str());
    std::printf("[GlassAluminumProof] scene: red solid glass in front of aluminum; IOR comparison 1.10 / 1.50 / 2.40\n");
    std::printf("[GlassAluminumProof] BSDF source: Engine/Shaders/MaterialEvaluation.slang compiled as C++\n");
    return 0;
}
