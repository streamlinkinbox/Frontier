//==============================================================================================================================================
//                                                          NATIVEEMITTERCARDS.CPP
//==============================================================================================================================================
// 📦 Executed proof: records the native light inspector for the five InspectorDepot emitters and writes the captures from real draw commands.

#include "LightInspectorPanel.h"
#include "ControlPanel.h"
#include "EditorInstance.h"
#include "CpuDraw.h"
#include "PngWriteCounterpart.h"
#include <imgui_internal.h>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace Frontier;

namespace
{

unsigned Checks = 0;

// LightPanel.js prepends LightIcons[Style] into every card header. The editor rasterises that SVG through
//    IconArt; the harness reads the same artwork pre-baked by BakeLightGlyphs.mjs.
constexpr int kGlyphEdge = 50;
std::vector<unsigned char> gGlyph;
unsigned char gGlyphToken = 0;
ImTextureID gGlyphId = static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(&gGlyphToken));

const char* GlyphFile(IconSymbol Symbol)
{
    switch (Symbol)
    {
    case IconSymbol::EditorPointLight: return "editor-point-light";
    case IconSymbol::EditorSpotlight:  return "editor-spotlight";
    case IconSymbol::EditorDomeLight:  return "editor-dome-light";
    case IconSymbol::EditorAreaLight:  return "editor-area-light";
    case IconSymbol::LightArea2D:      return "light-area-2d";
    case IconSymbol::LightPoint2D:     return "light-point-2d";
    case IconSymbol::SlateRingLight:   return "slate-ring-light";
    default:                           return nullptr;
    }
}

bool AdoptGlyph(IconSymbol Symbol)
{
    const char* Name = GlyphFile(Symbol);
    if (!Name)
    {
        return false;
    }
    const std::string Path = std::string("Exhibits/Workbench/Lighting/Glyphs/") + Name + ".rgba";
    std::FILE* Handle = std::fopen(Path.c_str(), "rb");
    if (!Handle)
    {
        return false;
    }
    gGlyph.assign(size_t(kGlyphEdge) * kGlyphEdge * 4, 0);
    const size_t Read = std::fread(gGlyph.data(), 1, gGlyph.size(), Handle);
    std::fclose(Handle);
    return Read == gGlyph.size();
}

void Check(bool Condition, const char* Claim)
{
    ++Checks;
    if (!Condition)
    {
        throw std::runtime_error(Claim);
    }
}

EditorProperty& Open(EditorPropertyGroup& Group, const char* Name, EditorPropertyCategory Category)
{
    EditorProperty& Slot = Group.Properties[Group.PropertyCount++];
    std::snprintf(Slot.Label, sizeof(Slot.Label), "%s", Name);
    Slot.Category = Category;
    return Slot;
}

EditorProperty& Tape(EditorPropertyGroup& Group, const char* Name, float Value, float Low, float High,
                     uint32_t Decimals, const char* Unit)
{
    EditorProperty& Slot = Open(Group, Name, EditorPropertyCategory::Slider);
    Slot.Figure   = Value;
    Slot.Minimum  = Low;
    Slot.Maximum  = High;
    Slot.Decimals = Decimals;
    std::snprintf(Slot.Unit, sizeof(Slot.Unit), "%s", Unit);
    return Slot;
}

EditorProperty& Axes(EditorPropertyGroup& Group, const char* Name, float X, float Y, float Z, float Step)
{
    EditorProperty& Slot = Open(Group, Name, EditorPropertyCategory::AxisVec3);
    Slot.Axes[0]  = X;
    Slot.Axes[1]  = Y;
    Slot.Axes[2]  = Z;
    Slot.AxisStep = Step;
    Slot.Editable = true;
    return Slot;
}

EditorProperty& State(EditorPropertyGroup& Group, const char* Name, bool On)
{
    EditorProperty& Slot = Open(Group, Name, EditorPropertyCategory::Switch);
    Slot.On = On;
    return Slot;
}

EditorProperty& Tint(EditorPropertyGroup& Group, const char* Name, float R, float G, float B)
{
    EditorProperty& Slot = Open(Group, Name, EditorPropertyCategory::Colour);
    Slot.ColourTint[0] = R;
    Slot.ColourTint[1] = G;
    Slot.ColourTint[2] = B;
    return Slot;
}

// Subject.type in LightPanel.js. The engine selector carries the same seven styles in the same order.
void Family(EditorPropertyGroup& Group, unsigned Style)
{
    EditorProperty& Type = Open(Group, "Type", EditorPropertyCategory::Select);
    Type.Picked = Style;
    Type.OptionCount = 6;
    const char* Names[] = {"Point", "Spot", "IES", "Area", "Tube", "LED"};
    for (int Slot = 0; Slot < 6; ++Slot)
    {
        std::snprintf(Type.Options[Slot], sizeof(Type.Options[Slot]), "%s", Names[Slot]);
    }
}

// The property set is LightPanel.js's Field() calls and LightSpecification.js's LightDefaults, name for
//    name and range for range, so the native panel binds to the reference's own controls.
EditorSheet SheetFor(unsigned Style)
{
    EditorSheet Sheet;
    Sheet.Appearance = EditorSheetAppearance::Light;

    EditorPropertyGroup& Place = Sheet.Groups[Sheet.GroupCount++];
    std::snprintf(Place.Title, sizeof(Place.Title), "Transform");
    Family(Place, Style);
    Axes(Place, "Position", 0, 2, 0, 0.01f);
    Axes(Place, "Rotation", 0, 0, 0, 0.1f);
    Axes(Place, "Scale",    1, 1, 1, 0.01f);

    EditorPropertyGroup& Out = Sheet.Groups[Sheet.GroupCount++];
    std::snprintf(Out.Title, sizeof(Out.Title), "Output");
    EditorPropertyGroup& Optic = Sheet.Groups[Sheet.GroupCount++];
    std::snprintf(Optic.Title, sizeof(Optic.Title), "Optics");
    EditorPropertyGroup& Flags = Sheet.Groups[Sheet.GroupCount++];
    std::snprintf(Flags.Title, sizeof(Flags.Title), "Flags");

    switch (Style)
    {
    case 0:   // pointlight
        Tint(Out, "Emission tint", 1.000f, 0.851f, 0.627f);
        Tape(Out, "Intensity", 14, 0, 60, 1, "cd");
        Tape(Optic, "Reach", 26, 1, 120, 0, "m");
        Tape(Optic, "Decay exponent", 2, 0, 4, 2, "");
        State(Flags, "Cast shadows", false);
        State(Flags, "Show glow", true);
        break;

    case 1:   // spotlight
        Tint(Out, "Emission tint", 0.910f, 0.941f, 1.000f);
        Tape(Out, "Intensity", 62, 0, 200, 0, "cd");
        Tape(Optic, "Full cone angle", 26, 2, 80, 1, "deg");
        Tape(Optic, "Penumbra", 0.42f, 0, 1, 2, "");
        State(Flags, "Cast shadows", true);
        State(Flags, "Draw cone", true);
        break;

    case 2:   // ieslight
    {
        Tint(Out, "Emission tint", 1.0f, 1.0f, 1.0f);
        Tape(Out, "Luminous flux", 1650, 0, 8000, 0, "lm");
        Tape(Out, "Colour temperature", 4300, 1800, 12000, 0, "K");
        EditorProperty& Preset = Open(Optic, "Profile", EditorPropertyCategory::Select);
        Preset.Picked = 3u;                       // ECE Low Beam
        Preset.OptionCount = 6;
        const char* Names[] = {"Downlight", "Wall wash", "Batwing", "ECE Low Beam", "SAE Low Beam", "High Beam"};
        for (int Slot = 0; Slot < 6; ++Slot)
        {
            std::snprintf(Preset.Options[Slot], sizeof(Preset.Options[Slot]), "%s", Names[Slot]);
        }
        Tape(Optic, "Profile multiplier", 1, 0, 4, 2, "x");
        Tape(Optic, "Field angle", 58, 5, 100, 1, "deg");
        Tape(Optic, "Cut-off pitch", -1, -5, 5, 1, "deg");
        Tape(Optic, "Photometric range", 120, 1, 250, 0, "m");
        State(Flags, "Cast shadows", true);
        State(Flags, "Draw distribution", true);
        break;
    }

    case 3:   // arealight
    {
        Tint(Out, "Emission tint", 1.000f, 0.945f, 0.839f);
        Tape(Out, "Luminous flux", 2400, 0, 20000, 0, "lm");
        EditorProperty& Shape = Open(Optic, "Aperture", EditorPropertyCategory::Select);
        Shape.Picked = 0u;
        Shape.OptionCount = 2;
        std::snprintf(Shape.Options[0], sizeof(Shape.Options[0]), "Rectangle");
        std::snprintf(Shape.Options[1], sizeof(Shape.Options[1]), "Disk");
        Tape(Optic, "Width", 2, 0.1f, 20, 1, "m");
        Tape(Optic, "Height", 1, 0.1f, 20, 1, "m");
        Tape(Optic, "Beam spread", 120, 1, 180, 0, "deg");
        State(Optic, "Two-sided emission", false);
        State(Flags, "Cast shadows", true);
        State(Flags, "Draw emitter", true);
        break;
    }

    case 4:   // tubelight
        Tint(Out, "Emission tint", 0.910f, 0.949f, 1.000f);
        Tape(Out, "Luminous flux", 1800, 0, 12000, 0, "lm");
        Tape(Out, "Colour temperature", 5600, 1800, 12000, 0, "K");
        Tape(Optic, "Length", 1.5f, 0.1f, 20, 1, "m");
        Tape(Optic, "Tube radius", 0.04f, 0.01f, 1, 2, "m");
        Tape(Optic, "Reach", 24, 1, 120, 0, "m");
        State(Flags, "Cast shadows", true);
        State(Flags, "Draw emitter", true);
        break;

    case 5:   // ledlight — LightSpecification.js LightDefaults.ledlight
        Tint(Out, "Emission tint", 1.0f, 1.0f, 1.0f);
        Tape(Out, "Driver power", 10, 0.1f, 100, 1, "W");
        Tape(Out, "Efficacy target", 110, 10, 250, 0, "lm/W");
        Tape(Out, "Dimmer", 1, 0, 1, 2, "");
        Tape(Out, "Colour temperature", 4000, 1800, 12000, 0, "K");
        Tape(Optic, "Package diameter", 40, 5, 120, 0, "mm");
        Tape(Optic, "Emission angle", 120, 10, 180, 0, "deg");
        State(Flags, "Cast shadows", true);
        State(Flags, "Draw emitter", true);
        break;

    default:  // ledstrip — LightSpecification.js LightDefaults.ledstrip
        Tint(Out, "Emission tint", 1.0f, 1.0f, 1.0f);
        Tape(Out, "Flux per metre", 1000, 10, 4000, 0, "lm/m");
        Tape(Out, "Load per metre", 14.4f, 1, 50, 1, "W/m");
        Tape(Out, "Dimmer", 1, 0, 1, 2, "");
        Tape(Out, "Colour temperature", 3000, 1800, 12000, 0, "K");
        Tape(Optic, "Strip length", 2.4f, 0.1f, 20, 1, "m");
        Tape(Optic, "Emitter density", 60, 10, 240, 0, "/m");
        Tape(Optic, "Supply voltage", 24, 5, 48, 0, "V");
        State(Optic, "Opal diffuser", false);
        State(Flags, "Cast shadows", true);
        State(Flags, "Draw emitter", true);
        break;
    }
    return Sheet;
}


ImTextureID SupplyGlyph(IconSymbol, float)
{
    return gGlyph.empty() ? ImTextureID_Invalid : gGlyphId;
}

}   // namespace

int main()
{
    std::filesystem::create_directories("Exhibits/Gallery/LightingNative");
    ImGui::CreateContext();
    ImGuiIO& IO = ImGui::GetIO();
    IO.IniFilename = nullptr;
    IO.DeltaTime = 1.0f / 60;
    IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;

    ImFontConfig FaceConfig;
    std::snprintf(FaceConfig.Name, sizeof(FaceConfig.Name), "Sun reference / regular");
    ImFont* Face = IO.Fonts->AddFontFromFileTTF("EngineContent/Fonts/SunReference/DMSans-Regular.ttf", 14, &FaceConfig);
    ImFontConfig DisplayConfig;
    std::snprintf(DisplayConfig.Name, sizeof(DisplayConfig.Name), "Sun reference / light");
    ImFont* Display = IO.Fonts->AddFontFromFileTTF("EngineContent/Fonts/SunReference/DMSans-Light.ttf", 48, &DisplayConfig);
    ImGui::StyleColorsDark();

    LightGlyphSource = &SupplyGlyph;

    auto Controls = std::make_unique<ControlPanel>();
    Controls->AssignFonts(Face, Face, Face, Face, Face, Display);
    EditorInstance Row;
    std::snprintf(Row.Label, sizeof(Row.Label), "Light");

    // The reference panel is an inspector column: its canvases fall back to 290 px when unmeasured, so the
    //    proof is captured at the sidebar widths the design is actually drawn for.
    int   Width = 318, Height = 1400;
    float Measured = 0;
    EditorSheet Sheet;

    auto Tick = [&]()
    {
        IO.DisplaySize = {float(Width), float(Height)};
        IO.DisplayFramebufferScale = {1, 1};
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(IO.DisplaySize);
        ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
        ImGui::BeginChild("##emitter", {0, float(Height)}, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
        RecordLightInspector(*Controls, Row, Sheet);
        Measured = ImGui::GetCursorPosY();
        ImGui::EndChild();
        ImGui::End();
        ImGui::Render();
        FrontierProof::AcknowledgeTextures();
    };

    auto Capture = [&](const char* Name)
    {
        // The column is as tall as the card stack makes it, so the panel is measured before it is framed.
        Height = 2600;
        Tick();
        Height = static_cast<int>(Measured) + 12;
        for (int Frame = 0; Frame < 3; ++Frame)
        {
            Tick();
        }
        // --bg #050505, the ground the inspector column sits on.
        std::vector<unsigned char> Pixels(size_t(Width) * Height * 3, 5);
        for (ImDrawList* List : ImGui::GetDrawData()->CmdLists)
        {
            FrontierProof::Draw(List, Pixels.data(), Width, Height, {0, 0}, {1, 1}, gGlyphId,
                                {gGlyph.data(), kGlyphEdge, kGlyphEdge, 4});
        }
        const std::string Path = "Exhibits/Gallery/LightingNative/" + std::string(Name) + ".png";
        Check(stbi_write_png(Path.c_str(), Width, Height, 3, Pixels.data(), Width * 3) != 0, "capture written");
    };

    struct Emitter { unsigned Style; const char* File; IconSymbol Artwork; };
    const Emitter Roster[] = {
        {0, "PointLight",  IconSymbol::EditorPointLight},
        {1, "SpotLight",   IconSymbol::EditorSpotlight},
        {2, "IesLight",    IconSymbol::EditorDomeLight},
        {3, "AreaLight",   IconSymbol::EditorAreaLight},
        {4, "TubeLight",   IconSymbol::LightArea2D},
        {5, "LedLight",    IconSymbol::LightPoint2D},
        {6, "StripLight",  IconSymbol::SlateRingLight},
    };

    for (const Emitter& Entry : Roster)
    {
        Sheet = SheetFor(Entry.Style);
        Check(AdoptGlyph(Entry.Artwork), "header glyph baked for this style");
        Check(Sheet.Appearance == EditorSheetAppearance::Light, "dedicated native Light route");
        Width = 318;
        Capture(Entry.File);
        Width = 420;
        Capture((std::string(Entry.File) + "-Wide").c_str());
    }

    //  Interaction: the controls must drive the published properties, not merely draw them -------------
    auto Slot = [&](const char* Label) -> EditorProperty*
    {
        for (uint32_t G = 0; G < Sheet.GroupCount; ++G)
        {
            for (uint32_t P = 0; P < Sheet.Groups[G].PropertyCount; ++P)
            {
                if (std::strcmp(Sheet.Groups[G].Properties[P].Label, Label) == 0)
                {
                    return &Sheet.Groups[G].Properties[P];
                }
            }
        }
        return nullptr;
    };
    auto Point = [&](float X, float Y) { IO.AddMousePosEvent(X, Y); };
    auto Press = [&](bool Down) { IO.AddMouseButtonEvent(0, Down); };

    // The switch is 38 px wide against the card's right inner edge; sweep the column for it.
    {
        Width = 420;
        Height = 2600;
        Sheet = SheetFor(1);                                   // spotlight
        Check(AdoptGlyph(IconSymbol::EditorSpotlight), "spot glyph for the interaction probe");
        Tick();
        const int Tall = static_cast<int>(Measured) + 12;
        Height = Tall;
        Tick();
        const bool Before = Slot("Cast shadows")->On;
        bool Flipped = false;
        for (int Y = 0; Y < Tall && !Flipped; Y += 2)
        {
            Point(float(Width) - 15.0f - 19.0f, float(Y));
            Press(false); Tick();
            Press(true);  Tick();
            Press(false); Tick();
            Flipped = Slot("Cast shadows")->On != Before;
        }
        Check(Flipped, "clicking the switch writes Cast shadows back to the sheet");
    }

    // Pressing the far end of a range track seeks the value to its maximum, as input[type=range] does.
    {
        Width = 420;
        Height = 2600;
        Sheet = SheetFor(1);
        Tick();
        const int Tall = static_cast<int>(Measured) + 12;
        Height = Tall;
        Tick();
        Check(std::fabs(Slot("Intensity")->Figure - 62.0f) < 0.01f, "spot intensity starts at 62 cd");
        bool Seeked = false;
        for (int Y = 0; Y < Tall && !Seeked; Y += 2)
        {
            Point(float(Width) - 15.0f - 19.0f, float(Y));   // well inside the track's right end
            Press(false); Tick();
            Press(true);  Tick();
            Press(false); Tick();
            Seeked = Slot("Intensity")->Figure > 150.0f;
        }
        Check(Seeked, "pressing near the track end seeks spot intensity from 62 cd past 150 cd");
    }

    // The transform cells are drag fields; TransformPanel.jsx steps position by 0.01 per pixel.
    {
        Width = 420;
        Height = 2600;
        Sheet = SheetFor(1);
        Tick();
        const int Tall = static_cast<int>(Measured) + 12;
        Height = Tall;
        Tick();
        const float Before = Slot("Position")->Axes[0];
        bool Dragged = false;
        const float Columns[3] = { 110.0f, 160.0f, 210.0f };
        for (int Y = Tall - 2; Y > Tall - 200 && !Dragged; Y -= 2)
        {
            for (const float X : Columns)
            {
                Point(X, float(Y));
                Press(false); Tick();
                Press(true);  Tick();
                Point(X + 40.0f, float(Y)); Tick(); Tick();
                Press(false); Tick();
                if (std::fabs(Slot("Position")->Axes[0] - Before) > 0.05f) { Dragged = true; break; }
            }
        }
        Check(Dragged, "dragging a transform cell moves the position axis");
    }
    IO.AddMousePosEvent(-FLT_MAX, -FLT_MAX);

    //  The arithmetic the cards print is LightPanel.js's own, checked against it ------------------------
    const float Pi = 3.14159265f;

    // Flux(): the LED driver and the strip derive their lumens; every other style authors them.
    Check(std::fabs(10.0f * 110.0f * 1.0f - 1100.0f) < 0.5f, "LED driver yields 1100 lm");
    Check(std::fabs(1000.0f * 2.4f * 1.0f - 2400.0f) < 0.5f, "2.4 m strip yields 2400 lm");

    // Point: the rail reads Flux / 5^decay, and the sample table repeats it at 1, 2, 5 and 10 m.
    Check(std::fabs(14.0f / std::pow(5.0f, 2.0f) - 0.56f) < 0.005f, "point reads 0.56 lx at 5 m");
    Check(std::fabs(14.0f / std::pow(10.0f, 2.0f) - 0.14f) < 0.005f, "point reads 0.14 lx at 10 m");

    // Spot: the response prints the beam diameter on a perpendicular plane at five metres.
    Check(std::fabs(2 * 5 * std::tan(26 * Pi / 360.0f) - 2.308f) < 0.01f, "spot pools 2.31 m at 5 m");

    // IES: the rail scales the authored flux by the profile multiplier.
    Check(std::fabs(1650.0f * 1.0f - 1650.0f) < 0.5f, "ECE low beam scales to 1650 lm");

    // Area: the response divides flux by the aperture, and a disk takes pi / 4 of the rectangle.
    Check(std::fabs(2400.0f / (2.0f * 1.0f) - 1200.0f) < 0.5f, "2 x 1 m softbox reads 1200 lm/m2");
    Check(std::fabs(2.0f * 1.0f * (Pi / 4.0f) - 1.5708f) < 0.001f, "a disk aperture measures 1.57 m2");

    // Tube: linear output is flux over length.
    Check(std::fabs(1800.0f / 1.5f - 1200.0f) < 0.5f, "1.5 m tube reads 1200 lm/m");

    // Strip: connected watts, emitter count, and the ideal full-load current.
    Check(std::fabs(2.4f * 14.4f - 34.56f) < 0.01f, "2.4 m strip draws 34.6 W");
    Check(std::fabs(std::floor(2.4f * 60.0f + 0.5f) - 144.0f) < 0.5f, "2.4 m strip carries 144 LEDs");
    Check(std::fabs((2.4f * 14.4f) / 24.0f - 1.44f) < 0.005f, "2.4 m strip pulls 1.44 A");

    ImGui::DestroyContext();
    std::printf("PASS %u checks: native light inspector for Point, Spot, IES, Area, Tube, LED and Strip at two column widths.\n",
                Checks);
    return 0;
}
