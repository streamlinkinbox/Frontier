//==============================================================================================================================================
//                                                       GASOBSTRUCTIONCONSENT.H
//==============================================================================================================================================
// 📦 Which objects the gas must flow around, as data an artist edits rather than a list someone compiled in — and how a moving one pushes it.
//
// GasCollisionIntake.h can already obstruct a field two ways: analytically from a primitive, or from whatever
//    answers "how far to the nearest surface". What it does not say is WHICH objects do either, and that is
//    deliberately not its business. This file is the answer, and the answer is a record per object:
//
//    | Level | What it is                  | Who opts in                          | Default |
//    |-------|-----------------------------|--------------------------------------|---------|
//    | ①     | Analytic primitive          | `Obstructs` with a shape             | off     |
//    | ②     | Global distance field       | `Obstructs` with `ByDistanceReading` | off     |
//    | ③     | Two-way, the gas pushes back| `Pushed`                             | off     |
//
// 🔴 EVERY DEFAULT IS OFF, AND THAT IS THE DELIVERABLE. A world where everything obstructs gas by default is
//    a world where one 32-cubed field walks every collider in the level each advance, and where turning the
//    cost off means editing code. A consent that has not been given costs one boolean comparison.
//
// 💡 THE THREE LEVELS COMPOSE AND ARE NOT RANKED. A crate may obstruct by its box and be pushed by the gas; a
//    cathedral obstructs through the distance field and is pushed by nothing. Level ③ is the toggle described
//    in GasWindContribution.h, and nothing here turns it on for anyone.
//
// 📐 A MOVING OBSTRUCTION PUSHES THROUGH ITS BOUNDARY, NOT THROUGH ITSELF. CoarseGasField.h holds still air
//    inside solids on purpose, so writing a velocity into solid voxels accomplishes nothing: the solver
//    clears it in the same advance. AdmitBoundaryMotion() instead writes the object's velocity into the OPEN
//    voxels it is about to sweep through, which is where the push belongs physically and is the only place
//    the projection will carry it from.
//
// ⚠️ Culling is the caller's job and this is the caller's tool. AdmitPrimitives() costs one comparison per
//    collider per voxel — 32768 of them — so handing it the level's whole collider list rather than the few
//    that overlap the domain is the difference between a free obstruction and a frame budget.

#pragma once

#include "GasCollisionIntake.h"
#include "GasWindContribution.h"

#include <cstdint>

namespace Frontier {

constexpr uint32_t kNoObstructingBody = 0xffffffffu;   // [-] - a consent nobody owns; valid, and used by the editor

// 🔴 THE ROUTING RULE, AND IT IS A RULE RATHER THAN A PREFERENCE.
//
//    | The object is                          | Level | Why                                                        |
//    |----------------------------------------|-------|------------------------------------------------------------|
//    | a cube, sphere, capsule, cylinder, ring| ①     | six signed distances, exact, no memory traffic, moves free  |
//    | a vehicle, a rock, a fractured wall    | ②     | nothing analytic describes it; the raster already knows it  |
//    | terrain                                | ②     | likewise, and it never moves, so the clipmap never restages |
//
//    AssignObstructionLevel() applies it from the one fact that decides it: whether a primitive was authored.
//    An artist does not get a third choice, because the two wrong answers are both expensive — a mesh forced
//    through level ① is a box that does not fit it, and a crate sent to level ② is 32768 clipmap samples to
//    rediscover six planes.
//
// ⚠️ TWO CAVEATS THE TABLE CANNOT STATE.
//    ① A MOVING OBJECT IS BETTER OFF ANALYTIC EVEN WHEN IT IS A MESH. The global distance field is staged for
//       a placement and restaged when the placement moves; sampling it for a car doing 30 m/s costs that
//       restage every frame and still lags by one. A vehicle is far better spent as a box and four tyre rings
//       — which is precisely why GasColliderShape::TyreRing exists — and ApproximatesWell() below says so.
//    ② GEOMETRY THINNER THAN HALF A VOXEL LEAKS AT EITHER LEVEL. At a 4 m domain that is 6 cm. A chain-link
//       fence does not obstruct gas on a 32-cubed lattice no matter which admission it arrives through, and
//       no amount of margin changes that: it is the lattice, not the admission.


// How far outside a collider the motion shell reaches, in voxels. One voxel is the least that can push at all
//    at this lattice, and more than two is an object shoving air it has not reached.
constexpr float GasMotionShellInVoxels = 1.25f;


// One object's answer to "does the gas know you are there, and do you know the gas is". Written by the object's
//    own inspector, saved with the scene, replicated like any other property — never inferred from a type.
struct GasObstructionConsent
{
    uint32_t         Carrier           = kNoObstructingBody;        // [-] - the object this belongs to
    bool             Obstructs         = false;                     // [-] - ① or ②: the gas flows around it
    bool             Pushed            = false;                     // [-] - ③: it feels the gas; see GasWindContribution.h
    bool             ByDistanceReading = false;                     // [-] - ②: arbitrary geometry rather than a primitive
    GasColliderShape Shape             = GasColliderShape::None;    // [-] - ① the primitive, when not ②
    float            Dimensions[3]     = { 0.5f, 0.5f, 0.5f };      // [m] - read per the shape
    float            Margin            = 0.0f;                      // [m] - obstruction grown outward
    float            Centre[3]         = { 0.0f, 0.0f, 0.0f };      // [m] - world centre this advance
    float            Velocity[3]       = { 0.0f, 0.0f, 0.0f };      // [m/s] - how it is moving this advance
};


/// 📦 Applies the routing rule above: a shape means level ①, no shape means level ②.
/// in    Consent   [-]  the record, edited in place
/// in    Shape     [-]  the primitive the object's own geometry is, or None for anything that is not one
/// out   -
/// note  this is the whole of the decision. Everything else about an object — whether it moves, how big it
///       is, how many of it there are — changes the cost, never the level
/// note  it does not tick Obstructs. Routing an object is not the same as consenting for it, and the one
///       thing an artist must still do by hand is say that the gas should know the object is there at all
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline void AssignObstructionLevel(GasObstructionConsent& Consent, GasColliderShape Shape) noexcept
{
    Consent.Shape             = Shape;
    Consent.ByDistanceReading = Shape == GasColliderShape::None;
}


/// 📦 Should this object be read in its OWN space rather than out of the composited world?
/// in    Consent    [-]  the record, already routed
/// in    Speed      [m/s]  how fast the object is moving, including any spin at its extremity
/// out   bool       [-]  true for a level ② object that moves at all
/// 🔴    THE COMPOSITED WORLD IS FOR THINGS THAT DO NOT MOVE. GlobalDistanceFieldSpace bakes placements into a
///       world volume and carries a translation and a scale and no rotation at all, so a car read out of it
///       is both stale and unturnable. GasSceneDistanceIntake.h reads a moving body by carrying the query
///       into the body's own space instead — the field is baked once and never rebaked, rotation is free
///       because it is the query that rotates, and a body at 30 m/s costs exactly what a parked one costs.
/// note  the threshold is a twentieth of a voxel of travel per advance at the common 4 m domain: anything
///       that moves perceptibly at all is better read locally, and the only objects worth compositing are
///       the ones that never move again
/// note  ⚠️ advice, not enforcement, and it does not rewrite anybody's object
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline bool PrefersLocalReading(const GasObstructionConsent& Consent, float Speed) noexcept
{
    constexpr float StillEnough = 0.375f;   // [m/s] - 1/20 voxel per advance at 60 Hz on a 4 m domain
    return Consent.ByDistanceReading && (Speed > StillEnough || Speed < -StillEnough);
}


/// 📦 Does this consent obstruct anything at all through a primitive?
/// in    Consent   [-]  the record
/// out   bool      [-]  true only when the object opted in AND carries a shape to be obstructed by
/// note  an object that opted in and has no shape is not an error: it is a crate the artist has ticked and
///       not yet given a box, and it obstructs nothing until they do
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline bool ObstructsByPrimitive(const GasObstructionConsent& Consent) noexcept
{
    return Consent.Obstructs && !Consent.ByDistanceReading && Consent.Shape != GasColliderShape::None;
}


/// 📦 Does this consent obstruct through the global distance field instead?
/// in    Consent   [-]  the record
/// out   bool      [-]  true for level ②
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline bool ObstructsByDistanceReading(const GasObstructionConsent& Consent) noexcept
{
    return Consent.Obstructs && Consent.ByDistanceReading;
}


/// 📦 Does the gas push this object back?
/// in    Consent   [-]  the record
/// out   bool      [-]  level ③, which is off until somebody says otherwise
/// note  spelled as its own question rather than read off the member, because every call site that asks it is
///       a place where a wrong default becomes a crate sliding across a room on its own
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline bool PushedByGas(const GasObstructionConsent& Consent) noexcept
{
    return Consent.Pushed;
}


/// 📦 The level ③ consent an object's authored coupling becomes, with the toggle taken from this record.
/// in    Consent    [-]  the object's obstruction record; its Pushed member is the toggle
/// in    Authored   [-]  the drag coefficient, area, mass and strength the artist set
/// out   GasCouplingConsent  [-]  the same tuning, enabled only if the object said so
/// 🔴    there must be exactly one place the two-way switch is read from, and this is it. A second copy of
///       the toggle living beside the first is how a feature that is off everywhere in the editor turns out
///       to be on for one crate in one level
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline GasCouplingConsent ConstructCouplingConsent(const GasObstructionConsent& Consent,
                                                   GasCouplingConsent Authored) noexcept
{
    Authored.CouplingEnabled = Consent.Pushed;
    return Authored;
}


/// 📦 The collider a primitive consent describes.
/// in    Consent      [-]  the record
/// out   GasCollider  [-]  shape, centre, dimensions and margin, as GasCollisionIntake.h wants them
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline GasCollider ConstructCollider(const GasObstructionConsent& Consent) noexcept
{
    GasCollider Collider;
    Collider.Shape         = Consent.Shape;
    Collider.Centre[0]     = Consent.Centre[0];
    Collider.Centre[1]     = Consent.Centre[1];
    Collider.Centre[2]     = Consent.Centre[2];
    Collider.Dimensions[0] = Consent.Dimensions[0];
    Collider.Dimensions[1] = Consent.Dimensions[1];
    Collider.Dimensions[2] = Consent.Dimensions[2];
    Collider.Margin        = Consent.Margin;
    return Collider;
}


/// 📦 How far a consent reaches from its centre, generously.
/// in    Consent   [-]  the record
/// out   float     [m]  a radius no part of the obstruction lies outside of
/// note  deliberately an over-estimate: the cull below must never drop a collider that does touch the domain,
///       and admitting one that does not costs a comparison, while dropping one that does leaves a hole in a wall
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline float ObstructionReach(const GasObstructionConsent& Consent) noexcept
{
    float Largest = Consent.Dimensions[0];
    if (Consent.Dimensions[1] > Largest) Largest = Consent.Dimensions[1];
    if (Consent.Dimensions[2] > Largest) Largest = Consent.Dimensions[2];

    // A box's corner is the furthest point of any shape here, and the ring's section adds to its radius.
    float Reach = Largest * 1.7321f;
    if (Consent.Shape == GasColliderShape::TyreRing) Reach = Consent.Dimensions[0] + Consent.Dimensions[1];
    return Reach + Consent.Margin;
}


/// 📦 Could this consent touch this field's cube at all?
/// in    Consent   [-]  the record
/// in    Field     [-]  the domain
/// out   bool      [-]  false only when it certainly cannot
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline bool ReachesDomain(const GasObstructionConsent& Consent, const CoarseGasField& Field) noexcept
{
    const float Reach = ObstructionReach(Consent);
    for (uint32_t Axis = 0u; Axis < 3u; ++Axis)
    {
        if (Consent.Centre[Axis] + Reach < Field.Origin[Axis]) return false;
        if (Consent.Centre[Axis] - Reach > Field.Origin[Axis] + Field.Span) return false;
    }
    return true;
}


/// 📦 The primitive colliders a field actually has to walk this advance.
/// in    Consents     [-]  every object's record, in scene order
/// in    Count        [-]  how many
/// in    Field        [-]  the domain being culled against
/// in    OutColliders [-]  destination
/// in    Room         [-]  how many fit
/// out   uint32_t     [-]  how many were written
/// err   a consent that does not fit is dropped from the END of scene order rather than displacing an earlier
///       one, so two machines with the same scene cull identically — a cull that depended on arrival order
///       would obstruct differently on each and the gas would not match
/// note  level ② consents are deliberately not returned: they obstruct through AdmitDistanceReading(), which
///       asks the scene once per voxel rather than once per object
/// cost  ✔️  linear in Count, no voxels touched
/// tag   api, nonallocating, nonthrowing
inline uint32_t ResolveObstructions(const GasObstructionConsent* Consents, uint32_t Count,
                                    const CoarseGasField& Field, GasCollider* OutColliders,
                                    uint32_t Room) noexcept
{
    if (Consents == nullptr || OutColliders == nullptr) return 0u;

    uint32_t Written = 0u;
    for (uint32_t Index = 0u; Index < Count && Written < Room; ++Index)
    {
        if (!ObstructsByPrimitive(Consents[Index])) continue;
        if (!ReachesDomain(Consents[Index], Field)) continue;
        OutColliders[Written++] = ConstructCollider(Consents[Index]);
    }
    return Written;
}


/// 📦 Writes a moving obstruction's velocity into the open air it is sweeping into.
/// in    Field      [-]  the field, with occupancy already admitted for this advance
/// in    Consents   [-]  every object's record
/// in    Count      [-]  how many
/// out   uint32_t   [-]  open voxels given the motion of something solid
/// err   a stationary obstruction writes nothing, so the common case costs one comparison per consent
/// note  ⚠️ This must run AFTER the occupancy admissions and BEFORE the advance, because it reads which voxels
///       are solid and writes into the ones that are not. Run before the admissions it would push air that is
///       about to be declared solid, which does nothing at all and looks like the shell being too thin
/// note  later consents overwrite earlier ones where two shells meet. Ascending order is the rule the whole
///       directory follows, and it is what makes the result identical on two machines
/// cost  🚩  Count passes over 32768 voxels, like the admissions themselves
/// tag   api, nonallocating, nonthrowing
inline uint32_t AdmitBoundaryMotion(CoarseGasField& Field, const GasObstructionConsent* Consents,
                                    uint32_t Count) noexcept
{
    if (Consents == nullptr) return 0u;
    const float Size  = VoxelSize(Field);
    const float Shell = Size * GasMotionShellInVoxels;

    uint32_t Pushed = 0u;
    for (uint32_t Index = 0u; Index < Count; ++Index)
    {
        const GasObstructionConsent& Consent = Consents[Index];
        if (!ObstructsByPrimitive(Consent) || !ReachesDomain(Consent, Field)) continue;

        const float Speed = Consent.Velocity[0] * Consent.Velocity[0]
                          + Consent.Velocity[1] * Consent.Velocity[1]
                          + Consent.Velocity[2] * Consent.Velocity[2];
        if (Speed <= 0.0f) continue;

        const GasCollider Collider = ConstructCollider(Consent);
        for (uint32_t Z = 0u; Z < CoarseExtent; ++Z)
        for (uint32_t Y = 0u; Y < CoarseExtent; ++Y)
        for (uint32_t X = 0u; X < CoarseExtent; ++X)
        {
            const uint32_t Slot = VoxelIndex(X, Y, Z);
            if (Field.Occupancy[Slot] > 0.5f) continue;      // inside: the solver will still it anyway

            const float Position[3] = { Field.Origin[0] + (static_cast<float>(X) + 0.5f) * Size,
                                        Field.Origin[1] + (static_cast<float>(Y) + 0.5f) * Size,
                                        Field.Origin[2] + (static_cast<float>(Z) + 0.5f) * Size };
            const float Distance = ColliderDistance(Collider, Position);
            if (Distance < 0.0f || Distance > Shell) continue;

            Field.VelocityX[Slot] = Consent.Velocity[0];
            Field.VelocityY[Slot] = Consent.Velocity[1];
            Field.VelocityZ[Slot] = Consent.Velocity[2];
            ++Pushed;
        }
    }
    return Pushed;
}


/// 📦 The whole obstruction step for one field: clear, admit the primitives that reach it, admit the scene's
///    own geometry for anyone who asked for level ②, then let whatever is moving push the air in front of it.
/// in    Field       [-]  the field
/// in    Consents    [-]  every object's record
/// in    Count       [-]  how many
/// in    Reading     [-]  the scene distance function, or null when nothing asked for level ②
/// in    Context     [-]  passed to the reading untouched
/// in    Room        [-]  how many primitives may be walked; the tail beyond it is dropped in scene order
/// in    OutColliders[-]  scratch the caller owns, Room long
/// out   uint32_t    [-]  solid voxels after both admissions
/// note  the order is the one the file header argues for and is not interchangeable
/// cost  🔴  the dominant per-advance cost of obstruction
/// tag   api, nonallocating, nonthrowing
inline uint32_t AdmitObstructions(CoarseGasField& Field, const GasObstructionConsent* Consents, uint32_t Count,
                                  SceneDistanceReading Reading, const void* Context,
                                  GasCollider* OutColliders, uint32_t Room) noexcept
{
    ClearOccupancy(Field);

    const uint32_t Walked = ResolveObstructions(Consents, Count, Field, OutColliders, Room);
    if (Walked > 0u) AdmitPrimitives(Field, OutColliders, Walked);

    bool WantsScene = false;
    for (uint32_t Index = 0u; Index < Count && Consents != nullptr; ++Index)
    {
        if (ObstructsByDistanceReading(Consents[Index])) { WantsScene = true; break; }
    }
    if (WantsScene && Reading != nullptr) AdmitDistanceReading(Field, Reading, Context, 0.0f);

    AdmitBoundaryMotion(Field, Consents, Count);
    return OccupiedVoxels(Field);
}

}   // namespace Frontier
