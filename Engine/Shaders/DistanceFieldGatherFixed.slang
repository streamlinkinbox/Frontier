//============================================================================================================================================
//                                                       DISTANCEFIELDGATHERFIXED.SLANG
//============================================================================================================================================
// 📦 Incoming diffuse irradiance gather using shared production materials and transport.

#version 460
#extension GL_GOOGLE_include_directive : require
#define FRONTIER_CONSTANT_MATERIALS 1
#include "DistanceFieldGatherBody.slang"
