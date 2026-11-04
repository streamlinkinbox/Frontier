/*==========================================================================================================================================
                                                            MATERIALSECTION.CPP
==========================================================================================================================================*/
// 🧩 The geometry inspector's material card (the Project Zero editor's compact MaterialPanel): the families the object's
//    copies use, one surface slot at a time, with their channel values and the controls that give the object surfaces of
//    its own (copy on write: the first edit copies the family into the object's material block).

#include "Engine/Interface/Inspectors/Geometry/GeometrySections.h"

#include "Engine/Interface/Controls/ActionRow.h"
#include "Engine/Interface/Controls/ChannelRows.h"
#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/Controls/SliderRow.h"
#include "Engine/Interface/Controls/TintRow.h"
#include "Engine/Interface/InspectorControls.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace Frontier
{

namespace
{

// 📝 .material-compact: padding 21 px, the 130° #252525 → #1c1c1c wash over 1 px #ffffff09, radius 24 px.
CardLook ResolveMaterialCardLook()
{
    CardLook Look;
    Look.Fill            = ResolveCssColour(0x252525FF);
    Look.FillEnd         = ResolveCssColour(0x1C1C1CFF);
    Look.GradientDegrees = 130.0f;
    Look.GradientStop    = 1.0f;
    Look.Border          = ResolveCssColour(0xFFFFFF09);
    Look.Radius          = 24.0f;
    Look.PaddingTop      = 21.0f;
    Look.PaddingSide     = 21.0f;
    Look.PaddingBottom   = 21.0f;
    Look.MarginBottom    = 16.0f;
    return Look;
}

// 📝 Linear to the display byte of a swatch the way TintRow shows its colour (the demo's channel^(1/2.2)), so the channel
//    list and the colour control below it name the same hex.
uint32_t EncodeDisplayByte(float Linear)
{
    return static_cast<uint32_t>(std::lround(std::pow(std::clamp(Linear, 0.0f, 1.0f), 1.0f / 2.2f) * 255.0f));
}

uint32_t EncodeSwatch(float Red, float Green, float Blue)
{
    return (EncodeDisplayByte(Red) << 24) | (EncodeDisplayByte(Green) << 16) | (EncodeDisplayByte(Blue) << 8) | 0xFFu;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    MATERIAL
//------------------------------------------------------------------------------------------------------------------------

bool GeometryMaterialSection::Construct(ControlColumn& Page, GeometryConfiguration& Parameters, const std::string& RecordName, int& Slot)
{
    const auto&            Resolved = Parameters.Resolved;
    const EditorTypefaces& Faces    = Page.QueryFaces();
    const float            S        = Page.QueryScale();
    if (!Resolved.Bound || Resolved.Families.empty()) return false;
    Slot = std::clamp(Slot, 0, static_cast<int>(Resolved.Families.size()) - 1);
    const size_t                 Chosen   = static_cast<size_t>(Slot);
    const uint32_t               Family   = Resolved.Families[Chosen];
    const std::array<float, 12>& Channels = Resolved.FamilyChannels[Chosen];
    Page.BeginCard(ResolveMaterialCardLook(), nullptr, nullptr);

    // ① the heading: MATERIAL / SURFACE SLOT n, the family, what it is bound to
    char Text[256];
    std::snprintf(Text, sizeof(Text), "MATERIAL / SURFACE SLOT %d", Slot);
    const ImVec2 Eyebrow = Page.Space(12.0f, 0.0f, 4.0f);
    (void)GlyphRun::Construct(Page, Faces.Regular, 8.0f * S, ImVec2(Eyebrow.x, GlyphRun::ResolveBaseline(Eyebrow.y, 8.0f * S, 1.5f)), ResolveCssColour(0x777777FF),
                              Text, 1.0f * S);
    const ImVec2 Title = Page.Space(22.0f * 1.3f, 4.0f, 6.0f);
    (void)GlyphRun::Construct(Page, Faces.Light, 22.0f * S, ImVec2(Title.x, GlyphRun::ResolveBaseline(Title.y, 22.0f * S, 1.3f)), ResolveCssColour(0xEEEEEEFF),
                              Resolved.FamilyNames[Chosen].c_str(), -0.6f * S);
    const ImVec2 Bound = Page.Space(16.0f, 0.0f, 14.0f);
    float        Pen   = Bound.x;
    const float  Base  = GlyphRun::ResolveBaseline(Bound.y, 10.0f * S, 1.6f);
    Pen += GlyphRun::Construct(Page, Faces.Regular, 10.0f * S, ImVec2(Pen, Base), ResolveCssColour(0x747474FF), "Bound to ");
    Pen += GlyphRun::Construct(Page, Faces.Regular, 10.0f * S, ImVec2(Pen, Base), ResolveCssColour(0xC8B59DFF), RecordName.c_str());
    std::snprintf(Text, sizeof(Text), " \xC2\xB7 family #%u", Family);
    (void)GlyphRun::Construct(Page, Faces.Regular, 10.0f * S, ImVec2(Pen, Base), ResolveCssColour(0x747474FF), Text);

    // ② the slots, when the copies use more than one family
    if (Resolved.Families.size() > 1)
    {
        std::vector<std::string> Labels;
        std::vector<const char*> Pointers;
        for (size_t Index = 0; Index < std::min<size_t>(Resolved.Families.size(), 4); ++Index) Labels.push_back(Resolved.FamilyNames[Index]);
        for (const std::string& Label : Labels) Pointers.push_back(Label.c_str());
        int Selected = Slot < static_cast<int>(Pointers.size()) ? Slot : -1;
        if (TabStrip::Construct(Page, "MaterialSlots", Pointers.data(), static_cast<int>(Pointers.size()), Selected) && Selected >= 0) Slot = Selected;
    }

    // ③ the figures and the channels
    const uint32_t Users = Chosen < Resolved.FamilyUsers.size() ? Resolved.FamilyUsers[Chosen] : 1u;
    char           UsersText[16], SlotsText[16];
    std::snprintf(UsersText, sizeof(UsersText), "%u", Users);
    std::snprintf(SlotsText, sizeof(SlotsText), "%zu", Resolved.Families.size());
    const MetricTile Figures[2] = {{"Surface slots", SlotsText, ""}, {"Objects sharing", UsersText, ""}};
    MetricTiles::Construct(Page, Figures, 2, MetricTiles::NoLead);

    // 📝 MaterialSpecification: albedo rgb, roughness, emission rgb, metalness, coat weight, coat roughness, specular level, transmission.
    char BaseColour[16], Metallic[16], Roughness[16], Reflectance[16], Emission[40], Coat[24], Transmission[16];
    const uint32_t BaseSwatch = EncodeSwatch(Channels[0], Channels[1], Channels[2]);
    std::snprintf(BaseColour, sizeof(BaseColour), "#%06X", BaseSwatch >> 8);
    std::snprintf(Roughness, sizeof(Roughness), "%.2f", Channels[3]);
    std::snprintf(Metallic, sizeof(Metallic), "%.2f", Channels[7]);
    std::snprintf(Coat, sizeof(Coat), "%.2f \xC2\xB7 %.2f", Channels[8], Channels[9]);
    std::snprintf(Reflectance, sizeof(Reflectance), "%.3f", Channels[10]);
    std::snprintf(Transmission, sizeof(Transmission), "%.2f", Channels[11]);
    const float    Strength       = std::max({Channels[4], Channels[5], Channels[6]});
    const uint32_t EmissionSwatch = Strength > 0.0f ? EncodeSwatch(Channels[4] / Strength, Channels[5] / Strength, Channels[6] / Strength) : 0u;
    if (Strength > 0.0f) std::snprintf(Emission, sizeof(Emission), "#%06X \xC3\x97 %.2f", EmissionSwatch >> 8, Strength);
    else std::snprintf(Emission, sizeof(Emission), "none");
    const ChannelRow Rows[7] = {
        {"Base colour", "Surface", BaseColour, "linear \xE2\x86\x92 sRGB", BaseSwatch},
        {"Metallic", "Surface", Metallic, "constant", 0u},
        {"Roughness", "Surface", Roughness, "constant", 0u},
        {"Reflectance", "Specular level", Reflectance, "constant", 0u},
        {"Emission", "Incandescence", Emission, Strength > 0.0f ? "linear" : "constant", EmissionSwatch},
        {"Clear coat", "Weight \xC2\xB7 roughness", Coat, "constant", 0u},
        {"Transmission", "Surface", Transmission, "constant", 0u},
    };
    (void)ChannelRows::Construct(Page, "Channels", Rows, 7, 0);

    // ④ the controls: the first edit copies the family into the object's surfaces (its material block), Revert gives it back
    auto Owned = std::find_if(Parameters.SurfaceSlots.begin(), Parameters.SurfaceSlots.end(),
                              [Family](const SurfaceSlotConfiguration& Entry) { return Entry.Family == Family; });
    const bool            Owns    = Owned != Parameters.SurfaceSlots.end();
    std::array<float, 12> Edited  = Owns ? Owned->Channels : Channels;
    bool                  Changed = false;
    const bool            Enabled = !Parameters.Locked;
    ImGui::PushID("MaterialControls");
    float Albedo[3] = {Edited[0], Edited[1], Edited[2]};
    if (TintRow::Construct(Page, "BaseColour", "Base colour", Albedo, Enabled, "The surface's albedo (linear)"))
    {
        for (int Channel = 0; Channel < 3; ++Channel) Edited[static_cast<size_t>(Channel)] = Albedo[Channel];
        Changed = true;
    }
    Changed |= SliderRow::Construct(Page, "Roughness", "Roughness", Edited[3], 0.02f, 1.0f, 0.01f, "", 1.0f, Enabled);
    Changed |= SliderRow::Construct(Page, "Metallic", "Metallic", Edited[7], 0.0f, 1.0f, 0.01f, "", 1.0f, Enabled);
    Changed |= SliderRow::Construct(Page, "Reflectance", "Reflectance", Edited[10], 0.0f, 1.0f, 0.005f, "", 1.0f, Enabled,
                                    "Specular level: the dielectric reflectance at normal incidence (F0); 0.04 is 4 %, most non-metals");
    Changed |= SliderRow::Construct(Page, "Coat", "Clear coat", Edited[8], 0.0f, 1.0f, 0.01f, "", 1.0f, Enabled);
    Changed |= SliderRow::Construct(Page, "CoatRoughness", "Coat roughness", Edited[9], 0.0f, 1.0f, 0.01f, "", 1.0f, Enabled);
    Changed |= SliderRow::Construct(Page, "Transmission", "Transmission", Edited[11], 0.0f, 1.0f, 0.01f, "", 1.0f, Enabled);
    const float Glow    = std::max({Edited[4], Edited[5], Edited[6]});
    float       Hue[3]  = {Glow > 0.0f ? Edited[4] / Glow : 1.0f, Glow > 0.0f ? Edited[5] / Glow : 1.0f, Glow > 0.0f ? Edited[6] / Glow : 1.0f};
    float       Lumens  = Glow;
    const bool  Tinted  = TintRow::Construct(Page, "EmissionColour", "Emission colour", Hue, Enabled, "The incandescence's colour");
    const bool  Brought = SliderRow::Construct(Page, "EmissionStrength", "Emission", Lumens, 0.0f, 40.0f, 0.05f, "", 1.0f, Enabled,
                                               "Incandescence strength (linear radiance scale)");
    if (Tinted || Brought)
    {
        for (int Channel = 0; Channel < 3; ++Channel) Edited[static_cast<size_t>(4 + Channel)] = Hue[Channel] * Lumens;
        Changed = true;
    }
    if (Changed)
    {
        if (Owns) Owned->Channels = Edited;
        else Parameters.SurfaceSlots.push_back({Family, Edited});
    }
    if (Owns && !Changed)
    {
        const ActionEntry Revert[1] = {{"RevertSurface", "Revert to the shared family", Enabled, "Drop this object's copy of the family"}};
        ActionRowLook     Look;
        Look.MarginTop    = 12.0f;
        Look.MarginBottom = 12.0f;
        if (ActionRow::Construct(Page, Revert, Look) == 0)
        {
            Parameters.SurfaceSlots.erase(Owned);
            Changed = true;
        }
    }
    ImGui::PopID();
    // 📝 An engine shape's family comes from its archive's [surface] (EngineContent/ShapeArchives/<Shape>.toml), the others from
    //    the scene archive's material table.
    const uint32_t    Others     = Users > 0 ? Users - 1 : 0u;
    const std::string FamilyName = Resolved.FamilyNames[Chosen];
    const std::string Origin     = Resolved.Shape ? "the engine shape's" : "the scene archive's";
    const std::string Source     = Resolved.Shape ? "From the engine shape's surface (ShapeArchives/" + Parameters.ShapeName + ".toml)"
                                                  : std::string("From the scene archive's material table (Materials.bin)");
    if (Owns && Others == 0)
        std::snprintf(Text, sizeof(Text), "This object owns its copy of %s (a material block of its own); no other object uses the family.",
                      FamilyName.c_str());
    else if (Owns)
        std::snprintf(Text, sizeof(Text), "This object owns its copy of %s (a material block of its own); the %u other object%s sharing the family keep%s "
                                          "%s values.",
                      FamilyName.c_str(), Others, Others == 1 ? "" : "s", Others == 1 ? "s" : "", Origin.c_str());
    else
        std::snprintf(Text, sizeof(Text), "%s, shared by %u object%s. The first edit copies %s into this object's own material block%s", Source.c_str(), Users,
                      Users == 1 ? "" : "s", FamilyName.c_str(), Others > 0 ? "; the others keep theirs." : ".");
    CaptionText::Construct(Page, Text, 0.0f, 0.0f);
    Page.EndCard();
    return Changed;
}

} // namespace Frontier
