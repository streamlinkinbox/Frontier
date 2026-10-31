/*==========================================================================================================================================
                                                              BAKEFIELD.H
==========================================================================================================================================*/
// 🧩 Flattened bake inputs and per-texel bake field shared by the Vulkan compute bake (Engine/Render/BakeRasterization)
//    and its CPU reference evaluation: one flat triangle/extent layout, one per-texel field, one map encoder.

#pragma once
#include "Engine/ContentInterchange/BakeMesh.h"
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                        FLAT GPU LAYOUT
//------------------------------------------------------------------------------------------------------------------------

// 📝 Every slot below is built from 16-byte rows so the C++ and Slang (std430) layouts agree byte-for-byte with
// no hidden padding; the static_asserts under each struct are the ABI contract Engine/Render/Shader/BakeFieldEvaluation.slang
// mirrors field-for-field.

// 📝 One high-mesh triangle in flat traversal order. A[3] carries the source part index as a float (exact for
// any part count under 2^24) so the id map needs no second buffer; the remaining [3] lanes are padding.
struct BakeTriangleSlot
{
    float A[4];       // [m] - First vertex position; [3] = high part index
    float B[4];       // [m] - Second vertex position
    float C[4];       // [m] - Third vertex position
    float NormalA[4]; // [-] - First vertex normal
    float NormalB[4]; // [-] - Second vertex normal
    float NormalC[4]; // [-] - Third vertex normal
};
static_assert(sizeof(BakeTriangleSlot) == 96, "BakeTriangleSlot must stay a 96-byte std430 row set.");

// 📝 One flat BVH extent. A leaf (Index[2] == 1) references triangles [Index[0], Index[0] + Index[1]) directly
// in the triangle slot array (triangles are emitted in traversal order, so no indirection table is needed);
// an interior extent references its two sub-extents Index[0] and Index[1]. Extent 0 is the root.
struct BakeExtentSlot
{
    float    Lower[4]; // [m] - Axis-aligned minimum corner
    float    Upper[4]; // [m] - Axis-aligned maximum corner
    uint32_t Index[4]; // [idx] - First / second / leaf marker (see above)
};
static_assert(sizeof(BakeExtentSlot) == 48, "BakeExtentSlot must stay a 48-byte std430 row set.");

// 📝 The low surface under one texel, rasterized from the target part's UV layout on the CPU
// (ConstructBakeFieldRequest). WorldNormal[3] is 1 when the texel is covered by the low surface, else 0;
// uncovered texels carry the legacy defaults (position at the origin, normal +Z) so every downstream
// fallback in the encoder reproduces RunMeshBake's uncovered-texel pixels exactly.
struct BakeTexelSurface
{
    float WorldPosition[4]; // [m] - Interpolated low-surface position after the part transform
    float WorldNormal[4];   // [-] - Interpolated low-surface normal; [3] = covered marker
};
static_assert(sizeof(BakeTexelSurface) == 32, "BakeTexelSurface must stay a 32-byte std430 row set.");

// 📝 The evaluated per-texel bake field: everything the BVH queries produce, before map encoding. Written by
// BakeFieldCompute (GPU) or EvaluateBakeField (CPU reference); consumed only by EncodeBakeResults.
//    HitPosition[3] = primary-ray hit marker (1/0); HitNormal[3] = hit part index as a float;
//    Bent[3] = hemisphere visibility; Bevel[3] = bevel mask;
//    Scalars = (secondary visibility, signed curvature, thickness distance, thickness hit marker).
struct BakeTexelField
{
    float HitPosition[4]; // [m] - Primary-ray hit position (meaningful only when the hit marker is 1)
    float HitNormal[4];   // [-] - Primary-ray hit normal (interpolated)
    float Bent[4];        // [-] - Bent normal (mean unblocked hemisphere direction)
    float Bevel[4];       // [-] - Bevel-blended normal
    float Scalars[4];     // [-] - Secondary visibility / signed curvature / thickness distance / marker
};
static_assert(sizeof(BakeTexelField) == 80, "BakeTexelField must stay an 80-byte std430 row set.");

// 📝 Push-constant block for BakeFieldCompute, filled from BakeSettings by ConstructBakeFieldRequest so the
// GPU dispatch and the CPU reference gate their hemisphere/curvature/thickness/bevel work identically.
// TexelCount is the whole target's texel count; a banded GPU dispatch narrows it per band (see BakeRasterization).
struct BakeFieldUniforms
{
    uint32_t TexelCount;          // [idx] - Texels this dispatch evaluates, starting at surface slot 0 of its band
    uint32_t Samples;              // [-] - Hemisphere sample count before the max(1) clamp both paths apply
    uint32_t BevelSamples;         // [-] - Bevel neighbour count before the max(1) clamp both paths apply
    uint32_t NeedMask;             // [-] - BakeFieldNeed bit set
    float    Front;                // [m] - Projection envelope in front of the low surface
    float    Back;                 // [m] - Projection envelope behind the low surface
    float    AoDistance;           // [m] - Occlusion ray length
    float    SecondaryAoDistance;  // [m] - Secondary occlusion ray length
    float    CurvatureRadius;      // [m] - Tangent-plane probe radius
    float    BevelRadius;          // [m] - Bevel normal-averaging radius
    float    BevelStrength;        // [0-1] - Bevel blend weight
    float    Epsilon;              // [m] - Self-intersection bias scaled from the high bounds diagonal
};
static_assert(sizeof(BakeFieldUniforms) == 48, "BakeFieldUniforms must stay a 48-byte push-constant block.");

// 📝 Which query groups the requested channels need; derived once in ConstructBakeFieldRequest with the same
// gating RunMeshBake applies, so neither path evaluates samples no requested map consumes.
enum BakeFieldNeed : uint32_t
{
    BakeFieldNeedHemisphere = 1u << 0, // occlusion, bent normals, dust, dirt, edge wear
    BakeFieldNeedSecondary  = 1u << 1, // occlusion-secondary (widens the shared hemisphere ray length)
    BakeFieldNeedCurvature  = 1u << 2, // curvature, convexity, cavity, dirt, edge wear
    BakeFieldNeedThickness  = 1u << 3, // thickness
    BakeFieldNeedBevel      = 1u << 4, // bevel-normal, bevel-mask
};

// 📝 The field path rasterizes UV coverage triangle-by-triangle instead of scanning every low triangle per
// texel, so it admits larger bakes than RunMeshBake's 1024 clamp. Bakes above 1024 therefore only hold on
// this path; RunMeshBake keeps clamping (see BakingWorkspacePanel's device choice).
constexpr uint32_t BakeFieldMaximumResolution = 4096; // [px] - Widest square bake the field path accepts

//------------------------------------------------------------------------------------------------------------------------
//                                                        REQUEST ASSEMBLY
//------------------------------------------------------------------------------------------------------------------------

// 📝 Wireframe's texture-space edge distance is a pure low-mesh quantity the encoder derives from the winning
// triangle's barycentric weights and UVs; retained only when the wireframe channel is requested.
struct BakeWireframeTexel
{
    float Weights[3];   // [-] - Winning triangle barycentric weights
    float WinningUv[6]; // [-] - Winning triangle UV corners (A, B, C)
};

// 📝 Everything EncodeBakeResults needs that never reaches the GPU: coverage retention (0 = uncovered, else
// chart id + 1, since a covered texel may legitimately sit on chart 0), the part transform for object-space
// normals, and the high bounds behind the position map. Per-texel world position/normal are read back from
// the target's BakeTexelSurface slots rather than duplicated here.
struct BakeEncodeContext
{
    std::string                 Name;
    uint32_t                    Resolution = 0; // [px] - Square bake edge
    uint32_t                    Charts = 0;     // [-] - Highest chart id on the target part
    std::array<float, 16>       ObjectToWorld{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::vector<uint32_t>       ChartIds;  // Resolution * Resolution entries
    std::vector<BakeWireframeTexel> Wireframe; // Empty unless the wireframe channel is requested
    float                       BoundsMinimum[3]{0, 0, 0}; // [m] - High bounds minimum
    float                       Diagonal[3]{0, 0, 0};      // [m] - High bounds extent per axis
};

struct BakeFieldTarget
{
    std::vector<BakeTexelSurface> Surfaces; // Resolution * Resolution, row-major, top-left origin
    BakeEncodeContext             Encode;
};

struct BakeFieldTriangles
{
    std::vector<BakeTriangleSlot> Triangles; // Emitted in BVH traversal order (leaves reference them directly)
    std::vector<BakeExtentSlot>   Extents;   // Extent 0 is the root
    float                         BoundsMinimum[3]{0, 0, 0}; // [m]
    float                         Diagonal[3]{0, 0, 0};      // [m]
    float                         Epsilon = 0;               // [m] - max(diagonal length * 1e-5, 1e-7), as RunMeshBake
};

struct BakeFieldRequest
{
    BakeFieldTriangles            Triangles;
    std::vector<BakeFieldTarget>  Targets;
    BakeFieldUniforms             Uniforms{};
    uint32_t                      Resolution = 0; // [px] - Square field tile edge
    uint32_t                      FullResolution = 0; // [px] - Full output edge
    uint32_t                      OffsetX = 0, OffsetY = 0; // [px] - Tile origin in full output
};

// 📝 Builds the complete flat request for one bake: the high-mesh BVH flattened, each target's UV coverage
// rasterized, uniforms and need mask derived from Settings. Mirrors RunMeshBake's inputs exactly, including
// its quirks (high parts flattened at their raw positions, low world normals left untransformed), so the
// field path reproduces the legacy maps rather than quietly correcting them.
[[nodiscard]] BakeFieldRequest ConstructBakeFieldRequest(const std::vector<const BakePart *> &Targets, const BakeMeshSet &High,
    const BakeSettings &Settings);

// Construct a bounded square tile at full-output coordinates. Pixels beyond the output edge are uncovered.
[[nodiscard]] BakeFieldRequest ConstructBakeFieldTileRequest(const std::vector<const BakePart *> &Targets, const BakeMeshSet &High,
    const BakeSettings &Settings, uint32_t TileResolution, uint32_t OffsetX, uint32_t OffsetY);


//------------------------------------------------------------------------------------------------------------------------
//                                                        FIELD EVALUATION + ENCODING
//------------------------------------------------------------------------------------------------------------------------

// 📝 CPU reference evaluation of one target's field over the flat layout — the same iterative traversal,
// sampling sequence and tolerances BakeFieldCompute runs on the GPU, kept as the executable specification
// the shader is validated against (and as the honest fallback arithmetic for exhibits).
// ⚠️ Fields is resized to the target's texel count; the optional progress report fires once per row.
void EvaluateBakeField(const BakeFieldRequest &Request, size_t TargetIndex, std::vector<BakeTexelField> &Fields,
    const std::function<void(const BakeProgress &)> &OnProgress = {});

// 📝 Encodes every requested (and implemented) channel for every target from evaluated fields, applying the
// same per-map rules, fallbacks and statistics as RunMeshBake. One encoder serves GPU fields and CPU
// reference fields alike, so the two routes cannot drift apart in map formatting.
[[nodiscard]] std::vector<BakePartResult> EncodeBakeResults(const BakeFieldRequest &Request,
    const std::vector<std::vector<BakeTexelField>> &Fields, const BakeSettings &Settings);

// 📝 Whole CPU reference bake (construct, evaluate, encode) with RunMeshBake's signature shape; the panel's
// shipping CPU route stays RunMeshBake, this route exists to validate the flat layout against it.
[[nodiscard]] std::vector<BakePartResult> RunBakeFieldEvaluation(const std::vector<const BakePart *> &Targets, const BakeMeshSet &High,
    const BakeSettings &Settings, const std::function<void(const BakeProgress &)> &OnProgress = {});

// Disk-backed, bounded-memory field baking for large square images (8 through 16384 pixels).
[[nodiscard]] std::vector<BakePartResult> RunTiledMeshBake(const std::vector<const BakePart *> &Targets,
    const BakeMeshSet &High, const BakeSettings &Settings, const std::function<void(const BakeProgress &)> &OnProgress = {});
// Stream either an in-memory or disk-backed map to a full-resolution RGBA PNG.
void WriteBakeMapPng(const BakeMapImage &Map, uint32_t Resolution, const std::filesystem::path &Path);

// 📝 Non-blocking GPU bake report the panel polls each frame (see BakingWorkspacePanel::PollGpuBake): progress
// while bands are in flight, then either the evaluated fields for every target or the failure that sends the
// panel back to its CPU route. Defined here so neither the panel nor its host needs Vulkan types to exchange it.
struct BakeGpuOutcome
{
    bool        Complete = false;
    bool        Failed = false;
    std::string Failure;
    uint64_t    DoneTexels = 0;  // [px] - Texels harvested across all targets so far
    uint64_t    TotalTexels = 0; // [px] - Texels across all targets
    std::vector<std::vector<BakeTexelField>> Fields; // One field array per request target, once Complete
};
} // namespace Frontier
