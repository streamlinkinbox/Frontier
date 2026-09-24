//============================================================================================================================================
//                                                      SHOWCASESTRUCTURE.H
//============================================================================================================================================
// 🧩 The default level (`--scene showcase`): a grid of spheres on a ground plane, one material per sphere across the
//    whole OpenPBR lobe set — anisotropic metal, IOR-driven glass, subsurface, coat, fuzz/cloth, thin film, haziness,
//    EON diffuse, emission — with scattered secondary shapes between the rows, and real area luminaires overhead.
//
//    Why it replaces the old field. The previous showcase came from RayTracingSolver::ConstructShowcaseScene, whose
//    materials are the five-field analytical struct (albedo, emissive, roughness, metalness). Everything it exports
//    goes through ReSTIRIntegrator::BuildMaterialDescriptors, which pins SpecularWeight = 0 to hold the Cornell
//    reference image — so every object was a Lambertian blob with no specular, no transmission, no coat, no anything.
//    It also contained NO emissive triangle, which is why the renderer reported "0 luminaires": with an empty emitter
//    set the shadow stage refuses to place taps (PlaceShadowTaps returns false) and the ReSTIR light loop has nothing
//    to sample, so the level rendered with neither shadows nor indirect light no matter what was toggled.
//
//    This structure authors MaterialDescriptor/MaterialSlabDescriptor directly — the same path the shader-ball level
//    uses — so the full slab reaches the kernel, and it appends emissive quads so the light set is non-empty.
//
//    World space is RH Z-up, metres. Built once, exported through SceneCodec::Encode, then imported like any level.

#pragma once

#include "MaterialDescriptor.h"
#include "../DeviceExchange/SwapchainExchange.h"
#include <cstdint>
#include <string>
#include <vector>

namespace Frontier {

// Revision of the authored level. Bump whenever Construct() changes what the level contains, so an already-exported
//    Showcase.gltf from an older revision is regenerated instead of being reused forever by the export-once rule.
inline constexpr uint32_t kShowcaseRevision = 6u;   // r6: 20 x 20, five finite-flake clearcoat families

// The r6 grid: 20 families x 20 variations = 400 spheres and 400 materials.
//    Published so the structure, the CPU reference harness and any proof agree on the layout without restating it.
inline constexpr uint32_t kShowcaseGridSide = 20u;

// ── The interface panel's berth ─────────────────────────────────────────────────────────────────────────────────────
// The showcase carries Project-Zero's spatial-interface panel as a physical exhibit: a stand and a housing slab are
//    authored here (so every consumer of the level — GPU build, CPU reference, exporter — agrees the panel exists and
//    occludes), and the panel's FACE plane is published so the project can seat its figures and its light proxy on
//    exactly the surface the geometry presents. The face looks along −Y (toward the default viewpoint), upright.
inline constexpr float kShowcasePanelCentreX = 2.6f;    // [m]
inline constexpr float kShowcasePanelCentreY = -3.60f;  // [m]  the FACE plane (5 mm proud of the housing slab)
inline constexpr float kShowcasePanelCentreZ = 1.18f;   // [m]
inline constexpr float kShowcasePanelScale   = 3.0f;    // [-]  interface-local metres → world metres

// True when the file at Path was written by this revision of ShowcaseStructure. A missing, unreadable or older file
//    answers false, and the caller re-exports. Cheap: it only scans the glTF header region for the revision marker.
[[nodiscard]] bool ShowcaseIsCurrent(const std::string& Path) noexcept;

class ShowcaseStructure
{
public:
    // Fills the world-space soup. Triangles carry UVs; CornerNormals holds 3 smooth normals per triangle. The
    //    emissive quads are appended last (the luminaire-last convention the other levels share).
    void Construct() noexcept;

    // Writes Showcase.gltf at Path. Error receives the codec message.
    [[nodiscard]] bool Export(const std::string& Path, std::string* Error) const noexcept;

    [[nodiscard]] const std::vector<TriangleIndex>&      QueryTriangles()     const noexcept { return Triangles; }
    [[nodiscard]] const std::vector<Vector3>&            QueryCornerNormals() const noexcept { return CornerNormals; }
    [[nodiscard]] const std::vector<MaterialDescriptor>& QueryMaterials()     const noexcept { return Materials; }
    [[nodiscard]] const std::vector<TriangleSpanRecord>& QuerySpans()         const noexcept { return Spans; }

    struct SpanScope
    {
        std::vector<TriangleSpanRecord>*  Spans     = nullptr;
        const std::vector<TriangleIndex>* Triangles = nullptr;
        uint32_t                          Span      = 0u;
        ~SpanScope() noexcept;
    };
    [[nodiscard]] SpanScope OpenSpan(const char* Name, bool Dynamic = false) noexcept;

private:
    void AppendSphere(const Vector3& Centre, float Radius, uint32_t Material, uint32_t Rings, uint32_t Segments) noexcept;
    void AppendQuad(const Vector3& A, const Vector3& B, const Vector3& C, const Vector3& D, uint32_t Material, float UvScale) noexcept;
    void AppendBox(const Vector3& Centre, const Vector3& HalfExtent, float RotationRadians, uint32_t Material) noexcept;
    void AppendCylinder(const Vector3& Base, float Radius, float Height, uint32_t Material, uint32_t Segments) noexcept;
    void AppendCone(const Vector3& Base, float Radius, float Height, uint32_t Material, uint32_t Segments) noexcept;
    void AppendTriangle(const Vector3 P[3], const Vector3 N[3], const float Uv[3][2], uint32_t Material) noexcept;

    std::vector<TriangleIndex>      Triangles;
    std::vector<Vector3>            CornerNormals;
    std::vector<MaterialDescriptor> Materials;
    std::vector<TriangleSpanRecord> Spans;
};

} // namespace Frontier
