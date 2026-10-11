//============================================================================================================================================
// 📦 Projects/Project-Drive/Source/DriveSceneAuthor.cpp — builds the `--scene drive` level: course, ControlVehicle, wheels
//============================================================================================================================================
#include "DriveSceneAuthor.h"
#include "DriveWheelMesh.h"
#include "../../../Engine/ContentInterchange/AutomotiveShowcasePresets.h"
#include "../../../Engine/ContentInterchange/SceneCodec.h"
#include "DriveCourse.h"
#include "ControlVehicleMesh.inl"
#include "ControlVehicleWheelMesh.inl"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace Frontier {
namespace Drive {

namespace {
constexpr float kPi = 3.14159265358979f;

// --- vehicle paint family -------------------------------------------------------------------------------------------
// ControlVehicle.blend authors MetalicCoat as a coated dielectric, because Blender's Principled BSDF has no flake
// lobe to author into.  Frontier ships five finite-flake automotive families (AutomotiveShowcasePresets.h), so the
// paint is promoted onto family 4, "Metallic" — the flake-over-pigment build, as opposed to Candy's transparent
// dye (1), RGB Glitter's sparse coarse flakes (2), or Iridescent Pearl's thin-film interference (3).
constexpr unsigned kVehiclePaintProfile = 4u;

// Family sweep t.  For the Metallic family this sets flake population (density 1 + 7t) and coat roughness.  Half
// way up the ladder gives a visibly metallic panel that still reads as paint rather than glitter.  This is the one
// value here that the .blend does not determine, so it is named rather than buried as a literal.
constexpr float kVehiclePaintSweep = 0.5f;

// Flake headroom.  A metallic basecoat sits slightly below its matching solid colour because the flake lobe adds
// the rest of the energy, so the authored albedo is scaled rather than written in raw.
//
// It is deliberately NOT normalised to the shipped Metallic Cobalt pigment (which peaks at 0.13).  Cobalt is dark
// because blue pigment IS dark; forcing a yellow basecoat down to a blue's luminance is not a physical rescale,
// it just makes yellow paint read black.  Pigment luminance follows hue, so only a modest headroom is applied.
constexpr float kVehiclePaintFlakeHeadroom = 0.78f;

Vector3 Sub(const Vector3& a, const Vector3& b) noexcept { return { a.x-b.x, a.y-b.y, a.z-b.z }; }
Vector3 Cross(const Vector3& a, const Vector3& b) noexcept
{ return { a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x }; }
Vector3 Normalize(const Vector3& v) noexcept
{ const float l = std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z); return l > 0.0f ? Vector3{ v.x/l, v.y/l, v.z/l } : Vector3{0,0,1}; }

// One OpenPBR slab wrapped in a named material.
MaterialDescriptor MakeMaterial(const char* Name, const MaterialSlabDescriptor& Slab) noexcept
{ MaterialDescriptor m; m.Name = Name; m.Slabs.push_back(Slab); return m; }

// The authored material families now arrive with the mesh.  ControlVehicleMesh.inl carries kTriangleFamily, a
// per-triangle CourseMaterial id recovered from the .blend's `material_index` attribute by ExtractVehicle.py.
//
// What used to sit here was ClassifyControlVehicleFace(): a hand-written guess that inferred paint / glazing /
// trim from face centroids against hardcoded bounding boxes, because the old flattened export had no slot
// stream to read.  It collapsed a seven-family body into three regions and rendered the shell one flat colour.
// It is deleted rather than kept as a fallback -- a fallback would quietly mask a regenerated header that had
// lost its family stream, which is precisely the failure that needs to be loud.
} // namespace

//------------------------------------------------------------------------------------------------------------------------ span bookkeeping (verbatim ShowcaseStructure pattern)
DriveSceneAuthor::SpanScope::~SpanScope() noexcept
{
    if (Spans == nullptr || Triangles == nullptr || Span >= Spans->size()) return;
    TriangleSpanRecord& S = (*Spans)[Span];
    const uint32_t Now = static_cast<uint32_t>(Triangles->size());
    S.TriangleCount = Now >= S.FirstTriangle ? Now - S.FirstTriangle : 0u;
}

DriveSceneAuthor::SpanScope DriveSceneAuthor::OpenSpan(const char* Name, bool Dynamic) noexcept
{
    TriangleSpanRecord S;
    S.FirstTriangle = static_cast<uint32_t>(Triangles.size());
    if (Name != nullptr) S.Name = Name;
    S.Dynamic = Dynamic;
    Spans.push_back(std::move(S));
    SpanScope Scope; Scope.Spans = &Spans; Scope.Triangles = &Triangles;
    Scope.Span = static_cast<uint32_t>(Spans.size()) - 1u;
    return Scope;
}

//------------------------------------------------------------------------------------------------------------------------ triangle emit
void DriveSceneAuthor::AppendTriangle(const Vector3 P[3], const Vector3 N[3], const float Uv[3][2], uint32_t Material) noexcept
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

void DriveSceneAuthor::AppendFace(const Vector3& A, const Vector3& B, const Vector3& C, uint32_t Material) noexcept
{
    const Vector3 N = Normalize(Cross(Sub(B, A), Sub(C, A)));
    const Vector3 P[3] = { A, B, C }; const Vector3 Ns[3] = { N, N, N };
    const float Uv[3][2] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f } };
    AppendTriangle(P, Ns, Uv, Material);
}

//------------------------------------------------------------------------------------------------------------------------ vehicle body (ControlVehicleMesh, shifted so local origin = centre of mass)
void DriveSceneAuthor::AppendVehicleBody(float ComHeight, uint32_t Material) noexcept
{
    (void)Material; // retained in the declaration for source compatibility; per-face family assignment is authoritative.
    using namespace Frontier::Drive::ControlVehicleMesh;
    auto Raw = [&](uint32_t i)
    { return Vector3{ kPositions[i*3+0], kPositions[i*3+1], kPositions[i*3+2] }; };
    // 🔴 Rebase model vertices onto the CoM, NOT onto ComHeight directly. The model origin sits 0.41715 m above
    //    the ground (axle socket +0.0914 less the 0.50855 tyre radius), so subtracting the ground-referenced CoM
    //    height buried the whole body by exactly that offset — the car looked dragged along the floor.
    Frontier::Vehicle::VehicleGeometry Geometry;
    Geometry.CoMHeight = ComHeight;
    const float Rebase = Geometry.CoMModelZ();
    auto Local = [&](const Vector3& P) { return Vector3{P.x, P.y, P.z - Rebase}; };
    for (uint32_t t = 0; t < kTriangleCount; ++t)
    {
        const Vector3 A = Raw(kTriangles[t*3+0]);
        const Vector3 B = Raw(kTriangles[t*3+1]);
        const Vector3 C = Raw(kTriangles[t*3+2]);
        AppendFace(Local(A), Local(B), Local(C), kTriangleFamily[t]);
    }
}

//------------------------------------------------------------------------------------------------------------------------ wheel (axle along +Y) — rest pose of the shared DriveWheelMesh surface
// Emits the SAME surface DriveSceneMirror draws per frame; the only difference is that this lattice is the
//    undeformed cylinder while the mirror's is XPBDSoftTyre's live node set. Rings/segments/spokes/materials all
//    come from DriveWheelMesh.h, so an exported wheel and a simulated wheel can never drift apart in topology.
void DriveSceneAuthor::AppendWheel(const Vector3& C, float Radius, float HalfWidth, uint32_t Segments, uint32_t Material) noexcept
{
    (void)Material;
    Frontier::Vehicle::VehicleGeometry geo;
    WheelTreadLattice lattice;
    lattice.RingCount    = kDriveWheelRingCount;
    lattice.SegmentCount = (Segments < 8u) ? 8u : Segments;
    lattice.RimRadius    = geo.TyreRimRadius;
    lattice.HalfWidth    = HalfWidth;
    lattice.SpokeCount   = kDriveWheelSpokeCount;

    std::vector<float> tread(static_cast<size_t>(lattice.RingCount) * lattice.SegmentCount * 3u, 0.0f);
    FillRestTreadLattice(tread.data(), lattice.RingCount, lattice.SegmentCount, Radius, HalfWidth);
    lattice.Tread = tread.data();

    EmitWheelSurface(lattice, MatTyre, MatHub, MatBrake,
        [&](const float A[3], const float B[3], const float D[3],
            const float NA[3], const float NB[3], const float ND[3], uint32_t Slot)
        {
            const Vector3 P[3] = { Vector3{C.x + A[0], C.y + A[1], C.z + A[2]},
                                   Vector3{C.x + B[0], C.y + B[1], C.z + B[2]},
                                   Vector3{C.x + D[0], C.y + D[1], C.z + D[2]} };
            const Vector3 N[3] = { Vector3{NA[0], NA[1], NA[2]},
                                   Vector3{NB[0], NB[1], NB[2]},
                                   Vector3{ND[0], ND[1], ND[2]} };
            const float Uv[3][2] = { {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f} };
            AppendTriangle(P, N, Uv, Slot);
        });

    // ── the authored rim, mags and all ──────────────────────────────────────────────────────────────────────
    // EmitWheelSurface above draws the TREAD (the XPBD lattice, which must stay procedural because it
    // deforms) and used to draw the rim as concentric bands with kDriveWheelSpokeCount = 0 — that is, a flat
    // disc with no mags whatsoever.  The real RimFL mesh is right there in ControlVehicle.blend, so it is
    // emitted here instead: 1568 triangles carrying the authored black rim and the blue Material.024 mag
    // accent that is visible on the wheels in the viewport.
    {
        using namespace Frontier::Drive::ControlVehicleWheelMesh;
        // The mesh is authored hub-local at its own rim radius; scale it if the configured rim differs, so the
        // rim always sits inside the tread rather than poking through it.
        const float Scale = (kRimRadius > 1e-4f) ? (geo.TyreRimRadius / kRimRadius) : 1.0f;
        auto At = [&](uint32_t i)
        {
            return Vector3{ C.x + kPositions[i*3+0] * Scale,
                            C.y + kPositions[i*3+1] * Scale,
                            C.z + kPositions[i*3+2] * Scale };
        };
        for (uint32_t t = 0; t < kTriangleCount; ++t)
            AppendFace(At(kTriangles[t*3+0]), At(kTriangles[t*3+1]), At(kTriangles[t*3+2]),
                       kTriangleFamily[t]);
    }
}

//------------------------------------------------------------------------------------------------------------------------ static course (world space; reuses the shared DriveCourse geometry)
void DriveSceneAuthor::AppendCourse() noexcept
{
    EmitCourseTriangles([&](float ax,float ay,float az, float bx,float by,float bz,
                            float cx,float cy,float cz, uint32_t m)
    { AppendFace(Vector3{ax,ay,az}, Vector3{bx,by,bz}, Vector3{cx,cy,cz}, m); });
}

//------------------------------------------------------------------------------------------------------------------------ materials (index-aligned with DriveCourse.h Materials enum)
void DriveSceneAuthor::AuthorMaterials() noexcept
{
    Materials.clear();
    auto Dielectric = [](float r, float g, float b, float rough) {
        MaterialSlabDescriptor s; s.BaseColor[0]=r; s.BaseColor[1]=g; s.BaseColor[2]=b;
        s.BaseMetalness=0.0f; s.SpecularRoughness=rough; s.SpecularIor=1.5f; return s; };

    Materials.push_back(MakeMaterial("Checker light", Dielectric(0.82f,0.82f,0.84f, 0.35f))); // 0
    Materials.push_back(MakeMaterial("Checker dark",  Dielectric(0.05f,0.05f,0.06f, 0.35f))); // 1
    Materials.push_back(MakeMaterial("Surround",      Dielectric(0.35f,0.36f,0.38f, 0.55f))); // 2
    Materials.push_back(MakeMaterial("Ramp",          Dielectric(0.42f,0.43f,0.45f, 0.40f))); // 3
    Materials.push_back(MakeMaterial("Speed bump",    Dielectric(0.90f,0.72f,0.08f, 0.45f))); // 4
    Materials.push_back(MakeMaterial("Cone",          Dielectric(0.95f,0.35f,0.05f, 0.40f))); // 5

    // ------------------------------------------------------------------------------------------------------------
    // Families 6..15 mirror the slots authored in ControlVehicle.blend.  Every colour below is the LINEAR Rec.709
    // Principled base colour read out of the .blend's node tree by ExtractVehicle.py — not a value invented here.
    // The legacy Material.r/g/b fields in the .blend are all 0.8 placeholders and must never be used.
    // ------------------------------------------------------------------------------------------------------------

    // 6 — MetalicCoat.  Authored as base (0.68, 0.80, 0.00) with Coat Weight 1.0 and Coat Roughness 0.03: a
    // clearcoat automotive paint.  Blender's Principled has no flake lobe, so the authored material can only
    // state "coated".  Frontier does have one, so the paint is promoted onto the engine's own finite-flake
    // family rather than being flattened to a coated Lambert.
    //
    // AuthorAutomotiveShowcase(profile 4, "Metallic") supplies the family: flake population, coat IOR/roughness
    // and the neutral specular tint, all the same OpenPBR fields the ReSTIR evaluator consumes.  The pigment is
    // then overridden with the authored hue, scaled only by the flake headroom above.
    MaterialSlabDescriptor paint;
    Frontier::AuthorAutomotiveShowcase(paint, kVehiclePaintProfile, kVehiclePaintSweep);
    {
        const float Authored[3] = { 0.680005f, 0.800458f, 0.000000f };   // MetalicCoat, linear Rec.709
        paint.BaseColor[0] = Authored[0] * kVehiclePaintFlakeHeadroom;
        paint.BaseColor[1] = Authored[1] * kVehiclePaintFlakeHeadroom;
        paint.BaseColor[2] = Authored[2] * kVehiclePaintFlakeHeadroom;
        // The .blend states Coat Roughness explicitly (0.03), so the authored value wins over the family sweep.
        paint.CoatRoughness = 0.03f;
        // AuthorAutomotiveShowcase leaves BaseMetalness at 0.8 for the cobalt/copper builds, where the pigment is
        // a tinted metal.  This paint is a coloured basecoat under flakes, so the pigment layer is dielectric and
        // the metallic character comes from the flake lobe.  Leaving 0.8 here tints the specular yellow and
        // darkens the diffuse, which is what made the panel read as dirty olive.
        paint.BaseMetalness = 0.0f;
    }
    Materials.push_back(MakeMaterial("MetalicCoat — finite-flake automotive clearcoat", paint));       // 6

    // 7 — Glass.  The .blend expresses the glazing as Alpha 0.087 over a black base, which is Blender's
    // rasteriser shorthand for "smoked".  Alpha blending is not glass: it has no refraction, no Fresnel and no
    // absorption with depth.  Use the same transmissive family Project-Zero's material level ships
    // (glass_soda_lime_clear / glass_amber_bottle): TransmissionWeight 1, SpecularIor 1.52, a low specular
    // roughness, and a Beer-Lambert tint whose depth reproduces the authored 8.7% transmittance.
    MaterialSlabDescriptor glass;
    glass.BaseColor[0] = 1.0f; glass.BaseColor[1] = 1.0f; glass.BaseColor[2] = 1.0f;
    glass.SpecularRoughness   = 0.02f;
    glass.SpecularIor         = 1.52f;
    glass.TransmissionWeight  = 1.0f;
    glass.TransmissionColor[0] = 0.26f; glass.TransmissionColor[1] = 0.28f; glass.TransmissionColor[2] = 0.30f;
    glass.TransmissionDepth   = 0.006f;                 // [m] automotive glazing thickness
    Materials.push_back(MakeMaterial("Glass — smoked transmissive cockpit glazing", glass));           // 7

    // 8 — Plastic.  Authored base (0, 0, 0), roughness 0.5: the black splitter, lower trim and grille.
    MaterialSlabDescriptor plastic = Dielectric(0.012f, 0.014f, 0.018f, 0.50f);
    plastic.CoatWeight = 0.0f;
    Materials.push_back(MakeMaterial("Plastic — lower trim, splitter and grille", plastic));           // 8

    // 9 — Rubber / StandardRubber.002.  Authored base (0.037, 0.037, 0.037), roughness 0.761.
    MaterialSlabDescriptor tyre = Dielectric(0.03741f, 0.03741f, 0.03741f, 0.76087f);
    tyre.CoatWeight = 0.0f;
    Materials.push_back(MakeMaterial("StandardRubber — tyre carcass and tread", tyre));                // 9

    // 10 — Material.024, the rim mag accent.  Authored as an emitter: colour (0.0, 0.277, 1.0) at strength 1.0.
    // That blue glow is visible on the wheels in the .blend viewport, so it is carried, not normalised away.
    // Authored emission colour (0.0, 0.277, 1.0) at strength 1.0.  Carried as a TINTED METAL with only a small
    // emissive lift rather than as a full emitter: emission is unshaded, so driving it at the authored 1.0 nit
    // painted the rim as a flat blue disc and erased all 1568 triangles of mag detail behind it.  Strength 1.0
    // is also Blender's default, so unlike FrontLight's 48.7 or RearLight's 6.5 it is weak evidence of intent —
    // the deliberate part of this material is the colour, which is what is kept.
    MaterialSlabDescriptor hub;
    hub.BaseColor[0]=0.06f; hub.BaseColor[1]=0.22f; hub.BaseColor[2]=0.62f;
    hub.BaseMetalness=1.0f; hub.SpecularRoughness=0.22f;
    hub.EmissionLuminance = 0.12f;
    hub.EmissionColor[0]=0.0f; hub.EmissionColor[1]=0.277313f; hub.EmissionColor[2]=1.0f;
    Materials.push_back(MakeMaterial("Material.024 — rim mag accent", hub));                           // 10

    // 11 — brake disc / caliper metal (no authored slot; a Drive-side addition, kept physically distinct).
    MaterialSlabDescriptor brake; brake.BaseColor[0]=0.30f; brake.BaseColor[1]=0.075f; brake.BaseColor[2]=0.025f;
    brake.BaseMetalness=0.82f; brake.SpecularRoughness=0.29f;
    Materials.push_back(MakeMaterial("Brake disc and caliper", brake));                                // 11

    // 12 — Plastic2.  Authored base (0.8, 0.8, 0.8): the light satin nose wedge, distinct from black Plastic.
    MaterialSlabDescriptor trim = Dielectric(0.8f, 0.8f, 0.8f, 0.50f);
    trim.CoatWeight = 0.0f;
    Materials.push_back(MakeMaterial("Plastic2 — satin nose and trim", trim));                         // 12

    // 13 — FrontLight.  Authored white emission at strength 48.7.
    MaterialSlabDescriptor headlight = Dielectric(0.8f, 0.8f, 0.8f, 0.10f);
    headlight.EmissionLuminance = 48.699997f;
    headlight.EmissionColor[0]=1.0f; headlight.EmissionColor[1]=1.0f; headlight.EmissionColor[2]=1.0f;
    Materials.push_back(MakeMaterial("FrontLight — headlamp emitter", headlight));                     // 13

    // 14 — RearLight.  Authored red emission (1.0, 0.0, 0.0164) at strength 6.5, with IOR 8.0.
    MaterialSlabDescriptor taillight = Dielectric(0.8f, 0.8f, 0.8f, 0.10f);
    taillight.EmissionLuminance = 6.5f;
    taillight.EmissionColor[0]=1.0f; taillight.EmissionColor[1]=0.0f; taillight.EmissionColor[2]=0.016442f;
    taillight.SpecularIor = 8.0f;
    Materials.push_back(MakeMaterial("RearLight — tail lamp emitter", taillight));                     // 14

    // 15 — Material.016 and any face the .blend leaves unassigned.  PROTO-X.002 carries totcol = 0, and
    // Blender itself falls back to its default 0.8 grey there, so that is what is reproduced.
    MaterialSlabDescriptor fallback = Dielectric(0.8f, 0.8f, 0.8f, 0.50f);
    Materials.push_back(MakeMaterial("Material.016 / unassigned — Blender default grey", fallback));   // 15
}

//------------------------------------------------------------------------------------------------------------------------ Construct + Export
void DriveSceneAuthor::Construct(bool StaticPose) noexcept
{
    Triangles.clear(); CornerNormals.clear(); Spans.clear();
    AuthorMaterials();

    Frontier::Vehicle::VehicleGeometry geo;   // real ControlVehicle socket data (dims, CoM height, wheel layout)
    const float comH   = geo.CoMHeight;        // shift the body so local origin = CoM
    const float radius = geo.TyreRadius;
    const float halfW  = 0.5f * geo.TyreWidth; // measured section width (ControlVehicle.blend RubberFL)

    // span 0 — body (dynamic) -> instance 0.  Its faces are partitioned into MAMetalicCoat, MAGlass and MAPlastic.
    { auto s = OpenSpan("ControlVehicle — paint / glass / plastic", /*Dynamic=*/true); (void)s;
      AppendVehicleBody(comH, MatBodyPaint);
      if (StaticPose)
          for (auto& Facet : Triangles)
          {
              Facet.VertexAlphaZ += comH + 0.02f;
              Facet.VertexBetaZ  += comH + 0.02f;
              Facet.VertexGammaZ += comH + 0.02f;
          }
    }

    // spans 1..4 — wheels (dynamic) -> instances 1..4 (FL, FR, RL, RR).  Every wheel emits MARubber + hub + brake.
    const char* wheelNames[4] = { "XPBD Tyre FL", "XPBD Tyre FR", "XPBD Tyre RL", "XPBD Tyre RR" };
    for (int w = 0; w < 4; ++w)
    { auto s = OpenSpan(wheelNames[w], /*Dynamic=*/true); (void)s;
      const Frontier::Vehicle::Vec3 Rest = geo.AxleMountLocal(static_cast<uint32_t>(w));
      const Vector3 Hub = StaticPose ? Vector3{ Rest.x, Rest.y, Rest.z + comH + 0.02f } : Vector3{0,0,0};
      AppendWheel(Hub, radius, halfW, /*Segments=*/kDriveWheelSegmentCount, MatTyre); }
    // 📝 Rim/mag spokes are off for now (kDriveWheelSpokeCount = 0): the wheel closes with a plain hub face while
    //    the tyre itself is the thing under review. Restoring them is one constant, not a rewrite.

    // span 5 — course (static) -> instances 5.. (one per material used)
    { auto s = OpenSpan("Course", /*Dynamic=*/false); (void)s;
      AppendCourse(); }
}

bool DriveSceneAuthor::Export(const std::string& Path, std::string* Error) const noexcept
{
    SceneEncodeConfiguration Configuration;
    Configuration.Name           = "DriveCourse.r" + std::to_string(kDriveSceneRevision);
    Configuration.CornerNormals  = &CornerNormals;
    Configuration.WriteTexcoords = true;
    Configuration.Spans          = &Spans;
    return SceneCodec::Encode(Path, Triangles, Materials, Error, Configuration);
}

//------------------------------------------------------------------------------------------------------------------------ revision check (cheap header scan for the stamped scene name)
bool DriveSceneMatchesRevision(const std::string& Path) noexcept
{
    std::ifstream f(Path, std::ios::binary);
    if (!f) return false;
    std::string head(4096, '\0');
    f.read(&head[0], static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<size_t>(f.gcount()));
    const std::string marker = "DriveCourse.r" + std::to_string(kDriveSceneRevision);
    return head.find(marker) != std::string::npos;
}

} // namespace Drive
} // namespace Frontier
