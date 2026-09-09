//============================================================================================================================================
//                                                     NOTIFICATIONQUEUE.H
//============================================================================================================================================
// 🧩 Transient toast notifications: pushed by any engine system, aged each frame, drawn top-right beneath the notch line.
//
//    Visual: 320 px wide cards, #1C1C1E fill, radius 16, 14 px white/90 title over 12 px white/50 body, slide in from
//    the right over 200 ms, hold 3.5 s, fade out over 300 ms. Newest at the top; at most 4 visible.
//    The Control Centre "Notifications" tile gates the whole queue: when disabled, Push() is a no-op.

#pragma once

#include "PixelSpace.h"
#include <cstdint>
#include <string>
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                  NOTIFICATION RECORD
//------------------------------------------------------------------------------------------------------------------------

struct NotificationRecord
{
    std::string Title;          // [text] one line
    std::string Body;           // [text] one line, may be empty
    float       Age = 0.0f;     // [s]   since push
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  NOTIFICATION QUEUE
//------------------------------------------------------------------------------------------------------------------------

class NotificationQueue
{
public:
    static constexpr float CardWidth     = 320.0f;   // [px]
    static constexpr float CardPadding   =  14.0f;   // [px]
    static constexpr float CardRadius    =  16.0f;   // [px]
    static constexpr float CardGap       =   8.0f;   // [px]
    static constexpr float EdgeInset     =  16.0f;   // [px] from the right edge and below the notch line
    static constexpr float EnterDuration =   0.20f;  // [s]
    static constexpr float HoldDuration  =   3.50f;  // [s]
    static constexpr float ExitDuration  =   0.30f;  // [s]
    static constexpr uint32_t VisibleLimit = 4u;

    NotificationQueue() noexcept = default;

    void AssignEnabled(bool Enabled) noexcept { EnabledCondition = Enabled; }
    [[nodiscard]] bool IsEnabled() const noexcept { return EnabledCondition; }
    // Control Centre › Notifications › "Hold Duration" (1 … 10 s); defaults to HoldDuration.
    void AssignHoldSeconds(float Seconds) noexcept { HoldSeconds = Seconds < 0.5f ? 0.5f : (Seconds > 30.0f ? 30.0f : Seconds); }
    [[nodiscard]] float QueryHoldSeconds() const noexcept { return HoldSeconds; }

    void Push(std::string Title, std::string Body = {}) noexcept;
    void Advance(float DeltaSeconds) noexcept;
    void ConstructNotificationLayout(PixelSpace& Surface, float TopInset) const noexcept;

    [[nodiscard]] uint32_t QueryPendingCount() const noexcept { return static_cast<uint32_t>(Records.size()); }

private:
    std::vector<NotificationRecord> Records;
    bool EnabledCondition = true;
    float HoldSeconds = HoldDuration;
};

} // namespace Frontier
