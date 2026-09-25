#pragma once
#include "GpuSurfaceData.h"
#include <vulkan/vulkan.h>
#include <memory>
namespace Frontier::ProjectFluid {
struct GpuSurfaceTiming {double FieldMs{},EdgesMs{},TrianglesMs{},HostWaitMs{};bool Timestamps{};uint32_t Indices{},Overflow{};};
// Borrowed device/queue; the caller must serialize submissions on that queue.
// Output buffers remain resident. Readback is explicitly opt-in for tests/export.
class GpuSurfaceExtractor {
public:
    GpuSurfaceExtractor(VkPhysicalDevice physical,VkDevice device,VkQueue queue,uint32_t family,const char* shader);
    ~GpuSurfaceExtractor();
    GpuSurfaceTiming Update(const SurfaceGpuInput& input);
    SurfaceGpuOutput Readback();
    VkBuffer VertexBuffer() const;
    VkBuffer IndexBuffer() const;
    uint64_t AllocatedBytes() const;
private:
    struct Implementation;std::unique_ptr<Implementation> P;
};
int RunFluidGpuTest(int argc,char** argv);
}
