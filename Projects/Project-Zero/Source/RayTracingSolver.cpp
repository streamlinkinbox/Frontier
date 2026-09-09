//============================================================================================================================================
// 📦 Project-Zero/Source/RayTracingSolver.cpp — Triangle Ray Intersection and Cornell Box Scene Solver Implementation
//============================================================================================================================================

#include "RayTracingSolver.h"
#include <cmath>
#include <limits>
#include <algorithm>

namespace Frontier::ProjectZero {

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

RayTracingSolver::RayTracingSolver() noexcept
{
    ConstructCornellBoxScene();
}

RayTracingSolver::SpanScope::~SpanScope() noexcept
{
    if (Spans == nullptr || Triangles == nullptr || Span >= Spans->size()) return;
    TriangleSpanRecord& S = (*Spans)[Span];
    const uint32_t Now = static_cast<uint32_t>(Triangles->size());
    S.TriangleCount = Now >= S.FirstTriangle ? Now - S.FirstTriangle : 0u;
}

RayTracingSolver::SpanScope RayTracingSolver::OpenSpan(const char* Name, bool Dynamic) noexcept
{
    TriangleSpanRecord S;
    S.FirstTriangle = static_cast<uint32_t>(Triangles.size());
    if (Name != nullptr) S.Name = Name;
    S.Dynamic = Dynamic;
    Spans.push_back(std::move(S));
    SpanScope Scope;
    Scope.Spans     = &Spans;
    Scope.Triangles = &Triangles;
    Scope.Span      = static_cast<uint32_t>(Spans.size()) - 1u;
    return Scope;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                SCENE GEOMETRY SETUP
//------------------------------------------------------------------------------------------------------------------------

void RayTracingSolver::ConstructCornellBoxScene() noexcept
{
    Triangles.clear();
    Materials.clear();
    Spans.clear();

    // Material 0: White diffuse walls, floor, ceiling
    Materials.push_back(AnalyticalMaterial{ Vector3{ 0.75f, 0.75f, 0.75f }, Vector3{ 0.0f, 0.0f, 0.0f }, 0.5f, 0.0f, 0 });
    // Material 1: Left wall (vibrant red)
    Materials.push_back(AnalyticalMaterial{ Vector3{ 0.85f, 0.12f, 0.12f }, Vector3{ 0.0f, 0.0f, 0.0f }, 0.5f, 0.0f, 1  });
    // Material 2: Right wall (vibrant green)
    Materials.push_back(AnalyticalMaterial{ Vector3{ 0.12f, 0.85f, 0.15f }, Vector3{ 0.0f, 0.0f, 0.0f }, 0.5f, 0.0f, 2  });
    // Material 3: Ceiling Light (bright emissive white)
    Materials.push_back(AnalyticalMaterial{ Vector3{ 1.0f, 1.0f, 1.0f }, Vector3{ 32.0f, 32.0f, 32.0f }, 0.1f, 0.0f, 3  });
    // Material 4: Tall Box (warm white diffuse)
    Materials.push_back(AnalyticalMaterial{ Vector3{ 0.78f, 0.78f, 0.78f }, Vector3{ 0.0f, 0.0f, 0.0f }, 0.4f, 0.0f, 4  });
    // Material 5: Short Box (cool white diffuse)
    Materials.push_back(AnalyticalMaterial{ Vector3{ 0.78f, 0.78f, 0.78f }, Vector3{ 0.0f, 0.0f, 0.0f }, 0.4f, 0.0f, 5  });
    // Material 6: Sphere (warm off-white, smoother than the boxes so the oculus highlight is visible on it)
    Materials.push_back(AnalyticalMaterial{ Vector3{ 0.82f, 0.78f, 0.72f }, Vector3{ 0.0f, 0.0f, 0.0f }, 0.25f, 0.0f, 6  });
    // Material 7: Cone (muted blue)
    Materials.push_back(AnalyticalMaterial{ Vector3{ 0.35f, 0.45f, 0.70f }, Vector3{ 0.0f, 0.0f, 0.0f }, 0.4f, 0.0f, 7  });
    // Material 8: Torus (muted amber)
    Materials.push_back(AnalyticalMaterial{ Vector3{ 0.78f, 0.55f, 0.25f }, Vector3{ 0.0f, 0.0f, 0.0f }, 0.35f, 0.0f, 8  });

    // Cornell Box in the engine's right-handed Z-up world (CLAUDE.md §7):
    //      X ∈ [−2, +2]  right / east       (red wall at X = −2, green wall at X = +2)
    //      Y ∈ [ 0, +4]  forward / north    (open front at Y = 0, back wall at Y = +4)
    //      Z ∈ [ 0, +3]  up / zenith        (floor at Z = 0, ceiling + luminaire at Z = +3)
    //    The camera stands at Y < 0 looking along +Y into the room.
    //
    //    The room is deliberately larger than the classic 2 × 2 × 2 Cornell box: the sky work needs floor area for a
    //    sun shaft to land on and enough height for the shaft to read as a shaft rather than a bright patch.
    //
    //    ⚠️ The emissive quad MUST remain the last geometry appended — the shader addresses light
    //       triangles as the trailing LightTriangleCount entries of the triangle buffer.

    // Room and aperture bounds. Named rather than inlined because the ceiling is built as four strips around the
    //    hole and every strip has to agree with the others on where the opening is.
    constexpr float RoomMinX = -2.0f, RoomMaxX = 2.0f;
    constexpr float RoomMinY =  0.0f, RoomMaxY = 4.0f;
    constexpr float RoomTopZ =  3.0f;
    // A CIRCULAR oculus rather than a rectangle: a round opening throws an elliptical shaft that reads as
    //    sunlight through a roof, and its silhouette is the clearest possible test that the sky is being sampled
    //    through real geometry rather than painted on.
    constexpr float HoleRadius   = 0.75f;                  // [m]
    constexpr float HoleCentreX  = 0.0f;                   // [m]
    // 🔴 The shaft goes NORTH, so the hole belongs SOUTH of where the light should land. This was 3.05 — set
    //    back toward the north wall on the reasoning that the shaft would cross the floor — and that is
    //    backwards. At any northern latitude the midday sun stands to the south, so a roof opening throws its
    //    shaft AWAY from the camera, toward +Y. From 3.05 the light landed at Y ≈ 4.3, which is behind the back
    //    wall: measured over a day at latitude 45, the shaft was fully on the floor for 0 % of daylight.
    //
    //    Swept rather than guessed. 2.10 puts the noon shaft at Y ≈ 3.3 and holds the whole disc on the floor
    //    for 32 % of daylight at latitude 45 and 39 % at the equator — the best either latitude achieves, since
    //    what carries the shaft out of the room is the sun's EAST-WEST travel, not its height. It also clears
    //    the luminaire, which occupies Y ∈ [0.9, 1.7] of the same ceiling.
    constexpr float HoleCentreY  = 2.10f;                  // [m] south of the target, because the shaft runs north
    constexpr uint32_t HoleSides = 48u;                    // 48 sides: the rim reads as a circle at room scale

    // Floor (Z = 0, normal +Z)
    {
        const auto FloorSpan = OpenSpan("Floor");
        AppendQuad(Vector3{ RoomMinX, RoomMinY, 0.0f }, Vector3{ RoomMaxX, RoomMinY, 0.0f }, Vector3{ RoomMaxX, RoomMaxY, 0.0f }, Vector3{ RoomMinX, RoomMaxY, 0.0f }, 0);
    }

    // Ceiling (Z = RoomTopZ, normal −Z) — a plate with the oculus cut out of it. Until the sky exists the
    //    opening reads as black, which is correct: there is genuinely nothing above it yet.
    {
        const auto CeilingSpan = OpenSpan("Ceiling");
        AppendPlateWithCircularHole(RoomMinX, RoomMinY, RoomMaxX, RoomMaxY, RoomTopZ,
                                    Vector3{ HoleCentreX, HoleCentreY, RoomTopZ }, HoleRadius, HoleSides, true, 0);
    }

    // Back Wall (Y = RoomMaxY, normal −Y)
    {
        const auto BackSpan = OpenSpan("Back Wall");
        AppendQuad(Vector3{ RoomMinX, RoomMaxY, 0.0f }, Vector3{ RoomMaxX, RoomMaxY, 0.0f }, Vector3{ RoomMaxX, RoomMaxY, RoomTopZ }, Vector3{ RoomMinX, RoomMaxY, RoomTopZ }, 0);
    }
    // Left Wall (X = −2, Red, normal +X)
    {
        const auto LeftSpan = OpenSpan("Left Wall");
        AppendQuad(Vector3{ RoomMinX, RoomMinY, 0.0f }, Vector3{ RoomMinX, RoomMaxY, 0.0f }, Vector3{ RoomMinX, RoomMaxY, RoomTopZ }, Vector3{ RoomMinX, RoomMinY, RoomTopZ }, 1);
    }
    // Right Wall (X = +2, Green, normal −X)
    {
        const auto RightSpan = OpenSpan("Right Wall");
        AppendQuad(Vector3{ RoomMaxX, RoomMaxY, 0.0f }, Vector3{ RoomMaxX, RoomMinY, 0.0f }, Vector3{ RoomMaxX, RoomMinY, RoomTopZ }, Vector3{ RoomMaxX, RoomMaxY, RoomTopZ }, 2);
    }

    // Tall Box (0.84 × 0.84 footprint, 1.8 m tall, rotated +22° about Z) — rear left, clear of the aperture so it
    //    catches the edge of the shaft and casts a long shadow rather than plugging the hole.
    {
        const auto TallSpan = OpenSpan("Tall Box", true);
        AppendBox(Vector3{ -0.90f, 2.70f, 0.90f }, Vector3{ 0.42f, 0.42f, 0.90f },  22.0f, 4);
    }
    // Short Box (0.84 × 0.84 footprint, 0.9 m tall, rotated −18° about Z) — front right
    {
        const auto ShortSpan = OpenSpan("Short Box", true);
        AppendBox(Vector3{  0.85f, 1.50f, 0.45f }, Vector3{ 0.42f, 0.42f, 0.45f }, -18.0f, 5);
    }

    // Parametric primitives, placed clear of the two boxes and of each other. Segment counts are chosen for a
    //    GTX 1650 SUPER: together these three add ~2 400 triangles against the room's 40, which is the right
    //    order for a test scene and still leaves headroom before the R8 GPU-BVH gate at ~15 000 moving triangles.
    {
        const auto SphereSpan = OpenSpan("Sphere", true);
        AppendSphere(Vector3{ -1.15f, 1.05f, 0.45f }, 0.45f, 32u, 16u, 6u);           //   960 tris
    }
    {
        const auto ConeSpan = OpenSpan("Cone", true);
        AppendCone  (Vector3{  1.30f, 3.05f, 0.00f }, 0.45f, 1.10f, 32u, 7u);         //    64 tris
    }
    {
        const auto TorusSpan = OpenSpan("Torus", true);
        AppendTorus (Vector3{  0.00f, 1.55f, 0.32f }, 0.42f, 0.14f, 36u, 18u, 8u);    // 1 296 tris
    }

    // Ceiling Luminaire (Z = RoomTopZ − 0.005, normal −Z) — LAST, see note above.
    //
    // ⚠️ Moved forward with the aperture, and it MUST stay clear of it. The quad hangs 5 mm below the ceiling
    //    plane, so any part of it inside the opening would be seen through the hole as a bright horizontal slab
    //    with the sky behind it — the one thing the aperture exists to show, blocked by the lamp that the
    //    aperture is meant to be compared against. The hole reaches Y = 1.35 at its nearest; this ends at 1.15.
    constexpr float LampMinY = 0.35f, LampMaxY = 1.15f, LampZ = RoomTopZ - 0.005f;
    {
        const auto LuminaireSpan = OpenSpan("Ceiling Luminaire");
        AppendQuad(Vector3{ -0.50f, LampMaxY, LampZ }, Vector3{ 0.50f, LampMaxY, LampZ },
                   Vector3{  0.50f, LampMinY, LampZ }, Vector3{ -0.50f, LampMinY, LampZ }, 3);
    }
}

void RayTracingSolver::AppendTriangle(const Vector3& v0, const Vector3& v1, const Vector3& v2, uint32_t MaterialIdx) noexcept
{
    TriangleGeometry Tri{};
    Tri.VertexAlpha    = v0;
    Tri.VertexBeta     = v1;
    Tri.VertexGamma    = v2;
    Vector3 Edge1      = v1 - v0;
    Vector3 Edge2      = v2 - v0;
    Tri.SurfaceNormal  = OrientationClassifier::CrossProduct(Edge1, Edge2).Normalized();
    Tri.MaterialIndex  = MaterialIdx;
    Tri.TriangleIndex  = static_cast<uint32_t>(Triangles.size());
    Triangles.push_back(Tri);
}

void RayTracingSolver::AppendQuad(const Vector3& v0, const Vector3& v1, const Vector3& v2, const Vector3& v3, uint32_t MaterialIdx) noexcept
{
    // Quad formed of two triangles with CCW outward normal
    AppendTriangle(v0, v1, v2, MaterialIdx);
    AppendTriangle(v0, v2, v3, MaterialIdx);
}

void RayTracingSolver::AppendBox(const Vector3& Center, const Vector3& Extents, float RotationDegrees, uint32_t MaterialIdx) noexcept
{
    // Z-up: the box is rotated about the vertical (+Z) axis; Extents = half-sizes (X, Y, Z).
    float Rad = RotationDegrees * 3.14159265359f / 180.0f;
    float CosAngle = std::cos(Rad);
    float SinAngle = std::sin(Rad);

    auto RotateZ = [CosAngle, SinAngle](const Vector3& p) -> Vector3
    {
        return Vector3{ p.x * CosAngle - p.y * SinAngle, p.x * SinAngle + p.y * CosAngle, p.z };
    };

    float hx = Extents.x;
    float hy = Extents.y;
    float hz = Extents.z;

    Vector3 Corners[8] = {
        Center + RotateZ(Vector3{ -hx, -hy, -hz }), // 0: Bottom-Left-Front   (−X, −Y, −Z)
        Center + RotateZ(Vector3{  hx, -hy, -hz }), // 1: Bottom-Right-Front  (+X, −Y, −Z)
        Center + RotateZ(Vector3{  hx,  hy, -hz }), // 2: Bottom-Right-Back   (+X, +Y, −Z)
        Center + RotateZ(Vector3{ -hx,  hy, -hz }), // 3: Bottom-Left-Back    (−X, +Y, −Z)
        Center + RotateZ(Vector3{ -hx, -hy,  hz }), // 4: Top-Left-Front      (−X, −Y, +Z)
        Center + RotateZ(Vector3{  hx, -hy,  hz }), // 5: Top-Right-Front     (+X, −Y, +Z)
        Center + RotateZ(Vector3{  hx,  hy,  hz }), // 6: Top-Right-Back      (+X, +Y, +Z)
        Center + RotateZ(Vector3{ -hx,  hy,  hz })  // 7: Top-Left-Back       (−X, +Y, +Z)
    };

    // 6 faces, counter-clockwise seen from outside so the geometric normal points outward:
    // Top (+Z)
    AppendQuad(Corners[4], Corners[5], Corners[6], Corners[7], MaterialIdx);
    // Bottom (−Z)
    AppendQuad(Corners[3], Corners[2], Corners[1], Corners[0], MaterialIdx);
    // Front (−Y)
    AppendQuad(Corners[0], Corners[1], Corners[5], Corners[4], MaterialIdx);
    // Back (+Y)
    AppendQuad(Corners[2], Corners[3], Corners[7], Corners[6], MaterialIdx);
    // Left (−X)
    AppendQuad(Corners[3], Corners[0], Corners[4], Corners[7], MaterialIdx);
    // Right (+X)
    AppendQuad(Corners[1], Corners[2], Corners[6], Corners[5], MaterialIdx);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  OUTDOOR SCENE
//------------------------------------------------------------------------------------------------------------------------
// A1–A7 built a physically correct sun, sky, sunset, moon, star field, skylight and adaptive exposure — and the
//    only place any of it could be seen was a 1.5 m oculus at 12.9° elevation, subtending 13° from a camera that
//    starts facing a wall. This scene exists so that work can actually be judged.
//
//    🔴 No ceiling and no walls. That is the entire point: every ray that misses geometry resolves to sky, so a
//    sunset fills the frame instead of a porthole and the moon has somewhere to rise.

void RayTracingSolver::ConstructOutdoorScene() noexcept
{
    Triangles.clear();
    Materials.clear();
    Spans.clear();

    // Ground is deliberately mid-grey and slightly rough. A bright ground would bounce enough light to mask the
    //    sky's own contribution, which is the thing being judged; a dark one would hide the sun's shadows.
    Materials.push_back(AnalyticalMaterial{ Vector3{ 0.32f, 0.32f, 0.30f }, Vector3{ 0.0f, 0.0f, 0.0f }, 0.6f, 0.0f, 0  });
    // A neutral white for the shadow casters, so their shading is the sky's colour and not their own.
    Materials.push_back(AnalyticalMaterial{ Vector3{ 0.80f, 0.80f, 0.80f }, Vector3{ 0.0f, 0.0f, 0.0f }, 0.4f, 0.0f, 1  });
    // Smoother, to catch a specular glint of the sun and the sky.
    Materials.push_back(AnalyticalMaterial{ Vector3{ 0.72f, 0.74f, 0.78f }, Vector3{ 0.0f, 0.0f, 0.0f }, 0.15f, 0.0f, 2  });
    // Warm, so the sunset's colour shift is legible against something that is not neutral.
    Materials.push_back(AnalyticalMaterial{ Vector3{ 0.70f, 0.45f, 0.28f }, Vector3{ 0.0f, 0.0f, 0.0f }, 0.5f, 0.0f, 3  });

    // ⚠️ The ground is 400 m across, not a few metres. Two reasons, both load-bearing:
    //      · the horizon has to be far enough away that the eye reads it as a horizon rather than as the edge of
    //        a plate, which is what makes the sky feel like a sky;
    //      · aerial perspective (A6) is invisible over six metres — 0.016 % colour shift, 24× below one 8-bit
    //        step — and only becomes measurable over hundreds. This scene is what makes that phase testable.
    constexpr float GroundExtent = 200.0f;   // [m] half-width
    {
        const auto GroundSpan = OpenSpan("Ground");
        AppendQuad(Vector3{ -GroundExtent, -GroundExtent, 0.0f }, Vector3{  GroundExtent, -GroundExtent, 0.0f },
                   Vector3{  GroundExtent,  GroundExtent, 0.0f }, Vector3{ -GroundExtent,  GroundExtent, 0.0f }, 0);
    }

    // Casters at a spread of heights, so shadow length changes visibly as the sun moves and the penumbra widens
    //    with distance from the ground — which is the A4 result made observable.
    {
        const auto WhiteSphereSpan = OpenSpan("White Sphere");
        AppendSphere(Vector3{ -3.20f, 6.00f, 1.20f }, 1.20f, 40u, 20u, 1u);
    }
    {
        const auto SteelSphereSpan = OpenSpan("Steel Sphere");
        AppendSphere(Vector3{  4.60f, 11.00f, 0.70f }, 0.70f, 32u, 16u, 2u);
    }
    {
        const auto ClayConeSpan = OpenSpan("Clay Cone");
        AppendCone  (Vector3{  1.80f, 5.20f, 0.00f }, 0.90f, 2.60f, 40u, 3u);
    }
    {
        const auto SteelTorusSpan = OpenSpan("Steel Torus");
        AppendTorus (Vector3{ -1.40f, 9.50f, 1.60f }, 1.10f, 0.30f, 44u, 22u, 2u);
    }

    // A tall thin slab. A long shadow is the clearest possible read on the sun's elevation, and its edge is
    //    where a penumbra is easiest to measure against the numbers A4 recorded.
    {
        const auto WhiteSlabSpan = OpenSpan("White Slab");
        AppendBox(Vector3{  6.50f, 4.00f, 2.00f }, Vector3{ 0.25f, 1.60f, 2.00f }, 18.0f, 1u);
    }
    {
        const auto ClayCrateSpan = OpenSpan("Clay Crate");
        AppendBox(Vector3{ -6.00f, 3.20f, 0.60f }, Vector3{ 1.00f, 1.00f, 0.60f }, -12.0f, 3u);
    }

    // ⚠️ NO luminaire. The sun is the only light, which is what A4 made possible: before it, a scene with no
    //    emissive triangle was simply black. That makes this scene a live test of the sun-as-emitter path — if
    //    it ever regresses, this render goes dark rather than merely looking wrong.
}

//------------------------------------------------------------------------------------------------------------------------
//                                             PARAMETRIC PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------
// Winding is counter-clockwise seen from OUTSIDE the solid, matching AppendBox: AppendTriangle derives the
//    geometric normal from the edge cross product, so a reversed winding produces a surface lit from inside and
//    shadowed from outside. That failure looks like a shading bug rather than a geometry one, which is why the
//    orientation of every ring below is stated explicitly.

void RayTracingSolver::AppendSphere(const Vector3& Center, float Radius, uint32_t Segments, uint32_t Rings, uint32_t MaterialIdx) noexcept
{
    if (Segments < 3u || Rings < 2u || Radius <= 0.0f) return;

    constexpr float Pi = 3.14159265359f;
    const auto Point = [&](uint32_t Ring, uint32_t Segment) -> Vector3
    {
        const float Polar     = Pi * static_cast<float>(Ring) / static_cast<float>(Rings);          // 0 at +Z pole
        const float Azimuth   = 2.0f * Pi * static_cast<float>(Segment % Segments) / static_cast<float>(Segments);
        const float SinPolar  = std::sin(Polar);
        return Center + Vector3{ Radius * SinPolar * std::cos(Azimuth),
                                 Radius * SinPolar * std::sin(Azimuth),
                                 Radius * std::cos(Polar) };
    };

    for (uint32_t Ring = 0u; Ring < Rings; ++Ring)
    {
        for (uint32_t Segment = 0u; Segment < Segments; ++Segment)
        {
            const Vector3 A = Point(Ring,      Segment);
            const Vector3 B = Point(Ring,      Segment + 1u);
            const Vector3 C = Point(Ring + 1u, Segment + 1u);
            const Vector3 D = Point(Ring + 1u, Segment);

            // The two polar rings collapse to a point on one side, so they are emitted as single triangles.
            //    A quad there would carry a zero-area half that the BVH must still store and test.
            // ⚠️ Ring advances from the +Z pole DOWNWARD while Segment advances anticlockwise about +Z, so the
            //    natural (A,B,C,D) order traverses clockwise seen from outside and yields inward normals — the
            //    surface then lights from within and shadows from without, which reads as a shading bug. The
            //    order below is reversed for that reason.
            if (Ring == 0u)                 AppendTriangle(A, D, C, MaterialIdx);
            else if (Ring + 1u == Rings)    AppendTriangle(A, C, B, MaterialIdx);
            else                            AppendQuad(A, D, C, B, MaterialIdx);
        }
    }
}

void RayTracingSolver::AppendCone(const Vector3& BaseCentre, float Radius, float Height, uint32_t Segments, uint32_t MaterialIdx) noexcept
{
    if (Segments < 3u || Radius <= 0.0f || Height <= 0.0f) return;

    constexpr float Pi = 3.14159265359f;
    const Vector3 Apex = BaseCentre + Vector3{ 0.0f, 0.0f, Height };
    const auto Rim = [&](uint32_t Segment) -> Vector3
    {
        const float Azimuth = 2.0f * Pi * static_cast<float>(Segment % Segments) / static_cast<float>(Segments);
        return BaseCentre + Vector3{ Radius * std::cos(Azimuth), Radius * std::sin(Azimuth), 0.0f };
    };

    for (uint32_t Segment = 0u; Segment < Segments; ++Segment)
    {
        const Vector3 A = Rim(Segment);
        const Vector3 B = Rim(Segment + 1u);
        AppendTriangle(A, B, Apex, MaterialIdx);        // side, outward
        AppendTriangle(B, A, BaseCentre, MaterialIdx);  // base, downward (−Z)
    }
}

void RayTracingSolver::AppendTorus(const Vector3& Center, float MajorRadius, float MinorRadius,
                                   uint32_t MajorSegments, uint32_t MinorSegments, uint32_t MaterialIdx) noexcept
{
    if (MajorSegments < 3u || MinorSegments < 3u || MinorRadius <= 0.0f) return;
    // A minor radius at or past the major one self-intersects through the hole; the surface is no longer a
    //    torus and the normals invert where it passes through itself.
    if (MinorRadius >= MajorRadius) return;

    constexpr float Pi = 3.14159265359f;
    const auto Point = [&](uint32_t Major, uint32_t Minor) -> Vector3
    {
        const float U = 2.0f * Pi * static_cast<float>(Major % MajorSegments) / static_cast<float>(MajorSegments);
        const float V = 2.0f * Pi * static_cast<float>(Minor % MinorSegments) / static_cast<float>(MinorSegments);
        const float RingRadius = MajorRadius + MinorRadius * std::cos(V);
        return Center + Vector3{ RingRadius * std::cos(U), RingRadius * std::sin(U), MinorRadius * std::sin(V) };
    };

    for (uint32_t Major = 0u; Major < MajorSegments; ++Major)
        for (uint32_t Minor = 0u; Minor < MinorSegments; ++Minor)
            // U (around the ring) and V (around the tube) are both anticlockwise, and their cross product
            //    already points away from the tube axis — so unlike the sphere this order is correct as written.
            //    Reversing it inverts every face uniformly, which the audit reports against the nearest point on
            //    the tube's centre circle rather than the torus centre: a torus is not star-shaped about its
            //    centre, so its inner wall legitimately faces inward and a centre-based test is meaningless.
            AppendQuad(Point(Major,      Minor),
                       Point(Major + 1u, Minor),
                       Point(Major + 1u, Minor + 1u),
                       Point(Major,      Minor + 1u), MaterialIdx);
}

void RayTracingSolver::AppendPlateWithCircularHole(float MinimumX, float MinimumY, float MaximumX, float MaximumY,
                                                   float Z, const Vector3& HoleCentre, float HoleRadius,
                                                   uint32_t Segments, bool FaceDown, uint32_t MaterialIdx) noexcept
{
    if (Segments < 3u || HoleRadius <= 0.0f) return;

    constexpr float Pi = 3.14159265359f;
    const auto Rim = [&](uint32_t Segment) -> Vector3
    {
        const float Azimuth = 2.0f * Pi * static_cast<float>(Segment % Segments) / static_cast<float>(Segments);
        return Vector3{ HoleCentre.x + HoleRadius * std::cos(Azimuth),
                        HoleCentre.y + HoleRadius * std::sin(Azimuth), Z };
    };

    // Each rim vertex is joined to the point where a ray from the hole centre through it leaves the rectangle.
    //    That keeps the ring a fan of quads with no T-junctions against the border, which a naive
    //    rectangle-minus-circle would produce and which show as hairline cracks under a moving camera.
    const auto Border = [&](uint32_t Segment) -> Vector3
    {
        const float Azimuth = 2.0f * Pi * static_cast<float>(Segment % Segments) / static_cast<float>(Segments);
        const float Dx = std::cos(Azimuth), Dy = std::sin(Azimuth);
        // Distance along the ray to each of the four edges; the nearest positive one is the exit.
        float Travel = 1e30f;
        if (Dx > 1e-6f)  Travel = std::min(Travel, (MaximumX - HoleCentre.x) / Dx);
        if (Dx < -1e-6f) Travel = std::min(Travel, (MinimumX - HoleCentre.x) / Dx);
        if (Dy > 1e-6f)  Travel = std::min(Travel, (MaximumY - HoleCentre.y) / Dy);
        if (Dy < -1e-6f) Travel = std::min(Travel, (MinimumY - HoleCentre.y) / Dy);
        return Vector3{ HoleCentre.x + Dx * Travel, HoleCentre.y + Dy * Travel, Z };
    };

    for (uint32_t Segment = 0u; Segment < Segments; ++Segment)
    {
        const Vector3 InnerA = Rim(Segment),      InnerB = Rim(Segment + 1u);
        const Vector3 OuterA = Border(Segment),   OuterB = Border(Segment + 1u);
        // ⚠️ The rim advances anticlockwise seen from +Z, so (Inner, Outer, Outer, Inner) in that order gives a
        //    +Z normal. A ceiling is seen from BELOW and must face −Z, hence the swap: FaceDown takes the
        //    reversed winding. Getting this backwards leaves the ceiling lit from above and black from the room,
        //    which reads as the light being wrong rather than the geometry.
        if (FaceDown) AppendQuad(InnerA, InnerB, OuterB, OuterA, MaterialIdx);
        else          AppendQuad(InnerA, OuterA, OuterB, InnerB, MaterialIdx);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                           RAY-TRIANGLE INTERSECTION
//------------------------------------------------------------------------------------------------------------------------

HitIntersection RayTracingSolver::EvaluateIntersection(const RayStructure& Ray) const noexcept
{
    HitIntersection ClosestHit{};
    ClosestHit.RayDistance    = Ray.MaximumDistance;
    ClosestHit.ValidCondition = false;

    constexpr float Epsilon = 1e-7f;

    for (const auto& Tri : Triangles)
    {
        Vector3 Edge1 = Tri.VertexBeta - Tri.VertexAlpha;
        Vector3 Edge2 = Tri.VertexGamma - Tri.VertexAlpha;

        Vector3 PVec = OrientationClassifier::CrossProduct(Ray.RayDirection, Edge2);
        float Det = OrientationClassifier::DotProduct(Edge1, PVec);

        if (std::abs(Det) < Epsilon)
        {
            continue;
        }

        float InvDet = 1.0f / Det;
        Vector3 TVec = Ray.SpatialOrigin - Tri.VertexAlpha;
        float u = OrientationClassifier::DotProduct(TVec, PVec) * InvDet;

        if (u < 0.0f || u > 1.0f)
        {
            continue;
        }

        Vector3 QVec = OrientationClassifier::CrossProduct(TVec, Edge1);
        float v = OrientationClassifier::DotProduct(Ray.RayDirection, QVec) * InvDet;

        if (v < 0.0f || (u + v) > 1.0f)
        {
            continue;
        }

        float t = OrientationClassifier::DotProduct(Edge2, QVec) * InvDet;

        if (t >= Ray.MinimumDistance && t < ClosestHit.RayDistance)
        {
            ClosestHit.RayDistance    = t;
            ClosestHit.HitLocation    = Ray.SpatialOrigin + Ray.RayDirection * t;
            ClosestHit.SurfaceNormal  = Tri.SurfaceNormal;
            ClosestHit.MaterialIndex  = Tri.MaterialIndex;
            ClosestHit.TriangleIndex  = Tri.TriangleIndex;
            ClosestHit.ValidCondition = true;
        }
    }

    return ClosestHit;
}

bool RayTracingSolver::EvaluateOcclusion(const Vector3& PointA, const Vector3& PointB) const noexcept
{
    Vector3 Dir = PointB - PointA;
    float Distance = Dir.Length();
    if (Distance <= 1e-4f)
    {
        return false;
    }

    Vector3 UnitDir = Dir / Distance;
    RayStructure ShadowRay{ PointA + UnitDir * 1e-4f, UnitDir, 1e-4f, Distance - 1e-4f };

    constexpr float Epsilon = 1e-7f;

    for (const auto& Tri : Triangles)
    {
        // Don't let light quad occlude itself
        if (Tri.MaterialIndex == 3)
        {
            continue;
        }

        Vector3 Edge1 = Tri.VertexBeta - Tri.VertexAlpha;
        Vector3 Edge2 = Tri.VertexGamma - Tri.VertexAlpha;

        Vector3 PVec = OrientationClassifier::CrossProduct(ShadowRay.RayDirection, Edge2);
        float Det = OrientationClassifier::DotProduct(Edge1, PVec);

        if (std::abs(Det) < Epsilon)
        {
            continue;
        }

        float InvDet = 1.0f / Det;
        Vector3 TVec = ShadowRay.SpatialOrigin - Tri.VertexAlpha;
        float u = OrientationClassifier::DotProduct(TVec, PVec) * InvDet;

        if (u < 0.0f || u > 1.0f)
        {
            continue;
        }

        Vector3 QVec = OrientationClassifier::CrossProduct(TVec, Edge1);
        float v = OrientationClassifier::DotProduct(ShadowRay.RayDirection, QVec) * InvDet;

        if (v < 0.0f || (u + v) > 1.0f)
        {
            continue;
        }

        float t = OrientationClassifier::DotProduct(Edge2, QVec) * InvDet;

        if (t >= ShadowRay.MinimumDistance && t <= ShadowRay.MaximumDistance)
        {
            return true; // Occluded
        }
    }

    return false;
}

} // namespace Frontier::ProjectZero
