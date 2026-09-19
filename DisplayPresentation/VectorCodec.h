//============================================================================================================================================
// 📦 Frontier/DisplayPresentation/VectorCodec.h — Categorized Scalable Vector Graphic (SVG) Path Decoders and Glyph Geometry
//============================================================================================================================================

#pragma once

#if defined(_MSC_VER)
    #pragma warning(disable: 4324)                              // Disable structure padding alignment warning under /WX
#endif

#include <cstdint>
#include <string_view>
#include <array>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                  ICON CATEGORY
//------------------------------------------------------------------------------------------------------------------------

enum class IconCategory : uint32_t
{
    Navigation                          = 0,                    // Directional arrows, chevrons, rotations and expands
    ControlCentre                       = 1,                    // Settings, appearance, display, input, bell, wifi, power
    EditorTools                         = 2,                    // Select, translate, rotate, scale, snap and viewport
    TexturePainting                     = 3,                    // Brush, eraser, eyedropper, bucket, stamp, gradient
    Outliner                            = 4,                    // Folder, hierarchy, light, camera, material, visibility
    Count                               = 5
};

//------------------------------------------------------------------------------------------------------------------------
//                                             NAVIGATION ICON CATEGORY
//------------------------------------------------------------------------------------------------------------------------

enum class NavigationIconCategory : uint32_t
{
    ArrowUp                             = 0,                    // ↑ straight up
    ArrowDown                           = 1,                    // ↓ straight down
    ArrowLeft                           = 2,                    // ← straight left
    ArrowRight                          = 3,                    // → straight right
    ChevronUp                           = 4,                    // ⌃ single chevron up
    ChevronDown                         = 5,                    // ⌄ single chevron down
    ChevronLeft                         = 6,                    // ‹ single chevron left
    ChevronRight                        = 7,                    // › single chevron right
    ChevronsUp                          = 8,                    // ︽ double chevron up
    ChevronsDown                        = 9,                    // ︾ double chevron down
    ChevronsLeft                        = 10,                   // « double chevron left
    ChevronsRight                       = 11,                   // » double chevron right
    CornerDownRight                     = 12,                   // ↳ tree branch right
    CornerDownLeft                      = 13,                   // ↵ tree branch left
    RotateClockwise                     = 14,                   // ↻ clockwise loop
    RotateCounterClockwise              = 15,                   // ↺ counter-clockwise loop
    ExpandDiagonal                      = 16,                   // ⤢ maximize/expand
    CollapseDiagonal                    = 17,                   // ⤡ minimize/collapse
    Count                               = 18
};

//------------------------------------------------------------------------------------------------------------------------
//                                           CONTROL CENTRE ICON CATEGORY
//------------------------------------------------------------------------------------------------------------------------

enum class ControlCentreIconCategory : uint32_t
{
    SettingsGear                        = 0,                    // ⚙ gear cog
    AppearancePalette                   = 1,                    // 🎨 artist palette / theme swatches
    DisplayMonitor                      = 2,                    // 🖥 monitor display screen
    InputDevices                        = 3,                    // ⌨ keyboard / input controller
    NotificationsBell                   = 4,                    // 🔔 bell notification alert
    WirelessSignal                      = 5,                    // 🛜 wifi wireless waves
    BluetoothSymbol                     = 6,                    // ᛒ bluetooth node
    MoonDisturbance                     = 7,                    // 🌙 do not disturb moon
    VolumeSpeaker                       = 8,                    // 🔊 audio master speaker
    SunIllumination                     = 9,                    // ☀️ brightness illumination
    Count                               = 10
};

//------------------------------------------------------------------------------------------------------------------------
//                                                VECTOR GLYPH RECORD
//------------------------------------------------------------------------------------------------------------------------

struct VectorGlyphRecord
{
    const char*             IdentifierName;                     // [text] unique glyph slug
    const char*             SvgPathString;                      // [svg] normalized 24x24 SVG path coordinate stream
    uint32_t                ViewBoxWidth;                       // [px] base viewbox width (24px standard)
    uint32_t                ViewBoxHeight;                      // [px] base viewbox height (24px standard)
    float                   DefaultStrokeWidth;                 // [px] standard stroke thickness (2.0px)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    VECTOR CODEC
//------------------------------------------------------------------------------------------------------------------------

class VectorCodec
{
public:
    VectorCodec() noexcept = default;
    ~VectorCodec() noexcept = default;

    VectorCodec(const VectorCodec&) = delete;
    VectorCodec& operator=(const VectorCodec&) = delete;

    [[nodiscard]] static const VectorGlyphRecord& QueryNavigationIcon(NavigationIconCategory Icon) noexcept;
    [[nodiscard]] static std::string_view QueryNavigationSvgPath(NavigationIconCategory Icon) noexcept;
    [[nodiscard]] static uint32_t         QueryNavigationIconCount() noexcept;

    [[nodiscard]] static const VectorGlyphRecord& QueryControlCentreIcon(ControlCentreIconCategory Icon) noexcept;
    [[nodiscard]] static std::string_view QueryControlCentreSvgPath(ControlCentreIconCategory Icon) noexcept;
    [[nodiscard]] static uint32_t         QueryControlCentreIconCount() noexcept;

    // Single unified conversion operator for icon record
    template<typename TargetType>
    [[nodiscard]] static TargetType Convert(NavigationIconCategory Icon) noexcept;

    template<typename TargetType>
    [[nodiscard]] static TargetType Convert(ControlCentreIconCategory Icon) noexcept;

private:
    static const std::array<VectorGlyphRecord, static_cast<size_t>(NavigationIconCategory::Count)> NavigationGlyphTable;
    static const std::array<VectorGlyphRecord, static_cast<size_t>(ControlCentreIconCategory::Count)> ControlCentreGlyphTable;
};

template<>
inline std::string_view VectorCodec::Convert<std::string_view>(NavigationIconCategory Icon) noexcept
{
    return QueryNavigationSvgPath(Icon);
}

template<>
inline const VectorGlyphRecord& VectorCodec::Convert<const VectorGlyphRecord&>(NavigationIconCategory Icon) noexcept
{
    return QueryNavigationIcon(Icon);
}

template<>
inline std::string_view VectorCodec::Convert<std::string_view>(ControlCentreIconCategory Icon) noexcept
{
    return QueryControlCentreSvgPath(Icon);
}

template<>
inline const VectorGlyphRecord& VectorCodec::Convert<const VectorGlyphRecord&>(ControlCentreIconCategory Icon) noexcept
{
    return QueryControlCentreIcon(Icon);
}

} // namespace Frontier
