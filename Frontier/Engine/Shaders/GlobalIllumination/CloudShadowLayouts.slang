
#pragma once
#include "IntrinsicParity.slang"
struct CloudShadowParameters {
 float4 Map;      // world XY origin on Z=0, span, enabled
 float4 Sun;      // frozen sun direction used to generate the map, strength
 float4 Quality;  // softness (m), sample count, global base/top
};
