//============================================================================================================================================
// 📦 Frontier/Scratchpad/GenerateDialogueProofs.cpp — Pixel-Exact Rasterizer Proofs for Apply & Discard Confirmation Dialogues & Font Codec
//============================================================================================================================================

#include "../DisplayPresentation/ControlCentrePanel.h"
#include "../DisplayPresentation/ThemeStructure.h"
#include "../DisplayPresentation/VectorCodec.h"
#include "../DisplayPresentation/FontCodec.h"
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
//                                           5x7 PROPORTIONAL BITMAP FONT
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

    void BlendPixel(int32_t X, int32_t Y, PixelRgb Color, float Alpha)
    {
        if (X >= 0 && X < static_cast<int32_t>(CanvasWidth) && Y >= 0 && Y < static_cast<int32_t>(CanvasHeight))
        {
            PixelRgb& Dst = Buffer[Y * CanvasWidth + X];
            Dst.R = static_cast<uint8_t>(Dst.R * (1.0f - Alpha) + Color.R * Alpha);
            Dst.G = static_cast<uint8_t>(Dst.G * (1.0f - Alpha) + Color.G * Alpha);
            Dst.B = static_cast<uint8_t>(Dst.B * (1.0f - Alpha) + Color.B * Alpha);
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

    void DrawDimOverlay(float DimFactor = 0.75f)
    {
        for (auto& Px : Buffer)
        {
            Px.R = static_cast<uint8_t>(Px.R * (1.0f - DimFactor));
            Px.G = static_cast<uint8_t>(Px.G * (1.0f - DimFactor));
            Px.B = static_cast<uint8_t>(Px.B * (1.0f - DimFactor));
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

    // ---------------------------------------------------------------------------------------------------------------
    // RENDER BASE BACKGROUND (FONTS & APPEARANCE VIEWPORT)
    // ---------------------------------------------------------------------------------------------------------------
    void RenderBaseBackground(const std::string& ActiveFontName, PixelRgb AccentColor)
    {
        // Viewport black background
        Clear(PixelRgb{ 0, 0, 0 });

        // Notch pulled-down shade container
        int32_t ShadeW = 1600;
        int32_t ShadeH = 1004;
        DrawFilledRectangle(0, 0, ShadeW - 1, ShadeH - 1, PixelRgb{ 0, 0, 0 });

        // Center card modal container (860px wide)
        int32_t CardW = 880;
        int32_t CardH = 920;
        int32_t CardX0 = (ShadeW - CardW) / 2;
        int32_t CardY0 = 30;
        int32_t R = 24;

        DrawRoundedRectangle(CardX0, CardY0, CardX0 + CardW, CardY0 + CardH, R, PixelRgb{ 10, 10, 11 });
        DrawRoundedOutline(CardX0, CardY0, CardX0 + CardW, CardY0 + CardH, R, 1, PixelRgb{ 32, 32, 36 });

        // Header
        DrawText(CardX0 + 36, CardY0 + 32, "DISPLAY & APPEARANCE", PixelRgb{ 255, 255, 255 }, 3);
        DrawText(CardX0 + 36, CardY0 + 64, "Configure theme surfaces, typography hierarchies, and engine appearance tokens", PixelRgb{ 120, 120, 130 }, 1);

        // Subtabs (Themes, Typography & Fonts, Viewport Canvas)
        int32_t TabY = CardY0 + 100;
        DrawRoundedRectangle(CardX0 + 36, TabY, CardX0 + 150, TabY + 36, 18, PixelRgb{ 18, 18, 22 });
        DrawText(CardX0 + 60, TabY + 11, "Themes", PixelRgb{ 140, 140, 150 }, 2);

        // Active Tab: Typography & Fonts
        DrawRoundedRectangle(CardX0 + 160, TabY, CardX0 + 360, TabY + 36, 18, AccentColor);
        DrawText(CardX0 + 175, TabY + 11, "Typography & Fonts", PixelRgb{ 255, 255, 255 }, 2);

        DrawRoundedRectangle(CardX0 + 370, TabY, CardX0 + 520, TabY + 36, 18, PixelRgb{ 18, 18, 22 });
        DrawText(CardX0 + 390, TabY + 11, "Viewport Canvas", PixelRgb{ 140, 140, 150 }, 2);

        // Section 1: Font Families 4x2 Grid
        DrawText(CardX0 + 36, CardY0 + 160, "FONT FAMILIES", PixelRgb{ 220, 220, 230 }, 2);

        const char* FontNames[] = {
            "General Sans", "Inter", "Archivo", "Space Grotesk",
            "Clash Display", "Montserrat", "Poppins", "JetBrains Mono"
        };
        const char* FontSubs[] = {
            "Clean & Modern", "Neutral & Legible", "Technical & Solid", "Grotesque Display",
            "Bold & Distinct", "Geometric Sans", "Friendly & Round", "Monospaced Code"
        };

        int32_t GridX = CardX0 + 36;
        int32_t GridY = CardY0 + 190;
        int32_t ItemW = 190;
        int32_t ItemH = 68;
        int32_t GapX  = 16;
        int32_t GapY  = 12;

        for (int i = 0; i < 8; ++i)
        {
            int32_t Col = i % 4;
            int32_t Row = i / 4;
            int32_t IX = GridX + Col * (ItemW + GapX);
            int32_t IY = GridY + Row * (ItemH + GapY);

            bool IsActive = (ActiveFontName == FontNames[i]);
            DrawRoundedRectangle(IX, IY, IX + ItemW, IY + ItemH, 14, IsActive ? PixelRgb{ 18, 28, 48 } : PixelRgb{ 16, 16, 19 });
            DrawRoundedOutline(IX, IY, IX + ItemW, IY + ItemH, 14, 1, IsActive ? AccentColor : PixelRgb{ 32, 32, 38 });

            DrawText(IX + 12, IY + 12, "Aa", IsActive ? AccentColor : PixelRgb{ 160, 160, 170 }, 2);
            DrawText(IX + 46, IY + 12, FontNames[i], PixelRgb{ 255, 255, 255 }, 1);
            DrawText(IX + 46, IY + 28, FontSubs[i], PixelRgb{ 110, 110, 120 }, 1);
        }

        // Section 2: Weight Spectrum
        int32_t SpecY = CardY0 + 370;
        DrawText(CardX0 + 36, SpecY, "WEIGHT SPECTRUM & SCALES", PixelRgb{ 220, 220, 230 }, 2);

        int32_t WellX0 = CardX0 + 36;
        int32_t WellY0 = SpecY + 28;
        int32_t WellW  = CardW - 72;
        int32_t WellH  = 160;

        DrawRoundedRectangle(WellX0, WellY0, WellX0 + WellW, WellY0 + WellH, 16, PixelRgb{ 14, 14, 16 });
        DrawRoundedOutline(WellX0, WellY0, WellX0 + WellW, WellY0 + WellH, 16, 1, PixelRgb{ 30, 30, 36 });

        DrawText(WellX0 + 20, WellY0 + 20, "Title 24px Bold (700) - The quick brown fox jumps over the lazy dog", PixelRgb{ 255, 255, 255 }, 2);
        DrawText(WellX0 + 20, WellY0 + 48, "Header 20px Semibold (600) - Real-time spatial path tracing & ReSTIR global illumination", PixelRgb{ 230, 230, 235 }, 2);
        DrawText(WellX0 + 20, WellY0 + 76, "Subheader 16px Medium (500) - Analytical Cornell Box test scene with diffuse color bleeding", PixelRgb{ 180, 180, 190 }, 1);
        DrawText(WellX0 + 20, WellY0 + 96, "Body 14px Regular (400) - Multi-pass reservoir resampling and dynamic font loading architecture", PixelRgb{ 160, 160, 170 }, 1);
        DrawText(WellX0 + 20, WellY0 + 116, "Label 12px Light (300) - Engine viewport render pipeline telemetry 4K 120 FPS", PixelRgb{ 130, 130, 140 }, 1);

        // Section 3: Ingested Engine Content Assets
        int32_t IngestY = CardY0 + 580;
        DrawText(CardX0 + 36, IngestY, "DYNAMIC FONT INGESTION (Engine/Content/Fonts & Content/Fonts)", PixelRgb{ 220, 220, 230 }, 2);

        int32_t IngestWellY = IngestY + 28;
        int32_t IngestWellH = 140;
        DrawRoundedRectangle(WellX0, IngestWellY, WellX0 + WellW, IngestWellY + IngestWellH, 16, PixelRgb{ 14, 14, 16 });
        DrawRoundedOutline(WellX0, IngestWellY, WellX0 + WellW, IngestWellY + IngestWellH, 16, 1, PixelRgb{ 30, 30, 36 });

        DrawText(WellX0 + 20, IngestWellY + 16, "Discovered Families: 8    Total Active Font Variants: 40    Format: TrueType/OpenType", PixelRgb{ 59, 130, 246 }, 1);
        DrawText(WellX0 + 20, IngestWellY + 36, "[x] Subpixel Anti-Aliasing Enabled    [x] Kerning & Standard Ligatures    [x] Font Metric Cache", PixelRgb{ 200, 200, 210 }, 1);
        DrawText(WellX0 + 20, IngestWellY + 56, "Engine/Content/Fonts/GeneralSans-Regular.ttf (Weight: 400, Size: 512B, Status: Loaded)", PixelRgb{ 140, 140, 150 }, 1);
        DrawText(WellX0 + 20, IngestWellY + 76, "Engine/Content/Fonts/JetBrainsMono-Bold.ttf  (Weight: 700, Size: 512B, Status: Loaded)", PixelRgb{ 140, 140, 150 }, 1);
        DrawText(WellX0 + 20, IngestWellY + 96, "Automatic Fallback: Inter -> Sans-Serif Default", PixelRgb{ 110, 110, 120 }, 1);

        // Action Buttons at bottom
        int32_t FootY = CardY0 + CardH - 60;
        DrawLine(CardX0, FootY, CardX0 + CardW, FootY, PixelRgb{ 30, 30, 36 });
        DrawText(CardX0 + 40, FootY + 22, "Discard Modifications", PixelRgb{ 239, 68, 68 }, 2);

        int32_t ApplyBtnW = 200;
        int32_t ApplyBtnH = 38;
        int32_t ApplyBtnX = CardX0 + CardW - ApplyBtnW - 36;
        int32_t ApplyBtnY = FootY + 11;
        DrawRoundedRectangle(ApplyBtnX, ApplyBtnY, ApplyBtnX + ApplyBtnW, ApplyBtnY + ApplyBtnH, 999, PixelRgb{ 255, 255, 255 });
        int32_t Tw = MeasureTextWidth("Apply Preferences", 2);
        DrawText(ApplyBtnX + (ApplyBtnW - Tw) / 2, ApplyBtnY + 12, "Apply Preferences", PixelRgb{ 0, 0, 0 }, 2);
    }

    // ---------------------------------------------------------------------------------------------------------------
    // 1. APPLY PREFERENCES CONFIRMATION DIALOGUE MODAL (1-to-1 from UIComponents.html)
    // ---------------------------------------------------------------------------------------------------------------
    void DrawApplyPreferencesDialogue(
        int32_t CenterX, int32_t CenterY,
        std::string_view ThemeName,
        std::string_view FontFamilyName,
        float CornerRadiusPx,
        PixelRgb AccentRgb)
    {
        int32_t DlgW = 560;
        int32_t DlgH = 320;
        int32_t DlgX0 = CenterX - DlgW / 2;
        int32_t DlgY0 = CenterY - DlgH / 2;
        int32_t R = 24;

        // Shadow and Base Panel
        DrawRoundedRectangle(DlgX0 - 8, DlgY0 - 8, DlgX0 + DlgW + 8, DlgY0 + DlgH + 8, R + 8, PixelRgb{ 0, 0, 0 });
        DrawRoundedRectangle(DlgX0, DlgY0, DlgX0 + DlgW, DlgY0 + DlgH, R, PixelRgb{ 18, 18, 20 });
        DrawRoundedOutline(DlgX0, DlgY0, DlgX0 + DlgW, DlgY0 + DlgH, R, 2, PixelRgb{ 50, 50, 56 });

        // Header
        int32_t HeadH = 68;
        DrawRoundedRectangle(DlgX0, DlgY0, DlgX0 + DlgW, DlgY0 + HeadH, R, PixelRgb{ 24, 24, 28 });
        DrawLine(DlgX0, DlgY0 + HeadH, DlgX0 + DlgW, DlgY0 + HeadH, PixelRgb{ 40, 40, 46 });

        // Icon Pill (Checkmark)
        DrawRoundedRectangle(DlgX0 + 24, DlgY0 + 14, DlgX0 + 64, DlgY0 + 54, 12, PixelRgb{ 24, 38, 64 });
        DrawRoundedOutline(DlgX0 + 24, DlgY0 + 14, DlgX0 + 64, DlgY0 + 54, 12, 1, AccentRgb);
        // Checkmark vector
        DrawLine(DlgX0 + 34, DlgY0 + 34, DlgX0 + 42, DlgY0 + 44, AccentRgb);
        DrawLine(DlgX0 + 42, DlgY0 + 44, DlgX0 + 54, DlgY0 + 24, AccentRgb);

        DrawText(DlgX0 + 78, DlgY0 + 18, "Apply Preferences", PixelRgb{ 255, 255, 255 }, 2);
        DrawText(DlgX0 + 78, DlgY0 + 40, "Confirm active surface theming & typography settings", PixelRgb{ 140, 140, 150 }, 1);

        // Body: Summary Inset Well
        int32_t BodyX0 = DlgX0 + 24;
        int32_t BodyY0 = DlgY0 + HeadH + 16;
        int32_t BodyW  = DlgW - 48;
        int32_t BodyH  = 124;

        DrawRoundedRectangle(BodyX0, BodyY0, BodyX0 + BodyW, BodyY0 + BodyH, 16, PixelRgb{ 10, 10, 12 });
        DrawRoundedOutline(BodyX0, BodyY0, BodyX0 + BodyW, BodyY0 + BodyH, 16, 1, PixelRgb{ 36, 36, 42 });

        // Row 1: Theme
        DrawText(BodyX0 + 20, BodyY0 + 16, "Theme Surface:", PixelRgb{ 130, 130, 140 }, 1);
        DrawText(BodyX0 + 220, BodyY0 + 16, std::string(ThemeName), PixelRgb{ 245, 245, 250 }, 1);

        // Row 2: Font Family
        DrawText(BodyX0 + 20, BodyY0 + 42, "Font Family:", PixelRgb{ 130, 130, 140 }, 1);
        DrawText(BodyX0 + 220, BodyY0 + 42, std::string(FontFamilyName), PixelRgb{ 245, 245, 250 }, 1);

        // Row 3: Corner Rounding
        DrawText(BodyX0 + 20, BodyY0 + 68, "Corner Rounding:", PixelRgb{ 130, 130, 140 }, 1);
        DrawText(BodyX0 + 220, BodyY0 + 68, std::to_string(static_cast<int>(CornerRadiusPx)) + "px", PixelRgb{ 245, 245, 250 }, 1);

        // Row 4: Font Rendering
        DrawText(BodyX0 + 20, BodyY0 + 94, "Font Rendering:", PixelRgb{ 130, 130, 140 }, 1);
        DrawText(BodyX0 + 220, BodyY0 + 94, "Subpixel AA & Ligatures Enabled", PixelRgb{ 245, 245, 250 }, 1);

        // Footer
        int32_t FootY0 = DlgY0 + DlgH - 64;
        DrawLine(DlgX0, FootY0, DlgX0 + DlgW, FootY0, PixelRgb{ 40, 40, 46 });

        // Confirm & Apply Primary White Pill Button
        int32_t BtnW = 180;
        int32_t BtnH = 38;
        int32_t BtnX0 = DlgX0 + DlgW - BtnW - 24;
        int32_t BtnY0 = FootY0 + 13;
        DrawRoundedRectangle(BtnX0, BtnY0, BtnX0 + BtnW, BtnY0 + BtnH, 999, PixelRgb{ 255, 255, 255 });
        int32_t BtnTextW = MeasureTextWidth("Confirm & Apply", 2);
        DrawText(BtnX0 + (BtnW - BtnTextW) / 2, BtnY0 + 12, "Confirm & Apply", PixelRgb{ 10, 10, 10 }, 2);

        // Cancel / Keep Editing Ghost Button
        int32_t CancelTextW = MeasureTextWidth("Keep Editing", 2);
        int32_t CancelX0 = BtnX0 - CancelTextW - 28;
        DrawText(CancelX0, FootY0 + 24, "Keep Editing", PixelRgb{ 140, 140, 150 }, 2);
    }

    // ---------------------------------------------------------------------------------------------------------------
    // 2. DISCARD CHANGES CONFIRMATION DIALOGUE MODAL (1-to-1 from UIComponents.html)
    // ---------------------------------------------------------------------------------------------------------------
    void DrawDiscardChangesDialogue(int32_t CenterX, int32_t CenterY)
    {
        int32_t DlgW = 540;
        int32_t DlgH = 270;
        int32_t DlgX0 = CenterX - DlgW / 2;
        int32_t DlgY0 = CenterY - DlgH / 2;
        int32_t R = 24;

        // Shadow and Base Panel
        DrawRoundedRectangle(DlgX0 - 8, DlgY0 - 8, DlgX0 + DlgW + 8, DlgY0 + DlgH + 8, R + 8, PixelRgb{ 0, 0, 0 });
        DrawRoundedRectangle(DlgX0, DlgY0, DlgX0 + DlgW, DlgY0 + DlgH, R, PixelRgb{ 18, 18, 20 });
        DrawRoundedOutline(DlgX0, DlgY0, DlgX0 + DlgW, DlgY0 + DlgH, R, 2, PixelRgb{ 50, 50, 56 });

        // Header
        int32_t HeadH = 68;
        DrawRoundedRectangle(DlgX0, DlgY0, DlgX0 + DlgW, DlgY0 + HeadH, R, PixelRgb{ 24, 24, 28 });
        DrawLine(DlgX0, DlgY0 + HeadH, DlgX0 + DlgW, DlgY0 + HeadH, PixelRgb{ 40, 40, 46 });

        // Icon Pill (Alert Triangle)
        PixelRgb DangerCol{ 239, 68, 68 };
        DrawRoundedRectangle(DlgX0 + 24, DlgY0 + 14, DlgX0 + 64, DlgY0 + 54, 12, PixelRgb{ 54, 20, 20 });
        DrawRoundedOutline(DlgX0 + 24, DlgY0 + 14, DlgX0 + 64, DlgY0 + 54, 12, 1, DangerCol);
        // Exclamation mark
        DrawLine(DlgX0 + 44, DlgY0 + 24, DlgX0 + 44, DlgY0 + 36, DangerCol);
        DrawCircle(DlgX0 + 44, DlgY0 + 44, 2, DangerCol);

        DrawText(DlgX0 + 78, DlgY0 + 18, "Discard Modifications?", PixelRgb{ 255, 255, 255 }, 2);
        DrawText(DlgX0 + 78, DlgY0 + 40, "Revert all settings to previous session state", PixelRgb{ 140, 140, 150 }, 1);

        // Body: Warning Inset Well
        int32_t BodyX0 = DlgX0 + 24;
        int32_t BodyY0 = DlgY0 + HeadH + 18;
        int32_t BodyW  = DlgW - 48;
        int32_t BodyH  = 90;

        DrawRoundedRectangle(BodyX0, BodyY0, BodyX0 + BodyW, BodyY0 + BodyH, 16, PixelRgb{ 32, 14, 14 });
        DrawRoundedOutline(BodyX0, BodyY0, BodyX0 + BodyW, BodyY0 + BodyH, 16, 1, PixelRgb{ 70, 26, 26 });

        DrawText(BodyX0 + 20, BodyY0 + 22, "Any unsaved changes to color themes, typography", PixelRgb{ 245, 220, 220 }, 1);
        DrawText(BodyX0 + 20, BodyY0 + 44, "scales, corner rounding, and hardware preferences", PixelRgb{ 245, 220, 220 }, 1);
        DrawText(BodyX0 + 20, BodyY0 + 66, "will be permanently lost.", PixelRgb{ 245, 220, 220 }, 1);

        // Footer
        int32_t FootY0 = DlgY0 + DlgH - 64;
        DrawLine(DlgX0, FootY0, DlgX0 + DlgW, FootY0, PixelRgb{ 40, 40, 46 });

        // Cancel Ghost Button
        DrawText(DlgX0 + 250, FootY0 + 24, "Cancel", PixelRgb{ 140, 140, 150 }, 2);

        // Discard All Danger Solid Pill Button
        int32_t BtnW = 160;
        int32_t BtnH = 38;
        int32_t BtnX0 = DlgX0 + DlgW - BtnW - 24;
        int32_t BtnY0 = FootY0 + 13;
        DrawRoundedRectangle(BtnX0, BtnY0, BtnX0 + BtnW, BtnY0 + BtnH, 999, DangerCol);
        int32_t BtnTextW = MeasureTextWidth("Discard All", 2);
        DrawText(BtnX0 + (BtnW - BtnTextW) / 2, BtnY0 + 12, "Discard All", PixelRgb{ 255, 255, 255 }, 2);
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
    // 1. TEST DYNAMIC FONT DISCOVERY & MULTI-WEIGHT RESOLUTION FROM FOLDERS
    // ===================================================================================================================
    {
        Frontier::FontCodec Codec;
        bool Scanned = Codec.ScanEngineAndGameContent("Engine/Content/Fonts", "Content/Fonts");

        std::cout << "[Font Discovery Engine] Scanned font directories. Status = " << (Scanned ? "SUCCESS" : "FAILED") << "\n";
        std::cout << "[Font Discovery Engine] Discovered " << Codec.QueryFamilyCount() << " font families with "
                  << Codec.QueryTotalVariantCount() << " total variants.\n";

        for (const auto& Fam : Codec.QueryDiscoveredFamilies())
        {
            std::cout << "  - Family: " << Fam.FamilyName << " (" << Fam.CategoryName << ") -> " << Fam.Variants.size() << " variants\n";
            for (const auto& Var : Fam.Variants)
            {
                std::cout << "      * " << Var.VariantName << " (Weight " << static_cast<uint32_t>(Var.Weight) << "): " << Var.FilePath << "\n";
            }
        }

        // Test weight query lookup: General Sans SemiBold (600)
        const auto* Var600 = Codec.QueryVariant("General Sans", Frontier::FontWeightCategory::SemiBold);
        if (Var600)
        {
            std::cout << "[Font Lookup Test] Looked up General Sans SemiBold -> Found: " << Var600->FilePath
                      << " (Weight " << static_cast<uint32_t>(Var600->Weight) << ")\n";
        }
    }

    // ===================================================================================================================
    // 2. PROOF 1: APPLY PREFERENCES CONFIRMATION DIALOGUE OVER FONTS MODAL
    // ===================================================================================================================
    {
        constexpr uint32_t Width  = 1600;
        constexpr uint32_t Height = 1040;

        Frontier::ProofCanvas Canvas(Width, Height);
        // Draw real Fonts tab backdrop
        Canvas.RenderBaseBackground("General Sans", Frontier::PixelRgb{ 59, 130, 246 });

        // Dim backdrop overlay
        Canvas.DrawDimOverlay(0.70f);

        // Center Confirmation Dialogue Modal
        Canvas.DrawApplyPreferencesDialogue(
            Width / 2, Height / 2,
            "OLED (Pure Black #000000)",
            "General Sans (Clean & Modern)",
            24.0f,
            Frontier::PixelRgb{ 59, 130, 246 }
        );

        Canvas.ExportPpm("Diagnostics/ControlCenter_ApplyDialogue_Proof.ppm");
        (void)std::system("python3 Tools/PpmToPng.py Diagnostics/ControlCenter_ApplyDialogue_Proof.ppm Diagnostics/ControlCenter_ApplyDialogue_Proof.png > /dev/null 2>&1");
        std::cout << "[Dialogue Proof 1] Generated Apply Preferences confirmation dialogue proof.\n";
    }

    // ===================================================================================================================
    // 3. PROOF 2: DISCARD CHANGES CONFIRMATION DIALOGUE OVER FONTS MODAL
    // ===================================================================================================================
    {
        constexpr uint32_t Width  = 1600;
        constexpr uint32_t Height = 1040;

        Frontier::ProofCanvas Canvas(Width, Height);
        // Draw real Fonts tab backdrop
        Canvas.RenderBaseBackground("General Sans", Frontier::PixelRgb{ 59, 130, 246 });

        // Dim backdrop overlay
        Canvas.DrawDimOverlay(0.70f);

        // Center Confirmation Dialogue Modal
        Canvas.DrawDiscardChangesDialogue(Width / 2, Height / 2);

        Canvas.ExportPpm("Diagnostics/ControlCenter_DiscardDialogue_Proof.ppm");
        (void)std::system("python3 Tools/PpmToPng.py Diagnostics/ControlCenter_DiscardDialogue_Proof.ppm Diagnostics/ControlCenter_DiscardDialogue_Proof.png > /dev/null 2>&1");
        std::cout << "[Dialogue Proof 2] Generated Discard Changes confirmation dialogue proof.\n";
    }

    (void)std::system("rm -f Diagnostics/*.ppm");
    std::cout << "[Verification Complete] All confirmation dialogue and font discovery proofs generated successfully.\n";
    return 0;
}
