//============================================================================================================================================
//                                                         RAYQUERYVIEWPORT.SLANG
//============================================================================================================================================
// 📦 ReSTIR shading with inline Vulkan hardware triangle traversal; software shading and material semantics are unchanged.

#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_ray_query : require
#define FRONTIER_RAYQUERY
#include "ViewportIntegrator.slang"
