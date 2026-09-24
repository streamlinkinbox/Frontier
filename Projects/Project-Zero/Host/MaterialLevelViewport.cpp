//============================================================================================================================================
// 📦 Projects/Project-Zero/Host/MaterialLevelViewport.cpp — the material library level, rendered by the Project-Zero CPU stack
//============================================================================================================================================
// 🖼️ What the Vulkan build draws when it opens `--scene materials`, drawn on the CPU instead: the SAME level (built by
//    Engine/ContentInterchange/MaterialSwatchStructure — the same Construct the app exports to Materials.gltf; the
//    export → decode round trip between authoring and the GPU import is the M10 gate's A/E checks, 65/65 zero drift),
//    the SAME material records (MaterialIndex's MaterialRecord / MaterialSlabRecord at the same Tier A slab limit,
//    resolved to a ShadingRecord by the transcription of ReSTIRViewport.slang's `ResolveMaterial` below), the SAME BSDF (Engine/Shaders/MaterialEvaluation.slang compiled 1:1 as C++, FRONTIER_CPU_PORT), the SAME
//    sky (Project-Zero's own SkySpecification/FogSpecification core, at the product's 17.93 h sunset staging), the SAME
//    camera (GameExecution's materials branch: (0, −5, 2.6), pitch −13°, FoV 55°), and the SAME tone map
//    (Engine/DisplayPresentation/ColourTransfer.h — the engine's single definition of linear → display).
//
//    What it is NOT is the ReSTIR kernel: no reservoirs, no reuse, no à-trous. Those are variance machinery, and a CPU
//    render can afford the samples the GPU cannot, so the picture here is the converged answer the GPU's ReSTIR+denoiser
//    is trying to estimate. Physics for physics the two agree — the same shader text turns the same lights into the same
//    radiance — so this is the honest stand-in for "what the level looks like", with the GPU's own frame being noisier
//    (or smoother, with the denoiser on), never different.
//
//    Deviations, all of them display-side and all deliberate (no GPU, no window, no Vulkan here):
//      · no lens flare, no panel post (the sky core's SkyPostApply vignette/flare chain) — those are the viewer's
//        post-process, not the level's radiance;
//      · no fog march / aerial perspective (the studio is 3–15 m deep and the level's own fog scenario is Clear);
//      · textures are not bound — the level is constants-only by design (the M10 gate prints the four texture-shaped
//        channels as acknowledged gaps), so every SampleChannel call resolves to its documented constant;
//      · one medium at a time while walking glass (M4b's v1 rule: no nested dielectrics), which the level never hits.
//
//    KEPT ON PURPOSE — the pure path tracer. Without `--restir` this file is a plain brute-force estimator: NEE with
//    power-heuristic MIS, BSDF sampling, the 8.0 firefly clamp, no reservoirs, no reuse, no history. It is not dead
//    code and is not to be removed when the ReSTIR path grows: it is the reference oracle. Every ReSTIR/denoiser number
//    in this repository is measured against it (the driver's ① panel is 192 spp of exactly this path, and the RMSE
//    table against it is the sheet's claim), and it stays available for whatever cross-check the GPU side needs later —
//    a converged image to A/B against the Vulkan frame, a per-material truth render, or a variance baseline for a new
//    reuse rule. It is also the bit-stability floor: 480×270 at 80 spp reproduces MaterialLibrary_Wide.png to a single
//    pixel (AE = 1, the standing PNG-metadata difference), which is how a change to the shared code is shown not to
//    have touched the physics.
//
//    Build/run: Projects/Project-Zero/Host/Makefile target `MaterialLevelViewport`, or
//    `Exhibits/Workbench/Materials/RunMaterialLevelViewport.sh` (which seats the third-party headers, renders the kept
//    sheets and prints their sha256).
//
//    Usage: MaterialLevelViewport [--out <file.png>] [--width 960] [--height 540] [--spp 64]
//                                 [--bounce 6] [--sun 17.93] [--fog clear|morning|backlit] [--view default|wide|glass|row5]
//                                 [--exposure 1.05] [--threads 2]

#include "SlangCpuShim.h"
#include "ShadingTableCodec.h"
#include "AtrousDenoiseMirror.h"

#include <algorithm>
#include <cmath>
#include <clocale>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

const Frontier::ShadingTableSet* g_Tables = nullptr;

} // namespace

inline vec3 FetchEnergy(float mu, float alpha)
{
    float Out[3] = { 0.0f, 0.0f, 0.0f };
    Frontier::ShadingTableCodec::SampleEnergy(*g_Tables, mu, alpha, Out);
    return vec3(Out[0], Out[1], Out[2]);
}

inline vec3 FetchSheen(float mu, float alpha)
{
    float Out[3] = { 0.0f, 0.0f, 0.0f };
    Frontier::ShadingTableCodec::SampleSheen(*g_Tables, mu, alpha, Out);
    return vec3(Out[0], Out[1], Out[2]);
}

inline vec4 FetchSheenFull(float mu, float alpha)
{
    float Out[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    Frontier::ShadingTableCodec::SampleSheenFull(*g_Tables, mu, alpha, Out);
    return vec4(Out[0], Out[1], Out[2], Out[3]);
}

#define FRONTIER_AUTOMOTIVE_SHOWCASE 1
#include "MaterialEvaluation.slang"   // the shipped OpenPBR lobe set, compiled 1:1 as C++ (see the file's own header)

// ── The spatial interface's figure evaluation, compiled 1:1 as C++ (the R4b discipline, same as the lobe set) ──────
// InterfaceSignedDistance.slang needs three GLSL builtins the material shim never met (GLSL's two-argument atan is
//    C's atan2; fract has no C name; floor gets a float overload for the same reason the shim gives sqrt one).
inline float atan(float y, float x) { return std::atan2(y, x); }
inline float fract(float x)         { return x - std::floor(x); }
inline float floor(float x)         { return std::floor(x); }
inline double abs(double x)         { return x < 0.0 ? -x : x; }   // un-suffixed literals promote; keep the call exact
#include "InterfaceSignedDistance.slang"

#include "PngWriteCounterpart.h"

#include "CameraProjection.h"
#include "ColourTransfer.h"
#include "MaterialIndex.h"
#include "MaterialSwatchStructure.h"
#include "ShowcaseStructure.h"
#include "SkyFogIntegrator.h"

// The spatial interface: the showcase carries Project-Zero's trial panel as a physical exhibit, so the CPU render
//    proves BOTH widget kinds end to end — Type 1 overlay figures (needle, ticks, readout: drawn, never lighting)
//    and Type 2 HMI figures (telltale, toggle LED, lit fills: drawn AND feeding the panel's scene luminaire).
#include "MotionIntegrator.h"
#include "InterfaceTrialSequence.h"
#include "../../../Engine/SpatialInterface/InterfaceLayoutCodec.h"
#include "../../../Engine/SpatialInterface/InterfaceLightProjection.h"
#include "../../../Engine/GeometricRaster/SceneStructure.h"

namespace {

using Frontier::ColourPipeline;
using Frontier::ColourTransfer;
using Frontier::MaterialFlagAlphaMask;
using Frontier::MaterialRecord;
using Frontier::MaterialSlabRecord;
using Frontier::MaterialSwatchStructure;
using Frontier::ShowcaseStructure;
using Frontier::MaterialFlagThinWalled;
using Frontier::MaterialIndex;

//------------------------------------------------------------------------------------------------------------------------
//                                                    RENDER SCENE
//------------------------------------------------------------------------------------------------------------------------
// One flat triangle list, one BVH, one material table — the same three things the GPU build hands the kernel, just in
//    CPU memory. Positions come world-space from the level's facet soup and the normals are the three authored corner
//    normals per triangle, which is what the kernel interpolates at a hit from its vertex/index path.

struct RenderTriangle
{
    vec3 P0, P1, P2;      // [m]  world-space corners
    vec3 N0, N1, N2;      // [-]  vertex normals (smooth; the level's CornerNormals)
    float U0, V0, U1, V1, U2, V2;
    int  Material;        // [idx] material record slot
    int  Light;           // [idx] emissive-triangle list entry, -1 when the triangle does not emit
    int  Object = 0;      // [idx] the SPAN this triangle belongs to — M10's object, i.e. the kernel's INSTANCE.
                          //       D10's history identity is this, not the triangle: the tessellation is finer than a
                          //       pixel, so a triangle is not a "thing" a temporal accumulator can recognise.
};

struct BvhNode
{
    vec3 Min, Max;
    int  Left = -1, Right = -1, Start = 0, Count = 0;
};

// [nit] the emissive triangles' total power, for the discrete selection pdf (see PickLight).
float g_AutomotiveRaySpread=0.001f;
void BindAutomotiveHit(ShadingRecord& m,const RenderTriangle& t,float u,float v,float distance,vec3 direction,vec3 normal) {
    if(m.AutomotiveData.x<1.0f||m.AutomotiveData.x>5.0f)return;
    m.AutomotiveData.z=(t.U0*(1-u-v)+t.U1*u+t.U2*v)*3.4557519f;
    m.AutomotiveData.w=(t.V0*(1-u-v)+t.V1*u+t.V2*v)*1.7278760f;
    float worldArea=length(cross(t.P1-t.P0,t.P2-t.P0));
    float uvArea=std::abs((t.U1-t.U0)*(t.V2-t.V0)-(t.V1-t.V0)*(t.U2-t.U0));
    m.AutomotiveFootprint=3.4557519f*std::sqrt(uvArea/std::max(worldArea,1e-12f))*distance*g_AutomotiveRaySpread/std::max(std::abs(dot(direction,normal)),.001f);
}
float TotalLightPower();

struct EmissiveTriangle
{
    vec3 P0, P1, P2;
    vec3 Ng;
    vec3 Radiance;        // [nit] emission_luminance × emission_color — the luminaire table's radiance
    float Area;           // [m²]
    float Power;          // [-] area × luminance, the discrete selection weight
};

std::vector<RenderTriangle>    g_Tris;
std::vector<BvhNode>           g_Nodes;
std::vector<int>               g_Order;
std::vector<EmissiveTriangle>  g_Lights;
std::vector<ShadingRecord>     g_Mat;
std::vector<uint32_t>          g_MatFlags;
std::vector<float>             g_MatCutoff;
std::vector<bool>              g_MatCutAway;   // [-] the material's constant opacity fails its cutoff: never hit
// M6 glints — the slab's slate_glint pair, seated beside the record exactly as the kernel reads P14.xy from the
//    slab (ShadingRecord carries no glint fields on either side; the perturbation happens before the frame builds).
std::vector<float>             g_MatGlintDensity;
std::vector<float>             g_MatGlintUvScale;
bool g_MatGlintsOff = false;   // [-] DIAGNOSTIC (--no-glints): the pre-M6 surface, the A/B arm for the glint sheets

Frontier::ProjectZero::SkyFogIntegrator g_Sky;
vec3  g_SunDirRender(0.0f, 0.0f, 1.0f);   // [-] unit vector toward the sun, render frame (Y-up)
float g_SunAngularRadius = 0.00465f;      // [rad] the sky core's disc radius (0.53° diameter)
bool  g_SunNee = true;
int   g_RowFilter = -1;    // [-] -1 = the whole grid; 0..5 = one sphere row (a lookdev crop, printed when used)
// Which level to render. "materials" is the M10 library (the historical default of this harness); "showcase" is the
//    product's own DEFAULT level — what Project-Zero.exe opens with no --scene argument at all. They are built from
//    the same kind of source (a Structure's Construct + MaterialIndex), so the renderer below does not care which.
std::string g_Level = "materials";

//------------------------------------------------------------------------------------------------------------------------
//                                 THE INTERFACE PANEL (showcase only) — two widget kinds, end to end
//------------------------------------------------------------------------------------------------------------------------
// The showcase carries Project-Zero's trial panel at the berth ShowcaseStructure authors (stand + housing slab).
//    The harness composes the panel's figures with the ENGINE's own pipeline (InterfaceTrialSequence → InterfaceSequence),
//    measures its radiance with the ENGINE's own MeasureRadiance — which is where the ⑧ light-role split bites:
//
//      · Type 1, Overlay figures (needle, tick ring, readout): drawn in the composite below, EXCLUDED from the light.
//      · Type 2, Illuminant figures (telltale lamp, toggle LED, lit fills, backlit buttons): drawn identically AND
//        averaged into the panel's proxy luminaire, which ComposeProxy registers through a real SceneStructure —
//        so the two panel triangles that light the plinth come out of the engine's own Finalise, not a lookalike.
//
//    The figures themselves are composited over the film after the trace (the raster overlay's CPU mirror): the
//    panel face pixel shows the FIGURES, while reflections and bounce light show the Low-tier average — exactly the
//    tier contract References/InterfaceLightContribution-Plan.md specifies.
bool  g_PanelActive = false;
std::vector<Frontier::InterfaceInstanceFigure> g_PanelFigures;   // composed slots, submission (draw) order
vec3  g_PanelCentre(0.0f), g_PanelNormal(0.0f, -1.0f, 0.0f);     // world face
float g_PanelGain = 120.0f;                                      // [-] proxy gain. GameExecution uses 26 against the
                                                                 //     showroom's 32-nit ceiling; this room's key is
                                                                 //     140 nits, so the proportional setting is ~120.
constexpr float kPanelDisplayNits = 120.0f;                      // [nit] what an emissive figure shows at on the face
constexpr float kPanelAmbientNits = 18.0f;                       // [nit] flat ambient on the albedo figures (P0 term)

//------------------------------------------------------------------------------------------------------------------------
//                                                          RNG
//------------------------------------------------------------------------------------------------------------------------

// ── Roadmap #7: the SEED STREAM ─────────────────────────────────────────────────────────────────────────────────────
// Every render in this harness is deterministic in (pixel, sample index, frame), which is what makes the sheets
//    reproducible. That determinism has a cost the report names: the converged reference ① and the budget render ② walk
//    the SAME stream — deliberately, since 4 spp x 128 frames IS the 512 seeds of the one-frame reference, which is why
//    ② ≡ ① measures AE 0 and doubles as the harness's own sanity gate. But it also means every RMSE against ① shares its
//    noise with the arm being measured, so those errors are understated: the difference of two correlated estimators is
//    smaller than the difference of two independent ones.
//
//    `--seed-stream N` shifts the whole stream by N, and at N = 0 it is the identity (a multiply by a constant and an
//    XOR that adds nothing), so every number recorded before this existed still describes this code. Stream 1 is a
//    genuinely independent draw of the same estimator: the reference is rendered on one stream and the arm on another,
//    and the error between them is then the error, with no shared modes subtracted out. Roadmap #7 asked for exactly
//    this — "the floor can be measured rather than shared" — and every absolute RMSE in the report should be read
//    against the pair of numbers (shared stream, independent stream), not one of them.
uint32_t g_SeedStream = 0u;

struct Rng
{
    uint32_t S;
    explicit Rng(uint32_t Seed) noexcept : S((Seed ^ (g_SeedStream * 0x9E3779B9u)) * 747796405u + 2891336453u) {}
    float Next() noexcept
    {
        S = S * 747796405u + 2891336453u;
        uint32_t W = ((S >> ((S >> 28u) + 4u)) ^ S) * 277803737u;
        W = (W >> 22u) ^ W;
        return static_cast<float>(W) * (1.0f / 4294967296.0f);
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                     DESCRIPTOR RECORDS → SHADING RECORD
//------------------------------------------------------------------------------------------------------------------------
// A transcription of ReSTIRViewport.slang's `ResolveMaterial` for the constants-only case (no textures bound). Every
//    line below corresponds to a line of that function; the channel fetches all resolve to their documented default
//    (`SampleChannel*` with no slot returns vec4(1.0) / vec4(1.0, 0.5, 1.0, 1.0) for anisotropy), which is why the folds
//    collapse to plain multiplies. Keeping the shape of the kernel's code — rather than writing "the obvious fold" —
//    is what makes this the engine's material model instead of a lookalike.

ShadingRecord TranscribeShadingRecord(const MaterialSlabRecord& S, uint32_t Selection)
{
    ShadingRecord m;
    m.AutomotiveData=vec4(S.Reserved0,S.Reserved1,0.0f,0.0f);m.AutomotiveDensity=S.SlateGlintDensity;
    m.BaseColor          = vec3(S.BaseWeight * S.BaseColorR, S.BaseWeight * S.BaseColorG, S.BaseWeight * S.BaseColorB);
    m.Metalness          = S.BaseMetalness;
    m.DiffuseRoughness   = S.BaseDiffuseRoughness;
    m.SpecularWeight     = S.SpecularWeight;
    m.SpecularColor      = vec3(S.SpecularColorR, S.SpecularColorG, S.SpecularColorB);
    m.SpecularRoughness  = S.SpecularRoughness;
    m.SpecularAnisotropy = S.SpecularRoughnessAnisotropy;                 // × anisoTex.z (=1 unbound)
    m.AnisotropyAngle    = S.AnisotropyRotation;                          // atan(1,0)=0 + Slate2.x
    m.SpecularIor        = S.SpecularIor;
    m.ThinFilmWeight     = S.ThinFilmWeight;
    m.ThinFilmThickness  = S.ThinFilmThickness;
    m.ThinFilmIor        = S.ThinFilmIor;
    m.HazinessWeight     = S.SlateHazinessWeight;
    m.HazinessRoughness  = S.SlateHazinessRoughness;
    m.CoatWeight         = S.CoatWeight;
    m.CoatColor          = vec3(S.CoatColorR, S.CoatColorG, S.CoatColorB);
    m.CoatRoughness      = S.CoatRoughness;
    m.CoatAnisotropy     = S.CoatRoughnessAnisotropy;
    m.CoatIor            = S.CoatIor;
    m.CoatDarkening      = S.CoatDarkening;
    m.CoatTangent        = vec3(1.0f, 0.0f, 0.0f);                        // identity (no coat normal texture bound)
    m.CoatNormal         = vec3(0.0f, 0.0f, 1.0f);
    m.FuzzWeight         = S.FuzzWeight;
    m.FuzzColor          = vec3(S.FuzzColorR, S.FuzzColorG, S.FuzzColorB);
    m.FuzzRoughness      = S.FuzzRoughness;
    m.Emission           = vec3(S.EmissionLuminance * S.EmissionColorR, S.EmissionLuminance * S.EmissionColorG,
                                S.EmissionLuminance * S.EmissionColorB);
    m.TransmissionWeight = S.TransmissionWeight;
    m.TransmissionColor  = vec3(S.TransmissionColorR, S.TransmissionColorG, S.TransmissionColorB);
    m.TransmissionDepth  = S.TransmissionDepth;
    m.TransmissionThickness = 0.0f;                                       // tracer-side: the true chord, or the no-exit fallback
    m.Selection          = Selection;
    m.SssWeight          = S.SubsurfaceWeight;
    m.SssColor           = vec3(S.SubsurfaceColorR, S.SubsurfaceColorG, S.SubsurfaceColorB);
    m.SssRadius          = S.SubsurfaceRadius;
    m.SssRadiusScale     = vec3(S.SubsurfaceRadiusScaleR, S.SubsurfaceRadiusScaleG, S.SubsurfaceRadiusScaleB);
    m.SssThickness       = 0.0f;                                          // filled per hit by SssChord
    return m;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     CAMERA
//------------------------------------------------------------------------------------------------------------------------
// GameExecution's Z-up fly-through camera, posed by the level's own branch. `ConstructRay` takes normalized viewport
//    coordinates in [-1, 1] with +Y up.

struct Viewpoint
{
    Frontier::Vector3 Position;
    float             PitchDegrees;
    float             YawDegrees;
    float             FieldOfView;
};

// Yaw is clockwise from north (+Y) with +Z up, so yaw 90° looks along +X — the direction a row runs in. The two row
//    views therefore stand just west of the grid, level with their row, and look down it: the seven spheres recede
//    left to right in ordinal order, which is how a lookdev sheet is read.
Viewpoint ViewpointFor(const std::string& Name)
{
    if (Name == "wide")  return { Frontier::Vector3{  0.0f, -7.20f, 3.05f }, -12.0f,  0.0f, 58.0f };   // the whole set
    if (Name == "glass") return { Frontier::Vector3{ -4.60f, 1.50f, 2.00f }, -16.0f, 60.0f, 50.0f };   // row 3, 3/4 down the row
    if (Name == "row5")  return { Frontier::Vector3{ -4.60f, 3.70f, 2.00f }, -16.0f, 60.0f, 50.0f };   // row 5, 3/4 down the row
    // Default: the product's own entry framing for `--scene materials` (GameExecution.cpp, the "Materials" branch).
    return { Frontier::Vector3{ 0.0f, -5.00f, 2.60f }, -13.0f, 0.0f, 55.0f };
}

// The showcase level's framings. The default here is GameExecution.cpp's "Showcase" branch VERBATIM — the position,
//    pitch, yaw and FoV the application itself opens with when it is launched with no --scene at all. That is the
//    whole point of this harness: the sheet must be the shot the product gives you, not a flattering angle.
Viewpoint ShowcaseViewpointFor(const std::string& Name)
{
    // R6 review cameras use the actual 400-sphere scene, not a separate studio.
    if (Name == "grid400") return { Frontier::Vector3{0.0f,-9.0f,42.0f},-62.0f,0.0f,44.0f };
    const char* PaintViews[5]={"paint-candy","paint-glitter","paint-iridescent","paint-cobalt","paint-copper"};
    for(int family=0;family<5;++family){
        const float rowY=-1.8f+1.5f*static_cast<float>(15+family);
        if(Name==PaintViews[family])return {Frontier::Vector3{-16.2f,rowY,1.25f},-18.5f,90.0f,42.0f};
        if(Name==std::string(PaintViews[family])+"-macro")return {Frontier::Vector3{-14.99f,rowY,.60f},0.0f,90.0f,40.0f};
    }

    if (Name == "grid")   return { Frontier::Vector3{  0.0f, -9.50f, 4.20f }, -10.0f,  0.0f, 55.0f };  // closer on the grid's front rows
    if (Name == "metals") return { Frontier::Vector3{ -1.0f, -5.40f, 1.60f },  -7.0f,  0.0f, 50.0f };  // row 0: anisotropic metals
    if (Name == "glass")  return { Frontier::Vector3{ -1.0f, -3.90f, 1.60f },  -6.0f,  0.0f, 50.0f };  // row 1: the IOR ramp
    if (Name == "wide")   return { Frontier::Vector3{  0.0f, -22.0f, 11.0f }, -18.0f,  0.0f, 62.0f };  // 15×15 grid + scattered ring
    if (Name == "panel")  return { Frontier::Vector3{ 2.35f, -5.60f, 1.35f },  -4.0f,  8.0f, 42.0f };  // the interface panel, close
    if (Name == "glints") return { Frontier::Vector3{ -1.0f, 12.70f, 1.60f },  -7.0f,  0.0f, 50.0f };  // M6: row 12 (Y +16.2), density 1 → 8 x uv_scale 4 → 12
    return { Frontier::Vector3{ 0.0f, -15.0f, 8.00f }, -21.0f, 0.0f, 55.0f };                          // the product's entry shot (r4)
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        BVH
//------------------------------------------------------------------------------------------------------------------------

void BuildBvh(int Node, int Start, int Count)
{
    BvhNode& N = g_Nodes[Node];
    N.Min = vec3(1e30f); N.Max = vec3(-1e30f);
    vec3 CMin(1e30f), CMax(-1e30f);
    for (int I = 0; I < Count; ++I)
    {
        const RenderTriangle& T = g_Tris[g_Order[Start + I]];
        N.Min = min(N.Min, min(T.P0, min(T.P1, T.P2)));
        N.Max = max(N.Max, max(T.P0, max(T.P1, T.P2)));
        vec3 C = (T.P0 + T.P1 + T.P2) / 3.0f;
        CMin = min(CMin, C); CMax = max(CMax, C);
    }
    if (Count <= 4)
    {
        N.Start = Start; N.Count = Count;
        return;
    }
    vec3 Ext = CMax - CMin;
    int Axis = Ext.x > Ext.y ? (Ext.x > Ext.z ? 0 : 2) : (Ext.y > Ext.z ? 1 : 2);
    int Mid = Start + Count / 2;
    std::nth_element(g_Order.begin() + Start, g_Order.begin() + Mid, g_Order.begin() + Start + Count,
        [Axis](int A, int B)
        {
            vec3 CA = (g_Tris[A].P0 + g_Tris[A].P1 + g_Tris[A].P2) / 3.0f;
            vec3 CB = (g_Tris[B].P0 + g_Tris[B].P1 + g_Tris[B].P2) / 3.0f;
            float VA = Axis == 0 ? CA.x : (Axis == 1 ? CA.y : CA.z);
            float VB = Axis == 0 ? CB.x : (Axis == 1 ? CB.y : CB.z);
            return VA < VB;
        });
    // ⚠️ Never hold a reference into g_Nodes across emplace_back: the vector can reallocate (the exhibit documents the
    //    exact bug this warning came from — a dangling root reference silently dropped half the scene).
    int L = static_cast<int>(g_Nodes.size()); g_Nodes.emplace_back();
    int R = static_cast<int>(g_Nodes.size()); g_Nodes.emplace_back();
    g_Nodes[Node].Left = L; g_Nodes[Node].Right = R;
    BuildBvh(L, Start, Mid - Start);
    BuildBvh(R, Mid, Start + Count - Mid);
}

struct Hit
{
    bool  Valid = false;
    float T = 1e30f;
    float U = 0.0f, V = 0.0f;
    int   TriId = -1;
};

bool IntersectTri(const vec3& O, const vec3& D, const RenderTriangle& T, float TMax, float& THit, float& U, float& V)
{
    vec3 E1 = T.P1 - T.P0, E2 = T.P2 - T.P0;
    vec3 P = cross(D, E2);
    float Det = dot(E1, P);
    if (Det > -1e-12f && Det < 1e-12f) return false;
    float Inv = 1.0f / Det;
    vec3 S = O - T.P0;
    U = dot(S, P) * Inv;
    if (U < 0.0f || U > 1.0f) return false;
    vec3 Q = cross(S, E1);
    V = dot(D, Q) * Inv;
    if (V < 0.0f || U + V > 1.0f) return false;
    THit = dot(E2, Q) * Inv;
    return THit > 1e-4f && THit < TMax;
}

bool IntersectBox(const vec3& O, const vec3& Inv, const vec3& Min, const vec3& Max, float TMax)
{
    vec3 T0 = (Min - O) * Inv, T1 = (Max - O) * Inv;
    vec3 TMin = min(T0, T1), TMax3 = max(T0, T1);
    float Near = TMin.x > TMin.y ? (TMin.x > TMin.z ? TMin.x : TMin.z) : (TMin.y > TMin.z ? TMin.y : TMin.z);
    float Far  = TMax3.x < TMax3.y ? (TMax3.x < TMax3.z ? TMax3.x : TMax3.z) : (TMax3.y < TMax3.z ? TMax3.y : TMax3.z);
    return Near <= Far && Far > 0.0f && Near < TMax;
}

// skipMat ≥ 0 skips every triangle carrying that material (the thin-wall transmission continuation).
Hit Intersect(const vec3& O, const vec3& D, float TMax, int SkipMat)
{
    Hit H;
    vec3 Inv(1.0f / D.x, 1.0f / D.y, 1.0f / D.z);
    int Stack[64]; int Depth = 0;
    Stack[Depth++] = 0;
    while (Depth > 0)
    {
        const BvhNode& N = g_Nodes[Stack[--Depth]];
        if (!IntersectBox(O, Inv, N.Min, N.Max, H.T)) continue;
        if (N.Count > 0)
        {
            for (int I = 0; I < N.Count; ++I)
            {
                const RenderTriangle& T = g_Tris[g_Order[N.Start + I]];
                if (SkipMat >= 0 && T.Material == SkipMat) continue;
                if (g_MatCutAway[T.Material]) continue;   // alpha test (constants-only level: per material, not per texel)
                float TH, UH, VH;
                if (IntersectTri(O, D, T, H.T < TMax ? H.T : TMax, TH, UH, VH))
                {
                    H.Valid = true; H.T = TH; H.U = UH; H.V = VH; H.TriId = g_Order[N.Start + I];
                }
            }
        }
        else
        {
            Stack[Depth++] = N.Left;
            Stack[Depth++] = N.Right;
        }
    }
    if (H.T >= TMax) H.Valid = false;
    return H;
}

bool Occluded(const vec3& A, const vec3& B)
{
    vec3 D = B - A;
    float Len = length(D);
    D = D / Len;
    Hit H = Intersect(A, D, Len - 1e-3f, -1);
    return H.Valid;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              SKY, SUN, LUMINAIRES
//------------------------------------------------------------------------------------------------------------------------

// World (Z-up) → render (Y-up) for the sky core, and the radiance the environment sends along a WORLD direction.
vec3 SkyRadianceWorld(const vec3& DirectionWorld)
{
    const vec3 Render = vec3(DirectionWorld.x, DirectionWorld.z, -DirectionWorld.y);
    const Frontier::Vector3 R = g_Sky.ComputeSkyRadiance(Frontier::Vector3{ Render.x, Render.y, Render.z });
    return vec3(R.x, R.y, R.z);
}

// A uniformly chosen emissive triangle, weighted by power (area × luminance) — the same discrete distribution the
//    engine's alias table approximates, exact here because the level has six emissive triangles.
int PickLight(Rng& R, float& OutPdf, float& OutArea, vec3& OutRadiance)
{
    if (g_Lights.empty()) return -1;
    float Total = 0.0f;
    for (const EmissiveTriangle& L : g_Lights) Total += L.Power;
    float Pick = R.Next() * Total;
    int Chosen = static_cast<int>(g_Lights.size()) - 1;
    for (size_t I = 0; I < g_Lights.size(); ++I)
    {
        Pick -= g_Lights[I].Power;
        if (Pick <= 0.0f) { Chosen = static_cast<int>(I); break; }
    }
    OutPdf = g_Lights[Chosen].Power / Total;
    OutArea = g_Lights[Chosen].Area;
    OutRadiance = g_Lights[Chosen].Radiance;
    return Chosen;
}

void ShadingFrame(const vec3& N, vec3& T, vec3& B)
{
    vec3 Helper = fabs(N.z) < 0.9f ? vec3(0.0f, 0.0f, 1.0f) : vec3(1.0f, 0.0f, 0.0f);
    T = normalize(cross(Helper, N));
    B = cross(N, T);
}

//------------------------------------------------------------------------------------------------------------------------
//                              RESTIR DIRECT LIGHTING — CPU MIRROR OF THE KERNEL'S DI BLOCK
//------------------------------------------------------------------------------------------------------------------------
// ReSTIRViewport.slang is not compilable off-GPU (bindless tables, a BVH in buffers, storage images), so this is the
//    same move the M9 proof made for the filter: a line-for-line mirror of the kernel's direct-lighting block, checked
//    against the shader text by the driver (it prints the constants it mirrors). What is mirrored:
//
//      · DrawDirectCandidate — the lamp/sun coin flip (kSunPickProbability 0.5), the sun's disc sample, w = p̂/p with
//        BOTH continuous pdfs (a mesh sample pays area/(p_source·p_pick), a sun sample pays Ω/p_pick);
//      · ResampleCandidate — the streaming RIS resample, weightSum/count accumulation, 1-in-M selection;
//      · the temporal merge — back-projection, the 25° normal and 10 % depth validation, the stride guard, the 20×
//        M-clamp, pairwise MIS on w = p̂·W·M, and the p̂ re-evaluation of the stored sample at THIS pixel (the
//        "revalidation re-evaluates the BSDF" claim: no Jacobian, no per-lobe history, just the same target function);
//      · the spatial merge — K taps on a rotated cross at radius 4…16 px (scaled by width/1280), read from the previous
//        frame's buffer, same validation and the same pairwise algebra;
//      · visibility re-traced at the shading pixel, an occluded reservoir contributing nothing (but still counting for
//        M next frame), and the shade step's own f·L·cos·cosL·W/dist² form;
//      · the running mean + first two luminance moments of ResolveSurface, so the filter downstream reads the variance
//        of the mean the kernel would have handed it.
//
//    Deviations, all deliberate and all because this is a harness and not the dispatch (printed by the driver):
//      · the reprojection is computed from the previous camera pose, not read from the R2 motion image: on the CPU the
//        pose is exact, and the kernel's rule (a back-projection off screen is a disocclusion, not a clamp) is kept;
//      · a pixel that missed geometry clears its reservoir slot; the kernel leaves the stale one for the M/stride
//        guards to reject (same outcome, one fewer way to read uninitialised memory);
//      · no cloud-shadow transmittance (the level's scenario is Clear) and no R2 raster jitter: the primary ray is the
//        pixel's own camera ray with the pixel's own sub-pixel offset.
//
//    Gated by --restir. With it off, Radiance() below is untouched: the sheets' reference and plain-accumulation
//    panels are the same estimator that produced the kept MaterialLibrary_Viewport sheets, bit for bit.

const float    kRestirNormalCos     = 0.906308f;   // cos(25°) — kTemporalNormalCos
const float    kRestirDepthTol      = 0.10f;       // kTemporalDepthTol
const uint32_t kRestirMClamp        = 20u;         // kTemporalMClamp
uint32_t g_RestirSpatialTaps = 4u;  // SpatialTapCount: the shipped default (R6 row 3 cross)
const uint32_t kRestirTapCeiling    = 4u;          // kSpatialTapCeiling
const float    kRestirRadiusMinPx   = 4.0f;        // kSpatialRadiusMinPx
const float    kRestirRadiusMaxPx   = 16.0f;       // kSpatialRadiusMaxPx
const float    kRestirSunPick       = 0.5f;        // kSunPickProbability (the kernel's legacy fixed coin)
// Power-proportional sun coin, mirroring the kernel's SunPickChance() (2026-09-19): 0 = the fixed 0.5 above;
//    otherwise the host-computed pSun = sunFlux / (sunFlux + lampFlux), clamped like the engine ([0.05, 0.95]).
//    Set in main() after the scene and sky exist; --sun-pick overrides for A/Bs.
float g_SunPickProbability = 0.0f;
float SunPickChance() { return g_SunPickProbability > 0.0f ? g_SunPickProbability : kRestirSunPick; }
const float    kRestirSunDistance   = 1.0e4f;      // kSunShadowDistance
const uint32_t kRestirSunLight      = 0xFFFFFFFFu; // kSunLightIndex
// A GI tap's vertices must be the SAME PLACE within this fraction of the path's own length: the primary depth test
//    alone merges across two different first-bounce hits whose primary pixels happen to line up.
const float    kRestirGiVertexTol   = 0.05f;

struct CpuReservoir
{
    uint32_t Identity        = 0u;   // D10: the surface this reservoir was built on (0 = none) — the kernel's
                                     //    instance<<14|primitive, here the mirrored triangle's own ordinal
    vec3     SelectedPoint   = vec3(0.0f);
    uint32_t SelectedLight   = 0u;
    float    SelectedUvU     = 0.0f;
    float    SelectedUvV     = 0.0f;
    float    WeightSum       = 0.0f;
    uint32_t SampleCount     = 0u;
    float    UnbiasedWeight  = 0.0f;
    uint32_t Visible         = 0u;
    uint32_t Age             = 0u;
    vec3     Normal          = vec3(0.0f, 0.0f, 1.0f);   // geometric normal of the reservoir's pixel
    float    Depth           = 0.0f;                      // primaryT there
    float    StrideWidth     = 0.0f;                      // the kernel's prev.Normal.w stride guard
    float    MotionU         = 0.0f, MotionV = 0.0f;      // what the R2 image would have carried
};

bool g_RestirNoReproject = false;
// D10: validate a reprojected history against the IDENTITY of the surface, not only its normal and depth. On by
//    default (the kernel's kFeatureTemporalIdentity); `--restir-no-identity` restores the pre-D10 rule so the fix can
//    be measured against it, which is the whole point of having the switch.
bool g_RestirIdentity = true;
// D10: the moving-object driver. Zero = the level is static (every sheet before this one). With a value, all triangles
//    carrying `g_RestirDriftMaterial` slide that many metres per frame along their own tile's dominant axis, in a
//    triangular excursion that RETURNS to the rest pose on the closing frame — so the last frame can be compared
//    against a one-frame reference render of the untouched level, exactly like the camera-pan pair.
float g_RestirDrift = 0.0f;
int   g_RestirDriftMaterial = 24;   // [idx] which swatch's material slides (the middle of the M10 grid by default)
int   g_RestirDriftAxis = -1;       // [-]  -1: the object's own longest extent (a flat swatch's in-plane axis);
                                    //         0/1/2: force x/y/z — a lateral slide crosses coplanar neighbours (the
                                    //         case normal+depth cannot see), a bob does not.
bool g_RestirHistorySplit = true;
// The indirect half's pool (ReSTIR GI). Off ⇒ the pre-pool single-sample arm, which is the A/B.
bool g_RestirGiReuse = true;
// Visibility reuse (Bitterli et al. 2020 §5): the spatial pass TRUSTS the winning tap's stored visibility instead of
//    re-tracing the merged selection, cutting the second DI shadow ray per pixel. Sound because a blocked reservoir
//    publishes W = 0 and cannot win a merge; the admitted bias is confined to shadow edges and bounded by the tap
//    radius. `--restir-final-visibility` restores the old always-re-trace arm — that flag IS the A/B this fix was
//    measured with before the kernel port, and the counter below says how many rays the reuse saved.
bool g_RestirFinalVisibility = false;
std::atomic<long> g_VisibilityReuseSaved{0};   // [rays] spatial-pass shadow rays NOT traced because reuse answered
// #6 — the dead-entry skip, MEASURED AND REFUSED (2026-09-23). The hypothesis: a blocked selection publishes
//    W = 0 yet its M still joins every merge's denominator, shrinking whatever DID win — §14.3's "weight loss
//    from dropped occluded selections" — so skipping dead entries should help. The measurement said the
//    opposite: refusing an entry because its OBSERVED W is 0 conditions the merge on the sample's own outcome
//    (adaptive-M bias — the estimator brightens where occlusion is common, because the denominator forgets
//    the failures). 240x135, 4 cand x 128 frames, showcase, independent stream: RMSE 5 685 (dead entries
//    merge) vs 6 221 (skipped) — 9 % WORSE — with the mean +1.3 % over the reference and 458k direct + 43k GI
//    merges refused. The M a dead entry carries is real information: those candidates were CONSIDERED, and
//    the set's estimator must count them. OFF by default, kept under `--restir-dead-skip` so the negative
//    result stays re-measurable; the counters below say how often each pool WOULD have refused an entry.
bool g_RestirDeadSkip = false;
std::atomic<long> g_DeadSkipDirect{0};   // [merges] direct-pool entries refused for W = 0 (temporal + spatial)
std::atomic<long> g_DeadSkipGi{0};       // [merges] GI-pool entries refused for W = 0 (temporal + spatial)
// Roadmap #5, step 1: WHY a pixel has no pool coverage, as a per-pixel image rather than a counter. §14.5's counters say
//    what share of the frame falls in each case; they cannot say whether those pixels are where the ERROR is, and that
//    is the question that decides whether the 16 → 100 % fix (replay + shift mapping) is worth its bias risk. Writing
//    the classification out as a PNG lets the same pixels be masked in an RMSE table — coverage and error, per class.
enum : unsigned char
{
    kGiClassNone     = 0u,   // the primary ray missed geometry — no surface, so no vertex and no pool either way
    kGiClassBad      = 1u,   // the primary BSDF draw was degenerate (zero weight / zero pdf)
    kGiClassEscape   = 2u,   // the primary BSDF sample saw the sky: no first-bounce vertex exists  (69 % of the frame)
    kGiClassEmitter  = 3u,   // it landed on a light: the path ends there (already a perfect light sample)
    kGiClassUnlit    = 4u,   // it landed on an emissive-only / unlit surface: the terminal IS the answer
    kGiClassUnusable = 5u,   // glass / SSS / solid-interface vertex: out of scope for a light-sample pool by design
    kGiClassVertex   = 6u,   // a reusable opaque vertex — the pool's coverage, and the ONLY class it currently gets
    kGiClassNoSample = 7u,   // a vertex was usable, but the frame's candidate draw produced NO sample (zero total
                             //    weight — every candidate behind the horizon), so there is nothing to merge or shade
};
std::atomic<long> g_GiBad{0}, g_GiEscape{0}, g_GiEmitter{0}, g_GiUnlit{0}, g_GiUnusable{0}, g_GiVertex{0}, g_GiNoPHat{0};
// The indirect pool's temporal half, counted so a DEAD merge is visible in the transcript rather than inferred from a
//    flat mean M: `Tried` is every attempt against a history pixel that held samples, `NoVertexHistory` the share of
//    those refused only because the previous frame's vertex record was missing, `Merged` the accepted ones. Measured
//    (240x135, 128 frames): 104 805 tried, 97 194 merged, 78 refused for a missing record — the temporal half IS alive
//    (the swap at the end of the frame keeps `VertexHistory` = last frame's vertices) and merges 93 % of the attempts it
//    gets; what it does not get is attempts. Only ~25 % of the pixels that hold a reservoir even look at a history pixel
//    that holds one, because a pixel only has an indirect reservoir on the frames its own primary BSDF sample reached
//    geometry at all. The counters exist so that distinction stays measured rather than assumed — it was assumed wrong
//    once already (a "dead VertexHistory" hypothesis, disproved by the swap plus these counters).
std::atomic<long> g_GiTemporalTried{0}, g_GiTemporalNoVertexHistory{0}, g_GiTemporalMerged{0};
// D10: temporal merges the geometry test would have allowed and the identity refused — per pool, because the two pools
//    ghost differently (the DI pool inherits a light sample chosen for another object; the GI pool inherits one chosen
//    at another object's first-bounce VERTEX, which reads as a reflection of something that is no longer there).
std::atomic<long> g_IdentityRefusedDirect{0}, g_IdentityRefusedGi{0};
std::atomic<long> g_GiTried{0}, g_GiTValid{0}, g_GiFDepth{0}, g_GiFNormal{0}, g_GiFVertex{0}, g_GiFHist{0};   // [-] the training-wheels switch: false restores the pre-fix feedback loop   // [-] the pre-R7a same-pixel history read (the D9 switch, for the A/B sheet)
bool g_RestirBounceMis = false;   // [-] DIAGNOSTIC: exclude the first-bounce emitter hit (see Radiance)
bool g_RestirNoSunCoin = false;   // [-] DIAGNOSTIC: lamps-only candidates (what the sun coin costs in this scene)
uint32_t g_RestirMCap = 0u;         // [-] 0 = the kernel's own rules (no absolute cap); >0 = a candidate ceiling, to
                                    //     measure what one would buy (the shipped kernel has only the 20x relative clamp)

// The camera pose, captured once per frame: the previous frame's copy is what reprojection projects into.
struct CameraPose
{
    vec3 Origin, Forward, Right, Up;
    float TanHalf = 0.0f, Aspect = 1.0f;
};

CameraPose CapturePose(const Frontier::CameraProjection& Camera)
{
    CameraPose Pose;
    const Frontier::Vector3 O = Camera.QuerySpatialLocation();
    const Frontier::Vector3 F = Camera.QueryForwardVector();
    const Frontier::Vector3 R = Camera.QueryRightVector();
    const Frontier::Vector3 U = Camera.QueryUpwardVector();
    Pose.Origin  = vec3(O.x, O.y, O.z);
    Pose.Forward = normalize(vec3(F.x, F.y, F.z));
    Pose.Right   = normalize(vec3(R.x, R.y, R.z));
    Pose.Up      = normalize(vec3(U.x, U.y, U.z));
    Pose.TanHalf = tanf(Camera.QueryFieldOfViewRadians() * 0.5f);
    Pose.Aspect  = Camera.QueryAspectRatio();
    return Pose;
}

// CameraProjection::ConstructRay's own maths, inverted: the pixel a world point lands on in THIS pose.
bool ProjectPoint(const CameraPose& Pose, const vec3& P, float& OutU, float& OutV)
{
    const vec3 D = P - Pose.Origin;
    const float Z = dot(D, Pose.Forward);
    if (Z <= 1.0e-4f) return false;
    const float ScreenX = dot(D, Pose.Right) / Z;
    const float ScreenY = dot(D, Pose.Up) / Z;
    OutU = 0.5f * (ScreenX / (Pose.TanHalf * Pose.Aspect) + 1.0f);
    OutV = 0.5f * (1.0f - ScreenY / Pose.TanHalf);
    return true;
}

float PHatSurface(const ShadingRecord& m, const ResolvedLayers& L, const vec3& Ng, const vec3& T, const vec3& B,
                  const vec3& Ns, const vec3& wo, const vec3& Emit, const vec3& ToLight, const vec3& LightNormal)
{
    const float Dist2 = dot(ToLight, ToLight);
    const float Dist  = sqrtf(max(Dist2, 1.0e-18f));
    const vec3  Ld    = ToLight / Dist;
    const float CosT  = max(0.0f, dot(Ng, Ld));
    if (CosT <= 0.0f) return 0.0f;
    const float CosL = max(0.0f, dot(LightNormal, -Ld));
    if (CosL <= 0.0f) return 0.0f;
    const vec3 wi(dot(Ld, T), dot(Ld, B), dot(Ld, Ns));
    if (wi.z <= 0.0f) return 0.0f;
    return max(dot(EvaluateBsdf(m, L, wo, wi), Emit), 0.0f) * CosT * CosL / (Dist2 + 0.001f);
}

float PHatSun(const ShadingRecord& m, const ResolvedLayers& L, const vec3& Ng, const vec3& T, const vec3& B,
              const vec3& Ns, const vec3& wo, const vec3& SunEmit, const vec3& SunDir)
{
    const float CosT = max(0.0f, dot(Ng, SunDir));
    if (CosT <= 0.0f) return 0.0f;
    const vec3 wi(dot(SunDir, T), dot(SunDir, B), dot(SunDir, Ns));
    if (wi.z <= 0.0f) return 0.0f;
    return max(dot(EvaluateBsdf(m, L, wo, wi), SunEmit), 0.0f) * CosT;
}

float SunSolidAngleRender()
{
    const float S = sinf(g_SunAngularRadius * 0.5f);
    return 4.0f * kPi * S * S;
}

// The NEE sun term, ALIGNED WITH THE KERNEL (2026-09-19). The kernel's SunEmission() is SkySunDirect/Ω, where the
//    record carries the panel's direct-sun factor Q = 0.11·Direct·colour·gain·transmittance (SkyConstantRecord.h
//    PackSkyConstants). This mirror used to return SkyRadianceWorld(sunDir) — the sky MODEL's disc radiance, which
//    carries the ×12 sunDiscBoost the panel applies for the visible disc, NOT for surface lighting. That made the
//    mirror's sun up to an order of magnitude hotter than the product's, so the A/B "proof" images flattered the
//    shadows the GPU could never reproduce. Now both sides light surfaces with the same Q/Ω: CoreSunRadiance is
//    exactly colour·gain·transmittance (SkyFogIntegrator.cpp:257, zeroed below the horizon like the packer's
//    elevation gate), and g_SunDirectGain mirrors the product's Direct slider (CelestialSequence default).
float g_SunDirectGain = 2.5f;             // [-] the panel's Direct slider; product default (CelestialSequence.h)

vec3 SunEmissionRender()
{
    const Frontier::Vector3 R = g_Sky.QuerySunRadiance();
    const float Q = 0.11f * g_SunDirectGain / SunSolidAngleRender();
    return vec3(R.x * Q, R.y * Q, R.z * Q);
}

// An ABSOLUTE cap on M, applied consistently: the weight sum is scaled with the count, so W (the estimate) is
//    untouched and only this reservoir's future *confidence* shrinks. The shipped kernel has no such cap — only the
//    20x relative clamp — which is exactly what the driver's --m-cap sweep measures (see RunRestirViewport.sh).
void ClampReservoirM(CpuReservoir& Res)
{
    if (g_RestirMCap == 0u || Res.SampleCount <= g_RestirMCap) return;
    Res.WeightSum *= static_cast<float>(g_RestirMCap) / static_cast<float>(Res.SampleCount);
    Res.SampleCount = g_RestirMCap;
}

void ResampleCandidate(CpuReservoir& Res, const vec3& Point, uint32_t Light, float UvU, float UvV, float Weight, Rng& R)
{
    Res.WeightSum   += Weight;
    Res.SampleCount += 1u;
    if (R.Next() * Res.WeightSum <= Weight)
    {
        Res.SelectedPoint = Point;
        Res.SelectedLight = Light;
        Res.SelectedUvU = UvU;
        Res.SelectedUvV = UvV;
    }
}

// DrawDirectCandidate: one candidate, lamp or sun, w = p̂/p with every continuous pdf included.
void DrawDirectCandidate(const ShadingRecord& m, const ResolvedLayers& L, const vec3& Ng, const vec3& T, const vec3& B,
                         const vec3& Ns, const vec3& wo, const vec3& HitPos, bool SunUp, Rng& R,
                         vec3& OutPoint, uint32_t& OutLight, float& OutU, float& OutV, float& OutWeight)
{
    const bool Sun = SunUp && !g_RestirNoSunCoin && (g_Lights.empty() || R.Next() < SunPickChance());
    if (Sun)
    {
        const float U1 = R.Next(), U2 = R.Next();
        // Uniform in the sun's cone (pdf 1/Ω) around the sun direction, basis excluding the dominant axis.
        const float CosMax = cosf(g_SunAngularRadius);
        const float CosT   = 1.0f - U1 * (1.0f - CosMax);
        const float SinT   = sqrtf(max(0.0f, 1.0f - CosT * CosT));
        const float Phi    = 2.0f * kPi * U2;
        vec3 St, Sb;
        ShadingFrame(g_SunDirRender, St, Sb);
        const vec3 SunDir = normalize(St * (SinT * cosf(Phi)) + Sb * (SinT * sinf(Phi)) + g_SunDirRender * CosT);
        OutPoint = HitPos + SunDir * kRestirSunDistance;
        OutLight = kRestirSunLight;
        OutU = U1; OutV = U2;
        const float PHat = PHatSun(m, L, Ng, T, B, Ns, wo, SunEmissionRender(), SunDir);
        const float PPick = g_Lights.empty() ? 1.0f : SunPickChance();
        OutWeight = PHat * SunSolidAngleRender() / PPick;
        return;
    }
    float Pl = 0.0f, Area = 0.0f;
    vec3 Radiance(0.0f);
    const int Li = PickLight(R, Pl, Area, Radiance);
    if (Li < 0) { OutPoint = vec3(0.0f); OutLight = 0u; OutU = OutV = 0.0f; OutWeight = 0.0f; return; }
    const float U1 = R.Next(), U2 = R.Next();
    float Su = U1, Sv = U2;
    if (Su + Sv > 1.0f) { Su = 1.0f - Su; Sv = 1.0f - Sv; }
    const EmissiveTriangle& Q = g_Lights[Li];
    OutPoint = Q.P0 * (1.0f - Su - Sv) + Q.P1 * Su + Q.P2 * Sv;
    OutLight = static_cast<uint32_t>(Li);
    OutU = Su; OutV = Sv;
    const vec3 ToLight = OutPoint - HitPos;
    const float PHat = PHatSurface(m, L, Ng, T, B, Ns, wo, Q.Radiance, ToLight, Q.Ng);
    const float PPick = (SunUp && !g_RestirNoSunCoin) ? 1.0f - SunPickChance() : 1.0f;
    OutWeight = PHat * Area / (Pl * PPick);
}

// The kernel's per-frame sub-pixel jitter (Halton bases 2 and 3, one offset per FRAME shared by the raster and the
//    resolve). Deterministic in the frame index, so a re-run reproduces every pixel.
inline float Halton(int Index, uint32_t Base)
{
    float Result = 0.0f, Fraction = 1.0f;
    int I = Index;
    while (I > 0)
    {
        Fraction /= static_cast<float>(Base);
        Result += Fraction * static_cast<float>(I % static_cast<int>(Base));
        I /= static_cast<int>(Base);
    }
    return Result;
}

// p̂ of a STORED sample — the one question every merge asks.
float PHatSelected(const ShadingRecord& m, const ResolvedLayers& L, const vec3& Ng, const vec3& T, const vec3& B,
                   const vec3& Ns, const vec3& wo, uint32_t Light, const vec3& ToLight)
{
    if (Light == kRestirSunLight)
    {
        const float Dist = max(sqrtf(dot(ToLight, ToLight)), 1.0e-9f);
        return PHatSun(m, L, Ng, T, B, Ns, wo, SunEmissionRender(), ToLight / Dist);
    }
    if (Light >= g_Lights.size()) return 0.0f;
    const EmissiveTriangle& Q = g_Lights[Light];
    return PHatSurface(m, L, Ng, T, B, Ns, wo, Q.Radiance, ToLight, Q.Ng);
}

// The G-buffer the SECOND pass reads: on the GPU that is SurfaceImage/NormalImage plus a fresh ResolveMaterial in
//    the spatial dispatch, so the mirror carries the same quantities (surface point, frame, material, layers).
struct RestirSurface
{
    bool          Valid  = false;
    uint32_t      Identity = 0u;   // D10: which surface this pixel shows (0 = none) — the reservoir's validation key
    int           MaterialIdx = -1;   // [idx] the material of that surface (diagnostics only — see the refusal split)
    // The UV this pixel's primary ray was cast through — the pixel centre plus this frame's jitter. A motion vector is
    //    the surface point's displacement, so it has to be measured from WHERE THE RAY WENT, not from the pixel centre:
    //    mixing the two left a sub-pixel offset in every reprojection, and on a still scene that read a neighbouring
    //    pixel's history about half the time (the raster's motion image carries a vertex's own displacement, which is
    //    why the kernel does not have this problem — this is the mirror being brought into line with it).
    float         RayU = 0.0f, RayV = 0.0f;
    vec3          P{0.0f, 0.0f, 0.0f};
    vec3          Ng{0.0f, 0.0f, 1.0f};
    vec3          T{1.0f, 0.0f, 0.0f};
    vec3          B{0.0f, 1.0f, 0.0f};
    vec3          Ns{0.0f, 0.0f, 1.0f};
    vec3          Wo{0.0f, 0.0f, 1.0f};
    ShadingRecord Mat{};
    ResolvedLayers Layers{};
    vec3          O{0.0f, 0.0f, 0.0f};   // the primary ray origin
    vec3          D{0.0f, 0.0f, -1.0f};   // the primary ray direction (what Radiance walks the indirect half from)
    float         Depth   = 0.0f;
    float         MotionU = 0.0f;
    float         MotionV = 0.0f;
};

// ────────────────────────────────── THE INDIRECT HALF'S RESERVOIR (ReSTIR GI) ─────────────────────────────────────
// The direct reservoir's samples live on the PRIMARY surface; these live on the FIRST-BOUNCE VERTEX — the surface
//    behind the indirect half, whose NEE stratum the mirror used to draw exactly once per frame. Same machinery
//    (RIS, temporal merge, spatial taps, pre-merge cap, temporal-only history), but every target evaluation happens
//    at the RECEIVER'S OWN vertex: that is what lets a neighbour's light sample be reused here at all, and it is the
//    same screen-space transfer approximation the direct path already leans on.
//    The vertex is derived once per frame per pixel from ONE primary BSDF sample. The pixel's indirect half then
//    partitions cleanly around it:
//        indirect = Beta0 · [ pool estimate of the vertex's NEE ] + Beta0 · [ vertex terminal ] + deeper trace from V
//    where the deeper trace starts AT the vertex with the vertex's own BSDF sample (its LastPdf seeded so its
//    emitter-hit MIS pairs with the pool, exactly as the single-sample arm pairs the vertex NEE with it). Pixels
//    whose vertex is unusable (glass, an SSS mat, a degenerate frame) fall back to the single-sample arm — no
//    reuse, no risk of a partition error. `--restir-no-gi-reuse` forces that fallback everywhere, which is the A/B.
struct RestirVertex
{
    bool          Valid   = false;                 // a usable first-bounce vertex exists (opaque, no SSS)
    vec3          P{0.0f, 0.0f, 0.0f};
    vec3          Ng{0.0f, 0.0f, 1.0f};
    vec3          T{1.0f, 0.0f, 0.0f}, B{0.0f, 1.0f, 0.0f}, Ns{0.0f, 0.0f, 1.0f};
    vec3          Wo{0.0f, 0.0f, 1.0f};            // outgoing, toward the primary point
    ShadingRecord Mat{};
    ResolvedLayers Layers{};
    vec3          Beta{0.0f, 0.0f, 0.0f};          // the primary bounce's f·cos/pdf (vertex radiance → pixel radiance)
    float         BsdfPdf   = 0.0f;                // pdf of the primary BSDF sample that reached the vertex
    vec3          WiOut{0.0f, 0.0f, -1.0f};        // the deeper half's world BSDF direction at the vertex
    vec3          DeeperBeta{0.0f, 0.0f, 0.0f};    // that sample's f·cos/pdf
    vec3          Terminal{0.0f, 0.0f, 0.0f};      // the path's terminal value AT the vertex (sky/emitter/unlit)
    float         PrimaryDepth = 0.0f;
};

struct CpuGiReservoir
{
    uint32_t Identity = 0u;                         // D10: the primary surface this vertex/sample belongs to
    vec3     Point{0.0f, 0.0f, 0.0f};               // the world light point the sample selected
    uint32_t Light = kRestirSunLight;
    float    WeightSum = 0.0f;
    uint32_t SampleCount = 0u;
    float    UnbiasedWeight = 0.0f;
    uint32_t Visible = 0u;
    uint32_t Age = 0u;
    vec3     Normal{0.0f, 0.0f, 1.0f};               // the PRIMARY surface's geometric normal (validation)
    vec3     Vertex{0.0f, 0.0f, 0.0f};               // the vertex the sample was drawn at (validation)
    vec3     VertexNg{0.0f, 0.0f, 1.0f};
    float    Depth = 0.0f;                           // the primary depth there
    float    MotionU = 0.0f, MotionV = 0.0f;
    float    StrideWidth = 0.0f;                     // the kernel's stride guard
};

void ResampleGiCandidate(CpuGiReservoir& Res, const vec3& Point, uint32_t Light, float Weight, Rng& R)
{
    Res.WeightSum   += Weight;
    Res.SampleCount += 1u;
    if (R.Next() * Res.WeightSum <= Weight)
    {
        Res.Point = Point;
        Res.Light = Light;
    }
}

void ClampGiReservoirM(CpuGiReservoir& Res)
{
    if (g_RestirMCap == 0u || Res.SampleCount <= g_RestirMCap) return;
    Res.WeightSum *= static_cast<float>(g_RestirMCap) / static_cast<float>(Res.SampleCount);
    Res.SampleCount = g_RestirMCap;
}

// ── D10: the moving object, shared by the sequence and the shadow probe ───────────────────────────────────────────────
// One definition of "what moved and where to", so the render and the shadow test cannot drift apart.
struct DriftCapture
{
    std::vector<int>  Triangles;
    std::vector<vec3> RestP0, RestP1, RestP2;
    vec3 Axis{1.0f, 0.0f, 0.0f};   // the in-plane axis the object slides along (its own dominant extent)
    vec3 Lo{0.0f, 0.0f, 0.0f}, Hi{0.0f, 0.0f, 0.0f};
    [[nodiscard]] bool Empty() const { return Triangles.empty(); }
};

DriftCapture CaptureDrift(int Material)
{
    DriftCapture C;
    vec3 Lo(1.0e30f, 1.0e30f, 1.0e30f), Hi(-1.0e30f, -1.0e30f, -1.0e30f);
    for (size_t I = 0; I < g_Tris.size(); ++I)
    {
        if (g_Tris[I].Material != Material) continue;
        const RenderTriangle& T = g_Tris[I];
        C.Triangles.push_back(static_cast<int>(I));
        C.RestP0.push_back(T.P0); C.RestP1.push_back(T.P1); C.RestP2.push_back(T.P2);
        const vec3 Corners[3]{ T.P0, T.P1, T.P2 };
        for (const vec3& V : Corners)
        {
            Lo = vec3(min(Lo.x, V.x), min(Lo.y, V.y), min(Lo.z, V.z));
            Hi = vec3(max(Hi.x, V.x), max(Hi.y, V.y), max(Hi.z, V.z));
        }
    }
    C.Lo = Lo;
    C.Hi = Hi;
    // A flat swatch's own longest extent is the in-plane one, so sliding along it keeps the object in its own plane —
    //    which is the HARD case on purpose: the surfaces it slides across share its normal and its depth, so the
    //    normal/depth rule alone cannot tell them apart.
    if (!C.Empty())
    {
        const vec3 Extent = Hi - Lo;
        if      (g_RestirDriftAxis == 0) C.Axis = vec3(1.0f, 0.0f, 0.0f);
        else if (g_RestirDriftAxis == 1) C.Axis = vec3(0.0f, 1.0f, 0.0f);
        else if (g_RestirDriftAxis == 2) C.Axis = vec3(0.0f, 0.0f, 1.0f);
        else if (Extent.y >= Extent.x && Extent.y >= Extent.z) C.Axis = vec3(0.0f, 1.0f, 0.0f);
        else if (Extent.z >= Extent.x && Extent.z >= Extent.y) C.Axis = vec3(0.0f, 0.0f, 1.0f);
        else                                                   C.Axis = vec3(1.0f, 0.0f, 0.0f);
    }
    return C;
}

// Writes the object to rest + Offset and rebuilds the acceleration structure, so primary rays AND every shadow ray see
//    it where it actually is this frame. A moving object whose shadows lagged its geometry would be the other half of
//    the acceptance test this milestone is judged by.
void ApplyDrift(const DriftCapture& C, const vec3& Offset)
{
    if (C.Empty()) return;
    for (size_t I = 0; I < C.Triangles.size(); ++I)
    {
        RenderTriangle& T = g_Tris[static_cast<size_t>(C.Triangles[I])];
        T.P0 = C.RestP0[I] + Offset;
        T.P1 = C.RestP1[I] + Offset;
        T.P2 = C.RestP2[I] + Offset;
    }
    for (size_t I = 0; I < g_Order.size(); ++I) g_Order[I] = static_cast<int>(I);
    BuildBvh(0, 0, static_cast<int>(g_Order.size()));
}

// ── THE SPLIT. The kernel's two reuse passes are two DISPATCHES: temporal reuse writes CurrReservoirs, and the
//    spatial pass reads them. Until this change the mirror ran both inside one per-sample call and — like the
//    kernel — wrote the POST-SPATIAL reservoir into the history the next frame's temporal merge reads. That is the
//    feedback loop that broke convergence, and it is measurable: taps 0 converges to 1952 RMSE by 128 frames while
//    2 taps stall at 4437 and 4 at 4660, and mean M reached 843 633 by frame 20.
//
//    Two things follow from separating the passes, and both are the published form:
//      · History carries the TEMPORAL reservoir only. Spatial reuse stays a shading-time refinement, so a frame's
//        spatial merges cannot inflate the M that the next frame's temporal cap reasons about. (Kajiya's ReSTIR
//        notes: "don't feed spatial back into temporal unless starved for samples".)
//      · A tap's M cap is taken against the RECEIVER'S PRE-MERGE count, not a count that grows inside the tap loop.
//        The old form let tap 2 add up to 20x what tap 1 had just added, compounding within a single frame.
//    `--restir-no-history-split` restores the old feedback (history := post-spatial) so the fix can be measured.
struct RestirFrameState
{
    std::vector<CpuReservoir> Temporal;     // THIS frame's post-temporal reservoirs — pass 1's output, pass 2's input
    std::vector<CpuReservoir> History;      // LAST frame's post-temporal reservoirs — what temporal reuse reads
    std::vector<CpuGiReservoir> GiTemporal; // the indirect pool's temporal reserves (same split as the direct one)
    std::vector<CpuGiReservoir> GiHistory;  // LAST frame's GiTemporal — always pre-spatial: the fix is not optional
    std::vector<RestirVertex>   Vertex;     // this frame's first-bounce vertices
    std::vector<RestirVertex>   VertexHistory;   // last frame's, for the vertex-proximity validation of a tap
    std::vector<RestirSurface> Surface;     // the G-buffer pass 2 reads (see RestirSurface)
    std::vector<unsigned char> GiClass;     // roadmap #5: this frame's class per pixel (see kGiClass* — diagnostic)
    CameraPose PreviousPose;
    CameraPose LastPose;
    int        FrameIndex  = 0;
    bool       HasPrevious = false;
};

// ─────────────────────────────────────────── PASS 1 — candidates, RIS, W, TEMPORAL ───────────────────────────────
// The kernel's sections 1-3 and its temporal reuse row, per pixel, writing the reservoir THIS FRAME PUBLISHES for
//    next frame's temporal reuse (CurrReservoirs). The spatial pass is deliberately not folded in here — see the
//    RestirFrameState comment for why that single change is the convergence fix.
CpuReservoir RestirTemporalReservoir(const RestirSurface& Surface, int Candidates, bool SunUp, RestirFrameState& State,
                                     int Width, int Height, int X, int Y, Rng& R)
{
    const vec3& P  = Surface.P;
    const vec3& Ng = Surface.Ng;
    const vec3& T  = Surface.T;
    const vec3& B  = Surface.B;
    const vec3& Ns = Surface.Ns;
    const vec3& wo = Surface.Wo;
    const ShadingRecord& m = Surface.Mat;
    const ResolvedLayers& L = Surface.Layers;

    CpuReservoir Res;
    Res.StrideWidth = static_cast<float>(Width);   // the kernel's prev.Normal.w stride guard
    for (int S = 0; S < Candidates; ++S)
    {
        vec3 Point; uint32_t Light; float U1, U2, Weight;
        DrawDirectCandidate(m, L, Ng, T, B, Ns, wo, P, SunUp, R, Point, Light, U1, U2, Weight);
        ResampleCandidate(Res, Point, Light, U1, U2, Weight, R);
    }

    float SelectedPHat = 0.0f;
    if (Res.WeightSum > 0.0f && Res.SampleCount > 0u)
    {
        SelectedPHat = PHatSelected(m, L, Ng, T, B, Ns, wo, Res.SelectedLight, Res.SelectedPoint - P);
        Res.UnbiasedWeight = SelectedPHat > 0.0f ? Res.WeightSum / (static_cast<float>(Res.SampleCount) * SelectedPHat) : 0.0f;

        // Motion: the pixel this surface point landed on in the PREVIOUS pose (the R2 image's content, computed exactly).
        float PrevU = 0.0f, PrevV = 0.0f;
        if (State.HasPrevious && ProjectPoint(State.PreviousPose, P, PrevU, PrevV))
        {
            // From the ray the pixel actually took (Surface.RayU/V), not from the pixel centre.
            Res.MotionU = Surface.RayU - PrevU;
            Res.MotionV = Surface.RayV - PrevV;
        }
        Res.Normal   = Ng;
        Res.Depth    = Surface.Depth;
        Res.Identity = Surface.Identity;   // D10: the surface this reservoir belongs to (0 = none)

        if (State.HasPrevious)
        {
            const float CurU = (static_cast<float>(X) + 0.5f) / static_cast<float>(Width);
            const float CurV = (static_cast<float>(Y) + 0.5f) / static_cast<float>(Height);
            const float PU = g_RestirNoReproject ? CurU : CurU - Res.MotionU;
            const float PV = g_RestirNoReproject ? CurV : CurV - Res.MotionV;
            const int PrevX = static_cast<int>(floorf(PU * static_cast<float>(Width)));
            const int PrevY = static_cast<int>(floorf(PV * static_cast<float>(Height)));
            if (PrevX >= 0 && PrevY >= 0 && PrevX < Width && PrevY < Height)
            {
                const CpuReservoir& Prev = State.History[static_cast<size_t>(PrevY) * Width + PrevX];
                const uint32_t PrevM = Prev.SampleCount;
                const bool GeometryOk = Prev.StrideWidth == static_cast<float>(Width)
                    && dot(Ng, Prev.Normal) > kRestirNormalCos
                    && fabsf(Surface.Depth - Prev.Depth) / max(Surface.Depth, 1.0e-3f) < kRestirDepthTol;
                const bool IdentityDiffers = Prev.Identity != Surface.Identity;
                const bool IdentityOk = !g_RestirIdentity || !IdentityDiffers;
                // #6 the dead-entry rule (see g_RestirDeadSkip): a blocked history carries W = 0 and only its M.
                const bool DeadOk = !g_RestirDeadSkip || Prev.UnbiasedWeight > 0.0f;
                if (PrevM > 0u && GeometryOk && IdentityOk && !DeadOk) ++g_DeadSkipDirect;
                const bool Valid = PrevM > 0u && GeometryOk && IdentityOk && DeadOk;
                // D10: a temporal merge that the geometry test alone would have allowed and the identity refuses. With
                //    the rule off this is the count of light samples the pixel inherited from ANOTHER surface.
                // D10: the case the rule exists for — a merge the GEOMETRY test allowed and the identity refuses. Off
                //    screen this is the count of light samples the pixel inherited from another object last frame.
                if (PrevM > 0u && GeometryOk && IdentityDiffers) ++g_IdentityRefusedDirect;
                if (Valid)
                {
                    const uint32_t PrevCapped = std::min(PrevM, kRestirMClamp * Res.SampleCount);
                    const float PPrev = PHatSelected(m, L, Ng, T, B, Ns, wo, Prev.SelectedLight, Prev.SelectedPoint - P);
                    const float WPrev = PPrev * Prev.UnbiasedWeight * static_cast<float>(PrevCapped);
                    const float WCur  = SelectedPHat * Res.UnbiasedWeight * static_cast<float>(Res.SampleCount);
                    const float Total = WCur + WPrev;
                    if (Total > 0.0f && R.Next() * Total <= WPrev)
                    {
                        Res.SelectedPoint = Prev.SelectedPoint;
                        Res.SelectedLight = Prev.SelectedLight;
                        Res.SelectedUvU   = Prev.SelectedUvU;
                        Res.SelectedUvV   = Prev.SelectedUvV;
                        SelectedPHat      = PPrev;
                    }
                    Res.SampleCount += PrevCapped;
                    Res.WeightSum    = Total;
                    ClampReservoirM(Res);
                    Res.Age = Prev.Age + 1u;
                    Res.UnbiasedWeight = SelectedPHat > 0.0f
                        ? Res.WeightSum / (static_cast<float>(Res.SampleCount) * SelectedPHat) : 0.0f;
                }
            }
        }
    }

    // VISIBILITY REUSE (kernel parity): the post-temporal trace — the reservoir this frame PUBLISHES carries an
    //    honest visibility bit, and a blocked one publishes W = 0 so no later merge (temporal next frame, spatial
    //    this frame) can be won by an occluded sample. This is the mirror of the kernel's histBlocked block; the
    //    spatial pass then TRUSTS it instead of re-tracing, so the per-pixel DI shadow-ray count stays at one.
    //    Under --restir-final-visibility (the old arm) the trace stays where it was — at the end of the spatial
    //    pass — and this block is skipped, which is exactly the pre-reuse mirror, ray for ray.
    if (!g_RestirFinalVisibility && Res.SampleCount > 0u && Res.UnbiasedWeight > 0.0f)
    {
        const bool Blocked = Res.SelectedLight == kRestirSunLight
            ? Occluded(P + Ng * 1.0e-4f, P + normalize(Res.SelectedPoint - P) * 1.0e4f)
            : Occluded(P + Ng * 1.0e-4f, Res.SelectedPoint - normalize(Res.SelectedPoint - P) * 1.0e-3f);
        Res.Visible = Blocked ? 0u : 1u;
        if (Blocked) Res.UnbiasedWeight = 0.0f;
    }
    return Res;
}

// ─────────────────────────────────────────── PASS 2 — spatial reuse + shade ───────────────────────────────────────
// The kernel's row 3, reading pass 1's output through the dispatch boundary (State.Temporal) so no tap can read a
//    neighbour's half-merged state, and capping each tap against the receiver's PRE-merge sample count.
vec3 RestirSpatialShade(const CpuReservoir& Temporal, const RestirSurface& Surface, bool SunUp, RestirFrameState& State,
                        int Width, int Height, int X, int Y, Rng& R, CpuReservoir& OutPublished)
{
    const vec3& P  = Surface.P;
    const vec3& Ng = Surface.Ng;
    const vec3& T  = Surface.T;
    const vec3& B  = Surface.B;
    const vec3& Ns = Surface.Ns;
    const vec3& wo = Surface.Wo;
    const ShadingRecord& m = Surface.Mat;
    const ResolvedLayers& L = Surface.Layers;

    CpuReservoir Res = Temporal;
    float SelectedPHat = Res.SampleCount > 0u && Res.UnbiasedWeight > 0.0f
        ? PHatSelected(m, L, Ng, T, B, Ns, wo, Res.SelectedLight, Res.SelectedPoint - P) : 0.0f;

    {
        // Seeded from the pixel AND the frame, like the kernel's tapSeed (MakeSeed(pixel, FrameIndex)): the cross is
        //    re-aimed and re-reached every frame, so no direction is systematically favoured over time.
        Rng TapRng((static_cast<uint32_t>(Y) * 73856093u) ^ (static_cast<uint32_t>(X) * 19349663u)
                   ^ (static_cast<uint32_t>(State.FrameIndex + 1) * 83492791u) ^ 0x9E3779B9u);
        const float Scale  = static_cast<float>(Width) / 1280.0f;
        const float Angle  = TapRng.Next() * 6.28318531f;
        const float Radius = (kRestirRadiusMinPx + (kRestirRadiusMaxPx - kRestirRadiusMinPx) * TapRng.Next()) * Scale;
        const uint32_t Taps = std::min<uint32_t>(g_RestirSpatialTaps, kRestirTapCeiling);

        // ⚠ The cap reference is the receiver's PRE-merge count. Capping against Res.SampleCount while the loop adds
        //   to it lets tap 2 scale up to 20x what tap 1 just added — compounding inside one frame, which is the
        //   other half of the runaway this revision removes.
        const uint32_t CapReference = Temporal.SampleCount;
        for (uint32_t Tap = 0u; Tap < Taps; ++Tap)
        {
            const float Theta = Angle + static_cast<float>(Tap) * (6.28318531f / static_cast<float>(Taps));
            const int OffX = static_cast<int>(lroundf(Radius * cosf(Theta)));
            const int OffY = static_cast<int>(lroundf(Radius * sinf(Theta)));
            if (OffX == 0 && OffY == 0) continue;
            const int NX = X + OffX, NY = Y + OffY;
            if (NX < 0 || NY < 0 || NX >= Width || NY >= Height) continue;
            const CpuReservoir& Neigh = State.Temporal[static_cast<size_t>(NY) * Width + NX];
            const uint32_t NeighM = Neigh.SampleCount;
            if (g_RestirDeadSkip && NeighM > 0u && Neigh.UnbiasedWeight <= 0.0f) { ++g_DeadSkipDirect; continue; }   // #6
            const bool NValid = NeighM > 0u
                && Neigh.StrideWidth == static_cast<float>(Width)
                && dot(Ng, Neigh.Normal) > kRestirNormalCos
                && fabsf(Surface.Depth - Neigh.Depth) / max(Surface.Depth, 1.0e-3f) < kRestirDepthTol;
            if (!NValid) continue;
            const uint32_t NeighCapped = std::min(NeighM, kRestirMClamp * CapReference);
            const float PNeigh = PHatSelected(m, L, Ng, T, B, Ns, wo, Neigh.SelectedLight, Neigh.SelectedPoint - P);
            const float WSelf  = SelectedPHat * Res.UnbiasedWeight * static_cast<float>(Res.SampleCount);
            const float WNeigh = PNeigh * Neigh.UnbiasedWeight * static_cast<float>(NeighCapped);
            const float NTotal = WSelf + WNeigh;
            uint32_t TakeAge = Res.Age;
            if (NTotal > 0.0f && R.Next() * NTotal <= WNeigh)
            {
                Res.SelectedPoint = Neigh.SelectedPoint;
                Res.SelectedLight = Neigh.SelectedLight;
                Res.SelectedUvU   = Neigh.SelectedUvU;
                Res.SelectedUvV   = Neigh.SelectedUvV;
                TakeAge    = Neigh.Age + 1u;
                SelectedPHat = PNeigh;
                // VISIBILITY REUSE: trust the winning tap's stored test. A blocked neighbour published W = 0, so
                //    its WNeigh was exactly 0 and it cannot land here — any winner was visible from its own pixel.
                if (!g_RestirFinalVisibility) Res.Visible = 1u;
            }
            Res.SampleCount += NeighCapped;
            Res.WeightSum    = NTotal;
            ClampReservoirM(Res);
            Res.Age = TakeAge;
            Res.UnbiasedWeight = SelectedPHat > 0.0f
                ? Res.WeightSum / (static_cast<float>(Res.SampleCount) * SelectedPHat) : 0.0f;
        }
    }

    if (g_RestirFinalVisibility)
    {
        // The pre-reuse arm, verbatim: one shadow ray for whichever sample the merges selected, every pixel.
        const bool Blocked = Res.SelectedLight == kRestirSunLight
            ? Occluded(P + Ng * 1.0e-4f, P + normalize(Res.SelectedPoint - P) * 1.0e4f)
            : Occluded(P + Ng * 1.0e-4f, Res.SelectedPoint - normalize(Res.SelectedPoint - P) * 1.0e-3f);
        Res.Visible = Blocked ? 0u : 1u;
        if (Res.Visible == 0u) Res.UnbiasedWeight = 0.0f;
    }
    else
    {
        // Visibility reuse: the temporal reservoir was traced at THIS pixel this frame (exact), and a spatial
        //    winner carries its own pixel's test (approximate by at most the tap radius). No ray here — count it.
        ++g_VisibilityReuseSaved;
        if (Res.Visible == 0u) Res.UnbiasedWeight = 0.0f;
    }
    OutPublished = Res;

    vec3 Acc(0.0f);
    if (Res.Visible == 1u && Res.SampleCount > 0u)
    {
        const vec3 ShadeDir = normalize(Res.SelectedPoint - P);
        const float ShadeCos = max(0.0f, dot(Ng, ShadeDir));
        const vec3 Wi(dot(ShadeDir, T), dot(ShadeDir, B), dot(ShadeDir, Ns));
        const vec3 F = Wi.z > 0.0f ? EvaluateBsdf(m, L, wo, Wi) : vec3(0.0f);
        if (Res.SelectedLight == kRestirSunLight)
            Acc = F * SunEmissionRender() * ShadeCos * Res.UnbiasedWeight;
        else
        {
            const EmissiveTriangle& Q = g_Lights[Res.SelectedLight];
            const float ShadeCosL = max(0.0f, dot(Q.Ng, -ShadeDir));
            const float Dist2 = dot(Res.SelectedPoint - P, Res.SelectedPoint - P);
            Acc = F * Q.Radiance * ShadeCos * ShadeCosL * Res.UnbiasedWeight / (Dist2 + 0.01f);
        }
    }
    (void)SunUp;
    return Acc;
}

// ────────────────────────────── PASS 1b — the vertex, its candidates, RIS, TEMPORAL ───────────────────────────────
// Mirrors Radiance()'s depth-1 surface resolution (frame, layers, SSS chord, the emissive-only/unlit short
//    circuits) and then runs the same candidate drawer the direct reservoir uses, at the VERTEX instead of the
//    primary point. Writes this frame's vertex G-buffer entry and the GI reservoir the history will take.
CpuGiReservoir RestirGiTemporalReservoir(const RestirSurface& Surface, int Candidates, bool SunUp,
                                            RestirFrameState& State, int Width, int Height, int X, int Y, Rng& R)
{
    CpuGiReservoir Res;
    Res.StrideWidth = static_cast<float>(Width);
    RestirVertex& V = State.Vertex[static_cast<size_t>(Y) * Width + X];
    V = RestirVertex{};
    V.PrimaryDepth = Surface.Depth;
    // Roadmap #5: every return below labels the pixel before it leaves, so the map covers the frame exactly like the
    //    counters do. The lambda is the only writer, which is what keeps the two in step.
    const size_t ClassPixel = static_cast<size_t>(Y) * Width + X;
    bool ClassSet = false;
    const auto MarkClass = [&](unsigned char C) { State.GiClass[ClassPixel] = C; ClassSet = true; };

    const ShadingRecord& m = Surface.Mat;
    const ResolvedLayers& L = Surface.Layers;
    const vec3 wo = Surface.Wo;

    // One primary BSDF sample — the same block Radiance() runs at depth 0, kept in step with it by hand.
    const vec4 S = SampleBsdf(m, L, wo, vec4(R.Next(), R.Next(), R.Next(), R.Next()));
    if (S.w <= 0.0f) { ++g_GiBad; MarkClass(kGiClassBad); return Res; }
    const vec3 wi = S.xyz;
    const vec3 F = EvaluateBsdf(m, L, wo, wi);
    const float CosS = wi.z < 0.0f ? -wi.z : wi.z;
    const vec3 Beta = F * (CosS / max(S.w, 1e-12f));
    V.Beta = Beta;
    if (Beta.x <= 0.0f && Beta.y <= 0.0f && Beta.z <= 0.0f) { ++g_GiBad; MarkClass(kGiClassBad); return Res; }
    const vec3 Dir = Surface.T * wi.x + Surface.B * wi.y + Surface.Ns * wi.z;

    // The vertex's own terminal cases come first: a BSDF-sampled ray that escapes or lands on an emitter ends the
    //    path at the vertex, and neither case is something a light-sample pool can estimate.
    const Hit H = Intersect(Surface.P + Surface.Ng * 1.0e-4f, Dir, 1e30f, -1);
    V.WiOut = Dir;
    if (!H.Valid)
    {
        vec3 E = SkyRadianceWorld(Dir);
        if (g_SunNee && dot(Dir, g_SunDirRender) > cosf(g_SunAngularRadius))
        {
            const float PdfSolid = 1.0f / (2.0f * kPi * (1.0f - cosf(g_SunAngularRadius)));
            const float W = (S.w * S.w) / (S.w * S.w + PdfSolid * PdfSolid + 1e-12f);
            E = E * W;
        }
        V.Terminal = Beta * E;
        ++g_GiEscape;
        MarkClass(kGiClassEscape);
        return Res;
    }

    const RenderTriangle& Rt = g_Tris[H.TriId];
    const vec3 VP = Surface.P + Dir * H.T;
    if (Rt.Light >= 0)
    {
        const vec3 Ng = normalize(cross(Rt.P1 - Rt.P0, Rt.P2 - Rt.P0));
        if (dot(Dir, Ng) < 0.0f && !g_RestirBounceMis)
        {
            // The MIS weight pairs this emitter hit with the primary's NEE — which the DIRECT reservoir owns in
            //    this variant, exactly as the single-sample arm pairs it with the primary's DirectMIS/SunNee.
            const EmissiveTriangle& Q = g_Lights[Rt.Light];
            const float CosL = dot(-Dir, Q.Ng);
            const float PdfOmega = (Q.Power / TotalLightPower()) * (H.T * H.T / (Q.Area * max(CosL, 1e-6f)));
            const float W = (S.w * S.w) / (PdfOmega * PdfOmega + S.w * S.w + 1e-12f);
            V.Terminal = Beta * g_Mat[Rt.Material].Emission * W;
        }
        ++g_GiEmitter;
        MarkClass(kGiClassEmitter);
        return Res;
    }

    vec3 VNg = normalize(cross(Rt.P1 - Rt.P0, Rt.P2 - Rt.P0));
    if (dot(VNg, Dir) > 0.0f) VNg = -VNg;
    vec3 VNs = normalize(Rt.N0 * (1.0f - H.U - H.V) + Rt.N1 * H.U + Rt.N2 * H.V);
    if (dot(VNs, Dir) > 0.0f) VNs = -VNs;
    vec3 VT, VB;
    ShadingFrame(VNs, VT, VB);

    ShadingRecord vm = g_Mat[Rt.Material];
    BindAutomotiveHit(vm,Rt,H.U,H.V,H.T,Dir,VNs);
    if(g_MatGlintsOff)vm.AutomotiveDensity=0;
    if (vm.Selection == static_cast<uint>(kReflectanceEmissiveOnly))
    {
        V.Terminal = Beta * vm.Emission;
        ++g_GiUnlit;
        MarkClass(kGiClassUnlit);
        return Res;
    }
    if (vm.Selection == static_cast<uint>(kReflectanceUnlit))
    {
        V.Terminal = Beta * vm.BaseColor;
        ++g_GiUnlit;
        MarkClass(kGiClassUnlit);
        return Res;
    }
    // Glass and SSS vertices are out of scope for the pool: the pool's candidates are surface/sun light samples,
    //    so an SSS vertex would silently lose the below stratum, and a dielectric vertex is a refraction site, not
    //    a NEE site. Those pixels keep the single-sample arm (see the caller).
    if (vm.TransmissionWeight > 0.0f || vm.SssWeight > 0.0f || L.SolidInterface)
    {
        ++g_GiUnusable;
        MarkClass(kGiClassUnusable);
        return Res;
    }

    const vec3 VWo(dot(-Dir, VT), dot(-Dir, VB), dot(-Dir, VNs));
    const ResolvedLayers VL = ResolveLayers(vm, VWo);
    V.P = VP; V.Ng = VNg; V.Ns = VNs; V.T = VT; V.B = VB; V.Wo = VWo;
    V.Mat = vm; V.Layers = VL; V.Beta = Beta; V.BsdfPdf = S.w;

    // The deeper half's BSDF sample at the vertex. Its weight carries the vertex's f·cos/pdf so the sub-trace can
    //    be handed exactly that Beta, and its pdf seeds the sub-trace's LastPdf — that is what keeps the emitter-hit
    //    MIS at the SECOND vertex paired with the pool's NEE at this one.
    const vec4 S2 = SampleBsdf(vm, VL, VWo, vec4(R.Next(), R.Next(), R.Next(), R.Next()));
    if (S2.w > 0.0f)
    {
        const vec3 wi2 = S2.xyz;
        const vec3 F2 = EvaluateBsdf(vm, VL, VWo, wi2);
        const float Cos2 = wi2.z < 0.0f ? -wi2.z : wi2.z;
        V.DeeperBeta = Beta * F2 * (Cos2 / max(S2.w, 1e-12f));
        V.WiOut = VT * wi2.x + VB * wi2.y + VNs * wi2.z;
    }

    Res.Point = vec3(0.0f); Res.Light = kRestirSunLight;
    for (int C = 0; C < Candidates; ++C)
    {
        vec3 Point; uint32_t Light; float U1, U2, Weight;
        DrawDirectCandidate(vm, VL, VNg, VT, VB, VNs, VWo, VP, SunUp, R, Point, Light, U1, U2, Weight);
        ResampleGiCandidate(Res, Point, Light, Weight, R);
    }

    float SelectedPHat = 0.0f;
    if (Res.WeightSum > 0.0f && Res.SampleCount > 0u)
    {
        SelectedPHat = PHatSelected(vm, VL, VNg, VT, VB, VNs, VWo, Res.Light, Res.Point - VP);
        Res.UnbiasedWeight = SelectedPHat > 0.0f
            ? Res.WeightSum / (static_cast<float>(Res.SampleCount) * SelectedPHat) : 0.0f;
        Res.Normal   = Surface.Ng;
        Res.Vertex   = VP;
        Res.VertexNg = VNg;
        Res.Depth    = Surface.Depth;
        Res.Identity = Surface.Identity;   // D10
        V.Valid = true;   // a reusable vertex exists — the caller can spend it on the pool

        ++g_GiVertex;
        MarkClass(kGiClassVertex);
        if (SelectedPHat <= 0.0f) ++g_GiNoPHat;
        float PrevU = 0.0f, PrevV = 0.0f;
        if (State.HasPrevious && ProjectPoint(State.PreviousPose, Surface.P, PrevU, PrevV))
        {
            Res.MotionU = Surface.RayU - PrevU;
            Res.MotionV = Surface.RayV - PrevV;
        }

        if (State.HasPrevious)
        {
            const float CurU = (static_cast<float>(X) + 0.5f) / static_cast<float>(Width);
            const float CurV = (static_cast<float>(Y) + 0.5f) / static_cast<float>(Height);
            const float PU = g_RestirNoReproject ? CurU : CurU - Res.MotionU;
            const float PV = g_RestirNoReproject ? CurV : CurV - Res.MotionV;
            const int PrevX = static_cast<int>(floorf(PU * static_cast<float>(Width)));
            const int PrevY = static_cast<int>(floorf(PV * static_cast<float>(Height)));
            if (PrevX >= 0 && PrevY >= 0 && PrevX < Width && PrevY < Height)
            {
                const size_t PrevPixel = static_cast<size_t>(PrevY) * Width + PrevX;
                const CpuGiReservoir& Prev = State.GiHistory[PrevPixel];
                const RestirVertex& PV2 = State.VertexHistory[PrevPixel];
                // ⚠️ An indirect sample is only transferable between vertices that are the SAME PLACE: the primary
                //    depth/normal test alone would happily merge across a shadow boundary's two different hits.
                // ⚠️ NO vertex-proximity test. The quantity being transferred is a LIGHT SAMPLE (a world point on
                //    an emitter), not a direction: the receiver re-evaluates its own target function against that
                //    point and re-traces the shadow ray, which is the validity test the direct pool uses for its
                //    taps too. Requiring the two vertices to be near-identical ("the same place") sounds stricter
                //    but is the wrong test here, because each frame redraws its own primary BSDF direction: with it
                //    in place, 6 989 of 20 831 temporal attempts failed on vertex distance alone and the pool
                //    bought almost nothing. Research note kept: the stricter form belongs with a replay + shift
                //    mapping (ReSTIR GI proper), not with a light-sample pool.
                const bool GeometryOk = PV2.Valid
                    && Prev.StrideWidth == static_cast<float>(Width)
                    && dot(Surface.Ng, Prev.Normal) > kRestirNormalCos
                    && fabsf(Surface.Depth - Prev.Depth) / max(Surface.Depth, 1.0e-3f) < kRestirDepthTol;
                const bool IdentityDiffers = Prev.Identity != Surface.Identity;
                const bool IdentityOk = !g_RestirIdentity || !IdentityDiffers;
                // #6 the dead-entry rule (see g_RestirDeadSkip): an occluded pool entry carries W = 0 and only its M.
                const bool DeadOk = !g_RestirDeadSkip || Prev.UnbiasedWeight > 0.0f;
                if (Prev.SampleCount > 0u && GeometryOk && IdentityOk && !DeadOk) ++g_DeadSkipGi;
                const bool Valid = Prev.SampleCount > 0u && GeometryOk && IdentityOk && DeadOk;
                if (Prev.SampleCount > 0u)
                {
                    ++g_GiTemporalTried;
                    if (!PV2.Valid) ++g_GiTemporalNoVertexHistory;
                    if (Valid)      ++g_GiTemporalMerged;
                }
                // D10, and here the sample is a light point chosen at the OTHER object's first-bounce vertex — the
                //    reflection-side ghost. Counted in both arms, honoured in one.
                if (Prev.SampleCount > 0u && GeometryOk && IdentityDiffers) ++g_IdentityRefusedGi;
                if (Valid)
                {
                    const uint32_t PrevCapped = std::min(Prev.SampleCount, kRestirMClamp * Res.SampleCount);
                    const float PPrev = PHatSelected(vm, VL, VNg, VT, VB, VNs, VWo, Prev.Light, Prev.Point - VP);
                    const float WPrev = PPrev * Prev.UnbiasedWeight * static_cast<float>(PrevCapped);
                    const float WCur  = SelectedPHat * Res.UnbiasedWeight * static_cast<float>(Res.SampleCount);
                    const float Total = WCur + WPrev;
                    if (Total > 0.0f && R.Next() * Total <= WPrev)
                    {
                        Res.Point = Prev.Point;
                        Res.Light = Prev.Light;
                        SelectedPHat = PPrev;
                    }
                    Res.SampleCount += PrevCapped;
                    Res.WeightSum    = Total;
                    ClampGiReservoirM(Res);
                    Res.Age = Prev.Age + 1u;
                    Res.UnbiasedWeight = SelectedPHat > 0.0f
                        ? Res.WeightSum / (static_cast<float>(Res.SampleCount) * SelectedPHat) : 0.0f;
                }
            }
        }
    }
    // ⚠️ Everything above labels itself; this is the one case that has no counter of its own, and it must not be left
    //    showing the enum's default. Measured (240x135, 32 frames): 762 pixels/frame on average — about 4 % of the
    //    surface — reach the end with a usable vertex but an empty candidate set. Before this line the map called them
    //    "bad" and the map's tally disagreed with `g_GiBad` by exactly that margin, which is how the label was caught:
    //    the two are meant to be checkable against each other, and now they are.
    if (!ClassSet) MarkClass(kGiClassNoSample);
    return Res;
}

// ─────────────────────────────────── PASS 3 — the indirect pool's taps and shade ────────────────────────────────
// Reads pass 1b's output through the dispatch boundary, exactly like the direct path: spatial reuse is a
//    shading-time refinement, so the history never sees it.
vec3 RestirGiSpatialShade(const CpuGiReservoir& Temporal, const RestirSurface& Surface, const RestirVertex& V,
                          bool SunUp, RestirFrameState& State, int Width, int Height, int X, int Y, Rng& R,
                          CpuGiReservoir& OutPublished)
{
    (void)SunUp;
    CpuGiReservoir Res = Temporal;
    const ShadingRecord& m = V.Mat;
    const ResolvedLayers& L = V.Layers;
    const vec3& VP = V.P;
    const vec3& VNg = V.Ng;
    const vec3& VT = V.T;
    const vec3& VB = V.B;
    const vec3& VNs = V.Ns;
    const vec3& VWo = V.Wo;

    float SelectedPHat = Res.SampleCount > 0u && Res.UnbiasedWeight > 0.0f
        ? PHatSelected(m, L, VNg, VT, VB, VNs, VWo, Res.Light, Res.Point - VP) : 0.0f;

    {
        Rng TapRng((static_cast<uint32_t>(Y) * 73856093u) ^ (static_cast<uint32_t>(X) * 19349663u)
                   ^ (static_cast<uint32_t>(State.FrameIndex + 1) * 83492791u) ^ 0x85EBCA6Bu);
        const float Scale  = static_cast<float>(Width) / 1280.0f;
        const float Angle  = TapRng.Next() * 6.28318531f;
        const float Radius = (kRestirRadiusMinPx + (kRestirRadiusMaxPx - kRestirRadiusMinPx) * TapRng.Next()) * Scale;
        const uint32_t Taps = std::min<uint32_t>(g_RestirSpatialTaps, kRestirTapCeiling);
        const uint32_t CapReference = Temporal.SampleCount;
        for (uint32_t Tap = 0u; Tap < Taps; ++Tap)
        {
            const float Theta = Angle + static_cast<float>(Tap) * (6.28318531f / static_cast<float>(Taps));
            const int OffX = static_cast<int>(lroundf(Radius * cosf(Theta)));
            const int OffY = static_cast<int>(lroundf(Radius * sinf(Theta)));
            if (OffX == 0 && OffY == 0) continue;
            const int NX = X + OffX, NY = Y + OffY;
            if (NX < 0 || NY < 0 || NX >= Width || NY >= Height) continue;
            const CpuGiReservoir& Neigh = State.GiTemporal[static_cast<size_t>(NY) * Width + NX];
            const RestirVertex& NV = State.Vertex[static_cast<size_t>(NY) * Width + NX];
            if (g_RestirDeadSkip && Neigh.SampleCount > 0u && Neigh.UnbiasedWeight <= 0.0f) { ++g_DeadSkipGi; continue; }   // #6
            const bool NValid = Neigh.SampleCount > 0u && NV.Valid
                && Neigh.StrideWidth == static_cast<float>(Width)
                && dot(Surface.Ng, Neigh.Normal) > kRestirNormalCos
                && fabsf(Surface.Depth - Neigh.Depth) / max(Surface.Depth, 1.0e-3f) < kRestirDepthTol;
            if (!NValid) continue;
            const uint32_t NeighCapped = std::min(Neigh.SampleCount, kRestirMClamp * CapReference);
            const float PNeigh = PHatSelected(m, L, VNg, VT, VB, VNs, VWo, Neigh.Light, Neigh.Point - VP);
            const float WSelf  = SelectedPHat * Res.UnbiasedWeight * static_cast<float>(Res.SampleCount);
            const float WNeigh = PNeigh * Neigh.UnbiasedWeight * static_cast<float>(NeighCapped);
            const float NTotal = WSelf + WNeigh;
            uint32_t TakeAge = Res.Age;
            if (NTotal > 0.0f && R.Next() * NTotal <= WNeigh)
            {
                Res.Point = Neigh.Point;
                Res.Light = Neigh.Light;
                TakeAge    = Neigh.Age + 1u;
                SelectedPHat = PNeigh;
            }
            Res.SampleCount += NeighCapped;
            Res.WeightSum    = NTotal;
            ClampGiReservoirM(Res);
            Res.Age = TakeAge;
            Res.UnbiasedWeight = SelectedPHat > 0.0f
                ? Res.WeightSum / (static_cast<float>(Res.SampleCount) * SelectedPHat) : 0.0f;
        }
    }

    // One shadow ray from the vertex to whichever light the pool selected — the same "visibility re-traced" rule
    //    the direct path uses, one pass later in the path.
    const bool Blocked = Res.Light == kRestirSunLight
        ? Occluded(VP + VNg * 1.0e-4f, VP + normalize(Res.Point - VP) * 1.0e4f)
        : Occluded(VP + VNg * 1.0e-4f, Res.Point - normalize(Res.Point - VP) * 1.0e-3f);
    Res.Visible = Blocked ? 0u : 1u;
    if (Res.Visible == 0u) Res.UnbiasedWeight = 0.0f;
    OutPublished = Res;

    vec3 Acc(0.0f);
    if (Res.Visible == 1u && Res.SampleCount > 0u)
    {
        const vec3 ShadeDir = normalize(Res.Point - VP);
        const float ShadeCos = max(0.0f, dot(VNg, ShadeDir));
        const vec3 Wi(dot(ShadeDir, VT), dot(ShadeDir, VB), dot(ShadeDir, VNs));
        const vec3 F = Wi.z > 0.0f ? EvaluateBsdf(m, L, VWo, Wi) : vec3(0.0f);
        if (Res.Light == kRestirSunLight)
            Acc = F * SunEmissionRender() * ShadeCos * Res.UnbiasedWeight;
        else
        {
            const EmissiveTriangle& Q = g_Lights[Res.Light];
            const float ShadeCosL = max(0.0f, dot(Q.Ng, -ShadeDir));
            const float Dist2 = dot(Res.Point - VP, Res.Point - VP);
            Acc = F * Q.Radiance * ShadeCos * ShadeCosL * Res.UnbiasedWeight / (Dist2 + 0.01f);
        }
    }
    // Vertex radiance → pixel radiance: the primary bounce's f·cos/pdf, which the caller's depth-0 sample produced.
    return Acc * V.Beta;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTEGRATOR
//------------------------------------------------------------------------------------------------------------------------

// NEE on the level's emissive triangles with power-heuristic MIS against the BSDF strategy (the exhibit's DirectMIS,
//    unchanged but for the light list being the level's own luminaires).
vec3 DirectMIS(const ShadingRecord& m, const ResolvedLayers& L, const vec3& P, const vec3& N,
               const vec3& T, const vec3& B, const vec3& wo, Rng& R)
{
    float Pl = 0.0f, Area = 0.0f;
    vec3 Radiance(0.0f);
    const int Li = PickLight(R, Pl, Area, Radiance);
    if (Li < 0) return vec3(0.0f);
    const EmissiveTriangle& Q = g_Lights[Li];
    float Su = R.Next(), Sv = R.Next();
    if (Su + Sv > 1.0f) { Su = 1.0f - Su; Sv = 1.0f - Sv; }
    const vec3 Lp = Q.P0 * (1.0f - Su - Sv) + Q.P1 * Su + Q.P2 * Sv;
    vec3 Dw = Lp - P;
    float D2 = dot(Dw, Dw);
    float Dist = sqrt(D2);
    Dw = Dw / Dist;
    float CosL = dot(-Dw, Q.Ng);
    if (CosL <= 1e-3f) return vec3(0.0f);   // grazing-epsilon: 1/CosL → ∞ along the quad silhouette
    vec3 wi(dot(Dw, T), dot(Dw, B), dot(Dw, N));
    if (wi.z <= 0.0f) return vec3(0.0f);
    if (Occluded(P + N * 1e-4f, Lp - Dw * 1e-3f)) return vec3(0.0f);
    vec3 F = EvaluateBsdf(m, L, wo, wi);
    // area → solid angle: pdf_Ω = pdf_A · d² / cosθ_l
    float PdfOmega = Pl * (D2 / (Area * CosL));
    float Pb = PdfBsdf(m, L, wo, wi);
    float W = (PdfOmega * PdfOmega) / (PdfOmega * PdfOmega + Pb * Pb + 1e-12f);
    return F * (wi.z * W / max(PdfOmega, 1e-12f)) * Radiance;
}

// The SSS below-horizon stratum (M5 v2): the light-sampled twin of the BSDF's dipole branch. No occlusion test — the
//    chord transport replaces visibility, so light behind the surface shines through, attenuated.
vec3 DirectMISsss(const ShadingRecord& m, const ResolvedLayers& L, const vec3& P, const vec3& N,
                  const vec3& T, const vec3& B, const vec3& wo, Rng& R)
{
    if (g_Lights.empty() || L.SssMix <= 0.0f) return vec3(0.0f);
    float Pl = 0.0f, Area = 0.0f;
    vec3 Radiance(0.0f);
    const int Li = PickLight(R, Pl, Area, Radiance);
    if (Li < 0) return vec3(0.0f);
    const EmissiveTriangle& Q = g_Lights[Li];
    float Su = R.Next(), Sv = R.Next();
    if (Su + Sv > 1.0f) { Su = 1.0f - Su; Sv = 1.0f - Sv; }
    const vec3 Lp = Q.P0 * (1.0f - Su - Sv) + Q.P1 * Su + Q.P2 * Sv;
    vec3 Dw = Lp - P;
    float D2 = dot(Dw, Dw);
    Dw = Dw / sqrt(D2);
    float CosL = dot(-Dw, Q.Ng);
    if (CosL <= 1e-3f) return vec3(0.0f);
    vec3 wi(dot(Dw, T), dot(Dw, B), dot(Dw, N));
    if (wi.z >= 0.0f) return vec3(0.0f);   // the above-stratum owns wi.z ≥ 0 (partition)
    vec3 F = EvaluateBsdf(m, L, wo, wi);
    float PdfOmega = Pl * (D2 / (Area * CosL));
    float Pb = PdfBsdf(m, L, wo, wi);
    float W = (PdfOmega * PdfOmega) / (PdfOmega * PdfOmega + Pb * Pb + 1e-12f);
    return F * ((-wi.z) * W / max(PdfOmega, 1e-12f)) * Radiance;
}

// The sun's disc as a light: a cone of half-angle g_SunAngularRadius around the sun direction, radiance read from the
//    sky core itself (disc + aureole + the veil in front of it), MIS-weighted against the BSDF strategy.
vec3 SunNee(const ShadingRecord& m, const ResolvedLayers& L, const vec3& P, const vec3& N,
            const vec3& T, const vec3& B, const vec3& wo, Rng& R)
{
    if (!g_SunNee) return vec3(0.0f);
    const float CosMax = cosf(g_SunAngularRadius);
    const float PdfSolid = 1.0f / (2.0f * kPi * (1.0f - CosMax));
    vec3 St, Sb;
    ShadingFrame(g_SunDirRender, St, Sb);
    const float CosTheta = 1.0f - R.Next() * (1.0f - CosMax);
    const float SinTheta = sqrtf(max(0.0f, 1.0f - CosTheta * CosTheta));
    const float Phi = 2.0f * kPi * R.Next();
    const vec3 DirWorld = normalize(St * (SinTheta * cosf(Phi)) + Sb * (SinTheta * sinf(Phi)) + g_SunDirRender * CosTheta);
    const float wiZ = dot(DirWorld, N);
    if (wiZ <= 0.0f) return vec3(0.0f);
    if (Occluded(P + N * 1e-4f, P + DirWorld * 1.0e4f)) return vec3(0.0f);
    const vec3 wi(dot(DirWorld, T), dot(DirWorld, B), wiZ);
    const vec3 F = EvaluateBsdf(m, L, wo, wi);
    const vec3 Sky = SkyRadianceWorld(DirWorld);            // the disc lives in the sky core, not beside it
    const float Pb = PdfBsdf(m, L, wo, wi);
    const float W = (PdfSolid * PdfSolid) / (PdfSolid * PdfSolid + Pb * Pb + 1e-12f);
    return F * (wi.z * W / PdfSolid) * Sky;
}

// The M5 geometric chord: an inward raycast from just below the surface, the exit the double-sided BVH finds. Miss ⇒
//    +∞ ⇒ Beer 0 (the ray crossed the whole volume).
float SssChord(const vec3& P, const vec3& N)
{
    Hit H = Intersect(P - N * 3e-4f, -N, 1e30f, -1);
    return H.Valid ? H.T : 1e30f;
}

vec3 Radiance(vec3 O, vec3 D, Rng& R, int Bounces, bool SkipPrimaryDirect = false,
              float InitialLastPdf = 0.0f, vec3 InitialBeta = vec3(1.0f))
{
    vec3 L(0.0f), Beta(InitialBeta);
    int Skip = -1;
    float LastPdf = InitialLastPdf;
    bool Inside = false;       // M4b: the ray is inside solid glass (single medium — nesting is v1-out)
    int EntryMat = -1;
    vec3 EntrySigma(0.0f);
    bool NeeSkipped = false;
    for (int Depth = 0; Depth < Bounces + 4; ++Depth)
    {
        const bool ReservoirOwnsDirect = SkipPrimaryDirect && Depth == 0;
        Hit H = Intersect(O, D, 1e30f, Skip);
        Skip = -1;
        const bool PrevNeeSkipped = NeeSkipped;
        NeeSkipped = false;
        if (!H.Valid)
        {
            if (Inside)   // open mesh / numeric leak: the nominal-Beer fallback, then out
            {
                Beta = Beta * exp(-EntrySigma * g_Mat[EntryMat].TransmissionThickness);
                Inside = false;
            }
            // Sky from a BSDF-sampled direction. Inside the sun cone the light strategy could have generated this
            //    direction, so the disc's share is MIS-weighted (the whole sky when the cone covers the ray — the disc
            //    is orders of magnitude brighter than the rest of the sky, so this is the term that matters).
            vec3 E = SkyRadianceWorld(D);
            if (g_SunNee && dot(D, g_SunDirRender) > cosf(g_SunAngularRadius))
            {
                const float PdfSolid = 1.0f / (2.0f * kPi * (1.0f - cosf(g_SunAngularRadius)));
                const float W = (LastPdf * LastPdf) / (LastPdf * LastPdf + PdfSolid * PdfSolid + 1e-12f);
                E = E * W;
            }
            L += Beta * E;
            break;
        }
        const RenderTriangle& T = g_Tris[H.TriId];
        vec3 P = O + D * H.T;
        if (Inside) Beta = Beta * exp(-EntrySigma * H.T);   // Beer over the interior segment just travelled
        const ShadingRecord& Mat = g_Mat[T.Material];

        if (T.Light >= 0)
        {
            // DIAGNOSTIC (--restir-bounce-mis): with the reservoir owning the primary pixel's direct integral, a
            //    first-bounce emitter hit is that same integral seen through the BSDF strategy a second time. The
            //    kernel adds it unweighted; this switch measures what excluding it does.
            if (g_RestirBounceMis && SkipPrimaryDirect && Depth == 1) break;
            const vec3 Ng = normalize(cross(T.P1 - T.P0, T.P2 - T.P0));
            if (dot(D, Ng) < 0.0f)
            {
                float W = 1.0f;
                if (Depth > 0 && !PrevNeeSkipped)
                {
                    const EmissiveTriangle& Q = g_Lights[T.Light];
                    const float CosL = dot(-D, Q.Ng);
                    const float PdfOmega = (Q.Power / TotalLightPower()) * (H.T * H.T / (Q.Area * max(CosL, 1e-6f)));
                    const float Pb = LastPdf;
                    W = (Pb * Pb) / (PdfOmega * PdfOmega + Pb * Pb + 1e-12f);
                }
                L += Beta * Mat.Emission * W;
            }
            break;
        }

        vec3 Ng = normalize(cross(T.P1 - T.P0, T.P2 - T.P0));
        if (dot(Ng, D) > 0.0f) Ng = -Ng;
        vec3 Ns = normalize(T.N0 * (1.0f - H.U - H.V) + T.N1 * H.U + T.N2 * H.V);
        if (dot(Ns, D) > 0.0f) Ns = -Ns;
        // M6 glints — the same perturbation the primary surface takes; a bounce that lands on a glinting
        //    material must see the same microsurface or the two paths shade two different materials.
        if (!g_MatGlintsOff && g_MatGlintDensity[T.Material] > 0.0f && Mat.AutomotiveData.x == 0.0f)
        {
            const float GlintCoverage = 1.0f - expf(-g_MatGlintDensity[T.Material] * 0.25f);
            const vec3 Gn = AutomotiveApplyTriCoatFlakeNormal(Ns, O + D * H.T, GlintCoverage,
                                                              g_MatGlintUvScale[T.Material] * 3.0f, 0.37f);
            if (dot(Gn, Ng) > 0.0f) Ns = Gn;
        }
        vec3 Tt, Bt;
        ShadingFrame(Ns, Tt, Bt);
        vec3 wo(dot(-D, Tt), dot(-D, Bt), dot(-D, Ns));

        ShadingRecord m = Mat;
        BindAutomotiveHit(m,T,H.U,H.V,H.T,D,Ns);
        if(g_MatGlintsOff)m.AutomotiveDensity=0; // local copy: the nested fallback may zero transmission
        const bool SolidHit = m.TransmissionWeight > 0.0f && (g_MatFlags[T.Material] & Frontier::MaterialFlagThinWalled) == 0u;
        const bool FromInside = Inside && T.Material == EntryMat;
        if (Inside && T.Material != EntryMat && m.TransmissionWeight > 0.0f)
        {
            m.TransmissionWeight = 0.0f;   // v1: no nested dielectrics — shade R-only, stay inside
        }
        if (!FromInside && m.SssWeight > 0.0f) m.SssThickness = SssChord(P, Ns);

        // Emissive-only and unlit short-circuits, exactly as the kernel orders them (radiance in, no BSDF).
        if (m.Selection == static_cast<uint>(kReflectanceEmissiveOnly))
        {
            L += Beta * m.Emission;
            break;
        }
        if (m.Selection == static_cast<uint>(kReflectanceUnlit))
        {
            L += Beta * m.BaseColor;
            break;
        }

        ResolvedLayers Lr = ResolveLayers(m, wo);
        if (SolidHit)
        {
            Lr.SolidInterface = true;
            Lr.IncidentIor = FromInside ? Lr.SpecularEta : 1.0f;
        }
        if (!FromInside)
        {
            if (ReservoirOwnsDirect)
                L += Beta * DirectMISsss(m, Lr, P, Ns, Tt, Bt, wo, R);   // the below-stratum stays outside the reservoir
            else
                L += Beta * (DirectMIS(m, Lr, P, Ns, Tt, Bt, wo, R) + DirectMISsss(m, Lr, P, Ns, Tt, Bt, wo, R)
                             + SunNee(m, Lr, P, Ns, Tt, Bt, wo, R));     // untouched: the reference/plain panels' estimator
        }
        else
            NeeSkipped = true;   // interior: an occlusion test would hit the exit wall — skip the light strata

        vec4 S = SampleBsdf(m, Lr, wo, vec4(R.Next(), R.Next(), R.Next(), R.Next()));
        if (S.w <= 0.0f) break;
        const vec3 wi = S.xyz;
        const vec3 F = EvaluateBsdf(m, Lr, wo, wi);
        const float CosS = wi.z < 0.0f ? -wi.z : wi.z;
        Beta *= F * (CosS / max(S.w, 1e-12f));
        Beta = min(Beta, vec3(8.0f, 8.0f, 8.0f));   // firefly clamp (softboxes through smooth glass)
        LastPdf = S.w;
        vec3 Dw = Tt * wi.x + Bt * wi.y + Ns * wi.z;
        if (wi.z < 0.0f && Lr.TransmitMix <= 0.0f && Lr.SssMix > 0.0f)
        {
            // M5 v2 dipole-virtual: a BSDF-sampled backlight direction, not a transport vertex. Skip non-emitters
            //    (bound 4, like the M4 shadow walk), then the environment; the light-sampled twin is DirectMISsss.
            vec3 Vo = P + Dw * 3e-4f;
            vec3 Vl(0.0f);
            bool Vhit = false;
            for (int Vs = 0; Vs < 4 && !Vhit; ++Vs)
            {
                Hit Vh = Intersect(Vo, Dw, 1e30f, -1);
                if (!Vh.Valid) break;
                const RenderTriangle& Vt = g_Tris[Vh.TriId];
                if (Vt.Light >= 0 && dot(Dw, normalize(cross(Vt.P1 - Vt.P0, Vt.P2 - Vt.P0))) < 0.0f)
                { Vl = g_Mat[Vt.Material].Emission; Vhit = true; }
                else Vo = Vo + Dw * (Vh.T + 3e-4f);
            }
            if (!Vhit) Vl = SkyRadianceWorld(Dw);
            L += Beta * Vl;
            break;
        }
        if (wi.z < 0.0f)
        {
            if (SolidHit)
            {
                if (!FromInside) { Inside = true; EntryMat = T.Material; EntrySigma = Lr.TransmissionSigma; }
                else { Inside = false; EntryMat = -1; }
            }
            else
                Skip = T.Material;   // thin-wall exit: continue past this wall's own interior
        }
        O = SolidHit ? P + Dw * 3e-4f : P + Ng * 1e-4f + Dw * 1e-4f;
        D = normalize(Dw);
    }
    return L;
}

float TotalLightPower()
{
    float Total = 0.0f;
    for (const EmissiveTriangle& L : g_Lights) Total += L.Power;
    return Total;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  LEVEL BUILD
//------------------------------------------------------------------------------------------------------------------------

// The level, straight from its authoring source: MaterialSwatchStructure::Construct builds the same 42 spheres, floor,
//    backdrop, signs and luminaires the exporter writes to Content/Scenes/Materials.gltf, and MaterialIndex turns the
//    same descriptors into the same MaterialRecord / MaterialSlabRecord rows the GPU build uploads (the export → decode
//    round trip that stands between the two is the M10 gate's A/E checks: 65/65, zero drift). Geometry arrives
//    world-space with three authored smooth normals per triangle, exactly what the kernel interpolates per hit.
// Compose the interface panel and register its light. Runs only for the showcase, AFTER the level's own triangles
//    are in g_Tris and BEFORE the luminaire gather + BVH build, so the proxy quad is picked up by both exactly the
//    way any authored emitter is. ObjectId is the span ordinal the proxy's triangles report as their instance.
bool BuildInterfacePanel(uint16_t ObjectId)
{
    using Frontier::ProjectZero::InterfaceTrialSequence;

    // ── The panel, composed by the engine's own pipeline ────────────────────────────────────────────────────────
    // Placement is the berth ShowcaseStructure authors: upright (local +Y onto world +Z), face along −Y toward the
    //    default viewpoint, at the published scale.
    Frontier::PlanePlacement Placement;
    Placement.Origin    = Frontier::PlaneOrigin{ Frontier::kShowcasePanelCentreX,
                                                 Frontier::kShowcasePanelCentreY,
                                                 Frontier::kShowcasePanelCentreZ };
    Placement.RotationX = 1.57079633f;
    Placement.Scale     = Frontier::kShowcasePanelScale;

    InterfaceTrialSequence Trial;
    Trial.AssignPanelPlacement(Placement);

    Frontier::InterfaceStructure Figures;
    Frontier::MotionIntegrator   Motion;
    Trial.Construct(Figures, Motion);

    // Drive the demonstration to t = 3.5 s (phase 0.58): the meter is pulled to full scale and has settled in the
    //    warning band, so the telltale burns, the toggle is engaged (green LED), the right button is backlit, and
    //    the bar sits near ⅚ fill — the moment that shows BOTH widget kinds doing their jobs.
    for (int Step = 0; Step < 210; ++Step)
        Trial.AdvanceTrial(Figures, Motion, 1.0 / 60.0, true);

    Frontier::InterfaceSequence Composition;
    Frontier::InterfaceViewConfiguration View;
    View.EyeX = 0.0f; View.EyeY = -9.5f; View.EyeZ = 5.6f;   // the default showcase viewpoint, for the sort
    View.ForwardX = 0.0f; View.ForwardY = 1.0f; View.ForwardZ = 0.0f;
    Composition.AssignView(View);
    Composition.Advance(Figures, 0.0);

    const Frontier::InterfaceInstanceFigure* Slots = Composition.QueryInstances();
    const uint32_t SlotCount = Composition.QueryInstanceCount();
    if (Slots == nullptr || SlotCount == 0u)
    {
        std::printf("[material-level] interface panel composed no figures — skipped\n");
        return true;
    }
    g_PanelFigures.assign(Slots, Slots + SlotCount);
    g_PanelActive = true;

    // The face frame, world space. Placement is baked into the half-axes (ComposeProxy's own contract). The proxy
    //    is INSET from the housing edge by the bezel: the emitting region of a display is its active area, and a
    //    proxy flush with the housing shows as a glowing rim wherever the rounded corners fall inside the quad.
    const float Inset = 0.020f * Frontier::kShowcasePanelScale;   // [m] world bezel inset (≥ the corner radius 0.016)
    const float HalfW = 0.180f * Frontier::kShowcasePanelScale - Inset;
    const float HalfH = 0.110f * Frontier::kShowcasePanelScale - Inset;
    g_PanelCentre = vec3(Placement.Origin.X, Placement.Origin.Y, Placement.Origin.Z);
    g_PanelNormal = vec3(0.0f, -1.0f, 0.0f);

    // ── The light: Low-tier proxy through the ENGINE's own path ─────────────────────────────────────────────────
    // MeasureRadiance applies the ⑧ split (Overlay figures contribute nothing); ComposeProxy registers the quad
    //    and its emissive material into a real SceneStructure whose Finalise flattens them — the two triangles
    //    appended to the harness scene below are the engine's own output, not a transcription of it.
    //    The panel area is in the figures' LOCAL metres, the same space the slots' half extents live in.
    const Frontier::PanelRadiance Radiance =
        Frontier::InterfaceLightProjection::MeasureRadiance(Figures, Composition, 4.0f * 0.180f * 0.110f);

    Frontier::PanelProxyRequest Request;
    Request.Tier    = Frontier::InterfaceFidelityTier::Low;
    Request.CentreX = g_PanelCentre.x; Request.CentreY = g_PanelCentre.y; Request.CentreZ = g_PanelCentre.z;
    Request.RightX  = HalfW; Request.RightY = 0.0f; Request.RightZ = 0.0f;
    Request.UpX     = 0.0f;  Request.UpY    = 0.0f; Request.UpZ    = HalfH;
    Request.Gain    = g_PanelGain;

    Frontier::SceneStructure Proxy;
    const uint32_t Instance = Frontier::InterfaceLightProjection::ComposeProxy(Proxy, Request, Radiance);
    if (Instance == 0xFFFFFFFFu)
    {
        std::printf("[material-level] interface panel: overlay only — the illuminant set emits nothing "
                    "(%u contributors)\n", Radiance.Contributors);
        return true;
    }
    Proxy.Finalise(1u, nullptr);

    const std::vector<Frontier::TriangleIndex>& Flats = Proxy.QueryFlatTriangles();
    const std::vector<MaterialRecord>&          Records = Proxy.QueryMaterials().QueryRecords();
    const std::vector<MaterialSlabRecord>&      Slabs   = Proxy.QueryMaterials().QuerySlabRecords();
    if (Flats.empty() || Records.empty() || Slabs.empty())
    {
        std::printf("[material-level] interface panel: the proxy scene flattened to nothing — skipped\n");
        return true;
    }

    const size_t MaterialBase = g_Mat.size();
    for (const MaterialRecord& R : Records)
    {
        const uint32_t Selection = (R.Flags & Frontier::kMaterialReflectanceMask) >> Frontier::kMaterialReflectanceShift;
        const MaterialSlabRecord& S = Slabs[std::min(static_cast<size_t>(R.SlabOffset), Slabs.size() - 1u)];
        g_Mat.push_back(TranscribeShadingRecord(S, Selection));
        g_MatFlags.push_back(R.Flags);
        g_MatCutoff.push_back(R.AlphaCutoff);
        g_MatCutAway.push_back(false);
        g_MatGlintDensity.push_back(S.SlateGlintDensity);   // M6: the kernel's P14.xy pair
        g_MatGlintUvScale.push_back(S.SlateGlintUvScale);
    }

    for (const Frontier::TriangleIndex& F : Flats)
    {
        RenderTriangle R;
        R.P0 = vec3(F.VertexAlphaX, F.VertexAlphaY, F.VertexAlphaZ);
        R.P1 = vec3(F.VertexBetaX,  F.VertexBetaY,  F.VertexBetaZ);
        R.P2 = vec3(F.VertexGammaX, F.VertexGammaY, F.VertexGammaZ);
        const vec3 Ng = normalize(cross(R.P1 - R.P0, R.P2 - R.P0));
        R.N0 = Ng; R.N1 = Ng; R.N2 = Ng;
        R.U0 = F.TextureAlphaU; R.V0 = F.TextureAlphaV;
        R.U1 = F.TextureBetaU;  R.V1 = F.TextureBetaV;
        R.U2 = F.TextureGammaU; R.V2 = F.TextureGammaV;
        uint32_t Slot = 0u;
        std::memcpy(&Slot, &F.MaterialSlot, sizeof(Slot));
        R.Material = static_cast<int>(MaterialBase + Slot);
        R.Object   = static_cast<int>(ObjectId);
        R.Light    = -1;   // the luminaire gather below this call assigns it, same as every authored emitter
        g_Tris.push_back(R);
    }

    std::printf("[material-level] interface panel: %u figures composed (%u illuminant contributors), light rgb "
                "(%.3f %.3f %.3f), %.0f%% coverage, gain %.0f — proxy quad and luminaire registered by the engine\n",
                SlotCount, Radiance.Contributors, Radiance.Red, Radiance.Green, Radiance.Blue,
                static_cast<double>(Radiance.Coverage()) * 100.0, static_cast<double>(g_PanelGain));
    return true;
}

bool BuildLevel()
{
    // Both levels are authored the same way — a Structure whose Construct() emits a world-space soup, three smooth
    //    normals per triangle, OpenPBR MaterialDescriptors and named spans — so the only thing that varies is which
    //    Construct runs. The showcase is the product's DEFAULT level (no --scene argument); the swatch library is the
    //    M10 set this harness shipped with. Everything below this point is level-agnostic.
    MaterialSwatchStructure Library;
    ShowcaseStructure       Showcase;
    const bool IsShowcase = (g_Level == "showcase");
    if (IsShowcase) Showcase.Construct(); else Library.Construct();

    const std::vector<Frontier::TriangleIndex>&      Tris     = IsShowcase ? Showcase.QueryTriangles()     : Library.QueryTriangles();
    const std::vector<Frontier::Vector3>&            Corners  = IsShowcase ? Showcase.QueryCornerNormals() : Library.QueryCornerNormals();
    const std::vector<Frontier::MaterialDescriptor>& Authored = IsShowcase ? Showcase.QueryMaterials()     : Library.QueryMaterials();
    if (Tris.empty() || Corners.size() != Tris.size() * 3u)
    {
        std::printf("[material-level] the level's triangle soup is malformed (%zu tris, %zu corner normals)\n",
                    Tris.size(), Corners.size());
        return false;
    }

    MaterialIndex Index;
    for (const Frontier::MaterialDescriptor& D : Authored) Index.Register(D);
    Index.Finalise(1u, nullptr);   // Tier A: the flattened resident level the GPU build seats

    const std::vector<MaterialRecord>& Records = Index.QueryRecords();
    const std::vector<MaterialSlabRecord>& Slabs = Index.QuerySlabRecords();
    g_Mat.resize(Records.size());
    g_MatGlintDensity.resize(Records.size());g_MatGlintUvScale.resize(Records.size());
    g_MatFlags.resize(Records.size(), 0u);
    g_MatCutoff.resize(Records.size(), 0.5f);
    for (size_t I = 0; I < Records.size(); ++I)
    {
        const uint32_t Selection = (Records[I].Flags & Frontier::kMaterialReflectanceMask) >> Frontier::kMaterialReflectanceShift;
        const MaterialSlabRecord& S = Slabs[std::min(static_cast<size_t>(Records[I].SlabOffset), Slabs.size() - 1u)];
        g_Mat[I] = TranscribeShadingRecord(S, Selection);
        g_MatGlintDensity[I]=S.SlateGlintDensity;g_MatGlintUvScale[I]=S.SlateGlintUvScale;
        g_MatFlags[I] = Records[I].Flags;
        g_MatCutoff[I] = Records[I].AlphaCutoff;
    }
    // Alpha test, resolved once per material. The level is constants-only, so the kernel's per-hit `F.Opacity <
    //    AlphaCutoff` discard is a property of the material here — a material that discards every texel is a material
    //    no ray can hit. (With the panel's authored opacity 0.62 against a 0.5 cutoff the panel survives, exactly as it
    //    does in the kernel; a level authored below its cutoff renders as the hole it is.)
    g_MatCutAway.assign(Records.size(), false);
    for (size_t I = 0; I < Records.size(); ++I)
    {
        const MaterialSlabRecord& S = Slabs[std::min(static_cast<size_t>(Records[I].SlabOffset), Slabs.size() - 1u)];
        g_MatCutAway[I] = (Records[I].Flags & MaterialFlagAlphaMask) != 0u && S.GeometryOpacity < Records[I].AlphaCutoff;
    }

    // Soup index → object (span), so every triangle knows which of the level's objects it belongs to. Spans are the
    //    builder's own record of one object's triangle range and are what the GPU uploads as instances.
    const std::vector<Frontier::TriangleSpanRecord>& Spans = IsShowcase ? Showcase.QuerySpans() : Library.QuerySpans();
    // ⚠️ uint8_t indexes the span list, and the showcase has 115 spans — fine today, but one more row of objects
    //    would wrap silently and put triangles on the wrong object. Widened to uint16_t rather than left to rot.
    std::vector<uint16_t> ObjectOfSoup(Tris.size(), 0u);
    for (size_t SpanIndex = 0; SpanIndex < Spans.size(); ++SpanIndex)
    {
        const Frontier::TriangleSpanRecord& Span = Spans[SpanIndex];
        const size_t End = std::min(static_cast<size_t>(Span.FirstTriangle) + Span.TriangleCount, Tris.size());
        for (size_t T = Span.FirstTriangle; T < End; ++T) ObjectOfSoup[T] = static_cast<uint16_t>(SpanIndex);
    }

    g_Tris.reserve(Tris.size());
    for (size_t I = 0; I < Tris.size(); ++I)
    {
        const Frontier::TriangleIndex& T = Tris[I];
        RenderTriangle R;
        R.P0 = vec3(T.VertexAlphaX, T.VertexAlphaY, T.VertexAlphaZ);
        R.P1 = vec3(T.VertexBetaX,  T.VertexBetaY,  T.VertexBetaZ);
        R.P2 = vec3(T.VertexGammaX, T.VertexGammaY, T.VertexGammaZ);
        R.N0 = vec3(Corners[I * 3u + 0u].x, Corners[I * 3u + 0u].y, Corners[I * 3u + 0u].z);
        R.N1 = vec3(Corners[I * 3u + 1u].x, Corners[I * 3u + 1u].y, Corners[I * 3u + 1u].z);
        R.N2 = vec3(Corners[I * 3u + 2u].x, Corners[I * 3u + 2u].y, Corners[I * 3u + 2u].z);
        R.U0 = T.TextureAlphaU; R.V0 = T.TextureAlphaV;
        R.U1 = T.TextureBetaU;  R.V1 = T.TextureBetaV;
        R.U2 = T.TextureGammaU; R.V2 = T.TextureGammaV;
        // ⚠️ MaterialSlot is a uint32 whose BITS live in a float (MaterialSwatchStructure::AppendTriangle memcpys it in,
        //    the GPU reads it back the same way). Casting the float's VALUE would read a denormal and land every
        //    triangle on material 0 — bit-cast, exactly as the facet's own comment says.
        uint32_t Slot = 0u;
        std::memcpy(&Slot, &T.MaterialSlot, sizeof(Slot));
        R.Material = static_cast<int>(Slot);
        R.Object   = static_cast<int>(ObjectOfSoup[I]);
        R.Light = -1;
        if (R.Material < 0 || R.Material >= static_cast<int>(g_Mat.size())) return false;
        if (g_RowFilter >= 0 && !IsShowcase)
        {
            // A lookdev crop: keep one sphere row and the studio around it, drop the other five. The material slots are
            //    laid out as floor, backdrop, swatch 0…41, panels, luminaires (MaterialSwatchStructure's constants), so
            //    "row N" is a range of slots — no geometry needs rebuilding, and the surviving pixels are the level's.
            const int First = static_cast<int>(MaterialSwatchStructure::kFirstSwatchMaterial);
            const uint32_t Row = static_cast<uint32_t>(g_RowFilter);
            const bool IsSwatch = R.Material >= First && R.Material < First + static_cast<int>(MaterialSwatchStructure::kSwatchCount);
            if (IsSwatch)
            {
                const uint32_t Ordinal = static_cast<uint32_t>(R.Material - First);
                if (Ordinal / MaterialSwatchStructure::kSwatchColumns != Row) continue;
            }
        }
        g_Tris.push_back(R);
    }

    // The interface panel (showcase only): composes the figures, and appends the engine-built proxy quad + emissive
    //    material to the soup — BEFORE the luminaire gather, so the panel's light enters the table like any other.
    if (IsShowcase && !BuildInterfacePanel(static_cast<uint16_t>(Spans.size()))) return false;

    // Luminaires: every emissive triangle (the level's key, fill and emissive panel — the same set Finalise gathers).
    for (size_t I = 0; I < g_Tris.size(); ++I)
    {
        RenderTriangle& T = g_Tris[I];
        if (g_MatCutAway[T.Material]) continue;
        const vec3 E = g_Mat[T.Material].Emission;
        const float Lum = 0.2126f * E.x + 0.7152f * E.y + 0.0722f * E.z;
        if (Lum <= 0.0f) continue;
        EmissiveTriangle L;
        L.P0 = T.P0; L.P1 = T.P1; L.P2 = T.P2;
        L.Ng = normalize(cross(T.P1 - T.P0, T.P2 - T.P0));
        L.Radiance = E;
        L.Area = 0.5f * length(cross(T.P1 - T.P0, T.P2 - T.P0));
        L.Power = L.Area * Lum;
        T.Light = static_cast<int>(g_Lights.size());
        g_Lights.push_back(L);
    }

    g_Order.resize(g_Tris.size());
    for (size_t I = 0; I < g_Tris.size(); ++I) g_Order[I] = static_cast<int>(I);
    g_Nodes.clear();
    g_Nodes.emplace_back();
    BuildBvh(0, 0, static_cast<int>(g_Order.size()));
    if (g_RowFilter >= 0)
        std::printf("[material-level] row crop: only swatch row %d is in the scene (the studio stays; other rows dropped)\n",
                    g_RowFilter);
    std::printf("[material-level] level: %zu triangles, %zu material records (%zu slabs), %zu emissive triangles\n",
                g_Tris.size(), Records.size(), Slabs.size(), g_Lights.size());
    return true;
}


//------------------------------------------------------------------------------------------------------------------------
//                                    SEQUENCE RENDER — frames, the running mean, and the filter
//------------------------------------------------------------------------------------------------------------------------
// The app renders one frame per dispatch and accumulates in ResolveSurface; a sheet needs the same thing off-GPU, so
//    this is that loop: N frames, a camera that may drift (to put the reprojection to work), the kernel's own
//    accumulator per pixel, and — when asked — the shipped à-trous chain over the result, level by level, exactly as
//    SwapchainExchange dispatches it.

struct SequenceResult
{
    std::vector<float> Mean;        // [W*H*3] the running mean, linear radiance (what ResolveSurface publishes)
    std::vector<float> Surface;     // [W*H*4] normal xyz + depth in w (0 = sky): the filter's geometry buffer
    std::vector<float> Variance;    // [W*H]   the variance of the mean the kernel's recursion reports
    long               NonFinite = 0;
    double             ReservoirCoverage = 0.0;   // [%]  pixels whose reservoir survived the frame
    double             MeanReservoirM = 0.0;      // [-]  mean M over the surface pixels (reuse is visible here)
    double             MeanReservoirMPublished = 0.0;   // [-] mean M of the reservoir the SHADING used (post-spatial)
    double             MeanGiM = 0.0;                   // [-] the indirect pool, same two numbers
    double             MeanGiMPublished = 0.0;
    double             GiOccludedPercent = 0.0;
    double             IdentityRefused = 0.0;   // D10: [reads] geometry agreed, the surface differed — the ghost case
    double             Disocclusion = 0.0;      // [px] every restart of the running mean
    double             ReservoirIdentityRefused = 0.0;   // [merges] the same case in the two reservoir pools
    double             IdentityRefusedCrossMaterial = 0.0;   // [reads] of those, those showing a different MATERIAL
    std::vector<unsigned char> GiClass;   // roadmap #5: the last frame's per-pixel GI class (0 = none, see kGiClass*)
};

struct FrameTally
{
    long     Bad = 0;
    // D10: reprojections refused because the history belonged to a DIFFERENT surface (the A/B's own tell — with
    //    --restir-no-identity the rule is not consulted, so this counts what it would have refused).
    double   IdentityRefused = 0.0;
    double   IdentityRefusedCrossMaterial = 0.0;
    double   Surface = 0.0, SamplesBehind = 0.0;
    double   Reprojected = 0.0, Moved = 0.0, Disocclusion = 0.0;
    double   Covered = 0.0, MSum = 0.0, Occluded = 0.0, MSumPublished = 0.0;
    double   CoveredGi = 0.0, MSumGi = 0.0, OccludedGi = 0.0;
    double   GiBad = 0.0, GiEscape = 0.0, GiEmitter = 0.0, GiUnlit = 0.0, GiUnusable = 0.0, GiVertex = 0.0, GiNoPHat = 0.0;
    uint32_t MaxM = 0u;
};

// The kernel's frame, in the kernel's order. Pass 1 publishes the temporal reservoir; pass 2 — a dispatch boundary
//    on the GPU, a barrier in the mirror — reads it for spatial reuse and shades. One sample per pixel per frame
//    (the kernel's budget), so the sheet's "4 candidates x 16 frames" means what it says: sixteen reservoir
//    estimates, each carrying Standard tier's four candidates plus whatever reuse moved in.
SequenceResult RenderSequence(const Viewpoint& VP, int Width, int Height, int Spp, int Bounces, int Frames,
                              float PanPerFrame, bool UseRestir, unsigned Threads, bool Verbose)
{
    SequenceResult Out;
    Out.Mean.assign(static_cast<size_t>(Width) * Height * 3u, 0.0f);
    Out.Surface.assign(static_cast<size_t>(Width) * Height * 4u, 0.0f);
    Out.Variance.assign(static_cast<size_t>(Width) * Height, 0.0f);

    // The running mean. The kernel keeps it in HistoryImage/HistorySurfaceImage and REPROJECTS it through the R2
    //    motion vectors (ResolveSurface, R7a), so the CPU mirror does too. Double-buffered — a GPU reads and writes
    //    one image in a single dispatch, which is a read-write hazard (a neighbour's write can land before a
    //    reprojected read); the mirror reads a stable previous frame.
    const size_t PixelCount = static_cast<size_t>(Width) * Height;
    std::vector<DenoiseMirror::Accumulator> FilmPrevious(PixelCount), FilmCurrent(PixelCount);
    std::vector<float> FilmSurfacePrevious(PixelCount * 4u, 0.0f), FilmSurfaceCurrent(PixelCount * 4u, 0.0f);
    // D10: WHICH surface each pixel's mean was shaded from, alongside the (normal, depth) pair R7a already keeps there.
    //    The GPU packs this into the moment image's reserved z/w; the mirror keeps it in its own array, and the rule it
    //    feeds is the same one: a different surface is a disocclusion whatever its normal and depth say.
    std::vector<uint32_t> FilmIdentityPrevious(PixelCount, 0u), FilmIdentityCurrent(PixelCount, 0u);
    // The material index as well, purely so the refusal can be split into its two very different cases: a DIFFERENT
    //    MATERIAL agreeing on normal and depth (the true ghost — two different surfaces whose shading has nothing in
    //    common) versus the same material's surface under a jittered sample (harmless, and it must not be counted as if
    //    it were the pathology). Without the split the number is unreadable.
    std::vector<int> FilmMaterialPrevious(PixelCount, -1), FilmMaterialCurrent(PixelCount, -1);

    RestirFrameState State;
    State.Temporal.resize(PixelCount);
    State.History.resize(PixelCount);
    State.GiTemporal.resize(PixelCount);
    State.GiHistory.resize(PixelCount);
    State.Vertex.resize(PixelCount);
    State.VertexHistory.resize(PixelCount);
    State.Surface.resize(PixelCount);
    State.GiClass.assign(PixelCount, kGiClassNone);

    g_IdentityRefusedDirect.store(0);
    g_IdentityRefusedGi.store(0);

    Frontier::CameraProjection Base;
    Base.AssignSpatialLocation(VP.Position);
    Base.AssignOrientationEuler(VP.PitchDegrees * kPi / 180.0f, VP.YawDegrees * kPi / 180.0f, 0.0f);
    Base.AssignFieldOfView(VP.FieldOfView);
    g_AutomotiveRaySpread=2.0f*std::tan(VP.FieldOfView*kPi/360.0f)/static_cast<float>(Height);
    Base.AssignAspectRatio(static_cast<float>(Width) / static_cast<float>(Height));

    const bool SunUp = dot(g_SunDirRender, g_SunDirRender) > 0.0f && SkyRadianceWorld(g_SunDirRender).y > 0.0f;

    // D10: the moving object. The driven triangles' rest poses are captured once; each frame they are placed at
    //    rest + Offset(F) and the BVH is rebuilt, so both the primary rays AND every shadow ray see the object where it
    //    actually is this frame (a moving object whose shadows lagged its geometry would be the other half of this
    //    milestone's acceptance). The excursion is triangular and returns home on the last frame.
    const DriftCapture Drift = (g_RestirDrift != 0.0f) ? CaptureDrift(g_RestirDriftMaterial) : DriftCapture();
    const std::vector<int>& DriftTriangles = Drift.Triangles;
    const vec3 DriftAxis = Drift.Axis;
    const int DriftPeak = (Frames - 1) / 2;
    if (!Drift.Empty())
        std::printf("[restir] D10 moving object: material %d, %zu triangles slide along (%.0f,%.0f,%.0f) by "
                    "%.3f m/frame, out and back (%d frames, peak %.3f m)\n",
                    g_RestirDriftMaterial, DriftTriangles.size(), DriftAxis.x, DriftAxis.y, DriftAxis.z,
                    g_RestirDrift, Frames, g_RestirDrift * static_cast<float>(DriftPeak));

    for (int F = 0; F < Frames; ++F)
    {
        // The offset this frame, and the one the previous frame used — their difference is what the motion vector has to
        //    carry: the surface point a pixel shows moved in WORLD space, not only on screen.
        const int Excursion = (F <= DriftPeak) ? F : (Frames - 1 - F);
        const int PreviousExcursion = (F - 1 <= DriftPeak) ? (F - 1) : (Frames - F);   // E(F-1), the same triangle
        const vec3 DriftOffset = DriftAxis * (g_RestirDrift * static_cast<float>(Excursion));
        const float DriftDelta = g_RestirDrift * static_cast<float>(Excursion - (F > 0 ? PreviousExcursion : Excursion));
        ApplyDrift(Drift, DriftOffset);
        const vec3 DriftStep = DriftAxis * DriftDelta;

        Frontier::CameraProjection Camera = Base;
        if (PanPerFrame != 0.0f)
        {
            const int Peak = (Frames - 1) / 2;                       // 0 … Peak … 0: the excursion returns home
            const int Drift = F <= Peak ? F : (Frames - 1 - F);
            const Frontier::Vector3 R = Base.QueryRightVector();
            Camera.AssignSpatialLocation(Frontier::Vector3{ VP.Position.x + R.x * PanPerFrame * static_cast<float>(Drift),
                                                            VP.Position.y + R.y * PanPerFrame * static_cast<float>(Drift),
                                                            VP.Position.z + R.z * PanPerFrame * static_cast<float>(Drift) });
        }
        if (UseRestir)
        {
            State.FrameIndex  = F;
            State.HasPrevious = F > 0;
            if (F > 0) State.PreviousPose = State.LastPose;
            State.LastPose = CapturePose(Camera);
        }

        // The kernel's anti-aliasing rule: ONE sub-pixel offset per frame, shared by the raster and the resolve.
        const float JitterU = UseRestir ? Halton(F + 1, 2u) : 0.0f;
        const float JitterV = UseRestir ? Halton(F + 1, 3u) : 0.0f;

        std::mutex TallyMutex;
        std::vector<FrameTally> Tally(4);

        auto RunRows = [&](int Phase, auto&& PerPixel)
        {
            std::vector<std::thread> Pool;
            for (unsigned Th = 0u; Th < Threads; ++Th)
                Pool.emplace_back([&, Th]()
                {
                    FrameTally Local;
                    for (int Y = static_cast<int>(Th); Y < Height; Y += static_cast<int>(Threads))
                        for (int X = 0; X < Width; ++X)
                            PerPixel(X, Y, Local);
                    std::lock_guard<std::mutex> Guard(TallyMutex);
                    FrameTally& T = Tally[static_cast<size_t>(Phase)];
                    T.Bad += Local.Bad; T.Surface += Local.Surface; T.SamplesBehind += Local.SamplesBehind;
                    T.Reprojected += Local.Reprojected; T.Moved += Local.Moved; T.Disocclusion += Local.Disocclusion;
                    T.Covered += Local.Covered; T.MSum += Local.MSum; T.Occluded += Local.Occluded;
                    T.IdentityRefused += Local.IdentityRefused;
                    T.IdentityRefusedCrossMaterial += Local.IdentityRefusedCrossMaterial;
                    T.MSumPublished += Local.MSumPublished;
                    T.CoveredGi += Local.CoveredGi; T.MSumGi += Local.MSumGi; T.OccludedGi += Local.OccludedGi;
                    if (Local.MaxM > T.MaxM) T.MaxM = Local.MaxM;
                });
            for (std::thread& T : Pool) T.join();
        };

        // ── The G-buffer and the film's history read (R7a), for both paths ───────────────────────────────────────
        RunRows(0, [&](int X, int Y, FrameTally& T)
        {
            const size_t Pixel = static_cast<size_t>(Y) * Width + X;
            const float CurU = (static_cast<float>(X) + 0.5f + JitterU) / static_cast<float>(Width);
            const float CurV = (static_cast<float>(Y) + 0.5f + JitterV) / static_cast<float>(Height);

            RestirSurface& Surf = State.Surface[Pixel];
            Surf = RestirSurface();
            {
                const Frontier::ViewRay Ray = Camera.ConstructRay(CurU, CurV);
                const vec3 O(Ray.OriginLocation.x, Ray.OriginLocation.y, Ray.OriginLocation.z);
                const vec3 D = normalize(vec3(Ray.UnitDirection.x, Ray.UnitDirection.y, Ray.UnitDirection.z));
                Surf.O = O;
                Surf.D = D;
                Surf.RayU = CurU;
                Surf.RayV = CurV;
                const Hit H = Intersect(O, D, 1.0e30f, -1);
                if (H.Valid)
                {
                    const RenderTriangle& Tri = g_Tris[H.TriId];
                    const bool Emitter = Tri.Light >= 0;
                    // D10: the identity is the OBJECT (the span), +1 so 0 can stay "no surface". The triangle ordinal
                    //    was the first cut and the still-scene measurement rejected it: with a per-frame sub-pixel
                    //    jitter, the same object's 1500-triangle sphere reports a different triangle almost every frame,
                    //    and the rule restarted 13.5 % of a static image for nothing.
                    Surf.Identity   = static_cast<uint32_t>(Tri.Object) + 1u;
                    Surf.MaterialIdx = Tri.Material;
                    Surf.Depth = H.T;
                    Surf.P     = O + D * H.T;
                    Surf.Ng    = normalize(cross(Tri.P1 - Tri.P0, Tri.P2 - Tri.P0));
                    if (dot(Surf.Ng, D) > 0.0f) Surf.Ng = -Surf.Ng;
                    Surf.Ns = normalize(Tri.N0 * (1.0f - H.U - H.V) + Tri.N1 * H.U + Tri.N2 * H.V);
                    if (dot(Surf.Ns, D) > 0.0f) Surf.Ns = -Surf.Ns;
                    // M6 glints — the kernel's rule, line for line: coverage 1 - exp(-D/4), frequency x3,
                    //    jitter 0.37, applied before the frame builds, below-surface rejected against Ng.
                    if (!g_MatGlintsOff && g_MatGlintDensity[Tri.Material] > 0.0f && g_Mat[Tri.Material].AutomotiveData.x == 0.0f)
                    {
                        const float GlintCoverage = 1.0f - expf(-g_MatGlintDensity[Tri.Material] * 0.25f);
                        const vec3 Gn = AutomotiveApplyTriCoatFlakeNormal(Surf.Ns, Surf.P, GlintCoverage,
                                                                          g_MatGlintUvScale[Tri.Material] * 3.0f, 0.37f);
                        if (dot(Gn, Surf.Ng) > 0.0f) Surf.Ns = Gn;
                    }
                    ShadingFrame(Surf.Ns, Surf.T, Surf.B);
                    Surf.Wo = vec3(dot(-D, Surf.T), dot(-D, Surf.B), dot(-D, Surf.Ns));
                    Surf.Mat = g_Mat[Tri.Material];
                    BindAutomotiveHit(Surf.Mat,Tri,H.U,H.V,H.T,D,Surf.Ns);
                    if(g_MatGlintsOff)Surf.Mat.AutomotiveDensity=0;
                    Surf.Layers = ResolveLayers(Surf.Mat, Surf.Wo);
                    const bool SolidHit = Surf.Mat.TransmissionWeight > 0.0f
                        && (g_MatFlags[Tri.Material] & Frontier::MaterialFlagThinWalled) == 0u;
                    if (SolidHit) { Surf.Layers.SolidInterface = true; Surf.Layers.IncidentIor = 1.0f; }
                    // The reservoir build, exactly where the kernel skips it: sky, a directly hit emitter, an
                    //    emissive-only or unlit surface. The G-buffer stays valid (the film and the filter read it).
                    Surf.Valid = !Emitter
                        && Surf.Mat.Selection != static_cast<uint>(kReflectanceEmissiveOnly)
                        && Surf.Mat.Selection != static_cast<uint>(kReflectanceUnlit);
                    if (State.HasPrevious)
                    {
                        float PrevU = 0.0f, PrevV = 0.0f;
                        // D10: a driven surface point was where the object left it last frame, not where the object is
                        //    now — the camera pose is only half of a motion vector once geometry moves (the GPU reads
                        //    the other half from InstanceRecord::PreviousWorld).
                        const vec3 PrevPoint = (DriftTriangles.empty() || Tri.Material != g_RestirDriftMaterial)
                            ? Surf.P : (Surf.P - DriftStep);
                        if (ProjectPoint(State.PreviousPose, PrevPoint, PrevU, PrevV))
                        {
                            Surf.MotionU = Surf.RayU - PrevU;
                            Surf.MotionV = Surf.RayV - PrevV;
                        }
                    }
                }
            }

            // ResolveSurface's history read: reproject through the motion vectors under the SAME 25 deg / 10 % rule
            //    the reservoirs use; a failure is disocclusion (the mean restarts rather than smearing). With the
            //    feature off, or with no surface (no motion), the read is the pixel's own address: the pre-R7a rule.
            DenoiseMirror::Accumulator& Accumulator = FilmCurrent[Pixel];
            Accumulator = DenoiseMirror::Accumulator();
            if (F > 0)
            {
                bool Resolved = false;
                if (Surf.Depth > 0.0f && !g_RestirNoReproject)
                {
                    // The address is the PIXEL's own position minus the surface's displacement — the kernel's
                    //    `cuv - motion` (ResolveSurface), where cuv is the pixel centre and the motion image carries the
                    //    surface point's screen motion.
                    const float CentreU = (static_cast<float>(X) + 0.5f) / static_cast<float>(Width);
                    const float CentreV = (static_cast<float>(Y) + 0.5f) / static_cast<float>(Height);
                    const float PU = CentreU - Surf.MotionU, PV = CentreV - Surf.MotionV;
                    const int PrevX = static_cast<int>(floorf(PU * static_cast<float>(Width)));
                    const int PrevY = static_cast<int>(floorf(PV * static_cast<float>(Height)));
                    if (PrevX >= 0 && PrevY >= 0 && PrevX < Width && PrevY < Height)
                    {
                        const size_t PrevPixel = static_cast<size_t>(PrevY) * Width + PrevX;
                        const float* PrevSurf = &FilmSurfacePrevious[PrevPixel * 4u];
                        // The two tests are kept apart ON PURPOSE: the measurement this milestone is judged by is how
                        //    many reads agree on normal and depth (so R7a would have merged them) and still belong to a
                        //    DIFFERENT surface. That is the ghost, and the count is the same number in both arms of the
                        //    A/B — one arm refuses them, the other inherits them.
                        const bool GeometryOk = PrevSurf[3] > 0.0f
                            && dot(Surf.Ng, vec3(PrevSurf[0], PrevSurf[1], PrevSurf[2])) > kRestirNormalCos
                            && fabsf(Surf.Depth - PrevSurf[3]) / max(Surf.Depth, 1.0e-3f) < kRestirDepthTol;
                        const bool IdentityDiffers = FilmIdentityPrevious[PrevPixel] != Surf.Identity;
                        const bool IdentityOk = !g_RestirIdentity || !IdentityDiffers;
                        if (GeometryOk && IdentityOk)
                        {
                            Accumulator = FilmPrevious[PrevPixel];
                            // SVGF-style history bound (kernel parity — ReSTIRViewport.slang kMovingHistoryBound):
                            //    a pixel that MOVED clamps its inherited count to 32 so the update weight never
                            //    falls below α ≈ 1/32; a still pixel keeps the unbounded mean and every held-frame
                            //    proof its convergence rests on.
                            if (PrevPixel != Pixel && Accumulator.Count > 32.0f) Accumulator.Count = 32.0f;
                            T.Reprojected += 1.0;
                            if (PrevPixel != Pixel) T.Moved += 1.0;
                            Resolved = true;
                        }
                        else
                        {
                            T.Disocclusion += 1.0;
                            // D10: the ghost-prevented count. Counted in BOTH arms, so the A/B reports the same geometry
                            //    and the only difference between the arms is whether those reads were honoured.
                            if (GeometryOk && IdentityDiffers)
                            {
                                T.IdentityRefused += 1.0;
                                // A different MATERIAL is not required to ghost — two different objects can share a
                                //    material — but when the materials differ the read is provably worthless, so it is
                                //    counted: it is the lower bound on how many of these merges were true ghosts.
                                if (FilmMaterialPrevious[PrevPixel] != Surf.MaterialIdx) T.IdentityRefusedCrossMaterial += 1.0;
                            }
                            Resolved = true;
                        }
                    }
                    else { T.Disocclusion += 1.0; Resolved = true; }
                }
                if (!Resolved) Accumulator = FilmPrevious[Pixel];
            }
            float* FilmSurf = &FilmSurfaceCurrent[Pixel * 4u];
            FilmSurf[0] = Surf.Ng.x; FilmSurf[1] = Surf.Ng.y; FilmSurf[2] = Surf.Ng.z; FilmSurf[3] = Surf.Depth;
            // D10: stored whether the feature is on or off — a history written while it was off must not restart the
            //    whole image on the first frame it is switched on.
            FilmIdentityCurrent[Pixel] = Surf.Identity;
            FilmMaterialCurrent[Pixel] = Surf.MaterialIdx;
        });

        // ── PASS 1 — candidates, RIS, W, temporal reuse: the reservoir this frame publishes ──────────────────────
        if (UseRestir)
            RunRows(1, [&](int X, int Y, FrameTally& T)
            {
                const size_t Pixel = static_cast<size_t>(Y) * Width + X;
                const RestirSurface& Surf = State.Surface[Pixel];
                CpuReservoir Temporal;
                if (Surf.Valid)
                {
                    Rng R((static_cast<uint32_t>(Y) * 73856093u) ^ (static_cast<uint32_t>(X) * 19349663u)
                          ^ (static_cast<uint32_t>(F + 1) * 83492791u));
                    Temporal = RestirTemporalReservoir(Surf, Spp, SunUp, State, Width, Height, X, Y, R);
                }
                Temporal.Normal = Surf.Ng;
                Temporal.Depth  = Surf.Depth;
                State.Temporal[Pixel] = Temporal;

                if (Surf.Depth > 0.0f)
                {
                    T.Covered += 1.0;
                    T.MSum += static_cast<double>(Temporal.SampleCount);
                    if (Temporal.SampleCount > T.MaxM) T.MaxM = Temporal.SampleCount;
                }
            });

        // ── PASS 2 — the indirect half's vertex, candidates, RIS, temporal (ReSTIR GI's pass 1) ─────────────────
        if (UseRestir && g_RestirGiReuse)
            RunRows(2, [&](int X, int Y, FrameTally& T)
            {
                const size_t Pixel = static_cast<size_t>(Y) * Width + X;
                const RestirSurface& Surf = State.Surface[Pixel];
                CpuGiReservoir Gi;
                if (Surf.Valid)
                {
                    Rng R((static_cast<uint32_t>(Y) * 40503u) ^ (static_cast<uint32_t>(X) * 2654435761u)
                          ^ (static_cast<uint32_t>(F + 1) * 2246822519u) ^ 0x27D4EB2Fu);
                    Gi = RestirGiTemporalReservoir(Surf, Spp, SunUp, State, Width, Height, X, Y, R);
                }
                Gi.StrideWidth = static_cast<float>(Width);
                State.GiTemporal[Pixel] = Gi;
                if (Surf.Depth > 0.0f)
                {
                    T.Covered += 1.0;
                    T.MSum += static_cast<double>(Gi.SampleCount);
                    if (Gi.SampleCount > T.MaxM) T.MaxM = Gi.SampleCount;
                }
                if (Gi.SampleCount > 0u) T.CoveredGi += 1.0;
            });

        // ── PASS 3 — spatial reuse, visibility, shade, resolve (one sample per pixel per frame) ──────────────────
        if (UseRestir)
            RunRows(3, [&](int X, int Y, FrameTally& T)
            {
                const size_t Pixel = static_cast<size_t>(Y) * Width + X;
                const RestirSurface& Surf = State.Surface[Pixel];
                DenoiseMirror::Accumulator& Accumulator = FilmCurrent[Pixel];

                vec3 L(0.0f);
                if (Surf.Depth <= 0.0f)
                {
                    // A missed primary ray sees the sky, exactly where the kernel resolves it (SkyAlong, the disc
                    //    included) rather than returning before the DI block. Radiance walks it from the camera.
                    Rng R((static_cast<uint32_t>(Y) * 40503u) ^ (static_cast<uint32_t>(X) * 2654435761u)
                          ^ (static_cast<uint32_t>(F + 1) * 2246822519u));
                    L = Radiance(Surf.O, Surf.D, R, Bounces, true);
                }
                else
                {
                    Rng R((static_cast<uint32_t>(Y) * 40503u) ^ (static_cast<uint32_t>(X) * 2654435761u)
                          ^ (static_cast<uint32_t>(F + 1) * 2246822519u));
                    CpuReservoir Published;
                    const vec3 Direct = RestirSpatialShade(State.Temporal[Pixel], Surf, SunUp, State,
                                                           Width, Height, X, Y, R, Published);
                    State.History[Pixel] = g_RestirHistorySplit ? State.Temporal[Pixel] : Published;
                    // ⚠️ The identity must survive the copy. A spatial TAP's merge rewrites the receiver's SelectedPoint
                    //    and SelectedLight but not its identity, and a reservoir with a zero weight never reaches the
                    //    branch that sets it at all — so `State.Temporal[Pixel]` above can still carry "no surface" while
                    //    the pixel plainly shows one. That was the whole of a measured 1275 phantom refusals per frame in
                    //    the direct pool; the history is the record the NEXT frame validates against, so it is normalised
                    //    here, where the record is final.
                    State.History[Pixel].Identity = Surf.Identity;
                    T.MSumPublished += static_cast<double>(Published.SampleCount);
                    if (Published.Visible == 0u) T.Occluded += 1.0;

                    // The indirect half. With the pool ON the frame's partition is
                    //      indirect = Beta0·[pool NEE at the vertex] + Beta0·[vertex terminal] + sub-trace from V
                    //    where the sub-trace is handed the vertex's own BSDF sample (direction, weight, pdf) so no
                    //    second direction is drawn and nothing is counted twice; its LastPdf seed is what keeps the
                    //    emitter-hit MIS at the second vertex paired with the pool's NEE at the first. With the pool
                    //    OFF this is the untouched single-sample arm — the pixel's own full path from the primary
                    //    point, which is also where an unusable vertex (glass, SSS) falls back to.
                    const RestirVertex& V = State.Vertex[Pixel];
                    vec3 Indirect(0.0f);
                    if (g_RestirGiReuse && V.Valid)
                    {
                        CpuGiReservoir GiPublished;
                        Indirect = RestirGiSpatialShade(State.GiTemporal[Pixel], Surf, V, SunUp, State,
                                                        Width, Height, X, Y, R, GiPublished);
                        State.GiHistory[Pixel] = State.GiTemporal[Pixel];   // pre-spatial, always (see the fix note)
                        Indirect += V.Beta * V.Terminal;
                        if (Bounces > 1)
                            Indirect += Radiance(V.P + V.Ng * 1.0e-4f, V.WiOut, R, Bounces - 1, false,
                                                 V.BsdfPdf, V.DeeperBeta);
                        T.MSumGi += static_cast<double>(GiPublished.SampleCount);
                        T.OccludedGi += GiPublished.Visible == 0u ? 1.0 : 0.0;
                        T.CoveredGi += 1.0;
                    }
                    else
                    {
                        const Hit Skip = {}; (void)Skip;
                        Indirect = Radiance(Surf.P + Surf.Ng * 1.0e-4f, Surf.D, R, Bounces, true);
                        State.GiHistory[Pixel] = CpuGiReservoir{};
                        State.VertexHistory[Pixel] = RestirVertex{};
                    }
                    L = Direct + Indirect;
                }
                const float Sample[3] = { L.x, L.y, L.z };
                float RadianceOut[3], VarianceOut = 0.0f;
                Accumulator.Resolve(Sample, RadianceOut, &VarianceOut);
                for (int C = 0; C < 3; ++C)
                    if (!(RadianceOut[C] >= 0.0f) || RadianceOut[C] > 1.0e7f) ++T.Bad;
                Out.Mean[Pixel * 3u + 0u] = RadianceOut[0];
                Out.Mean[Pixel * 3u + 1u] = RadianceOut[1];
                Out.Mean[Pixel * 3u + 2u] = RadianceOut[2];
                Out.Variance[Pixel] = VarianceOut;
                float* Surf4 = &Out.Surface[Pixel * 4u];
                Surf4[0] = Surf.Ng.x; Surf4[1] = Surf.Ng.y; Surf4[2] = Surf.Ng.z; Surf4[3] = Surf.Depth;
                if (Surf.Depth > 0.0f) { T.Surface += 1.0; T.SamplesBehind += static_cast<double>(Accumulator.Count); }
            });

        // ── The plain path: same budget, no reservoirs — one pass, Spp samples per pixel per frame ───────────────
        if (!UseRestir)
            RunRows(3, [&](int X, int Y, FrameTally& T)
            {
                const size_t Pixel = static_cast<size_t>(Y) * Width + X;
                const RestirSurface& Surf = State.Surface[Pixel];
                DenoiseMirror::Accumulator& Accumulator = FilmCurrent[Pixel];
                for (int S = 0; S < Spp; ++S)
                {
                    Rng R((static_cast<uint32_t>(Y) * 73856093u) ^ (static_cast<uint32_t>(X) * 19349663u)
                          ^ (static_cast<uint32_t>(F * Spp + S + 1) * 83492791u));
                    const float U = (static_cast<float>(X) + R.Next()) / static_cast<float>(Width);
                    const float V = (static_cast<float>(Y) + R.Next()) / static_cast<float>(Height);
                    const Frontier::ViewRay Ray = Camera.ConstructRay(U, V);
                    const vec3 O(Ray.OriginLocation.x, Ray.OriginLocation.y, Ray.OriginLocation.z);
                    const vec3 D = normalize(vec3(Ray.UnitDirection.x, Ray.UnitDirection.y, Ray.UnitDirection.z));
                    const vec3 L = Radiance(O, D, R, Bounces, false);
                    const float Sample[3] = { L.x, L.y, L.z };
                    float RadianceOut[3], VarianceOut = 0.0f;
                    Accumulator.Resolve(Sample, RadianceOut, &VarianceOut);
                    for (int C = 0; C < 3; ++C)
                        if (!(RadianceOut[C] >= 0.0f) || RadianceOut[C] > 1.0e7f) ++T.Bad;
                    Out.Mean[Pixel * 3u + 0u] = RadianceOut[0];
                    Out.Mean[Pixel * 3u + 1u] = RadianceOut[1];
                    Out.Mean[Pixel * 3u + 2u] = RadianceOut[2];
                    Out.Variance[Pixel] = VarianceOut;
                }
                // ⚠️ The film's surface record is the CENTRE-ray G-buffer pass 0 published (`Surf`), which is what the
                //    kernel's ResolveSurface writes and what the next frame's history read validates against. The
                //    sample loop must NOT publish its own last hit here: a jittered sample lands on a different texel
                //    by up to a pixel, and validating a reprojection against the wrong texel rejects (or accepts)
                //    reads the centre ray would not — it moved the plain path's multi-frame renders by ~1.4 % of
                //    pixels against the pre-rewrite mirror at AE 289 (2 frames) / 447 (3 frames), with frame 1
                //    bit-identical, which is exactly this: a first frame touches no history.
                float* Surf4 = &Out.Surface[Pixel * 4u];
                Surf4[0] = Surf.Ng.x; Surf4[1] = Surf.Ng.y; Surf4[2] = Surf.Ng.z; Surf4[3] = Surf.Depth;
                float* FilmSurf = &FilmSurfaceCurrent[Pixel * 4u];
                FilmSurf[0] = Surf.Ng.x; FilmSurf[1] = Surf.Ng.y; FilmSurf[2] = Surf.Ng.z; FilmSurf[3] = Surf.Depth;
                if (Surf.Depth > 0.0f) { T.Surface += 1.0; T.SamplesBehind += static_cast<double>(Accumulator.Count); }
            });

        const FrameTally& Film  = Tally[0];   // the G-buffer/film pass
        const FrameTally& Rest  = Tally[1];   // pass 1: the temporal reservoir this frame publishes
        const FrameTally& Gi    = Tally[2];   // pass 2: the indirect pool's temporal reservoir
        const FrameTally& Shade = Tally[3];   // pass 3: what the shading actually used
        Out.NonFinite += Film.Bad + Shade.Bad;

        const double SurfaceSum = Film.Surface;
        if (Verbose && (UseRestir || PanPerFrame != 0.0f))
            std::printf("[film]   frame %3d: running mean reprojected on %.1f%% of surface pixels (%.1f%% of those to a "
                        "moved texel), %.1f%% restarted on disocclusion, %.0f samples behind the mean\n",
                        F + 1, SurfaceSum > 0.0 ? 100.0 * Film.Reprojected / SurfaceSum : 0.0,
                        Film.Reprojected > 0.0 ? 100.0 * Film.Moved / Film.Reprojected : 0.0,
                        SurfaceSum > 0.0 ? 100.0 * Film.Disocclusion / SurfaceSum : 0.0,
                        SurfaceSum > 0.0 ? Film.SamplesBehind / SurfaceSum : 0.0);

        if (UseRestir)
        {
            long SurfacePixels = 0;
            for (size_t I = 0; I < Out.Variance.size(); ++I) if (Out.Surface[I * 4u + 3u] > 0.0f) ++SurfacePixels;
            Out.ReservoirCoverage = SurfacePixels > 0 ? 100.0 * Rest.Covered / static_cast<double>(SurfacePixels) : 0.0;
            Out.MeanReservoirM = Rest.Covered > 0.0 ? Rest.MSum / Rest.Covered : 0.0;
            Out.MeanReservoirMPublished = Rest.Covered > 0.0 ? Shade.MSumPublished / Rest.Covered : 0.0;
            Out.MeanGiM = Gi.Covered > 0.0 ? Gi.MSum / Gi.Covered : 0.0;
            Out.MeanGiMPublished = Shade.CoveredGi > 0.0 ? Shade.MSumGi / Shade.CoveredGi : 0.0;
            Out.GiOccludedPercent = Shade.CoveredGi > 0.0 ? 100.0 * Shade.OccludedGi / Shade.CoveredGi : 0.0;
            Out.IdentityRefusedCrossMaterial += Film.IdentityRefusedCrossMaterial;
            Out.IdentityRefused += Film.IdentityRefused
                                 + static_cast<double>(g_IdentityRefusedDirect.load())
                                 + static_cast<double>(g_IdentityRefusedGi.load());
            Out.Disocclusion    += Film.Disocclusion;
            if (Verbose)
                std::printf("[restir] frame %3d: %zu surface pixels, reservoirs on %.1f%% of them, mean M %.1f (max M %u), "
                            "shaded M %.1f, occluded selections %.1f%%, %ld bad samples%s\n",
                            F + 1, static_cast<size_t>(SurfacePixels), Out.ReservoirCoverage, Out.MeanReservoirM, Rest.MaxM,
                            Out.MeanReservoirMPublished,
                            Rest.Covered > 0.0 ? 100.0 * Shade.Occluded / Rest.Covered : 0.0, Film.Bad + Shade.Bad,
                            Rest.MaxM > 100000000u ? "  \u26a0 M is approaching the uint32 ceiling the kernel stores it in" : "");
            if (Verbose)
                std::printf("[restir gi] frame %3d: indirect pool on %.1f%% of surface pixels, mean M %.1f, "
                            "shaded M %.1f, occluded selections %.1f%%%s\n",
                            F + 1, Rest.Covered > 0.0 ? 100.0 * Gi.CoveredGi / Rest.Covered : 0.0,
                            Gi.Covered > 0.0 ? Gi.MSum / Gi.Covered : 0.0, Out.MeanGiMPublished, Out.GiOccludedPercent,
                            g_RestirGiReuse ? "" : "   (pool disabled: --restir-no-gi-reuse)");
            if (Verbose)
                std::printf("[restir] D10 identity: %ld film reads + %ld DI merges + %ld GI merges agreed on "
                            "normal+depth and belonged to a DIFFERENT surface (%ld of the reads showed a different "
                            "MATERIAL) — %s\n",
                            static_cast<long>(Film.IdentityRefused),
                            g_IdentityRefusedDirect.load(), g_IdentityRefusedGi.load(),
                            static_cast<long>(Film.IdentityRefusedCrossMaterial),
                            g_RestirIdentity ? "refused (identity validation ON)"
                                             : "inherited (the pre-D10 rule, --restir-no-identity)");
            if (F + 1 == Frames)
                std::printf("[restir gi] reasons: bad %ld escape %ld emitter %ld unlit %ld unusable %ld vertex %ld noPHat %ld\n",
                            g_GiBad.load(), g_GiEscape.load(), g_GiEmitter.load(), g_GiUnlit.load(),
                            g_GiUnusable.load(), g_GiVertex.load(), g_GiNoPHat.load());
            if (F + 1 == Frames)
                std::printf("[restir gi] temporal: tried %ld, refused for a MISSING previous vertex record %ld, merged %ld\n",
                            g_GiTemporalTried.load(), g_GiTemporalNoVertexHistory.load(), g_GiTemporalMerged.load());
        }

        std::swap(FilmPrevious, FilmCurrent);
        std::swap(FilmSurfacePrevious, FilmSurfaceCurrent);
        std::swap(FilmIdentityPrevious, FilmIdentityCurrent);
        std::swap(FilmMaterialPrevious, FilmMaterialCurrent);
        // The vertex history is read an entire frame later (temporal validation), so it follows the film buffers.
        std::swap(State.VertexHistory, State.Vertex);
        // ⚠️ NO History/Temporal swap here. There used to be one, and it silently made the reservoir the next
        //    frame's temporal source *the pass-1 output*, overwriting the very line that chooses between the
        //    temporal and the post-spatial reservoir — i.e. `--restir-no-history-split` was a no-op and the
        //    flag's two arms produced byte-identical PNGs (the tell). Pass 2 now decides what the history takes;
        //    pass 1 overwrites every pixel of State.Temporal next frame, so the two buffers stay disjoint.
    }
    Out.GiClass = State.GiClass;   // roadmap #5: the class map is the LAST frame's classification, same as the film
    return Out;
}

// The shipped à-trous chain, dispatched the way SwapchainExchange does it: one level per call, StepSize 1 << level,
//    the engine's σn/σz/σl, the filter's own 8×8 workgroup per level, the presentation tone map on the last live level.
void ApplyAtrousChain(SequenceResult& Result, int Extent, int Levels, float Exposure)
{
    const size_t Count = static_cast<size_t>(Extent) * Extent;
    std::vector<float> A(Count * 4u, 0.0f), B(Count * 4u, 0.0f);
    for (size_t I = 0; I < Count; ++I)
    {
        A[I * 4u + 0u] = Result.Mean[I * 3u + 0u];
        A[I * 4u + 1u] = Result.Mean[I * 3u + 1u];
        A[I * 4u + 2u] = Result.Mean[I * 3u + 2u];
        A[I * 4u + 3u] = Result.Variance[I];
    }
    std::vector<float> Output(Count * 4u, 0.0f);
    bool SourceIsA = true;
    for (int Level = 0; Level < Levels; ++Level)
    {
        DenoiseMirror::RunConfiguration Config;
        Config.Extent = static_cast<uint32_t>(Extent);
        Config.StepSize = 1u << Level;
        Config.Enabled = true;
        Config.FinalLevel = (Level == Levels - 1);
        Config.Exposure = Exposure;
        Config.ColourSaturation = 1.0f;
        DenoiseMirror::Run(Config, (SourceIsA ? A : B).data(), Result.Surface.data(),
                           (SourceIsA ? B : A).data(), Output.data());
        SourceIsA = !SourceIsA;
    }
    const std::vector<float>& Filtered = SourceIsA ? A : B;
    for (size_t I = 0; I < Count; ++I)
    {
        Result.Mean[I * 3u + 0u] = Filtered[I * 4u + 0u];
        Result.Mean[I * 3u + 1u] = Filtered[I * 4u + 1u];
        Result.Mean[I * 3u + 2u] = Filtered[I * 4u + 2u];
        Result.Variance[I] = Filtered[I * 4u + 3u];
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                              THE INTERFACE OVERLAY — the raster stage's CPU mirror, over the film
//------------------------------------------------------------------------------------------------------------------------
// The engine draws the interface as an overlay composited after the resolve (premultiplied alpha, one quad per
//    figure, InterfaceRaster.frag.slang). This is that stage on the CPU: for every film pixel whose primary ray
//    strikes the panel's face plane no deeper than the traced hit, walk the composed figures at that plane point —
//    THE SAME DistanceFigure/CoverageFromDistance text the GPU compiles — and alpha-over the film.
//
//    The ⑦ surface response is honoured per figure: emissive figures (whatever their ⑧ role) show their tint at
//    display luminance; albedo figures show BaseColour under a flat ambient — the P0 AmbientIrradiance term. The ⑧
//    role changes NOTHING here, which is the whole point: a Type 1 needle and a Type 2 telltale draw the same way;
//    only the light differs, and that was settled at scene-build time by MeasureRadiance/ComposeProxy.
void CompositeInterfaceOverlay(SequenceResult& Result, const Viewpoint& VP, int Width, int Height)
{
    if (!g_PanelActive || g_PanelFigures.empty()) return;

    Frontier::CameraProjection Camera;
    Camera.AssignSpatialLocation(VP.Position);
    Camera.AssignOrientationEuler(VP.PitchDegrees * kPi / 180.0f, VP.YawDegrees * kPi / 180.0f, 0.0f);
    Camera.AssignFieldOfView(VP.FieldOfView);
    Camera.AssignAspectRatio(static_cast<float>(Width) / static_cast<float>(Height));

    const vec3  N = g_PanelNormal;
    const float PlaneD = dot(g_PanelCentre, N);
    // The panel's plane basis: local +X (right) and +Y (up) in world space, at world scale. The figures' rows carry
    //    placement × scale, so plane points are measured in WORLD metres here and the figures' own rows undo it.
    const vec3 Right(1.0f, 0.0f, 0.0f);
    const vec3 Up(0.0f, 0.0f, 1.0f);

    long Touched = 0;
    for (int Y = 0; Y < Height; ++Y)
        for (int X = 0; X < Width; ++X)
        {
            const size_t Pixel = static_cast<size_t>(Y) * Width + X;
            const float U = (static_cast<float>(X) + 0.5f) / static_cast<float>(Width);
            const float V = (static_cast<float>(Y) + 0.5f) / static_cast<float>(Height);
            const Frontier::ViewRay Ray = Camera.ConstructRay(U, V);
            const vec3 O(Ray.OriginLocation.x, Ray.OriginLocation.y, Ray.OriginLocation.z);
            const vec3 D = normalize(vec3(Ray.UnitDirection.x, Ray.UnitDirection.y, Ray.UnitDirection.z));

            const float Facing = dot(D, N);
            if (Facing >= -1.0e-6f) continue;                    // behind the face or edge-on: single-sided fascia
            const float T = (PlaneD - dot(O, N)) / Facing;
            if (T <= 0.0f) continue;

            // Occlusion against the traced scene: the film's own G-buffer depth. A sphere in front of the panel
            //    hides it; the panel's own slab (5 mm behind the face) does not.
            const float SceneDepth = Result.Surface[Pixel * 4u + 3u];
            if (SceneDepth > 0.0f && SceneDepth < T - 2.0e-3f) continue;

            const vec3 P = O + D * T;
            const vec3 FromCentre = P - g_PanelCentre;
            const vec2 Plane(dot(FromCentre, Right), dot(FromCentre, Up));

            // One pixel in plane metres, from the projected pixel footprint at this depth (the fwidth stand-in).
            const float PixelWidth = 2.0f * T * std::tan(Camera.QueryFieldOfViewRadians() * 0.5f)
                                   / static_cast<float>(Height);

            // Walk the composed slots in submission order (back-to-front within the transparent group — the sort
            //    the engine already did) and alpha-over, exactly as the raster's blend state would.
            vec3  Colour(Result.Mean[Pixel * 3u + 0u], Result.Mean[Pixel * 3u + 1u], Result.Mean[Pixel * 3u + 2u]);
            bool  Hit = false;
            for (const Frontier::InterfaceInstanceFigure& Figure : g_PanelFigures)
            {
                // Plane point → figure-local metres: subtract the figure's translation projected on the panel
                //    axes, divide by the figure's scale (rows are orthonormal × scale — same inverse the GPU
                //    sampler and InterfacePointerProjection use).
                const vec3 FigRight(Figure.RowXx, Figure.RowYx, Figure.RowZx);
                const vec3 FigUp   (Figure.RowXy, Figure.RowYy, Figure.RowZy);
                const float Scale = length(FigRight);
                if (Scale < 1.0e-9f) continue;
                const vec3 Translation(Figure.RowXw, Figure.RowYw, Figure.RowZw);
                const vec3 FromFigure = P - Translation;
                const vec2 Local(dot(FromFigure, FigRight) / (Scale * Scale),
                                 dot(FromFigure, FigUp)    / (Scale * Scale));

                const uint32_t Category = Figure.CategoryPalette >> 24;
                const float Distance = DistanceFigure(Category, vec2(Local.x, Local.y),
                                                      vec2(Figure.HalfWidth, Figure.HalfHeight),
                                                      Figure.CornerRadius, Figure.ScalarAlpha, Figure.ScalarBeta);
                float Coverage = CoverageFromDistance(Distance, PixelWidth / Scale);
                Coverage *= ClipCoverage(vec2(Local.x, Local.y),
                                         vec4(Figure.ClipMinimumX, Figure.ClipMinimumY,
                                              Figure.ClipMaximumX, Figure.ClipMaximumY),
                                         0.0f, PixelWidth / Scale);
                if (Coverage <= 0.0f) continue;

                const auto Unpack = [](uint32_t Packed) -> vec4
                {
                    constexpr float K = 1.0f / 255.0f;
                    return vec4(static_cast<float>( Packed         & 0xFFu) * K,
                                static_cast<float>((Packed >>  8u) & 0xFFu) * K,
                                static_cast<float>((Packed >> 16u) & 0xFFu) * K,
                                static_cast<float>((Packed >> 24u) & 0xFFu) * K);
                };
                const auto ToLinear = [](const vec4& C) -> vec3
                {
                    const auto Chan = [](float E) { return E <= 0.04045f ? E / 12.92f
                                                                         : std::pow((E + 0.055f) / 1.055f, 2.4f); };
                    return vec3(Chan(C.x), Chan(C.y), Chan(C.z));
                };

                const vec4 Tint  = Unpack(Figure.Tint);
                const float Alpha = Coverage * Figure.Opacity * Tint.w;
                if (Alpha <= 0.0f) continue;

                // ⑦ InterfaceRaster.frag.slang's surface response, in the film's linear nits: emissive figures at
                //    display luminance, albedo figures under the flat ambient.
                const vec3 Emitted  = ToLinear(Tint) * kPanelDisplayNits;
                const vec3 Received = ToLinear(Unpack(Figure.BaseColour)) * kPanelAmbientNits;
                const float Weight  = std::clamp(Figure.EmissiveWeight, 0.0f, 1.0f);
                const vec3 FigureColour = mix(Received, Emitted, Weight);

                Colour = FigureColour * Alpha + Colour * (1.0f - Alpha);
                Hit = true;
            }
            if (Hit)
            {
                Result.Mean[Pixel * 3u + 0u] = Colour.x;
                Result.Mean[Pixel * 3u + 1u] = Colour.y;
                Result.Mean[Pixel * 3u + 2u] = Colour.z;
                ++Touched;
            }
        }
    std::printf("[material-level] interface overlay: %ld film pixels composited (%zu figures)\n",
                Touched, g_PanelFigures.size());
}

//------------------------------------------------------------------------------------------------------------------------
//                                          D10 — DOES THE OBJECT'S SHADOW FOLLOW IT?
//------------------------------------------------------------------------------------------------------------------------
// The acceptance for moving geometry is not "the transform was uploaded" — it is that the SHADOW (and the reflection) is
//    where the object is, this frame. This probe asks the renderer's own intersector that question directly, with no
//    image, no noise and no denoiser in the way:
//
//      · one grid of points on the studio floor, over and around the moving object's own footprint;
//      · from each point, the renderer's own shadow ray — the sun-ward `Intersect` the kernel's TraceShadow wraps;
//      · the set of points whose FIRST blocker belongs to the moving object: that set IS the object's shadow;
//      · the same measurement with the object at its peak excursion instead of its rest pose.
//
//    Both poses must cast one (a count of zero would mean the probe measured nothing), and the two sets must DIFFER —
//    which is precisely the statement "the shadow moved with the object". The centroid displacement is printed as
//    information, not asserted: a vertical bob under an oblique sun moves the footprint sideways and shrinks it, so the
//    only direction-agnostic, always-true assertion is that the footprint is not the same one.
int RunShadowProbe(int Material, float Drift, int Frames, bool Verbose)
{
    if (!(g_SunDirRender.y > 0.0f))
    {
        std::printf("[shadow-probe] RED — the sun is below the horizon (dir %.3f %.3f %.3f); a shadow test needs "
                    "daylight (try --sun 12)\n", g_SunDirRender.x, g_SunDirRender.y, g_SunDirRender.z);
        return 1;
    }
    const DriftCapture DriftGeo = CaptureDrift(Material);
    if (DriftGeo.Empty())
    {
        std::printf("[shadow-probe] RED — material %d carries no geometry\n", Material);
        return 1;
    }
    const int PeakFrame = (Frames - 1) / 2;
    const vec3 PeakOffset = DriftGeo.Axis * (Drift * static_cast<float>(PeakFrame));
    if (PeakFrame < 1 || Drift == 0.0f)
    {
        std::printf("[shadow-probe] RED — a probe needs a moving object: --drift metres and --frames N > 1 "
                    "(the excursion peaks at frame N/2)\n");
        return 1;
    }

    // The floor: the level's lowest plane, sampled on a grid that covers the object's footprint plus room for the
    //    shadow to leave it (a low sun throws a long one, so the margin is generous on purpose).
    float FloorY = 1.0e30f;
    for (const RenderTriangle& T : g_Tris) FloorY = min(FloorY, min(T.P0.y, min(T.P1.y, T.P2.y)));
    // ⚠️ The grid must cover where the SHADOW lands, not where the object is. The first version of this probe centred
    //    the window on the object's own footprint and found nothing: the sun is 6 m above the floor at a 64° elevation,
    //    so a 0.8 m swatch throws its shadow 2.5 m sideways — outside the window entirely, and the probe reported "no
    //    shadow" for an object that plainly had one. The window is therefore derived the way the shadow is: project the
    //    object's bounding box along the sun direction onto the floor plane, for BOTH poses, and take that box.
    const vec3  Extent = DriftGeo.Hi - DriftGeo.Lo;
    auto FloorPoint = [&](const vec3& V) -> vec2
    {
        const float T = (V.y - FloorY) / g_SunDirRender.y;   // sun-ward drop to the floor
        return vec2(V.x - g_SunDirRender.x * T, V.z - g_SunDirRender.z * T);
    };
    vec2 Lo2(1.0e30f, 1.0e30f), Hi2(-1.0e30f, -1.0e30f);
    for (int Corner = 0; Corner < 8; ++Corner)
    {
        const vec3 C(((Corner & 1) ? DriftGeo.Hi.x : DriftGeo.Lo.x),
                     ((Corner & 2) ? DriftGeo.Hi.y : DriftGeo.Lo.y),
                     ((Corner & 4) ? DriftGeo.Hi.z : DriftGeo.Lo.z));
        for (int Pose = 0; Pose < 2; ++Pose)
        {
            const vec2 S = FloorPoint(C + (Pose ? PeakOffset : vec3(0.0f, 0.0f, 0.0f)));
            Lo2 = vec2(min(Lo2.x, S.x), min(Lo2.y, S.y));
            Hi2 = vec2(max(Hi2.x, S.x), max(Hi2.y, S.y));
        }
    }
    const float Margin = 0.25f * max(Extent.x, max(Extent.y, Extent.z)) + 0.1f;
    const int Steps = 96;
    const float X0 = Lo2.x - Margin, X1 = Hi2.x + Margin;
    const float Z0 = Lo2.y - Margin, Z1 = Hi2.y + Margin;
    const float Eps = 1.0e-3f;

    std::vector<char> IsMoving(g_Tris.size(), 0);
    for (int Tri : DriftGeo.Triangles) IsMoving[static_cast<size_t>(Tri)] = 1;

    // One pose's footprint: every grid point whose sun-ward ray is blocked by the moving object, and where the shadow
    //    actually lands (the first blocker's hit point), so the two poses' shadow sets can be compared home to home.
    struct Footprint { int Count = 0; vec3 Centroid{0.0f, 0.0f, 0.0f}; std::vector<char> Shadowed; };
    auto Measure = [&](const vec3& Offset) -> Footprint
    {
        ApplyDrift(DriftGeo, Offset);
        Footprint F;
        F.Shadowed.assign(static_cast<size_t>(Steps) * Steps, 0);
        for (int Iz = 0; Iz < Steps; ++Iz)
            for (int Ix = 0; Ix < Steps; ++Ix)
            {
                const vec3 P(X0 + (X1 - X0) * (static_cast<float>(Ix) + 0.5f) / Steps, FloorY + Eps,
                             Z0 + (Z1 - Z0) * (static_cast<float>(Iz) + 0.5f) / Steps);
                const Hit H = Intersect(P, g_SunDirRender, 1.0e30f, -1);
                if (!H.Valid || !IsMoving[static_cast<size_t>(H.TriId)]) continue;
                F.Shadowed[static_cast<size_t>(Iz) * Steps + Ix] = 1;
                ++F.Count;
                F.Centroid += P;
            }
        if (F.Count > 0) F.Centroid = F.Centroid / static_cast<float>(F.Count);
        return F;
    };

    const Footprint Rest = Measure(vec3(0.0f, 0.0f, 0.0f));
    const Footprint Peak = Measure(PeakOffset);
    ApplyDrift(DriftGeo, vec3(0.0f, 0.0f, 0.0f));   // leave the level as it was found

    int Changed = 0, StillShadowed = 0;
    for (size_t I = 0; I < Rest.Shadowed.size(); ++I)
    {
        if (Rest.Shadowed[I] == Peak.Shadowed[I]) { if (Rest.Shadowed[I]) ++StillShadowed; continue; }
        ++Changed;
    }
    const vec3 CentroidDelta = Peak.Centroid - Rest.Centroid;
    const int Points = Steps * Steps;
    if (Verbose)
        std::printf("[shadow-probe] sun (%.3f %.3f %.3f), floor y %.3f, grid %d x %d over x[%.2f,%.2f] z[%.2f,%.2f]\n",
                    g_SunDirRender.x, g_SunDirRender.y, g_SunDirRender.z, FloorY, Steps, Steps, X0, X1, Z0, Z1);
    std::printf("[shadow-probe] object: x[%.2f,%.2f] y[%.2f,%.2f] z[%.2f,%.2f]\n",
                DriftGeo.Lo.x, DriftGeo.Hi.x, DriftGeo.Lo.y, DriftGeo.Hi.y, DriftGeo.Lo.z, DriftGeo.Hi.z);
    std::printf("[shadow-probe] rest        : %d of %d floor points blocked by material %d (centroid %.2f %.2f %.2f)\n",
                Rest.Count, Points, Material, Rest.Centroid.x, Rest.Centroid.y, Rest.Centroid.z);
    std::printf("[shadow-probe] peak %.3f m : %d of %d floor points blocked by material %d (%.0f kept the same point)\n",
                length(PeakOffset), Peak.Count, Points, Material, static_cast<double>(StillShadowed));
    std::printf("[shadow-probe] footprint: %d points changed state, centroid moved (%.3f %.3f %.3f) = %.3f m\n",
                Changed, CentroidDelta.x, CentroidDelta.y, CentroidDelta.z, length(CentroidDelta));

    if (Rest.Count == 0 || Peak.Count == 0)
    {
        std::printf("[shadow-probe] RED — a pose casts no shadow on the floor (%d rest / %d peak): the probe "
                    "measured nothing\n", Rest.Count, Peak.Count);
        return 1;
    }
    if (Changed == 0)
    {
        std::printf("[shadow-probe] RED — the shadow footprint is IDENTICAL in both poses: the shadow did not "
                    "follow the object\n");
        return 1;
    }
    std::printf("[shadow-probe] PASS — both poses cast a shadow ONTO the floor, and the footprint moved with the "
                "object (%d of %d points changed state)\n", Changed, Points);
    return 0;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                        MAIN
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char** ArgumentValues)
{
    std::setlocale(LC_ALL, "C");
    std::string OutPath = "Exhibits/Gallery/Materials/MaterialLibrary_View.png";
    std::string RawOutPath;
    std::string ClassMapPath;   // roadmap #5 diagnostic: per-pixel GI class, written as a grey PNG when asked for
    std::string View = "default";
    std::string FogName = "clear";
    int Width = 960, Height = 540, Spp = 64, Bounces = 6;
    int Frames = 1;             // accumulation frames: the app's own ResolveSurface loop, off-GPU
    int DenoiseLevels = 5;      // the shipped chain's ceiling (ReSTIRIntegratorConfiguration::DenoiseLevelCount)
    double SunHour = 17.93;
    float Exposure = 1.05f;
    float PanPerFrame = 0.0f;   // [m/frame] camera drift, so the reprojection has something to reproject
    bool  UseRestir = false;    // the CPU ReSTIR DI mirror (see the block above)
    bool  Denoise = false;      // the shipped à-trous chain over the accumulated film
    unsigned Threads = std::thread::hardware_concurrency();
    if (Threads == 0u) Threads = 2u;
    bool ProbeMode = false;   // D10: --shadow-probe — does the moving object's shadow follow it? (no image, no noise)
    uint32_t SeedStream = 0u; // #7: the RNG stream offset — an independent reference and an independent arm

    for (int I = 1; I < ArgumentCount; ++I)
    {
        const std::string A = ArgumentValues[I];
        auto Next = [&](const char* Name) -> const char*
        {
            if (I + 1 >= ArgumentCount) { std::printf("[material-level] %s needs a value\n", Name); std::exit(2); }
            return ArgumentValues[++I];
        };
        if      (A == "--out")      OutPath = Next("--out");
        else if (A == "--raw-out")  RawOutPath = Next("--raw-out");
        else if (A == "--level")    g_Level = Next("--level");   // materials (M10 library) | showcase (the product default)
        else if (A == "--view")     View = Next("--view");
        else if (A == "--fog")      FogName = Next("--fog");
        else if (A == "--width")    Width = std::atoi(Next("--width"));
        else if (A == "--height")   Height = std::atoi(Next("--height"));
        else if (A == "--spp")      Spp = std::atoi(Next("--spp"));
        else if (A == "--bounce")   Bounces = std::atoi(Next("--bounce"));
        else if (A == "--sun")      SunHour = std::atof(Next("--sun"));
        else if (A == "--exposure") Exposure = static_cast<float>(std::atof(Next("--exposure")));
        else if (A == "--threads")  Threads = static_cast<unsigned>(std::atoi(Next("--threads")));
        else if (A == "--no-sun")   g_SunNee = false;
        else if (A == "--sun-direct") g_SunDirectGain = static_cast<float>(std::atof(Next("--sun-direct")));   // panel Direct slider (default = product default)
        else if (A == "--sun-pick")   g_SunPickProbability = static_cast<float>(std::atof(Next("--sun-pick"))); // override the power-proportional sun coin (A/B; 0.5 = legacy)
        else if (A == "--row")      g_RowFilter = std::atoi(Next("--row"));
        else if (A == "--frames")   Frames = std::atoi(Next("--frames"));
        else if (A == "--pan")      PanPerFrame = static_cast<float>(std::atof(Next("--pan")));
        else if (A == "--restir")   UseRestir = true;
        else if (A == "--no-reproject") g_RestirNoReproject = true;
        else if (A == "--restir-no-history-split") g_RestirHistorySplit = false;
        else if (A == "--restir-no-gi-reuse")      g_RestirGiReuse = false;
        else if (A == "--restir-final-visibility") g_RestirFinalVisibility = true;   // the pre-reuse arm: re-trace the merged selection
        else if (A == "--no-glints") g_MatGlintsOff = true;   // M6's A/B: the pre-glint surface
        else if (A == "--restir-dead-skip") g_RestirDeadSkip = true;   // #6's refused arm (adaptive-M bias, +9 % RMSE): kept for re-measurement
        else if (A == "--restir-no-identity")      g_RestirIdentity = false;
        else if (A == "--drift")        g_RestirDrift = static_cast<float>(std::atof(Next("--drift")));
        else if (A == "--drift-material") g_RestirDriftMaterial = std::atoi(Next("--drift-material"));
        else if (A == "--shadow-probe") ProbeMode = true;
        else if (A == "--seed-stream") SeedStream = static_cast<uint32_t>(std::atoi(Next("--seed-stream")));
        else if (A == "--class-map") ClassMapPath = Next("--class-map");
        else if (A == "--drift-axis")
        {
            const std::string Ax = Next("--drift-axis");
            if      (Ax == "x") g_RestirDriftAxis = 0;
            else if (Ax == "y") g_RestirDriftAxis = 1;
            else if (Ax == "z") g_RestirDriftAxis = 2;
            else { std::printf("[material-level] --drift-axis expects x, y or z (got '%s')\n", Ax.c_str()); return 2; }
        }
        else if (A == "--no-sun-coin") g_RestirNoSunCoin = true;
        else if (A == "--taps")     g_RestirSpatialTaps = static_cast<uint32_t>(std::atoi(Next("--taps")));
        else if (A == "--restir-bounce-mis") g_RestirBounceMis = true;
        else if (A == "--m-cap")    g_RestirMCap = static_cast<uint32_t>(std::atoi(Next("--m-cap")));
        else if (A == "--panel-gain") g_PanelGain = static_cast<float>(std::atof(Next("--panel-gain")));   // 0 = overlay only, no scene light
        else if (A == "--denoise")  Denoise = true;
        else if (A == "--denoise-levels") DenoiseLevels = std::atoi(Next("--denoise-levels"));
        else if (A == "--help")
        {
            std::printf("usage: MaterialLevelViewport [--out file.png] [--level materials|showcase]\n"
                        "                            [--view default|wide|glass|row5]   (materials)\n"
                        "                            [--view default|grid|metals|glass|wide|grid400|paint-candy|paint-glitter|paint-iridescent|paint-cobalt|paint-copper] (showcase) [--row N]\n"
                        "                            [--width W] [--height H] [--spp N] [--bounce N] [--sun H]\n"
                        "                            [--fog clear|morning|backlit] [--exposure X] [--threads N]\n"
                        "                            [--frames N] [--pan metres] [--restir] [--no-reproject]\n"
                        "                            [--drift metres] [--drift-material idx] [--restir-no-identity]\n"
                        "                            [--shadow-probe]  (with --drift/--frames: does the shadow follow?)\n"
                        "                            [--seed-stream N] (an independent RNG stream: 0 = the shipped one)\n"
                        "                            [--class-map file.png] (roadmap #5: per-pixel GI class, grey = class * 32)\n"
                        "                            [--drift-axis x|y|z]\n"
                        "                            [--denoise] [--denoise-levels N]\n"
                        "                            [--panel-gain X] (showcase: the interface panel's luminaire gain; 0 = widget only)\n");
            return 0;
        }
        else { std::printf("[material-level] unknown argument '%s' (try --help)\n", A.c_str()); return 2; }
    }
    if (Width < 8) Width = 8;
    if (Height < 8) Height = 8;
    if (Spp < 1) Spp = 1;

    std::printf("================================================================================\n");
    std::printf(g_Level == "showcase"
                ? "   PROJECT-ZERO — SHOWCASE, THE DEFAULT LEVEL, CPU RENDER OF THE VULKAN SCENE   \n"
                : "   PROJECT-ZERO — MATERIAL LIBRARY LEVEL (M10), CPU RENDER OF THE VULKAN SCENE   \n");
    std::printf("================================================================================\n");

    if (!BuildLevel())
    {
        std::printf("[material-level] RED — the level did not build\n");
        return 1;
    }
    g_Sky.AssignSunHour(SunHour);
    if      (FogName == "morning") g_Sky.AssignFogScenario(Frontier::ProjectZero::SkyFogIntegrator::FogScenario::Morning);
    else if (FogName == "backlit") g_Sky.AssignFogScenario(Frontier::ProjectZero::SkyFogIntegrator::FogScenario::Backlit);
    else                           g_Sky.AssignFogScenario(Frontier::ProjectZero::SkyFogIntegrator::FogScenario::Clear);
    {
        const Frontier::Vector3 S = g_Sky.QuerySunDirectionRender();
        g_SunDirRender = normalize(vec3(S.x, S.y, S.z));
    }
    std::printf("[material-level] sky: sun hour %.2f, sun dir render (%.3f %.3f %.3f), fog %s\n",
                SunHour, g_SunDirRender.x, g_SunDirRender.y, g_SunDirRender.z, FogName.c_str());

    // The power-proportional sun coin, the engine's formula verbatim (GameExecution ④d): sunFlux = the packed
    //    direct term's luminance × sin(elevation) × the level's footprint; lampFlux = Σ area·luminance × π.
    //    --sun-pick overrides for A/Bs; clamped to [0.05, 0.95] like AssignSunPickProbability.
    if (g_SunPickProbability <= 0.0f)
    {
        const Frontier::Vector3 SunRgb = g_Sky.QuerySunRadiance();
        const float SunLum = 0.11f * g_SunDirectGain
                           * (0.2126f * SunRgb.x + 0.7152f * SunRgb.y + 0.0722f * SunRgb.z);
        const float SinElevation = max(0.0f, g_SunDirRender.y);          // render frame is Y-up
        float MinX = 1.0e9f, MaxX = -1.0e9f, MinY = 1.0e9f, MaxY = -1.0e9f;
        for (const RenderTriangle& T : g_Tris)
        {
            const vec3 Ps[3] = { T.P0, T.P1, T.P2 };
            for (const vec3& P : Ps)
            {
                MinX = min(MinX, P.x); MaxX = max(MaxX, P.x);
                MinY = min(MinY, P.y); MaxY = max(MaxY, P.y);
            }
        }
        const float Footprint = max(1.0f, (MaxX - MinX) * (MaxY - MinY));
        float LampPower = 0.0f;
        for (const EmissiveTriangle& L : g_Lights) LampPower += L.Power;
        const float SunFlux  = SunLum * SinElevation * Footprint;
        const float LampFlux = LampPower * kPi;
        if (SunFlux + LampFlux > 0.0f)
        {
            const float P = SunFlux / (SunFlux + LampFlux);
            g_SunPickProbability = P < 0.05f ? 0.05f : P > 0.95f ? 0.95f : P;
        }
    }
    if (g_SunPickProbability > 0.0f)
        std::printf("[material-level] sun pick: %.3f power-proportional (was the fixed 0.5 coin)\n",
                    static_cast<double>(g_SunPickProbability));

    const Viewpoint VP = (g_Level == "showcase") ? ShowcaseViewpointFor(View) : ViewpointFor(View);
    std::printf("[material-level] view '%s': eye (%.2f %.2f %.2f), pitch %.1f°, yaw %.1f°, FoV %.0f°, %dx%d @ %d spp, %d bounces, %d frame%s%s%s%s\n",
                View.c_str(), VP.Position.x, VP.Position.y, VP.Position.z, VP.PitchDegrees, VP.YawDegrees, VP.FieldOfView,
                Width, Height, Spp, Bounces, Frames, Frames == 1 ? "" : "s",
                UseRestir ? ", RESTIR DI (CPU mirror)" : "",
                PanPerFrame != 0.0f ? ", camera pan" : "",
                Denoise ? ", à-trous" : "");

    if (ProbeMode) return RunShadowProbe(g_RestirDriftMaterial, g_RestirDrift, Frames, true);
    if (SeedStream != 0u)
        std::printf("[material-level] seed stream %u — an INDEPENDENT draw of the same estimator (roadmap #7: the RMSE "
                    "floor is measured rather than shared)\n", SeedStream);
    g_SeedStream = SeedStream;

    Frontier::ShadingTableSet Tables = Frontier::ShadingTableCodec::Bake(1024u);
    g_Tables = &Tables;

    if (Denoise && Width != Height)
    {
        std::printf("[material-level] RED — --denoise runs the shipped filter, whose dispatch is square (8×8 workgroup); "
                    "render a square panel\n");
        return 2;
    }

    std::printf("[material-level] %s\n", UseRestir
        ? "estimator: the kernel's DI block on the CPU — RIS candidates, temporal + spatial reuse (25° / 10 % validation, "
          "20× M-clamp, pairwise MIS), visibility re-trace; indirect from the same BSDF-sampled bounce."
        : "estimator: brute-force accumulation — NEE with MIS for lamps, the sun's disc, and the sky on a missed ray.");
    if (UseRestir && g_RestirNoReproject)
        std::printf("[material-level] reprojection DISABLED: the temporal merge reads the same pixel (the pre-R7a rule)\n");

    SequenceResult Sequence = RenderSequence(VP, Width, Height, Spp, Bounces, Frames, PanPerFrame, UseRestir, Threads, true);
    // The interface overlay composites on LINEAR radiance, straight after the resolve — the engine's own order.
    //    (With --denoise the filter then runs over the composited film; the gallery render does not filter.)
    CompositeInterfaceOverlay(Sequence, VP, Width, Height);
    if(!RawOutPath.empty()){
        ColourTransfer RawTransfer;RawTransfer.ToneMap=Frontier::ToneMapCategory::Aces;RawTransfer.Exposure=Exposure;RawTransfer.Saturation=1.0f;
        std::vector<unsigned char> RawPng(Sequence.Mean.size());
        for(size_t i=0;i<Sequence.Mean.size();i+=3)ColourPipeline::ApplyToByte(RawTransfer,Sequence.Mean.data()+i,RawPng.data()+i);
        if(!PngWriteCounterpart::WritePng(RawOutPath.c_str(),Width,Height,3,RawPng.data(),Width*3))return 2;
        std::printf("[material-level] unfiltered accumulated film -> %s\n",RawOutPath.c_str());
    }
    if (Denoise) ApplyAtrousChain(Sequence, Width, DenoiseLevels, Exposure);
    const std::vector<float>& Film = Sequence.Mean;
    if (UseRestir)
        std::printf("[material-level] reservoirs: %.1f %% of surface pixels held one, mean M %.1f\n",
                    Sequence.ReservoirCoverage, Sequence.MeanReservoirM);
    if (UseRestir && !g_RestirFinalVisibility)
        std::printf("[material-level] visibility reuse: %ld spatial-pass shadow rays skipped (the winner's stored "
                    "test answered) — the A/B arm is --restir-final-visibility\n",
                    g_VisibilityReuseSaved.load());
    if (UseRestir && g_RestirDeadSkip)
        std::printf("[material-level] dead-entry skip (#6, the REFUSED arm): %ld direct + %ld GI merges refused a "
                    "W = 0 entry — measured +9 %% RMSE over the honest rule (adaptive-M bias)\n",
                    g_DeadSkipDirect.load(), g_DeadSkipGi.load());

    // Presentation: the engine's own transfer, at the engine's own manual exposure.
    ColourTransfer Transfer;
    Transfer.ToneMap = Frontier::ToneMapCategory::Aces;
    Transfer.Exposure = Exposure;
    Transfer.Saturation = 1.0f;   // Manual mode hard-wires the Purkinje curve to 1.0 (ExposureIntegrator)
    std::vector<unsigned char> Png(static_cast<size_t>(Width) * Height * 3u);
    double Mean = 0.0;
    long Bad = 0;
    for (size_t I = 0; I < Film.size(); I += 3u)
    {
        const float Linear[3] = { Film[I], Film[I + 1u], Film[I + 2u] };
        for (int C = 0; C < 3; ++C)
            if (!(Film[I + static_cast<size_t>(C)] >= 0.0f) || Film[I + static_cast<size_t>(C)] > 1.0e7f) ++Bad;
        Mean += (Linear[0] + Linear[1] + Linear[2]) / 3.0;
        ColourPipeline::ApplyToByte(Transfer, Linear, &Png[I]);
    }
    Mean /= static_cast<double>(Film.size() / 3u);
    std::printf("[material-level] film: mean %.4f, %ld non-finite/out-of-range samples\n", Mean, Bad + Sequence.NonFinite);

    const int Ok = PngWriteCounterpart::WritePng(OutPath.c_str(), Width, Height, 3, Png.data(), Width * 3);
    std::printf("[material-level] %s -> %s\n", Ok != 0 ? "wrote" : "FAILED", OutPath.c_str());

    // Roadmap #5: the class map, written with the same writer as the film so the two are the same frame. The grey step
    //    is 32 per class, and the analysis tool knows it (`GiClassError.cpp`), so the PNG is a picture of the
    //    classification and the tool's masks come from the same file the eye can check.
    if (!ClassMapPath.empty())
    {
        std::vector<unsigned char> Map(static_cast<size_t>(Width) * Height * 3u, 0u);
        long Tally[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        for (size_t I = 0; I < Sequence.GiClass.size(); ++I)
        {
            const unsigned char Raw = Sequence.GiClass[I];
            const unsigned char C = Raw <= kGiClassNoSample ? Raw : static_cast<unsigned char>(kGiClassNone);
            Tally[C] += 1;
            const unsigned char Grey = static_cast<unsigned char>(C * 32u);
            Map[I * 3u + 0u] = Grey; Map[I * 3u + 1u] = Grey; Map[I * 3u + 2u] = Grey;
        }
        const int MapOk = PngWriteCounterpart::WritePng(ClassMapPath.c_str(), Width, Height, 3, Map.data(), Width * 3);
        std::printf("[material-level] %s -> %s (GI class map)\n", MapOk != 0 ? "wrote" : "FAILED",
                    ClassMapPath.c_str());
        std::printf("[material-level] GI classes (last frame): none %ld, bad %ld, escape %ld, emitter %ld, unlit %ld, "
                    "unusable %ld, VERTEX %ld, no sample %ld\n", Tally[0], Tally[1], Tally[2], Tally[3], Tally[4],
                    Tally[5], Tally[6], Tally[7]);
    }
    return Ok != 0 ? 0 : 1;
}
