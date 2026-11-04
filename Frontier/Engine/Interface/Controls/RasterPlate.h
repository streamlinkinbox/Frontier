/*==========================================================================================================================================
                                                               RASTERPLATE.H
==========================================================================================================================================*/
// 🧩 A reference image retained by the ImGui context until its renderer acknowledges texture retirement.

#pragma once

#include <imgui.h>

#include <cstdint>
#include <memory>
#include <span>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    RASTER PLATE
//------------------------------------------------------------------------------------------------------------------------

class RasterPlate
{
  public:
    RasterPlate() = default;
    ~RasterPlate();
    void Reset() noexcept;
    RasterPlate(const RasterPlate &)            = delete;
    RasterPlate &operator=(const RasterPlate &) = delete;

    bool               Configure(int Width, int Height, std::span<const uint8_t> Rgba);
    void               Construct(ImDrawList *Ink,
                                 ImVec2      Minimum,
                                 ImVec2      Maximum,
                                 ImU32       Tint    = IM_COL32_WHITE,
                                 float       Radius  = 0,
                                 ImDrawFlags Corners = ImDrawFlags_RoundCornersAll) const;
    [[nodiscard]] bool QueryReady() const noexcept
    {
        return Context == ImGui::GetCurrentContext() && !Pixels.expired();
    }

  private:
    std::weak_ptr<ImTextureData> Pixels;
    ImGuiContext                *Context = nullptr;
};

} // namespace Frontier
