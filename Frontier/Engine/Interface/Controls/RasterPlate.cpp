/*==========================================================================================================================================
                                                              RASTERPLATE.CPP
==========================================================================================================================================*/
// 🧩 Immutable reference textures; deletion of a record never frees a texture still named by an in-flight draw list.

#include "Engine/Interface/Controls/RasterPlate.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace Frontier
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONTEXT RETIREMENT
//------------------------------------------------------------------------------------------------------------------------

struct PlateStore
{
    std::vector<std::shared_ptr<ImTextureData>> Sheets;
};

void ReclaimTexture(const std::shared_ptr<ImTextureData> &Sheet)
{
    IM_ASSERT(Sheet->BackendUserData == nullptr);
    ImGui::UnregisterUserTexture(Sheet.get());
    auto &Textures = ImGui::GetPlatformIO().Textures;
    Textures.find_erase(Sheet.get());
}

void AdvancePlateRetirement(ImGuiContext *, ImGuiContextHook *Hook)
{
    auto &Store = *static_cast<PlateStore *>(Hook->UserData);
    if (Hook->Type == ImGuiContextHookType_Shutdown)
    {
        for (const auto &Sheet : Store.Sheets)
            ReclaimTexture(Sheet);
        delete &Store;
        return;
    }
    std::erase_if(Store.Sheets,
                  [](const auto &Sheet)
                  {
                      if (Sheet->Status != ImTextureStatus_Destroyed) return false;
                      ReclaimTexture(Sheet);
                      return true;
                  });
}

PlateStore &ResolvePlateStore()
{
    ImGuiContext *Context = ImGui::GetCurrentContext();
    for (const auto &Hook : Context->Hooks)
        if (Hook.Callback == AdvancePlateRetirement && Hook.Type == ImGuiContextHookType_Shutdown)
            return *static_cast<PlateStore *>(Hook.UserData);
    auto            *Store = new PlateStore();
    ImGuiContextHook Hook;
    Hook.Callback = AdvancePlateRetirement;
    Hook.UserData = Store;
    Hook.Type     = ImGuiContextHookType_NewFramePre;
    ImGui::AddContextHook(Context, &Hook);
    Hook.Type = ImGuiContextHookType_Shutdown;
    ImGui::AddContextHook(Context, &Hook);
    return *Store;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    IMAGE PUBLICATION
//------------------------------------------------------------------------------------------------------------------------

RasterPlate::~RasterPlate()
{
    Reset();
}

void RasterPlate::Reset() noexcept
{
    if (const auto Sheet = Pixels.lock()) Sheet->WantDestroyNextFrame = true;
    Pixels.reset();
    Context = nullptr;
}

bool RasterPlate::Configure(int Width, int Height, std::span<const uint8_t> Rgba)
{
    if (ImGui::GetCurrentContext() == nullptr || !(ImGui::GetIO().BackendFlags & ImGuiBackendFlags_RendererHasTextures)) return false;
    if (Width <= 0 || Height <= 0 || Width > 4096 || Height > 4096 ||
        Rgba.size() != static_cast<size_t>(Width) * static_cast<size_t>(Height) * 4)
        return false;
    auto  &Store         = ResolvePlateStore();
    size_t ResidentBytes = 0;
    for (const auto &Retained : Store.Sheets)
        ResidentBytes += static_cast<size_t>(Retained->Width) * Retained->Height * 4;
    // 📝 A stalled texture consumer cannot grow an unbounded retirement queue while controls are dragged.
    if (Store.Sheets.size() >= 64 || ResidentBytes + Rgba.size() > 64 * 1024 * 1024) return false;
    auto Sheet = std::make_shared<ImTextureData>();
    Sheet->Create(ImTextureFormat_RGBA32, Width, Height);
    Sheet->UseColors = true;
    std::memcpy(Sheet->Pixels, Rgba.data(), Rgba.size());
    ImGui::RegisterUserTexture(Sheet.get());
    Store.Sheets.push_back(Sheet);
    if (const auto Previous = Pixels.lock()) Previous->WantDestroyNextFrame = true;
    Pixels  = Sheet;
    Context = ImGui::GetCurrentContext();
    return true;
}

void RasterPlate::Construct(ImDrawList *Ink, ImVec2 Minimum, ImVec2 Maximum, ImU32 Tint, float Radius, ImDrawFlags Corners) const
{
    if (Context != ImGui::GetCurrentContext() || Ink == nullptr) return;
    if (const auto Sheet = Pixels.lock())
    {
        if (Radius > 0)
            Ink->AddImageRounded(Sheet->GetTexRef(), Minimum, Maximum, ImVec2(0, 0), ImVec2(1, 1), Tint, Radius, Corners);
        else Ink->AddImage(Sheet->GetTexRef(), Minimum, Maximum, ImVec2(0, 0), ImVec2(1, 1), Tint);
    }
}

} // namespace Frontier
