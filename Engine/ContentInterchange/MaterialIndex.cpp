//============================================================================================================================================
//                                                       MATERIALINDEX.CPP
//============================================================================================================================================
// 🧩 Slab-graph flatten (RestirPhaseR4 plan §3) and GPU record construction.

#include "MaterialIndex.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace Frontier {

namespace {

// Every float of a slab, in declaration order, so lerp / scale can run over the whole parameter set at once.
constexpr size_t kSlabFloatCount = offsetof(MaterialSlabDescriptor, GeometryThinWalled) / sizeof(float);
static_assert(kSlabFloatCount == 58u, "MaterialSlabDescriptor float prefix changed — revisit Flatten");

float* SlabFloats(MaterialSlabDescriptor& S)             noexcept { return reinterpret_cast<float*>(&S); }
const float* SlabFloats(const MaterialSlabDescriptor& S) noexcept { return reinterpret_cast<const float*>(&S); }

float Luminance(const float C[3]) noexcept { return 0.2126f * C[0] + 0.7152f * C[1] + 0.0722f * C[2]; }

// Directional albedo at normal incidence of a slab seen as a "top" layer — what OpenPBR §3.10 uses to darken the
//    layer underneath when the stack is collapsed: coat Fresnel + fuzz coverage + metal/dielectric specular.
float TopAlbedo(const MaterialSlabDescriptor& S) noexcept
{
    const auto F0 = [](float Ior) { const float R = (Ior - 1.0f) / (Ior + 1.0f); return R * R; };
    float E = 0.0f;
    if (S.CoatWeight > 0.0f)     E += S.CoatWeight * F0(S.CoatIor);
    if (S.FuzzWeight > 0.0f)     E += (1.0f - E) * S.FuzzWeight * Luminance(S.FuzzColor);
    if (S.BaseMetalness > 0.0f)  E += (1.0f - E) * S.BaseWeight * S.BaseMetalness * Luminance(S.BaseColor);
    if (S.SpecularWeight > 0.0f) E += (1.0f - E) * S.SpecularWeight * (1.0f - S.BaseMetalness) * F0(S.SpecularIor) * Luminance(S.SpecularColor);
    return std::clamp(E, 0.0f, 1.0f);
}

MaterialSlabDescriptor Lerp(const MaterialSlabDescriptor& A, const MaterialSlabDescriptor& B, float T) noexcept
{
    MaterialSlabDescriptor Out = T >= 0.5f ? B : A;          // textures / bools follow the dominant side
    float* O = SlabFloats(Out); const float* FA = SlabFloats(A); const float* FB = SlabFloats(B);
    for (size_t I = 0; I < kSlabFloatCount; ++I) O[I] = FA[I] + (FB[I] - FA[I]) * T;
    return Out;
}

// Fold `Top` into `Bottom` (albedo scaling): the bottom keeps its identity, loses the light the top took.
MaterialSlabDescriptor FoldVertical(const MaterialSlabDescriptor& Top, const MaterialSlabDescriptor& Bottom) noexcept
{
    MaterialSlabDescriptor Out = Bottom;
    const float Keep = 1.0f - TopAlbedo(Top);
    for (float& C : Out.BaseColor)      C *= Keep;
    for (float& C : Out.SpecularColor)  C *= Keep;
    Out.EmissionLuminance *= Keep;
    // The top's own lobes are carried down where the record has a home for them.
    if (Top.CoatWeight > 0.0f && Out.CoatWeight == 0.0f)
    {
        Out.CoatWeight = Top.CoatWeight; Out.CoatIor = Top.CoatIor; Out.CoatRoughness = Top.CoatRoughness;
        Out.CoatRoughnessAnisotropy = Top.CoatRoughnessAnisotropy; Out.CoatDarkening = Top.CoatDarkening;
        std::memcpy(Out.CoatColor, Top.CoatColor, sizeof(Out.CoatColor));
    }
    if (Top.FuzzWeight > 0.0f && Out.FuzzWeight == 0.0f)
    {
        Out.FuzzWeight = Top.FuzzWeight; Out.FuzzRoughness = Top.FuzzRoughness;
        std::memcpy(Out.FuzzColor, Top.FuzzColor, sizeof(Out.FuzzColor));
    }
    if (Top.ThinFilmWeight > 0.0f && Out.ThinFilmWeight == 0.0f)
    {
        Out.ThinFilmWeight = Top.ThinFilmWeight; Out.ThinFilmThickness = Top.ThinFilmThickness; Out.ThinFilmIor = Top.ThinFilmIor;
    }
    if (Top.SlateGlintDensity > 0.0f && Out.SlateGlintDensity == 0.0f)
    {
        Out.SlateGlintDensity = Top.SlateGlintDensity; Out.SlateGlintUvScale = Top.SlateGlintUvScale;
    }
    if (Top.EmissionLuminance > 0.0f)
    {
        // Emission adds; keep the brighter colour.
        const float Sum = Out.EmissionLuminance + Top.EmissionLuminance;
        if (Top.EmissionLuminance > Out.EmissionLuminance) std::memcpy(Out.EmissionColor, Top.EmissionColor, sizeof(Out.EmissionColor));
        Out.EmissionLuminance = Sum;
    }
    for (uint32_t C = 0u; C < kMaterialTextureChannelCount; ++C)
        if (!Out.Textures[C].IsBound() && Top.Textures[C].IsBound()) Out.Textures[C] = Top.Textures[C];
    return Out;
}

// A stack: top-first list of slabs plus the mix weight / mask of the slab against the one below it (HorizontalMix
//    kept as two slabs). Operations evaluate to stacks; the flatten then trims to the limit.
struct Stack
{
    std::vector<MaterialSlabDescriptor> Slabs;      // top first
    std::vector<float>                  MixWeight;  // per slab: constant HorizontalMix factor vs. the next slab (1 = vertical)
    std::vector<TextureReference>       MixMask;
};

Stack Single(const MaterialSlabDescriptor& S) { Stack St; St.Slabs.push_back(S); St.MixWeight.push_back(1.0f); St.MixMask.emplace_back(); return St; }

void Append(Stack& Into, const Stack& Below)
{
    Into.Slabs.insert(Into.Slabs.end(), Below.Slabs.begin(), Below.Slabs.end());
    Into.MixWeight.insert(Into.MixWeight.end(), Below.MixWeight.begin(), Below.MixWeight.end());
    Into.MixMask.insert(Into.MixMask.end(), Below.MixMask.begin(), Below.MixMask.end());
}

// Collapse a whole stack to one slab (bottom-up albedo scaling; horizontal pairs lerp).
MaterialSlabDescriptor Collapse(const Stack& St)
{
    if (St.Slabs.empty()) return MaterialSlabDescriptor{};
    MaterialSlabDescriptor Acc = St.Slabs.back();
    for (size_t I = St.Slabs.size() - 1u; I-- > 0u;)
    {
        const float W = St.MixWeight[I];
        Acc = W >= 1.0f ? FoldVertical(St.Slabs[I], Acc) : Lerp(Acc, St.Slabs[I], W);
    }
    return Acc;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         FLATTEN
//------------------------------------------------------------------------------------------------------------------------

std::vector<MaterialSlabDescriptor> MaterialIndex::Flatten(const MaterialDescriptor& D, uint32_t Limit, uint32_t* Folded, std::vector<std::string>* Report) noexcept
{
    Limit = std::clamp(Limit, 1u, kMaterialSlabCeiling);
    if (Folded) *Folded = 0u;
    if (D.Slabs.empty()) return { MaterialSlabDescriptor{} };

    // ① Evaluate the operation list to one stack.
    Stack Result;
    if (D.Operations.empty())
    {
        for (const MaterialSlabDescriptor& S : D.Slabs) Append(Result, Single(S));   // implicit vertical chain
    }
    else
    {
        std::vector<Stack> Results;
        const auto Operand = [&](uint32_t Ref) -> Stack
        {
            if (Ref & kMaterialOperandResultBit) { const uint32_t I = Ref & ~kMaterialOperandResultBit; return I < Results.size() ? Results[I] : Stack{}; }
            return Ref < D.Slabs.size() ? Single(D.Slabs[Ref]) : Stack{};
        };
        for (const MaterialOperation& Op : D.Operations)
        {
            Stack L = Operand(Op.Left);
            switch (Op.Category)
            {
            case MaterialOperationCategory::VerticalLayer: { Stack R = Operand(Op.Right); Append(L, R); break; }
            case MaterialOperationCategory::HorizontalMix:
            {
                Stack R = Operand(Op.Right);
                if (L.Slabs.empty()) { L = R; break; }
                if (R.Slabs.empty()) break;
                // Horizontal mix of two stacks: collapse each side to one slab first (a mix of stacks is a 2-slab
                //    result at most), keep both when a mask texture is bound or the weight is fractional.
                const MaterialSlabDescriptor A = Collapse(L), B = Collapse(R);
                Stack M;
                if (Op.Mask.IsBound() || (Op.Weight > 0.0f && Op.Weight < 1.0f))
                {
                    M = Single(A); M.MixWeight[0] = Op.Mask.IsBound() ? 0.5f : Op.Weight; M.MixMask[0] = Op.Mask; Append(M, Single(B));
                }
                else M = Single(Op.Weight >= 1.0f ? A : B);
                L = M; break;
            }
            case MaterialOperationCategory::Weight:
                for (MaterialSlabDescriptor& S : L.Slabs) S.BaseWeight *= Op.Weight;
                break;
            case MaterialOperationCategory::Coverage:
                for (MaterialSlabDescriptor& S : L.Slabs) S.GeometryOpacity *= Op.Weight;
                break;
            }
            Results.push_back(std::move(L));
        }
        if (!Results.empty()) Result = std::move(Results.back());
    }
    if (Result.Slabs.empty()) return { MaterialSlabDescriptor{} };

    // ② Trim to the limit: fold the top-most slabs down until it fits (the bottom slab is the material's identity).
    //    Fractional horizontal pairs beyond the limit lerp (mask textures are dropped — reported).
    const uint32_t Before = static_cast<uint32_t>(Result.Slabs.size());
    while (Result.Slabs.size() > Limit)
    {
        const float W = Result.MixWeight[0];
        MaterialSlabDescriptor Merged = W >= 1.0f ? FoldVertical(Result.Slabs[0], Result.Slabs[1]) : Lerp(Result.Slabs[1], Result.Slabs[0], W);
        Result.Slabs[1] = Merged;
        Result.Slabs.erase(Result.Slabs.begin());
        Result.MixWeight.erase(Result.MixWeight.begin());
        Result.MixMask.erase(Result.MixMask.begin());
    }
    const uint32_t After = static_cast<uint32_t>(Result.Slabs.size());
    if (Folded) *Folded = Before - After;
    if (Report && Before != After)
    {
        char Line[256];
        std::snprintf(Line, sizeof(Line), "material '%s': %u slab(s) folded into %u (slab_limit %u, albedo scaling)", D.Name.c_str(), Before, After, Limit);
        Report->emplace_back(Line);
    }

    // Surviving horizontal pairs carry their weight / mask in the slab record (MixWeight / MaskTexture).
    for (size_t I = 0; I < Result.Slabs.size(); ++I)
        if (Result.MixWeight[I] < 1.0f) Result.Slabs[I].Texture(MaterialTextureChannel::Mask) = Result.MixMask[I];
    return Result.Slabs;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      RECORD CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

MaterialSlabRecord MaterialIndex::ConstructSlabRecord(const MaterialSlabDescriptor& S) noexcept
{
    MaterialSlabRecord R{};
    std::memcpy(&R, SlabFloats(S), kSlabFloatCount * sizeof(float));   // identical prefix order by construction
    static_assert(offsetof(MaterialSlabRecord, SlateGlintUvScale) == (kSlabFloatCount - 1u) * sizeof(float), "record prefix must mirror the descriptor");
    R.SlabFlags = S.GeometryThinWalled ? 1u : 0u;
    for (uint32_t& Slot : R.TextureSlots) Slot = 0xFFFFFFFFu;
    for (uint32_t C = 0u; C < kMaterialTextureChannelCount; ++C)
    {
        const TextureReference& T = S.Textures[C];
        const uint32_t Slot = T.IsBound() ? std::min(T.Texture, 0xFFFEu) : 0xFFFFu;
        R.TextureSlots[C / 2u] = (R.TextureSlots[C / 2u] & ~(0xFFFFu << ((C & 1u) * 16u))) | (Slot << ((C & 1u) * 16u));
        R.TextureUvSets |= static_cast<uint32_t>(T.UvSet & 3u) << (C * 2u);
    }
    const TextureReference& Mask = S.Texture(MaterialTextureChannel::Mask);
    R.MaskTexturePacked   = (Mask.IsBound() ? std::min(Mask.Texture, 0xFFFEu) : 0xFFFFu) | (static_cast<uint32_t>(Mask.UvSet & 3u) << 16u);
    R.NormalScale         = S.Texture(MaterialTextureChannel::GeometryNormal).Scalar;
    R.OcclusionStrength   = S.Texture(MaterialTextureChannel::Occlusion).Scalar;
    R.MixWeight           = 1.0f;
    return R;
}

uint32_t MaterialIndex::ClassifyComplexity(const std::vector<MaterialSlabDescriptor>& Slabs) noexcept
{
    if (Slabs.size() > 1u) return MaterialComplexityComplex;
    const MaterialSlabDescriptor& S = Slabs.empty() ? MaterialSlabDescriptor{} : Slabs.front();
    const bool Special = S.TransmissionWeight > 0.0f || S.SubsurfaceWeight > 0.0f || S.SlateGlintDensity > 0.0f || S.ThinFilmWeight > 0.0f;
    if (Special) return MaterialComplexitySpecial;
    const bool Extra = S.CoatWeight > 0.0f || S.FuzzWeight > 0.0f || S.SlateHazinessWeight > 0.0f || S.SpecularRoughnessAnisotropy > 0.0f;
    return Extra ? MaterialComplexitySingle : MaterialComplexitySimple;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       TABLE LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

uint32_t MaterialIndex::Register(const MaterialDescriptor& Descriptor) noexcept
{
    Descriptors.push_back(Descriptor);
    if (Descriptors.back().Slabs.empty()) Descriptors.back().Slabs.emplace_back();
    return static_cast<uint32_t>(Descriptors.size() - 1u);
}

void MaterialIndex::Finalise(uint32_t SlabLimit, std::vector<std::string>* Report) noexcept
{
    SlabLimit = std::clamp(SlabLimit, 1u, kMaterialSlabCeiling);
    Records.clear(); SlabRecords.clear();
    Metrics = MaterialIndexMetrics{};
    Metrics.SlabLimit       = SlabLimit;
    Metrics.DescriptorCount = static_cast<uint32_t>(Descriptors.size());

    for (const MaterialDescriptor& D : Descriptors)
    {
        uint32_t Folded = 0u;
        const std::vector<MaterialSlabDescriptor> Slabs = Flatten(D, SlabLimit, &Folded, Report);
        Metrics.FoldedCount += Folded;

        MaterialRecord R{};
        R.SlabOffset = static_cast<uint32_t>(SlabRecords.size());
        R.SlabCount  = static_cast<uint32_t>(Slabs.size());
        for (const MaterialSlabDescriptor& S : Slabs) SlabRecords.push_back(ConstructSlabRecord(S));

        // Header = the bottom-most slab (the material's identity); for single-slab glTF materials this is exactly the
        //    R2/R3 RadianceStructure content.
        const MaterialSlabDescriptor& Bottom = Slabs.back();
        R.AlbedoR   = Bottom.BaseColor[0]; R.AlbedoG = Bottom.BaseColor[1]; R.AlbedoB = Bottom.BaseColor[2];
        R.Roughness = Bottom.SpecularRoughness;
        R.Metalness = Bottom.BaseMetalness;
        float Emission[3] = { 0.0f, 0.0f, 0.0f };
        for (const MaterialSlabDescriptor& S : Slabs)
            for (int C = 0; C < 3; ++C) Emission[C] += S.EmissionLuminance * S.EmissionColor[C];
        R.EmissiveR = Emission[0]; R.EmissiveG = Emission[1]; R.EmissiveB = Emission[2];
        R.Flags      = D.Flags & ~MaterialFlagEmissive;
        if (Emission[0] + Emission[1] + Emission[2] > 0.0f) R.Flags |= MaterialFlagEmissive;
        if (Bottom.GeometryThinWalled) R.Flags |= MaterialFlagThinWalled;
        R.Complexity = ClassifyComplexity(Slabs);
        Metrics.ComplexityCount[R.Complexity & 3u] += 1u;   // R6 row 3: complexity histogram for the F3 popup
        R.BaseColourTexture = Bottom.Texture(MaterialTextureChannel::BaseColor).Texture;
        R.NormalTexture     = Bottom.Texture(MaterialTextureChannel::GeometryNormal).Texture;
        R.AlphaCutoff       = D.AlphaCutoff;
        R.NormalScale       = Bottom.Texture(MaterialTextureChannel::GeometryNormal).Scalar;
        Records.push_back(R);
    }
    Metrics.SlabCount = static_cast<uint32_t>(SlabRecords.size());
}

void MaterialIndex::Clear() noexcept
{
    Descriptors.clear(); Records.clear(); SlabRecords.clear(); Metrics = MaterialIndexMetrics{};
}

} // namespace Frontier
