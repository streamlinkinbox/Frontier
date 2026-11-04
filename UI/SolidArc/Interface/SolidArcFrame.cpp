/*====================================================================================================================================
                                                          SOLIDARCFRAME.CPP
====================================================================================================================================*/
// 🧩 One workspace frame on Vulkan: viewport raster → sampled texture → ImGui (dynamic rendering) into any colour target

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "UI/SolidArc/Interface/SolidArcFrame.h"

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Frontier::SolidArc
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

SolidArcWorkspaceFrame::~SolidArcWorkspaceFrame()
{
    Destroy();
}

bool SolidArcWorkspaceFrame::Create(const SolidArcDevice& OpenDevice, VkFormat TargetFormat, uint32_t ImageCount, std::string& Refusal)
{
    Device = &OpenDevice;
    if (!Raster.Create(OpenDevice, Refusal)) return false;

    ImGui_ImplVulkan_InitInfo Setup{};
    Setup.ApiVersion          = OpenDevice.ApiVersion;
    Setup.Instance            = OpenDevice.Instance;
    Setup.PhysicalDevice      = OpenDevice.Physical;
    Setup.Device              = OpenDevice.Device;
    Setup.QueueFamily         = OpenDevice.QueueFamily;
    Setup.Queue               = OpenDevice.Queue;
    Setup.DescriptorPoolSize  = 64 + SolidArcMatcapLayerLimit + 1;                           // font + viewport + matcap swatches
    Setup.MinImageCount       = std::max<uint32_t>(2, ImageCount);
    Setup.ImageCount          = std::max<uint32_t>(2, ImageCount);
    Setup.UseDynamicRendering = true;
    Setup.PipelineInfoMain.PipelineRenderingCreateInfo                         = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    Setup.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount    = 1;
    Setup.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &TargetFormat;   // deep-copied by the backend
    if (!ImGui_ImplVulkan_Init(&Setup))
    {
        Refusal = "ImGui Vulkan backend refused to initialise";
        return false;
    }
    BackendReady = true;
    return true;
}

void SolidArcWorkspaceFrame::Destroy()
{
    if (!Device) return;
    vkDeviceWaitIdle(Device->Device);
    ReleaseThumbnails();
    if (Texture != VK_NULL_HANDLE) ImGui_ImplVulkan_RemoveTexture(Texture);
    Texture        = VK_NULL_HANDLE;
    TextureTargets = 0;
    if (BackendReady) ImGui_ImplVulkan_Shutdown();
    BackendReady = false;
    Raster.Destroy();
    Device = nullptr;
}

void SolidArcWorkspaceFrame::ReleaseThumbnails()
{
    if (!Device) return;
    for (VkDescriptorSet Set : ThumbnailSets)
        if (Set != VK_NULL_HANDLE && BackendReady) ImGui_ImplVulkan_RemoveTexture(Set);
    for (SolidArcImage& Image : Thumbnails) Device->DestroyImage(Image);
    ThumbnailSets.clear();
    Thumbnails.clear();
}

// 📝 On a new library generation: the texture array (layers 1 … n → array layers 0 … n − 1) and one 64 × 64 swatch per layer
//    (layer 0 = the Studio mirror) registered as ImGui textures for the Inspector and the M picker.
void SolidArcWorkspaceFrame::UploadMatcaps(SolidArcWorkspaceState& State)
{
    RefreshSolidArcMatcaps(State);
    if (State.Matcaps.Generation == MatcapGeneration && Raster.MatcapLayerCount() > 0) return;
    const SolidArcMatcapLibrary& Library = State.Matcaps.Library;
    Raster.UploadMatcaps(Library.Pictures);
    vkDeviceWaitIdle(Device->Device);
    ReleaseThumbnails();
    State.Matcaps.Thumbnails.clear();
    for (size_t Layer = 0; Layer < Library.Names.size(); ++Layer)
    {
        constexpr uint32_t Side = 64;
        const SolidArcPicture Swatch = FitSolidArcMatcapPicture(Layer == 0 ? ComposeSolidArcStudioMatcap(Side * 2) : Library.Pictures[Layer - 1], Side);
        SolidArcImage Image = Device->CreateImage(Side, Side, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        SolidArcBuffer Staging = Device->CreateBuffer(VkDeviceSize(Side) * Side * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
        // 📝 Round swatch: outside the disc goes transparent (the ImGui pass blends), a one-pixel soft edge inside.
        std::vector<uint8_t> Pixels = Swatch.Pixels;
        for (uint32_t Row = 0; Row < Side; ++Row)
        {
            for (uint32_t Column = 0; Column < Side; ++Column)
            {
                const double Across = (double(Column) + 0.5) / Side * 2.0 - 1.0;
                const double Down   = (double(Row) + 0.5) / Side * 2.0 - 1.0;
                const double Edge   = std::clamp((1.0 - std::hypot(Across, Down)) * Side * 0.5, 0.0, 1.0);
                Pixels[(size_t(Row) * Side + Column) * 4 + 3] = uint8_t(std::lround(Edge * 255.0));
            }
        }
        std::memcpy(Staging.Mapped, Pixels.data(), Pixels.size());
        Device->SubmitImmediately([&](VkCommandBuffer Commands)
        {
            TransitionSolidArcImage(Commands, Image.Image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkBufferImageCopy Copy{};
            Copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
            Copy.imageExtent      = { Side, Side, 1 };
            vkCmdCopyBufferToImage(Commands, Staging.Buffer, Image.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &Copy);
            TransitionSolidArcImage(Commands, Image.Image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        });
        Device->DestroyBuffer(Staging);
        const VkDescriptorSet Set = BackendReady ? ImGui_ImplVulkan_AddTexture(Image.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) : VK_NULL_HANDLE;
        Thumbnails.push_back(Image);
        ThumbnailSets.push_back(Set);
        State.Matcaps.Thumbnails.push_back(ImTextureID(Set));
    }
    MatcapGeneration = State.Matcaps.Generation;
}

void SolidArcWorkspaceFrame::PrepareViewport(SolidArcWorkspaceState& State)
{
    const auto& TargetRect = State.SplitView.Layout == SolidArcViewportLayout::Single ? State.ViewportRect : State.SplitView.FullRect;
    const uint32_t Width  = uint32_t(std::max(8.0f, std::round(TargetRect[2])));
    const uint32_t Height = uint32_t(std::max(8.0f, std::round(TargetRect[3])));
    Raster.Resize(Width, Height);
    // ⚠️ Keyed on the target generation, not the VkImageView handle: a driver may hand the destroyed view's handle to the new
    //    view after a resize. A handle-keyed descriptor then keeps the old image (freed memory, old size) and the viewport
    //    comes out in displaced tiles (seen on an AMD RX 9060 XT).
    if (Raster.TargetGeneration() != TextureTargets || Texture == VK_NULL_HANDLE)
    {
        // The previous frame has completed (fence / idle), so its descriptor set may be released now.
        if (Texture != VK_NULL_HANDLE) ImGui_ImplVulkan_RemoveTexture(Texture);
        Texture        = ImGui_ImplVulkan_AddTexture(Raster.ColourView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        TextureTargets = Raster.TargetGeneration();
    }
    const SolidArcScene& Scene = DisplayedSolidArcScene(State);
    if (Scene.Generation != Raster.SceneGeneration()) Raster.UploadScene(Scene);
    // 📝 Image planes: decode + upload when the set of picture paths changes; rebuild the draw list every frame.
    std::string Key;
    std::vector<std::string> Paths;
    for (const SolidArcImagePlane& Plane : State.Document.ImagePlanes)
    {
        if (std::find(Paths.begin(), Paths.end(), Plane.Path) == Paths.end()) Paths.push_back(Plane.Path);
    }
    for (const std::string& Path : Paths) Key += Path + "\n";
    if (Key != State.Pictures.Key)
    {
        std::vector<SolidArcPicture> Pictures;
        State.Pictures = {};
        State.Pictures.Key = Key;
        for (const std::string& Path : Paths)
        {
            Pictures.push_back(DecodeSolidArcPicture(Path));
            State.Pictures.Paths.push_back(Path);
            State.Pictures.Widths.push_back(Pictures.back().Width);
            State.Pictures.Heights.push_back(Pictures.back().Height);
            State.Pictures.Refusals.push_back(Pictures.back().Refusal);
        }
        Raster.UploadPictures(Pictures);
    }
    State.Style.ImagePlanes.clear();
    if (State.ImagePlanesEnabled)
    {
        for (const SolidArcImagePlane& Plane : State.Document.ImagePlanes)
        {
            const size_t Index = size_t(std::find(State.Pictures.Paths.begin(), State.Pictures.Paths.end(), Plane.Path) - State.Pictures.Paths.begin());
            if (!Plane.VisibleCondition || Index >= State.Pictures.Paths.size() || !State.Pictures.Refusals[Index].empty()) continue;
            const float Width  = float(State.Pictures.Widths[Index]);
            const float Height = float(State.Pictures.Heights[Index]);
            const bool  WholeCondition = Plane.Crop[2] <= Plane.Crop[0] || Plane.Crop[3] <= Plane.Crop[1];
            SolidArcImagePlaneDraw Draw;
            Draw.Picture = uint32_t(Index);
            Draw.Corner  = { Plane.Corner.X, Plane.Corner.Y, Plane.Corner.Z };
            Draw.Right   = { Plane.Right.X, Plane.Right.Y, Plane.Right.Z };
            Draw.Down    = { Plane.Down.X, Plane.Down.Y, Plane.Down.Z };
            if (!WholeCondition) Draw.Crop = { Plane.Crop[0] / Width, Plane.Crop[1] / Height, Plane.Crop[2] / Width, Plane.Crop[3] / Height };
            if (Plane.FlipAcrossCondition) std::swap(Draw.Crop[0], Draw.Crop[2]);             // The shader lerps u0 → u1: swapped, it mirrors
            if (Plane.FlipDownCondition) std::swap(Draw.Crop[1], Draw.Crop[3]);
            Draw.Opacity = float(Plane.Opacity);
            Draw.Mode    = Plane.PhotoCondition ? 1.0f : 0.0f;
            Draw.FrontOnlyCondition = Plane.FrontOnlyCondition;
            State.Style.ImagePlanes.push_back(Draw);
        }
    }
    // This diagnostic describes the active camera; quad panes independently
    // apply the same facing predicate in the rasterizer.
    const OrbitCameraFrame ActiveFrame = FrameOrbitCamera(State.Camera, 1.0, 1.0);
    State.Pictures.Drawn = int(std::count_if(State.Style.ImagePlanes.begin(), State.Style.ImagePlanes.end(),
        [&](const SolidArcImagePlaneDraw& Draw) { return FacesSolidArcImagePlane(Draw, ActiveFrame); }));
    UploadMatcaps(State);
    const SolidArcSceneStates States = DisplayedSolidArcStates(State);
    Raster.AssignStates(States.Faces, States.Edges);
    State.Style.EdgeStroke = State.Options.EdgeStroke;
}

void SolidArcWorkspaceFrame::Record(VkCommandBuffer Commands, const SolidArcWorkspaceState& State, ImDrawData* DrawLists, const SolidArcFrameTarget& Target,
                                    bool ReadbackCondition)
{
    // ① viewport raster at its own size; the pointer probe feeds GPU identity hover / picking
    const ImVec2 Pointer = ImGui::GetIO().MousePos;
    int ProbeX = -1;
    int ProbeY = -1;
    if (State.SplitView.Layout == SolidArcViewportLayout::Single)
    {
        if (State.ViewportHovered && State.ViewportRect[2] > 0.0f && State.ViewportRect[3] > 0.0f)
        {
            ProbeX = int(std::floor((Pointer.x - State.ViewportRect[0]) * float(Raster.Width()) / State.ViewportRect[2]));
            ProbeY = int(std::floor((Pointer.y - State.ViewportRect[1]) * float(Raster.Height()) / State.ViewportRect[3]));
        }
        const std::vector<SolidArcRasterPane> Panes = ComposeSolidArcRasterPanes(State, Raster.Width(), Raster.Height());
        Raster.RecordPanes(Commands, Panes, State.Style.Background, ReadbackCondition, ProbeX, ProbeY);
    }
    else
    {
        if (State.ViewportHovered && State.SplitView.FullRect[2] > 0.0f && State.SplitView.FullRect[3] > 0.0f)
        {
            ProbeX = int(std::floor((Pointer.x - State.SplitView.FullRect[0]) * float(Raster.Width()) / State.SplitView.FullRect[2]));
            ProbeY = int(std::floor((Pointer.y - State.SplitView.FullRect[1]) * float(Raster.Height()) / State.SplitView.FullRect[3]));
        }
        const std::vector<SolidArcRasterPane> Panes = ComposeSolidArcRasterPanes(State, Raster.Width(), Raster.Height());
        Raster.RecordPanes(Commands, Panes, State.Style.Background, ReadbackCondition, ProbeX, ProbeY);
    }

    // ② ImGui composes every panel (the viewport colour target is sampled as a texture)
    TransitionSolidArcImage(Commands, Target.Image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo Colour{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    Colour.imageView   = Target.View;
    Colour.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    Colour.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
    Colour.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
    Colour.clearValue.color = { { 0.059f, 0.059f, 0.063f, 1.0f } };                          // #0f0f10 page background
    VkRenderingInfo Rendering{ VK_STRUCTURE_TYPE_RENDERING_INFO };
    Rendering.renderArea           = { { 0, 0 }, { Target.Width, Target.Height } };
    Rendering.layerCount           = 1;
    Rendering.colorAttachmentCount = 1;
    Rendering.pColorAttachments    = &Colour;
    vkCmdBeginRendering(Commands, &Rendering);
    if (DrawLists) ImGui_ImplVulkan_RenderDrawData(DrawLists, Commands);
    vkCmdEndRendering(Commands);
    TransitionSolidArcImage(Commands, Target.Image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, Target.FinalLayout);
}

void SolidArcWorkspaceFrame::Conclude(SolidArcWorkspaceState& State) const
{
    State.HoverIdentity = State.ViewportHovered ? Raster.ProbedIdentity() : 0u;
}

} // namespace Frontier::SolidArc
