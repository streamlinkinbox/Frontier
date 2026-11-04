/*====================================================================================================================================
                                                           SOLIDARCFRAME.H
====================================================================================================================================*/
// 🧩 One workspace frame on Vulkan: viewport raster → sampled texture → ImGui (dynamic rendering) into any colour target

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "UI/SolidArc/Interface/SolidArcInterface.h"
#include "UI/SolidArc/Viewport/SolidArcDevice.h"
#include "UI/SolidArc/Viewport/SolidArcRasterization.h"

#include <volk.h>

#include <string>
#include <vector>

struct ImDrawData;

namespace Frontier::SolidArc
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Where the composed frame goes: a swapchain image (host) or an offscreen image (capture).
struct SolidArcFrameTarget
{
    VkImage       Image       = VK_NULL_HANDLE;                                                  // [-]  - Colour image
    VkImageView   View        = VK_NULL_HANDLE;                                                  // [-]  - Its view
    uint32_t      Width       = 0;                                                               // [px] - Extent
    uint32_t      Height      = 0;                                                               // [px] - Extent
    VkImageLayout FinalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;                            // [-]  - PRESENT_SRC_KHR or TRANSFER_SRC_OPTIMAL
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    CLASSES
//------------------------------------------------------------------------------------------------------------------------

class SolidArcWorkspaceFrame
{
public:
    SolidArcWorkspaceFrame() = default;
    SolidArcWorkspaceFrame(const SolidArcWorkspaceFrame&)            = delete;
    SolidArcWorkspaceFrame& operator=(const SolidArcWorkspaceFrame&) = delete;
    ~SolidArcWorkspaceFrame();

    // ImGui context must exist (fonts configured). TargetFormat is the composed frame's colour format.
    bool Create(const SolidArcDevice& Device, VkFormat TargetFormat, uint32_t ImageCount, std::string& Refusal);
    void Destroy();

    // 💡 Before ImGui::NewFrame: follows the viewport rectangle of the previous frame (resize + texture re-registration),
    //    uploads the displayed scene when its revision changed and assigns the selection / hover state words.
    void PrepareViewport(SolidArcWorkspaceState& State);

    // Records the viewport raster (with the pointer probe) and the ImGui draw lists into Target.
    void Record(VkCommandBuffer Commands, const SolidArcWorkspaceState& State, ImDrawData* DrawLists, const SolidArcFrameTarget& Target, bool ReadbackCondition);

    // After the frame's fence: the identity under the pointer becomes the next frame's hover.
    void Conclude(SolidArcWorkspaceState& State) const;

    [[nodiscard]] ImTextureID ViewportTexture() const { return ImTextureID(Texture); }

    SolidArcRasterization Raster = {};                                                       // [-] - Viewport targets + pipelines

private:
    const SolidArcDevice* Device       = nullptr;                                            // [-] - Borrowed
    VkDescriptorSet       Texture      = VK_NULL_HANDLE;                                     // [-] - ImGui texture of the colour target
    uint64_t              TextureTargets = 0;                                                // [-] - Raster target generation it was registered with
    bool                  BackendReady = false;                                              // [-] - ImGui_ImplVulkan_Init succeeded
    std::vector<SolidArcImage>   Thumbnails      = {};                                       // [-] - Matcap swatches (64 × 64)
    std::vector<VkDescriptorSet> ThumbnailSets   = {};                                       // [-] - Their ImGui textures
    uint64_t                     MatcapGeneration = 0;                                       // [-] - Library generation uploaded
    void ReleaseThumbnails();
    void UploadMatcaps(SolidArcWorkspaceState& State);
};

} // namespace Frontier::SolidArc
