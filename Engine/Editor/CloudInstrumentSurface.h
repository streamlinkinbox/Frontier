//==============================================================================================================================================
//                                                         CLOUDINSTRUMENTSURFACE.H
//==============================================================================================================================================
// 📦 Drawing surface for the shipped cloud panel: the satellite map, the coverage histogram, the vertical section and the
//    morphology controls InspectorDepot/panels/clouds.js paints. Shares the depot's control kit with WindInstrumentSurface.h.

#pragma once

#include "WindInstrumentSurface.h"

namespace Frontier::CloudInstrument
{

// The depot's shared furniture — palette, text primitives, tape, stepper, card head — was converted with the
//    wind panel, which came first. It is the same kit, not a copy.
namespace Kit = Frontier::WindInstrument;

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE LAYER
//------------------------------------------------------------------------------------------------------------------------

struct CloudDraft
{
    float    Coverage = 0.46f;    // [0..1] P.coverage
    float    Density  = 0.62f;    // [0..1] P.density
    float    Altitude = 130.0f;   // [m]    P.altitude
    float    Scale    = 1.0f;     // [x]    P.scale
    float    Detail   = 0.55f;    // [0..1] P.detail
    float    Speed    = 1.0f;     // [x]    P.speed
    unsigned Tint     = 0xeef3f8; // [-]    P.tint
    unsigned Shade    = 0x5c6a7c; // [-]    P.shade
    bool     Linked   = true;     // [-]    P.windLinked
};

inline float Threshold(const CloudDraft& Layer) { return 1.0f - Layer.Coverage * 0.78f; }

inline const char* CloudName(float Coverage)
{
    return Coverage < 0.12f ? "Few" : Coverage < 0.35f ? "Scattered" : Coverage < 0.65f ? "Broken" : "Overcast";
}

// The browser runs this in doubles; so must we, or the sieve lands on different cells.
inline double Hash(double X, double Y)
{
    const double N = std::sin(X * 127.1 + Y * 311.7) * 43758.5453;
    return N - std::floor(N);
}

inline double ValueNoise(double X, double Y)
{
    const double IX = std::floor(X), IY = std::floor(Y);
    const double FX = X - IX, FY = Y - IY;
    const double U = FX * FX * (3 - 2 * FX), V = FY * FY * (3 - 2 * FY);
    const double A = Hash(IX, IY), B = Hash(IX + 1, IY), C = Hash(IX, IY + 1), D = Hash(IX + 1, IY + 1);
    return A + (B - A) * U + (C - A) * V + (A - B - C + D) * U * V;
}

inline float Noise(double X, double Y, float Scale, float Detail)
{
    const double F = std::max(0.2f, Scale);
    double N = ValueNoise(X * 0.028 * F, Y * 0.028 * F) * 0.55
             + ValueNoise(X * 0.067 * F + 9, Y * 0.067 * F - 4) * 0.3;
    N += ValueNoise(X * 0.16 * F - 3, Y * 0.16 * F + 7) * (0.07 + Detail * 0.08);
    return float(std::clamp(N / (0.92 + Detail * 0.08), 0.0, 1.0));
}

inline void Unpack(unsigned Hex, float& R, float& G, float& B)
{
    R = float((Hex >> 16) & 255);
    G = float((Hex >> 8) & 255);
    B = float(Hex & 255);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE SATELLITE MAP
//------------------------------------------------------------------------------------------------------------------------

constexpr float MapTall   = 170.0f;   // [px] the hero canvas, 142 when the panel is compact
constexpr float MapShort  = 142.0f;

inline void PaintMap(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Wide, float Tall,
                     const CloudDraft& Layer, float WindFrom, ImU32 Backdrop = IM_COL32(5, 5, 5, 255))
{
    Draw->PushClipRect(Spot, { Spot.x + Wide, Spot.y + Tall }, true);
    float TR, TG, TB, SR, SG, SB;
    Unpack(Layer.Tint, TR, TG, TB);
    Unpack(Layer.Shade, SR, SG, SB);
    const float Level = Threshold(Layer);
    const float Diagonal = Wide * Wide + Tall * Tall;

    constexpr float Step = 3.0f;
    for (float Y = 0; Y < Tall; Y += Step) for (float X = 0; X < Wide; X += Step)
    {
        // The sea is a corner-to-corner ramp, so each cell takes its own place along the diagonal.
        const float Along = std::clamp((X * Wide + Y * Tall) / Diagonal, 0.0f, 1.0f);
        Draw->AddRectFilled({ Spot.x + X, Spot.y + Y }, { Spot.x + X + Step + 0.3f, Spot.y + Y + Step + 0.3f },
                            IM_COL32(int(17 + (5 - 17) * Along + 0.5f), int(24 + (7 - 24) * Along + 0.5f),
                                     int(32 + (8 - 32) * Along + 0.5f), 255));
        const float N = Noise(X, Y, Layer.Scale, Layer.Detail);
        if (N <= Level) continue;
        const float Body = std::clamp((N - Level) / std::max(1e-6f, 1.0f - Level), 0.0f, 1.0f);
        const float Lift = Body * 0.55f + 0.2f;
        const int Alpha = int(std::clamp((0.16f + Body * 0.72f) * Layer.Density, 0.0f, 1.0f) * 255.0f + 0.5f);
        Draw->AddRectFilled({ Spot.x + X, Spot.y + Y }, { Spot.x + X + Step + 0.3f, Spot.y + Y + Step + 0.3f },
                            IM_COL32(int(SR + (TR - SR) * Lift + 0.5f), int(SG + (TG - SG) * Lift + 0.5f),
                                     int(SB + (TB - SB) * Lift + 0.5f), Alpha));
    }

    for (float X = 0; X < Wide; X += Wide / 8.0f)
        Draw->AddLine({ Spot.x + X, Spot.y }, { Spot.x + X, Spot.y + Tall }, IM_COL32(255, 255, 255, 26), 1.0f);
    for (float Y = 0; Y < Tall; Y += Tall / 5.0f)
        Draw->AddLine({ Spot.x, Spot.y + Y }, { Spot.x + Wide, Spot.y + Y }, IM_COL32(255, 255, 255, 26), 1.0f);

    Kit::Inked(Draw, Face, Spot.x + 8.0f, Spot.y + 13.0f, 8.0f, IM_COL32(255, 255, 255, 87), "N");
    char Swath[32];
    std::snprintf(Swath, sizeof(Swath), "%d km SWATH", int(std::lround(12.0f / std::max(0.01f, Layer.Scale))));
    Kit::Inked(Draw, Face, Spot.x + Wide - 8.0f, Spot.y + 13.0f, 8.0f, IM_COL32(255, 255, 255, 87), Swath,
               Kit::Anchor::End);

    const float Angle = (WindFrom + 90.0f) * Kit::Pi / 180.0f;
    const float AX = Spot.x + Wide - 30.0f, AY = Spot.y + Tall - 22.0f;
    Draw->AddLine({ AX, AY }, { AX + std::cos(Angle) * 18.0f, AY + std::sin(Angle) * 18.0f },
                  IM_COL32(255, 255, 255, 179), 1.0f);
    Draw->AddCircleFilled({ AX, AY }, 2.0f, IM_COL32(255, 255, 255, 255), 16);

    // The caption. .cl-sub drops the uppercase the wind panel's subtitle carries.
    const float CapTall = 8.0f + 12.5f + 2.0f + 10.0f + 9.0f;
    const float CapTop  = Spot.y + Tall - CapTall;
    for (int I = 0; I < 16; ++I)
    {
        const float A = float(I) / 16.0f, B = float(I + 1) / 16.0f;
        Draw->AddRectFilled({ Spot.x, CapTop + CapTall * A }, { Spot.x + Wide, CapTop + CapTall * B + 1.0f },
                            IM_COL32(5, 7, 15, int(224.0f * (A + B) * 0.5f + 0.5f)));
    }
    Kit::Boxed(Draw, Face, Spot.x + 12.0f, CapTop + 8.0f, 12.5f, Kit::TextFull, CloudName(Layer.Coverage));
    char Sub[96];
    std::snprintf(Sub, sizeof(Sub), "%d / 8 oktas \xc2\xb7 optical depth %.1f",
                  int(std::lround(Layer.Coverage * 8.0f)), double(Layer.Density * 12.0f));
    Kit::Tracked(Draw, Face, Spot.x + 12.0f, CapTop + 8.0f + 12.5f + 2.0f, 10.0f, Kit::TextFaint, Sub, 0.3f);
    char Right[32];
    std::snprintf(Right, sizeof(Right), "base %.0f m", double(Layer.Altitude));
    Kit::Inked(Draw, Face, Spot.x + Wide - 12.0f, CapTop + CapTall - 9.0f, 10.5f, Kit::TextDim, Right,
               Kit::Anchor::End);

    Draw->PopClipRect();
    const float R = Kit::CardRound, X1 = Spot.x + Wide, Y1 = Spot.y + Tall;
    Kit::CornerNotch(Draw, Spot,            { Spot.x + R, Spot.y + R }, R,  Kit::Pi,          Backdrop);
    Kit::CornerNotch(Draw, { X1, Spot.y },  { X1 - R, Spot.y + R },     R, -Kit::Pi * 0.5f,   Backdrop);
    Kit::CornerNotch(Draw, { X1, Y1 },      { X1 - R, Y1 - R },         R,  0.0f,             Backdrop);
    Kit::CornerNotch(Draw, { Spot.x, Y1 },  { Spot.x + R, Y1 - R },     R,  Kit::Pi * 0.5f,   Backdrop);
    Draw->AddRect(Spot, { X1, Y1 }, IM_COL32(46, 46, 46, 255), R, 0, 1.0f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 THE RAIL AND THE DUO
//------------------------------------------------------------------------------------------------------------------------

inline void PaintRail(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Wide, const CloudDraft& Layer)
{
    const float Gap = 5.0f, Share = (Wide - Gap * 3.0f) / 4.25f;
    char Value[4][24], Unit[4][12];
    std::snprintf(Value[0], 24, "%.0f", double(Layer.Coverage * 100.0f)); std::snprintf(Unit[0], 12, "%%");
    std::snprintf(Value[1], 24, "%.1f", double(Layer.Density * 12.0f));   std::snprintf(Unit[1], 12, "%s", "");
    std::snprintf(Value[2], 24, "%.0f", double(Layer.Altitude));          std::snprintf(Unit[2], 12, "m");
    std::snprintf(Value[3], 24, "%.1f", double(Layer.Speed));             std::snprintf(Unit[3], 12, "\xc3\x97");
    const char* Keys[4] = { "Coverage", "Optical", "Base", "Drift" };

    float X = Spot.x;
    for (int I = 0; I < 4; ++I)
    {
        const float Run = I == 3 ? Share * 1.25f : Share;
        Draw->AddRectFilled({ X, Spot.y }, { X + Run, Spot.y + Kit::PillTall }, Kit::PanelFill, 999.0f);
        Draw->AddRect({ X, Spot.y }, { X + Run, Spot.y + Kit::PillTall }, Kit::Stroke, 999.0f, 0, 1.0f);
        const float Lead = Kit::Tracked(Draw, Face, X + 9.0f, Spot.y + 7.0f, 14.0f, Kit::TextFull, Value[I], -0.3f);
        if (Unit[I][0])
            Kit::Tracked(Draw, Face, X + 9.0f + Lead + 2.0f, Spot.y + 7.0f + 14.0f - 9.5f, 9.5f,
                         Kit::TextDim, Unit[I], 0.4f);
        char Key[16];
        Kit::Upper(Key, sizeof(Key), Keys[I]);
        Kit::Tracked(Draw, Face, X + 9.0f, Spot.y + 7.0f + 14.0f + 1.0f, 9.0f, Kit::TextFaint, Key, 1.1f);
        X += Run + Gap;
    }
}

// icons.js P.sun and P.cloud on their 24 unit grid.
inline void PaintSunGlyph(ImDrawList* Draw, ImVec2 Spot, float Size, ImU32 Colour)
{
    const float U = Size / 24.0f, Thick = 1.75f * U;
    auto At = [&](float X, float Y) { return ImVec2{ Spot.x + X * U, Spot.y + Y * U }; };
    Draw->AddCircle(At(12, 12), 5.0f * U, Colour, 24, Thick);
    const float Spokes[8][4] = { { 12, 1, 12, 4 }, { 12, 20, 12, 23 }, { 1, 12, 4, 12 }, { 20, 12, 23, 12 },
                                 { 4.2f, 4.2f, 6.3f, 6.3f }, { 17.7f, 17.7f, 19.8f, 19.8f },
                                 { 19.8f, 4.2f, 17.7f, 6.3f }, { 6.3f, 17.7f, 4.2f, 19.8f } };
    for (const auto& Leg : Spokes) Draw->AddLine(At(Leg[0], Leg[1]), At(Leg[2], Leg[3]), Colour, Thick);
}

inline void PaintCloudGlyph(ImDrawList* Draw, ImVec2 Spot, float Size, ImU32 Colour)
{
    const float U = Size / 24.0f, Thick = 1.75f * U;
    auto At = [&](float X, float Y) { return ImVec2{ Spot.x + X * U, Spot.y + Y * U }; };
    // Three lobes closed along the 18 unit baseline, as the single path arc does.
    Draw->PathArcTo(At(6.5f, 13.5f), 4.5f * U, Kit::Pi * 0.5f, Kit::Pi * 1.62f, 16);
    Draw->PathArcTo(At(11.9f, 9.3f), 6.0f * U, Kit::Pi * 1.18f, Kit::Pi * 1.95f, 18);
    Draw->PathArcTo(At(17.5f, 14.1f), 3.9f * U, Kit::Pi * 1.42f, Kit::Pi * 0.5f, 16);
    Draw->PathStroke(Colour, ImDrawFlags_Closed, Thick);
}

// The two readings the depot panel puts under the rail. "Sun reaching datum" is long enough to wrap in its
//    own grid column at a 340 px panel width, which is what makes the duo taller than one .mp-stat row.
inline void DuoBody(const CloudDraft& Layer, int Which, char* Body, size_t Size, char* Unit, size_t UnitSize)
{
    if (Which == 0)
    {
        std::snprintf(Body, Size, "%.0f", double(std::exp(-Layer.Density * Layer.Coverage * 2.2f) * 100.0f));
        std::snprintf(Unit, UnitSize, "%%");
    }
    else
    {
        std::snprintf(Body, Size, "%d", int(std::lround(Layer.Coverage * 8.0f)));
        std::snprintf(Unit, UnitSize, "/8");
    }
}

inline const char* DuoLabel(int Which) { return Which == 0 ? "Sun reaching datum" : "Sky cover"; }

inline float DuoHeight(ImFont* Face, float Wide, const CloudDraft& Layer)
{
    const float Half = (Wide - 10.0f) * 0.5f;
    float Tall = 0.0f;
    for (int I = 0; I < 2; ++I)
    {
        char Body[16], Unit[8];
        DuoBody(Layer, I, Body, sizeof(Body), Unit, sizeof(Unit));
        Tall = std::max(Tall, Kit::StatHeight(Face, Half, DuoLabel(I), Body, Unit));
    }
    return Tall;
}

inline void PaintDuo(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Wide, const CloudDraft& Layer)
{
    const float Gap = 10.0f, Half = (Wide - Gap) * 0.5f;
    for (int I = 0; I < 2; ++I)
    {
        char Body[16], Unit[8];
        DuoBody(Layer, I, Body, sizeof(Body), Unit, sizeof(Unit));
        Kit::PaintStat(Draw, Face, { Spot.x + I * (Half + Gap), Spot.y }, Half, DuoLabel(I), Body, Unit,
                       false, I == 0 ? PaintSunGlyph : PaintCloudGlyph, DuoHeight(Face, Wide, Layer));
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                              THE COVERAGE DISTRIBUTION
//------------------------------------------------------------------------------------------------------------------------
// Sixteen hundred probes of the same field the map draws, binned twenty ways. The bins past the condensate
//    threshold are the sky that actually has cloud in it.

constexpr float HistTall = 110.0f;

inline void Population(const CloudDraft& Layer, int Bins[20])
{
    for (int I = 0; I < 20; ++I) Bins[I] = 0;
    for (int I = 0; I < 1600; ++I)
    {
        const float N = Noise((I % 40) * 7, (I / 40) * 5, Layer.Scale, Layer.Detail);
        ++Bins[std::min(19, int(N * 20.0f))];
    }
}

inline void PaintHistogram(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Wide, float Tall,
                           const CloudDraft& Layer)
{
    Draw->PushClipRect(Spot, { Spot.x + Wide, Spot.y + Tall }, true);
    int Bins[20];
    Population(Layer, Bins);
    int Peak = 1;
    for (int Count : Bins) Peak = std::max(Peak, Count);
    constexpr float L = 8.0f, R = 8.0f, T = 8.0f, B = 17.0f;
    const float Level = Threshold(Layer);
    for (int I = 0; I < 20; ++I)
    {
        const float X = Spot.x + L + float(I) / 20.0f * (Wide - L - R);
        const float Deep = float(Bins[I]) / Peak * (Tall - T - B);
        const ImU32 Ink = float(I) / 20.0f > Level ? IM_COL32(238, 243, 248, 184)
                                                   : IM_COL32(255, 255, 255, 28);
        Draw->AddRectFilled({ X, Spot.y + Tall - B - Deep },
                            { X + (Wide - L - R) / 20.0f - 2.0f, Spot.y + Tall - B }, Ink);
    }
    const float Cut = Spot.x + L + Level * (Wide - L - R);
    Kit::Dashed(Draw, { Cut, Spot.y + T }, { Cut, Spot.y + Tall - B }, IM_COL32(255, 255, 255, 255), 3.0f, 3.0f, 1.0f);
    Kit::Inked(Draw, Face, Spot.x + L, Spot.y + Tall - 3.0f, 8.0f, IM_COL32(255, 255, 255, 77), "CLEAR AIR");
    Kit::Inked(Draw, Face, Spot.x + Wide - R, Spot.y + Tall - 3.0f, 8.0f, IM_COL32(255, 255, 255, 77),
               "CONDENSED", Kit::Anchor::End);
    Draw->PopClipRect();
}

inline float CoverageHeight()
{
    return Kit::CardPadTop + Kit::HeadHeight()
         + Kit::NumLift + Kit::NumSize * 0.94f + Kit::NumDrop + Kit::KeySize
         + Kit::ChartLift + HistTall + Kit::MetricFoot;
}

inline void PaintCoverage(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Wide, const CloudDraft& Layer)
{
    const float Inner = Wide - Kit::CardPadX * 2.0f, Left = Spot.x + Kit::CardPadX;
    Draw->AddRectFilled(Spot, { Spot.x + Wide, Spot.y + CoverageHeight() }, Kit::CardFill, Kit::CardRound);
    float Y = Spot.y + Kit::CardPadTop;
    Kit::PaintHead(Draw, Face, { Left, Y }, "Coverage", "Condensate threshold \xc2\xb7 cell population");
    Y += Kit::HeadHeight() + Kit::NumLift;

    const float Percent = Layer.Coverage * 100.0f;
    char Whole[16], Fraction[8];
    std::snprintf(Whole, sizeof(Whole), "%d", int(std::floor(Percent)));
    std::snprintf(Fraction, sizeof(Fraction), ".%d", int(std::lround(Percent * 10.0f)) % 10);
    const float WholeRun = Kit::Tracked(Draw, Face, Left, Y, Kit::NumSize, Kit::TextFull, Whole, -2.4f);
    const float FracRun  = Kit::Tracked(Draw, Face, Left + WholeRun, Y, Kit::NumSize, Kit::TextFaint, Fraction, -2.4f);
    Kit::Boxed(Draw, Face, Left + WholeRun + FracRun + 7.0f,
               Y + Kit::NumSize * 0.94f - Kit::NumUnit - 2.0f, Kit::NumUnit, Kit::TextDim, "%");
    Y += Kit::NumSize * 0.94f + Kit::NumDrop;

    char Key[20];
    Kit::Upper(Key, sizeof(Key), "Sky fraction");
    const float KeyRun = Kit::Tracked(Draw, Face, Left, Y, Kit::KeySize, Kit::TextFaint, Key, 1.3f);
    char Named[32];
    std::snprintf(Named, sizeof(Named), "%s cloud", CloudName(Layer.Coverage));
    for (char* Scan = Named; *Scan; ++Scan) *Scan = static_cast<char>(std::tolower(static_cast<unsigned char>(*Scan)));
    Kit::Boxed(Draw, Face, Left + KeyRun + 6.0f, Y + Kit::KeySize - Kit::KeyValue, Kit::KeyValue, Kit::TextDim, Named);
    Y += Kit::KeySize + Kit::ChartLift;

    PaintHistogram(Draw, Face, { Left - Kit::ChartBleed, Y }, Inner + Kit::ChartBleed * 2.0f, HistTall, Layer);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 THE VERTICAL SECTION
//------------------------------------------------------------------------------------------------------------------------

constexpr float SectionTall = 104.0f;

inline void PaintSection(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Wide, float Tall,
                         const CloudDraft& Layer)
{
    Draw->AddRectFilled(Spot, { Spot.x + Wide, Spot.y + Tall }, IM_COL32(6, 7, 8, 255));
    // A canvas clips whatever runs past its box. A base well above the 400 m window would otherwise
    //    paint its columns over the card above this one.
    Draw->PushClipRect(Spot, { Spot.x + Wide, Spot.y + Tall }, true);
    float TR, TG, TB, SR, SG, SB;
    Unpack(Layer.Tint, TR, TG, TB);
    Unpack(Layer.Shade, SR, SG, SB);
    auto PY = [&](float Metres) { return Spot.y + Tall - 12.0f - Metres / 400.0f * (Tall - 20.0f); };
    const float Base = PY(Layer.Altitude), Thick = 10.0f + 34.0f * Layer.Density;

    for (float X = 0; X < Wide; X += 4.0f)
    {
        const float Top = Base - Thick * (0.65f + 0.3f * std::sin(X * 0.08f * Layer.Scale)
                                               + 0.12f * std::sin(X * 0.31f));
        const float Deep = Base - Top + 4.0f;
        if (Deep <= 0.0f) continue;
        // The column is a three-stop ramp: a wisp at the crown, body through the middle, shadow at the foot.
        constexpr int Bands = 12;
        for (int I = 0; I < Bands; ++I)
        {
            const float A = float(I) / Bands, B = float(I + 1) / Bands;
            const float Mid = (A + B) * 0.5f;
            float Red, Green, Blue, Alpha;
            if (Mid < 0.35f)
            {
                const float Share = Mid / 0.35f;
                Red = TR; Green = TG; Blue = TB;
                Alpha = 0.05f + (0.75f * Layer.Density - 0.05f) * Share;
            }
            else
            {
                const float Share = (Mid - 0.35f) / 0.65f;
                Red = TR + (SR - TR) * Share; Green = TG + (SG - TG) * Share; Blue = TB + (SB - TB) * Share;
                Alpha = 0.75f * Layer.Density + (0.72f * Layer.Density - 0.75f * Layer.Density) * Share;
            }
            Draw->AddRectFilled({ Spot.x + X, Top + Deep * A }, { Spot.x + X + 4.3f, Top + Deep * B + 0.5f },
                                IM_COL32(int(Red + 0.5f), int(Green + 0.5f), int(Blue + 0.5f),
                                         int(std::clamp(Alpha, 0.0f, 1.0f) * 255.0f + 0.5f)));
        }
    }

    for (int Metres : { 0, 100, 200, 300, 400 })
    {
        const float Y = PY(float(Metres));
        Draw->AddLine({ Spot.x, Y }, { Spot.x + Wide, Y }, IM_COL32(255, 255, 255, 18), 1.0f);
        char Label[12];
        if (Metres == 0) std::snprintf(Label, sizeof(Label), "DATUM");
        else             std::snprintf(Label, sizeof(Label), "%d m", Metres);
        Kit::Inked(Draw, Face, Spot.x + 5.0f, Y - 2.0f, 8.0f, IM_COL32(255, 255, 255, 69), Label);
    }
    Kit::Dashed(Draw, { Spot.x, Base }, { Spot.x + Wide, Base }, IM_COL32(255, 255, 255, 166), 3.0f, 3.0f, 1.0f);
    Draw->PopClipRect();
}

inline float DeckHeight()
{
    return Kit::CardPadTop + Kit::HeadHeight()
         + 2.0f + (8.0f + 8.5f + 2.0f + SectionTall + 2.0f) + 10.0f
         + Kit::TapeHeight() * 2.0f + 2.0f;
}

inline void PaintDeck(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Wide, const CloudDraft& Layer)
{
    const float Inner = Wide - Kit::CardPadX * 2.0f, Left = Spot.x + Kit::CardPadX;
    Draw->AddRectFilled(Spot, { Spot.x + Wide, Spot.y + DeckHeight() }, Kit::CardFill, Kit::CardRound);
    float Y = Spot.y + Kit::CardPadTop;
    Kit::PaintHead(Draw, Face, { Left, Y }, "Cloud deck", "Base altitude \xc2\xb7 optical body");
    Y += Kit::HeadHeight() + 2.0f;

    const float MeterTall = 8.0f + 8.5f + 2.0f + SectionTall + 2.0f;
    Draw->AddRectFilled({ Left, Y }, { Left + Inner, Y + MeterTall }, Kit::FieldFill, Kit::CardRound);
    Draw->AddRect({ Left, Y }, { Left + Inner, Y + MeterTall }, Kit::Stroke, Kit::CardRound, 0, 1.0f);
    char Key[40];
    Kit::Upper(Key, sizeof(Key), "vertical section \xc2\xb7 0\xe2\x80\x93""400 m");
    Kit::Tracked(Draw, Face, Left + 9.0f, Y + 8.0f, 8.5f, Kit::TextFaint, Key, 1.1f);
    PaintSection(Draw, Face, { Left + 1.0f, Y + 8.0f + 8.5f + 2.0f }, Inner - 2.0f, SectionTall, Layer);
    Y += MeterTall + 10.0f;

    static const Kit::Mark BaseMarks[3] = { { 0.0f, "20 m" }, { 0.289f, "LOW 130" }, { 1.0f, "400 m" } };
    static const Kit::Mark BodyMarks[3] = { { 0.0f, "VEIL" }, { 0.62f, "BODY .62" }, { 1.0f, "OPAQUE" } };
    Kit::PaintTape(Draw, Face, { Left, Y }, Inner, "Cloud base", Layer.Altitude, 20, 400, 0, "m", BaseMarks, 3);
    Y += Kit::TapeHeight();
    Kit::PaintTape(Draw, Face, { Left, Y }, Inner, "Optical density", Layer.Density, 0, 1, 2, "", BodyMarks, 3);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE MORPHOLOGY
//------------------------------------------------------------------------------------------------------------------------

inline float MorphologyHeight()
{
    return Kit::CardPadTop + Kit::HeadHeight() + Kit::TapeHeight() * 3.0f
         + 10.0f + 9.0f + 4.0f + 30.0f
         + 8.0f + Kit::TagTall + 2.0f
         + 8.0f + 10.5f * 2.0f + 14.0f;
}

inline void PaintMorphology(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Wide, const CloudDraft& Layer)
{
    const float Inner = Wide - Kit::CardPadX * 2.0f, Left = Spot.x + Kit::CardPadX;
    Draw->AddRectFilled(Spot, { Spot.x + Wide, Spot.y + MorphologyHeight() }, Kit::CardFill, Kit::CardRound);
    float Y = Spot.y + Kit::CardPadTop;
    Kit::PaintHead(Draw, Face, { Left, Y }, "Morphology", "Cell scale \xc2\xb7 edge detail \xc2\xb7 advection");
    Y += Kit::HeadHeight();

    static const Kit::Mark SizeMarks[3]   = { { 0.0f, "SHEETS" }, { 0.21f, "1\xc3\x97" }, { 1.0f, "CELLS" } };
    static const Kit::Mark DetailMarks[3] = { { 0.0f, "SOFT" }, { 0.55f, "NATURAL" }, { 1.0f, "FRACTAL" } };
    static const Kit::Mark DriftMarks[3]  = { { 0.0f, "STILL" }, { 0.167f, "REAL 1\xc3\x97" }, { 1.0f, "6\xc3\x97" } };
    Kit::PaintTape(Draw, Face, { Left, Y }, Inner, "Feature size", Layer.Scale, 0.2f, 4.0f, 2, "\xc3\x97",
                   SizeMarks, 3);
    Y += Kit::TapeHeight();
    Kit::PaintTape(Draw, Face, { Left, Y }, Inner, "Edge detail", Layer.Detail, 0, 1, 2, "", DetailMarks, 3);
    Y += Kit::TapeHeight();
    Kit::PaintTape(Draw, Face, { Left, Y }, Inner, "Drift speed", Layer.Speed, 0, 6, 2, "\xc3\x97", DriftMarks, 3);
    Y += Kit::TapeHeight() + 10.0f;

    // .cl-colours is a two column grid; each column is a subhead with a 30 px chip pushed to its right.
    const float Column = (Inner - 10.0f) * 0.5f;
    const char* Captions[2] = { "sunlit", "shadowed" };
    const unsigned Swatch[2] = { Layer.Tint, Layer.Shade };
    for (int I = 0; I < 2; ++I)
    {
        const float X = Left + I * (Column + 10.0f);
        char Caption[16];
        Kit::Upper(Caption, sizeof(Caption), Captions[I]);
        Kit::Tracked(Draw, Face, X, Y, 9.0f, Kit::TextFaint, Caption, 1.3f);
        const float ChipY = Y + 9.0f + 4.0f;
        float R, G, B;
        Unpack(Swatch[I], R, G, B);
        Draw->AddRectFilled({ X, ChipY }, { X + 30.0f, ChipY + 30.0f },
                            IM_COL32(int(R), int(G), int(B), 255), 10.0f);
        Draw->AddRect({ X, ChipY }, { X + 30.0f, ChipY + 30.0f }, Kit::Stroke, 10.0f, 0, 1.0f);
        char Hex[12];
        std::snprintf(Hex, sizeof(Hex), "#%06X", Swatch[I]);
        Kit::Tracked(Draw, Face, X + 38.0f, ChipY + 15.0f - 5.75f, 11.5f, Kit::TextDim, Hex, 0.4f);
    }
    Y += 9.0f + 4.0f + 30.0f + 8.0f;

    char Tag[20];
    Kit::Upper(Tag, sizeof(Tag), "FOLLOW WIND");
    const float TagRun = Kit::Tracked(Draw, Face, 0, 0, 8.5f, 0, Tag, 1.2f, false) + 18.0f;
    if (Layer.Linked) Draw->AddRectFilled({ Left, Y }, { Left + TagRun, Y + Kit::TagTall }, Kit::CardFill, 999.0f);
    Draw->AddRect({ Left, Y }, { Left + TagRun, Y + Kit::TagTall },
                  Layer.Linked ? IM_COL32(46, 46, 46, 255) : Kit::Stroke, 999.0f, 0, 1.0f);
    Kit::Tracked(Draw, Face, Left + 9.0f, Y + 4.0f, 8.5f, Layer.Linked ? Kit::TextFull : Kit::TextFaint, Tag, 1.2f);
    Y += Kit::TagTall + 2.0f + 8.0f;

    char First[96], Second[96];
    std::snprintf(First, sizeof(First), "%s Feature scale is %.2f\xc3\x97",
                  Layer.Linked ? "Advection follows the Wind Field." : "Layer has independent drift.",
                  double(Layer.Scale));
    std::snprintf(Second, sizeof(Second), "with %d%% edge detail.", int(std::lround(Layer.Detail * 100.0f)));
    Kit::Tracked(Draw, Face, Left + 2.0f, Y, 10.5f, Kit::TextFaint, First, 0.2f);
    Kit::Tracked(Draw, Face, Left + 2.0f, Y + 10.5f, 10.5f, Kit::TextFaint, Second, 0.2f);
}

} // namespace Frontier::CloudInstrument
