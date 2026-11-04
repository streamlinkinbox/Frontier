//============================================================================================================================================
//                                                         DISTANCELAYOUTS.SLANG
//============================================================================================================================================
// 🧩 Cascade profiles of the scene distance field, common-domain by the union dispatch that writes it and every dispatch that traverses it.

#pragma once
#include "IntrinsicParity.slang"

struct ClipProfile
{
    int3 OriginVoxel;           // [voxel] world voxel coordinate of the window's minimum corner
    uint Resolution;              // [voxel] lattice edge
    float VoxelExtent;             // [m]
    float Band;                    // [m]     truncation distance written by the union dispatch
    float InverseWorldExtent;      // [1/m]   1 / (Resolution · VoxelExtent)
    float Pad;                
};

struct DistanceConstants
{
    ClipProfile Clips[4];                 
    uint ClipCount;                  // [-]
    uint DistanceOccupantCount;      // [-]  occupants [0, count) own distance volumes
    float GroundHeight;               // [m]  analytic course plane
    float HitFraction;                // [-]  surface threshold as a fraction of the voxel edge
    uint BrickCount;                 // [-]  bricks the union dispatch rewrites this refresh
    uint RefreshIndex;                 // [-]  stamped into every rewritten brick
    uint BricksPerAxis;              // [-]  Resolution / BrickEdge
    uint Pad;                   
};

static const uint BrickEdge = 8u;
static const uint SkyIdentity = 2045u;
static const uint GroundIdentity = 2046u;
static const uint VacantIdentity = 2047u;

// 📝 Toroidal brick slot of a world brick in one cascade — the same wrap the voxels use, so an epoch follows its brick.
uint ResolveBrickSlot(uint Level, int3 WorldBrick, uint BricksPerAxis)
{
    let Edge = ToSigned(BricksPerAxis);
    let Wrapped = uint3(((WorldBrick % Edge) + Edge) % Edge);
    return Level * BricksPerAxis * BricksPerAxis * BricksPerAxis + (Wrapped.z * BricksPerAxis + Wrapped.y) * BricksPerAxis + Wrapped.x;
}

// 📝 A brick epoch is the refresh of its last rewrite in the low 31 bits; bit 31 profiles that a moving occupant caused
//    it, as opposed to the cascade window scrolling over it. The inspection views glow the two causes differently.
static const uint OccupantEpochBit = 0x80000000u;
static const uint VacantEpoch = 0xFFFFFFFFu;

uint ConstructBrickEpoch(uint RefreshIndex, bool OccupantCaused)
{
    return (RefreshIndex & 0x7FFFFFFFu) | SelectBranch(0u, OccupantEpochBit, OccupantCaused);
}

// 📝 Returns (refreshes since the rewrite, 1 when a moving occupant caused it); a never-written brick is infinitely old.
float2 ResolveBrickAge(uint Epoch, uint RefreshIndex)
{
    if (Epoch == VacantEpoch)
    {
        return float2(1.0e9, 0.0);
    }
    let Written = Epoch & 0x7FFFFFFFu;
    return float2(float((RefreshIndex & 0x7FFFFFFFu) - Written), SelectBranch(0.0, 1.0, (Epoch & OccupantEpochBit) != 0u));
}
