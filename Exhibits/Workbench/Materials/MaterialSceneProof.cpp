//============================================================================================================================================
//                                                  MATERIALSCENEPROOF.CPP
//============================================================================================================================================
// M8 gate — full-scene validation + the Tier B decision data. CornellBox (committed), GlassProof (committed), the
// R4b shaderball level (generated headless via ShaderBallStructure — the same export-once path GameExecution uses),
// and Sponza when present (fetch script; skipped otherwise — the conditional idiom of the M7a runner's GameExecution
// check): decode → Finalise at slab_limit 1/2/8 → census + fold review. Synthetic multi-slab probes pin the fold
// (real content is all single-slab — itself the Tier datum); every material of every scene shades through the M7b
// preview entry (Bad == 0); the 128-cell consumption matrix re-pins the M3 table the perf budget rests on. The Tier
// verdict is printed AND asserted (section F). No GPU, no window.

#include "SlangCpuShim.h"
#include "ContentCodec.h"
#include "SceneCodec.h"
#include "SceneStructure.h"
#include "MaterialIndex.h"
#include "ShaderBallStructure.h"
#include "ShaderballPreview.h"

#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// The slang file is instantiated exactly once in this binary — inside the linked ShaderballPreview.cpp TU (Engine/ContentInterchange) (with its
// real table-backed Fetch*). Re-including it here would multiply-define every shading function at link time, so §E
// reaches the pure stage×channel table through an extern declaration of its single instantiation instead.
bool ReflectanceConsumes(unsigned int Selection, unsigned int Channel);

namespace {

int Passed = 0, Failed = 0, Serial = 0;

void Check(bool Condition, const char* Label)
{
    ++Serial;
    if (Condition) { ++Passed; std::printf("ok %d - %s\n", Serial, Label); }
    else           { ++Failed; std::printf("FAIL %d - %s\n", Serial, Label); }
    std::fflush(stdout);
}

bool Contains(const char* Haystack, const char* Needle)
{
    return Haystack && Needle && std::strstr(Haystack, Needle) != nullptr;
}

bool FileExists(const char* Path)
{
    FILE* F = std::fopen(Path, "rb");
    if (F) std::fclose(F);
    return F != nullptr;
}

bool ReadFile(const char* Path, std::vector<unsigned char>& Out)
{
    Out.clear();
    FILE* F = std::fopen(Path, "rb");
    if (!F) return false;
    unsigned char Buf[4096];
    size_t N = 0;
    while ((N = std::fread(Buf, 1, sizeof(Buf), F)) > 0) Out.insert(Out.end(), Buf, Buf + N);
    std::fclose(F);
    return !Out.empty();
}

// Local name tables (MaterialInspector's are UI-linked; these pin the same literals for the census print).
const char* SelName(Frontier::MaterialReflectance S)
{
    static const char* kNames[8] = { "Standard", "Anisotropic", "ClearCoated", "Cloth", "Subsurface", "Transmissive", "EmissiveOnly", "Unlit" };
    const uint32_t I = static_cast<uint32_t>(S);
    return kNames[I < 8u ? I : 0u];
}

const char* CompName(uint32_t C)
{
    static const char* kNames[4] = { "Simple", "Single", "Complex", "Special" };
    return kNames[C < 4u ? C : 0u];
}

int FindByName(const Frontier::MaterialIndex& Index, const char* Name)
{
    const std::vector<Frontier::MaterialDescriptor>& Ds = Index.QueryDescriptors();
    for (uint32_t I = 0u; I < Ds.size(); ++I)
        if (Ds[I].Name == Name) return static_cast<int>(I);
    return -1;
}

} // namespace

int main()
{
    std::setlocale(LC_ALL, "C");
    using namespace Frontier;

    // ── A. Scene loads ──────────────────────────────────────────────────────────────────────────────────────────
    SceneStructure Cornell, Glass, Ball;
    std::string Error;
    SceneDecodeConfiguration Decode;
    Decode.SlabLimit = 1u;
    Check(ContentCodec::Decode("Projects/Project-Zero/Content/Scenes/CornellBox.gltf", Cornell, nullptr, Decode, &Error), "A1 CornellBox decodes (textures nullptr)");
    Check(Cornell.QueryMaterials().QueryCount() == 10u, "A2 Cornell 9 materials + fallback");
    Check(ContentCodec::Decode("Projects/Project-Zero/Content/Scenes/GlassProof.gltf", Glass, nullptr, Decode, &Error), "A3 GlassProof decodes");
    Check(Glass.QueryMaterials().QueryCount() == 5u, "A4 GlassProof 4 materials + fallback");
    {
        ShaderBallStructure Gen;
        Gen.Construct();
        Check(Gen.Export("/tmp/MaterialScenes_ShaderBall.gltf", &Error), "A5 shaderball generates headless (export-once path)");
        Check(ContentCodec::Decode("/tmp/MaterialScenes_ShaderBall.gltf", Ball, nullptr, Decode, &Error), "A6 generated shaderball decodes");
    }
    Check(Ball.QueryMaterials().QueryCount() == 29u, "A7 shaderball 28 authored + fallback");
    const bool HasSponza = FileExists("Projects/Project-Zero/Content/Scenes/Sponza/Sponza.gltf");
    SceneStructure Sponza;
    if (HasSponza)
        Check(ContentCodec::Decode("Projects/Project-Zero/Content/Scenes/Sponza/Sponza.gltf", Sponza, nullptr, Decode, &Error), "A8 Sponza decodes");
    else
        Check(true, "A8 Sponza skipped (fetch script; geometry-only, not committed)");
    const uint32_t SponzaCount = HasSponza ? Sponza.QueryMaterials().QueryCount() : 0u;
    std::printf("[scene] Sponza materials: %u%s\n", SponzaCount, HasSponza ? "" : " (absent)");

    // ── B. Limit matrix 1/2/8: zero folds, resident == authored, census ──────────────────────────────────────────
    struct SceneCase { const char* Tag; SceneStructure* Level; uint32_t WantCount; };
    SceneCase Cases[4] = { { "cornell", &Cornell, 10u }, { "glass", &Glass, 5u }, { "ball", &Ball, 29u }, { "sponza", &Sponza, SponzaCount } };
    const uint32_t Limits[3] = { 1u, 2u, 8u };
    uint32_t AuthoredTotal = 0u, MaterialsTotal = 0u;
    for (uint32_t Ci = 0u; Ci < 4u; ++Ci)
    {
        if (Ci == 3u && !HasSponza) continue;
        SceneCase& C = Cases[Ci];
        char Label[128];
        for (uint32_t Li = 0u; Li < 3u; ++Li)
        {
            std::vector<std::string> Report;
            C.Level->AccessMaterials().Finalise(Limits[Li], &Report);
            std::snprintf(Label, sizeof(Label), "B-%s/%u zero folds on real content", C.Tag, Limits[Li]);
            Check(Report.empty(), Label);
            bool ResidentOne = true;
            for (const MaterialRecord& R : C.Level->AccessMaterials().QueryRecords())
                ResidentOne = ResidentOne && R.SlabCount == 1u;
            std::snprintf(Label, sizeof(Label), "B-%s/%u every record resident 1 slab", C.Tag, Limits[Li]);
            Check(ResidentOne && C.Level->AccessMaterials().QueryCount() == C.WantCount, Label);
        }
        // Census (reviewed in the log, quoted in report §9).
        C.Level->AccessMaterials().Finalise(1u, nullptr);
        std::printf("[scene] --- %s census (limit 1) ---\n", C.Tag);
        const std::vector<MaterialDescriptor>& Ds = C.Level->AccessMaterials().QueryDescriptors();
        const std::vector<MaterialRecord>& Rs = C.Level->AccessMaterials().QueryRecords();
        for (uint32_t I = 0u; I < Ds.size(); ++I)
        {
            uint32_t Folded = 0u;
            const std::vector<MaterialSlabDescriptor> Flat = MaterialIndex::Flatten(Ds[I], 1u, &Folded, nullptr);
            static const MaterialSlabDescriptor kDef{};
            const MaterialSlabDescriptor& S = Flat.empty() ? kDef : Flat.front();
            std::printf("[scene]   %-16s %-12s %-7s authored %u\n", Ds[I].Name.c_str(),
                        SelName(MaterialIndex::DeriveReflectance(Ds[I], S)), CompName(Rs[I].Complexity),
                        static_cast<uint32_t>(Ds[I].Slabs.size()));
            AuthoredTotal += static_cast<uint32_t>(Ds[I].Slabs.size());
            ++MaterialsTotal;
        }
    }

    // GlassProof shape (by name — the M8-authored scene pins exactly).
    Check(FindByName(Glass.QueryMaterials(), "clear-glass") == 0 && FindByName(Glass.QueryMaterials(), "frosted-glass") == 1 &&
          FindByName(Glass.QueryMaterials(), "emissive-panel") == 2 && FindByName(Glass.QueryMaterials(), "diffuse-wall") == 3,
          "B-glass material order");
    {
        const std::vector<MaterialDescriptor>& Ds = Glass.QueryMaterials().QueryDescriptors();
        uint32_t Folded = 0u;
        const std::vector<MaterialSlabDescriptor> FlatClear = MaterialIndex::Flatten(Ds[0], 1u, &Folded, nullptr);
        const std::vector<MaterialSlabDescriptor> FlatFrost = MaterialIndex::Flatten(Ds[1], 1u, &Folded, nullptr);
        const std::vector<MaterialSlabDescriptor> FlatLamp = MaterialIndex::Flatten(Ds[2], 1u, &Folded, nullptr);
        const std::vector<MaterialSlabDescriptor> FlatWall = MaterialIndex::Flatten(Ds[3], 1u, &Folded, nullptr);
        const MaterialSlabDescriptor& Clear = FlatClear.front();
        const MaterialSlabDescriptor& Frost = FlatFrost.front();
        const MaterialSlabDescriptor& Lamp = FlatLamp.front();
        const MaterialSlabDescriptor& Wall = FlatWall.front();
        Check(MaterialIndex::DeriveReflectance(Ds[0], Clear) == MaterialReflectance::Transmissive &&
              MaterialIndex::DeriveReflectance(Ds[1], Frost) == MaterialReflectance::Transmissive,
              "B-glass both panes Transmissive (weight > 0)");
        Check(Clear.TransmissionWeight == 1.0f && Clear.SpecularRoughness == 0.06f, "B-glass clear values exact");
        Check(Frost.TransmissionWeight == 0.8f && Frost.SpecularRoughness == 0.4f && Ds[1].VolumeThickness == 0.1f,
              "B-glass frosted values + volume thickness exact");
        Check(MaterialIndex::DeriveReflectance(Ds[2], Lamp) == MaterialReflectance::Standard && Lamp.EmissionLuminance == 3.0f,
              "B-glass emissive panel Standard + 3 nit (BaseWeight defaults 1: EmissiveOnly unreachable from glTF)");
        Check(MaterialIndex::DeriveReflectance(Ds[3], Wall) == MaterialReflectance::Standard, "B-glass diffuse wall Standard");
    }

    // Shaderball encode→decode fidelity on rich slabs (M6 TIER-1 exact, TIER-2 approximate — see report §6).
    {
        const MaterialIndex& Idx = Ball.QueryMaterials();
        const int Coat = FindByName(Idx, "blue_coat_r03"), NoCoat = FindByName(Idx, "blue_no_coat");
        const int Fuzz = FindByName(Idx, "velvet_fuzz_10"), Gold = FindByName(Idx, "gold");
        const int Film = FindByName(Idx, "soap_film_03"), Lamp = FindByName(Idx, "emitter_sphere");
        const int Mask = FindByName(Idx, "alpha_card"), Eon = FindByName(Idx, "eon_r1");
        Check(Coat >= 0 && NoCoat >= 0 && Fuzz >= 0 && Gold >= 0 && Film >= 0 && Lamp >= 0 && Mask >= 0 && Eon >= 0,
              "B-ball spot materials found by name");
        const std::vector<MaterialDescriptor>& Ds = Idx.QueryDescriptors();
        Check(Ds[static_cast<uint32_t>(Coat)].Slabs[0].CoatWeight == 1.0f &&
              Ds[static_cast<uint32_t>(Coat)].Slabs[0].CoatRoughness == 0.3f &&
              Ds[static_cast<uint32_t>(NoCoat)].Slabs[0].CoatWeight == 0.0f,
              "B-ball clearcoat round-trips; uncoated twin discriminates");
        Check(Ds[static_cast<uint32_t>(Fuzz)].Slabs[0].FuzzRoughness == 0.8f &&
              Ds[static_cast<uint32_t>(Fuzz)].Slabs[0].FuzzWeight > 0.9f,
              "B-ball sheen round-trips (rough exact, weight TIER-2)");
        Check(Ds[static_cast<uint32_t>(Gold)].Slabs[0].BaseMetalness == 1.0f &&
              Ds[static_cast<uint32_t>(Gold)].Slabs[0].BaseColor[0] == 1.0f,
              "B-ball metal F0 + metallic exact");
        Check(Ds[static_cast<uint32_t>(Film)].Slabs[0].ThinFilmWeight == 1.0f &&
              std::fabs(Ds[static_cast<uint32_t>(Film)].Slabs[0].ThinFilmThickness - 0.3f) / 0.3f < 1e-5f,
              "B-ball film weight exact, thickness TIER-2 (nm)");
        Check(std::fabs(Ds[static_cast<uint32_t>(Lamp)].Slabs[0].EmissionLuminance - 8.0f) / 8.0f < 1e-5f,
              "B-ball emission TIER-2 (config fold)");
        Check((Ds[static_cast<uint32_t>(Mask)].Flags & MaterialFlagAlphaMask) != 0u &&
              Ds[static_cast<uint32_t>(Mask)].AlphaCutoff == 0.5f,
              "B-ball mask flag + cutoff exact");
        Check(Ds[static_cast<uint32_t>(Eon)].Slabs[0].BaseDiffuseRoughness == 1.0f, "B-ball EON extras exact");
        uint32_t Trans = 0u, CoatCt = 0u, Cloth = 0u, Aniso = 0u, Sss = 0u, EmOnly = 0u, Unlit = 0u;
        for (const MaterialDescriptor& D : Ds)
        {
            uint32_t Folded = 0u;
            const std::vector<MaterialSlabDescriptor> Flat = MaterialIndex::Flatten(D, 1u, &Folded, nullptr);
            static const MaterialSlabDescriptor kDef{};
            const MaterialSlabDescriptor& S = Flat.empty() ? kDef : Flat.front();
            switch (MaterialIndex::DeriveReflectance(D, S))
            {
                case MaterialReflectance::Transmissive: ++Trans; break;
                case MaterialReflectance::ClearCoated:  ++CoatCt; break;
                case MaterialReflectance::Cloth:        ++Cloth; break;
                case MaterialReflectance::Anisotropic:  ++Aniso; break;
                case MaterialReflectance::Subsurface:   ++Sss; break;
                case MaterialReflectance::EmissiveOnly: ++EmOnly; break;
                case MaterialReflectance::Unlit:        ++Unlit; break;
                default: break;
            }
        }
        Check(Trans == 0u && CoatCt == 2u && Cloth == 2u && Aniso == 0u && Sss == 0u && EmOnly == 0u && Unlit == 0u,
              "B-ball selection census (coat twins + cloth pair live; fuzz+spec/emitter=Standard)");
    }

    // Fallbacks: last slot of every scene, Standard, untextured.
    for (uint32_t Ci = 0u; Ci < 4u; ++Ci)
    {
        if (Ci == 3u && !HasSponza) continue;
        const std::vector<MaterialDescriptor>& Ds = Cases[Ci].Level->AccessMaterials().QueryDescriptors();
        uint32_t Folded = 0u;
        const std::vector<MaterialSlabDescriptor> Flat = MaterialIndex::Flatten(Ds.back(), 1u, &Folded, nullptr);
        char Label[128];
        std::snprintf(Label, sizeof(Label), "B-%s fallback Standard", Cases[Ci].Tag);
        Check(MaterialIndex::DeriveReflectance(Ds.back(), Flat.front()) == MaterialReflectance::Standard, Label);
    }

    // ── C. Fold probes (synthetic multi-slab — the cases Tier B would serve) ────────────────────────────────────
    {
        MaterialIndex Probes;
        MaterialDescriptor Chain; Chain.Name = "fold-glass-3";   // 3-slab transmission chain (M7a glass-01 shape)
        for (float W : { 0.9f, 0.5f, 0.3f }) { MaterialSlabDescriptor T; T.TransmissionWeight = W; Chain.Slabs.push_back(T); }
        Probes.Register(Chain);
        MaterialDescriptor Stack; Stack.Name = "coat-stack-2";   // coat slab over a red base slab
        { MaterialSlabDescriptor Top; Top.CoatWeight = 1.0f; Stack.Slabs.push_back(Top); }
        { MaterialSlabDescriptor Bottom; Bottom.BaseColor[0] = 0.9f; Bottom.BaseColor[1] = 0.05f; Bottom.BaseColor[2] = 0.05f; Stack.Slabs.push_back(Bottom); }
        Probes.Register(Stack);
        MaterialDescriptor Mix; Mix.Name = "mix-red-blue";       // HorizontalMix pair, factor 0.5
        { MaterialSlabDescriptor R; R.BaseColor[0] = 1.0f; R.BaseColor[1] = 0.0f; R.BaseColor[2] = 0.0f; Mix.Slabs.push_back(R); }
        { MaterialSlabDescriptor B; B.BaseColor[0] = 0.0f; B.BaseColor[1] = 0.0f; B.BaseColor[2] = 1.0f; Mix.Slabs.push_back(B); }
        { MaterialOperation Op; Op.Category = MaterialOperationCategory::HorizontalMix; Op.Left = 0u; Op.Right = 1u; Op.Weight = 0.5f; Mix.Operations.push_back(Op); }
        Probes.Register(Mix);

        std::vector<std::string> R1, R2, R8;
        Probes.Finalise(1u, &R1);
        Check(R1.size() == 3u, "C1 limit 1 folds all three probes (verticals + the evaluated mix)");
        bool ChainLine = false, StackLine = false, MixLine = false;
        uint32_t StrategyTags = 0u;
        for (const std::string& L : R1)
        {
            if (Contains(L.c_str(), "material 'fold-glass-3':") && Contains(L.c_str(), "3 slab(s) folded into 1")) ChainLine = true;
            if (Contains(L.c_str(), "material 'coat-stack-2':") && Contains(L.c_str(), "2 slab(s) folded into 1")) StackLine = true;
            if (Contains(L.c_str(), "material 'mix-red-blue':") && Contains(L.c_str(), "2 slab(s) folded into 1")) MixLine = true;
            if (Contains(L.c_str(), "albedo scaling")) ++StrategyTags;
            std::printf("[scene] fold@1: %s\n", L.c_str());
        }
        Check(ChainLine && StackLine && MixLine && StrategyTags == 3u, "C2 fold lines attributed + worded + strategy-tagged");
        const std::vector<MaterialDescriptor>& Ds = Probes.QueryDescriptors();
        uint32_t Folded = 0u;
        const MaterialSlabDescriptor ResolvedChain = MaterialIndex::Flatten(Ds[0], 1u, &Folded, nullptr).front();
        Check(ResolvedChain.TransmissionWeight == 0.3f,
              "C3 bottom-up identity: bottom transmission survives exactly, tops' dropped (no carry-down)");
        const MaterialSlabDescriptor ResolvedStack = MaterialIndex::Flatten(Ds[1], 1u, &Folded, nullptr).front();
        Check(ResolvedStack.CoatWeight == 1.0f, "C4 coat carried down exactly");
        const MaterialSlabDescriptor ResolvedMix = MaterialIndex::Flatten(Ds[2], 1u, &Folded, nullptr).front();
        Check(ResolvedMix.BaseColor[0] == 0.5f && ResolvedMix.BaseColor[1] == 0.0f && ResolvedMix.BaseColor[2] == 0.5f,
              "C5 mix evaluates to purple (exact float)");
        // C9: albedo-scaling closed form — the lower base is multiplied by Keep = 1 - TopAlbedo(top) (OpenPBR §3.10
        // rule, MaterialIndex.cpp TopAlbedo): coat Fresnel F0(1.6) then spec Fresnel F0(1.5) on the default top slab
        // (fuzz/metal terms zero). Double-precision shadow; the float chain agrees to ~1e-8 and the 1e-6 budget
        // absorbs toolchain FMA contraction.
        const double F0Coat = 0.6 * 0.6 / (2.6 * 2.6);
        const double F0Spec = 0.5 * 0.5 / (2.5 * 2.5);
        const double Taken = F0Coat + (1.0 - F0Coat) * F0Spec;
        const double Keep = 1.0 - Taken;
        Check(Keep < 1.0 && Keep > 0.5 &&
              std::fabs(ResolvedStack.BaseColor[0] - 0.9 * Keep) / (0.9 * Keep) < 1e-6 &&
              std::fabs(ResolvedStack.BaseColor[1] - 0.05 * Keep) / (0.05 * Keep) < 1e-6 &&
              std::fabs(ResolvedStack.BaseColor[2] - 0.05 * Keep) / (0.05 * Keep) < 1e-6,
              "C9 albedo scaling: lower base x (1 - TopAlbedo(top)) (rel-1e-6)");
        std::printf("[scene] loss@1 fold-glass-3: resolved trans %.3g = bottom 0.3 (tops 0.9/0.5 absorbed, no carry-down)\n",
                    static_cast<double>(ResolvedChain.TransmissionWeight));
        std::printf("[scene] loss@1 coat-stack-2: coat %.3g carried down + base (%.3g, %.3g, %.3g) = red x %.6g keep (top took %.4g)\n",
                    static_cast<double>(ResolvedStack.CoatWeight), static_cast<double>(ResolvedStack.BaseColor[0]),
                    static_cast<double>(ResolvedStack.BaseColor[1]), static_cast<double>(ResolvedStack.BaseColor[2]),
                    Keep, Taken);
        Probes.Finalise(2u, &R2);
        Check(R2.size() == 1u && Contains(R2[0].c_str(), "fold-glass-3") && Contains(R2[0].c_str(), "into 2"),
              "C6 limit 2 folds only the 3-chain");
        Probes.Finalise(8u, &R8);
        Check(R8.empty() && Probes.QueryRecords()[0].SlabCount == 3u && Probes.QueryRecords()[1].SlabCount == 2u,
              "C7 limit 8: nothing folds, residents 3/2");
        Check(MaterialIndex::Flatten(Ds[0], 8u, &Folded, nullptr).front() == Ds[0].Slabs[0],
              "C8 no-fold identity: flatten without folding returns the slabs verbatim");
    }

    // ── D. Preview sweep: every material of every scene shades clean ────────────────────────────────────────────
    {
        uint32_t Serial_D = 0u;
        for (uint32_t Ci = 0u; Ci < 4u; ++Ci)
        {
            if (Ci == 3u && !HasSponza) continue;
            const std::vector<MaterialDescriptor>& Ds = Cases[Ci].Level->AccessMaterials().QueryDescriptors();
            for (uint32_t I = 0u; I < Ds.size(); ++I)
            {
                const MaterialDescriptor& D = Ds[I];
                uint32_t Folded = 0u;
                const std::vector<MaterialSlabDescriptor> Flat = MaterialIndex::Flatten(D, 1u, &Folded, nullptr);
                static const MaterialSlabDescriptor kDef{};
                const MaterialSlabDescriptor& S = Flat.empty() ? kDef : Flat.front();
                ShaderballPreviewRequest Req;
                Req.Material = &D; Req.Selection = MaterialIndex::DeriveReflectance(D, S);
                Req.Size = 64; Req.Spp = 2;
                char Path[128];
                std::snprintf(Path, sizeof(Path), "/tmp/MaterialScenes_D%u.png", Serial_D);
                Req.OutPath = Path;
                ShaderballPreviewResult Res;
                char Label[160];
                std::snprintf(Label, sizeof(Label), "D%u %s/%s shades clean", Serial_D, Cases[Ci].Tag, D.Name.c_str());
                const bool Ok = RenderShaderballPreview(Req, Res);
                Check(Ok && Res.Bad == 0 && Res.Mean > 1e-4 && Res.Tris > 15000, Label);
                std::remove(Path);
                ++Serial_D;
            }
        }
        // Determinism spot-check on clear glass (solid traversal — the heaviest path in the sweep).
        const std::vector<MaterialDescriptor>& Ds = Glass.QueryMaterials().QueryDescriptors();
        uint32_t Folded = 0u;
        const std::vector<MaterialSlabDescriptor> Flat = MaterialIndex::Flatten(Ds[0], 1u, &Folded, nullptr);
        ShaderballPreviewRequest Req;
        Req.Material = &Ds[0]; Req.Selection = MaterialIndex::DeriveReflectance(Ds[0], Flat.front());
        Req.Size = 64; Req.Spp = 2; Req.OutPath = "/tmp/MaterialScenes_DET1.png";
        ShaderballPreviewResult R1, R2;
        Check(RenderShaderballPreview(Req, R1), "D-det re-render (1/2)");
        Req.OutPath = "/tmp/MaterialScenes_DET2.png";
        Check(RenderShaderballPreview(Req, R2), "D-det re-render (2/2)");
        std::vector<unsigned char> A, B;
        Check(ReadFile("/tmp/MaterialScenes_DET1.png", A) && ReadFile("/tmp/MaterialScenes_DET2.png", B) &&
              A.size() == B.size() && std::memcmp(A.data(), B.data(), A.size()) == 0, "D-det byte-identical");
        std::remove("/tmp/MaterialScenes_DET1.png"); std::remove("/tmp/MaterialScenes_DET2.png");
    }

    // ── E. Consumption matrix (the M3 table the perf budget rests on — tripwire) ────────────────────────────────
    {
        // Verbatim from MaterialFurnaceProof.cpp ProofConsumesTable (bit C = channel C).
        const uint32_t kExpect[8] = {
            (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4) | (1u << 14),
            (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4) | (1u << 13) | (1u << 14),
            (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4) | (1u << 5) | (1u << 10) | (1u << 14),
            (1u << 0) | (1u << 2) | (1u << 4) | (1u << 7) | (1u << 11) | (1u << 14),
            (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4) | (1u << 9) | (1u << 14),
            (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4) | (1u << 8) | (1u << 14),
            0u,
            0u,
        };
        bool All = true;
        for (uint32_t s = 0u; s < 8u; ++s)
            for (uint32_t c = 0u; c < 16u; ++c)
            {
                const bool Want = ((kExpect[s] >> c) & 1u) != 0u;
                const bool Got = ReflectanceConsumes(s, c);
                if (Want != Got) { std::printf("[scene] CONSUMES DRIFT sel=%u ch=%u want=%d got=%d\n", s, c, (int)Want, (int)Got); All = false; }
            }
        Check(All, "E1 consumption matrix 128/128 unchanged since M3");
        for (uint32_t s = 0u; s < 8u; ++s)
        {
            uint32_t Fetches = 0u;
            for (uint32_t c = 0u; c < 16u; ++c) Fetches += ReflectanceConsumes(s, c) ? 1u : 0u;
            std::printf("[scene] gated fetches %-12s %u (+3 unconditional base/emission/opacity)\n", SelName(static_cast<MaterialReflectance>(s)), Fetches);
        }
    }

    // ── F. Tier verdict ─────────────────────────────────────────────────────────────────────────────────────────
    {
        Check(AuthoredTotal == MaterialsTotal, "F1 every real material is single-slab (the Tier datum)");
        std::printf("[scene] TIER VERDICT: keep Tier A + fold — 0 multi-slab materials in %u real materials%s\n",
                    MaterialsTotal, HasSponza ? " (Sponza included)" : " (Sponza absent)");
    }

    std::remove("/tmp/MaterialScenes_ShaderBall.gltf");
    std::remove("/tmp/MaterialScenes_ShaderBall.bin");

    if (Failed == 0) std::printf("MATERIAL SCENES: PASS (%d/%d)\n", Passed, Passed + Failed);
    else std::printf("MATERIAL SCENES: FAIL (%d passed, %d failed)\n", Passed, Failed);
    return Failed == 0 ? 0 : 1;
}
