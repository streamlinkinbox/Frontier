//============================================================================================================================================
//                                                  CONTROLCENTREPANEL.H
//============================================================================================================================================
// 🧩 Development editor Control Centre — the built dashboard, drawn through ImGui. The figures below are the
//    DisplayPresentation host's own (ControlCentreHost.h: notch, shade travel, card, discs, pill); the editor port
//    keeps them verbatim so the two never drift apart. What differs is the backend: the host records a PixelSpace
//    for the Vulkan overlay, this panel records ImGui draw lists over the dock columns.
//
//    The notch handle (400 × 36, project name centred) hangs from the top edge at rest; a downward carry, a tap,
//    or the viewport's gear slides the shade down, and the 420 × 480 dashboard rides its middle: five discs
//    (Global Illumination, Anti-Aliasing, FPS Overlay, Notifications, Quality) over the render-scale pill. GI
//    seats the render path (full ReSTIR while on, the visibility raster while off); render scale trims the view
//    rows; the rest hold live settings figures the engine build consumes, each change raising the applied toast.
//    The settings hub pages belong to the game overlay, so the header gear raises a note toast in the editor.

#pragma once

#include <cstdint>

#include <imgui.h>

namespace Frontier {

class ControlPanel;

enum class ControlCentreTile : uint32_t
{
    GlobalIllumination = 0,
    AntiAliasing       = 1,
    FrameRateOverlay   = 2,
    Notifications      = 3,
    Quality            = 4,
    Count              = 5
};

enum class ControlCentreQuality : uint32_t
{
    Minimal   = 0,
    Economy   = 1,
    Standard  = 2,
    Ultra     = 3,
    Reference = 4
};

class ControlCentrePanel final
{
public:
    // Notch + shade travel (the host's figures, verbatim).
    static constexpr float kNotchW      = 400.0f;   // [px] handle width
    static constexpr float kNotchH      = 36.0f;    // [px] handle height; the dock host offsets by this
    static constexpr float kTapTravel   = 6.0f;     // [px] beyond this a contact is a carry
    static constexpr float kTapDuration = 0.35f;    // [s] beyond this a contact is a press
    static constexpr float kElastic     = 0.05f;    // [-] overshoot accepted past a bound
    static constexpr float kSnapRate    = 20.0f;    // [px/s] release-velocity threshold
    static constexpr float kSnapOffset  = 50.0f;    // [px] release-offset threshold
    static constexpr float kRateKeep    = 0.60f;    // [-] velocity estimate smoothing
    static constexpr float kScrimMax    = 0.40f;    // [-] black over the columns when fully open

    // Dashboard card (the host's figures, verbatim).
    static constexpr float kCardW       = 420.0f;   // [px]
    static constexpr float kCardH       = 480.0f;   // [px]
    static constexpr float kDisc        = 64.0f;    // [px] tile disc diameter
    static constexpr float kGlyph       = 24.0f;    // [px] tile glyph size
    static constexpr float kLabelGap    = 12.0f;    // [px] disc-to-label gap
    static constexpr float kColGap      = 16.0f;    // [px] disc column gap
    static constexpr float kRowGap      = 40.0f;    // [px] disc row gap
    static constexpr float kStackGap    = 40.0f;    // [px] header/grid/pill gap
    static constexpr float kScaleMin    = 0.25f;    // [-] render scale floor

    void AssignControls(ControlPanel* Controls) noexcept;

    // Shares the open figure with the viewport's gear: either side toggles, both agree.
    void AssignOpen(bool* Open) noexcept;

    // Names the project the handle carries; the harness seats the level under test.
    void AssignProjectName(const char* Name) noexcept;

    void Record() noexcept;

    [[nodiscard]] bool     QueryGiEnabled() const noexcept { return GiOn_; }
    [[nodiscard]] float    QueryRenderScale() const noexcept { return RenderScale_; }
    [[nodiscard]] uint32_t QueryRevision() const noexcept { return Revision_; }

    // The harness seams: handle centre (always seated), GI disc centre and pill track span (seated while the
    //    card takes the pointer), so a harness can tap and drag the shade it sees.
    [[nodiscard]] float QueryNotchX() const noexcept { return NotchCX_; }
    [[nodiscard]] float QueryNotchY() const noexcept { return NotchCY_; }
    [[nodiscard]] float QueryGiTileX() const noexcept { return GiCX_; }
    [[nodiscard]] float QueryGiTileY() const noexcept { return GiCY_; }
    [[nodiscard]] float QueryPillX0() const noexcept { return PillX0_; }
    [[nodiscard]] float QueryPillX1() const noexcept { return PillX1_; }
    [[nodiscard]] float QueryPillY() const noexcept { return PillY_; }

private:
    void DrawHandle(float NotchX, float ShadeY, float Bound) noexcept;
    void DrawScrim(float ShadeY, float OpenTravel, float Width, float Height) noexcept;
    void DrawCard(float ShadeY, float Width, float Opacity) noexcept;
    void DrawTile(uint32_t Slot, float DiscCX, float DiscCY, float Opacity, bool Live) noexcept;
    void DrawPill(float PillX, float PillY, float PillW, float Opacity, bool Live) noexcept;
    void DrawToast(float ShadeY, float Width) noexcept;
    void RaiseToast() noexcept;

    static void DrawSun(ImDrawList* Draw, float CX, float CY, float Size, ImU32 Tint, float Thick) noexcept;
    static void DrawSparkles(ImDrawList* Draw, float CX, float CY, float Size, ImU32 Tint) noexcept;
    static void DrawGauge(ImDrawList* Draw, float CX, float CY, float Size, ImU32 Tint, float Thick) noexcept;
    static void DrawBell(ImDrawList* Draw, float CX, float CY, float Size, ImU32 Tint, float Thick) noexcept;
    static void DrawSliders(ImDrawList* Draw, float CX, float CY, float Size, ImU32 Tint, float Thick) noexcept;
    static void DrawWifi(ImDrawList* Draw, float CX, float CY, float Size, ImU32 Tint, float Thick) noexcept;
    static void DrawGear(ImDrawList* Draw, float CX, float CY, float Size, ImU32 Tint, float Thick) noexcept;
    static void DrawVideo(ImDrawList* Draw, float CX, float CY, float Size, ImU32 Tint, float Thick) noexcept;
    static ImU32 FadeTint(ImU32 Tint, float Opacity) noexcept;

    ControlPanel* Controls_ = nullptr;
    bool*         Open_     = nullptr;

    // The dashboard settings; Revision bumps on every tile tap and pill release.
    bool                 GiOn_        = true;
    bool                 AaOn_        = true;
    bool                 FpsOn_       = false;
    bool                 NotifOn_     = true;
    ControlCentreQuality Quality_     = ControlCentreQuality::Standard;
    float                RenderScale_ = 1.0f;
    uint32_t             Revision_    = 0u;

    char ProjectName_[64] = { 'F', 'r', 'o', 'n', 't', 'i', 'e', 'r', '\0' };

    float ShadeY_     = 0.0f;    // [px] the shade's lower edge, easing toward Target_
    float Target_     = 0.0f;    // [px] 0 shut, H − 36 open
    float NotchX_     = 0.0f;    // [px] signed slide from the centred handle
    float NotchAim_   = 0.0f;    // [px] where the slide settles on release

    bool  Grabbed_    = false;
    bool  AxisSet_    = false;
    bool  YCarry_     = true;
    float GrabX_      = 0.0f;
    float GrabY_      = 0.0f;
    float GrabShade_  = 0.0f;
    float GrabNotch_  = 0.0f;
    float Travelled_  = 0.0f;
    float Contact_    = 0.0f;
    float VelY_       = 0.0f;
    float PrevY_      = 0.0f;
    bool  PillHeld_   = false;

    double ToastUntil_   = -1.0;   // [s] imgui time the applied toast rests, −1 none
    double ToastRaised_  = 0.0;
    char   ToastTitle_[48] = {};
    char   ToastSub_[96]   = {};

    float NotchCX_ = -1.0f;
    float NotchCY_ = -1.0f;
    float GiCX_    = -1.0f;
    float GiCY_    = -1.0f;
    float PillX0_  = -1.0f;
    float PillX1_  = -1.0f;
    float PillY_   = -1.0f;
};

} // namespace Frontier
