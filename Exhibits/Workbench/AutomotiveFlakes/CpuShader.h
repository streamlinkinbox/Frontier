#pragma once
#include "SlangCpuShim.h"
#include <stdexcept>
// Other, unused MaterialEvaluation entry points require these LUT declarations.
// Fail if accidentally used: this exhibit does not invent compensation LUT data.
inline vec3 FetchEnergy(float,float){throw std::logic_error("Unexpected energy LUT access");}
inline vec3 FetchSheen(float,float){throw std::logic_error("Unexpected sheen LUT access");}
inline vec4 FetchSheenFull(float,float){throw std::logic_error("Unexpected sheen LUT access");}
#include "MaterialEvaluation.slang"
#define FRONTIER_NATIVE_FLAKES 1
#include "AutomotiveFlakePaint.slang"
