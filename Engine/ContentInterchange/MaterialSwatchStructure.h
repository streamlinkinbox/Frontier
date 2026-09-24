//============================================================================================================================================
//                                                     MATERIALSWATCHSTRUCTURE.H
//============================================================================================================================================
// 🧩 The material library level (`--scene materials`): the curated index of the channel set M1–M8 proved one lobe at a
//    time. Forty-two swatch spheres in a 7 × 6 grid on a matte studio floor against a neutral backdrop, one material
//    per sphere, plus three sign panels on the backdrop for the channels that are not sphere-shaped (alpha cutout,
//    unlit, emissive-only), one ceiling key and one side fill.
//
//    Each swatch is its OWN MaterialDescriptor — no two share a descriptor, no two share a value set — so the outliner
//    and the material inspector give every one its own row, and a render reads like a lookdev sheet: plastics and
//    ceramics, clear-coated paints and varnishes, metals (F82 tints, brushed anisotropy), the glass family
//    (clear / frosted / tinted / crystal / ice / thin-walled acrylic / water), the subsurface family (bone, jade, wax,
//    marble, skin, milk, resin), and the specials (velvet and felt cloth, satin, soap film, hazy acrylic, emerald,
//    liquid mercury). `Exhibits/Workbench/Materials/MaterialSwatchProof.cpp` pins that census, the grid geometry and
//    the channel coverage as executable code — the level is content, and content without a gate drifts.
//
//    Nothing here is new shading: the level is built once in world space (RH Z-up, metres) and exported through
//    SceneCodec::Encode to `Projects/Project-Zero/Content/Scenes/Materials.gltf`, after which the renderer only ever
//    sees the file — the same export-once-then-import discipline the Cornell box, the shader ball and the showroom
//    follow. Constants only (no texture files are committed with the level), so the channels that are texture-shaped
//    — normal, coat normal, occlusion, and channel 20 displacement — are carried by imported content (Sponza) rather
//    than by the library; the proof names those four as acknowledged gaps instead of pretending to cover them.

#pragma once

#include "MaterialDescriptor.h"
#include "../DeviceExchange/SwapchainExchange.h"
#include <string>
#include <vector>

namespace Frontier {

class MaterialSwatchStructure
{
public:
    // Grid shape and swatch size. Column index runs +X (left to right from the default camera), row index runs +Y
    //    (near to far), so ordinal N sits at X = (N % Columns) pitch and Y = FirstRow + (N / Columns) pitch.
    static constexpr uint32_t kSwatchColumns   = 7u;
    static constexpr uint32_t kSwatchRows      = 6u;
    static constexpr uint32_t kSwatchCount     = kSwatchColumns * kSwatchRows;   // 42
    static constexpr float    kSwatchRadius    = 0.40f;                          // [m]
    static constexpr float    kSwatchPitch     = 1.10f;                          // [m] centre to centre, both axes
    static constexpr float    kSwatchFirstRow  = 0.60f;                          // [m] centre Y of row 0 (nearest)

    // Material ordinals — the order Construct pushes them, and the order the glTF file carries.
    static constexpr uint32_t kFloorMaterial      = 0u;
    static constexpr uint32_t kBackdropMaterial   = 1u;
    static constexpr uint32_t kFirstSwatchMaterial = 2u;                         // swatch N is kFirstSwatchMaterial + N
    static constexpr uint32_t kPanelCutoutMaterial = kFirstSwatchMaterial + kSwatchCount + 0u;
    static constexpr uint32_t kPanelUnlitMaterial  = kFirstSwatchMaterial + kSwatchCount + 1u;
    static constexpr uint32_t kPanelEmissionMaterial = kFirstSwatchMaterial + kSwatchCount + 2u;
    static constexpr uint32_t kKeyLuminaireMaterial  = kFirstSwatchMaterial + kSwatchCount + 3u;
    static constexpr uint32_t kFillLuminaireMaterial = kFirstSwatchMaterial + kSwatchCount + 4u;
    static constexpr uint32_t kMaterialCount         = kFillLuminaireMaterial + 1u;   // 49 authored

    // Fills the world-space soup. Triangles carry UVs; CornerNormals holds three smooth normals per triangle (the
    //    flat TriangleIndex has no room for them). The emissive quads are appended last — the luminaire convention the
    //    Cornell box and the shader ball share.
    void Construct() noexcept;

    // Writes Materials.gltf at Path (SceneCodec::Encode with smooth normals and texcoords). Error receives the codec
    //    message.
    [[nodiscard]] bool Export(const std::string& Path, std::string* Error) const noexcept;

    [[nodiscard]] const std::vector<TriangleIndex>&      QueryTriangles()     const noexcept { return Triangles; }
    [[nodiscard]] const std::vector<Vector3>&            QueryCornerNormals() const noexcept { return CornerNormals; }
    [[nodiscard]] const std::vector<MaterialDescriptor>& QueryMaterials()     const noexcept { return Materials; }

    // Swatch geometry: where ordinal N stands, which material slot it owns, and its authored name (the glTF material
    //    name, so the inspector and the census print agree).
    [[nodiscard]] static Vector3 QuerySwatchOrigin(uint32_t Ordinal) noexcept;
    [[nodiscard]] static uint32_t QuerySwatchMaterial(uint32_t Ordinal) noexcept { return kFirstSwatchMaterial + Ordinal; }
    [[nodiscard]] static const char* QuerySwatchName(uint32_t Ordinal) noexcept;

    // Object spans: one record per scene object over Triangles, in append order. The scope closes itself when it dies,
    //    so a span covers exactly the Appends in its block — hold one per object in Construct.
    struct SpanScope
    {
        std::vector<TriangleSpanRecord>*     Spans     = nullptr;
        const std::vector<TriangleIndex>*    Triangles = nullptr;
        uint32_t                             Span      = 0u;
        ~SpanScope() noexcept;
    };
    [[nodiscard]] SpanScope                              OpenSpan(const char* Name, bool Dynamic = false) noexcept;
    [[nodiscard]] const std::vector<TriangleSpanRecord>& QuerySpans() const noexcept { return Spans; }

private:
    void AppendSphere(const Vector3& Centre, float Radius, uint32_t Material, uint32_t Rings, uint32_t Segments) noexcept;
    void AppendQuad(const Vector3& A, const Vector3& B, const Vector3& C, const Vector3& D, uint32_t Material, float UvScale) noexcept;
    void AppendTriangle(const Vector3 P[3], const Vector3 N[3], const float Uv[3][2], uint32_t Material) noexcept;

    std::vector<TriangleIndex>      Triangles;
    std::vector<Vector3>            CornerNormals;
    std::vector<MaterialDescriptor> Materials;
    std::vector<TriangleSpanRecord> Spans;
};

} // namespace Frontier
