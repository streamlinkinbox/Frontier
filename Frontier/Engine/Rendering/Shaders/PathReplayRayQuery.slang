/*==========================================================================================================================================
                                                 PATHREPLAYRAYQUERY.SLANG
==========================================================================================================================================*/
// 🧩 Compile primary, reservoir-generation, reservoir-resolve, and sample-history kernels with VK_KHR_ray_query inline RT.
// 📝 Shares 100% of BRDF, NEE, MIS, reservoir, seed-bank, and Vogel-disk logic with PathReplayReservoir/SampleHistory;
//    NearestFacet and ReplaySegmentOccluded dispatch RayQuery against binding 8 SceneAcceleration (SPIR-V 1.4 + spvRayQueryKHR).

#define FRONTIER_HARDWARE_RAY_QUERY 1
#include "PathReplaySampleHistory.slang"
