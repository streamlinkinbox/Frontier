//============================================================================================================================================
// 📦 Frontier/DeviceExchange/RenderTargetExchange.h — Multi-Viewport Offscreen Vulkan Render Target and Texture Binding Exchange
//============================================================================================================================================

#pragma once

#if defined(_MSC_VER)
    #pragma warning(disable: 4324)                              // Disable structure padding alignment warning under /WX
#endif

#include "OrientationClassifier.h"
#include <cstdint>
#include <string>
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                             RENDER TARGET CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

struct RenderTargetConfiguration
{
    uint32_t                TargetWidth;                        // [px] horizontal offscreen resolution
    uint32_t                TargetHeight;                       // [px] vertical offscreen resolution
    uint32_t                ColorFormatToken;                   // [format] Vulkan VkFormat enum token (e.g. VK_FORMAT_R8G8B8A8_UNORM)
    uint32_t                SampleCountToken;                   // [samples] multisample count (1, 2, 4, 8)
    bool                    DepthStencilEnabled;                // [bool] true if depth attachment is required
    bool                    SampledTextureEnabled;              // [bool] true if bound to ImGui viewport as ImTextureID
};

//------------------------------------------------------------------------------------------------------------------------
//                                               RENDER TARGET EXCHANGE
//------------------------------------------------------------------------------------------------------------------------

class RenderTargetExchange
{
public:
    RenderTargetExchange() noexcept;
    explicit RenderTargetExchange(const RenderTargetConfiguration& Config) noexcept;
    ~RenderTargetExchange() noexcept;

    RenderTargetExchange(const RenderTargetExchange&) = delete;
    RenderTargetExchange& operator=(const RenderTargetExchange&) = delete;

    [[nodiscard]] bool      AllocateTargetResources(const RenderTargetConfiguration& Config) noexcept;
    void                    ReleaseTargetResources() noexcept;

    void                    AdaptResolution(uint32_t NewWidth, uint32_t NewHeight) noexcept;

    [[nodiscard]] uint32_t  QueryWidth() const noexcept { return Configuration.TargetWidth; }
    [[nodiscard]] uint32_t  QueryHeight() const noexcept { return Configuration.TargetHeight; }
    [[nodiscard]] float     QueryAspectRatio() const noexcept;
    [[nodiscard]] void*     QueryColorImageViewToken() const noexcept { return ColorImageViewToken; }
    [[nodiscard]] void*     QueryTextureDescriptorToken() const noexcept { return TextureDescriptorToken; }
    [[nodiscard]] void*     QueryFramebufferToken() const noexcept { return FramebufferToken; }
    [[nodiscard]] bool      IsAllocated() const noexcept { return AllocatedCondition; }

    // Single unified conversion operator for texture descriptor token
    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

private:
    RenderTargetConfiguration Configuration;                    // [config] active target parameters
    void*                   ColorImageToken;                    // [token] VkImage handle
    void*                   ColorImageMemoryToken;              // [token] VkDeviceMemory handle
    void*                   ColorImageViewToken;                // [token] VkImageView handle
    void*                   DepthImageToken;                    // [token] VkImage depth handle
    void*                   DepthImageViewToken;                // [token] VkImageView depth handle
    void*                   FramebufferToken;                   // [token] VkFramebuffer handle
    void*                   SamplerToken;                       // [token] VkSampler handle
    void*                   TextureDescriptorToken;             // [token] VkDescriptorSet / ImTextureID
    bool                    AllocatedCondition;                 // [bool] allocation validity status
};

template<>
inline void* RenderTargetExchange::Convert<void*>() const noexcept
{
    return TextureDescriptorToken;
}

template<>
inline bool RenderTargetExchange::Convert<bool>() const noexcept
{
    return AllocatedCondition;
}

} // namespace Frontier
