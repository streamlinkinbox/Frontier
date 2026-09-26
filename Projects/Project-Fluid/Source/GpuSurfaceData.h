#pragma once
#include "SurfaceReconstruction.h"
#include <array>
#include <cstddef>
#include <vector>
namespace Frontier::ProjectFluid {
struct alignas(16) Float4 {float x{},y{},z{},w{};};
struct alignas(16) SurfaceGpuKernel {Float4 Centre,A,B,C;};
struct alignas(16) SurfaceGpuVertex {Float4 Position,Normal;};
struct alignas(16) SurfaceGpuParameters {
    std::array<uint32_t,4> Grid{65,65,45,0};
    Float4 OriginSpacing{-2.24f,-.16f,-1.54f,.07f};
    std::array<uint32_t,4> Config{600000,0,8,0}; // index capacity, stage, brick width, reserved
    Float4 Scalar{.075f,0,0,0};
};
static_assert(sizeof(SurfaceGpuKernel)==64&&sizeof(SurfaceGpuVertex)==32&&sizeof(SurfaceGpuParameters)==64);
struct SurfaceGpuInput {
    SurfaceGpuParameters Parameters;
    std::vector<SurfaceGpuKernel> Kernels;
    std::vector<uint32_t> Offsets,Ids;
};
struct SurfaceGpuOutput {
    std::vector<float> Field;
    std::vector<SurfaceGpuVertex> Vertices; // 3 deterministic lattice-edge slots per node
    std::vector<uint32_t> Indices;
    uint32_t RequestedIndices{},Overflow{};
};
SurfaceGpuInput PrepareGpuSurface(const std::vector<SurfaceKernel>& kernels);
SurfaceGpuOutput MirrorGpuSurface(const SurfaceGpuInput& input);
void SaveGpuSurfaceObj(const SurfaceGpuOutput& mesh,const char* path);
}
