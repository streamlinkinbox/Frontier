//============================================================================================================================================
// 📦 Frontier/DeviceExchange/VendorClassifier.cpp — Hardware Classification Implementation
//============================================================================================================================================

#include "VendorClassifier.h"

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

VendorClassifier::VendorClassifier() noexcept
    : Capabilities{ HardwareCategory::Category2_Ada_RayTracing, 16ULL * 1024ULL * 1024ULL * 1024ULL, 1024, true, true, true }
{
}

//------------------------------------------------------------------------------------------------------------------------
//                                              CLASSIFICATION OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

VendorCapabilities VendorClassifier::ClassifyDevice(uint32_t VendorId, uint32_t DeviceId) const noexcept
{
    (void)DeviceId;
    VendorCapabilities Result{};
    Result.ComputeWorkGroupSize = 1024;
    Result.LocalMemoryBytes     = 8ULL * 1024ULL * 1024ULL * 1024ULL;

    if (VendorId == 0x10DE)
    {
        Result.Category              = HardwareCategory::Category2_Ada_RayTracing;
        Result.RayQuerySupport       = true;
        Result.MeshletShaderSupport  = true;
        Result.AtomicFloatSupport    = true;
    }
    else
    {
        Result.Category              = HardwareCategory::Category1_Pascal_Compute;
        Result.RayQuerySupport       = false;
        Result.MeshletShaderSupport  = false;
        Result.AtomicFloatSupport    = false;
    }

    return Result;
}

const char* VendorClassifier::QueryVendorName(uint32_t VendorId) const noexcept
{
    switch (VendorId)
    {
        case 0x10DE: return "NVIDIA";
        case 0x1002: return "AMD";
        case 0x8086: return "Intel";
        default:     return "Generic";
    }
}

} // namespace Frontier
