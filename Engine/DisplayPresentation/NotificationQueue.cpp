//============================================================================================================================================
//                                                    NOTIFICATIONQUEUE.CPP
//============================================================================================================================================
// 🧩 Toast ageing and layout — see NotificationQueue.h.

#include "NotificationQueue.h"
#include "ControlKit.h"

#include <algorithm>

namespace Frontier {

void NotificationQueue::Push(std::string Title, std::string Body) noexcept
{
    if (!EnabledCondition) return;
    Records.insert(Records.begin(), NotificationRecord{ std::move(Title), std::move(Body), 0.0f });
    if (Records.size() > VisibleLimit) Records.resize(VisibleLimit);
}

void NotificationQueue::Advance(float DeltaSeconds) noexcept
{
    if (DeltaSeconds <= 0.0f) return;
    for (NotificationRecord& R : Records) R.Age += DeltaSeconds;
    const float Lifetime = EnterDuration + HoldSeconds + ExitDuration;
    Records.erase(std::remove_if(Records.begin(), Records.end(),
                                 [Lifetime](const NotificationRecord& R) { return R.Age >= Lifetime; }),
                  Records.end());
}

void NotificationQueue::ConstructNotificationLayout(PixelSpace& Surface, float TopInset) const noexcept
{
    if (!Surface.IsRecording() || Records.empty()) return;

    const ColorQuad Card  = ControlKit::Palette().CardSub;   // bg-[#1C1C1E] → theme CardSubBackground
    const ColorQuad Title = ControlKit::Palette().Text;      // text-white/90 → colors.text
    const ColorQuad Body  = ControlKit::Palette().TextDim;   // text-white/50 → colors.textMuted
    constexpr float TitleSize = 14.0f, BodySize = 12.0f, LineGap = 3.0f;

    const float Right = Surface.QueryDisplayWidth() - EdgeInset;
    float Y = TopInset + EdgeInset;

    for (const NotificationRecord& R : Records)
    {
        // Enter: slide from +40 px right and fade in. Exit: fade out in place.
        float Alpha = 1.0f, SlideX = 0.0f;
        if (R.Age < EnterDuration)
        {
            const float T = R.Age / EnterDuration;
            const float Eased = 1.0f - (1.0f - T) * (1.0f - T);
            Alpha = Eased; SlideX = 40.0f * (1.0f - Eased);
        }
        else if (R.Age > EnterDuration + HoldSeconds)
        {
            Alpha = 1.0f - std::clamp((R.Age - EnterDuration - HoldSeconds) / ExitDuration, 0.0f, 1.0f);
        }

        const bool  HasBody = !R.Body.empty();
        const float Height  = CardPadding * 2.0f + TitleSize + (HasBody ? LineGap + BodySize : 0.0f);
        const PlaneExtent Extent = Spanning(Right - CardWidth + SlideX, Y, CardWidth, Height);

        ColorQuad C = Card;  C.Alpha *= Alpha;
        ColorQuad T = Title; T.Alpha *= Alpha;
        ColorQuad B = Body;  B.Alpha *= Alpha;

        Surface.FillRectangle(Extent, C, CardRadius);
        Surface.Text(Extent.MinimumX + CardPadding, Extent.MinimumY + CardPadding, T, R.Title.c_str(), TitleSize);
        if (HasBody)
            Surface.Text(Extent.MinimumX + CardPadding, Extent.MinimumY + CardPadding + TitleSize + LineGap, B, R.Body.c_str(), BodySize);

        Y += Height + CardGap;
    }
}

} // namespace Frontier
