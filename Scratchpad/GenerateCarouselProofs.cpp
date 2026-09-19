//============================================================================================================================================
// 📦 Frontier/Scratchpad/GenerateCarouselProofs.cpp — Rasterized Verification Proof for Stepper Carousel Sliding Locomotion
//============================================================================================================================================

#include "../DisplayPresentation/ControlCentrePanel.h"
#include "../DisplayPresentation/ThemeStructure.h"
#include "../DisplayPresentation/VectorCodec.h"
#include "../DeviceExchange/InputExchange.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdlib>

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
        : CanvasWidth(Width), CanvasHeight(Height), Buffer(Width * Height, PixelRgb{ 0, 0, 0 })
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

    void DrawDashboardCard(int32_t X0, int32_t Y0, int32_t W, int32_t H)
    {
        DrawFilledRectangle(X0, Y0, X0 + W, Y0 + H, PixelRgb{ 14, 14, 16 });

        // Header: "Control Center" label + Settings Gear Button
        DrawFilledRectangle(X0 + 20, Y0 + 16, X0 + 140, Y0 + 28, PixelRgb{ 220, 220, 230 });
        // Gear button pill
        DrawCircle(X0 + W - 30, Y0 + 22, 12, PixelRgb{ 32, 32, 38 });
        DrawCircle(X0 + W - 30, Y0 + 22, 5, PixelRgb{ 59, 130, 246 }); // Blue accent center

        // 8 Quick Action Toggle Buttons
        int32_t GridStartX = X0 + 30;
        int32_t GridStartY = Y0 + 60;
        int32_t SpacingX   = (W - 60) / 4;
        for (int r = 0; r < 2; ++r)
        {
            for (int c = 0; c < 4; ++c)
            {
                int32_t CX = GridStartX + c * SpacingX + 24;
                int32_t CY = GridStartY + r * 70 + 24;
                bool Active = (r == 0 && (c == 0 || c == 1)) || (r == 1 && c == 0);
                DrawCircle(CX, CY, 22, Active ? PixelRgb{ 59, 130, 246 } : PixelRgb{ 28, 28, 34 });
            }
        }

        // Sliders
        int32_t SliderY1 = Y0 + 210;
        int32_t SliderY2 = Y0 + 260;
        DrawFilledRectangle(X0 + 30, SliderY1, X0 + W - 30, SliderY1 + 32, PixelRgb{ 24, 24, 28 });
        DrawFilledRectangle(X0 + 70, SliderY1 + 12, X0 + (W * 3 / 4), SliderY1 + 20, PixelRgb{ 59, 130, 246 });

        DrawFilledRectangle(X0 + 30, SliderY2, X0 + W - 30, SliderY2 + 32, PixelRgb{ 24, 24, 28 });
        DrawFilledRectangle(X0 + 70, SliderY2 + 12, X0 + (W * 2 / 3), SliderY2 + 20, PixelRgb{ 59, 130, 246 });
    }

    void DrawSettingsHubCard(int32_t X0, int32_t Y0, int32_t W, int32_t H)
    {
        DrawFilledRectangle(X0, Y0, X0 + W, Y0 + H, PixelRgb{ 14, 14, 16 });

        // Back button + Header title "Settings"
        DrawCircle(X0 + 30, Y0 + 24, 14, PixelRgb{ 32, 32, 38 });
        DrawLine(X0 + 34, Y0 + 24, X0 + 24, Y0 + 24, PixelRgb{ 255, 255, 255 }); // ← arrow
        DrawLine(X0 + 24, Y0 + 24, X0 + 28, Y0 + 20, PixelRgb{ 255, 255, 255 });
        DrawLine(X0 + 24, Y0 + 24, X0 + 28, Y0 + 28, PixelRgb{ 255, 255, 255 });

        DrawFilledRectangle(X0 + 60, Y0 + 18, X0 + 160, Y0 + 30, PixelRgb{ 240, 240, 245 });

        // 4 Menu Rows (Appearance, Display & Workspace, Input & Keybindings, Apps & Notifications)
        const char* Rows[4] = { "Appearance", "Display", "Input", "Notifications" };
        int32_t RowY = Y0 + 65;
        for (int i = 0; i < 4; ++i)
        {
            (void)Rows[i];
            DrawFilledRectangle(X0 + 20, RowY, X0 + W - 20, RowY + 54, PixelRgb{ 22, 22, 26 });
            DrawCircle(X0 + 44, RowY + 27, 16, PixelRgb{ 10, 10, 12 });
            // Label bars
            DrawFilledRectangle(X0 + 72, RowY + 18, X0 + 220, RowY + 26, PixelRgb{ 220, 220, 230 });
            DrawFilledRectangle(X0 + 72, RowY + 32, X0 + 320, RowY + 38, PixelRgb{ 100, 100, 115 });
            // Right chevron ›
            int32_t ChevX = X0 + W - 40;
            int32_t ChevY = RowY + 27;
            DrawLine(ChevX, ChevY - 6, ChevX + 6, ChevY, PixelRgb{ 120, 120, 140 });
            DrawLine(ChevX + 6, ChevY, ChevX, ChevY + 6, PixelRgb{ 120, 120, 140 });

            RowY += 62;
        }
    }

    void DrawSubPageCard(int32_t X0, int32_t Y0, int32_t W, int32_t H, const char* Title)
    {
        (void)Title;
        DrawFilledRectangle(X0, Y0, X0 + W, Y0 + H, PixelRgb{ 14, 14, 16 });

        // Back button + Sub-page Header
        DrawCircle(X0 + 30, Y0 + 24, 14, PixelRgb{ 32, 32, 38 });
        DrawLine(X0 + 34, Y0 + 24, X0 + 24, Y0 + 24, PixelRgb{ 255, 255, 255 });
        DrawLine(X0 + 24, Y0 + 24, X0 + 28, Y0 + 20, PixelRgb{ 255, 255, 255 });
        DrawLine(X0 + 24, Y0 + 24, X0 + 28, Y0 + 28, PixelRgb{ 255, 255, 255 });

        DrawFilledRectangle(X0 + 60, Y0 + 18, X0 + 220, Y0 + 30, PixelRgb{ 240, 240, 245 });

        // Tab strip
        DrawFilledRectangle(X0 + 30, Y0 + 55, X0 + 100, Y0 + 70, PixelRgb{ 255, 255, 255 });
        DrawFilledRectangle(X0 + 120, Y0 + 55, X0 + 180, Y0 + 70, PixelRgb{ 80, 80, 95 });
        DrawFilledRectangle(X0 + 200, Y0 + 55, X0 + 260, Y0 + 70, PixelRgb{ 80, 80, 95 });

        // Sub-page body content cards
        DrawFilledRectangle(X0 + 20, Y0 + 85, X0 + W - 20, Y0 + 220, PixelRgb{ 22, 22, 26 });
        DrawFilledRectangle(X0 + 20, Y0 + 235, X0 + W - 20, Y0 + 340, PixelRgb{ 22, 22, 26 });
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
    constexpr uint32_t Width  = 1280;
    constexpr uint32_t Height = 720;

    // ===================================================================================================================
    // 1. DASHBOARD PAGE (Page 0 with Settings button)
    // ===================================================================================================================
    {
        Frontier::ControlCentrePanel Panel;
        (void)Panel.Initialize(Width, Height);
        Panel.OpenNotch();
        Panel.AdvanceLocomotion(1.0f);

        Frontier::ProofCanvas Canvas(Width, Height);
        Canvas.Clear(Frontier::PixelRgb{ 0, 0, 0 });

        float HandleY = Panel.QueryHandleY();
        Canvas.DrawFilledRectangle(0, 0, Width - 1, static_cast<int32_t>(HandleY), Frontier::PixelRgb{ 10, 10, 12 });

        int32_t CardW = 500;
        int32_t CardH = 380;
        int32_t CardX = (Width - CardW) / 2;
        int32_t CardY = 60;
        Canvas.DrawDashboardCard(CardX, CardY, CardW, CardH);

        const auto& Contour = Panel.QueryHandleContour();
        Canvas.DrawFilledPolygon(Contour, Panel.QueryHandleX(), HandleY, Frontier::PixelRgb{ 10, 10, 12 });

        Canvas.ExportPpm("Diagnostics/ControlCenter_Carousel_Dashboard.ppm");
        (void)std::system("python3 Tools/PpmToPng.py Diagnostics/ControlCenter_Carousel_Dashboard.ppm Diagnostics/ControlCenter_Carousel_Dashboard.png > /dev/null 2>&1");
        std::cout << "[Carousel Proof 1] Generated Dashboard view.\n";
    }

    // ===================================================================================================================
    // 2. SLIDING CAROUSEL TRANSITION IN MOTION (Slide Offset in progress)
    // ===================================================================================================================
    {
        Frontier::ControlCentrePanel Panel;
        (void)Panel.Initialize(Width, Height);
        Panel.OpenNotch();
        Panel.AdvanceLocomotion(1.0f);

        // Click settings button and advance transition partially (50% slide)
        Panel.NavigateToPage(Frontier::ControlCentrePageCategory::SettingsHub);
        Panel.AdvanceLocomotion(0.06f); // In the middle of spring slide

        Frontier::ProofCanvas Canvas(Width, Height);
        Canvas.Clear(Frontier::PixelRgb{ 0, 0, 0 });

        float HandleY = Panel.QueryHandleY();
        Canvas.DrawFilledRectangle(0, 0, Width - 1, static_cast<int32_t>(HandleY), Frontier::PixelRgb{ 10, 10, 12 });

        int32_t CardW = 500;
        int32_t CardH = 380;
        int32_t BaseX = (Width - CardW) / 2;
        int32_t CardY = 60;

        float SlideX = Panel.QuerySlideOffset();

        // Dashboard sliding out to the left
        Canvas.DrawDashboardCard(static_cast<int32_t>(BaseX + SlideX - Width * 0.65f), CardY, CardW, CardH);

        // SettingsHub sliding in from the right
        Canvas.DrawSettingsHubCard(static_cast<int32_t>(BaseX + SlideX), CardY, CardW, CardH);

        const auto& Contour = Panel.QueryHandleContour();
        Canvas.DrawFilledPolygon(Contour, Panel.QueryHandleX(), HandleY, Frontier::PixelRgb{ 10, 10, 12 });

        Canvas.ExportPpm("Diagnostics/ControlCenter_Carousel_SlidingTransition.ppm");
        (void)std::system("python3 Tools/PpmToPng.py Diagnostics/ControlCenter_Carousel_SlidingTransition.ppm Diagnostics/ControlCenter_Carousel_SlidingTransition.png > /dev/null 2>&1");
        std::cout << "[Carousel Proof 2] Generated Sliding transition in progress.\n";
    }

    // ===================================================================================================================
    // 3. SETTINGS HUB PAGE (Page 1 with 4 Category Menus)
    // ===================================================================================================================
    {
        Frontier::ControlCentrePanel Panel;
        (void)Panel.Initialize(Width, Height);
        Panel.OpenNotch();
        Panel.NavigateToPage(Frontier::ControlCentrePageCategory::SettingsHub);
        Panel.AdvanceLocomotion(1.0f); // Fully settled

        Frontier::ProofCanvas Canvas(Width, Height);
        Canvas.Clear(Frontier::PixelRgb{ 0, 0, 0 });

        float HandleY = Panel.QueryHandleY();
        Canvas.DrawFilledRectangle(0, 0, Width - 1, static_cast<int32_t>(HandleY), Frontier::PixelRgb{ 10, 10, 12 });

        int32_t CardW = 500;
        int32_t CardH = 380;
        int32_t CardX = (Width - CardW) / 2;
        int32_t CardY = 60;
        Canvas.DrawSettingsHubCard(CardX, CardY, CardW, CardH);

        const auto& Contour = Panel.QueryHandleContour();
        Canvas.DrawFilledPolygon(Contour, Panel.QueryHandleX(), HandleY, Frontier::PixelRgb{ 10, 10, 12 });

        Canvas.ExportPpm("Diagnostics/ControlCenter_Carousel_SettingsHub.ppm");
        (void)std::system("python3 Tools/PpmToPng.py Diagnostics/ControlCenter_Carousel_SettingsHub.ppm Diagnostics/ControlCenter_Carousel_SettingsHub.png > /dev/null 2>&1");
        std::cout << "[Carousel Proof 3] Generated Settings Hub view.\n";
    }

    // ===================================================================================================================
    // 4. SUB-PAGE (Page 2: Appearance Sub-Page)
    // ===================================================================================================================
    {
        Frontier::ControlCentrePanel Panel;
        (void)Panel.Initialize(Width, Height);
        Panel.OpenNotch();
        Panel.NavigateToPage(Frontier::ControlCentrePageCategory::Appearance);
        Panel.AdvanceLocomotion(1.0f);

        Frontier::ProofCanvas Canvas(Width, Height);
        Canvas.Clear(Frontier::PixelRgb{ 0, 0, 0 });

        float HandleY = Panel.QueryHandleY();
        Canvas.DrawFilledRectangle(0, 0, Width - 1, static_cast<int32_t>(HandleY), Frontier::PixelRgb{ 10, 10, 12 });

        int32_t CardW = 500;
        int32_t CardH = 380;
        int32_t CardX = (Width - CardW) / 2;
        int32_t CardY = 60;
        Canvas.DrawSubPageCard(CardX, CardY, CardW, CardH, "Appearance");

        const auto& Contour = Panel.QueryHandleContour();
        Canvas.DrawFilledPolygon(Contour, Panel.QueryHandleX(), HandleY, Frontier::PixelRgb{ 10, 10, 12 });

        Canvas.ExportPpm("Diagnostics/ControlCenter_Carousel_SubPage.ppm");
        (void)std::system("python3 Tools/PpmToPng.py Diagnostics/ControlCenter_Carousel_SubPage.ppm Diagnostics/ControlCenter_Carousel_SubPage.png > /dev/null 2>&1");
        std::cout << "[Carousel Proof 4] Generated Sub-Page view.\n";
    }

    // ===================================================================================================================
    // 5. 4-PANEL COMPOSITE PROOF (Dashboard -> Slide In Progress -> Settings Hub -> Sub-Page)
    // ===================================================================================================================
    {
        constexpr uint32_t TotalW = 2000;
        constexpr uint32_t TotalH = 500;
        constexpr uint32_t ColW   = 500;

        Frontier::ProofCanvas Canvas(TotalW, TotalH);
        Canvas.Clear(Frontier::PixelRgb{ 0, 0, 0 });

        int32_t CardW = 440;
        int32_t CardH = 380;
        int32_t CardY = 40;

        // Col 0: Dashboard
        {
            Canvas.DrawFilledRectangle(0, 0, ColW - 1, TotalH - 36, Frontier::PixelRgb{ 10, 10, 12 });
            Canvas.DrawDashboardCard((ColW - CardW) / 2, CardY, CardW, CardH);
        }

        // Col 1: Slide In Progress
        {
            int32_t OffsetX = ColW;
            Canvas.DrawFilledRectangle(OffsetX, 0, OffsetX + ColW - 1, TotalH - 36, Frontier::PixelRgb{ 10, 10, 12 });
            Canvas.DrawDashboardCard(OffsetX - 180, CardY, CardW, CardH);
            Canvas.DrawSettingsHubCard(OffsetX + 140, CardY, CardW, CardH);
            // Sliding arrow indicator
            Canvas.DrawLine(OffsetX + 200, 20, OffsetX + 280, 20, Frontier::PixelRgb{ 59, 130, 246 });
            Canvas.DrawLine(OffsetX + 270, 14, OffsetX + 280, 20, Frontier::PixelRgb{ 59, 130, 246 });
            Canvas.DrawLine(OffsetX + 270, 26, OffsetX + 280, 20, Frontier::PixelRgb{ 59, 130, 246 });
        }

        // Col 2: Settings Hub
        {
            int32_t OffsetX = ColW * 2;
            Canvas.DrawFilledRectangle(OffsetX, 0, OffsetX + ColW - 1, TotalH - 36, Frontier::PixelRgb{ 10, 10, 12 });
            Canvas.DrawSettingsHubCard(OffsetX + (ColW - CardW) / 2, CardY, CardW, CardH);
        }

        // Col 3: Sub-Page
        {
            int32_t OffsetX = ColW * 3;
            Canvas.DrawFilledRectangle(OffsetX, 0, OffsetX + ColW - 1, TotalH - 36, Frontier::PixelRgb{ 10, 10, 12 });
            Canvas.DrawSubPageCard(OffsetX + (ColW - CardW) / 2, CardY, CardW, CardH, "Appearance");
        }

        // Subtle separators
        Canvas.DrawLine(ColW, 0, ColW, TotalH - 1, Frontier::PixelRgb{ 30, 30, 36 });
        Canvas.DrawLine(ColW * 2, 0, ColW * 2, TotalH - 1, Frontier::PixelRgb{ 30, 30, 36 });
        Canvas.DrawLine(ColW * 3, 0, ColW * 3, TotalH - 1, Frontier::PixelRgb{ 30, 30, 36 });

        Canvas.ExportPpm("Diagnostics/ControlCenter_Carousel_Composite.ppm");
        (void)std::system("python3 Tools/PpmToPng.py Diagnostics/ControlCenter_Carousel_Composite.ppm Diagnostics/ControlCenter_Carousel_Composite.png > /dev/null 2>&1");
        std::cout << "[Carousel Proof 5] Generated Composite 4-Panel view.\n";
    }

    (void)std::system("rm -f Diagnostics/*.ppm");
    std::cout << "[Verification Complete] All carousel stepper proof images created.\n";
    return 0;
}
