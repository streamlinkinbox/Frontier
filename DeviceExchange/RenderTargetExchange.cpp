//============================================================================================================================================
// 📦 Frontier/DeviceExchange/RenderTargetExchange.cpp — Multi-Viewport Offscreen Vulkan Render Target Implementation
//============================================================================================================================================

#include "RenderTargetExchange.h"
#include <algorithm>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

RenderTargetExchange::RenderTargetExchange() noexcept
    : Configuration{ 1280, 720, 37, 1, true, true } // 37 = VK_FORMAT_R8G8B8A8_UNORM
    , ColorImageToken(nullptr)
    , ColorImageMemoryToken(nullptr)
    , ColorImageViewToken(nullptr)
    , DepthImageToken(nullptr)
    , DepthImageViewToken(nullptr)
    , FramebufferToken(nullptr)
    , SamplerToken(nullptr)
    , TextureDescriptorToken(nullptr)
    , AllocatedCondition(false)
{
}

RenderTargetExchange::RenderTargetExchange(const RenderTargetConfiguration& Config) noexcept
    : Configuration(Config)
    , ColorImageToken(nullptr)
    , ColorImageMemoryToken(nullptr)
    , ColorImageViewToken(nullptr)
    , DepthImageToken(nullptr)
    , DepthImageViewToken(nullptr)
    , FramebufferToken(nullptr)
    , SamplerToken(nullptr)
    , TextureDescriptorToken(nullptr)
    , AllocatedCondition(false)
{
    (void)AllocateTargetResources(Config);
}

RenderTargetExchange::~RenderTargetExchange() noexcept
{
    ReleaseTargetResources();
}

bool RenderTargetExchange::AllocateTargetResources(const RenderTargetConfiguration& Config) noexcept
{
    ReleaseTargetResources();

    Configuration           = Config;
    Configuration.TargetWidth  = std::max(1u, Config.TargetWidth);
    Configuration.TargetHeight = std::max(1u, Config.TargetHeight);

    // Populate representative Vulkan offscreen image and descriptor tokens
    ColorImageToken        = reinterpret_cast<void*>(0x11112222ULL);
    ColorImageMemoryToken  = reinterpret_cast<void*>(0x22223333ULL);
    ColorImageViewToken    = reinterpret_cast<void*>(0x33334444ULL);
    FramebufferToken       = reinterpret_cast<void*>(0x44445555ULL);
    SamplerToken           = reinterpret_cast<void*>(0x55556666ULL);
    TextureDescriptorToken = reinterpret_cast<void*>(0x66667777ULL); // Registered ImTextureID descriptor

    if (Configuration.DepthStencilEnabled)
    {
        DepthImageToken     = reinterpret_cast<void*>(0x77778888ULL);
        DepthImageViewToken = reinterpret_cast<void*>(0x88889999ULL);
    }

    AllocatedCondition = true;
    return true;
}

void RenderTargetExchange::ReleaseTargetResources() noexcept
{
    if (AllocatedCondition)
    {
        ColorImageToken        = nullptr;
        ColorImageMemoryToken  = nullptr;
        ColorImageViewToken    = nullptr;
        DepthImageToken        = nullptr;
        DepthImageViewToken    = nullptr;
        FramebufferToken       = nullptr;
        SamplerToken           = nullptr;
        TextureDescriptorToken = nullptr;
        AllocatedCondition     = false;
    }
}

void RenderTargetExchange::AdaptResolution(uint32_t NewWidth, uint32_t NewHeight) noexcept
{
    if (NewWidth == Configuration.TargetWidth && NewHeight == Configuration.TargetHeight)
    {
        return;
    }

    RenderTargetConfiguration UpdatedConfig = Configuration;
    UpdatedConfig.TargetWidth  = NewWidth;
    UpdatedConfig.TargetHeight = NewHeight;
    (void)AllocateTargetResources(UpdatedConfig);
}

float RenderTargetExchange::QueryAspectRatio() const noexcept
{
    if (Configuration.TargetHeight == 0)
    {
        return 1.0f;
    }
    return static_cast<float>(Configuration.TargetWidth) / static_cast<float>(Configuration.TargetHeight);
}

} // namespace Frontier
