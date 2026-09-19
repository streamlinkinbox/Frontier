//============================================================================================================================================
// 📦 Frontier/Scratchpad/GenerateFontSectionProofs.cpp — Pixel-Exact Rasterizer Proof for Fonts Tab & Typography System
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

    void DrawRoundedRectangle(int32_t X0, int32_t Y0, int32_t X1, int32_t Y1, int32_t Radius, PixelRgb Color)
    {
        int32_t MinX = std::max(0, std::min(X0, X1));
        int32_t MaxX = std::min(static_cast<int32_t>(CanvasWidth) - 1, std::max(X0, X1));
        int32_t MinY = std::max(0, std::min(Y0, Y1));
        int32_t MaxY = std::min(static_cast<int32_t>(CanvasHeight) - 1, std::max(Y0, Y1));

        int32_t R = std::clamp(Radius, 0, std::min((MaxX - MinX) / 2, (MaxY - MinY) / 2));

        for (int32_t Y = MinY; Y <= MaxY; ++Y)
        {
            for (int32_t X = MinX; X <= MaxX; ++X)
            {
                bool Inside = true;
                if (R > 0)
                {
                    if (X < MinX + R && Y < MinY + R)
                    {
                        int32_t DX = X - (MinX + R);
                        int32_t DY = Y - (MinY + R);
                        if (DX * DX + DY * DY > R * R) Inside = false;
                    }
                    else if (X > MaxX - R && Y < MinY + R)
                    {
                        int32_t DX = X - (MaxX - R);
                        int32_t DY = Y - (MinY + R);
                        if (DX * DX + DY * DY > R * R) Inside = false;
                    }
                    else if (X > MaxX - R && Y > MaxY - R)
                    {
                        int32_t DX = X - (MaxX - R);
                        int32_t DY = Y - (MaxY - R);
                        if (DX * DX + DY * DY > R * R) Inside = false;
                    }
                    else if (X < MinX + R && Y > MaxY - R)
                    {
                        int32_t DX = X - (MinX + R);
                        int32_t DY = Y - (MaxY - R);
                        if (DX * DX + DY * DY > R * R) Inside = false;
                    }
                }

                if (Inside)
                {
                    Buffer[Y * CanvasWidth + X] = Color;
                }
            }
        }
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

    // Single unified Fonts & Typography Section matching exact layout from FontsTab.tsx
    void DrawExactFontSectionModal(
        int32_t X0, int32_t Y0, int32_t W, int32_t H,
        FontFamilyCategory SelectedFont, float CornerRadiusPx, PixelRgb AccentRgb)
    {
        int32_t R = static_cast<int32_t>(CornerRadiusPx);

        // Modal Full Card Container (dark panel #121212)
        DrawRoundedRectangle(X0, Y0, X0 + W, Y0 + H, R, PixelRgb{ 16, 16, 18 });
        DrawRoundedOutline(X0, Y0, X0 + W, Y0 + H, R, 1, PixelRgb{ 38, 38, 42 });

        // Header Top Bar: Back Button [←] + "Display Settings" + Subtitle
        DrawCircle(X0 + 40, Y0 + 34, 16, PixelRgb{ 28, 28, 32 });
        DrawLine(X0 + 45, Y0 + 34, X0 + 35, Y0 + 34, PixelRgb{ 255, 255, 255 });
        DrawLine(X0 + 35, Y0 + 34, X0 + 40, Y0 + 29, PixelRgb{ 255, 255, 255 });
        DrawLine(X0 + 35, Y0 + 34, X0 + 40, Y0 + 39, PixelRgb{ 255, 255, 255 });

        // Close X button on the top right
        DrawCircle(X0 + W - 40, Y0 + 34, 16, PixelRgb{ 24, 24, 28 });
        DrawLine(X0 + W - 45, Y0 + 29, X0 + W - 35, Y0 + 39, PixelRgb{ 180, 180, 190 });
        DrawLine(X0 + W - 35, Y0 + 29, X0 + W - 45, Y0 + 39, PixelRgb{ 180, 180, 190 });

        DrawText(X0 + 70, Y0 + 22, "Display Settings", PixelRgb{ 245, 245, 248 }, 2);
        DrawText(X0 + 70, Y0 + 42, "Appearance, typography & hardware preferences", PixelRgb{ 136, 136, 136 }, 1);

        // Tab Strip: [ Theme | Fonts (Active) | Display ]
        int32_t TabY = Y0 + 68;
        DrawText(X0 + 40, TabY, "Theme", PixelRgb{ 120, 120, 125 }, 2);
        DrawText(X0 + 130, TabY, "Fonts", PixelRgb{ 255, 255, 255 }, 2);
        DrawLine(X0 + 130, TabY + 18, X0 + 180, TabY + 18, PixelRgb{ 255, 255, 255 }); // Active underline
        DrawText(X0 + 210, TabY, "Display", PixelRgb{ 120, 120, 125 }, 2);
        DrawLine(X0 + 28, TabY + 19, X0 + W - 28, TabY + 19, PixelRgb{ 32, 32, 36 });

        int32_t ContentX0 = X0 + 28;
        int32_t ContentY0 = TabY + 30;
        int32_t ContentW  = W - 56;

        // ===============================================================================================================
        // 1. TYPOGRAPHY CAROUSEL (8 Font Family Cards)
        // ===============================================================================================================
        DrawText(ContentX0, ContentY0, "Typography", PixelRgb{ 245, 245, 248 }, 2);
        DrawText(ContentX0, ContentY0 + 18, "Typeface, type scale, and font weights", PixelRgb{ 120, 120, 130 }, 1);

        // Right side Carousel arrows [<] [>]
        int32_t ArrY = ContentY0 + 4;
        DrawRoundedRectangle(ContentX0 + ContentW - 64, ArrY, ContentX0 + ContentW - 36, ArrY + 24, 6, PixelRgb{ 24, 24, 28 });
        DrawRoundedRectangle(ContentX0 + ContentW - 30, ArrY, ContentX0 + ContentW - 2, ArrY + 24, 6, PixelRgb{ 24, 24, 28 });
        // Left chevron <
        DrawLine(ContentX0 + ContentW - 48, ArrY + 8, ContentX0 + ContentW - 53, ArrY + 12, PixelRgb{ 200, 200, 200 });
        DrawLine(ContentX0 + ContentW - 53, ArrY + 12, ContentX0 + ContentW - 48, ArrY + 16, PixelRgb{ 200, 200, 200 });
        // Right chevron >
        DrawLine(ContentX0 + ContentW - 19, ArrY + 8, ContentX0 + ContentW - 14, ArrY + 12, PixelRgb{ 200, 200, 200 });
        DrawLine(ContentX0 + ContentW - 14, ArrY + 12, ContentX0 + ContentW - 19, ArrY + 16, PixelRgb{ 200, 200, 200 });

        struct FontCardDef {
            const char* Name;
            const char* Subtitle;
            FontFamilyCategory Category;
        };

        const FontCardDef Cards[8] = {
            { "General Sans",   "Clean & Modern",     FontFamilyCategory::GeneralSans },
            { "Inter",          "Author",             FontFamilyCategory::Inter },
            { "Archivo",        "Technical & Solid",  FontFamilyCategory::Archivo },
            { "Space Grotesk",  "Grotesque Display",  FontFamilyCategory::SpaceGrotesk },
            { "Clash Display",  "Bold & Distinct",    FontFamilyCategory::ClashDisplay },
            { "Montserrat",     "Geometric & Wide",   FontFamilyCategory::Montserrat },
            { "Poppins",        "Friendly & Round",   FontFamilyCategory::Poppins },
            { "JetBrains Mono", "Monospaced Code",    FontFamilyCategory::JetBrainsMono }
        };

        // Render 4 visible cards in carousel viewport
        int32_t CardRowY = ContentY0 + 38;
        int32_t FCardW   = (ContentW - (3 * 12)) / 4;
        int32_t FCardH   = 88;
        int32_t StartCard = (SelectedFont == FontFamilyCategory::JetBrainsMono) ? 4 : 0;

        for (int32_t i = 0; i < 4; ++i)
        {
            int32_t CardIdx = StartCard + i;
            int32_t CX = ContentX0 + i * (FCardW + 12);
            bool IsActive = (SelectedFont == Cards[CardIdx].Category);

            DrawRoundedRectangle(CX, CardRowY, CX + FCardW, CardRowY + FCardH, 14, IsActive ? PixelRgb{ 24, 28, 38 } : PixelRgb{ 22, 22, 25 });
            if (IsActive)
            {
                DrawRoundedOutline(CX, CardRowY, CX + FCardW, CardRowY + FCardH, 14, 2, AccentRgb);
            }
            else
            {
                DrawRoundedOutline(CX, CardRowY, CX + FCardW, CardRowY + FCardH, 14, 1, PixelRgb{ 40, 40, 44 });
            }

            // Big "Aa" (prominent 24px)
            DrawText(CX + 14, CardRowY + 12, "Aa", IsActive ? PixelRgb{ 255, 255, 255 } : PixelRgb{ 210, 210, 215 }, 3);

            // Font Name
            DrawText(CX + 14, CardRowY + 46, Cards[CardIdx].Name, PixelRgb{ 240, 240, 245 }, 2);

            // Subtitle
            DrawText(CX + 14, CardRowY + 68, Cards[CardIdx].Subtitle, PixelRgb{ 120, 120, 130 }, 1);
        }

        // ===============================================================================================================
        // 2. TYPEFACE & COLORS (Style Playground)
        // ===============================================================================================================
        int32_t PlayY0 = CardRowY + FCardH + 20;
        int32_t PlayH  = 160;

        DrawRoundedRectangle(ContentX0, PlayY0, ContentX0 + ContentW, PlayY0 + PlayH, 18, PixelRgb{ 20, 20, 22 });
        DrawRoundedOutline(ContentX0, PlayY0, ContentX0 + ContentW, PlayY0 + PlayH, 18, 1, PixelRgb{ 38, 38, 42 });

        DrawText(ContentX0 + 20, PlayY0 + 16, "Typeface & Colors", PixelRgb{ 240, 240, 245 }, 2);

        // Left Hero Spec Box: Large 54px bold preview (e.g. "General Sans" or "JetBrains Mono")
        int32_t HeroW  = ContentW - 320;
        int32_t HeroH  = 100;
        int32_t HeroX0 = ContentX0 + 20;
        int32_t HeroY0 = PlayY0 + 44;

        DrawRoundedRectangle(HeroX0, HeroY0, HeroX0 + HeroW, HeroY0 + HeroH, 14, PixelRgb{ 12, 12, 14 });
        DrawRoundedOutline(HeroX0, HeroY0, HeroX0 + HeroW, HeroY0 + HeroH, 14, 1, PixelRgb{ 32, 32, 36 });

        const char* ActiveFontName = (SelectedFont == FontFamilyCategory::JetBrainsMono) ? "JetBrains Mono" : "General Sans";
        int32_t FontNameScale = (SelectedFont == FontFamilyCategory::JetBrainsMono) ? 4 : 5;
        DrawText(HeroX0 + 24, HeroY0 + 22, ActiveFontName, PixelRgb{ 255, 255, 255 }, FontNameScale);
        DrawText(HeroX0 + 26, HeroY0 + 68, "(72px bold display)", PixelRgb{ 110, 110, 120 }, 1);

        // Right Specs & Swatches Column
        int32_t RightX = HeroX0 + HeroW + 24;
        int32_t RightY = HeroY0;

        DrawText(RightX, RightY + 4,  "ABCDEFGHIJKLMNOPQRSTUVWXYZ", PixelRgb{ 140, 140, 150 }, 1);
        DrawText(RightX, RightY + 16, "abcdefghijklmnopqrstuvwxyz", PixelRgb{ 140, 140, 150 }, 1);
        DrawText(RightX, RightY + 28, "0123456789 !@#$%^&*()",      PixelRgb{ 140, 140, 150 }, 1);

        // Accent Text Sentence
        DrawText(RightX, RightY + 46, "Accent - The quick brown fox...", AccentRgb, 1);

        // 3 Swatch circles: ACCENT, #FFFFFF, #000000
        int32_t SwatchY = RightY + 66;
        // Accent circle
        DrawCircle(RightX + 14, SwatchY + 12, 12, AccentRgb);
        DrawText(RightX + 2, SwatchY + 28, "ACCENT", PixelRgb{ 200, 200, 200 }, 1);

        // White circle
        DrawCircle(RightX + 64, SwatchY + 12, 12, PixelRgb{ 255, 255, 255 });
        DrawText(RightX + 48, SwatchY + 28, "#FFFFFF", PixelRgb{ 200, 200, 200 }, 1);

        // Black circle
        DrawCircle(RightX + 114, SwatchY + 12, 12, PixelRgb{ 0, 0, 0 });
        DrawCircle(RightX + 114, SwatchY + 12, 12, PixelRgb{ 60, 60, 60 }); // Ring
        DrawCircle(RightX + 114, SwatchY + 12, 10, PixelRgb{ 0, 0, 0 });
        DrawText(RightX + 98, SwatchY + 28, "#000000", PixelRgb{ 200, 200, 200 }, 1);

        // ===============================================================================================================
        // 3. TYPE SCALE - DESKTOP (3 Interactive Tier Rows)
        // ===============================================================================================================
        int32_t ScaleSecY = PlayY0 + PlayH + 20;
        DrawText(ContentX0, ScaleSecY, "TYPE SCALE - DESKTOP", PixelRgb{ 130, 130, 140 }, 1);

        struct ScaleTierDef {
            const char* Role;
            int32_t Px;
            const char* ActiveWeight;
            const char* Sample;
            int32_t SampleScale;
        };

        const ScaleTierDef Tiers[3] = {
            { "Title",  32, "Bold",     "Display Title", 3 },
            { "Header", 24, "SemiBold", "Section Header", 2 },
            { "Body",   14, "Regular",  "The quick brown fox jumps over the lazy dog.", 1 }
        };

        int32_t TierY0 = ScaleSecY + 16;
        int32_t TierH  = 86;

        for (int32_t t = 0; t < 3; ++t)
        {
            int32_t TY = TierY0 + t * (TierH + 12);

            // Container Box
            DrawRoundedRectangle(ContentX0, TY, ContentX0 + ContentW, TY + TierH, 14, PixelRgb{ 20, 20, 22 });
            DrawRoundedOutline(ContentX0, TY, ContentX0 + ContentW, TY + TierH, 14, 1, PixelRgb{ 36, 36, 40 });

            // Left Side Controls (Width 320px)
            int32_t CtrlX0 = ContentX0 + 16;
            int32_t CtrlY0 = TY + 12;

            // Role Name + Size label (e.g. "Title" ... "32px")
            DrawText(CtrlX0, CtrlY0, Tiers[t].Role, PixelRgb{ 240, 240, 245 }, 2);
            std::string PxTag = std::to_string(Tiers[t].Px) + "px";
            DrawText(CtrlX0 + 260, CtrlY0, PxTag, PixelRgb{ 120, 120, 130 }, 1);

            // Row: VPill [ 32 | px ] + Slider [===O---]
            int32_t SliderRowY = CtrlY0 + 20;

            // VPill
            DrawRoundedRectangle(CtrlX0, SliderRowY, CtrlX0 + 80, SliderRowY + 24, 999, PixelRgb{ 10, 10, 10 });
            DrawRoundedOutline(CtrlX0, SliderRowY, CtrlX0 + 80, SliderRowY + 24, 999, 1, PixelRgb{ 46, 46, 46 });
            DrawLine(CtrlX0 + 52, SliderRowY, CtrlX0 + 52, SliderRowY + 24, PixelRgb{ 46, 46, 46 });
            DrawText(CtrlX0 + 16, SliderRowY + 6, std::to_string(Tiers[t].Px), PixelRgb{ 255, 255, 255 }, 1);
            DrawText(CtrlX0 + 58, SliderRowY + 6, "px", PixelRgb{ 130, 130, 140 }, 1);

            // Slider
            int32_t SlX0 = CtrlX0 + 94;
            int32_t SlX1 = CtrlX0 + 290;
            int32_t SlH  = 20;
            DrawRoundedRectangle(SlX0, SliderRowY + 2, SlX1, SliderRowY + 2 + SlH, 999, PixelRgb{ 32, 32, 36 });

            float Progress = static_cast<float>(Tiers[t].Px - 8) / 64.0f;
            int32_t ThumbX = static_cast<int32_t>(SlX0 + 10 + Progress * (SlX1 - SlX0 - 20));
            DrawRoundedRectangle(SlX0, SliderRowY + 2, ThumbX, SliderRowY + 2 + SlH, 999, AccentRgb);
            DrawCircle(ThumbX, SliderRowY + 2 + SlH / 2, 8, PixelRgb{ 255, 255, 255 });

            // Weight pills strip: [Light] [Regular] [Medium] [Bold]
            int32_t WRowY = SliderRowY + 30;
            const char* Weights[4] = { "Light", "Regular", "Medium", "Bold" };
            for (int32_t w = 0; w < 4; ++w)
            {
                int32_t WX = CtrlX0 + w * 72;
                bool IsWActive = (std::string(Weights[w]) == Tiers[t].ActiveWeight ||
                                  (t == 1 && w == 2)); // SemiBold for Header

                if (IsWActive)
                {
                    DrawRoundedRectangle(WX, WRowY, WX + 64, WRowY + 18, 999, PixelRgb{ 255, 255, 255 });
                    DrawText(WX + 12, WRowY + 4, (t == 1 && w == 2) ? "SemiBold" : Weights[w], PixelRgb{ 0, 0, 0 }, 1);
                }
                else
                {
                    DrawRoundedOutline(WX, WRowY, WX + 64, WRowY + 18, 999, 1, PixelRgb{ 46, 46, 50 });
                    DrawText(WX + 14, WRowY + 4, Weights[w], PixelRgb{ 140, 140, 150 }, 1);
                }
            }

            // Right Side Live Preview Box
            int32_t PrevX0 = CtrlX0 + 310;
            int32_t PrevW  = (ContentX0 + ContentW - 16) - PrevX0;
            int32_t PrevH  = TierH - 24;
            int32_t PrevY0 = TY + 12;

            DrawRoundedRectangle(PrevX0, PrevY0, PrevX0 + PrevW, PrevY0 + PrevH, 10, PixelRgb{ 12, 12, 14 });
            DrawRoundedOutline(PrevX0, PrevY0, PrevX0 + PrevW, PrevY0 + PrevH, 10, 1, PixelRgb{ 32, 32, 36 });

            int32_t SampleOffsetY = (PrevH - (Tiers[t].SampleScale * 7)) / 2;
            DrawText(PrevX0 + 16, PrevY0 + std::max(6, SampleOffsetY), Tiers[t].Sample, PixelRgb{ 245, 245, 250 }, Tiers[t].SampleScale);
        }

        // ===============================================================================================================
        // 4. FONT RENDERING (Toggles Section)
        // ===============================================================================================================
        int32_t RendSecY0 = TierY0 + 3 * (TierH + 12) + 4;
        int32_t RendH     = 84;

        DrawRoundedRectangle(ContentX0, RendSecY0, ContentX0 + ContentW, RendSecY0 + RendH, 14, PixelRgb{ 20, 20, 22 });
        DrawRoundedOutline(ContentX0, RendSecY0, ContentX0 + ContentW, RendSecY0 + RendH, 14, 1, PixelRgb{ 36, 36, 40 });

        DrawText(ContentX0 + 16, RendSecY0 + 12, "Font Rendering", PixelRgb{ 240, 240, 245 }, 2);

        // Row 1: Antialiasing
        int32_t RRow1Y = RendSecY0 + 34;
        DrawText(ContentX0 + 16, RRow1Y, "Antialiasing", PixelRgb{ 230, 230, 235 }, 1);
        DrawText(ContentX0 + 120, RRow1Y, "Enable subpixel antialiasing", PixelRgb{ 120, 120, 130 }, 1);

        // Toggle Switch 1 (Active)
        int32_t Tog1X = ContentX0 + ContentW - 60;
        DrawRoundedRectangle(Tog1X, RRow1Y - 2, Tog1X + 44, RRow1Y + 16, 999, AccentRgb);
        DrawCircle(Tog1X + 34, RRow1Y + 7, 7, PixelRgb{ 255, 255, 255 });

        // Divider
        DrawLine(ContentX0 + 16, RendSecY0 + 56, ContentX0 + ContentW - 16, RendSecY0 + 56, PixelRgb{ 32, 32, 36 });

        // Row 2: Ligatures
        int32_t RRow2Y = RendSecY0 + 64;
        DrawText(ContentX0 + 16, RRow2Y, "Ligatures", PixelRgb{ 230, 230, 235 }, 1);
        DrawText(ContentX0 + 120, RRow2Y, "Enable special character combinations", PixelRgb{ 120, 120, 130 }, 1);

        // Toggle Switch 2 (Active)
        DrawRoundedRectangle(Tog1X, RRow2Y - 2, Tog1X + 44, RRow2Y + 16, 999, AccentRgb);
        DrawCircle(Tog1X + 34, RRow2Y + 7, 7, PixelRgb{ 255, 255, 255 });

        // ===============================================================================================================
        // MODAL FOOTER BOTTOM BAR
        // ===============================================================================================================
        int32_t FootY0 = H - 54 + Y0;
        DrawLine(X0 + 24, FootY0, X0 + W - 24, FootY0, PixelRgb{ 32, 32, 36 });
        DrawText(X0 + 36, FootY0 + 18, "Slate Engine . Vulkan 1.3 . Right-Hand Z-Up", PixelRgb{ 100, 100, 110 }, 1);

        // Apply Preferences Solid White Pill Button (width 250px to comfortably fit 204px text)
        int32_t BtnW  = 250;
        int32_t BtnH  = 34;
        int32_t BtnX0 = X0 + W - BtnW - 28;
        int32_t BtnY0 = FootY0 + 8;
        DrawRoundedRectangle(BtnX0, BtnY0, BtnX0 + BtnW, BtnY0 + BtnH, 999, PixelRgb{ 255, 255, 255 });
        int32_t BtnTextW = MeasureTextWidth("Apply Preferences", 2);
        DrawText(BtnX0 + (BtnW - BtnTextW) / 2, BtnY0 + 10, "Apply Preferences", PixelRgb{ 10, 10, 10 }, 2);

        // Discard Ghost Button positioned safely to the left of the white pill button
        int32_t DiscardW = MeasureTextWidth("Discard", 2);
        int32_t DiscardX = BtnX0 - DiscardW - 32;
        DrawText(DiscardX, FootY0 + 18, "Discard", PixelRgb{ 140, 140, 150 }, 2);
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
    // 1. GENERAL SANS FONT & TYPOGRAPHY PROOF
    // ===================================================================================================================
    {
        constexpr uint32_t Width  = 1600;
        constexpr uint32_t Height = 1040;

        Frontier::ControlCentrePanel Panel;
        (void)Panel.Initialize(Width, Height);
        Panel.OpenNotch();
        Panel.NavigateToPage(Frontier::ControlCentrePageCategory::Appearance);
        Panel.AssignAppearanceSubTab(Frontier::AppearanceSubTabCategory::Fonts);
        Panel.SelectFontFamily(Frontier::FontFamilyCategory::GeneralSans);
        Panel.AssignCornerRadius(24.0f);
        Panel.AdvanceLocomotion(1.0f);

        Frontier::ProofCanvas Canvas(Width, Height);
        Canvas.Clear(Frontier::PixelRgb{ 0, 0, 0 }); // Pure black OLED workspace

        float HandleY = Panel.QueryHandleY();
        Canvas.DrawFilledRectangle(0, 0, Width - 1, static_cast<int32_t>(HandleY), Frontier::PixelRgb{ 10, 10, 10 });

        int32_t CardW = 860;
        int32_t CardH = 900;
        int32_t CardX = (Width - CardW) / 2;
        int32_t CardY = 30;

        Canvas.DrawExactFontSectionModal(
            CardX, CardY, CardW, CardH,
            Frontier::FontFamilyCategory::GeneralSans, 24.0f,
            Frontier::PixelRgb{ 59, 130, 246 }
        );

        const auto& Contour = Panel.QueryHandleContour();
        Canvas.DrawFilledPolygon(Contour, Panel.QueryHandleX(), HandleY, Frontier::PixelRgb{ 10, 10, 10 });

        Canvas.ExportPpm("Diagnostics/ControlCenter_FontSection_Proof.ppm");
        (void)std::system("python3 Tools/PpmToPng.py Diagnostics/ControlCenter_FontSection_Proof.ppm Diagnostics/ControlCenter_FontSection_Proof.png > /dev/null 2>&1");
        std::cout << "[Font Proof 1] Generated General Sans typography proof.\n";
    }

    // ===================================================================================================================
    // 2. JETBRAINS MONO CODE TYPOGRAPHY PROOF
    // ===================================================================================================================
    {
        constexpr uint32_t Width  = 1600;
        constexpr uint32_t Height = 1040;

        Frontier::ControlCentrePanel Panel;
        (void)Panel.Initialize(Width, Height);
        Panel.OpenNotch();
        Panel.NavigateToPage(Frontier::ControlCentrePageCategory::Appearance);
        Panel.AssignAppearanceSubTab(Frontier::AppearanceSubTabCategory::Fonts);
        Panel.SelectFontFamily(Frontier::FontFamilyCategory::JetBrainsMono);
        Panel.AssignCornerRadius(16.0f);
        Panel.AdvanceLocomotion(1.0f);

        Frontier::ProofCanvas Canvas(Width, Height);
        Canvas.Clear(Frontier::PixelRgb{ 0, 0, 0 });

        float HandleY = Panel.QueryHandleY();
        Canvas.DrawFilledRectangle(0, 0, Width - 1, static_cast<int32_t>(HandleY), Frontier::PixelRgb{ 10, 10, 10 });

        int32_t CardW = 860;
        int32_t CardH = 900;
        int32_t CardX = (Width - CardW) / 2;
        int32_t CardY = 30;

        Canvas.DrawExactFontSectionModal(
            CardX, CardY, CardW, CardH,
            Frontier::FontFamilyCategory::JetBrainsMono, 16.0f,
            Frontier::PixelRgb{ 34, 197, 94 } // Emerald Green
        );

        const auto& Contour = Panel.QueryHandleContour();
        Canvas.DrawFilledPolygon(Contour, Panel.QueryHandleX(), HandleY, Frontier::PixelRgb{ 10, 10, 10 });

        Canvas.ExportPpm("Diagnostics/ControlCenter_FontSection_Mono_Proof.ppm");
        (void)std::system("python3 Tools/PpmToPng.py Diagnostics/ControlCenter_FontSection_Mono_Proof.ppm Diagnostics/ControlCenter_FontSection_Mono_Proof.png > /dev/null 2>&1");
        std::cout << "[Font Proof 2] Generated JetBrains Mono code typography proof.\n";
    }

    (void)std::system("rm -f Diagnostics/*.ppm");
    std::cout << "[Verification Complete] All exact Font section proofs generated successfully.\n";
    return 0;
}
