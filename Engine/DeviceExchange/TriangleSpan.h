//============================================================================================================================================
//                                           DeviceExchange/TriangleSpan.h — Object Span over Triangle Soup
//============================================================================================================================================
// A TriangleSpanRecord marks one scene object's triangle range in the CPU builder's soup (first triangle +
//    count + display name). It lived in SwapchainExchange.h, which requires the Vulkan SDK; it is a plain
//    data record with no GPU dependency, so it moves here where both the GPU headers and the Vulkan-free CPU
//    reference (TracingIndex.h) can share the single definition.

#pragma once

#include <cstdint>
#include <string>

namespace Frontier {

struct TriangleSpanRecord
{
    uint32_t    FirstTriangle = 0u;      // [idx] first triangle of the object in the builder's soup
    uint32_t    TriangleCount = 0u;      // [cnt]
    std::string Name;                    // [-]   display name ("Tall Box")
    bool        Dynamic = false;         // [-]   the object moves (--animate / physics drive it)
};

} // namespace Frontier
