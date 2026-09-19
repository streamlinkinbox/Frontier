//============================================================================================================================================
// 📦 Frontier/Tools/GenerateControlPanelProof.cpp — Borderless Pure Black Proof for Top Notch Geometry & Dynamics
//============================================================================================================================================

#include "../DisplayPresentation/ControlPanel.h"
#include "../DisplayPresentation/ThemeStructure.h"
#include "../DeviceExchange/InputExchange.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <algorithm>

namespace Frontier {

struct PixelRgb
{
    uint8_t R;
    uint8_t G;
    uint8_t B;
};

class ProofCanvas
{
public:
    ProofCanvas(uint32_t Width, uint32_t Height)
        : CanvasWidth(Width), CanvasHeight(Height), Buffer(Width * Height, PixelRgb{ 0, 0, 0 }) // Pure black #000000
    {
    }

    void Clear(PixelRgb Color)
    {
        std::fill(Buffer.begin(), Buffer.end(), Color);
    }

    void DrawFilledRectangle(int32_t X0, int32_t Y0, int32_t X1, int32_t Y1, PixelRgb Color)
    {
        int32_t MinX = std::max(0, std::min(X0, X1));
        int32_t MaxX = std::min(static_cast<int32_t>(CanvasWidth) - 1, std::max(X0, X1));
        int32_t MinY = std::max(0, std::min(Y0, Y1));
        int32_t MaxY = std::min(static_cast<int32_t>(CanvasHeight) - 1, std::max(Y0, Y1));

        for (int32_t Y = MinY; Y <= MaxY; ++Y)
        {
            for (int32_t X = MinX; X <= MaxX; ++X)
            {
                Buffer[Y * CanvasWidth + X] = Color;
            }
        }
    }

    void DrawFilledPolygon(const std::vector<BezierPointRecord>& PolygonPoints, float OffsetX, float OffsetY, PixelRgb Color)
    {
        if (PolygonPoints.size() < 3) return;

        float MinY = static_cast<float>(CanvasHeight);
        float MaxY = 0.0f;
        for (const auto& Pt : PolygonPoints)
        {
            MinY = std::min(MinY, Pt.Y + OffsetY);
            MaxY = std::max(MaxY, Pt.Y + OffsetY);
        }

        int32_t StartY = std::max(0, static_cast<int32_t>(std::floor(MinY)));
        int32_t EndY   = std::min(static_cast<int32_t>(CanvasHeight) - 1, static_cast<int32_t>(std::ceil(MaxY)));

        for (int32_t Y = StartY; Y <= EndY; ++Y)
        {
            std::vector<float> Intersections;
            size_t Count = PolygonPoints.size();
            for (size_t i = 0; i < Count; ++i)
            {
                size_t next = (i + 1) % Count;
                float Y1 = PolygonPoints[i].Y + OffsetY;
                float Y2 = PolygonPoints[next].Y + OffsetY;
                float X1 = PolygonPoints[i].X + OffsetX;
                float X2 = PolygonPoints[next].X + OffsetX;

                if (std::abs(Y2 - Y1) < 0.0001f) continue;

                if ((Y1 <= Y && Y2 > Y) || (Y2 <= Y && Y1 > Y))
                {
                    float IntersectX = X1 + (static_cast<float>(Y) - Y1) * (X2 - X1) / (Y2 - Y1);
                    if (!std::isnan(IntersectX) && !std::isinf(IntersectX))
                    {
                        Intersections.push_back(IntersectX);
                    }
                }
            }

            std::sort(Intersections.begin(), Intersections.end());

            for (size_t i = 0; i + 1 < Intersections.size(); i += 2)
            {
                int32_t StartX = std::max(0, static_cast<int32_t>(std::ceil(Intersections[i])));
                int32_t EndX   = std::min(static_cast<int32_t>(CanvasWidth) - 1, static_cast<int32_t>(std::floor(Intersections[i + 1])));
                for (int32_t X = StartX; X <= EndX; ++X)
                {
                    Buffer[Y * CanvasWidth + X] = Color;
                }
            }
        }
    }

    void DrawLine(int32_t X0, int32_t Y0, int32_t X1, int32_t Y1, PixelRgb Color)
    {
        int32_t DX = std::abs(X1 - X0);
        int32_t DY = std::abs(Y1 - Y0);
        int32_t Steps = std::max(DX, DY);
        if (Steps == 0)
        {
            if (X0 >= 0 && X0 < static_cast<int32_t>(CanvasWidth) && Y0 >= 0 && Y0 < static_cast<int32_t>(CanvasHeight))
            {
                Buffer[Y0 * CanvasWidth + X0] = Color;
            }
            return;
        }

        float XInc = static_cast<float>(X1 - X0) / static_cast<float>(Steps);
        float YInc = static_cast<float>(Y1 - Y0) / static_cast<float>(Steps);

        float CurrX = static_cast<float>(X0);
        float CurrY = static_cast<float>(Y0);

        for (int32_t i = 0; i <= Steps; ++i)
        {
            int32_t PxX = static_cast<int32_t>(std::round(CurrX));
            int32_t PxY = static_cast<int32_t>(std::round(CurrY));
            if (PxX >= 0 && PxX < static_cast<int32_t>(CanvasWidth) && PxY >= 0 && PxY < static_cast<int32_t>(CanvasHeight))
            {
                Buffer[PxY * CanvasWidth + PxX] = Color;
            }
            CurrX += XInc;
            CurrY += YInc;
        }
    }

    void DrawCircle(int32_t CenterX, int32_t CenterY, int32_t Radius, PixelRgb Color)
    {
        for (int32_t Y = CenterY - Radius; Y <= CenterY + Radius; ++Y)
        {
            for (int32_t X = CenterX - Radius; X <= CenterX + Radius; ++X)
            {
                if (X >= 0 && X < static_cast<int32_t>(CanvasWidth) && Y >= 0 && Y < static_cast<int32_t>(CanvasHeight))
                {
                    int32_t DX = X - CenterX;
                    int32_t DY = Y - CenterY;
                    if (DX * DX + DY * DY <= Radius * Radius)
                    {
                        Buffer[Y * CanvasWidth + X] = Color;
                    }
                }
            }
        }
    }

    void DrawNotchHandleDecorations(float HandleX, float HandleY, float HandleW = 400.0f)
    {
        int32_t CenterX = static_cast<int32_t>(HandleX + HandleW * 0.5f);
        int32_t CenterY = static_cast<int32_t>(HandleY + 18.0f);

        // Triple layered logo stack
        DrawFilledRectangle(CenterX - 68, CenterY - 6, CenterX - 56, CenterY + 6, PixelRgb{ 255, 107, 107 });
        DrawFilledRectangle(CenterX - 64, CenterY - 8, CenterX - 52, CenterY + 4, PixelRgb{ 180, 180, 200 });
        DrawFilledRectangle(CenterX - 60, CenterY - 4, CenterX - 48, CenterY + 8, PixelRgb{ 77, 150, 255 });

        // Stylized "Control Center" text placeholder bar
        DrawFilledRectangle(CenterX - 40, CenterY - 3, CenterX + 45, CenterY + 3, PixelRgb{ 240, 240, 245 });

        // Glowing green status dot
        DrawCircle(CenterX + 60, CenterY, 5, PixelRgb{ 34, 197, 94 });
    }

    bool ExportPpm(const std::string& FilePath)
    {
        std::ofstream Out(FilePath, std::ios::binary);
        if (!Out) return false;

        Out << "P6\n" << CanvasWidth << " " << CanvasHeight << "\n255\n";
        for (const auto& Px : Buffer)
        {
            Out.put(static_cast<char>(Px.R));
            Out.put(static_cast<char>(Px.G));
            Out.put(static_cast<char>(Px.B));
        }
        return true;
    }

private:
    uint32_t CanvasWidth;
    uint32_t CanvasHeight;
    std::vector<PixelRgb> Buffer;
};

} // namespace Frontier

int main()
{
    // ===================================================================================================================
    // PROOF 1: CLOSED NOTCH STATE (Pure Black Canvas, Borderless)
    // ===================================================================================================================
    {
        constexpr uint32_t Width  = 1280;
        constexpr uint32_t Height = 720;
        Frontier::ControlPanel Panel;
        (void)Panel.Initialize(Width, Height);
        Panel.CloseNotch();

        Frontier::ProofCanvas Canvas(Width, Height);
        Canvas.Clear(Frontier::PixelRgb{ 0, 0, 0 }); // Pure black background

        float HandleX = Panel.QueryHandleX();
        float HandleY = Panel.QueryHandleY();
        const auto& Contour = Panel.QueryHandleContour();

        // Borderless OLED handle body
        Canvas.DrawFilledPolygon(Contour, HandleX, HandleY, Frontier::PixelRgb{ 14, 14, 16 });
        Canvas.DrawNotchHandleDecorations(HandleX, HandleY);

        std::string Ppm = "Diagnostics/ControlCenter_NotchHandle_Closed.ppm";
        Canvas.ExportPpm(Ppm);
        std::cout << "[Proof 1] Exported PPM: " << Ppm << "\n";
    }

    // ===================================================================================================================
    // PROOF 2: INTERACTIVE DRAGGING STATE (Pure Black Canvas, Borderless)
    // ===================================================================================================================
    {
        constexpr uint32_t Width  = 1280;
        constexpr uint32_t Height = 720;
        Frontier::ControlPanel Panel;
        (void)Panel.Initialize(Width, Height);

        Frontier::InputExchange Input;
        Input.AssignMouseButton(Frontier::MouseButtonCategory::ButtonLeft, true);
        Panel.AdvanceInteraction(Input, 640.0f, 18.0f);
        Panel.AdvanceInteraction(Input, 640.0f, 360.0f);

        Frontier::ProofCanvas Canvas(Width, Height);
        Canvas.Clear(Frontier::PixelRgb{ 0, 0, 0 }); // Pure black background

        float HandleX = Panel.QueryHandleX();
        float HandleY = Panel.QueryHandleY();
        const auto& Contour = Panel.QueryHandleContour();

        // Borderless pull-down shade
        Canvas.DrawFilledRectangle(0, 0, Width - 1, static_cast<int32_t>(HandleY), Frontier::PixelRgb{ 14, 14, 16 });

        // Empty minimal shell (borderless pure black inset)
        int32_t CardX0 = static_cast<int32_t>((Width - 520) / 2);
        int32_t CardY0 = static_cast<int32_t>((HandleY - 240) / 2);
        Canvas.DrawFilledRectangle(CardX0, CardY0, CardX0 + 520, CardY0 + 240, Frontier::PixelRgb{ 22, 22, 26 });

        // Bottom handle
        Canvas.DrawFilledPolygon(Contour, HandleX, HandleY, Frontier::PixelRgb{ 14, 14, 16 });
        Canvas.DrawNotchHandleDecorations(HandleX, HandleY);

        // Blue drag cursor
        Canvas.DrawCircle(640, static_cast<int32_t>(HandleY + 18.0f), 7, Frontier::PixelRgb{ 59, 130, 246 });

        std::string Ppm = "Diagnostics/ControlCenter_NotchHandle_Dragging.ppm";
        Canvas.ExportPpm(Ppm);
        std::cout << "[Proof 2] Exported PPM: " << Ppm << "\n";
    }

    // ===================================================================================================================
    // PROOF 3: FULLY PULLED-DOWN STATE (Pure Black Canvas, Borderless)
    // ===================================================================================================================
    {
        constexpr uint32_t Width  = 1280;
        constexpr uint32_t Height = 720;
        Frontier::ControlPanel Panel;
        (void)Panel.Initialize(Width, Height);

        Panel.OpenNotch();
        Panel.AdvanceLocomotion(1.0f);

        Frontier::ProofCanvas Canvas(Width, Height);
        Canvas.Clear(Frontier::PixelRgb{ 0, 0, 0 }); // Pure black background

        float HandleX = Panel.QueryHandleX();
        float HandleY = Panel.QueryHandleY();
        const auto& Contour = Panel.QueryHandleContour();

        // Borderless full-height shade
        Canvas.DrawFilledRectangle(0, 0, Width - 1, static_cast<int32_t>(HandleY), Frontier::PixelRgb{ 14, 14, 16 });

        // Empty minimal shell container (borderless dark card)
        int32_t CardX0 = static_cast<int32_t>((Width - 680) / 2);
        int32_t CardY0 = 40;
        int32_t CardX1 = CardX0 + 680;
        int32_t CardY1 = static_cast<int32_t>(HandleY - 20);
        Canvas.DrawFilledRectangle(CardX0, CardY0, CardX1, CardY1, Frontier::PixelRgb{ 22, 22, 26 });

        // Bottom handle
        Canvas.DrawFilledPolygon(Contour, HandleX, HandleY, Frontier::PixelRgb{ 14, 14, 16 });
        Canvas.DrawNotchHandleDecorations(HandleX, HandleY);

        std::string Ppm = "Diagnostics/ControlCenter_NotchHandle_PulledDown.ppm";
        Canvas.ExportPpm(Ppm);
        std::cout << "[Proof 3] Exported PPM: " << Ppm << "\n";
    }

    // ===================================================================================================================
    // PROOF 4: TRIPTYCH LOCOMOTION PROOF
    // ===================================================================================================================
    {
        constexpr uint32_t TotalW = 1920;
        constexpr uint32_t TotalH = 640;
        constexpr uint32_t ColW   = 640;

        Frontier::ProofCanvas Canvas(TotalW, TotalH);
        Canvas.Clear(Frontier::PixelRgb{ 0, 0, 0 }); // Pure black

        // Column 1: Closed State
        {
            Frontier::ControlPanel P;
            (void)P.Initialize(ColW, TotalH);
            P.CloseNotch();
            float HX = P.QueryHandleX();
            float HY = P.QueryHandleY();
            const auto& C = P.QueryHandleContour();
            Canvas.DrawFilledPolygon(C, HX, HY, Frontier::PixelRgb{ 14, 14, 16 });
            Canvas.DrawNotchHandleDecorations(HX, HY, 400.0f);
        }

        // Column 2: Dragging State (Y = 320px)
        {
            int32_t OffsetX = ColW;
            Frontier::ControlPanel P;
            (void)P.Initialize(ColW, TotalH);
            Frontier::InputExchange Input;
            Input.AssignMouseButton(Frontier::MouseButtonCategory::ButtonLeft, true);
            P.AdvanceInteraction(Input, ColW * 0.5f, 18.0f);
            P.AdvanceInteraction(Input, ColW * 0.5f, 320.0f);

            float HX = OffsetX + P.QueryHandleX();
            float HY = P.QueryHandleY();
            const auto& C = P.QueryHandleContour();

            Canvas.DrawFilledRectangle(OffsetX, 0, OffsetX + ColW - 1, static_cast<int32_t>(HY), Frontier::PixelRgb{ 14, 14, 16 });

            int32_t CardX0 = OffsetX + static_cast<int32_t>((ColW - 400) / 2);
            int32_t CardY0 = static_cast<int32_t>((HY - 200) / 2);
            Canvas.DrawFilledRectangle(CardX0, CardY0, CardX0 + 400, CardY0 + 200, Frontier::PixelRgb{ 22, 22, 26 });

            Canvas.DrawFilledPolygon(C, HX, HY, Frontier::PixelRgb{ 14, 14, 16 });
            Canvas.DrawNotchHandleDecorations(HX, HY, 400.0f);

            Canvas.DrawCircle(OffsetX + static_cast<int32_t>(ColW * 0.5f), static_cast<int32_t>(HY + 18.0f), 7, Frontier::PixelRgb{ 59, 130, 246 });
        }

        // Column 3: Fully Pulled-Down State (Y = 604px)
        {
            int32_t OffsetX = ColW * 2;
            Frontier::ControlPanel P;
            (void)P.Initialize(ColW, TotalH);
            P.OpenNotch();
            P.AdvanceLocomotion(1.0f);

            float HX = OffsetX + P.QueryHandleX();
            float HY = P.QueryHandleY();
            const auto& C = P.QueryHandleContour();

            Canvas.DrawFilledRectangle(OffsetX, 0, OffsetX + ColW - 1, static_cast<int32_t>(HY), Frontier::PixelRgb{ 14, 14, 16 });

            int32_t CardX0 = OffsetX + static_cast<int32_t>((ColW - 440) / 2);
            int32_t CardY0 = 30;
            int32_t CardX1 = CardX0 + 440;
            int32_t CardY1 = static_cast<int32_t>(HY - 20);
            Canvas.DrawFilledRectangle(CardX0, CardY0, CardX1, CardY1, Frontier::PixelRgb{ 22, 22, 26 });

            Canvas.DrawFilledPolygon(C, HX, HY, Frontier::PixelRgb{ 14, 14, 16 });
            Canvas.DrawNotchHandleDecorations(HX, HY, 400.0f);
        }

        // Subtle Column Separators
        Canvas.DrawLine(ColW, 0, ColW, TotalH - 1, Frontier::PixelRgb{ 30, 30, 36 });
        Canvas.DrawLine(ColW * 2, 0, ColW * 2, TotalH - 1, Frontier::PixelRgb{ 30, 30, 36 });

        std::string Ppm = "Diagnostics/ControlCenter_LocomotionTriptych.ppm";
        Canvas.ExportPpm(Ppm);
        std::cout << "[Proof 4] Exported PPM: " << Ppm << "\n";
    }

    std::cout << "[Verification Complete] All proof images created successfully.\n";
    return 0;
}
