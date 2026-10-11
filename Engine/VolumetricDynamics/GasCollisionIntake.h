//==============================================================================================================================================
//                                                          GASCOLLISIONINTAKE.H
//==============================================================================================================================================
// 📦 How the world becomes an obstruction the gas must flow around — analytic primitives, or a reading of the global distance field.
//
// Two admissions, both opt-in per object, and they compose: an object carrying a primitive is admitted through
//    the first because it is cheaper and exact, and anything else falls through to the second.
//
//    ① PRIMITIVE — sphere, capsule, box, cylinder, or the tyre ring the simulator already understands.
//       Written analytically into the occupancy reading each advance. Six signed distances, no storage.
//    ② DISTANCE READING — sample whatever already answers "how far to the nearest surface" at a world point.
//       Arbitrary geometry obstructs the gas with no new code in the solver, because the engine already bakes
//       that answer: Engine/GeometricRaster/GlobalDistanceFieldSpace.h exposes SampleSceneDistance(Vector3),
//       and no target presently consumes it. The adapter is one line, shown below.
//
// 💡 The seam here is a function and a context pointer rather than an include of GlobalDistanceFieldSpace.h, for
//    the reason the rest of this directory is header-only: the solver has to run in the headless checks, where
//    GeometricRaster is not linked, and a hard dependency would drag the whole raster in to obstruct a sphere.
//    The adapter in the engine is:
//
//        float ReadSceneDistance(const void* Context, const float Position[3])
//        {
//            const auto* Scene = static_cast<const GlobalDistanceFieldSpace*>(Context);
//            return Scene->SampleSceneDistance({ Position[0], Position[1], Position[2] });
//        }
//
// ⚠️ Occupancy is rewritten from nothing on every advance, never accumulated. A moving obstruction that left its
//    previous position marked would trail a solid wake, and the gas would pile up against a shape that is no
//    longer there — a defect that looks exactly like a solver instability and is not one.

#pragma once

#include "CoarseGasField.h"

#include <cmath>
#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                     ADMITTED SHAPES
//------------------------------------------------------------------------------------------------------------------------

enum class GasColliderShape : uint32_t
{
    None     = 0u,   // [-] - admitted but contributing nothing; the resting condition of the component
    Sphere   = 1u,   // [-] - Dimensions[0] is the radius
    Capsule  = 2u,   // [-] - Dimensions[0] radius, Dimensions[2] half-length along axis 2
    Box      = 3u,   // [-] - Dimensions are the three half-extents
    Cylinder = 4u,   // [-] - Dimensions[0] radius, Dimensions[2] half-height along axis 2
    TyreRing = 5u,   // [-] - Dimensions[0] ring radius, Dimensions[1] section radius; axis 2 is the spin axis
};


// One obstruction as the solver sees it. No rotation: the five shapes are all axis-aligned about axis 2, which
//    covers the tyre, the barrel and the crate, and a rotated obstruction is admitted through the distance
//    reading instead. Adding an orientation here would mean inverting it per voxel for no case that needs it.
struct GasCollider
{
    GasColliderShape Shape         = GasColliderShape::None;
    float            Centre[3]     = { 0.0f, 0.0f, 0.0f };   // [m] - world centre
    float            Dimensions[3] = { 0.5f, 0.5f, 0.5f };   // [m] - read per the shape above
    float            Margin        = 0.0f;                   // [m] - obstruction grown outward; thickens thin geometry
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   SIGNED DISTANCES
//------------------------------------------------------------------------------------------------------------------------

/// 📦 Signed distance from a world position to one collider: negative inside, positive outside.
/// in    Collider   [-]  the obstruction
/// in    Position   [m]  world position
/// out   float      [m]  distance, margin already subtracted
/// err   a None collider returns a large positive distance, so it obstructs nothing
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline float ColliderDistance(const GasCollider& Collider, const float Position[3]) noexcept
{
    const float Dx = Position[0] - Collider.Centre[0];
    const float Dy = Position[1] - Collider.Centre[1];
    const float Dz = Position[2] - Collider.Centre[2];
    const float Radial = std::sqrt(Dx * Dx + Dy * Dy);

    float Distance = 1.0e6f;
    switch (Collider.Shape)
    {
        case GasColliderShape::None:
            return 1.0e6f;

        case GasColliderShape::Sphere:
            Distance = std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz) - Collider.Dimensions[0];
            break;

        case GasColliderShape::Capsule:
        {
            const float Along = std::fabs(Dz) - Collider.Dimensions[2];
            const float Axial = Along > 0.0f ? Along : 0.0f;
            Distance = std::sqrt(Radial * Radial + Axial * Axial) - Collider.Dimensions[0];
            break;
        }

        case GasColliderShape::Box:
        {
            const float Qx = std::fabs(Dx) - Collider.Dimensions[0];
            const float Qy = std::fabs(Dy) - Collider.Dimensions[1];
            const float Qz = std::fabs(Dz) - Collider.Dimensions[2];
            const float Ox = Qx > 0.0f ? Qx : 0.0f, Oy = Qy > 0.0f ? Qy : 0.0f, Oz = Qz > 0.0f ? Qz : 0.0f;
            const float Outside = std::sqrt(Ox * Ox + Oy * Oy + Oz * Oz);
            const float Largest = Qx > Qy ? (Qx > Qz ? Qx : Qz) : (Qy > Qz ? Qy : Qz);
            Distance = Outside + (Largest < 0.0f ? Largest : 0.0f);
            break;
        }

        case GasColliderShape::Cylinder:
        {
            const float Qx = Radial - Collider.Dimensions[0];
            const float Qz = std::fabs(Dz) - Collider.Dimensions[2];
            const float Ox = Qx > 0.0f ? Qx : 0.0f, Oz = Qz > 0.0f ? Qz : 0.0f;
            const float Largest = Qx > Qz ? Qx : Qz;
            Distance = std::sqrt(Ox * Ox + Oz * Oz) + (Largest < 0.0f ? Largest : 0.0f);
            break;
        }

        case GasColliderShape::TyreRing:
        {
            // 📐 Torus about axis 2: the distance from the circle of radius Dimensions[0] lying in the plane
            //    through the centre, minus the section radius. This is the same ring the browser simulator
            //    obstructs with, so a burnout reads the same in both.
            const float FromRing = Radial - Collider.Dimensions[0];
            Distance = std::sqrt(FromRing * FromRing + Dz * Dz) - Collider.Dimensions[1];
            break;
        }
    }
    return Distance - Collider.Margin;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  ADMITTING THE WORLD
//------------------------------------------------------------------------------------------------------------------------

/// 📦 Clears every voxel to open air. Called once per advance before the admissions below, because occupancy is
///    rewritten rather than accumulated — see the warning in the file header.
/// in    Field   [-]  the field
/// out   -
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline void ClearOccupancy(CoarseGasField& Field) noexcept
{
    for (uint32_t Slot = 0u; Slot < CoarseVoxelCount; ++Slot) Field.Occupancy[Slot] = 0.0f;
}


/// 📦 Admission ①. Marks every voxel whose centre lies inside the collider as solid.
/// in    Field       [-]  the field
/// in    Colliders   [-]  the obstructions that have opted in and overlap this field's cube
/// in    Count       [-]  how many
/// out   -
/// note  a collider entirely outside the cube costs one bounds comparison per voxel and nothing else; the
///       caller is still expected to cull, because that comparison is 32768 of them
/// cost  🚩  Count passes over 32768 voxels
/// tag   api, nonallocating, nonthrowing
inline void AdmitPrimitives(CoarseGasField& Field, const GasCollider* Colliders, uint32_t Count) noexcept
{
    const float Size = VoxelSize(Field);
    for (uint32_t Z = 0u; Z < CoarseExtent; ++Z)
    for (uint32_t Y = 0u; Y < CoarseExtent; ++Y)
    for (uint32_t X = 0u; X < CoarseExtent; ++X)
    {
        const uint32_t Slot = VoxelIndex(X, Y, Z);
        if (Field.Occupancy[Slot] > 0.5f) continue;
        const float Position[3] = { Field.Origin[0] + (static_cast<float>(X) + 0.5f) * Size,
                                    Field.Origin[1] + (static_cast<float>(Y) + 0.5f) * Size,
                                    Field.Origin[2] + (static_cast<float>(Z) + 0.5f) * Size };
        for (uint32_t Entry = 0u; Entry < Count; ++Entry)
        {
            if (ColliderDistance(Colliders[Entry], Position) <= 0.0f)
            {
                Field.Occupancy[Slot] = 1.0f;
                break;
            }
        }
    }
}


// Reads the distance to the nearest surface at a world position. Negative inside geometry, as every signed
//    distance in this engine is. The context is whatever the caller bound; the solver never inspects it.
using SceneDistanceReading = float (*)(const void* Context, const float Position[3]);


/// 📦 Admission ②. Marks every voxel the supplied distance reading places inside geometry.
/// in    Field       [-]  the field
/// in    Reading     [-]  the distance function; a null reading admits nothing and is not an error
/// in    Context     [-]  passed through untouched, typically a GlobalDistanceFieldSpace
/// in    Thickness   [m]  surfaces are grown by this much; a plane of zero thickness would otherwise fall
///                        between two voxel centres and the gas would pass straight through it
/// out   uint32_t    [-]  voxels newly marked solid, which is what a check asserts against
/// err   a reading that returns a large positive distance everywhere marks nothing, which is the correct
///       behaviour for a field that has not been raster-filled yet
/// note  ⚠️ Thickness must be at least half a voxel for thin geometry. At the default 8 m span that is 0.125 m,
///       and a thinner wall will leak regardless of what the distance reading says, because 32 cubed cannot
///       represent it. The leak is a resolution limit, not a bug in this routine.
/// cost  🔴  one distance reading per voxel; the dominant cost of admission ②
/// tag   api, nonallocating, nonthrowing
inline uint32_t AdmitDistanceReading(CoarseGasField& Field, SceneDistanceReading Reading,
                                     const void* Context, float Thickness) noexcept
{
    if (Reading == nullptr) return 0u;
    const float Size = VoxelSize(Field);
    const float Grown = Thickness > 0.0f ? Thickness : 0.5f * Size;

    uint32_t Marked = 0u;
    for (uint32_t Z = 0u; Z < CoarseExtent; ++Z)
    for (uint32_t Y = 0u; Y < CoarseExtent; ++Y)
    for (uint32_t X = 0u; X < CoarseExtent; ++X)
    {
        const uint32_t Slot = VoxelIndex(X, Y, Z);
        if (Field.Occupancy[Slot] > 0.5f) continue;
        const float Position[3] = { Field.Origin[0] + (static_cast<float>(X) + 0.5f) * Size,
                                    Field.Origin[1] + (static_cast<float>(Y) + 0.5f) * Size,
                                    Field.Origin[2] + (static_cast<float>(Z) + 0.5f) * Size };
        if (Reading(Context, Position) <= Grown)
        {
            Field.Occupancy[Slot] = 1.0f;
            ++Marked;
        }
    }
    return Marked;
}


/// 📦 Solid voxels in the field — the quantity the checks compare before and after an obstruction moves.
/// in    Field      [-]  the field
/// out   uint32_t   [-]  count of voxels marked solid
/// cost  🚩
/// tag   api, nonallocating, nonthrowing
inline uint32_t OccupiedVoxels(const CoarseGasField& Field) noexcept
{
    uint32_t Count = 0u;
    for (uint32_t Slot = 0u; Slot < CoarseVoxelCount; ++Slot)
        if (Field.Occupancy[Slot] > 0.5f) ++Count;
    return Count;
}

}   // namespace Frontier
