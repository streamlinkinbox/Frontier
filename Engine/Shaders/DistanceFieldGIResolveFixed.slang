//============================================================================================================================================
//                                                     DISTANCEFIELDGIRESOLVE.SLANG
//============================================================================================================================================
// 📦 Raster primary visibility, shared OpenPBR materials, distance-field diffuse transport and exact mesh reflections/refraction.

#version 460
#extension GL_GOOGLE_include_directive : require
#define FRONTIER_CONSTANT_MATERIALS 1
#include "DistanceFieldGIResolveBody.slang"
