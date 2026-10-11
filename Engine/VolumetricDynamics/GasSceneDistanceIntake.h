//==============================================================================================================================================
//                                                      GASSCENEDISTANCEINTAKE.H
//==============================================================================================================================================
// 📦 Level ②, bound to the engine's own global distance field — the one file that knows both the gas solver and the raster exist.
//
// GasCollisionIntake.h takes obstruction from a function pointer and a context so that the solver can run in
//    the headless checks with no raster linked. It prints the adapter in its own header as a comment and
//    leaves writing it to whoever has both. This is that file, and it is the whole of it.
//
// 💡 ARBITRARY GEOMETRY OBSTRUCTS GAS WITH NO NEW CODE IN THE SOLVER. A cathedral, a fractured wall, a tree —
//    anything already registered with GlobalDistanceFieldSpace answers "how far to the nearest surface", which
//    is the only question the admission asks. Nothing has to author a primitive for it.
//
// ⚠️ A SAMPLE IS A CLIPMAP LOOKUP AND THE ADMISSION TAKES 32768 OF THEM. That is the price of level ② and the
//    reason level ① exists: a crate is a box, and a box is six signed distances with no memory traffic. Give
//    the distance field the geometry that has no primitive, and only that.
//
// 📐 Both sides are metres, both put +Z up, and both call a negative distance "inside". There is nothing to
//    convert, which is why this file is a function and not a layer.

#pragma once

#include "GasCollisionIntake.h"

#include "../GeometricRaster/GlobalDistanceFieldSpace.h"

namespace Frontier {

/// 📦 The scene's distance at a world position, in the shape GasCollisionIntake.h wants it.
/// in    Context    [-]  a const GlobalDistanceFieldSpace*; null answers "nothing near", not a crash
/// in    Position   [m]  world position of a voxel centre
/// out   float      [m]  distance to the nearest surface; negative inside geometry
/// note  spelled against the composited global volume rather than the cascades, because a gas domain is a few
///       metres across and sits wherever it was authored, not wherever the camera happens to be looking
/// cost  🔴  one clipmap sample; the admission calls this per voxel
/// tag   api, nonallocating, nonthrowing
inline float ReadSceneDistance(const void* Context, const float Position[3]) noexcept
{
    const GlobalDistanceFieldSpace* const Scene = static_cast<const GlobalDistanceFieldSpace*>(Context);
    if (Scene == nullptr) return 1.0e6f;
    return Scene->SampleSceneDistance(Vector3{ Position[0], Position[1], Position[2] });
}


//------------------------------------------------------------------------------------------------------------------------
//                                          A BODY READ IN ITS OWN LOCAL SPACE
//------------------------------------------------------------------------------------------------------------------------

// 🔴 A MOVING BODY IS NOT RESTAGED. IT IS ASKED IN ITS OWN SPACE.
//
//    GlobalDistanceFieldSpace composites by rasterising every placement into a world volume — UpdateGlobalGrid()
//    is a triple loop over the whole grid — and a placement carries a translation and a scale and NO ROTATION.
//    For static geometry that is the right trade: bake once, sample cheaply, forever. For a car it is the wrong
//    one twice over: the composite is stale the moment the car moves, and it could not have turned the car
//    anyway.
//
//    The alternative costs nothing and is what Unreal does with its mesh distance fields: the object's field is
//    baked ONCE in its own local space and never rebaked; a world query is carried INTO that space by the
//    inverse of the body's transform, sampled there, and the answer carried back out by the scale. A body that
//    moves 30 m/s and spins is exactly as cheap as one standing still, and rotation is free because it is the
//    query that rotates, not the geometry.
//
//        local    = Basisᵀ · (world − Translation) / Scale
//        distance = Local->SampleDistance(local) · Scale
//
// 📐 The basis is column-major and must be orthonormal — it is a rotation, so its transpose is its inverse, and
//    that is the only reason the query is three dot products rather than a matrix solve. A non-uniform scale is
//    deliberately not offered: it would make the sampled distance wrong by a direction-dependent factor, and a
//    distance field that lies about distance is worse than no obstruction at all.

struct GasRigidDistanceBody
{
    const DistanceFieldSpace* Local = nullptr;                  // [-]   - baked once, in object space
    float Translation[3] = { 0.0f, 0.0f, 0.0f };                // [m]   - where its origin sits in the world
    float Basis[9]       = { 1.0f, 0.0f, 0.0f,                  // [-]   - column-major, orthonormal
                             0.0f, 1.0f, 0.0f,
                             0.0f, 0.0f, 1.0f };
    float Scale          = 1.0f;                                // [-]   - uniform, and uniform only
    float Velocity[3]    = { 0.0f, 0.0f, 0.0f };                // [m/s] - of its origin
    float Spin[3]        = { 0.0f, 0.0f, 0.0f };                // [rad/s] - about its origin, world axes
};


// Everything a field may be obstructed by through a distance reading: any number of bodies in their own
//    spaces, plus the static scene in its composited one. The two are not alternatives; a car drives across
//    terrain.
struct GasRigidDistanceScene
{
    const GasRigidDistanceBody*     Bodies = nullptr;
    uint32_t                        Count  = 0u;
    const GlobalDistanceFieldSpace* Static = nullptr;   // [-] - the baked world, or null
};


/// 📦 A world position carried into a body's own space.
/// in    Body         [-]  the body
/// in    Position     [m]  world position
/// out   OutLocal     [-]  the same point in the body's space, scale divided out
/// cost  ✔️  three dot products
/// tag   internal, nonallocating, nonthrowing
inline void CrossIntoBody(const GasRigidDistanceBody& Body, const float Position[3], float OutLocal[3]) noexcept
{
    const float Offset[3] = { Position[0] - Body.Translation[0],
                              Position[1] - Body.Translation[1],
                              Position[2] - Body.Translation[2] };
    const float Divide = Body.Scale != 0.0f ? 1.0f / Body.Scale : 1.0f;
    for (uint32_t Axis = 0u; Axis < 3u; ++Axis)
    {
        // The transpose of an orthonormal basis is its inverse, so this row is that column.
        OutLocal[Axis] = (Body.Basis[Axis * 3u + 0u] * Offset[0]
                        + Body.Basis[Axis * 3u + 1u] * Offset[1]
                        + Body.Basis[Axis * 3u + 2u] * Offset[2]) * Divide;
    }
}


/// 📦 The distance to one body, asked in the body's own space and answered in the world's.
/// in    Body       [-]  the body; a null field answers "nothing near"
/// in    Position   [m]  world position
/// out   float      [m]  distance to that body's surface, negative inside
/// note  the body is never restaged, rebaked or re-registered, however fast it moves or turns
/// cost  ✔️  one local sample
/// tag   api, nonallocating, nonthrowing
inline float ReadBodyDistance(const GasRigidDistanceBody& Body, const float Position[3]) noexcept
{
    if (Body.Local == nullptr) return 1.0e6f;
    float Local[3];
    CrossIntoBody(Body, Position, Local);
    return Body.Local->SampleDistance(Vector3{ Local[0], Local[1], Local[2] }) * Body.Scale;
}


/// 📦 The nearest surface of anything at all: every moving body in its own space, and the static world in its.
/// in    Context    [-]  a const GasRigidDistanceScene*; null answers "nothing near"
/// in    Position   [m]  world position
/// out   float      [m]  the smallest distance of all of them
/// note  the minimum is taken in ascending body order and the static scene last, because float comparison of
///       equals keeps the first and two machines must keep the same one
/// cost  🔴  one sample per body per voxel, plus one clipmap sample; this is the price of level ②
/// tag   api, nonallocating, nonthrowing
inline float ReadRigidSceneDistance(const void* Context, const float Position[3]) noexcept
{
    const GasRigidDistanceScene* const Scene = static_cast<const GasRigidDistanceScene*>(Context);
    if (Scene == nullptr) return 1.0e6f;

    float Nearest = 1.0e6f;
    for (uint32_t Index = 0u; Index < Scene->Count && Scene->Bodies != nullptr; ++Index)
    {
        const float Reading = ReadBodyDistance(Scene->Bodies[Index], Position);
        if (Reading < Nearest) Nearest = Reading;
    }
    if (Scene->Static != nullptr)
    {
        const float Reading = Scene->Static->SampleSceneDistance(Vector3{ Position[0], Position[1], Position[2] });
        if (Reading < Nearest) Nearest = Reading;
    }
    return Nearest;
}


/// 📦 Writes each moving body's surface velocity into the open air it is sweeping into.
/// in    Field        [-]  the field, with occupancy already admitted this advance
/// in    Scene        [-]  the bodies; the static world has no velocity and is skipped
/// in    ShellVoxels  [-]  how far outside a surface the push reaches, in voxels
/// out   uint32_t     [-]  open voxels given the motion of something solid
/// note  the velocity at a point is the origin's plus the spin crossed into the arm, so a wheel that is
///       turning but not travelling still drags air around itself — which is the whole difference between a
///       spinning tyre and a parked one
/// note  ⚠️ as with the primitive version: after the admissions, before the advance
/// cost  🔴  one body sample per voxel
/// tag   api, nonallocating, nonthrowing
inline uint32_t AdmitBodyMotion(CoarseGasField& Field, const GasRigidDistanceScene& Scene,
                                float ShellVoxels = 1.25f) noexcept
{
    if (Scene.Bodies == nullptr || Scene.Count == 0u) return 0u;
    const float Size  = VoxelSize(Field);
    const float Shell = Size * (ShellVoxels > 0.0f ? ShellVoxels : 1.25f);

    uint32_t Pushed = 0u;
    for (uint32_t Index = 0u; Index < Scene.Count; ++Index)
    {
        const GasRigidDistanceBody& Body = Scene.Bodies[Index];
        if (Body.Local == nullptr) continue;
        const bool Moving = Body.Velocity[0] != 0.0f || Body.Velocity[1] != 0.0f || Body.Velocity[2] != 0.0f
                         || Body.Spin[0] != 0.0f || Body.Spin[1] != 0.0f || Body.Spin[2] != 0.0f;
        if (!Moving) continue;

        for (uint32_t Z = 0u; Z < CoarseExtent; ++Z)
        for (uint32_t Y = 0u; Y < CoarseExtent; ++Y)
        for (uint32_t X = 0u; X < CoarseExtent; ++X)
        {
            const uint32_t Slot = VoxelIndex(X, Y, Z);
            if (Field.Occupancy[Slot] > 0.5f) continue;

            const float Position[3] = { Field.Origin[0] + (static_cast<float>(X) + 0.5f) * Size,
                                        Field.Origin[1] + (static_cast<float>(Y) + 0.5f) * Size,
                                        Field.Origin[2] + (static_cast<float>(Z) + 0.5f) * Size };
            const float Distance = ReadBodyDistance(Body, Position);
            if (Distance < 0.0f || Distance > Shell) continue;

            const float Arm[3] = { Position[0] - Body.Translation[0],
                                   Position[1] - Body.Translation[1],
                                   Position[2] - Body.Translation[2] };
            Field.VelocityX[Slot] = Body.Velocity[0] + Body.Spin[1] * Arm[2] - Body.Spin[2] * Arm[1];
            Field.VelocityY[Slot] = Body.Velocity[1] + Body.Spin[2] * Arm[0] - Body.Spin[0] * Arm[2];
            Field.VelocityZ[Slot] = Body.Velocity[2] + Body.Spin[0] * Arm[1] - Body.Spin[1] * Arm[0];
            ++Pushed;
        }
    }
    return Pushed;
}


/// 📦 Admits the whole scene's geometry into one field as obstruction.
/// in    Field       [-]  the field
/// in    Scene       [-]  the global distance field; null admits nothing and is not an error
/// in    Thickness   [m]  surfaces grown by this much; zero asks for half a voxel, which is the thinnest
///                        wall this lattice can hold at all
/// out   uint32_t    [-]  voxels newly marked solid
/// cost  🔴  one distance sample per voxel
/// tag   api, nonallocating, nonthrowing
inline uint32_t AdmitSceneGeometry(CoarseGasField& Field, const GlobalDistanceFieldSpace* Scene,
                                   float Thickness = 0.0f) noexcept
{
    if (Scene == nullptr) return 0u;
    return AdmitDistanceReading(Field, &ReadSceneDistance, Scene, Thickness);
}

}   // namespace Frontier
