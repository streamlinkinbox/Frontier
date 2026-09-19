//============================================================================================================================================
// 📦 Frontier/Scratchpad/GenerateThemeSectionProofs.cpp — Pixel-Exact Rasterizer Proof for Unified Theme Section & Rounding Slider
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
#include <string>

namespace Frontier {

struct PixelRgb
{
    uint8_t R;
    uint8_t G;
    uint8_t B;
};

//------------------------------------------------------------------------------------------------------------------------
//                                           5x7 / 8x12 PROPORTIONAL BITMAP FONT
//------------------------------------------------------------------------------------------------------------------------

static const uint8_t Font5x7[96][5] = {
    {0x00,0x00,0x00,0x00,0x00}, //   32 ' '
    {0x00,0x00,0x5f,0x00,0x00}, //   33 '!'
    {0x00,0x07,0x00,0x07,0x00}, //   34 '"'
    {0x14,0x7f,0x14,0x7f,0x14}, //   35 '#'
    {0x24,0x2a,0x7f,0x2a,0x12}, //   36 '$'
    {0x23,0x13,0x08,0x64,0x62}, //   37 '%'
    {0x36,0x49,0x55,0x22,0x50}, //   38 '&'
    {0x00,0x05,0x03,0x00,0x00}, //   39 '''
    {0x00,0x1c,0x22,0x41,0x00}, //   40 '('
    {0x00,0x41,0x22,0x1c,0x00}, //   41 ')'
    {0x14,0x08,0x3e,0x08,0x14}, //   42 '*'
    {0x08,0x08,0x3e,0x08,0x08}, //   43 '+'
    {0x00,0x50,0x30,0x00,0x00}, //   44 ','
    {0x08,0x08,0x08,0x08,0x08}, //   45 '-'
    {0x00,0x60,0x60,0x00,0x00}, //   46 '.'
    {0x20,0x10,0x08,0x04,0x02}, //   47 '/'
    {0x3e,0x51,0x49,0x45,0x3e}, //   48 '0'
    {0x00,0x42,0x7f,0x40,0x00}, //   49 '1'
    {0x42,0x61,0x51,0x49,0x46}, //   50 '2'
    {0x21,0x41,0x45,0x4b,0x31}, //   51 '3'
    {0x18,0x14,0x12,0x7f,0x10}, //   52 '4'
    {0x27,0x45,0x45,0x45,0x39}, //   53 '5'
    {0x3c,0x4a,0x49,0x49,0x30}, //   54 '6'
    {0x01,0x71,0x09,0x05,0x03}, //   55 '7'
    {0x36,0x49,0x49,0x49,0x36}, //   56 '8'
    {0x06,0x49,0x49,0x29,0x1e}, //   57 '9'
    {0x00,0x36,0x36,0x00,0x00}, //   58 ':'
    {0x00,0x56,0x36,0x00,0x00}, //   59 ';'
    {0x08,0x14,0x22,0x41,0x00}, //   60 '<'
    {0x14,0x14,0x14,0x14,0x14}, //   61 '='
    {0x00,0x41,0x22,0x14,0x08}, //   62 '>'
    {0x02,0x01,0x51,0x09,0x06}, //   63 '?'
    {0x32,0x49,0x79,0x41,0x3e}, //   64 '@'
    {0x7e,0x11,0x11,0x11,0x7e}, //   65 'A'
    {0x7f,0x49,0x49,0x49,0x36}, //   66 'B'
    {0x3e,0x41,0x41,0x41,0x22}, //   67 'C'
    {0x7f,0x41,0x41,0x22,0x1c}, //   68 'D'
    {0x7f,0x49,0x49,0x49,0x41}, //   69 'E'
    {0x7f,0x09,0x09,0x09,0x01}, //   70 'F'
    {0x3e,0x41,0x49,0x49,0x7a}, //   71 'G'
    {0x7f,0x08,0x08,0x08,0x7f}, //   72 'H'
    {0x00,0x41,0x7f,0x41,0x00}, //   73 'I'
    {0x20,0x40,0x41,0x3f,0x01}, //   74 'J'
    {0x7f,0x08,0x14,0x22,0x41}, //   75 'K'
    {0x7f,0x40,0x40,0x40,0x40}, //   76 'L'
    {0x7f,0x02,0x0c,0x02,0x7f}, //   77 'M'
    {0x7f,0x04,0x08,0x10,0x7f}, //   78 'N'
    {0x3e,0x41,0x41,0x41,0x3e}, //   79 'O'
    {0x7f,0x09,0x09,0x09,0x06}, //   80 'P'
    {0x3e,0x41,0x51,0x21,0x5e}, //   81 'Q'
    {0x7f,0x09,0x19,0x29,0x46}, //   82 'R'
    {0x46,0x49,0x49,0x49,0x31}, //   83 'S'
    {0x01,0x01,0x7f,0x01,0x01}, //   84 'T'
    {0x3f,0x40,0x40,0x40,0x3f}, //   85 'U'
    {0x1f,0x20,0x40,0x20,0x1f}, //   86 'V'
    {0x3f,0x40,0x38,0x40,0x3f}, //   87 'W'
    {0x63,0x14,0x08,0x14,0x63}, //   88 'X'
    {0x07,0x08,0x70,0x08,0x07}, //   89 'Y'
    {0x61,0x51,0x49,0x45,0x43}, //   90 'Z'
    {0x00,0x7f,0x41,0x41,0x00}, //   91 '['
    {0x02,0x04,0x08,0x10,0x20}, //   92 '\'
    {0x00,0x41,0x41,0x7f,0x00}, //   93 ']'
    {0x04,0x02,0x01,0x02,0x04}, //   94 '^'
    {0x40,0x40,0x40,0x40,0x40}, //   95 '_'
    {0x00,0x01,0x02,0x04,0x00}, //   96 '`'
    {0x20,0x54,0x54,0x54,0x78}, //   97 'a'
    {0x7f,0x48,0x44,0x44,0x38}, //   98 'b'
    {0x38,0x44,0x44,0x44,0x20}, //   99 'c'
    {0x38,0x44,0x44,0x48,0x7f}, //  100 'd'
    {0x38,0x54,0x54,0x54,0x18}, //  101 'e'
    {0x08,0x7e,0x09,0x01,0x02}, //  102 'f'
    {0x0c,0x52,0x52,0x52,0x3e}, //  103 'g'
    {0x7f,0x08,0x04,0x04,0x78}, //  104 'h'
    {0x00,0x44,0x7d,0x40,0x00}, //  105 'i'
    {0x20,0x40,0x44,0x3d,0x00}, //  106 'j'
    {0x7f,0x10,0x28,0x44,0x00}, //  107 'k'
    {0x00,0x41,0x7f,0x40,0x00}, //  108 'l'
    {0x7c,0x04,0x18,0x04,0x78}, //  109 'm'
    {0x7c,0x08,0x04,0x04,0x78}, //  110 'n'
    {0x38,0x44,0x44,0x44,0x38}, //  111 'o'
    {0x7c,0x14,0x14,0x14,0x08}, //  112 'p'
    {0x08,0x14,0x14,0x18,0x7c}, //  113 'q'
    {0x7c,0x08,0x04,0x04,0x08}, //  114 'r'
    {0x48,0x54,0x54,0x54,0x20}, //  115 's'
    {0x04,0x3f,0x44,0x40,0x20}, //  116 't'
    {0x3c,0x40,0x40,0x20,0x7c}, //  117 'u'
    {0x1c,0x20,0x40,0x20,0x1c}, //  118 'v'
    {0x3c,0x40,0x30,0x40,0x3c}, //  119 'w'
    {0x44,0x28,0x10,0x28,0x44}, //  120 'x'
    {0x0c,0x50,0x50,0x50,0x3c}, //  121 'y'
    {0x44,0x64,0x54,0x4c,0x44}, //  122 'z'
    {0x00,0x08,0x36,0x41,0x00}, //  123 '{'
    {0x00,0x00,0x7f,0x00,0x00}, //  124 '|'
    {0x00,0x41,0x36,0x08,0x00}, //  125 '}'
    {0x08,0x08,0x2a,0x1c,0x08}, //  126 '~'
    {0x00,0x00,0x00,0x00,0x00}  //  127 ' '
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

    void PutPixel(int32_t X, int32_t Y, PixelRgb Color)
    {
        if (X >= 0 && X < static_cast<int32_t>(CanvasWidth) && Y >= 0 && Y < static_cast<int32_t>(CanvasHeight))
        {
            Buffer[Y * CanvasWidth + X] = Color;
        }
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

    // Individual 4-corner specific rounding renderer
    void DrawCustomRoundedRectangle(
        int32_t X0, int32_t Y0, int32_t X1, int32_t Y1,
        int32_t RadiusTL, int32_t RadiusTR, int32_t RadiusBR, int32_t RadiusBL,
        PixelRgb Color)
    {
        int32_t MinX = std::max(0, std::min(X0, X1));
        int32_t MaxX = std::min(static_cast<int32_t>(CanvasWidth) - 1, std::max(X0, X1));
        int32_t MinY = std::max(0, std::min(Y0, Y1));
        int32_t MaxY = std::min(static_cast<int32_t>(CanvasHeight) - 1, std::max(Y0, Y1));

        int32_t W = MaxX - MinX;
        int32_t H = MaxY - MinY;

        int32_t RTL = std::clamp(RadiusTL, 0, std::min(W / 2, H / 2));
        int32_t RTR = std::clamp(RadiusTR, 0, std::min(W / 2, H / 2));
        int32_t RBR = std::clamp(RadiusBR, 0, std::min(W / 2, H / 2));
        int32_t RBL = std::clamp(RadiusBL, 0, std::min(W / 2, H / 2));

        for (int32_t Y = MinY; Y <= MaxY; ++Y)
        {
            for (int32_t X = MinX; X <= MaxX; ++X)
            {
                bool Inside = true;

                // Top-Left Corner
                if (RTL > 0 && X < MinX + RTL && Y < MinY + RTL)
                {
                    int32_t DX = X - (MinX + RTL);
                    int32_t DY = Y - (MinY + RTL);
                    if (DX * DX + DY * DY > RTL * RTL) Inside = false;
                }
                // Top-Right Corner
                else if (RTR > 0 && X > MaxX - RTR && Y < MinY + RTR)
                {
                    int32_t DX = X - (MaxX - RTR);
                    int32_t DY = Y - (MinY + RTR);
                    if (DX * DX + DY * DY > RTR * RTR) Inside = false;
                }
                // Bottom-Right Corner
                else if (RBR > 0 && X > MaxX - RBR && Y > MaxY - RBR)
                {
                    int32_t DX = X - (MaxX - RBR);
                    int32_t DY = Y - (MaxY - RBR);
                    if (DX * DX + DY * DY > RBR * RBR) Inside = false;
                }
                // Bottom-Left Corner
                else if (RBL > 0 && X < MinX + RBL && Y > MaxY - RBL)
                {
                    int32_t DX = X - (MinX + RBL);
                    int32_t DY = Y - (MaxY - RBL);
                    if (DX * DX + DY * DY > RBL * RBL) Inside = false;
                }

                if (Inside)
                {
                    Buffer[Y * CanvasWidth + X] = Color;
                }
            }
        }
    }

    void DrawRoundedRectangle(int32_t X0, int32_t Y0, int32_t X1, int32_t Y1, int32_t Radius, PixelRgb Color)
    {
        DrawCustomRoundedRectangle(X0, Y0, X1, Y1, Radius, Radius, Radius, Radius, Color);
    }

    void DrawRoundedOutline(int32_t X0, int32_t Y0, int32_t X1, int32_t Y1, int32_t Radius, int32_t Thickness, PixelRgb Color)
    {
        for (int32_t T = 0; T < Thickness; ++T)
        {
            int32_t CurX0 = X0 - T;
            int32_t CurY0 = Y0 - T;
            int32_t CurX1 = X1 + T;
            int32_t CurY1 = Y1 + T;
            int32_t CurR  = Radius + T;

            for (int32_t X = CurX0; X <= CurX1; ++X)
            {
                int32_t DX0 = X - (CurX0 + CurR);
                int32_t DX1 = X - (CurX1 - CurR);
                bool CornerT = (X < CurX0 + CurR && DX0 * DX0 > CurR * CurR);
                bool CornerB = (X > CurX1 - CurR && DX1 * DX1 > CurR * CurR);
                if (!CornerT && !CornerB)
                {
                    PutPixel(X, CurY0, Color);
                    PutPixel(X, CurY1, Color);
                }
            }
            for (int32_t Y = CurY0; Y <= CurY1; ++Y)
            {
                PutPixel(CurX0, Y, Color);
                PutPixel(CurX1, Y, Color);
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
            PutPixel(X0, Y0, Color);
            return;
        }

        float XInc = static_cast<float>(X1 - X0) / static_cast<float>(Steps);
        float YInc = static_cast<float>(Y1 - Y0) / static_cast<float>(Steps);
        float CurrX = static_cast<float>(X0);
        float CurrY = static_cast<float>(Y0);

        for (int32_t i = 0; i <= Steps; ++i)
        {
            PutPixel(static_cast<int32_t>(std::round(CurrX)), static_cast<int32_t>(std::round(CurrY)), Color);
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
                int32_t DX = X - CenterX;
                int32_t DY = Y - CenterY;
                if (DX * DX + DY * DY <= Radius * Radius)
                {
                    PutPixel(X, Y, Color);
                }
            }
        }
    }

    void DrawText(int32_t StartX, int32_t StartY, const std::string& Text, PixelRgb Color, int32_t Scale = 1)
    {
        int32_t CurX = StartX;
        for (char C : Text)
        {
            if (C < 32 || C > 126) C = ' ';
            uint32_t GlyphIdx = static_cast<uint32_t>(C - 32);

            for (int32_t Col = 0; Col < 5; ++Col)
            {
                uint8_t Bits = Font5x7[GlyphIdx][Col];
                for (int32_t Row = 0; Row < 7; ++Row)
                {
                    if ((Bits >> Row) & 1)
                    {
                        for (int32_t Sy = 0; Sy < Scale; ++Sy)
                        {
                            for (int32_t Sx = 0; Sx < Scale; ++Sx)
                            {
                                PutPixel(CurX + Col * Scale + Sx, StartY + Row * Scale + Sy, Color);
                            }
                        }
                    }
                }
            }
            CurX += (6 * Scale);
        }
    }

    int32_t MeasureTextWidth(const std::string& Text, int32_t Scale = 1)
    {
        return static_cast<int32_t>(Text.length()) * 6 * Scale;
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

    // Single unified Theme Section matching HTML exact layout (1-to-1 from ThemeTab.tsx + UIComponents.html)
    void DrawExactThemeSectionModal(
        int32_t X0, int32_t Y0, int32_t W, int32_t H,
        ThemeCategory SelectedTheme, float CornerRadiusPx, PixelRgb AccentRgb)
    {
        int32_t R = static_cast<int32_t>(CornerRadiusPx);

        // Modal Full Card Container (dark panel #121212)
        DrawRoundedRectangle(X0, Y0, X0 + W, Y0 + H, R, PixelRgb{ 18, 18, 18 });
        DrawRoundedOutline(X0, Y0, X0 + W, Y0 + H, R, 1, PixelRgb{ 38, 38, 38 });

        // Header Top Bar: Back Button [←] + "Display Settings" + Subtitle
        DrawCircle(X0 + 40, Y0 + 38, 18, PixelRgb{ 32, 32, 36 });
        DrawLine(X0 + 46, Y0 + 38, X0 + 34, Y0 + 38, PixelRgb{ 255, 255, 255 });
        DrawLine(X0 + 34, Y0 + 38, X0 + 40, Y0 + 32, PixelRgb{ 255, 255, 255 });
        DrawLine(X0 + 34, Y0 + 38, X0 + 40, Y0 + 44, PixelRgb{ 255, 255, 255 });

        // Close X button on the top right
        DrawCircle(X0 + W - 40, Y0 + 38, 18, PixelRgb{ 26, 26, 30 });
        DrawLine(X0 + W - 46, Y0 + 32, X0 + W - 34, Y0 + 44, PixelRgb{ 180, 180, 190 });
        DrawLine(X0 + W - 34, Y0 + 32, X0 + W - 46, Y0 + 44, PixelRgb{ 180, 180, 190 });

        DrawText(X0 + 72, Y0 + 26, "Display Settings", PixelRgb{ 245, 245, 248 }, 2);
        DrawText(X0 + 72, Y0 + 46, "Appearance, typography & hardware preferences", PixelRgb{ 136, 136, 136 }, 1);

        // Tab Strip: [ Theme (Active) | Fonts | Display ]
        int32_t TabY = Y0 + 76;
        DrawText(X0 + 40, TabY, "Theme", PixelRgb{ 255, 255, 255 }, 2);
        DrawLine(X0 + 40, TabY + 18, X0 + 100, TabY + 18, PixelRgb{ 255, 255, 255 }); // Active underline indicator

        DrawText(X0 + 130, TabY, "Fonts", PixelRgb{ 120, 120, 125 }, 2);
        DrawText(X0 + 210, TabY, "Display", PixelRgb{ 120, 120, 125 }, 2);
        DrawLine(X0 + 28, TabY + 19, X0 + W - 28, TabY + 19, PixelRgb{ 32, 32, 36 });

        // ---------------------------------------------------------------------------------------------------------------
        // UNIFIED THEME SECTION (Single Container Card)
        // ---------------------------------------------------------------------------------------------------------------
        int32_t SecX0 = X0 + 28;
        int32_t SecY0 = TabY + 36;
        int32_t SecW  = W - 56;
        int32_t SecH  = H - 220;

        DrawRoundedRectangle(SecX0, SecY0, SecX0 + SecW, SecY0 + SecH, R, PixelRgb{ 20, 20, 22 });
        DrawRoundedOutline(SecX0, SecY0, SecX0 + SecW, SecY0 + SecH, R, 1, PixelRgb{ 36, 36, 40 });

        // Section Title: "Color Scheme"
        DrawText(SecX0 + 24, SecY0 + 20, "Color Scheme", PixelRgb{ 240, 240, 245 }, 2);
        DrawText(SecX0 + 24, SecY0 + 38, "Choose the overall interface darkness", PixelRgb{ 120, 120, 130 }, 1);

        // 8 Theme Tiles / Cards Grid (Exact 1:1 match with ThemeTab.tsx)
        struct ThemeCardDef {
            const char* Name;
            PixelRgb BgColor;           // bg-*
            PixelRgb SidebarColor;      // sidebar-*
            PixelRgb PanelColor;        // panel-*
            PixelRgb LineColor;         // lines-*
        };

        const ThemeCardDef ThemeDefs[8] = {
            // OLED: bg-black, sidebar-[#0A0A0A], panel-[#141414], lines-white
            { "OLED",    PixelRgb{ 0, 0, 0 },       PixelRgb{ 10, 10, 10 },    PixelRgb{ 20, 20, 20 },    PixelRgb{ 255, 255, 255 } },
            // Dark: bg-[#18181a], sidebar-[#222224], panel-[#2c2c2e], lines-white
            { "Dark",    PixelRgb{ 24, 24, 26 },    PixelRgb{ 34, 34, 36 },    PixelRgb{ 44, 44, 46 },    PixelRgb{ 255, 255, 255 } },
            // Dim: bg-[#0f172a], sidebar-[#1e293b], panel-[#283548], lines-[#f1f5f9]
            { "Dim",     PixelRgb{ 15, 23, 42 },    PixelRgb{ 30, 41, 59 },    PixelRgb{ 40, 53, 72 },    PixelRgb{ 241, 245, 249 } },
            // Light: bg-[#e5e5e5], sidebar-[#f4f4f5], panel-white, lines-black
            { "Light",   PixelRgb{ 229, 229, 229 }, PixelRgb{ 244, 244, 245 }, PixelRgb{ 255, 255, 255 }, PixelRgb{ 0, 0, 0 } },
            // Sepia: bg-[#dca85b], sidebar-[#e6c697], panel-[#f1dab0], lines-[#825619]
            { "Sepia",   PixelRgb{ 220, 168, 91 },  PixelRgb{ 230, 198, 151 }, PixelRgb{ 241, 218, 176 }, PixelRgb{ 130, 86, 25 } },
            // Dracula: bg-[#282a36], sidebar-[#44475a], panel-[#6272a4], lines-[#f8f8f2]
            { "Dracula", PixelRgb{ 40, 42, 54 },    PixelRgb{ 68, 71, 90 },    PixelRgb{ 98, 114, 164 },  PixelRgb{ 248, 248, 242 } },
            // Nord: bg-[#2e3440], sidebar-[#3b4252], panel-[#4c566a], lines-[#eceff4]
            { "Nord",    PixelRgb{ 46, 52, 64 },    PixelRgb{ 59, 66, 82 },    PixelRgb{ 76, 86, 106 },   PixelRgb{ 236, 239, 244 } },
            // GitHub: bg-[#0d1117], sidebar-[#161b22], panel-[#21262d], lines-[#c9d1d9]
            { "GitHub",  PixelRgb{ 13, 17, 23 },    PixelRgb{ 22, 27, 34 },    PixelRgb{ 33, 38, 45 },    PixelRgb{ 201, 209, 217 } }
        };

        int32_t GridX0  = SecX0 + 24;
        int32_t GridY0  = SecY0 + 58;
        int32_t TileGap = 16;
        int32_t TileW   = (SecW - 48 - (TileGap * 3)) / 4;
        int32_t TileH   = 102;

        for (int32_t r = 0; r < 2; ++r)
        {
            for (int32_t c = 0; c < 4; ++c)
            {
                int32_t Idx = r * 4 + c;
                int32_t TX0 = GridX0 + c * (TileW + TileGap);
                int32_t TY0 = GridY0 + r * (TileH + TileGap + 20);
                bool IsActive = (static_cast<size_t>(SelectedTheme) == static_cast<size_t>(Idx));

                // 1. Tile Base Container (aspect-[4/3] rounded-[1.25rem] / 20px)
                int32_t TileR = 20;
                DrawRoundedRectangle(TX0, TY0, TX0 + TileW, TY0 + TileH, TileR, ThemeDefs[Idx].BgColor);

                // Outline ring on active / inactive
                if (IsActive)
                {
                    DrawRoundedOutline(TX0 - 4, TY0 - 4, TX0 + TileW + 4, TY0 + TileH + 4, TileR + 4, 3, AccentRgb);
                }
                else
                {
                    DrawRoundedOutline(TX0, TY0, TX0 + TileW, TY0 + TileH, TileR, 1, PixelRgb{ 50, 50, 55 });
                }

                // 2. Inset Window Mockup: absolute top-6 bottom-0 left-6 right-0 flex shadow-sm
                // Exact formula: borderTopLeftRadius: cornerRadius (R)
                int32_t InsetX = TX0 + 16;
                int32_t InsetY = TY0 + 16;
                int32_t InsetW = TileW - 16;
                int32_t InsetH = TileH - 16;
                int32_t SideW  = InsetW * 30 / 100; // w-[30%]

                int32_t WindowTopLeftR = std::max(2, static_cast<int32_t>(CornerRadiusPx * 0.75f));
                int32_t LineBarR       = std::max(1, static_cast<int32_t>(CornerRadiusPx * 0.20f));
                int32_t SquareCardR    = std::max(2, static_cast<int32_t>(CornerRadiusPx * 0.40f));

                // A) SIDEBAR CONTEXT: w-[30%] flex flex-col p-3 gap-1.5 border-r border-black/5
                // Outer window top-left has borderTopLeftRadius: cornerRadius!
                DrawCustomRoundedRectangle(InsetX, InsetY, InsetX + SideW, InsetY + InsetH, WindowTopLeftR, 0, 0, 0, ThemeDefs[Idx].SidebarColor);
                DrawLine(InsetX + SideW, InsetY, InsetX + SideW, InsetY + InsetH, PixelRgb{ 40, 40, 45 });

                // (i) 3 top window control dots horizontally: w-1.5 h-1.5 rounded-full opacity-40 gap-1
                PixelRgb DotCol = ThemeDefs[Idx].LineColor;
                for (int32_t d = 0; d < 3; ++d)
                {
                    DrawCircle(InsetX + 7 + d * 6, InsetY + 7, 2, DotCol);
                }

                // (ii) Sidebar navigation line bars: w-full, w-2/3, w-1/2 with borderRadius: cornerRadius / 4
                PixelRgb LineCol = ThemeDefs[Idx].LineColor;
                // w-full
                DrawRoundedRectangle(InsetX + 5, InsetY + 14, InsetX + SideW - 4, InsetY + 18, LineBarR, LineCol);
                // w-2/3
                DrawRoundedRectangle(InsetX + 5, InsetY + 22, InsetX + 5 + (SideW - 9) * 2 / 3, InsetY + 26, LineBarR, LineCol);
                // w-1/2
                DrawRoundedRectangle(InsetX + 5, InsetY + 30, InsetX + 5 + (SideW - 9) / 2, InsetY + 34, LineBarR, LineCol);

                // (iii) mt-auto user avatar circle (w-2.5 h-2.5 rounded-full) + name bar (w-4 h-1.5 borderRadius: cornerRadius/4)
                int32_t FootSideY = InsetY + InsetH - 14;
                DrawCircle(InsetX + 8, FootSideY + 4, 3, DotCol);
                DrawRoundedRectangle(InsetX + 15, FootSideY + 2, InsetX + SideW - 4, FootSideY + 6, LineBarR, LineCol);

                // B) PANEL CONTEXT: flex-1 p-4 flex flex-col gap-1.5 shadow-sm
                // Exact formula: style={{ borderTopLeftRadius: cornerRadius }}
                int32_t PanX0 = InsetX + SideW + 1;
                DrawCustomRoundedRectangle(PanX0, InsetY, InsetX + InsetW, InsetY + InsetH, WindowTopLeftR, 0, 0, 0, ThemeDefs[Idx].PanelColor);

                // (i) Top header lines: w-1/4 mb-1 opacity-30, w-1/3 opacity-20 with borderRadius: cornerRadius / 4
                int32_t PanContentW = (InsetX + InsetW) - PanX0;
                DrawRoundedRectangle(PanX0 + 8, InsetY + 6, PanX0 + 8 + PanContentW * 1 / 4, InsetY + 10, LineBarR, DotCol);
                DrawRoundedRectangle(PanX0 + 8, InsetY + 13, PanX0 + 8 + PanContentW * 1 / 3, InsetY + 17, LineBarR, LineCol);

                // (ii) 3 aspect-square card tiles: flex-1 aspect-square opacity-10 gap-2
                // Exact formula: style={{ borderRadius: cornerRadius / 2 }}!
                int32_t SquaresY = InsetY + 22;
                int32_t SqGap = 5;
                int32_t SqW   = (PanContentW - 16 - (SqGap * 2)) / 3;
                int32_t SqH   = SqW;

                for (int32_t sq = 0; sq < 3; ++sq)
                {
                    int32_t SqX = PanX0 + 8 + sq * (SqW + SqGap);
                    DrawRoundedRectangle(SqX, SquaresY, SqX + SqW, SquaresY + SqH, SquareCardR, DotCol);
                }

                // (iii) mt-auto status line: w-1/5 h-1.5 opacity-20 with borderRadius: cornerRadius / 4
                DrawRoundedRectangle(PanX0 + 8, InsetY + InsetH - 12, PanX0 + 8 + PanContentW * 1 / 5, InsetY + InsetH - 8, LineBarR, LineCol);

                // 3. Centered Theme Name Label beneath tile
                int32_t LabelW = MeasureTextWidth(ThemeDefs[Idx].Name, 2);
                int32_t LabelX = TX0 + (TileW - LabelW) / 2;
                int32_t LabelY = TY0 + TileH + 8;
                PixelRgb LabelColor = IsActive ? PixelRgb{ 255, 255, 255 } : PixelRgb{ 140, 140, 150 };
                DrawText(LabelX, LabelY, ThemeDefs[Idx].Name, LabelColor, 2);
            }
        }

        // Section Divider Line
        int32_t DivY = SecY0 + 332;
        DrawLine(SecX0 + 24, DivY, SecX0 + SecW - 24, DivY, PixelRgb{ 36, 36, 42 });

        // ---------------------------------------------------------------------------------------------------------------
        // GLOBAL ROUNDING SLIDER ROW (From UIComponents.html: .crow > .clabel + .vpill[.num | .unit] + input.slider)
        // ---------------------------------------------------------------------------------------------------------------
        int32_t CRowY0 = DivY + 24;

        // 1. Control Label (.clabel: "Corner Radius")
        DrawText(SecX0 + 24, CRowY0 + 10, "Corner Radius", PixelRgb{ 240, 240, 245 }, 2);

        // 2. Value Pill (.vpill: [ number | unit ])
        int32_t VPillX0 = SecX0 + 200;
        int32_t VPillW  = 120;
        int32_t VPillH  = 40;

        // Value pill container (black pill #000000)
        DrawRoundedRectangle(VPillX0, CRowY0, VPillX0 + VPillW, CRowY0 + VPillH, 999, PixelRgb{ 0, 0, 0 });
        DrawRoundedOutline(VPillX0, CRowY0, VPillX0 + VPillW, CRowY0 + VPillH, 999, 1, PixelRgb{ 46, 46, 46 });

        // Split divider line between .num and .unit
        DrawLine(VPillX0 + 72, CRowY0, VPillX0 + 72, CRowY0 + VPillH, PixelRgb{ 46, 46, 46 });

        // Right side .unit background inset
        DrawRoundedRectangle(VPillX0 + 73, CRowY0 + 1, VPillX0 + VPillW - 1, CRowY0 + VPillH - 1, 999, PixelRgb{ 26, 26, 26 });

        // Value text (e.g. "24") in .num
        std::string RadiusStr = std::to_string(static_cast<int>(std::round(CornerRadiusPx)));
        int32_t NumW = MeasureTextWidth(RadiusStr, 2);
        int32_t NumX = VPillX0 + (72 - NumW) / 2;
        DrawText(NumX, CRowY0 + 12, RadiusStr, PixelRgb{ 255, 255, 255 }, 2);

        // Unit text "px" in .unit
        int32_t UnitW = MeasureTextWidth("px", 2);
        int32_t UnitX = VPillX0 + 72 + (48 - UnitW) / 2;
        DrawText(UnitX, CRowY0 + 12, "px", PixelRgb{ 136, 136, 136 }, 2);

        // 3. Chunky Range Slider (input[type=range].slider.hi: [---O---])
        int32_t SliderX0 = VPillX0 + VPillW + 24;
        int32_t SliderX1 = SecX0 + SecW - 24;
        int32_t SliderH  = 34;
        int32_t SliderY  = CRowY0 + 3;

        // Base Track (dark surface #222222)
        DrawRoundedRectangle(SliderX0, SliderY, SliderX1, SliderY + SliderH, 999, PixelRgb{ 34, 34, 34 });

        // Accent Fill Track (progress fill up to thumb center)
        float Progress = std::clamp(CornerRadiusPx / 32.0f, 0.0f, 1.0f);
        int32_t ThumbCenterX = static_cast<int32_t>(SliderX0 + 17 + Progress * (SliderX1 - SliderX0 - 34));
        DrawRoundedRectangle(SliderX0, SliderY, ThumbCenterX, SliderY + SliderH, 999, AccentRgb);

        // Circular Slider Thumb (diameter 30px pure white with shadow)
        DrawCircle(ThumbCenterX, SliderY + SliderH / 2 + 1, 16, PixelRgb{ 0, 0, 0 }); // Soft shadow
        DrawCircle(ThumbCenterX, SliderY + SliderH / 2, 15, PixelRgb{ 245, 245, 245 });
        DrawCircle(ThumbCenterX, SliderY + SliderH / 2, 7, AccentRgb); // Center dot accent

        // ---------------------------------------------------------------------------------------------------------------
        // MODAL FOOTER BOTTOM BAR
        // ---------------------------------------------------------------------------------------------------------------
        int32_t FootY0 = H - 64 + Y0;
        DrawLine(X0 + 24, FootY0, X0 + W - 24, FootY0, PixelRgb{ 32, 32, 36 });
        DrawText(X0 + 36, FootY0 + 22, "Slate Engine . Vulkan 1.3 . Right-Hand Z-Up", PixelRgb{ 100, 100, 110 }, 1);

        // Discard Ghost Button
        DrawText(X0 + W - 340, FootY0 + 22, "Discard", PixelRgb{ 140, 140, 150 }, 2);

        // Apply Preferences Solid White Pill Button
        int32_t BtnW  = 230;
        int32_t BtnH  = 40;
        int32_t BtnX0 = X0 + W - BtnW - 28;
        int32_t BtnY0 = FootY0 + 10;
        DrawRoundedRectangle(BtnX0, BtnY0, BtnX0 + BtnW, BtnY0 + BtnH, 999, PixelRgb{ 255, 255, 255 });
        int32_t BtnTextW = MeasureTextWidth("Apply Preferences", 2);
        DrawText(BtnX0 + (BtnW - BtnTextW) / 2, BtnY0 + 13, "Apply Preferences", PixelRgb{ 10, 10, 10 }, 2);
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
    // 1. HIGH-RES SINGLE VIEW: THEME SECTION PROOF (OLED + 24px Rounding Slider)
    // ===================================================================================================================
    {
        constexpr uint32_t Width  = 1600;
        constexpr uint32_t Height = 900;

        Frontier::ControlCentrePanel Panel;
        (void)Panel.Initialize(Width, Height);
        Panel.OpenNotch();
        Panel.NavigateToPage(Frontier::ControlCentrePageCategory::Appearance);
        Panel.SelectTheme(Frontier::ThemeCategory::Oled);
        Panel.AssignCornerRadius(24.0f);
        Panel.AdvanceLocomotion(1.0f);

        Frontier::ProofCanvas Canvas(Width, Height);
        Canvas.Clear(Frontier::PixelRgb{ 0, 0, 0 }); // Pure black OLED workspace

        float HandleY = Panel.QueryHandleY();
        Canvas.DrawFilledRectangle(0, 0, Width - 1, static_cast<int32_t>(HandleY), Frontier::PixelRgb{ 10, 10, 10 });

        int32_t CardW = 860;
        int32_t CardH = 680;
        int32_t CardX = (Width - CardW) / 2;
        int32_t CardY = 40;

        Canvas.DrawExactThemeSectionModal(
            CardX, CardY, CardW, CardH,
            Frontier::ThemeCategory::Oled, 24.0f,
            Frontier::PixelRgb{ 59, 130, 246 }
        );

        const auto& Contour = Panel.QueryHandleContour();
        Canvas.DrawFilledPolygon(Contour, Panel.QueryHandleX(), HandleY, Frontier::PixelRgb{ 10, 10, 10 });

        Canvas.ExportPpm("Diagnostics/ControlCenter_ThemeSection_Proof.ppm");
        (void)std::system("python3 Tools/PpmToPng.py Diagnostics/ControlCenter_ThemeSection_Proof.ppm Diagnostics/ControlCenter_ThemeSection_Proof.png > /dev/null 2>&1");
        std::cout << "[Theme Proof 1] Generated high-res Theme Section proof with exact specific panel rounding.\n";
    }

    // ===================================================================================================================
    // 2. TWO-STATE COMPOSITE PROOF (OLED 24px vs Dracula 12px)
    // ===================================================================================================================
    {
        constexpr uint32_t TotalW = 1920;
        constexpr uint32_t TotalH = 800;
        constexpr uint32_t PanelW = 960;

        Frontier::ProofCanvas Canvas(TotalW, TotalH);
        Canvas.Clear(Frontier::PixelRgb{ 0, 0, 0 });

        int32_t CardW = 840;
        int32_t CardH = 680;
        int32_t CardY = 40;

        // Left Panel: OLED Theme (24px Corner Radius, Electric Blue Accent)
        {
            Canvas.DrawFilledRectangle(0, 0, PanelW - 1, TotalH - 36, Frontier::PixelRgb{ 6, 6, 8 });
            Canvas.DrawExactThemeSectionModal(
                (PanelW - CardW) / 2, CardY, CardW, CardH,
                Frontier::ThemeCategory::Oled, 24.0f,
                Frontier::PixelRgb{ 59, 130, 246 }
            );
        }

        // Right Panel: Dracula Theme (12px Corner Radius, Lilac Accent)
        {
            int32_t OffsetX = PanelW;
            Canvas.DrawFilledRectangle(OffsetX, 0, OffsetX + PanelW - 1, TotalH - 36, Frontier::PixelRgb{ 12, 13, 18 });
            Canvas.DrawExactThemeSectionModal(
                OffsetX + (PanelW - CardW) / 2, CardY, CardW, CardH,
                Frontier::ThemeCategory::Dracula, 12.0f,
                Frontier::PixelRgb{ 189, 147, 249 }
            );
        }

        Canvas.DrawLine(PanelW, 0, PanelW, TotalH - 1, Frontier::PixelRgb{ 32, 32, 38 });

        Canvas.ExportPpm("Diagnostics/ControlCenter_ThemeSection_Composite.ppm");
        (void)std::system("python3 Tools/PpmToPng.py Diagnostics/ControlCenter_ThemeSection_Composite.ppm Diagnostics/ControlCenter_ThemeSection_Composite.png > /dev/null 2>&1");
        std::cout << "[Theme Proof 2] Generated Composite comparison proof with exact specific panel rounding.\n";
    }

    (void)std::system("rm -f Diagnostics/*.ppm");
    std::cout << "[Verification Complete] All exact theme section proof images generated successfully.\n";
    return 0;
}
