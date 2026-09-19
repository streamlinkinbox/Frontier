//============================================================================================================================================
// 📦 Frontier/DeviceExchange/VendorClassifier.h — Hardware Capability Classification and Feature Profiling
//============================================================================================================================================

#pragma once

#include <cstdint>
#include <string_view>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                  HARDWARE CATEGORY
//------------------------------------------------------------------------------------------------------------------------

enum class HardwareCategory : uint32_t
{
    Category1_Pascal_Compute            = 1,                    // Basic compute, screen-space fallback
    Category2_Ada_RayTracing            = 2                     // Hardware ray query, meshlet shaders
};

//------------------------------------------------------------------------------------------------------------------------
//                                                VENDOR CAPABILITIES
//------------------------------------------------------------------------------------------------------------------------

struct VendorCapabilities
{
    HardwareCategory        Category;                           // [category] hardware classification rank
    uint64_t                LocalMemoryBytes;                   // [B] dedicated device memory in bytes
    uint32_t                ComputeWorkGroupSize;               // [threads] maximum local invocation size
    bool                    RayQuerySupport;                    // [bool] inline ray tracing in compute
    bool                    MeshletShaderSupport;               // [bool] hardware task/mesh shader
    bool                    AtomicFloatSupport;                 // [bool] native 32-bit float atomics
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 VENDOR CLASSIFIER
//------------------------------------------------------------------------------------------------------------------------

class VendorClassifier
{
public:
    VendorClassifier() noexcept;
    ~VendorClassifier() noexcept = default;

    [[nodiscard]] VendorCapabilities ClassifyDevice(uint32_t VendorId, uint32_t DeviceId) const noexcept;
    [[nodiscard]] const char*        QueryVendorName(uint32_t VendorId) const noexcept;

    // Single unified conversion operator for capabilities
    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

private:
    VendorCapabilities      Capabilities;                       // [struct] cached hardware capabilities
};

template<>
inline VendorCapabilities VendorClassifier::Convert<VendorCapabilities>() const noexcept
{
    return Capabilities;
}

template<>
inline HardwareCategory VendorClassifier::Convert<HardwareCategory>() const noexcept
{
    return Capabilities.Category;
}

} // namespace Frontier
