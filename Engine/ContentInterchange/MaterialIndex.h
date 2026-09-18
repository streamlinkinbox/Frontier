//============================================================================================================================================
//                                                        MATERIALINDEX.H
//============================================================================================================================================
// 🧩 Resident material table addressed by material id (CLAUDE.md role 15). Owns the GPU record layout — MaterialRecord
//    (64 B header, one per material) and MaterialSlabRecord (256 B, SlabCount per material) — and the flatten step
//    that reduces a MaterialDescriptor's slab graph to at most `SlabLimit` slabs (RestirPhaseR4 plan §3).
//
// Layout mirrors: Shaders/SceneRecords.slang (MaterialRecord, MaterialSlabRecord). Compute bindings 2 and 10.

#pragma once

#include "MaterialDescriptor.h"
#include <cstdint>
#include <string>
#include <vector>

namespace Frontier {

static constexpr uint32_t kMaterialSlabCeiling = 8u;   // hard ceiling of the GPU layout; [render] slab_limit ≤ this

//------------------------------------------------------------------------------------------------------------------------
//                                                     GPU RECORDS
//------------------------------------------------------------------------------------------------------------------------

enum MaterialComplexityClass : uint32_t { MaterialComplexitySimple = 0u, MaterialComplexitySingle = 1u, MaterialComplexityComplex = 2u, MaterialComplexitySpecial = 3u };

//------------------------------------------------------------------------------------------------------------------------
//                                               REFLECTANCE SELECTION (M1)
//------------------------------------------------------------------------------------------------------------------------
// Sultan-42 §5 / Sultan-18 §3's eight shading selections: which of the twenty channels the kernel samples for a material.
// DERIVED per material at Finalise (DeriveReflectance, from the resolved slab's exact-zero weights — never per texel),
// packed into MaterialRecord.Flags bits 8–11. Cost is still ComplexityClass's job; selection is *which channels*.
// Unread channels are retained on the descriptor (Finalise never mutates Descriptors) and un-sampled by the kernel.

enum class MaterialReflectance : uint32_t
{
    Standard     = 0u,   // Sultan-18 §3 channels 1–8 (+ extensions by weight)
    Anisotropic  = 1u,   // Standard + anisotropy direction (M2)
    ClearCoated  = 2u,   // Standard + coat weight/roughness + coat orientation (M2)
    Cloth        = 3u,   // fuzz-dominant, no specular (M3: Charlie + sheen-primary)
    Subsurface   = 4u,   // Standard + subsurface colour/thickness (M5)
    Transmissive = 5u,   // Standard + transmission + refractive IOR (M4)
    EmissiveOnly = 6u,   // emission + opacity only; provably nothing reflective
    Unlit        = 7u,   // base colour is the radiance (KHR_materials_unlit)
    Count        = 8u
};

static constexpr uint32_t kMaterialReflectanceShift = 8u;
static constexpr uint32_t kMaterialReflectanceMask  = 0xF00u;

struct MaterialRecord                       // 64 B — header, Tier A fast path
{
    float    AlbedoR, AlbedoG, AlbedoB;     // [0..1] flattened base colour (linear Rec.709)
    float    Roughness;                     // [0..1] flattened specular roughness
    float    EmissiveR, EmissiveG, EmissiveB; // [nit] emission_luminance × emission_color
    float    Metalness;                     // [0..1]
    uint32_t SlabOffset;                    // [idx] first MaterialSlabRecord
    uint32_t SlabCount;                     // [cnt] 1 … slab limit
    uint32_t Flags;                         // [bit] MaterialFlag
    uint32_t Complexity;                    // [-]   MaterialComplexityClass
    uint32_t BaseColourTexture;             // [idx] TextureIndex slot, kMaterialTextureNone = none
    uint32_t NormalTexture;                 // [idx]
    float    AlphaCutoff;                   // [-]
    float    NormalScale;                   // [-]
};
static_assert(sizeof(MaterialRecord) == 64u, "MaterialRecord must be 64 bytes (std430 mirror)");

struct MaterialSlabRecord                   // 304 B = 19 vec4 — float prefix mirrors MaterialSlabDescriptor exactly (memcpy)
{
    // OpenPBR §5 in spec order (58 floats — same order and count as MaterialSlabDescriptor's float prefix)
    float BaseWeight, BaseColorR, BaseColorG, BaseColorB, BaseMetalness, BaseDiffuseRoughness;
    float SpecularWeight, SpecularColorR, SpecularColorG, SpecularColorB, SpecularRoughness, SpecularRoughnessAnisotropy, SpecularIor;
    float TransmissionWeight, TransmissionColorR, TransmissionColorG, TransmissionColorB, TransmissionDepth;
    float TransmissionScatterR, TransmissionScatterG, TransmissionScatterB, TransmissionScatterAnisotropy, TransmissionDispersionScale, TransmissionDispersionAbbeNumber;
    float SubsurfaceWeight, SubsurfaceColorR, SubsurfaceColorG, SubsurfaceColorB, SubsurfaceRadius;
    float SubsurfaceRadiusScaleR, SubsurfaceRadiusScaleG, SubsurfaceRadiusScaleB, SubsurfaceScatterAnisotropy;
    float CoatWeight, CoatColorR, CoatColorG, CoatColorB, CoatRoughness, CoatRoughnessAnisotropy, CoatIor, CoatDarkening;
    float FuzzWeight, FuzzColorR, FuzzColorG, FuzzColorB, FuzzRoughness;
    float EmissionLuminance, EmissionColorR, EmissionColorG, EmissionColorB;
    float ThinFilmWeight, ThinFilmThickness, ThinFilmIor;
    float GeometryOpacity;
    float SlateHazinessWeight, SlateHazinessRoughness, SlateGlintDensity, SlateGlintUvScale;
    // Runtime block
    uint32_t SlabFlags;                     // bit0 thin-walled
    uint32_t MaskTexturePacked;             // HorizontalMix mask slot (uint16) | UvSet << 16
    uint32_t TextureSlots[8];               // 16 × uint16 — MaterialTextureChannel order, 0xFFFF = none
    uint32_t TextureUvSets;                 // 16 × 2 bits (uv set 0-3)
    float    NormalScale, OcclusionStrength;
    float    MixWeight;                     // constant HorizontalMix factor against the slab below (1 = vertical layer)
    // M2 extension vec4 (appended — every earlier offset is unchanged, so pre-M2 readers keep working):
    float    AnisotropyRotation;            // [rad] added to the anisotropy direction angle (SlateAnisotropyRotation)
    float    DirectF0Weight;                // [-]   M6 reserved, always 0 until then (0 = IOR-derived F0)
    float    Reserved0, Reserved1;          // zero
};
static_assert(sizeof(MaterialSlabRecord) == 304u, "MaterialSlabRecord must be 304 bytes = 19 vec4 (std430 mirror)");

//------------------------------------------------------------------------------------------------------------------------
//                                                     MATERIAL INDEX
//------------------------------------------------------------------------------------------------------------------------

struct MaterialIndexMetrics
{
    uint32_t DescriptorCount = 0u;
    uint32_t SlabCount       = 0u;          // resident slabs after flatten
    uint32_t FoldedCount     = 0u;          // slabs folded away by the limit
    uint32_t SlabLimit       = 1u;
    uint32_t ComplexityCount[4]{};          // R6 row 3: per-class histogram (Simple / Single / Complex / Special)
};

class MaterialIndex
{
public:
    MaterialIndex() noexcept = default;
    ~MaterialIndex() noexcept = default;
    MaterialIndex(const MaterialIndex&) = delete;
    MaterialIndex& operator=(const MaterialIndex&) = delete;

    // Register one descriptor; returns its material id (the index the scene's InstanceRecord.MaterialIndex refers to).
    uint32_t Register(const MaterialDescriptor& Descriptor) noexcept;

    // Flatten every descriptor to ≤ SlabLimit slabs and build the GPU records. Fold notes are retained (one line per
    //    folded material) and copied to `Report` when provided. Idempotent: may be called again with a different limit.
    void     Finalise(uint32_t SlabLimit, std::vector<std::string>* Report = nullptr) noexcept;
    void     Clear() noexcept;

    [[nodiscard]] const std::vector<MaterialDescriptor>& QueryDescriptors() const noexcept { return Descriptors; }

    // M7b: mutable access for the material inspector's Apply (commits drafts into Slabs[0]/AlphaCutoff; the caller
    //    re-runs Finalise after). nullptr when Id is out of range. Finalise never mutates Descriptors, so the commit
    //    (descriptor write) and the derive (Finalise) stay two explicit steps.
    [[nodiscard]] MaterialDescriptor* AccessDescriptor(uint32_t Id) noexcept { return Id < Descriptors.size() ? &Descriptors[Id] : nullptr; }
    [[nodiscard]] std::vector<MaterialDescriptor>&       ModifyDescriptors()      noexcept { return Descriptors; }
    [[nodiscard]] const std::vector<MaterialRecord>&     QueryRecords()     const noexcept { return Records; }
    [[nodiscard]] const std::vector<MaterialSlabRecord>& QuerySlabRecords() const noexcept { return SlabRecords; }
    [[nodiscard]] const MaterialIndexMetrics&            QueryMetrics()     const noexcept { return Metrics; }
    [[nodiscard]] const std::vector<std::string>&        QueryFoldReport()  const noexcept { return FoldReport; }
    [[nodiscard]] uint32_t                               QueryCount()       const noexcept { return static_cast<uint32_t>(Descriptors.size()); }

    // Flatten one descriptor to ≤ Limit slabs (top first). Public so harnesses can inspect the fold; `Folded` counts
    //    slabs removed. Implements plan §3. `MixWeights` (M1, optional) receives the per-slab HorizontalMix factor
    //    against the slab below (1 = vertical layer) so surviving fractional pairs keep their factor in the record.
    [[nodiscard]] static std::vector<MaterialSlabDescriptor> Flatten(const MaterialDescriptor& Descriptor, uint32_t Limit, uint32_t* Folded,
                                                                     std::vector<std::string>* Report, std::vector<float>* MixWeights = nullptr) noexcept;

    [[nodiscard]] static MaterialSlabRecord ConstructSlabRecord(const MaterialSlabDescriptor& Slab, float MixWeight = 1.0f) noexcept;
    [[nodiscard]] static uint32_t           ClassifyComplexity(const std::vector<MaterialSlabDescriptor>& Slabs) noexcept;

    // Derive the reflectance selection from the RESOLVED slab (Slabs.front — the one the Tier A kernel samples).
    // Exact-zero weight comparisons, so gating (skip iff weight == 0 and unconsumed) can never skip a contribution.
    [[nodiscard]] static MaterialReflectance DeriveReflectance(const MaterialDescriptor& Descriptor, const MaterialSlabDescriptor& Resolved) noexcept;

private:
    std::vector<MaterialDescriptor> Descriptors;
    std::vector<MaterialRecord>     Records;
    std::vector<MaterialSlabRecord> SlabRecords;
    MaterialIndexMetrics            Metrics;
    std::vector<std::string>        FoldReport;   // M7a: last Finalise's fold notes, retained for the inspector
};

} // namespace Frontier
