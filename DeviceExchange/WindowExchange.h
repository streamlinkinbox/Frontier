//============================================================================================================================================
// 📦 Frontier/DeviceExchange/WindowExchange.h — Cross-Platform Native Window Creation and Display Exchange (Win32 & X11)
//============================================================================================================================================

#pragma once

#if defined(_MSC_VER)
    #pragma warning(disable: 4324)                              // Disable structure padding alignment warning under /WX
#endif

#include "InputExchange.h"
#include <cstdint>
#include <string_view>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                WINDOW CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

struct WindowConfiguration
{
    uint32_t                Width;                              // [px] window horizontal resolution
    uint32_t                Height;                             // [px] window vertical resolution
    const char*             Title;                              // [text] window title text
    bool                    FullscreenCondition;                // [bool] true for exclusive fullscreen
    bool                    VerticalSyncCondition;              // [bool] true for vertical blank synchronisation
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   WINDOW EXCHANGE
//------------------------------------------------------------------------------------------------------------------------

class WindowExchange
{
public:
    WindowExchange() noexcept;
    ~WindowExchange() noexcept;

    WindowExchange(const WindowExchange&) = delete;
    WindowExchange& operator=(const WindowExchange&) = delete;

    [[nodiscard]] bool      OpenDisplayWindow(const WindowConfiguration& Config) noexcept;
    void                    CloseDisplayWindow() noexcept;

    void                    PollEvents(InputExchange* TargetInputExchange = nullptr) noexcept;
    [[nodiscard]] bool      ShouldClose() const noexcept { return CloseRequestedCondition; }
    void                    RequestClose() noexcept { CloseRequestedCondition = true; }

    [[nodiscard]] uint32_t  QueryWidth() const noexcept { return CurrentWidth; }
    [[nodiscard]] uint32_t  QueryHeight() const noexcept { return CurrentHeight; }
    [[nodiscard]] void*     QueryNativeWindowToken() const noexcept { return NativeWindowToken; }
    [[nodiscard]] void*     QueryNativeDisplayToken() const noexcept { return NativeDisplayToken; }

    // Single unified conversion operator for window openness
    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

private:
    void*                   NativeWindowToken;                  // [token] OS window handle (HWND on Win32, Window on X11)
    void*                   NativeDisplayToken;                 // [token] OS display handle (HINSTANCE on Win32, Display* on X11)
    uint32_t                CurrentWidth;                       // [px] active horizontal resolution
    uint32_t                CurrentHeight;                      // [px] active vertical resolution
    bool                    CloseRequestedCondition;            // [bool] window close requested status
    bool                    OpenCondition;                      // [bool] active window status
};

template<>
inline bool WindowExchange::Convert<bool>() const noexcept
{
    return OpenCondition;
}

template<>
inline uint32_t WindowExchange::Convert<uint32_t>() const noexcept
{
    return CurrentWidth;
}

} // namespace Frontier
