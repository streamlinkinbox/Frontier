//==============================================================================================================================================
//                                                           LIGHTINSPECTORPANEL.CPP
//==============================================================================================================================================
// 📦 Native light inspector ported from ProjectZeroEditor/LightPanel.js, LightPanel.css and LightProjection.js — the panel the bundle ships.

#include "LightInspectorPanel.h"
#include "LightPanelSurface.h"
#include "EditorInstance.h"
#include "../DisplayPresentation/IconArt.h"
#include "ControlPanel.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace Frontier
{
using namespace Lighting;

// The host hands over the engine's own rasterized glyph; LightSpecification.js reuses the shipped
//    icon library rather than inventing a second light glyph set, and so does this panel.
ImTextureID (*LightGlyphSource)(IconSymbol, float) = nullptr;

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        LIGHT KINDS
//------------------------------------------------------------------------------------------------------------------------
// LightPanel.js branches on Subject.type. Seven styles, and the engine's Type selector folds onto them.

enum class LightKind : unsigned { Point = 0, Spot, Ies, Area, Tube, Led, Strip };

struct KindFacts
{
    const char* Preview;      // [-] Preview card title
    const char* Shape;        // [-] Shape card title
    const char* Response;     // [-] Response block title
    const char* Output;       // [-] Output card title
    const char* Caption;      // [-] .lp-primary-caption
    const char* Unit;         // [-] .lp-primary small
    IconSymbol  Artwork;      // [-] LightIcons[Style]
};

const KindFacts& Facts(LightKind Kind)
{
    static const KindFacts Table[] = {
        {"Radial falloff",          "Attenuation",          "Distance response", "Source & colour",  "Authored luminous intensity", "cd", IconSymbol::EditorPointLight},
        {"Beam envelope",           "Beam shaping",         "Beam section",      "Source & colour",  "Authored luminous intensity", "cd", IconSymbol::EditorSpotlight},
        {"Photometric distribution","Distribution profile", "Angular response",  "Source & colour",  "Profile source flux",         "lm", IconSymbol::EditorDomeLight},
        {"Luminous surface",        "Emitter dimensions",   "Aperture balance",  "Source & colour",  "Authored luminous flux",      "lm", IconSymbol::EditorAreaLight},
        {"Linear radiance",         "Emitter dimensions",   "Linear output",     "Source & colour",  "Authored luminous flux",      "lm", IconSymbol::LightArea2D},
        {"LED emitter",             "Package & optic",      "Conversion budget", "Driver & colour",  "Estimated luminous flux",     "lm", IconSymbol::LightPoint2D},
        {"Ribbon light",            "Layout & segments",    "Electrical budget", "Output per metre", "Estimated luminous flux",     "lm", IconSymbol::SlateRingLight},
    };
    return Table[static_cast<unsigned>(Kind)];
}

const char* const ProfileNames[8] = { "Downlight", "Wall wash", "Batwing", "ECE Low Beam",
                                      "SAE Low Beam", "High Beam", "Fog Lamp", "Parking Lamp" };

//------------------------------------------------------------------------------------------------------------------------
//                                                      PROPERTY BAG
//------------------------------------------------------------------------------------------------------------------------
// Subject.props with LightPanel.js's Read(Key, Fallback): the sheet when it carries the name, else the
//    reference default. Names match world.js and LightSpecification.js exactly.

struct Emitter
{
    LightKind Kind = LightKind::Point;
    float Rgb[3]   = { 1.0f, 1.0f, 1.0f };   // Colour(): hex tint folded with the kelvin curve

    float Intensity = 14.0f, Lumens = 1600.0f;
    float Distance  = 26.0f, Decay  = 2.0f;
    float Angle     = 26.0f, Penumbra = 0.42f;
    float Cone = 58.0f, Cutoff = -1.0f, Multiplier = 1.0f, Range = 120.0f;
    float Across = 2.0f, Tall = 1.0f, Spread = 120.0f;
    float Length = 1.5f, Radius = 0.04f;
    float Watts = 10.0f, Efficacy = 110.0f, Dimmer = 1.0f, Diameter = 40.0f;
    float LumensPerMetre = 1000.0f, WattsPerMetre = 14.4f, LedsPerMetre = 60.0f, Voltage = 24.0f;
    float Temperature = 4300.0f;

    bool Disk = false, TwoSided = false, Diffuser = false;
    bool Shadows = true, Shown = true;
    unsigned Profile = 3u;

    float Position[3] = { 0.0f, 2.0f, 0.0f };
    float Rotation[3] = { 0.0f, 0.0f, 0.0f };
    float Scale[3]    = { 1.0f, 1.0f, 1.0f };
};

bool Named(const char* Label, const char* Wanted)
{
    return std::strcmp(Label, Wanted) == 0;
}

// Walk the sheet once and overlay whatever the host published onto the reference defaults.
void Adopt(const EditorSheet& Sheet, Emitter& Light)
{
    for (uint32_t G = 0; G < Sheet.GroupCount; ++G)
    {
        const EditorPropertyGroup& Group = Sheet.Groups[G];
        for (uint32_t P = 0; P < Group.PropertyCount; ++P)
        {
            const EditorProperty& Slot = Group.Properties[P];
            const float F = Slot.Figure;
            if      (Named(Slot.Label, "Intensity"))          Light.Intensity = F;
            else if (Named(Slot.Label, "Luminous flux"))      Light.Lumens = F;
            else if (Named(Slot.Label, "Reach"))              Light.Distance = F;
            else if (Named(Slot.Label, "Decay exponent"))     Light.Decay = F;
            else if (Named(Slot.Label, "Full cone angle"))    Light.Angle = F;
            else if (Named(Slot.Label, "Penumbra"))           Light.Penumbra = F;
            else if (Named(Slot.Label, "Field angle"))        Light.Cone = F;
            else if (Named(Slot.Label, "Cut-off pitch"))      Light.Cutoff = F;
            else if (Named(Slot.Label, "Profile multiplier")) Light.Multiplier = F;
            else if (Named(Slot.Label, "Photometric range"))  Light.Range = F;
            else if (Named(Slot.Label, "Width"))              Light.Across = F;
            else if (Named(Slot.Label, "Height"))             Light.Tall = F;
            else if (Named(Slot.Label, "Beam spread"))        Light.Spread = F;
            else if (Named(Slot.Label, "Length"))             Light.Length = F;
            else if (Named(Slot.Label, "Strip length"))       Light.Length = F;
            else if (Named(Slot.Label, "Tube radius"))        Light.Radius = F;
            else if (Named(Slot.Label, "Driver power"))       Light.Watts = F;
            else if (Named(Slot.Label, "Efficacy target"))    Light.Efficacy = F;
            else if (Named(Slot.Label, "Dimmer"))             Light.Dimmer = F;
            else if (Named(Slot.Label, "Package diameter"))   Light.Diameter = F;
            else if (Named(Slot.Label, "Emission angle"))     Light.Angle = F;
            else if (Named(Slot.Label, "Flux per metre"))     Light.LumensPerMetre = F;
            else if (Named(Slot.Label, "Load per metre"))     Light.WattsPerMetre = F;
            else if (Named(Slot.Label, "Emitter density"))    Light.LedsPerMetre = F;
            else if (Named(Slot.Label, "Supply voltage"))     Light.Voltage = F;
            else if (Named(Slot.Label, "Colour temperature")) Light.Temperature = F;
            else if (Named(Slot.Label, "Cast shadows"))       Light.Shadows = Slot.On;
            else if (Named(Slot.Label, "Two-sided emission")) Light.TwoSided = Slot.On;
            else if (Named(Slot.Label, "Opal diffuser"))      Light.Diffuser = Slot.On;
            else if (Named(Slot.Label, "Emission tint"))
            {
                Light.Rgb[0] = Slot.ColourTint[0];
                Light.Rgb[1] = Slot.ColourTint[1];
                Light.Rgb[2] = Slot.ColourTint[2];
            }
            else if (Named(Slot.Label, "Position")) { for (int A = 0; A < 3; ++A) Light.Position[A] = Slot.Axes[A]; }
            else if (Named(Slot.Label, "Rotation")) { for (int A = 0; A < 3; ++A) Light.Rotation[A] = Slot.Axes[A]; }
            else if (Named(Slot.Label, "Scale"))    { for (int A = 0; A < 3; ++A) Light.Scale[A]    = Slot.Axes[A]; }
            else if (Named(Slot.Label, "Aperture")) Light.Disk = Slot.Picked == 1u;
            else if (Named(Slot.Label, "Distribution") || Named(Slot.Label, "Profile"))
            {
                Light.Profile = Slot.Picked < 8u ? Slot.Picked : 0u;
            }
            else if (Named(Slot.Label, "Draw cone") || Named(Slot.Label, "Show glow")
                  || Named(Slot.Label, "Draw emitter") || Named(Slot.Label, "Draw distribution"))
            {
                Light.Shown = Slot.On;
            }
        }
    }
}

// Write(Key, Next) needs the published slot, not the snapshot Adopt() took from it.
EditorProperty* Bind(EditorSheet& Sheet, const char* Label)
{
    for (uint32_t G = 0; G < Sheet.GroupCount; ++G)
    {
        for (uint32_t P = 0; P < Sheet.Groups[G].PropertyCount; ++P)
        {
            if (Named(Sheet.Groups[G].Properties[P].Label, Label))
            {
                return &Sheet.Groups[G].Properties[P];
            }
        }
    }
    return nullptr;
}

// LightPanel.js Flux()
float Flux(const Emitter& L)
{
    const float Raw = L.Kind == LightKind::Led   ? L.Watts * L.Efficacy * L.Dimmer
                    : L.Kind == LightKind::Strip ? L.LumensPerMetre * L.Length * L.Dimmer
                    : (L.Kind == LightKind::Point || L.Kind == LightKind::Spot) ? L.Intensity
                    : L.Lumens;
    return ImMax(0.0f, Raw);
}

// LightProjection.js Polar()
float Polar(unsigned Profile, float Angle, float Cone, int Section)
{
    Angle *= 60.0f / ImMax(5.0f, Cone);
    if (std::fabs(Angle) > Pi * 0.5f)
    {
        return 0.0f;
    }
    const float Axis = ImMax(0.0f, std::cos(Angle));
    if (Profile == 2u)   // Batwing
    {
        return std::pow(std::fabs(std::sin(Angle * 2.0f)), 1.4f) * Axis;
    }
    if (Profile == 1u)   // Wall wash
    {
        return std::pow(ImMax(0.0f, std::cos(Angle - 0.55f)), Section ? 3.0f : 5.0f);
    }
    const float Power = Profile == 6u ? 0.65f : Profile == 7u ? 0.4f : (Profile == 5u ? 240.0f : 130.0f) / 60.0f;
    float Lobe = std::pow(Axis, ImMax(0.4f, Power * (Section ? 0.65f : 1.0f)));
    if (Profile == 3u || Profile == 4u)   // ECE / SAE Low Beam
    {
        Lobe *= std::sin(Angle) > 0.0f ? (Profile == 4u ? 0.5f : 0.65f) : 1.0f;
    }
    return Lobe;
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    SOURCE STUDY CANVAS
//------------------------------------------------------------------------------------------------------------------------
// LightProjection.js paints into a fixed 380 x 260 space, scaled to fit and centred.

struct Study
{
    ImDrawList* Draw = nullptr;
    ImFont*     Face = nullptr;
    ImVec2      Base{};
    float       Zoom = 1.0f;
    const float* Rgb = nullptr;

    ImVec2 At(float X, float Y) const { return { Base.x + X * Zoom, Base.y + Y * Zoom }; }
    float  Of(float V) const { return V * Zoom; }
    ImU32  Tint(float Alpha) const { return FromBytes(Rgb, Alpha); }

    void Text(const char* Body, float X, float Y, Anchor Align = Anchor::Left, ImU32 Colour = StudyInk) const
    {
        Inked(Draw, Face, 10.0f * Zoom, At(X, Y), Colour, Body, Align);
    }
    void Line(float X0, float Y0, float X1, float Y1, ImU32 Colour = StudyRule, bool Dash = false, float Thick = 1.0f) const
    {
        if (Dash) DashedLine(Draw, At(X0, Y0), At(X1, Y1), Colour, Of(3.0f), Of(5.0f), Thick);
        else      Draw->AddLine(At(X0, Y0), At(X1, Y1), Colour, Thick);
    }
    void Ring(float X, float Y, float R, ImU32 Colour = StudyRing, float Thick = 1.0f) const
    {
        Draw->AddCircle(At(X, Y), Of(R), Colour, 64, Thick);
    }
    void Disc(float X, float Y, float R, ImU32 Colour) const
    {
        Draw->AddCircleFilled(At(X, Y), Of(R), Colour, 64);
    }
    // LightProjection.js Dimension(): end ticks, a rule, and a centred caption 17 below.
    void Span(float Left, float Right, float Y, const char* Caption) const
    {
        Line(Left,  Y - 4.0f, Left,  Y + 4.0f);
        Line(Right, Y - 4.0f, Right, Y + 4.0f);
        Line(Left,  Y, Right, Y, IM_COL32(255, 255, 255, 85));
        Text(Caption, (Left + Right) * 0.5f, Y + 17.0f, Anchor::Centre);
    }
};

void PaintStudy(const Study& S, const Emitter& L)
{
    char Word[96];
    const float Shine = Flux(L);

    if (L.Kind == LightKind::Area)
    {
        const float Fit = ImMin(230.0f / ImMax(0.1f, L.Across), 140.0f / ImMax(0.1f, L.Tall));
        const float HalfW = L.Across * Fit * 0.5f, HalfH = L.Tall * Fit * 0.5f, Mid = 122.0f;
        S.Text("APERTURE / FRONT ELEVATION", 22, 18);
        S.Line(190, 36, 190, 207, IM_COL32(255, 255, 255, 24), true);
        S.Line(40, Mid, 340, Mid, IM_COL32(255, 255, 255, 24), true);
        const ImU32 Face = S.Tint(0.07f + ImMin(0.12f, Shine / 80000.0f));
        if (L.Disk)
        {
            S.Draw->AddEllipseFilled(S.At(190, Mid), { S.Of(HalfW), S.Of(HalfH) }, Face, 0.0f, 64);
            S.Draw->AddEllipse(S.At(190, Mid), { S.Of(HalfW), S.Of(HalfH) }, S.Tint(0.9f), 0.0f, 64, 1.5f);
        }
        else
        {
            S.Draw->AddRectFilled(S.At(190 - HalfW, Mid - HalfH), S.At(190 + HalfW, Mid + HalfH), Face);
            S.Draw->AddRect(S.At(190 - HalfW, Mid - HalfH), S.At(190 + HalfW, Mid + HalfH), S.Tint(0.9f), 0.0f, 0, 1.5f);
        }
        S.Draw->PushClipRect(S.At(190 - HalfW, Mid - HalfH), S.At(190 + HalfW, Mid + HalfH), true);
        for (float X = 50.0f; X <= 330.0f; X += 12.0f)
        {
            for (float Y = 44.0f; Y <= 206.0f; Y += 12.0f)
            {
                S.Draw->AddRectFilled(S.At(X, Y), S.At(X + 1.5f, Y + 1.5f), S.Tint(0.35f));
            }
        }
        S.Draw->PopClipRect();
        Fixed(Word, sizeof(Word), L.Across, 1); std::strcat(Word, " m");
        S.Span(190 - HalfW, 190 + HalfW, 218, Word);
        const float Edge = 190.0f + HalfW + 15.0f;
        S.Line(Edge - 4, Mid - HalfH, Edge + 4, Mid - HalfH, IM_COL32(255, 255, 255, 102));
        S.Line(Edge, Mid - HalfH, Edge, Mid + HalfH, IM_COL32(255, 255, 255, 68));
        S.Line(Edge - 4, Mid + HalfH, Edge + 4, Mid + HalfH, IM_COL32(255, 255, 255, 102));
        Fixed(Word, sizeof(Word), L.Tall, 1); std::strcat(Word, " m");
        S.Text(Word, Edge + 20.0f, Mid + 3.0f, Anchor::Left);
        S.Text(L.TwoSided ? "BOTH FACES" : "FRONT FACE", 22, 252);
        std::snprintf(Word, sizeof(Word), "%g\u00b0 SPREAD", double(L.Spread));
        S.Text(Word, 358, 252, Anchor::Right);
    }
    else if (L.Kind == LightKind::Ies)
    {
        const float Reach = 96.0f, Mid = 118.0f;
        for (float Part : { 0.25f, 0.5f, 0.75f, 1.0f })
        {
            S.Ring(190, Mid, Reach * Part);
        }
        for (int Index = 0; Index < 8; ++Index)
        {
            const float A = float(Index) * Pi / 4.0f;
            S.Line(190, Mid, 190 + std::sin(A) * Reach, Mid + std::cos(A) * Reach, IM_COL32(255, 255, 255, 20), true);
        }
        for (int Section : { 1, 0 })
        {
            ImVec2 Shape[241];
            for (int Index = 0; Index <= 240; ++Index)
            {
                const float A = (float(Index) / 240.0f) * Pi * 2.0f;
                const float Pitch = L.Cutoff * Pi / 180.0f;
                const float Lobe  = Polar(L.Profile, A, L.Cone, Section);
                Shape[Index] = S.At(190 + std::sin(A + Pitch) * Reach * Lobe, Mid + std::cos(A + Pitch) * Reach * Lobe);
            }
            const ImU32 Fill = Section ? IM_COL32(148, 183, 173, 11)
                                       : S.Tint(ImMin(0.16f, Shine * L.Multiplier / 18000.0f));
            // A photometric lobe is star-shaped about the origin but not convex, so it is filled as a
            //    fan from the centre rather than through AddConvexPolyFilled.
            const ImVec2 Core = S.At(190, Mid);
            for (int Step = 0; Step < 240; ++Step)
            {
                S.Draw->AddTriangleFilled(Core, Shape[Step], Shape[Step + 1], Fill);
            }
            S.Draw->AddPolyline(Shape, 241, Section ? IM_COL32(148, 183, 173, 255) : S.Tint(0.95f),
                                ImDrawFlags_Closed, 1.0f);
        }
        S.Text("C0",  25, 24, Anchor::Left,  S.Tint(0.9f));
        S.Text("C90", 330, 24, Anchor::Right, IM_COL32(148, 183, 173, 255));
        S.Text("90\u00b0", 69, 122);
        S.Text("90\u00b0", 312, 122, Anchor::Right);
        S.Text("0\u00b0", 190, 235, Anchor::Centre);
        S.Text("NORMALIZED PRESET", 190, 254, Anchor::Centre);
    }
    else if (L.Kind == LightKind::Led)
    {
        const float Size = ImClamp(100.0f + L.Diameter * 0.5f, 102.0f, 158.0f);
        const float Left = 190.0f - Size * 0.5f, Top = 126.0f - Size * 0.5f;
        S.Draw->AddRectFilled(S.At(Left - 14, Top - 14), S.At(Left + Size + 14, Top + Size + 14),
                              IM_COL32(48, 52, 48, 255), S.Of(9.0f));
        S.Draw->AddRect(S.At(Left - 14, Top - 14), S.At(Left + Size + 14, Top + Size + 14),
                        IM_COL32(119, 125, 114, 255), S.Of(9.0f), 0, 1.0f);
        for (int Sign : { -1, 1 })
        {
            for (int Index = 0; Index < 5; ++Index)
            {
                const float X = 190.0f + float(Sign) * (Size * 0.5f + 20.0f) - (Sign < 0 ? 8.0f : 0.0f);
                const float Y = Top + 9.0f + (float(Index) * (Size - 18.0f)) / 4.0f;
                S.Draw->AddRectFilled(S.At(X, Y), S.At(X + 8.0f, Y + 3.0f), IM_COL32(133, 121, 88, 255));
            }
        }
        Glow(S.Draw, S.At(190, 126), S.Of(95.0f), S.Rgb, 0.22f * L.Dimmer);
        S.Draw->AddRectFilled(S.At(Left, Top), S.At(Left + Size, Top + Size), S.Tint(0.15f + 0.35f * L.Dimmer));
        for (int Column = 0; Column < 4; ++Column)
        {
            for (int Row = 0; Row < 4; ++Row)
            {
                const float Pitch = Size / 4.0f;
                S.Draw->AddRectFilled(S.At(Left + float(Column) * Pitch + 7.0f, Top + float(Row) * Pitch + 7.0f),
                                      S.At(Left + float(Column) * Pitch + Pitch - 7.0f,
                                           Top + float(Row) * Pitch + Pitch - 7.0f),
                                      S.Tint(0.15f + 0.65f * L.Dimmer));
            }
        }
        const float Half = L.Angle * Pi / 360.0f;
        S.Draw->PathArcTo(S.At(190, 126), S.Of(Size * 0.79f), -Pi * 0.5f - Half, -Pi * 0.5f + Half, 48);
        S.Draw->PathStroke(S.Tint(0.55f), ImDrawFlags_None, 1.0f);
        for (int Sign : { -1, 1 })
        {
            S.Ring(190.0f + float(Sign) * (Size * 0.5f + 7.0f), Top - 7.0f, 2.0f, IM_COL32(153, 159, 146, 255));
        }
        std::snprintf(Word, sizeof(Word), "%g mm PACKAGE", double(L.Diameter));
        S.Span(Left, Left + Size, 225, Word);
        std::snprintf(Word, sizeof(Word), "%g\u00b0 OPTIC", double(L.Angle));
        S.Text(Word, 190, 17, Anchor::Centre);
    }
    else if (L.Kind == LightKind::Strip)
    {
        const int Count = int(L.Length * L.LedsPerMetre + 0.5f);
        const int Samples = ImClamp(Count, 4, 64);
        S.Draw->AddLine(S.At(40, 123), S.At(340, 123), IM_COL32(66, 66, 56, 255), S.Of(18.0f));
        S.Draw->AddLine(S.At(40, 123), S.At(340, 123), IM_COL32(38, 42, 35, 255), S.Of(12.0f));
        if (L.Diffuser)
        {
            SoftEdge(S.Draw, S.At(40, 118), S.At(340, 128), S.Of(5.0f), S.Tint(0.6f), S.Of(14.0f));
            S.Draw->AddLine(S.At(40, 123), S.At(340, 123), S.Tint(0.12f + 0.45f * L.Dimmer), S.Of(10.0f));
        }
        else
        {
            for (int Index = 0; Index < Samples; ++Index)
            {
                const float Part = float(Index) / float(ImMax(1, Samples - 1));
                const float X = 40.0f + Part * 300.0f;
                S.Draw->AddRectFilled(S.At(X - 2.5f, 120.5f), S.At(X + 2.5f, 125.5f),
                                      S.Tint(0.12f + 0.7f * L.Dimmer));
                if (Index % 8 == 0)
                {
                    S.Draw->AddRect(S.At(X - 4.0f, 118.0f), S.At(X + 4.0f, 128.0f), IM_COL32(188, 170, 101, 85));
                }
            }
        }
        S.Text("+", 23, 119, Anchor::Centre, IM_COL32(196, 172, 121, 255));
        S.Text("\u2212", 23, 133, Anchor::Centre);
        char Run[32]; Fixed(Run, sizeof(Run), L.Length, 1);
        std::snprintf(Word, sizeof(Word), "%s m RUN \u00b7 %d EMITTERS", Run, Count);
        S.Span(40, 340, 225, Word);
        std::snprintf(Word, sizeof(Word), "%g V DC", double(L.Voltage));
        S.Text(Word, 20, 18);
        S.Text(L.Diffuser ? "OPAL DIFFUSER" : "EXPOSED PACKAGES", 360, 18, Anchor::Right);
    }
    else if (L.Kind == LightKind::Spot)
    {
        const float Half = ImClamp(std::tan(L.Angle * Pi / 360.0f) * 220.0f, 12.0f, 145.0f);
        const int First = S.Draw->VtxBuffer.Size;
        ImVec2 Wedge[3] = { S.At(190, 49), S.At(190 + Half, 211), S.At(190 - Half, 211) };
        S.Draw->AddConvexPolyFilled(Wedge, 3, IM_COL32_WHITE);
        ShadeDown(S.Draw, First, S.Draw->VtxBuffer.Size, S.At(0, 50).y, S.At(0, 215).y,
                  S.Tint(0.015f), S.Tint(0.06f + ImMin(0.2f, Shine / 1000.0f)));
        S.Draw->AddPolyline(Wedge, 3, S.Tint(0.45f), ImDrawFlags_Closed, 1.0f);
        DashedLine(S.Draw, S.At(190 - Half * (1.0f - L.Penumbra), 211), S.At(190, 49), S.Tint(0.65f), S.Of(3), S.Of(5));
        DashedLine(S.Draw, S.At(190, 49), S.At(190 + Half * (1.0f - L.Penumbra), 211), S.Tint(0.65f), S.Of(3), S.Of(5));
        for (int Index = 1; Index <= 3; ++Index)
        {
            const float Part = float(Index) / 3.0f;
            S.Line(190 - Half * Part, 49 + 162 * Part, 190 + Half * Part, 49 + 162 * Part,
                   IM_COL32(255, 255, 255, 24), true);
        }
        S.Draw->AddRectFilled(S.At(172, 22), S.At(208, 46), IM_COL32(69, 73, 67, 255), S.Of(5.0f));
        S.Draw->AddRect(S.At(172, 22), S.At(208, 46), IM_COL32(165, 171, 158, 255), S.Of(5.0f), 0, 1.0f);
        S.Line(190, 49, 190, 211, IM_COL32(255, 255, 255, 48), true);
        std::snprintf(Word, sizeof(Word), "%g\u00b0 FULL CONE", double(L.Angle));
        S.Span(190 - Half, 190 + Half, 225, Word);
        S.Text("PENUMBRA", 20, 16);
        std::snprintf(Word, sizeof(Word), "%d %%", int(L.Penumbra * 100.0f + 0.5f));
        S.Text(Word, 360, 16, Anchor::Right);
    }
    else if (L.Kind == LightKind::Point)
    {
        for (int Index = 4; Index > 0; --Index)
        {
            const float Reach = float(Index) * 24.0f;
            S.Ring(190, 123, Reach, IM_COL32(255, 255, 255, 28));
            Fixed(Word, sizeof(Word), L.Distance * float(Index) / 4.0f, 0); std::strcat(Word, " m");
            S.Text(Word, 190 + Reach + 7.0f, 123);
        }
        for (int Index = 0; Index < 12; ++Index)
        {
            const float A = float(Index) * Pi / 6.0f;
            S.Line(190 + std::cos(A) * 25.0f, 123 + std::sin(A) * 25.0f,
                   190 + std::cos(A) * 96.0f, 123 + std::sin(A) * 96.0f, S.Tint(0.15f), true);
        }
        Glow(S.Draw, S.At(190, 123), S.Of(80.0f), S.Rgb, ImMin(0.45f, Shine / 100.0f));
        S.Disc(190, 123, 6.0f, S.Tint(0.65f));
        S.Ring(190, 123, 6.0f, S.Tint(0.9f));
        S.Text("OMNIDIRECTIONAL", 20, 18);
        char Fall[16]; Fixed(Fall, sizeof(Fall), L.Decay, 1);
        std::snprintf(Word, sizeof(Word), "DECAY %s", Fall);
        S.Text(Word, 360, 18, Anchor::Right);
        S.Text("Reach guides \u00b7 free space", 190, 248, Anchor::Centre);
    }
    else   // Tube
    {
        const float Run  = ImClamp(160.0f + L.Length * 22.0f, 165.0f, 302.0f);
        const float Thick = ImClamp(7.0f + L.Radius * 85.0f, 7.0f, 28.0f);
        const float Left = 190.0f - Run * 0.5f;
        SoftEdge(S.Draw, S.At(Left, 104 - Thick), S.At(Left + Run, 104 + Thick), S.Of(Thick), S.Tint(0.4f), S.Of(20.0f));
        const int First = S.Draw->VtxBuffer.Size;
        S.Draw->AddRectFilled(S.At(Left, 104 - Thick), S.At(Left + Run, 104 + Thick), IM_COL32_WHITE, S.Of(Thick));
        ShadeDown(S.Draw, First, S.Draw->VtxBuffer.Size, S.At(0, 104 - Thick).y, S.At(0, 104 + Thick).y,
                  S.Tint(0.1f), S.Tint(0.15f));
        S.Draw->AddRect(S.At(Left, 104 - Thick), S.At(Left + Run, 104 + Thick), S.Tint(0.7f), S.Of(Thick), 0, 1.0f);
        for (float X : { Left + 7.0f, Left + Run - 7.0f })
        {
            S.Line(X, 104 - Thick, X, 104 + Thick, IM_COL32(155, 165, 152, 255));
        }
        for (int Index = 0; Index < 9; ++Index)
        {
            const float X = Left + (Run * float(Index)) / 8.0f;
            S.Line(X, 104 + Thick + 10.0f, X, 191, S.Tint(0.12f), true);
            S.Ring(X, 191, 1.0f, S.Tint(0.25f));
        }
        char Len[16]; Fixed(Len, sizeof(Len), L.Length, 1);
        std::snprintf(Word, sizeof(Word), "%s m LENGTH", Len);
        S.Span(Left, Left + Run, 221, Word);
        Fixed(Word, sizeof(Word), L.Radius * 1000.0f, 0); std::strcat(Word, " mm RADIUS");
        S.Text(Word, 20, 18);
        S.Text("RADIAL EMISSION", 360, 18, Anchor::Right);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      RESPONSE PLOT
//------------------------------------------------------------------------------------------------------------------------
// ProjectResponse(): the point source gets a full illuminance plot, every other style a normalized section.

void PaintResponse(const Study& S, const Emitter& L, float Wide, float High)
{
    ImDrawList* Draw = S.Draw;
    ImFont* Face = S.Face;
    const float Shine = Flux(L);
    char Word[64];

    auto Spot = [&](float X, float Y) { return ImVec2{ S.Base.x + X, S.Base.y + Y }; };

    if (L.Kind == LightKind::Point)
    {
        const float Left = 43.0f, Right = Wide - 15.0f, Top = 28.0f, Foot = High - 30.0f;
        const float Peak = ImMax(0.01f, Shine);
        auto ValueAt = [&](float D) { return Shine / std::pow(D, L.Decay); };
        auto PlotX   = [&](float D) { return Left + ((D - 1.0f) / 9.0f) * (Right - Left); };
        auto PlotY   = [&](float V) { return Foot - (V / Peak) * (Foot - Top); };
        auto Label   = [&](float V, char* Out, size_t Size)
        {
            if (V >= 1000.0f) { char N[32]; Fixed(N, sizeof(N), V / 1000.0, 1); std::snprintf(Out, Size, "%sk", N); }
            else              { Fixed(Out, Size, V, V < 10.0f ? 2 : 1); }
        };

        Inked(Draw, Face, 9.0f, Spot(Left, 12.0f), PlotText, "ILLUMINANCE \u00b7 lx", Anchor::Left);
        for (float Part : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            const float Y = PlotY(Peak * Part);
            Draw->AddLine(Spot(Left, Y), Spot(Right, Y), PlotTick, 1.0f);
            Label(Peak * Part, Word, sizeof(Word));
            Inked(Draw, Face, 9.0f, Spot(Left - 7.0f, Y + 3.0f), PlotText, Word, Anchor::Right);
        }
        ImVec2 Curve[121];
        for (int Index = 0; Index <= 120; ++Index)
        {
            const float D = 1.0f + (9.0f * float(Index)) / 120.0f;
            Curve[Index] = Spot(PlotX(D), PlotY(ValueAt(D)));
        }
        // The region under a 1 / d^n curve is concave, which AddConvexPolyFilled cannot close; fill it
        //    as one quad per segment down to the baseline instead.
        const ImU32 Under = S.Tint(0.06f);
        for (int Index = 0; Index < 120; ++Index)
        {
            Draw->AddQuadFilled(Curve[Index], Curve[Index + 1],
                                { Curve[Index + 1].x, Spot(0.0f, Foot).y },
                                { Curve[Index].x,     Spot(0.0f, Foot).y }, Under);
        }
        Draw->AddPolyline(Curve, 121, S.Tint(0.9f), ImDrawFlags_None, 1.5f);
        for (float D : { 1.0f, 2.0f, 5.0f, 10.0f })
        {
            const bool Keyed = D == 5.0f;
            DashedLine(Draw, Spot(PlotX(D), Top), Spot(PlotX(D), Foot), Keyed ? PlotEdge : PlotTick, 2.0f, 4.0f);
            Draw->AddCircleFilled(Spot(PlotX(D), PlotY(ValueAt(D))), 3.0f, Keyed ? PlotMark : S.Tint(0.9f), 24);
            std::snprintf(Word, sizeof(Word), "%g m", double(D));
            Inked(Draw, Face, 9.0f, Spot(PlotX(D), Foot + 16.0f), PlotFoot, Word, Anchor::Centre);
        }
        return;
    }

    const float Left = 8.0f, Right = Wide - 8.0f, Top = 10.0f, Foot = High - 18.0f;
    for (int Index = 0; Index < 3; ++Index)
    {
        const float Y = Top + (float(Index) * (Foot - Top)) / 2.0f;
        DashedLine(Draw, Spot(Left, Y), Spot(Right, Y), IM_COL32(255, 255, 255, 23), 2.0f, 5.0f);
    }
    auto Section = [&](float Part)
    {
        const float Normal = std::fabs((Part - 0.5f) * 180.0f) / (ImMax(2.0f, L.Angle) * 0.5f);
        return Normal > 1.0f ? 0.0f
             : Normal <= 1.0f - L.Penumbra ? 1.0f
             : ImMax(0.0f, (1.0f - Normal) / ImMax(0.001f, L.Penumbra));
    };
    auto At = [&](float Part)
    {
        switch (L.Kind)
        {
        case LightKind::Ies:   return Polar(L.Profile, (Part - 0.5f) * Pi, L.Cone, 0);
        case LightKind::Spot:  return Section(Part);
        case LightKind::Led:   return Part * L.Dimmer;
        case LightKind::Strip: return Part;
        case LightKind::Area:  return std::cos((Part - 0.5f) * Pi);
        default:               return 0.72f;
        }
    };
    ImVec2 Curve[81];
    for (int Index = 0; Index <= 80; ++Index)
    {
        const float Part = float(Index) / 80.0f;
        Curve[Index] = Spot(Left + Part * (Right - Left), Foot - At(Part) * (Foot - Top));
    }
    Draw->AddPolyline(Curve, 81, S.Tint(0.8f), ImDrawFlags_None, 1.0f);

    const bool Angular = L.Kind == LightKind::Ies || L.Kind == LightKind::Spot || L.Kind == LightKind::Area;
    Inked(Draw, Face, 9.0f, Spot(Left, High - 3.0f), AxisFoot, Angular ? "\u221290\u00b0" : "0", Anchor::Left);
    if (Angular)              std::snprintf(Word, sizeof(Word), "90\u00b0");
    else if (L.Kind == LightKind::Strip) { char N[24]; Fixed(N, sizeof(N), L.Length, 1); std::snprintf(Word, sizeof(Word), "%s m", N); }
    else                      std::snprintf(Word, sizeof(Word), "100%%");
    Inked(Draw, Face, 9.0f, Spot(Right, High - 3.0f), AxisFoot, Word, Anchor::Right);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        CARD CHROME
//------------------------------------------------------------------------------------------------------------------------
// The column is .lighting-panel: flex, column, gap 12. Cards measure themselves, so the background is
//    drawn behind the content through a splitter once the height is known.

struct Column
{
    ImDrawList*        Draw = nullptr;
    ImFont*            Face = nullptr;
    ImDrawListSplitter Split{};
    float              Left = 0.0f, Wide = 0.0f, Pen = 0.0f;
    float              Mark = 0.0f, PadX = CardPadX;

    void Begin(float PadLeft)
    {
        PadX = PadLeft;
        Mark = Pen;
        Split.Split(Draw, 2);
        Split.SetCurrentChannel(Draw, 1);
    }
    void End()
    {
        Split.SetCurrentChannel(Draw, 0);
        Draw->AddRectFilled({ Left, Mark }, { Left + Wide, Pen }, CardFill, CardRadius);
        Draw->AddRect({ Left, Mark }, { Left + Wide, Pen }, CardEdge, CardRadius, 0, 1.0f);
        Split.Merge(Draw);
        Pen += StackGap;
    }
    float Inner() const { return Wide - PadX * 2.0f; }
    float Ink()   const { return Left + PadX; }

    // .lp-card > header — the glyph, the title, the right-hand caption, then a 15 px skirt.
    void Head(const char* Title, const char* Caption, const IconSymbol* Artwork)
    {
        Pen += CardPadY;
        float Cursor = Ink();
        if (Artwork && LightGlyphSource)
        {
            if (ImTextureID Sheet = LightGlyphSource(*Artwork, 25.0f))
            {
                Draw->AddImage(Sheet, { Cursor, Pen }, { Cursor + 25.0f, Pen + 25.0f });
            }
            Cursor += 33.0f;
        }
        Draw->AddText(Face, 14.0f, { Cursor, Pen + (Artwork ? 5.0f : 0.0f) }, HeadText, Title);
        if (Caption && *Caption)
        {
            Inked(Draw, Face, 8.0f, { Ink() + Inner(), Pen + (Artwork ? 16.0f : 11.0f) }, HeadCaption, Caption,
                  Anchor::Right);
        }
        Pen += (Artwork ? 25.0f : 17.0f) + HeadSkirt;
    }

    // .field — a 10 px label, then the pill holding the black entry box and the range track.
    void Field(const char* Name, float Value, float Low, float High, int Decimals, const char* Unit,
               EditorProperty* Bound = nullptr, float Step = 0.0f)
    {
        Pen += 17.0f;
        Draw->AddText(Face, 10.0f, { Ink(), Pen }, FieldLabel, Name);
        Pen += 10.0f + 9.0f;
        char Figure[48];
        Fixed(Figure, sizeof(Figure), Value, Decimals);
        const float BoxWide = 58.0f;
        Draw->AddRectFilled({ Ink(), Pen }, { Ink() + BoxWide, Pen + 26.0f }, EntryFill, 7.0f);
        Inked(Draw, Face, 11.0f, { Ink() + BoxWide - 7.0f, Pen + 17.0f }, EntryText, Figure, Anchor::Right);
        if (Unit && *Unit)
        {
            Draw->AddText(Face, 9.0f, { Ink() + BoxWide + 7.0f, Pen + 9.0f }, FieldLabel, Unit);
        }
        const float UnitWide = Unit && *Unit ? Width(Face, 9.0f, Unit) + 14.0f : 6.0f;
        const float TrackLeft = Ink() + BoxWide + UnitWide;
        const float TrackRight = Ink() + Inner();
        const float Share = ImClamp((Value - Low) / ImMax(0.0001f, High - Low), 0.0f, 1.0f);
        const float Divide = TrackLeft + (TrackRight - TrackLeft) * Share;
        Draw->AddRectFilled({ TrackLeft, Pen }, { TrackRight, Pen + 26.0f }, TrackDim, 13.0f);
        if (Divide > TrackLeft + 1.0f)
        {
            Draw->PushClipRect({ TrackLeft, Pen }, { Divide, Pen + 26.0f }, true);
            Draw->AddRectFilled({ TrackLeft, Pen }, { TrackRight, Pen + 26.0f }, TrackLit, 13.0f);
            Draw->PopClipRect();
        }
        // input[type=range]: pressing anywhere on the track seeks to it, and a drag keeps following.
        if (Bound && Bound->Editable)
        {
            ImGui::SetCursorScreenPos({ TrackLeft, Pen });
            ImGui::InvisibleButton(Name, { ImMax(1.0f, TrackRight - TrackLeft), 26.0f });
            if (ImGui::IsItemActive())
            {
                const float Reach = ImMax(0.0001f, TrackRight - TrackLeft);
                float Next = Low + ImClamp((ImGui::GetIO().MousePos.x - TrackLeft) / Reach, 0.0f, 1.0f) * (High - Low);
                if (Step > 0.0f)
                {
                    Next = Low + std::floor((Next - Low) / Step + 0.5f) * Step;
                }
                Bound->Figure = ImClamp(Next, Low, High);
            }
        }
        Pen += 26.0f + 17.0f;
    }

    // .lp-toggle-row with .lp-switch — 38 x 21, the knob sliding 17 px.
    void Switch(const char* Name, bool On, EditorProperty* Bound = nullptr)
    {
        Pen += 15.0f;
        Draw->AddText(Face, 11.0f, { Ink(), Pen + 3.0f }, RowText, Name);
        const float Right = Ink() + Inner();
        Draw->AddRectFilled({ Right - 38.0f, Pen }, { Right, Pen + 21.0f }, On ? SwitchOn : SwitchOff, 10.5f);
        const float Knob = Right - 38.0f + 2.0f + (On ? 17.0f : 0.0f);
        Draw->AddCircleFilled({ Knob + 8.5f, Pen + 10.5f }, 8.5f, SwitchKnob, 32);
        if (Bound && Bound->Editable)
        {
            ImGui::SetCursorScreenPos({ Right - 38.0f, Pen });
            ImGui::InvisibleButton(Name, { 38.0f, 21.0f });
            if (ImGui::IsItemClicked())
            {
                Bound->On = !Bound->On;
            }
        }
        Pen += 21.0f + 15.0f;
    }

    // .lp-note
    void Note(const char* Body)
    {
        Pen += 12.0f;
        Draw->AddText(Face, 9.0f, { Ink(), Pen }, NoteText, Body);
        Pen += 13.0f + 12.0f;
    }

    // .lp-profiles — a two-column grid of pressed-state buttons.
    void Choices(const char* const* Names, unsigned Count, unsigned Picked, EditorProperty* Bound = nullptr)
    {
        const float Cell = (Inner() - 6.0f) * 0.5f;
        for (unsigned Index = 0; Index < Count; ++Index)
        {
            const float X = Ink() + float(Index % 2u) * (Cell + 6.0f);
            const float Y = Pen + float(Index / 2u) * (29.0f + 6.0f);
            const bool On = Index == Picked;
            Draw->AddRectFilled({ X, Y }, { X + Cell, Y + 29.0f }, On ? PickedFill : ChoiceFill, 4.0f);
            Draw->AddRect({ X, Y }, { X + Cell, Y + 29.0f }, On ? PickedEdge : ChoiceEdge, 4.0f, 0, 1.0f);
            Inked(Draw, Face, 10.0f, { X + Cell * 0.5f, Y + 18.0f }, On ? PickedText : ChoiceText, Names[Index],
                  Anchor::Centre);
            if (Bound && Bound->Editable)
            {
                ImGui::SetCursorScreenPos({ X, Y });
                ImGui::InvisibleButton(Names[Index], { Cell, 29.0f });
                if (ImGui::IsItemClicked())
                {
                    Bound->Picked = Index;
                }
            }
        }
        Pen += float((Count + 1u) / 2u) * (29.0f + 6.0f) - 6.0f;
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE RECORDED PANEL
//------------------------------------------------------------------------------------------------------------------------

LightKind Resolve(const EditorSheet& Sheet)
{
    for (uint32_t G = 0; G < Sheet.GroupCount; ++G)
    {
        for (uint32_t P = 0; P < Sheet.Groups[G].PropertyCount; ++P)
        {
            const EditorProperty& Slot = Sheet.Groups[G].Properties[P];
            if (Named(Slot.Label, "Type") && Slot.Picked < 7u)
            {
                return static_cast<LightKind>(Slot.Picked);
            }
        }
    }
    return LightKind::Point;
}

}   // namespace

void RecordLightInspector(ControlPanel&, EditorInstance&, EditorSheet& Sheet)
{
    Emitter Light;
    Light.Kind = Resolve(Sheet);
    Adopt(Sheet, Light);

    // Colour(): the authored hex folded with the kelvin curve, exactly as LightPanel.js does it.
    float Rgb[3] = { Light.Rgb[0], Light.Rgb[1], Light.Rgb[2] };
    if (Light.Kind == LightKind::Ies  || Light.Kind == LightKind::Tube
     || Light.Kind == LightKind::Led  || Light.Kind == LightKind::Strip)
    {
        float Warm[3];
        Kelvin(Light.Temperature, Warm);
        for (int Channel = 0; Channel < 3; ++Channel)
        {
            Rgb[Channel] *= Warm[Channel];
        }
    }

    const KindFacts& Kind = Facts(Light.Kind);
    const float Shine = Flux(Light);
    const bool  Point = Light.Kind == LightKind::Point;
    const bool  Spot  = Light.Kind == LightKind::Spot;
    const bool  Ies   = Light.Kind == LightKind::Ies;
    const bool  Area  = Light.Kind == LightKind::Area;
    const bool  Led   = Light.Kind == LightKind::Led;
    const bool  Strip = Light.Kind == LightKind::Strip;
    const bool  Tube  = Light.Kind == LightKind::Tube;

    const ImVec2 Origin = ImGui::GetCursorScreenPos();
    Column Panel;
    Panel.Draw = ImGui::GetWindowDrawList();
    Panel.Face = ImGui::GetFont();
    Panel.Left = Origin.x;
    Panel.Wide = ImGui::GetContentRegionAvail().x;
    Panel.Pen  = Origin.y;

    char Word[128], Figure[64], Rest[64];

    //--------------------------------------------------------------------------------------------------------------
    // 1 — Preview. .lp-preview overrides the card padding to 15 / 12 / 12.
    //--------------------------------------------------------------------------------------------------------------
    Panel.Begin(12.0f);
    Panel.Head(Kind.Preview, "", &Kind.Artwork);
    {
        Grouped(Figure, sizeof(Figure), Shine, 1);
        char* Break = std::strrchr(Figure, '.');
        std::snprintf(Rest, sizeof(Rest), "%s", Break ? Break : ".0");
        if (Break) { *Break = '\0'; }
        const float WholeWide = Width(Panel.Face, 42.0f, Figure);
        const float FractWide = Width(Panel.Face, 42.0f, Rest);
        Panel.Draw->AddText(Panel.Face, 42.0f, { Panel.Ink(), Panel.Pen }, PrimaryText, Figure);
        Panel.Draw->AddText(Panel.Face, 42.0f, { Panel.Ink() + WholeWide, Panel.Pen }, PrimaryFract, Rest);
        Panel.Draw->AddText(Panel.Face, 14.0f, { Panel.Ink() + WholeWide + FractWide + 7.0f, Panel.Pen + 26.0f },
                            PrimaryUnit, Kind.Unit);
        Panel.Pen += 44.0f + 8.0f;
        Panel.Draw->AddText(Panel.Face, 10.0f, { Panel.Ink(), Panel.Pen }, CaptionText, Kind.Caption);
        Panel.Pen += 12.0f + 16.0f;

        Panel.Draw->AddLine({ Panel.Ink(), Panel.Pen }, { Panel.Ink() + Panel.Inner(), Panel.Pen }, CardEdge, 1.0f);
        Panel.Pen += 11.0f;
        if (Ies)        std::snprintf(Word, sizeof(Word), "%s / synthetic polar study", ProfileNames[Light.Profile]);
        else if (Area)  { char A[16], Bv[16]; Fixed(A, sizeof(A), Light.Across, 1); Fixed(Bv, sizeof(Bv), Light.Tall, 1);
                          std::snprintf(Word, sizeof(Word), "%s / %s \u00d7 %s m aperture", Light.Disk ? "Disk" : "Rectangle", A, Bv); }
        else if (Strip) { char A[16]; Fixed(A, sizeof(A), Light.Length, 1);
                          std::snprintf(Word, sizeof(Word), "Horizontal / %s m run", A); }
        else if (Led)   std::snprintf(Word, sizeof(Word), "%g mm package / %g K", double(Light.Diameter), double(Light.Temperature));
        else if (Spot)  std::snprintf(Word, sizeof(Word), "%g\u00b0 cone / %d%% soft edge", double(Light.Angle),
                                      int(Light.Penumbra * 100.0f + 0.5f));
        else if (Point) { char D[16]; Fixed(D, sizeof(D), Light.Decay, 1);
                          std::snprintf(Word, sizeof(Word), "Omnidirectional / decay %s", D); }
        else            { char A[16]; Fixed(A, sizeof(A), Light.Length, 1);
                          std::snprintf(Word, sizeof(Word), "%s m linear emitter", A); }
        Panel.Draw->AddText(Panel.Face, 10.0f, { Panel.Ink(), Panel.Pen }, DescribeText, Word);
        Panel.Pen += 15.0f + 10.0f;

        const float WellWide = Panel.Inner(), WellHigh = 260.0f;
        Panel.Draw->AddRectFilled({ Panel.Ink(), Panel.Pen }, { Panel.Ink() + WellWide, Panel.Pen + WellHigh },
                                  IM_COL32(0, 0, 0, 0), WellRadius);
        Study Canvas;
        Canvas.Draw = Panel.Draw;
        Canvas.Face = Panel.Face;
        Canvas.Rgb  = Rgb;
        Canvas.Zoom = ImMin(WellWide / StudyWide, WellHigh / StudyHigh);
        Canvas.Base = { Panel.Ink() + (WellWide - StudyWide * Canvas.Zoom) * 0.5f,
                        Panel.Pen  + (WellHigh - StudyHigh * Canvas.Zoom) * 0.5f };
        Panel.Draw->PushClipRect({ Panel.Ink(), Panel.Pen },
                                 { Panel.Ink() + WellWide, Panel.Pen + WellHigh }, true);
        PaintStudy(Canvas, Light);
        Panel.Draw->PopClipRect();
        Panel.Pen += WellHigh + 3.0f;

        Panel.Draw->AddText(Panel.Face, 9.0f, { Panel.Ink(), Panel.Pen }, StudyText,
                            Ies ? "Illustrative preset \u00b7 no measured IES data" : "Source study \u00b7 HTML authoring");
        Panel.Pen += 12.0f + 12.0f;
    }
    Panel.End();

    //--------------------------------------------------------------------------------------------------------------
    // 2 — The readings rail. Two tiles, column-reverse so the key sits above the value.
    //--------------------------------------------------------------------------------------------------------------
    {
        const char* Keys[2];
        char Values[2][48];
        if (Led)
        {
            Keys[0] = "Driver power";    Grouped(Values[0], 48, Light.Watts, 1);    std::strcat(Values[0], " W");
            Keys[1] = "Efficacy target"; Grouped(Values[1], 48, Light.Efficacy, 0); std::strcat(Values[1], " lm/W");
        }
        else if (Strip)
        {
            Keys[0] = "Connected load";  Grouped(Values[0], 48, Light.Length * Light.WattsPerMetre, 1); std::strcat(Values[0], " W");
            Keys[1] = "Emitter count";   Grouped(Values[1], 48, std::floor(Light.Length * Light.LedsPerMetre + 0.5f), 0); std::strcat(Values[1], " LEDs");
        }
        else if (Point)
        {
            Keys[0] = "At 5 m \u00b7 estimate"; Grouped(Values[0], 48, Shine / std::pow(5.0f, Light.Decay), 2); std::strcat(Values[0], " lx");
            Keys[1] = "Reach";           Grouped(Values[1], 48, Light.Distance, 0); std::strcat(Values[1], " m");
        }
        else if (Spot)
        {
            Keys[0] = "Full cone";       Grouped(Values[0], 48, Light.Angle, 1); std::strcat(Values[0], "\u00b0");
            Keys[1] = "Soft edge";       Grouped(Values[1], 48, Light.Penumbra * 100.0f, 0); std::strcat(Values[1], " %");
        }
        else if (Ies)
        {
            Keys[0] = "Scaled output";   Grouped(Values[0], 48, Shine * Light.Multiplier, 0); std::strcat(Values[0], " lm");
            Keys[1] = "Field angle";     Grouped(Values[1], 48, Light.Cone, 1); std::strcat(Values[1], "\u00b0");
        }
        else if (Area)
        {
            Keys[0] = "Aperture";        Grouped(Values[0], 48, Light.Across * Light.Tall * (Light.Disk ? Pi / 4.0f : 1.0f), 2); std::strcat(Values[0], " m\u00b2");
            Keys[1] = "Emission";        std::snprintf(Values[1], 48, "%s", Light.TwoSided ? "Two-sided" : "One-sided");
        }
        else
        {
            Keys[0] = "Linear output";   Grouped(Values[0], 48, Shine / ImMax(0.1f, Light.Length), 0); std::strcat(Values[0], " lm/m");
            Keys[1] = "Tube radius";     Grouped(Values[1], 48, Light.Radius * 1000.0f, 0); std::strcat(Values[1], " mm");
        }
        const float Cell = (Panel.Wide - 8.0f) * 0.5f;
        for (int Index = 0; Index < 2; ++Index)
        {
            const float X = Panel.Left + float(Index) * (Cell + 8.0f);
            Panel.Draw->AddRectFilled({ X, Panel.Pen }, { X + Cell, Panel.Pen + 80.0f }, TileFill, 6.0f);
            Panel.Draw->AddText(Panel.Face, 9.0f, { X + 12.0f, Panel.Pen + 13.0f }, TileKey, Keys[Index]);
            Panel.Draw->AddText(Panel.Face, 24.0f, { X + 12.0f, Panel.Pen + 80.0f - 13.0f - 26.0f }, TileValue,
                                Values[Index]);
        }
        Panel.Pen += 80.0f + StackGap;
    }

    //--------------------------------------------------------------------------------------------------------------
    // 3 — Scene participation.
    //--------------------------------------------------------------------------------------------------------------
    Panel.Begin(CardPadX);
    Panel.Head("Scene participation", "FLAGS", nullptr);
    Panel.Switch("Cast shadows", Light.Shadows, Bind(Sheet, "Cast shadows"));
    const char* ShownName = Ies ? "Draw distribution" : Spot ? "Draw cone" : Point ? "Show glow" : "Draw emitter";
    Panel.Switch(ShownName, Light.Shown, Bind(Sheet, ShownName));
    Panel.Pen += CardPadY - 15.0f;
    Panel.End();

    //--------------------------------------------------------------------------------------------------------------
    // 4 — Output, with the response block kept inside the card that drives it.
    //--------------------------------------------------------------------------------------------------------------
    Panel.Begin(CardPadX);
    Panel.Head(Kind.Output, "OUTPUT", nullptr);
    if (Led)
    {
        Panel.Field("Driver power", Light.Watts, 0.1f, 100.0f, 1, "W", Bind(Sheet, "Driver power"), 0.1f);
        Panel.Field("Efficacy target", Light.Efficacy, 10.0f, 250.0f, 0, "lm/W", Bind(Sheet, "Efficacy target"), 1.0f);
        Panel.Field("Dimmer", Light.Dimmer, 0.0f, 1.0f, 2, "", Bind(Sheet, "Dimmer"), 0.01f);
    }
    else if (Strip)
    {
        Panel.Field("Flux per metre", Light.LumensPerMetre, 10.0f, 4000.0f, 0, "lm/m", Bind(Sheet, "Flux per metre"), 10.0f);
        Panel.Field("Load per metre", Light.WattsPerMetre, 1.0f, 50.0f, 1, "W/m", Bind(Sheet, "Load per metre"), 0.1f);
        Panel.Field("Dimmer", Light.Dimmer, 0.0f, 1.0f, 2, "", Bind(Sheet, "Dimmer"), 0.01f);
    }
    else
    {
        const char* FluxName = Point || Spot ? "Intensity" : "Luminous flux";
        Panel.Field(FluxName, Point || Spot ? Light.Intensity : Light.Lumens, 0.0f,
                    Point ? 60.0f : Spot ? 200.0f : Ies ? 8000.0f : Area ? 20000.0f : 12000.0f,
                    Point ? 1 : 0, Point || Spot ? "cd" : "lm", Bind(Sheet, FluxName),
                    Point ? 0.1f : Spot ? 1.0f : 50.0f);
    }
    if (Ies || Tube || Led || Strip)
    {
        Panel.Field("Colour temperature", Light.Temperature, 1800.0f, 12000.0f, 0, "K", Bind(Sheet, "Colour temperature"), 100.0f);
    }
    {
        // .lp-colour — the swatch and its hex readout.
        Panel.Pen += 17.0f;
        Panel.Draw->AddText(Panel.Face, 10.0f, { Panel.Ink(), Panel.Pen + 6.0f }, FieldLabel, "Emission tint");
        const float Chip = Panel.Ink() + Width(Panel.Face, 10.0f, "Emission tint") + 10.0f;
        Panel.Draw->AddRectFilled({ Chip, Panel.Pen }, { Chip + 34.0f, Panel.Pen + 22.0f },
                                  FromBytes(Light.Rgb, 1.0f), 5.0f);
        std::snprintf(Word, sizeof(Word), "#%02X%02X%02X",
                      int(ImClamp(Light.Rgb[0], 0.0f, 1.0f) * 255.0f + 0.5f),
                      int(ImClamp(Light.Rgb[1], 0.0f, 1.0f) * 255.0f + 0.5f),
                      int(ImClamp(Light.Rgb[2], 0.0f, 1.0f) * 255.0f + 0.5f));
        Panel.Draw->AddText(Panel.Face, 10.0f, { Chip + 44.0f, Panel.Pen + 6.0f }, FieldLabel, Word);
        Panel.Pen += 22.0f;
    }
    {
        // The response block: Card() built its header, then the card styling was stripped off it.
        Panel.Pen += 17.0f;
        Panel.Draw->AddText(Panel.Face, 14.0f, { Panel.Ink(), Panel.Pen }, HeadText, Kind.Response);
        Inked(Panel.Draw, Panel.Face, 8.0f, { Panel.Ink() + Panel.Inner(), Panel.Pen + 11.0f }, HeadCaption,
              "ANALYTICAL", Anchor::Right);
        Panel.Pen += 17.0f + HeadSkirt;

        if (Ies)        { Grouped(Word, sizeof(Word), Light.Cone, 0); std::strcat(Word, "\u00b0"); }
        else if (Area)  { Grouped(Word, sizeof(Word), Shine / ImMax(0.01f, Light.Across * Light.Tall * (Light.Disk ? Pi / 4.0f : 1.0f)), 0); std::strcat(Word, " lm/m\u00b2"); }
        else if (Strip) { Grouped(Word, sizeof(Word), (Light.Length * Light.WattsPerMetre) / ImMax(1.0f, Light.Voltage), 2); std::strcat(Word, " A"); }
        else if (Led)   { Grouped(Word, sizeof(Word), Light.Dimmer * 100.0f, 0); std::strcat(Word, " %"); }
        else if (Spot)  { Grouped(Word, sizeof(Word), 2.0f * 5.0f * std::tan(Light.Angle * Pi / 360.0f), 2); std::strcat(Word, " m"); }
        else if (Point) { Grouped(Word, sizeof(Word), Shine / std::pow(5.0f, Light.Decay), 2); std::strcat(Word, " lx"); }
        else            { Grouped(Word, sizeof(Word), Shine / ImMax(0.1f, Light.Length), 0); std::strcat(Word, " lm/m"); }
        Panel.Draw->AddText(Panel.Face, 18.0f, { Panel.Ink(), Panel.Pen }, TileValue, Word);
        Panel.Pen += 20.0f;

        Panel.Note(Ies ? "Normalized preset sections \u00b7 not measured candela"
                 : Area ? "Flux per aperture area \u00b7 not surface luminance"
                 : Strip ? "Connected watts \u00f7 supply volts \u00b7 ideal full-load current"
                 : Led ? "Authored dimmer \u00b7 estimated output ignores thermal losses"
                 : Spot ? "Beam diameter on a perpendicular plane at 5 m"
                 : Point ? "Authored decay at 5 m \u00b7 free-space estimate"
                 : "Authored source flux per metre");

        const float PlotHigh = Point ? 180.0f : 100.0f;
        Study Plot;
        Plot.Draw = Panel.Draw;
        Plot.Face = Panel.Face;
        Plot.Rgb  = Rgb;
        Plot.Zoom = 1.0f;
        Plot.Base = { Panel.Ink(), Panel.Pen };
        Panel.Draw->PushClipRect({ Panel.Ink(), Panel.Pen },
                                 { Panel.Ink() + Panel.Inner(), Panel.Pen + PlotHigh }, true);
        PaintResponse(Plot, Light, Panel.Inner(), PlotHigh);
        Panel.Draw->PopClipRect();
        Panel.Pen += PlotHigh;

        if (Point)
        {
            // .lp-response-samples — the 1 / 2 / 5 / 10 m table.
            Panel.Pen += 10.0f;
            const char* Heads[3] = { "Distance", "Estimate", "Relative" };
            const float Columns[3] = { 0.0f, Panel.Inner() * 0.40f, Panel.Inner() * 0.78f };
            for (int Index = 0; Index < 3; ++Index)
            {
                Panel.Draw->AddText(Panel.Face, 9.0f, { Panel.Ink() + Columns[Index], Panel.Pen }, TileKey, Heads[Index]);
            }
            Panel.Pen += 14.0f;
            for (float Distance : { 1.0f, 2.0f, 5.0f, 10.0f })
            {
                const float Share = 1.0f / std::pow(Distance, Light.Decay);
                std::snprintf(Word, sizeof(Word), "%g m", double(Distance));
                Panel.Draw->AddText(Panel.Face, 9.0f, { Panel.Ink() + Columns[0], Panel.Pen }, DescribeText, Word);
                Grouped(Figure, sizeof(Figure), Shine * Share, 2); std::strcat(Figure, " lx");
                Panel.Draw->AddText(Panel.Face, 9.0f, { Panel.Ink() + Columns[1], Panel.Pen }, DescribeText, Figure);
                Fixed(Rest, sizeof(Rest), Share * 100.0f, 1); std::strcat(Rest, "%");
                Panel.Draw->AddText(Panel.Face, 9.0f, { Panel.Ink() + Columns[2], Panel.Pen }, DescribeText, Rest);
                Panel.Pen += 14.0f;
            }
        }
        Panel.Pen += CardPadY;
    }
    Panel.End();

    //--------------------------------------------------------------------------------------------------------------
    // 5 — Shape.
    //--------------------------------------------------------------------------------------------------------------
    Panel.Begin(CardPadX);
    Panel.Head(Kind.Shape, "OPTICS", nullptr);
    if (Led)
    {
        Panel.Field("Package diameter", Light.Diameter, 5.0f, 120.0f, 0, "mm", Bind(Sheet, "Package diameter"), 1.0f);
        Panel.Field("Emission angle", Light.Angle, 10.0f, 180.0f, 0, "\u00b0", Bind(Sheet, "Emission angle"), 1.0f);
    }
    else if (Strip)
    {
        Panel.Field("Strip length", Light.Length, 0.1f, 20.0f, 1, "m", Bind(Sheet, "Strip length"), 0.1f);
        Panel.Field("Emitter density", Light.LedsPerMetre, 10.0f, 240.0f, 0, "/m", Bind(Sheet, "Emitter density"), 1.0f);
        Panel.Field("Supply voltage", Light.Voltage, 5.0f, 48.0f, 0, "V", Bind(Sheet, "Supply voltage"), 1.0f);
        Panel.Switch("Opal diffuser", Light.Diffuser, Bind(Sheet, "Opal diffuser"));
    }
    else if (Ies)
    {
        Panel.Choices(ProfileNames, 8u, Light.Profile, Bind(Sheet, "Profile"));
        Panel.Note("Preset illustration \u00b7 IES file import pending.");
        Panel.Field("Profile multiplier", Light.Multiplier, 0.0f, 4.0f, 2, "\u00d7", Bind(Sheet, "Profile multiplier"), 0.05f);
        Panel.Field("Field angle", Light.Cone, 5.0f, 100.0f, 1, "\u00b0", Bind(Sheet, "Field angle"), 0.5f);
        Panel.Field("Cut-off pitch", Light.Cutoff, -5.0f, 5.0f, 1, "\u00b0", Bind(Sheet, "Cut-off pitch"), 0.1f);
        Panel.Field("Photometric range", Light.Range, 1.0f, 250.0f, 0, "m", Bind(Sheet, "Photometric range"), 1.0f);
    }
    else if (Point)
    {
        Panel.Field("Reach", Light.Distance, 1.0f, 120.0f, 0, "m", Bind(Sheet, "Reach"), 1.0f);
        Panel.Field("Decay exponent", Light.Decay, 0.0f, 4.0f, 2, "", Bind(Sheet, "Decay exponent"), 0.05f);
    }
    else if (Spot)
    {
        Panel.Field("Full cone angle", Light.Angle, 2.0f, 80.0f, 1, "\u00b0", Bind(Sheet, "Full cone angle"), 0.5f);
        Panel.Field("Penumbra", Light.Penumbra, 0.0f, 1.0f, 2, "", Bind(Sheet, "Penumbra"), 0.01f);
    }
    else if (Area)
    {
        static const char* const Apertures[2] = { "Rectangle", "Disk" };
        Panel.Choices(Apertures, 2u, Light.Disk ? 1u : 0u, Bind(Sheet, "Aperture"));
        Panel.Field("Width", Light.Across, 0.1f, 20.0f, 1, "m", Bind(Sheet, "Width"), 0.1f);
        Panel.Field("Height", Light.Tall, 0.1f, 20.0f, 1, "m", Bind(Sheet, "Height"), 0.1f);
        Panel.Field("Beam spread", Light.Spread, 1.0f, 180.0f, 0, "\u00b0", Bind(Sheet, "Beam spread"), 1.0f);
        Panel.Switch("Two-sided emission", Light.TwoSided, Bind(Sheet, "Two-sided emission"));
    }
    else
    {
        Panel.Field("Length", Light.Length, 0.1f, 20.0f, 1, "m", Bind(Sheet, "Length"), 0.1f);
        Panel.Field("Tube radius", Light.Radius, 0.01f, 1.0f, 2, "m", Bind(Sheet, "Tube radius"), 0.01f);
        Panel.Field("Reach", Light.Distance, 1.0f, 120.0f, 0, "m", Bind(Sheet, "Reach"), 1.0f);
    }
    Panel.Pen += CardPadY - 17.0f;
    Panel.End();

    //--------------------------------------------------------------------------------------------------------------
    // 6 — Transform. TransformPanel.jsx: a fixed Position / Rotation / Scale grid under a WORLD SPACE kicker.
    //--------------------------------------------------------------------------------------------------------------
    Panel.Begin(CardPadX);
    Panel.Head("Transform", "WORLD SPACE", nullptr);
    {
        // TransformPanel.jsx's fixed rows, with the step, floor and ceiling it clamps each axis to.
        struct Row { const char* Name; const char* Unit; const float* Values; int Decimals;
                     float Step; float Floor; float Ceiling; };
        const Row Rows[3] = { { "Position", "m",       Light.Position, 2, 0.01f, -100000.0f, 100000.0f },
                              { "Rotation", "deg",     Light.Rotation, 1, 0.1f,   -36000.0f,  36000.0f },
                              { "Scale",    "\u00d7",  Light.Scale,    2, 0.01f,      0.001f,  1000.0f } };
        const float Label = 58.0f;
        const float Gutter = 20.0f;                       // the row unit keeps its own column
        const float Cell  = (Panel.Inner() - Label - Gutter - 8.0f) / 3.0f;
        for (const Row& Line : Rows)
        {
            Panel.Draw->AddText(Panel.Face, 10.0f, { Panel.Ink(), Panel.Pen + 8.0f }, FieldLabel, Line.Name);
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                const float X = Panel.Ink() + Label + float(Axis) * (Cell + 4.0f);
                Panel.Draw->AddRectFilled({ X, Panel.Pen }, { X + Cell, Panel.Pen + 26.0f }, EntryFill, 7.0f);
                Fixed(Figure, sizeof(Figure), Line.Values[Axis], Line.Decimals);
                Inked(Panel.Draw, Panel.Face, 11.0f, { X + Cell - 7.0f, Panel.Pen + 17.0f }, AxisText, Figure,
                      Anchor::Right);
                if (EditorProperty* Bound = Bind(Sheet, Line.Name); Bound && Bound->Editable)
                {
                    ImGui::PushID(Line.Name);
                    ImGui::SetCursorScreenPos({ X, Panel.Pen });
                    ImGui::InvisibleButton(Axis == 0 ? "x" : Axis == 1 ? "y" : "z", { Cell, 26.0f });
                    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                    {
                        Bound->Axes[Axis] = ImClamp(Bound->Axes[Axis] + ImGui::GetIO().MouseDelta.x * Line.Step,
                                                    Line.Floor, Line.Ceiling);
                    }
                    ImGui::PopID();
                }
            }
            Inked(Panel.Draw, Panel.Face, 9.0f, { Panel.Ink() + Panel.Inner(), Panel.Pen + 17.0f }, FieldLabel,
                  Line.Unit, Anchor::Right);
            Panel.Pen += 26.0f + 6.0f;
        }
        Panel.Pen += CardPadY - 6.0f;
    }
    Panel.End();

    // Nothing above moved the ImGui cursor, so claim the column's measured height in one go.
    ImGui::SetCursorScreenPos(Origin);
    ImGui::Dummy({ Panel.Wide, Panel.Pen - StackGap - Origin.y });
}

}   // namespace Frontier
