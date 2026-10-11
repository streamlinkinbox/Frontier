//========================================================================================================================
// 🧩 AppearanceInspector — content of the Control Centre's Appearance page (Display · Fonts · Theme tabs).
//    Holds an Applied and a Draft copy of the settings; the footer's Apply / Discard enable only while they differ.
//    Display tab: real display settings (values stored and reported; swapchain wiring is a later step).
//    Theme tab:   1:1 port of Notch ThemeTab.tsx (colour-scheme tiles, corner radius, accent, semantic colours).
//========================================================================================================================
#pragma once

#include "ControlKit.h"
#include "FontCodec.h"
#include "ThemeStructure.h"

#include <cstdint>

namespace Frontier {

enum class RenderResolutionCategory : uint32_t { Native = 0, Quad1440 = 1, Full1080 = 2, Half720 = 3, Count = 4 };
enum class VerticalSyncCategory     : uint32_t { Off = 0, On = 1, Adaptive = 2, Count = 3 };
enum class FrameCapCategory         : uint32_t { Unlimited = 0, Cap60 = 1, Cap120 = 2, Cap144 = 3, Count = 4 };
enum class OverlayCornerCategory    : uint32_t { TopLeft = 0, TopRight = 1, BottomLeft = 2, BottomRight = 3, Count = 4 };
enum class SampleCountCategory      : uint32_t { Single = 0, Double = 1, Quad = 2, Octa = 3, Count = 4 };   // 1x 2x 4x 8x

struct AppearanceSettings
{
    // ── Display ───────────────────────────────────────────────────────────────────────────────────────────────────
    RenderResolutionCategory Resolution      = RenderResolutionCategory::Native;
    float                    InterfaceScale  = 100.0f;   // [%] 50 … 200
    bool                     MatchQualityTier= true;     // render scale follows the Quality tier
    VerticalSyncCategory     VerticalSync    = VerticalSyncCategory::On;
    FrameCapCategory         FrameCap        = FrameCapCategory::Unlimited;
    bool                     Fullscreen      = false;
    SampleCountCategory      AntiAliasing    = SampleCountCategory::Single;
    float                    SafeAreaPadding = 32.0f;    // [px] 0 … 128
    bool                     FrameRateOverlay= false;
    OverlayCornerCategory    OverlayCorner   = OverlayCornerCategory::TopRight;

    // ── Theme ─────────────────────────────────────────────────────────────────────────────────────────────────────
    ThemeCategory            Theme           = ThemeCategory::Dark;
    float                    CornerRadius    = 16.0f;    // [px] 0 … 32
    AccentCategory           Accent          = AccentCategory::Blue;
    uint32_t                 WarningSwatch   = 0u;       // index into the 4-swatch rows below
    uint32_t                 SuccessSwatch   = 1u;
    uint32_t                 InfoSwatch      = 0u;
    uint32_t                 CautionSwatch   = 0u;

    // ── Fonts ─────────────────────────────────────────────────────────────────────────────────────────────────────
    //    FontFamily indexes TypefaceRegistry tile order. Roles follow Notch FontsTab SCALES[] (Title … Caption).
    static constexpr uint32_t TypeRoleCount = 6u;
    uint32_t                 FontFamily      = 0u;
    float                    RoleSize[TypeRoleCount]           = { 32.0f, 24.0f, 18.0f, 14.0f, 12.0f, 10.0f };
    FontWeightCategory       RoleWeight[TypeRoleCount]         = { FontWeightCategory::Bold, FontWeightCategory::Bold, FontWeightCategory::Medium,
                                                                   FontWeightCategory::Regular, FontWeightCategory::Medium, FontWeightCategory::Regular };
    bool                     FontAntialiasing= true;
    bool                     Ligatures       = true;

    [[nodiscard]] bool operator==(const AppearanceSettings&) const noexcept = default;
};

// Names one differing field for the footer status line.
struct AppearanceDifference
{
    uint32_t    Count = 0u;
    const char* Names[8] = {};   // first eight changed fields
};

class AppearanceInspector
{
public:
    AppearanceInspector() noexcept;

    [[nodiscard]] const AppearanceSettings& QueryApplied() const noexcept { return Applied; }
    [[nodiscard]] const AppearanceSettings& QueryDraft()   const noexcept { return Draft; }
    [[nodiscard]] bool  IsDirty() const noexcept { return !(Draft == Applied); }
    [[nodiscard]] AppearanceDifference QueryDifference() const noexcept;
    [[nodiscard]] uint32_t QueryRevision() const noexcept { return Revision; }

    void Apply()   noexcept { Applied = Draft; ++Revision; }
    // Seed both applied and draft from a persisted record (start-up). Bumps Revision so hosts react once.
    void Seed(const AppearanceSettings& Persisted) noexcept { Applied = Persisted; Draft = Persisted; ++Revision; }
    void Discard() noexcept { Draft = Applied; CloseMenus(); }
    void ResetDefaults() noexcept { Draft = AppearanceSettings{}; }
    void CloseMenus() noexcept { OpenDropdown = -1; DraggingSlider = -1; }
    [[nodiscard]] bool HasOpenMenu() const noexcept { return OpenDropdown >= 0; }
    [[nodiscard]] bool IsDragging() const noexcept { return DraggingSlider >= 0; }

    // Records one tab's content into Body (already clipped by the caller) offset by ScrollY; returns content height.
    float ConstructDisplayTabLayout(PixelSpace& Surface, const PlaneExtent& Body, float ScrollY, const ControlPointer& Pointer, float Opacity) noexcept;
    float ConstructThemeTabLayout  (PixelSpace& Surface, const PlaneExtent& Body, float ScrollY, const ControlPointer& Pointer, float Opacity) noexcept;
    float ConstructFontsTabLayout  (PixelSpace& Surface, const PlaneExtent& Body, float ScrollY, const ControlPointer& Pointer, float Opacity) noexcept;
    void  AdvanceFontsTab(float DeltaSeconds) noexcept;   // smooth strip scroll (scrollBy behavior:"smooth")

    // Face for a type role of the APPLIED settings (hosts render chrome with these). nullptr → backend default.
    [[nodiscard]] void* QueryAppliedFace(uint32_t Role) const noexcept;
    [[nodiscard]] static const char* QueryTypeRoleLabel(uint32_t Role) noexcept;

    // Dropdown menus float above everything else in the page; the host calls this after the body clip is popped.
    void  ConstructFloatingLayout(PixelSpace& Surface, const ControlPointer& Pointer, float Opacity) noexcept;

    // Palette / preview helpers shared with the host (the draft theme previews live).
    [[nodiscard]] static const char* QueryThemeName(ThemeCategory Theme) noexcept;
    [[nodiscard]] static ColorQuad  QueryAccentColour(AccentCategory Accent) noexcept;
    [[nodiscard]] static ColorQuad  QuerySemanticColour(uint32_t Row, uint32_t Swatch) noexcept;   // rows: 0 warning 1 success 2 info 3 caution

    // Layout figures
    static constexpr float SectionGap    = 32.0f;   // space-y-8
    static constexpr float SectionRadiusMax = 32.0f;

    // Proof-harness access: extents recorded on the last frame.
    [[nodiscard]] PlaneExtent QueryThemeTileExtent(uint32_t Index) const noexcept { return Index < 7u ? TileExtents[Index] : PlaneExtent{}; }
    [[nodiscard]] PlaneExtent QueryRadiusSliderExtent() const noexcept { return RadiusSliderExtent; }
    [[nodiscard]] PlaneExtent QueryScaleSliderExtent()  const noexcept { return ScaleSliderExtent; }
    [[nodiscard]] PlaneExtent QueryResolutionDropdownExtent() const noexcept { return DropdownExtents[0]; }
    [[nodiscard]] PlaneExtent QueryFullscreenSwitchExtent() const noexcept { return FullscreenSwitchExtent; }
    [[nodiscard]] PlaneExtent QueryAccentSwatchExtent(uint32_t Index) const noexcept { return Index < 10u ? AccentExtents[Index] : PlaneExtent{}; }
    [[nodiscard]] PlaneExtent QueryVsyncSegmentExtent() const noexcept { return VsyncExtent; }
    [[nodiscard]] PlaneExtent QueryFontCardExtent(uint32_t Index) const noexcept { return Index < 10u ? FontCardExtents[Index] : PlaneExtent{}; }
    [[nodiscard]] PlaneExtent QueryFontStripButtonExtent(bool Forward) const noexcept { return Forward ? StripForwardExtent : StripBackExtent; }
    [[nodiscard]] PlaneExtent QueryRoleSliderExtent(uint32_t Role) const noexcept { return Role < AppearanceSettings::TypeRoleCount ? RoleSliderExtents[Role] : PlaneExtent{}; }
    [[nodiscard]] PlaneExtent QueryRoleWeightChipExtent(uint32_t Role, FontWeightCategory Weight) const noexcept;
    [[nodiscard]] PlaneExtent QueryFontSwitchExtent(bool Ligatures) const noexcept { return Ligatures ? LigatureSwitchExtent : FontAaSwitchExtent; }
    [[nodiscard]] float       QueryFontStripScroll() const noexcept { return StripScroll; }

private:
    struct DropdownRecord { PlaneExtent Button; const char* const* Options; uint32_t Count; uint32_t Value; int Pick; };   // Pick: choice made in the floating layer, consumed by the next RecordDropdown

    void RecordDropdown(PixelSpace& Surface, const PlaneExtent& Extent, int Ordinal, const char* const* Options, uint32_t Count, uint32_t& Value, const ControlPointer& Pointer, float Opacity) noexcept;
    void RecordThemeTile(PixelSpace& Surface, const PlaneExtent& Extent, ThemeCategory Theme, bool Active, float Radius, const ControlPointer& Pointer, float Opacity) noexcept;
    void RecordSemanticRow(PixelSpace& Surface, float X, float Y, float Width, uint32_t Row, const char* Label, uint32_t& Swatch, ControlCentreIconCategory Icon, float Radius, bool Last, const ControlPointer& Pointer, float Opacity) noexcept;

    AppearanceSettings Applied;
    AppearanceSettings Draft;
    uint32_t           Revision;
    int                OpenDropdown;      // ordinal of the open dropdown, -1 none
    int                DraggingSlider;    // ordinal of the slider being dragged, -1 none
    DropdownRecord     Dropdowns[4];
    uint32_t           DropdownCount;

    PlaneExtent        TileExtents[7];
    PlaneExtent        AccentExtents[10];
    PlaneExtent        RadiusSliderExtent, ScaleSliderExtent, DropdownExtents[4], FullscreenSwitchExtent, VsyncExtent;

    // Fonts tab state
    float              StripScroll, StripScrollTarget;   // [px] horizontal offset of the family strip
    float              StripContentWidth, StripViewWidth;
    PlaneExtent        FontCardExtents[10], StripBackExtent, StripForwardExtent;
    PlaneExtent        RoleSliderExtents[AppearanceSettings::TypeRoleCount];
    PlaneExtent        ChipExtents[AppearanceSettings::TypeRoleCount][9];
    PlaneExtent        FontAaSwitchExtent, LigatureSwitchExtent;
};

} // namespace Frontier
