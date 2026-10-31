// 🧩 StackRail chrome, FolderRow, TreeRow, IdentityHeader: the kit set the SceneDirectory consumes (interface SDK only).

#include "Engine/Interface/StackRailControls.h"

#if defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/ControlKit.h"
#include "Engine/Interface/TypefaceRegistry.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{

constexpr ImU32 KitQuad(ColorQuad Quad) noexcept
{
    return KitInkRgba(std::clamp(static_cast<int>(Quad.Red * 255.0f + 0.5f), 0, 255),
                      std::clamp(static_cast<int>(Quad.Green * 255.0f + 0.5f), 0, 255),
                      std::clamp(static_cast<int>(Quad.Blue * 255.0f + 0.5f), 0, 255),
                      std::clamp(static_cast<int>(Quad.Alpha * 255.0f + 0.5f), 0, 255));
}

} // namespace

KitTheme ResolveKitTheme() noexcept
{
    const ControlKitPalette& K        = ControlKit::Palette();
    const auto               Contrast = [&](float Alpha) -> ImU32 {
        const int Byte = std::clamp(static_cast<int>(Alpha * 255.0f + 0.5f), 0, 255);
        return K.LightSurface ? KitInkRgba(15, 23, 42, Byte) : KitInkRgba(255, 255, 255, Byte);
    };

    KitTheme Theme;
    Theme.PanelBg     = KitQuad(K.Panel);
    Theme.PanelEdge   = KitQuad(K.Stroke);
    Theme.Surface     = KitQuad(K.Card);
    Theme.Surface2    = KitQuad(K.Inset);
    Theme.Surface3    = KitQuad(K.Raised);
    Theme.Line        = KitQuad(K.Divider);
    Theme.LineSoft    = Contrast(0.045f);
    Theme.RowHover    = Contrast(0.036f);
    Theme.Text        = KitQuad(K.Text);
    Theme.Muted       = KitQuad(K.TextDim);
    Theme.Faint       = KitQuad(K.TextFaint);
    Theme.ChipOnBg    = KitQuad(K.Primary);
    Theme.ChipOnInk   = KitQuad(K.PrimaryInk);
    Theme.LightCanvas = K.LightSurface;
    if (K.LightSurface)
    {
        Theme.Mint    = KitInkHex(0x0b9e6f);
        Theme.MintDim = KitInkRgba(11, 158, 111, 28);
    }
    return Theme;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

KitFaces ResolveKitFaces() noexcept
{
    KitFaces Faces;
    Faces.Light = Faces.Regular = Faces.Medium = Faces.SemiBold = ImGui::GetFont();
    TypefaceRegistry* Registry = TypefaceRegistry::QueryCurrent();
    if (Registry == nullptr)
    {
        return Faces;
    }
    const int32_t Family = Registry->ResolveFamilyIndex("DM Sans");
    if (Family < 0)
    {
        return Faces;
    }
    const auto Grab = [Registry, Family](FontWeightCategory Weight) -> ImFont* {
        const TypefaceFace* Face = Registry->Resolve(static_cast<uint32_t>(Family), Weight);
        return Face != nullptr ? static_cast<ImFont*>(Face->Handle) : nullptr;
    };
    if (ImFont* Face = Grab(FontWeightCategory::Light))    Faces.Light    = Face;
    if (ImFont* Face = Grab(FontWeightCategory::Regular))  Faces.Regular  = Face;
    if (ImFont* Face = Grab(FontWeightCategory::Medium))   Faces.Medium   = Face;
    if (ImFont* Face = Grab(FontWeightCategory::SemiBold)) Faces.SemiBold = Face;
    return Faces;
}

float KitTextWidth(ImFont* Font, float Size, const char* Begin, const char* End) noexcept
{
    return Font->CalcTextSizeA(Size, 3.402823466e+38f, 0.0f, Begin, End).x;
}

void KitDrawText(ImDrawList* Ink, ImFont* Font, float Size, ImVec2 Position, ImU32 Colour, const char* Begin,
                 const char* End)
{
    Ink->AddText(Font, Size, Position, Colour, Begin, End);
}

float KitDrawTracked(ImDrawList* Ink, ImFont* Font, float Size, ImVec2 Position, ImU32 Colour, const char* Text,
                     float Tracking)
{
    float X = Position.x;
    for (const char* Cursor = Text; *Cursor != '\0'; ++Cursor)
    {
        Ink->AddText(Font, Size, ImVec2(X, Position.y), Colour, Cursor, Cursor + 1);
        X += KitTextWidth(Font, Size, Cursor, Cursor + 1) + Tracking;
    }
    return X - Position.x - (Text[0] != '\0' ? Tracking : 0.0f);
}

bool KitTarget(const char* Id, ImVec2 Minimum, ImVec2 Size, bool& Hovered, const char* Tooltip)
{
    ImGui::SetCursorScreenPos(Minimum);
    const bool Clicked = ImGui::InvisibleButton(Id, Size);
    Hovered            = ImGui::IsItemHovered();
    if (Hovered && Tooltip != nullptr)
    {
        ImGui::SetTooltip("%s", Tooltip);
    }
    return Clicked;
}

void KitPillSurface(ImDrawList* Ink, ImVec2 Minimum, ImVec2 Maximum, ImU32 Background, ImU32 Border, float Radius)
{
    if ((Background >> 24) != 0)
    {
        Ink->AddRectFilled(Minimum, Maximum, Background, Radius);
    }
    if ((Border >> 24) != 0)
    {
        Ink->AddRect(Minimum, Maximum, Border, Radius);
    }
}

void DrawFolderTabGlyph(ImDrawList* Ink, ImVec2 Centre, float Scale, ImU32 Accent, bool Open)
{
    const float HalfW = 8.5f * Scale;
    const float HalfH = 6.2f * Scale;
    const ImVec2 BackMin(Centre.x - HalfW, Centre.y - HalfH + 1.5f * Scale);
    const ImVec2 BackMax(Centre.x + HalfW, Centre.y + HalfH);

    // Raised upper tab (inspired by SourceImages/Folder UI editorial folder silhouette).
    const ImVec2 TabMin(Centre.x + 0.5f * Scale, Centre.y - HalfH - 1.6f * Scale);
    const ImVec2 TabMax(Centre.x + HalfW, Centre.y - HalfH + 3.5f * Scale);
    Ink->AddRectFilled(TabMin, TabMax, KitInkWithAlpha(Accent, 0.34f), 2.2f * Scale);
    Ink->AddRect(TabMin, TabMax, KitInkWithAlpha(Accent, 0.72f), 2.2f * Scale, 0, 1.15f);

    // Rear folder card.
    Ink->AddRectFilled(BackMin, BackMax, KitInkWithAlpha(Accent, 0.18f), 2.6f * Scale);
    Ink->AddRect(BackMin, BackMax, KitInkWithAlpha(Accent, 0.65f), 2.6f * Scale, 0, 1.15f);

    // Front flap (dropped slightly when open to show depth).
    const float FlapTop = Centre.y - (Open ? 0.8f : 2.8f) * Scale;
    Ink->AddRectFilled(ImVec2(BackMin.x, FlapTop), BackMax, KitInkWithAlpha(Accent, Open ? 0.36f : 0.28f),
                       2.4f * Scale);
    Ink->AddLine(ImVec2(BackMin.x + 1.5f * Scale, FlapTop), ImVec2(BackMax.x - 1.5f * Scale, FlapTop),
                 KitInkWithAlpha(Accent, 0.92f), 1.25f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONTROLS
//------------------------------------------------------------------------------------------------------------------------

StackRailHeadInteraction DrawOutlinerHead(const KitFaces& Faces, ImVec2 Minimum, float Width, const char* Title,
                                          const char* CountText)
{
    const KitTheme           Theme   = ResolveKitTheme();
    ImDrawList*              Ink     = ImGui::GetWindowDrawList();
    const float              CentreY = Minimum.y + 18.0f;
    const float              Right   = Minimum.x + Width;
    StackRailHeadInteraction Interaction{};

    // Stack glyph tile.
    Ink->AddRectFilled(ImVec2(Minimum.x, CentreY - 15.0f), ImVec2(Minimum.x + 30.0f, CentreY + 15.0f),
                       Theme.Surface2, 9.0f);
    Ink->AddRect(ImVec2(Minimum.x, CentreY - 15.0f), ImVec2(Minimum.x + 30.0f, CentreY + 15.0f), Theme.Line, 9.0f);
    for (int Bar = 0; Bar < 3; ++Bar)
    {
        const float Y = CentreY - 7.0f + static_cast<float>(Bar) * 6.0f;
        Ink->AddRectFilled(ImVec2(Minimum.x + 8.0f, Y), ImVec2(Minimum.x + 22.0f, Y + 2.6f), Theme.Mint, 1.3f);
    }

    const float TitleX = Minimum.x + 30.0f + 12.0f;
    KitDrawText(Ink, Faces.Medium, 20.0f, ImVec2(TitleX, CentreY - 10.0f), Theme.Text, Title);

    // HeadCount pill.
    const float CountW = KitTextWidth(Faces.Regular, 12.0f, CountText);
    const float PillW  = 12.0f + 7.0f + 7.0f + CountW + 12.0f;
    const float PillX  = TitleX + KitTextWidth(Faces.Medium, 20.0f, Title) + 10.0f;
    if (PillX + PillW < Right - 104.0f)
    {
        KitPillSurface(Ink, ImVec2(PillX, CentreY - 12.0f), ImVec2(PillX + PillW, CentreY + 12.0f), Theme.Surface2,
                       Theme.Line, 999.0f);
        Ink->AddCircleFilled(ImVec2(PillX + 15.5f, CentreY), 3.2f, Theme.Mint);
        KitDrawText(Ink, Faces.Regular, 12.0f, ImVec2(PillX + 26.0f, CentreY - 6.0f), Theme.Muted, CountText);
    }

    // "+ Folder" button.
    const float FolderW = 68.0f;
    const float FolderX = Right - 30.0f - 6.0f - FolderW;
    bool        FolderHovered = false;
    Interaction.FolderClicked = KitTarget("##StackRailNewFolder", ImVec2(FolderX, CentreY - 15.0f),
                                          ImVec2(FolderW, 30.0f), FolderHovered, "Create a new Outliner folder");
    KitPillSurface(Ink, ImVec2(FolderX, CentreY - 15.0f), ImVec2(FolderX + FolderW, CentreY + 15.0f),
                   FolderHovered ? Theme.Surface3 : Theme.Surface2, Theme.Line, 9.0f);
    DrawFolderTabGlyph(Ink, ImVec2(FolderX + 15.0f, CentreY + 0.5f), 0.72f, Theme.Mint, true);
    KitDrawText(Ink, Faces.Medium, 11.5f, ImVec2(FolderX + 27.0f, CentreY - 5.75f),
                FolderHovered ? Theme.Text : Theme.Muted, "Folder");

    // AddHost button.
    bool AddHovered        = false;
    Interaction.AddClicked = KitTarget("##StackRailAdd", ImVec2(Right - 30.0f, CentreY - 15.0f), ImVec2(30.0f, 30.0f),
                                       AddHovered, "Add scene record");
    KitPillSurface(Ink, ImVec2(Right - 30.0f, CentreY - 15.0f), ImVec2(Right, CentreY + 15.0f),
                   AddHovered ? Theme.Surface3 : Theme.Surface2, Theme.Line, 9.0f);
    const ImU32 PlusColour = AddHovered ? Theme.Text : Theme.Muted;
    Ink->AddRectFilled(ImVec2(Right - 15.0f - 6.0f, CentreY - 1.3f), ImVec2(Right - 15.0f + 6.0f, CentreY + 1.3f),
                       PlusColour, 1.3f);
    Ink->AddRectFilled(ImVec2(Right - 15.0f - 1.3f, CentreY - 6.0f), ImVec2(Right - 15.0f + 1.3f, CentreY + 6.0f),
                       PlusColour, 1.3f);
    return Interaction;
}

bool DrawStackRailHead(const KitFaces& Faces, ImVec2 Minimum, float Width, const char* Title,
                       const char* CountText)
{
    return DrawOutlinerHead(Faces, Minimum, Width, Title, CountText).AddClicked;
}

void DrawStackRailFoot(const KitFaces& Faces, ImVec2 Minimum, float Width, const char* LeftText,
                       const char* RightText)
{
    const KitTheme Theme = ResolveKitTheme();
    ImDrawList*    Ink   = ImGui::GetWindowDrawList();
    Ink->AddRectFilled(Minimum, ImVec2(Minimum.x + Width, Minimum.y + 1.0f), Theme.LineSoft);
    KitDrawText(Ink, Faces.Regular, 11.5f, ImVec2(Minimum.x, Minimum.y + 8.0f), Theme.Faint, LeftText);
    const float RightW = KitTextWidth(Faces.Regular, 11.5f, RightText);
    KitDrawText(Ink, Faces.Regular, 11.5f, ImVec2(Minimum.x + Width - RightW, Minimum.y + 8.0f), Theme.Faint,
                RightText);
}

void DrawIdentityHeader(const KitFaces& Faces, ImVec2 Minimum, float Width, ImU32 IconColour, const char* Name,
                        const char* Sub)
{
    const KitTheme Theme = ResolveKitTheme();
    ImDrawList*    Ink   = ImGui::GetWindowDrawList();
    KitPillSurface(Ink, Minimum, ImVec2(Minimum.x + Width, Minimum.y + 56.0f), Theme.Surface, Theme.LineSoft,
                   12.0f);
    const ImVec2 DiscCentre(Minimum.x + 28.0f, Minimum.y + 28.0f);
    Ink->AddCircleFilled(DiscCentre, 14.0f, KitInkWithAlpha(IconColour, 0.16f));
    Ink->AddCircle(DiscCentre, 14.0f, KitInkWithAlpha(IconColour, 0.45f));
    Ink->AddCircleFilled(DiscCentre, 4.5f, IconColour);
    KitDrawText(Ink, Faces.Medium, 14.5f, ImVec2(Minimum.x + 54.0f, Minimum.y + 10.0f), Theme.Text, Name);
    KitDrawText(Ink, Faces.Regular, 11.5f, ImVec2(Minimum.x + 54.0f, Minimum.y + 32.0f), Theme.Muted, Sub);
}

TreeRowInteraction DrawTreeRow(const KitFaces& Faces, const char* Id, ImVec2 Minimum, float Width, float Indent,
                               bool HasEnclosed, bool Collapsed, ImU32 DotColour, const char* Name,
                               const char* Meta, bool Visible, bool Selected)
{
    const KitTheme     Theme   = ResolveKitTheme();
    ImDrawList*        Ink     = ImGui::GetWindowDrawList();
    const float        Height  = 30.0f;
    const float        CentreY = Minimum.y + Height * 0.5f;
    const ImVec2       Maximum(Minimum.x + Width, Minimum.y + Height);
    TreeRowInteraction Result;

    ImGui::PushID(Id);
    bool BodyHovered = false;
    Result.Clicked   = KitTarget("##Row", Minimum, ImVec2(Width, Height), BodyHovered);

    if (Selected)
    {
        Ink->AddRectFilled(Minimum, Maximum, Theme.MintDim, 8.0f);
        Ink->AddRectFilled(ImVec2(Minimum.x, Minimum.y + 5.0f), ImVec2(Minimum.x + 2.5f, Maximum.y - 5.0f),
                           Theme.Mint, 1.2f);
    }
    else if (BodyHovered)
    {
        Ink->AddRectFilled(Minimum, Maximum, Theme.RowHover, 8.0f);
    }

    // Subtle vertical hierarchy rail for nested rows.
    if (Indent >= 12.0f)
    {
        const float GuideX = Minimum.x + 14.0f + Indent - 14.0f;
        Ink->AddLine(ImVec2(GuideX, Minimum.y + 1.0f), ImVec2(GuideX, Maximum.y - 1.0f), Theme.Line, 1.0f);
        Ink->AddLine(ImVec2(GuideX, CentreY), ImVec2(GuideX + 7.0f, CentreY), Theme.Line, 1.0f);
    }

    float CursorX = Minimum.x + 10.0f + Indent;

    if (HasEnclosed)
    {
        bool ChevronHovered = false;
        Result.ChevronClicked =
            KitTarget("##Fold", ImVec2(CursorX - 3.0f, CentreY - 9.0f), ImVec2(18.0f, 18.0f), ChevronHovered,
                      Collapsed ? "Expand" : "Collapse");
        const ImU32  ChevronColour = ChevronHovered ? Theme.Text : Theme.Faint;
        const ImVec2 C(CursorX + 6.0f, CentreY);
        if (Collapsed)
        {
            Ink->AddTriangleFilled(ImVec2(C.x - 3.0f, C.y - 5.0f), ImVec2(C.x - 3.0f, C.y + 5.0f),
                                   ImVec2(C.x + 4.0f, C.y), ChevronColour);
        }
        else
        {
            Ink->AddTriangleFilled(ImVec2(C.x - 5.0f, C.y - 3.0f), ImVec2(C.x + 5.0f, C.y - 3.0f),
                                   ImVec2(C.x, C.y + 4.0f), ChevronColour);
        }
    }
    CursorX += 18.0f;

    Ink->AddCircleFilled(ImVec2(CursorX + 4.0f, CentreY), 3.5f,
                         Visible ? DotColour : KitInkWithAlpha(DotColour, 0.35f));
    CursorX += 16.0f;
    const ImU32 NameColour = !Visible ? Theme.Faint : (Selected ? Theme.Text : Theme.Muted);
    KitDrawText(Ink, Selected ? Faces.Medium : Faces.Regular, 13.0f, ImVec2(CursorX, CentreY - 6.5f), NameColour,
                Name);

    const float EyeX       = Maximum.x - 8.0f - 24.0f;
    bool        EyeHovered = false;
    Result.EyeClicked      = KitTarget("##Eye", ImVec2(EyeX, CentreY - 12.0f), ImVec2(24.0f, 24.0f), EyeHovered,
                                       Visible ? "Hide in viewport" : "Show in viewport");
    const ImU32  EyeColour = EyeHovered ? Theme.Text : (Visible ? Theme.Muted : Theme.Faint);
    const ImVec2 EyeCentre(EyeX + 12.0f, CentreY);
    Ink->AddCircle(EyeCentre, 6.5f, EyeColour, 24, 1.3f);
    Ink->AddCircleFilled(EyeCentre, 2.2f, Visible ? EyeColour : KitInkWithAlpha(EyeColour, 0.0f));
    if (!Visible)
    {
        Ink->AddLine(ImVec2(EyeCentre.x - 7.0f, EyeCentre.y + 6.0f), ImVec2(EyeCentre.x + 7.0f, EyeCentre.y - 6.0f),
                     EyeColour, 1.4f);
    }

    const float MetaW  = KitTextWidth(Faces.Regular, 11.0f, Meta);
    const float NameEnd = CursorX + KitTextWidth(Selected ? Faces.Medium : Faces.Regular, 13.0f, Name) + 10.0f;
    if (EyeX - 10.0f - MetaW > NameEnd)
    {
        KitDrawText(Ink, Faces.Regular, 11.0f, ImVec2(EyeX - 10.0f - MetaW, CentreY - 5.5f), Theme.Faint, Meta);
    }

    ImGui::PopID();
    return Result;
}

TreeRowInteraction DrawFolderRow(const KitFaces& Faces, const char* Id, ImVec2 Minimum, float Width, float Indent,
                                 bool Collapsed, ImU32 AccentColour, const char* Name, const char* TagText,
                                 uint32_t EnclosedCount, bool Visible, bool Selected)
{
    const KitTheme     Theme   = ResolveKitTheme();
    ImDrawList*        Ink     = ImGui::GetWindowDrawList();
    const float        Height  = 34.0f;
    const float        CentreY = Minimum.y + Height * 0.5f;
    const ImVec2       Maximum(Minimum.x + Width, Minimum.y + Height);
    TreeRowInteraction Result;

    ImGui::PushID(Id);
    bool BodyHovered = false;
    Result.Clicked   = KitTarget("##FolderBody", Minimum, ImVec2(Width, Height), BodyHovered);

    const ImU32 CardBg = Selected ? Theme.MintDim : (BodyHovered ? Theme.Surface3 : Theme.Surface2);
    const ImU32 CardEdge = Selected ? KitInkWithAlpha(Theme.Mint, 0.55f) : Theme.LineSoft;
    KitPillSurface(Ink, ImVec2(Minimum.x + Indent, Minimum.y + 1.5f), ImVec2(Maximum.x, Maximum.y - 1.5f), CardBg,
                   CardEdge, 8.5f);
    Ink->AddRectFilled(ImVec2(Minimum.x + Indent, Minimum.y + 6.0f),
                       ImVec2(Minimum.x + Indent + 3.0f, Maximum.y - 6.0f),
                       Visible ? AccentColour : KitInkWithAlpha(AccentColour, 0.35f), 1.5f);

    float CursorX = Minimum.x + Indent + 10.0f;

    // Fold chevron (always active on folders).
    bool ChevronHovered = false;
    Result.ChevronClicked =
        KitTarget("##FolderFold", ImVec2(CursorX - 2.0f, CentreY - 10.0f), ImVec2(20.0f, 20.0f), ChevronHovered,
                  Collapsed ? "Expand folder" : "Collapse folder");
    const ImU32  ChevronColour = ChevronHovered ? Theme.Text : Theme.Muted;
    const ImVec2 C(CursorX + 7.0f, CentreY);
    if (Collapsed)
    {
        Ink->AddTriangleFilled(ImVec2(C.x - 3.0f, C.y - 5.0f), ImVec2(C.x - 3.0f, C.y + 5.0f),
                               ImVec2(C.x + 4.5f, C.y), ChevronColour);
    }
    else
    {
        Ink->AddTriangleFilled(ImVec2(C.x - 5.0f, C.y - 3.0f), ImVec2(C.x + 5.0f, C.y - 3.0f),
                               ImVec2(C.x, C.y + 4.5f), ChevronColour);
    }
    CursorX += 20.0f;

    // Tabbed folder silhouette glyph.
    DrawFolderTabGlyph(Ink, ImVec2(CursorX + 9.0f, CentreY + 0.5f), 0.92f,
                       Visible ? AccentColour : KitInkWithAlpha(AccentColour, 0.4f), !Collapsed);
    CursorX += 24.0f;

    const ImU32 TitleColour = !Visible ? Theme.Faint : Theme.Text;
    KitDrawText(Ink, Faces.SemiBold, 13.0f, ImVec2(CursorX, CentreY - 6.5f), TitleColour, Name);
    const float NameW = KitTextWidth(Faces.SemiBold, 13.0f, Name);

    // Cascade visibility eye at right + enclosed count pill before it.
    const float EyeX       = Maximum.x - 8.0f - 24.0f;
    bool        EyeHovered = false;
    Result.EyeClicked      = KitTarget("##FolderEye", ImVec2(EyeX, CentreY - 12.0f), ImVec2(24.0f, 24.0f), EyeHovered,
                                       Visible ? "Hide folder & enclosed items" : "Show folder & enclosed items");
    const ImU32  EyeColour = EyeHovered ? Theme.Text : (Visible ? Theme.Muted : Theme.Faint);
    const ImVec2 EyeCentre(EyeX + 12.0f, CentreY);
    Ink->AddCircle(EyeCentre, 6.5f, EyeColour, 24, 1.3f);
    Ink->AddCircleFilled(EyeCentre, 2.2f, Visible ? EyeColour : KitInkWithAlpha(EyeColour, 0.0f));
    if (!Visible)
    {
        Ink->AddLine(ImVec2(EyeCentre.x - 7.0f, EyeCentre.y + 6.0f), ImVec2(EyeCentre.x + 7.0f, EyeCentre.y - 6.0f),
                     EyeColour, 1.4f);
    }

    char CountBadge[24] = {};
    std::snprintf(CountBadge, sizeof(CountBadge), "%u", EnclosedCount);
    const float BadgeTextW = KitTextWidth(Faces.Medium, 11.0f, CountBadge);
    const float BadgeW     = std::max(22.0f, BadgeTextW + 12.0f);
    const float BadgeX     = EyeX - 6.0f - BadgeW;
    KitPillSurface(Ink, ImVec2(BadgeX, CentreY - 9.5f), ImVec2(BadgeX + BadgeW, CentreY + 9.5f), Theme.Surface,
                   Theme.Line, 999.0f);
    KitDrawText(Ink, Faces.Medium, 11.0f,
                ImVec2(BadgeX + (BadgeW - BadgeTextW) * 0.5f, CentreY - 5.5f), Theme.Muted, CountBadge);

    if (TagText != nullptr && TagText[0] != '\0')
    {
        const float TagW = KitTextWidth(Faces.Medium, 9.5f, TagText) + 1.2f * 6.0f;
        if (BadgeX - 10.0f - TagW > CursorX + NameW + 12.0f)
        {
            KitDrawTracked(Ink, Faces.Medium, 9.5f, ImVec2(BadgeX - 10.0f - TagW, CentreY - 4.75f), Theme.Faint,
                           TagText, 1.0f);
        }
    }

    ImGui::PopID();
    return Result;
}

} // namespace Frontier

#endif // FRONTIER_WITH_INTERFACE_SDK
