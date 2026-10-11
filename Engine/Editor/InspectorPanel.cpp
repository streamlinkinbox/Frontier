//============================================================================================================================================
//                                                    INSPECTORPANEL.CPP
//============================================================================================================================================
// 🧩 Development editor inspector — the picked instance as a property sheet.

#include "InspectorPanel.h"
#include "OutlinerMetadata.h"
#include "EntityNotesSurface.h"
#include "TyreInspectorPanel.h"

#include "ControlPanel.h"
#include "SunInspectorPanel.h"
#include "LensFlareInspectorPanel.h"
#include "AtmosphereSkyInspectorPanel.h"
#include "MoonInspectorPanel.h"
#include "StarsInspectorPanel.h"
#include "CloudsInspectorPanel.h"
#include "FogInspectorPanel.h"
#include "WeatherInspectorPanel.h"
#include "CameraInspectorPanel.h"
#include "LightInspectorPanel.h"
#include "PostProcessInspectorPanel.h"
#include "SolidArcInspectorPanel.h"
#include <imgui_internal.h>   // ImGuiWindow: the SkipItems early-out

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Frontier {

namespace {

//------------------------------------------------------------------------------------------------------------------------
//                                                          TOKENS
//------------------------------------------------------------------------------------------------------------------------

constexpr ImU32 kInset  = IM_COL32(26, 26, 26, 255);
constexpr ImU32 kText   = IM_COL32(240, 240, 240, 255);
constexpr ImU32 kDim    = IM_COL32(136, 136, 136, 255);
constexpr ImU32 kFaint  = IM_COL32(92, 92, 92, 255);
constexpr ImU32 kStroke = IM_COL32(255, 255, 255, 13);
constexpr ImU32 kStrong = IM_COL32(46, 46, 46, 255);
constexpr ImU32 kWash   = IM_COL32(255, 255, 255, 5);
constexpr ImU32 kGreen  = IM_COL32(0x34, 0xC7, 0x59, 255);
constexpr ImU32 kAmber  = IM_COL32(245, 158, 11, 255);

// The warning triangle the foot strips hang off a Poor realtime band: three strokes, the upright bar,
//    and its dot. One painter, copied to each strip's file, so the three feet warn alike.
void FootWarn(ImDrawList* Draw, const ImVec2& At, float Size, ImU32 Tint) noexcept
{
    Draw->AddTriangle(ImVec2(At.x + Size * 0.5f, At.y), ImVec2(At.x + Size, At.y + Size),
        ImVec2(At.x, At.y + Size), Tint, 1.5f);
    Draw->AddLine(ImVec2(At.x + Size * 0.5f, At.y + Size * 0.34f),
        ImVec2(At.x + Size * 0.5f, At.y + Size * 0.62f), Tint, 1.5f);
    Draw->AddCircleFilled(ImVec2(At.x + Size * 0.5f, At.y + Size * 0.78f), 1.2f, Tint);
}

float CapsAdvance(ImFont* Small, const char* Text) noexcept
{
    // RecordCaps' advance, without the paint: the right-hung figures measure before they draw.
    float Advance = 0.0f;
    for (const char* P = Text; *P != '\0'; ++P)
    {
        const char Upper[2] = { static_cast<char>(std::toupper(static_cast<unsigned char>(*P))), '\0' };
        Advance += Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, Upper).x + 1.4f;
    }
    return Advance;
}

float ProwHeight(EditorPropertyCategory Category) noexcept
{
    switch (Category)
    {
    case EditorPropertyCategory::Slider:   return 30.0f;
    case EditorPropertyCategory::Switch:   return 26.0f;
    case EditorPropertyCategory::AxisVec3: return 26.0f;
    case EditorPropertyCategory::Colour:   return 26.0f;
    case EditorPropertyCategory::Select:   return 32.0f;
    case EditorPropertyCategory::Readout:  return 18.0f;
    default:                           return 26.0f;
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                           WIRING
//------------------------------------------------------------------------------------------------------------------------

void InspectorPanel::AssignControls(ControlPanel* Controls) noexcept
{
    Controls_ = Controls;
    PrepareSunInspectorFonts();
}

void InspectorPanel::AssignTabOpen(bool* Open) noexcept
{
    TabOpen_ = Open;
}
#ifdef FRONTIER_DEVELOPMENT
void InspectorPanel::RecordCollectionProof(ControlPanel& Controls,EditorInstance* Rows,uint32_t Count,uint32_t Selected,const char* Search) noexcept
{
    Controls_=&Controls;Roster_=Rows;RosterCount_=Count;if(Rows&&Selected<Count){RecordCollection(Rows[Selected],Selected);if(Search)std::snprintf(Collection_.Search,sizeof(Collection_.Search),"%s",Search);}
}
#endif

//------------------------------------------------------------------------------------------------------------------------
//                                                           RECORD
//------------------------------------------------------------------------------------------------------------------------

void InspectorPanel::AssignReadout(const EditorReadout* Readout) noexcept
{
    Readout_ = Readout;
}

void InspectorPanel::Record(EditorInstance* Picked, uint32_t PickedIndex, EditorSheet* Sheet, bool Embedded) noexcept
{
    IM_ASSERT(Controls_ != nullptr);
    if (!Embedded && !ImGui::Begin(WindowTitle_, TabOpen_, ImGuiWindowFlags_NoScrollbar))
    {
        if(!Embedded)ImGui::End();
        return;
    }

    if (PickedIndex != SheetFor_)
    {
        SheetFor_ = PickedIndex;
        NotesSeen_ = false;   // a new subject re-derives its own note disclosure
        NameFor_  = kNoEditorInstance;
        for (uint32_t i = 0u; i < 8u; ++i)
        {
            CardShut_[i] = false;
        }
    }

    // Nothing picked, yet the sheet carries the document hero: the SolidArc inspector shows the document itself.
    static EditorInstance DocumentRow;
    if (Picked == nullptr && Sheet != nullptr && GlassCards_ && Sheet->Appearance == EditorSheetAppearance::SolidArc && Sheet->Hero.Active)
    {
        DocumentRow = EditorInstance{};
        std::snprintf(DocumentRow.Label, sizeof(DocumentRow.Label), "SolidArc document");
        DocumentRow.Symbol = EditorSymbol::None;
        DocumentRow.Tint[0] = 1.0f; DocumentRow.Tint[1] = 0.706f; DocumentRow.Tint[2] = 0.329f;
        Picked = &DocumentRow;
    }

    if (Picked == nullptr || Sheet == nullptr)
    {
        if (GlassCards_)
            RecordSolidArcEmpty(*Controls_);
        else
            RecordEmpty();
        const float Gap = Controls_->QueryFootTop() - ImGui::GetCursorScreenPos().y;
        if (Gap > 0.0f)
        {
            ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, Gap));
        }
        RecordFooter(nullptr);
        if(!Embedded)ImGui::End();
        return;
    }

    if (Sheet->Appearance == EditorSheetAppearance::SolidArc)
    {
        // SolidArc's own glass-card sheet: header, tiles, transform rows. The folds persist in CardShut_.
        ImGui::BeginChild("##solidarc-properties", ImVec2(0.0f, ImMax(0.0f, Controls_->QueryFootTop() - ImGui::GetCursorScreenPos().y)), false);
        ImGui::PushID(static_cast<int>(PickedIndex));
        RecordSolidArcInspector(*Controls_, *Picked, PickedIndex, *Sheet, CardShut_);
        ImGui::PopID();
        RecordNotes(Picked);
        ImGui::EndChild();
        RecordFooter(Picked);
        if(!Embedded)ImGui::End();
        return;
    }
    if(Sheet->Appearance==EditorSheetAppearance::Tyre||Sheet->Appearance==EditorSheetAppearance::TyreTread
       ||Sheet->Appearance==EditorSheetAppearance::TyreLattice){
        ImGui::BeginChild("##tyre-properties",ImVec2(0,ImMax(0.f,Controls_->QueryFootTop()-ImGui::GetCursorScreenPos().y)),false);
        ImGui::PushID(static_cast<int>(PickedIndex));RecordTyreInspector(*Controls_,*Picked,*Sheet);ImGui::PopID();
        RecordNotes(Picked);
        ImGui::EndChild();RecordFooter(Picked);if(!Embedded)ImGui::End();return;
    }
    if(Sheet->Appearance==EditorSheetAppearance::Light){
        ImGui::BeginChild("##light-properties",ImVec2(0,ImMax(0.f,Controls_->QueryFootTop()-ImGui::GetCursorScreenPos().y)),ImGuiChildFlags_None,ImGuiWindowFlags_NoScrollbar);
        ImGui::PushID(static_cast<int>(PickedIndex));RecordLightInspector(*Controls_,*Picked,*Sheet);ImGui::PopID();
        RecordNotes(Picked);
        ImGui::EndChild();RecordFooter(Picked);if(!Embedded)ImGui::End();return;
    }
    if(Sheet->Appearance==EditorSheetAppearance::PostProcess){
        ImGui::BeginChild("##post-process-properties",ImVec2(0,ImMax(0.f,Controls_->QueryFootTop()-ImGui::GetCursorScreenPos().y)),false);
        ImGui::PushID(static_cast<int>(PickedIndex));RecordPostProcessInspector(*Controls_,*Picked,*Sheet);ImGui::PopID();RecordNotes(Picked);
        ImGui::EndChild();RecordFooter(Picked);if(!Embedded)ImGui::End();return;
    }
    if(Sheet->Appearance==EditorSheetAppearance::Camera){
        ImGui::BeginChild("##camera-properties",ImVec2(0,ImMax(0.f,Controls_->QueryFootTop()-ImGui::GetCursorScreenPos().y)),false);
        ImGui::PushID(static_cast<int>(PickedIndex));RecordCameraInspector(*Controls_,*Picked,*Sheet);ImGui::PopID();
        RecordNotes(Picked);
        ImGui::EndChild();RecordFooter(Picked);if(!Embedded)ImGui::End();return;
    }
    if (Sheet->Appearance == EditorSheetAppearance::Wind || Sheet->Appearance == EditorSheetAppearance::Precipitation || Sheet->Appearance == EditorSheetAppearance::Rainbow) {
        ImGui::BeginChild("##weather-properties",ImVec2(0,ImMax(0.f,Controls_->QueryFootTop()-ImGui::GetCursorScreenPos().y)),false);
        ImGui::PushID(static_cast<int>(PickedIndex));RecordWeatherInspector(*Controls_,*Picked,*Sheet);ImGui::PopID();
        RecordNotes(Picked);
        ImGui::EndChild();RecordFooter(Picked);if(!Embedded)ImGui::End();return;
    }
    if (Sheet->Appearance == EditorSheetAppearance::HeightFog || Sheet->Appearance == EditorSheetAppearance::AerialFog || Sheet->Appearance == EditorSheetAppearance::LocalFog) {
        ImGui::BeginChild("##fog-properties",ImVec2(0,ImMax(0.f,Controls_->QueryFootTop()-ImGui::GetCursorScreenPos().y)),false);
        ImGui::PushID(static_cast<int>(PickedIndex));RecordFogInspector(*Controls_,*Picked,*Sheet);ImGui::PopID();
        RecordNotes(Picked);
        ImGui::EndChild();RecordFooter(Picked);if(!Embedded)ImGui::End();return;
    }
    if (Sheet->Appearance == EditorSheetAppearance::GlobalCloud || Sheet->Appearance == EditorSheetAppearance::LocalCloud) {
        ImGui::BeginChild("##cloud-properties",ImVec2(0,ImMax(0.f,Controls_->QueryFootTop()-ImGui::GetCursorScreenPos().y)),false);
        ImGui::PushID(static_cast<int>(PickedIndex));RecordCloudsInspector(*Controls_,*Picked,*Sheet);ImGui::PopID();
        RecordNotes(Picked);
        ImGui::EndChild();RecordFooter(Picked);if(!Embedded)ImGui::End();return;
    }
    if (Sheet->Appearance == EditorSheetAppearance::Stars) {
        ImGui::BeginChild("##stars-properties",ImVec2(0,0),ImGuiChildFlags_None,ImGuiWindowFlags_None);
        ImGui::PushID(static_cast<int>(PickedIndex));RecordStarsInspector(*Controls_,*Picked,*Sheet);ImGui::PopID();
        RecordNotes(Picked);
        ImGui::EndChild();if(!Embedded)ImGui::End();return;
    }
    if (Sheet->Appearance == EditorSheetAppearance::Moon) {
        ImGui::BeginChild("##moon-properties",ImVec2(0,ImMax(0.f,Controls_->QueryFootTop()-ImGui::GetCursorScreenPos().y)),false);
        ImGui::PushID(static_cast<int>(PickedIndex));RecordMoonInspector(*Controls_,*Picked,*Sheet);ImGui::PopID();
        RecordNotes(Picked);
        ImGui::EndChild();RecordFooter(Picked);if(!Embedded)ImGui::End();return;
    }
    if (Sheet->Appearance == EditorSheetAppearance::AtmosphereSky)
    {
        ImGui::BeginChild("##sky-properties", ImVec2(0.0f, ImMax(0.0f, Controls_->QueryFootTop() - ImGui::GetCursorScreenPos().y)), false);
        ImGui::PushID(static_cast<int>(PickedIndex));
        RecordAtmosphereSkyInspector(*Controls_, *Picked, *Sheet);
        ImGui::PopID();RecordNotes(Picked);ImGui::EndChild();RecordFooter(Picked);if(!Embedded)ImGui::End();return;
    }
    if (Sheet->Appearance == EditorSheetAppearance::LensFlare)
    {
        ImGui::BeginChild("##flare-properties", ImVec2(0.0f, ImMax(0.0f, Controls_->QueryFootTop() - ImGui::GetCursorScreenPos().y)), false);
        ImGui::PushID(static_cast<int>(PickedIndex));
        RecordLensFlareInspector(*Controls_, *Picked, *Sheet);
        ImGui::PopID();RecordNotes(Picked);ImGui::EndChild();RecordFooter(Picked);if(!Embedded)ImGui::End();return;
    }

    if (Picked->Category == EditorInstanceCategory::Folder && Roster_ && PickedIndex < RosterCount_)
    {
        ImGui::BeginChild("##collection-content",ImVec2(0,ImMax(0.f,Controls_->QueryFootTop()-ImGui::GetCursorScreenPos().y)),ImGuiChildFlags_None,ImGuiWindowFlags_NoScrollbar);
        RecordCollection(*Picked, PickedIndex);
        // Every other branch draws the note; folders were the one that did not. The reference renders
        //    EntityNotes from the shared Header(), so a group carries one exactly like anything else.
        RecordNotes(Picked);
        ImGui::EndChild();
        RecordFooter(Picked);
        if (!Embedded) ImGui::End();
        return;
    }

    // The force fields. The row is a page rather than an object: no transform, no hierarchy, no notes
    //    about a thing — the scene's fields, grouped by what they contribute.
    if (Sheet->Appearance == EditorSheetAppearance::ForceFields)
    {
        RecordIdent(Picked, PickedIndex);
        ImGui::BeginChild("##force-properties", ImVec2(0.0f, ImMax(0.0f, Controls_->QueryFootTop() - ImGui::GetCursorScreenPos().y)), false);
        ImGui::PushID(static_cast<int>(PickedIndex));
        RecordForceFields();
        ImGui::PopID();
        RecordNotes(Picked);
        ImGui::EndChild();
        RecordFooter(Picked);
        if (!Embedded) ImGui::End();
        return;
    }

    // The gas domain and its child emitter. Both lead with a transform and a hierarchy, which the generic
    //    sheet path has no notion of, so they draw their own stack exactly as the sun and flare pages do.
    if (Sheet->Appearance == EditorSheetAppearance::Gas ||
        Sheet->Appearance == EditorSheetAppearance::GasEmitter)
    {
        RecordIdent(Picked, PickedIndex);
        ImGui::BeginChild("##gas-properties", ImVec2(0.0f, ImMax(0.0f, Controls_->QueryFootTop() - ImGui::GetCursorScreenPos().y)), false);
        ImGui::PushID(static_cast<int>(PickedIndex));
        RecordGas(*Picked, PickedIndex, Sheet->Appearance == EditorSheetAppearance::GasEmitter);
        ImGui::PopID();
        RecordNotes(Picked);
        ImGui::EndChild();
        RecordFooter(Picked);
        if (!Embedded) ImGui::End();
        return;
    }

    if (Sheet->Appearance == EditorSheetAppearance::Sun)
    {
        ImGui::BeginChild("##sun-properties", ImVec2(0.0f, ImMax(0.0f, Controls_->QueryFootTop() - ImGui::GetCursorScreenPos().y)), false);
        ImGui::PushID(static_cast<int>(PickedIndex));
        RecordSunInspector(*Controls_, *Picked, *Sheet);
        ImGui::PopID();
        RecordNotes(Picked);
        ImGui::EndChild();
        RecordFooter(Picked);
        if(!Embedded)ImGui::End();
        return;
    }

    RecordIdent(Picked, PickedIndex);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 2.0f));
    ImGui::BeginChild("##props", ImVec2(0.0f, ImMax(0.0f, Controls_->QueryFootTop() - ImGui::GetCursorScreenPos().y)), false);
    for (uint32_t i = 0u; i < Sheet->GroupCount && i < kMaxEditorSheetGroups; ++i)
    {
        RecordCard(Sheet->Groups[i], i);
    }
    // FracturePanel.jsx hangs the fracture card off the end of the geometry sheet, after every property
    //    group and before the rest of the inspector.
    if (Picked->Category == EditorInstanceCategory::Geometry) RecordFracture(*Picked, PickedIndex);
    RecordStanding(Picked, PickedIndex);
    RecordNotes(Picked);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    RecordFooter(Picked);
    if(!Embedded)ImGui::End();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     PER-OBJECT FRACTURE
//------------------------------------------------------------------------------------------------------------------------
// FracturePanel.jsx, mounted. The card draws itself from Engine/Editor/FractureCardSurface.h; this adds the
//    hit targets the browser gets from real buttons, and holds the recipe beside the instance.

void InspectorPanel::RecordFracture(EditorInstance& Picked, uint32_t PickedIndex) noexcept
{
    namespace FR = Fracture;

    // The slot for this instance, or the first free one. Sixteen covers any sane selection history.
    FractureRecord* Slot = nullptr;
    for (FractureRecord& One : Fracture_) if (One.For == PickedIndex) { Slot = &One; break; }
    if (Slot == nullptr)
        for (FractureRecord& One : Fracture_) if (One.For == kNoEditorInstance)
        { One = FractureRecord{}; One.For = PickedIndex; Slot = &One; break; }
    if (Slot == nullptr) { Fracture_[0] = FractureRecord{}; Fracture_[0].For = PickedIndex; Slot = &Fracture_[0]; }

    ImFont* Light = ImGui::GetFont();
    ImFont* Regular = Light;
    for (ImFont* Face : ImGui::GetIO().Fonts->Fonts)
    {
        if (!std::strcmp(Face->GetDebugName(), "Sun reference / light"))   Light = Face;
        if (!std::strcmp(Face->GetDebugName(), "Sun reference / regular")) Regular = Face;
    }

    // Describe().Primitive, 1:1: the browser reads the primitive off the row's icon, never off its name.
    //    A row whose artwork is not one of the five base meshes has no primitive, and the card says so.
    const char* Primitive = BaseMeshPrimitive(Picked.Artwork);
    const FR::Owner Who { Picked.Label[0] ? Picked.Label : "Object", Primitive ? Primitive : "mesh" };

    const float Wide = ImGui::GetContentRegionAvail().x;
    if (Wide < 120.0f) return;
    const ImVec2 Spot = ImGui::GetCursorScreenPos();
    FR::Settings& Recipe = Slot->Recipe;
    const FR::Plan Plan = FR::Measure(Light, Regular, Wide, Recipe, Who);
    FR::PaintCard(ImGui::GetWindowDrawList(), Light, Regular, Spot, Wide, Recipe, Who);

    ImGui::PushID(static_cast<int>(PickedIndex));
    auto Hit = [&](const char* Id, float X, float Y, float W, float H)
    {
        ImGui::SetCursorScreenPos({ Spot.x + X, Spot.y + Y });
        return ImGui::InvisibleButton(Id, { W, H });
    };

    if (Hit("##fracture-enable", Wide - FR::PadX - FR::ToggleWide, Plan.EnableTop,
            FR::ToggleWide, FR::ToggleTall))
        Recipe.Enabled = !Recipe.Enabled;

    if (Recipe.Enabled)
    {
        const float Each = (Plan.Inner - 2.0f - FR::ModePad * 3.0f) * 0.5f;
        for (int I = 0; I < 2; ++I)
            if (Hit(I == 0 ? "##fracture-dynamic" : "##fracture-baked",
                    FR::PadX + 1.0f + FR::ModePad + I * (Each + FR::ModePad),
                    Plan.ModeTop + 1.0f + FR::ModePad, Each, FR::ButtonTall))
                Recipe.Run = I == 0 ? FR::Mode::Dynamic : FR::Mode::Baked;

        if (Plan.HasSdf && Hit("##fracture-sdf", Wide - FR::PadX - FR::ToggleWide, Plan.SdfSwitchTop,
                               FR::ToggleWide, FR::ToggleTall))
            Recipe.PieceSdf = !Recipe.PieceSdf;

        // A select with three options; clicking cycles it, which is as close as a draw-list card gets.
        if (Plan.HasField && Hit("##fracture-resolution", FR::PadX, Plan.SelectTop, Plan.Inner, FR::SelectTall))
            Recipe.SdfResolution = Recipe.SdfResolution == 32 ? 64 : Recipe.SdfResolution == 64 ? 128 : 32;
    }
    ImGui::PopID();

    ImGui::SetCursorScreenPos(Spot);
    ImGui::Dummy({ Wide, Plan.Tall + 12.0f });   // .generic-card margin-bottom
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE GAS DOMAIN
//------------------------------------------------------------------------------------------------------------------------
// GasPanel.jsx, mounted. The stack draws itself from Engine/Editor/GasCardSurface.h and reports where it
//    painted every control; this adds the hit targets the browser gets from real inputs, and holds the
//    domain beside the instance.
//
// 🔴 THE CARD CANNOT OPEN THE FLUID EDITOR AND DOES NOT PRETEND TO.
//    In the browser ↗ opens an iframe hosting Experimental/Fluid. There is no native equivalent of that
//    window yet, so the button raises a request the host consumes. A button that silently does nothing
//    would be worse than one that is honest about who answers it.

void InspectorPanel::RecordGas(EditorInstance& Picked, uint32_t PickedIndex, bool Emitter) noexcept
{
    namespace GC = GasCards;

    // The slot for this instance, or the first free one. Eight covers any sane selection history, and a
    //    full set recycles the first rather than refusing to draw.
    GasRecord* Slot = nullptr;
    for (GasRecord& One : Gas_) if (One.For == PickedIndex) { Slot = &One; break; }
    if (Slot == nullptr)
        for (GasRecord& One : Gas_) if (One.For == kNoEditorInstance)
        { One = GasRecord{}; One.For = PickedIndex; Slot = &One; break; }
    if (Slot == nullptr) { Gas_[0] = GasRecord{}; Gas_[0].For = PickedIndex; Slot = &Gas_[0]; }

    GC::GasCardSubject& Domain = Slot->Domain;
    Domain.Name    = Picked.Label[0] ? Picked.Label : "Gas Domain";
    Domain.Hidden  = !Picked.Visible;
    Domain.Emitter = Emitter;

    ImFont* Light = ImGui::GetFont();
    ImFont* Regular = Light;
    for (ImFont* Face : ImGui::GetIO().Fonts->Fonts)
    {
        if (!std::strcmp(Face->GetDebugName(), "Sun reference / light"))   Light = Face;
        if (!std::strcmp(Face->GetDebugName(), "Sun reference / regular")) Regular = Face;
    }

    const float Wide = ImGui::GetContentRegionAvail().x;
    if (Wide < 120.0f) return;
    const ImVec2 Spot = ImGui::GetCursorScreenPos();

    // An emitter is its own entity and shows its own card; it is not a fourth group on the domain. Until
    //    the emitter surface is drawn it borrows the domain stack's transform and says so, rather than
    //    showing a blank page.
    GC::GasHitRegions Where;
    const float Tall = GC::PaintGasCard(ImGui::GetWindowDrawList(), Light, Regular, Spot, Wide, Domain, &Where);
    GasWhere_ = Where;   // the panel's own answer to "where is that button", for hosts and harnesses

    auto Target = [&](const char* Id, const ImVec4& Region)
    {
        if (Region.z <= 0.0f) return false;
        ImGui::SetCursorScreenPos({ Region.x, Region.y });
        return ImGui::InvisibleButton(Id, { Region.z, Region.w });
    };

    // A draw-list slider has no thumb to grab, so the track is dragged: press anywhere on it and the
    //    reading follows the pointer, which is what the browser's range input does.
    auto Drag = [&](const char* Id, const ImVec4& Region, float Minimum, float Maximum, float& Reading)
    {
        if (Region.z <= 0.0f) return;
        ImGui::SetCursorScreenPos({ Region.x, Region.y });
        ImGui::InvisibleButton(Id, { Region.z, Region.w });
        if (ImGui::IsItemActive())
        {
            const float Part = ImClamp((ImGui::GetIO().MousePos.x - Region.x) / ImMax(1.0f, Region.z), 0.0f, 1.0f);
            Reading = Minimum + Part * (Maximum - Minimum);
        }
    };

    if (Target("##gas-visible", Where.Tiles[0]))   Picked.Visible = !Picked.Visible;
    if (Target("##gas-obstructs", Where.Tiles[1])) Domain.Obstructs = !Domain.Obstructs;
    // 🔴 Two-way coupling stays a toggle, and stays off until somebody turns it on.
    if (Target("##gas-coupled", Where.Tiles[2]))   Domain.Coupled = !Domain.Coupled;

    // Selects on a draw-list card cycle, which is as close as this gets to a dropdown.
    if (Target("##gas-policy", Where.Policy)) Domain.Policy = (Domain.Policy + 1u) % 4u;
    if (Target("##gas-quality", Where.Quality))
    {
        static const char* const Rungs[6] = { "Automatic - by distance", "Hero - 128 cubed", "Near - 96 cubed",
                                              "Mid - 64 cubed", "Far - 32 cubed", "Flipbook - no solver" };
        uint32_t At = 0u;
        for (uint32_t Index = 0u; Index < 6u; ++Index)
            if (!std::strcmp(Domain.QualityPin, Rungs[Index])) At = Index;
        Domain.QualityPin = Rungs[(At + 1u) % 6u];
    }

    if (Target("##gas-retire", Where.Retire)) Domain.Retire = !Domain.Retire;
    if (Target("##gas-fire", Where.Fire))     Domain.Fired  = !Domain.Fired;

    Drag("##gas-lifetime", Where.Lifetime, 0.2f, 30.0f, Domain.Lifetime);
    Drag("##gas-distance", Where.Distance, 0.0f, 200.0f, Domain.Distance);

    uint32_t Count = 0u;
    const GC::GasField* Fields = GC::GasSheet(Count);
    for (uint32_t Index = 0u; Index < Count && Index < 16u; ++Index)
    {
        if (Where.Field[Index].z <= 0.0f) continue;
        char Id[32];
        std::snprintf(Id, sizeof(Id), "##gas-field-%u", Index);
        if (Fields[Index].Control == GC::GasControl::Switch)
        {
            if (Target(Id, Where.Field[Index]))
                Domain.Readings[Index] = Domain.Readings[Index] > 0.5f ? 0.0f : 1.0f;
        }
        else
        {
            Drag(Id, Where.Field[Index], Fields[Index].Minimum, Fields[Index].Maximum, Domain.Readings[Index]);
        }
    }

    // ② Emitters are children, so adding one adds a row. The panel cannot grow the roster itself; it marks
    //    the domain and the host that owns the scene does the parenting.
    if (Target("##gas-add-emitter", Where.AddEmitter) && Domain.ChildCount < 4u)
    {
        GC::GasChild& Fresh = Domain.Children[Domain.ChildCount];
        Fresh = GC::GasChild{};
        Fresh.Name = "Emitter";
        Domain.ChildCount += 1u;
    }

    // ③ One ↗, two destinations. An emitter opens the Particle Editor; a domain opens the Fluid one.
    if (Target("##gas-open-fluid", Where.Open))
    {
        if (Emitter) ParticleEditorFor_ = PickedIndex;
        else         FluidEditorFor_    = PickedIndex;
    }

    ImGui::SetCursorScreenPos(Spot);
    ImGui::Dummy({ Wide, Tall });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE FORCE FIELDS
//------------------------------------------------------------------------------------------------------------------------

void InspectorPanel::RecordForceFields() noexcept
{
    namespace FC = ForceCards;

    ImFont* Light = ImGui::GetFont();
    ImFont* Regular = Light;
    for (ImFont* Face : ImGui::GetIO().Fonts->Fonts)
    {
        if (!std::strcmp(Face->GetDebugName(), "Sun reference / light"))   Light = Face;
        if (!std::strcmp(Face->GetDebugName(), "Sun reference / regular")) Regular = Face;
    }

    const float Wide = ImGui::GetContentRegionAvail().x;
    if (Wide < 120.0f) return;
    const ImVec2 Spot = ImGui::GetCursorScreenPos();

    FC::ForceHitRegions Where;
    const float Tall = FC::PaintForceCard(ImGui::GetWindowDrawList(), Light, Regular, Spot, Wide, Forces_, &Where);
    ForceWhere_ = Where;

    auto Target = [&](const char* Id, const ImVec4& Region)
    {
        if (Region.z <= 0.0f) return false;
        ImGui::SetCursorScreenPos({ Region.x, Region.y });
        return ImGui::InvisibleButton(Id, { Region.z, Region.w });
    };
    auto Drag = [&](const char* Id, const ImVec4& Region, float Minimum, float Maximum, float& Reading)
    {
        if (Region.z <= 0.0f) return;
        ImGui::SetCursorScreenPos({ Region.x, Region.y });
        ImGui::InvisibleButton(Id, { Region.z, Region.w });
        if (ImGui::IsItemActive())
        {
            const float Part = ImClamp((ImGui::GetIO().MousePos.x - Region.x) / ImMax(1.0f, Region.z), 0.0f, 1.0f);
            Reading = Minimum + Part * (Maximum - Minimum);
        }
    };

    // Adding a field takes the kind's own defaults, which is why ForceFieldKindFacts carries a reach: a
    //    gravity added here must arrive reaching everywhere, not as an 8 m ball somebody has to notice.
    for (uint32_t At = 0u; At < uint32_t(ForceFieldKind::Count); ++At)
    {
        char Id[40];
        std::snprintf(Id, sizeof(Id), "##force-add-%u", At);
        if (!Target(Id, Where.Add[At]) || Forces_.Count >= FC::MaxFields) continue;
        ForceField& Fresh = Forces_.Fields[Forces_.Count];
        Fresh = ForceField{};
        Fresh.Kind    = ForceFieldKind(At);
        Fresh.Reaches = FactsOf(Fresh.Kind).Reaches;
        std::snprintf(Fresh.Name, sizeof(Fresh.Name), "%s", FactsOf(Fresh.Kind).Name);
        Forces_.Count += 1u;
    }

    for (uint32_t At = 0u; At < Forces_.Count && At < FC::MaxFields; ++At)
    {
        char Id[40];
        std::snprintf(Id, sizeof(Id), "##force-enabled-%u", At);
        if (Target(Id, Where.Enabled[At])) Forces_.Fields[At].Enabled = !Forces_.Fields[At].Enabled;

        std::snprintf(Id, sizeof(Id), "##force-strength-%u", At);
        Drag(Id, Where.Strength[At], -20.0f, 20.0f, Forces_.Fields[At].Strength);
    }

    // Removal is done after the pass, and once: deleting inside the loop would shift every later card out
    //    from under the regions this frame was drawn with.
    uint32_t Doomed = FC::MaxFields;
    for (uint32_t At = 0u; At < Forces_.Count && At < FC::MaxFields; ++At)
    {
        char Id[40];
        std::snprintf(Id, sizeof(Id), "##force-remove-%u", At);
        if (Target(Id, Where.Remove[At])) Doomed = At;
    }
    if (Doomed < Forces_.Count)
    {
        for (uint32_t At = Doomed + 1u; At < Forces_.Count; ++At) Forces_.Fields[At - 1u] = Forces_.Fields[At];
        Forces_.Count -= 1u;
        Forces_.Fields[Forces_.Count] = ForceField{};
    }

    ImGui::SetCursorScreenPos(Spot);
    ImGui::Dummy({ Wide, Tall });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       LETTERSPACED CAPS
//------------------------------------------------------------------------------------------------------------------------

float InspectorPanel::RecordCaps(const char* Text, const ImVec2& At, ImU32 Tint) noexcept
{
    ImFont*     Small = Controls_->QuerySmall();
    ImDrawList* Draw  = ImGui::GetWindowDrawList();
    ImGui::PushFont(Small);
    float Advance = 0.0f;
    for (const char* P = Text; *P != '\0'; ++P)
    {
        const char Upper[2] = { static_cast<char>(std::toupper(static_cast<unsigned char>(*P))), '\0' };
        Draw->AddText(ImVec2(At.x + Advance, At.y), Tint, Upper);
        Advance += Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, Upper).x + 1.4f;
    }
    ImGui::PopFont();
    return Advance;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                           EMPTY
//------------------------------------------------------------------------------------------------------------------------

void InspectorPanel::RecordCollection(EditorInstance& Selected, uint32_t Index) noexcept
{
    auto& C=Collection_;C.Traverse(Roster_,RosterCount_,Index);ImGui::PushID("collection");ImGui::PushFont(Controls_->QueryUi(),14);
    auto* CurrentWindow=ImGui::GetCurrentWindow();if(CurrentWindow&&CurrentWindow->ParentWindow){auto* Parent=CurrentWindow->ParentWindow;Parent->DrawList->AddRectFilled(Parent->Pos,{Parent->Pos.x+Parent->Size.x,Parent->Pos.y+Parent->Size.y},IM_COL32(18,18,18,255));}auto* D=ImGui::GetWindowDrawList();const ImVec2 BackgroundMin=ImGui::GetWindowPos(),BackgroundMax={BackgroundMin.x+ImGui::GetWindowSize().x,BackgroundMin.y+ImGui::GetWindowSize().y};D->AddRectFilled(BackgroundMin,BackgroundMax,IM_COL32(18,18,18,255));ImGui::Indent(7);const float W=ImGui::GetContentRegionAvail().x-8;auto CountText=[](char* Out,size_t Capacity,uint32_t Value){if(Value>=1000)std::snprintf(Out,Capacity,"%u,%03u",Value/1000,Value%1000);else std::snprintf(Out,Capacity,"%u",Value);};
    auto Card=[&](const char* Id,float H){ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,25);ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{23,24});ImGui::PushStyleColor(ImGuiCol_ChildBg,IM_COL32(32,32,32,255));ImGui::PushStyleColor(ImGuiCol_Border,IM_COL32(255,255,255,12));return ImGui::BeginChild(Id,{W,H},ImGuiChildFlags_Borders|ImGuiChildFlags_AlwaysUseWindowPadding,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);};
    auto EndCard=[&](){ImGui::EndChild();ImGui::PopStyleColor(2);ImGui::PopStyleVar(2);ImGui::Dummy({0,3});};
    ImGui::Dummy({0,1});RecordCaps("COLLECTION / SCENE INVENTORY",ImGui::GetCursorScreenPos(),kDim);ImGui::Dummy({0,27});
    const ImVec2 TitleAt=ImGui::GetCursorScreenPos();ImGui::Dummy({0,38});D->AddRectFilled({TitleAt.x+3,TitleAt.y+10},{TitleAt.x+25,TitleAt.y+29},IM_COL32(76,79,75,255),3);D->AddRectFilled({TitleAt.x+5,TitleAt.y+6},{TitleAt.x+15,TitleAt.y+12},IM_COL32(96,100,94,255),2);if(!std::strcmp(Selected.Label,"Asset archive")){D->AddLine({TitleAt.x+8,TitleAt.y+16},{TitleAt.x+20,TitleAt.y+16},IM_COL32(145,140,113,180),1);D->AddLine({TitleAt.x+8,TitleAt.y+20},{TitleAt.x+17,TitleAt.y+20},IM_COL32(145,140,113,150),1);}else D->AddRectFilled({TitleAt.x+6,TitleAt.y+21},{TitleAt.x+9,TitleAt.y+24},IM_COL32(75,135,151,220),1);ImGui::SetCursorScreenPos({TitleAt.x+35,TitleAt.y});ImGui::PushFont(Controls_->QueryTitle(),30);ImGui::SetNextItemWidth(W-78);ImGui::PushStyleColor(ImGuiCol_FrameBg,{0,0,0,0});ImGui::InputText("##folder-name",Selected.Label,sizeof(Selected.Label));ImGui::PopStyleColor();ImGui::PopFont();ImGui::SetCursorScreenPos({TitleAt.x,TitleAt.y+50});
    // Reference FolderInspector.jsx:70 prints the row description, defaulting to "Scene collection".
    const char* const FolderDescription="Scene collection · indexed from the current scene";
    ImGui::PushStyleColor(ImGuiCol_Text,kDim);ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+W);ImGui::TextWrapped("%s",!std::strcmp(Selected.Label,"Asset archive")?"Browser test fixture · 2,500 asset records · indexed from the current scene":FolderDescription);ImGui::PopTextWrapPos();ImGui::PopStyleColor();const ImVec2 AfterDescription=ImGui::GetCursorScreenPos();ImGui::SetCursorScreenPos({TitleAt.x+W-58,TitleAt.y+4});if(ImGui::InvisibleButton("##folder-notes-reveal",{54,24}))NotesFocus_=true;if(ImGui::IsItemHovered())D->AddText(Controls_->QuerySmall(),9,{TitleAt.x+W-54,TitleAt.y+11},kDim,Selected.Notes[0]?"Edit notes":"+ Notes");ImGui::SetCursorScreenPos(AfterDescription);if(Selected.Notes[0]){const ImVec2 NotesAt=ImGui::GetCursorScreenPos();if(ImGui::InvisibleButton("##edit-folder-notes",{72,26}))NotesFocus_=true;D->AddRectFilled(NotesAt,{NotesAt.x+72,NotesAt.y+26},IM_COL32(30,31,29,255),13);D->AddText(Controls_->QuerySmall(),9,{NotesAt.x+12,NotesAt.y+8},kText,"Edit Notes");}
    if(Selected.Notes[0]||NotesFocus_){ImGui::SetNextItemWidth(-1);ImGui::PushStyleColor(ImGuiCol_FrameBg,IM_COL32(13,13,13,255));ImGui::InputTextMultiline("##folder-notes",Selected.Notes,sizeof(Selected.Notes),{-1,58});NotesFocus_=ImGui::IsItemFocused()||NotesFocus_;ImGui::PopStyleColor();}
    ImGui::Dummy({0,12});ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,14);ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,1);ImGui::PushStyleColor(ImGuiCol_Border,{.28f,.28f,.25f,1});ImGui::PushStyleColor(ImGuiCol_Button,{.12f,.12f,.12f,1});ImGui::PushStyleColor(ImGuiCol_ButtonHovered,{.17f,.17f,.17f,1});
    std::vector<uint32_t> Trail;for(uint32_t I=0;I<=Index&&I<RosterCount_;++I){while(!Trail.empty()&&Roster_[Trail.back()].Depth>=Roster_[I].Depth)Trail.pop_back();if(I==Index||Roster_[I].Category==EditorInstanceCategory::Folder)Trail.push_back(I);}const size_t First=Trail.size()>4?Trail.size()-4:0;for(size_t I=First;I<Trail.size();++I){if(I>First)ImGui::SameLine(0,3);const char* Crumb=Roster_[Trail[I]].Label;const float CrumbW=Controls_->QuerySmall()->CalcTextSizeA(10,FLT_MAX,0,Crumb).x+20;const ImVec2 CrumbAt=ImGui::GetCursorScreenPos();ImGui::PushID(int(Trail[I]));if(ImGui::InvisibleButton("##crumb",{CrumbW,27}))CollectionPick_=Trail[I];ImGui::PopID();D->AddRectFilled(CrumbAt,{CrumbAt.x+CrumbW,CrumbAt.y+27},IM_COL32(30,31,29,255),14);D->AddRect(CrumbAt,{CrumbAt.x+CrumbW,CrumbAt.y+27},IM_COL32(108,111,101,255),14);D->AddText(Controls_->QuerySmall(),10,{CrumbAt.x+10,CrumbAt.y+8},kText,Crumb);if(I+1<Trail.size()){ImGui::SameLine(0,3);ImGui::TextDisabled("/");}}
    ImGui::PopStyleColor(3);ImGui::PopStyleVar(2);ImGui::Dummy({0,11});

    Card("##collection-total",260);D=ImGui::GetWindowDrawList();const ImVec2 TotalOrigin=ImGui::GetWindowPos();D->AddRectFilled({TotalOrigin.x+23,TotalOrigin.y+26},{TotalOrigin.x+26,TotalOrigin.y+41},IM_COL32(95,190,151,255),2);ImGui::SetCursorPos({35,24});ImGui::TextUnformatted("Collection contents");ImGui::SetCursorPos({23,62});ImGui::PushFont(Controls_->QueryDisplay(),61);char TotalText[32];CountText(TotalText,sizeof(TotalText),C.Total);ImGui::TextUnformatted(TotalText);ImGui::PopFont();const float TotalWidth=Controls_->QueryDisplay()->CalcTextSizeA(61,FLT_MAX,0,TotalText).x;D->AddText(Controls_->QuerySmall(),10,{TotalOrigin.x+29+TotalWidth,TotalOrigin.y+100},kDim,"entries");ImGui::SetCursorPos({23,137});ImGui::TextDisabled("All descendants, including nested folders");D->AddLine({TotalOrigin.x+23,TotalOrigin.y+173},{TotalOrigin.x+W-23,TotalOrigin.y+173},IM_COL32(255,255,255,10));ImGui::SetCursorPos({23,195});
    if(ImGui::BeginTable("##facts",3,ImGuiTableFlags_SizingStretchSame)){const uint32_t V[]={C.Direct,C.Folders,C.MaximumDepth};const char* L[]={"Direct children","Nested folders","Levels below"};for(int I=0;I<3;++I){ImGui::TableNextColumn();ImGui::PushFont(Controls_->QueryUi(),21);ImGui::Text("%u",V[I]);ImGui::PopFont();ImGui::TextDisabled("%s",L[I]);}ImGui::EndTable();}EndCard();

    const float Half=(W-10)*.5f;for(int I=0;I<2;++I){if(I)ImGui::SameLine(0,10);ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,23);ImGui::PushStyleColor(ImGuiCol_ChildBg,IM_COL32(32,32,32,255));ImGui::PushStyleColor(ImGuiCol_Border,C.Visibility==I+1?IM_COL32(165,185,154,102):IM_COL32(255,255,255,8));ImGui::BeginChild(I?"##hidden-status":"##visible-status",{Half,109},ImGuiChildFlags_Borders);ImGui::SetCursorPos({17,17});D=ImGui::GetWindowDrawList();const auto P=ImGui::GetCursorScreenPos();D->AddCircleFilled({P.x+3.5f,P.y+6},3.5f,I?IM_COL32(160,135,111,255):IM_COL32(91,185,121,255),16);ImGui::Dummy({14,0});ImGui::SameLine();ImGui::TextUnformatted(I?"Hidden":"Visible");ImGui::SetCursorPos({17,52});ImGui::PushFont(Controls_->QueryDisplay(),40);char StatusText[32];CountText(StatusText,sizeof(StatusText),I?C.Total-C.Visible:C.Visible);ImGui::TextUnformatted(StatusText);ImGui::PopFont();if(ImGui::IsWindowHovered()&&ImGui::IsMouseClicked(0)){C.Visibility=C.Visibility==I+1?0:I+1;C.Page=0;}ImGui::EndChild();ImGui::PopStyleColor(2);ImGui::PopStyleVar();}ImGui::Dummy({0,7});

    struct CompositionType{const char* Name;uint32_t Count;int GlyphFilter;int CategoryFilter;};std::vector<CompositionType> CompositionTypes;const char* CategoryNames[]={"Folders","Geometry","Lights","Cameras"};for(unsigned I=0;I<4;++I)if(C.AutoCategories[I])CompositionTypes.push_back({CategoryNames[I],C.AutoCategories[I],0,int(I+1)});auto GlyphName=[](EditorGlyph G)->const char*{switch(G){case EditorGlyph::Atmosphere:return "Atmospheres";case EditorGlyph::AerialFog:return "Aerial fog";case EditorGlyph::VolumeFog:return "Height fog";case EditorGlyph::Fog:return "Local fog";case EditorGlyph::Cloud:return "Clouds";case EditorGlyph::VolumeClouds:return "Volume clouds";case EditorGlyph::LocalCloud:return "Local clouds";case EditorGlyph::Rain:return "Precipitation";case EditorGlyph::Wind:return "Wind";case EditorGlyph::Sun:return "Sun";case EditorGlyph::Sky:return "Sky";case EditorGlyph::Stars:return "Stars";case EditorGlyph::Moon:return "Moons";case EditorGlyph::Camera:return "Cameras";case EditorGlyph::Bulb:return "Lights";default:return nullptr;}};uint32_t OtherTypes=0;for(unsigned I=1;I<unsigned(EditorGlyph::Count);++I)if(C.Glyphs[I]){if(const char* Name=GlyphName(EditorGlyph(I)))CompositionTypes.push_back({Name,C.Glyphs[I],int(I),0});else OtherTypes+=C.Glyphs[I];}if(OtherTypes)CompositionTypes.push_back({"Other",OtherTypes,0,0});std::sort(CompositionTypes.begin(),CompositionTypes.end(),[](const CompositionType& A,const CompositionType& B){return A.Count!=B.Count?A.Count>B.Count:std::strcmp(A.Name,B.Name)<0;});
    const float CompositionH=170.f+float((CompositionTypes.size()+1)/2)*31.f;Card("##collection-composition",CompositionH);D=ImGui::GetWindowDrawList();{const ImVec2 P=ImGui::GetWindowPos();D->AddRectFilled({P.x+23,P.y+26},{P.x+26,P.y+41},IM_COL32(201,189,102,255),2);}ImGui::SetCursorPosX(35);ImGui::TextUnformatted("Composition");ImGui::SameLine(W-58);ImGui::TextDisabled("%zu types",CompositionTypes.size());ImGui::Dummy({0,18});
    const ImU32 Palette[]={IM_COL32(185,199,174,255),IM_COL32(156,171,185,255),IM_COL32(209,184,152,255),IM_COL32(166,161,152,255),IM_COL32(135,156,147,255)};float BX=ImGui::GetCursorScreenPos().x,BY=ImGui::GetCursorScreenPos().y,BW=ImGui::GetContentRegionAvail().x,DistributionW=std::max(1.f,BW-3.f*float(CompositionTypes.size()>0?CompositionTypes.size()-1:0));for(size_t I=0;I<CompositionTypes.size();++I){float Part=std::max(4.f,DistributionW*CompositionTypes[I].Count/std::max(1u,C.Total));const ImDrawFlags Corners=I==0?ImDrawFlags_RoundCornersLeft:I+1==CompositionTypes.size()?ImDrawFlags_RoundCornersRight:ImDrawFlags_None;D->AddRectFilled({BX,BY},{BX+Part,BY+12},Palette[I%std::size(Palette)],6,Corners);BX+=Part+3;}ImGui::Dummy({0,CompositionTypes.size()<=2?31.f:23.f});
    if(ImGui::BeginTable("##composition-types",2,ImGuiTableFlags_SizingStretchSame)){for(size_t I=0;I<CompositionTypes.size();++I){const auto& Type=CompositionTypes[I];ImGui::TableNextColumn();ImGui::PushID(int(I));const float CellW=ImGui::GetContentRegionAvail().x;const bool SelectedType=Type.GlyphFilter?C.GlyphFilter==Type.GlyphFilter:(Type.CategoryFilter&&C.Category==Type.CategoryFilter);if(ImGui::Selectable(Type.Name,SelectedType,0,{CellW,27})){if(Type.GlyphFilter){C.GlyphFilter=SelectedType?0:Type.GlyphFilter;C.Category=0;}else if(Type.CategoryFilter){C.Category=SelectedType?0:Type.CategoryFilter;C.GlyphFilter=0;}C.Page=0;}ImGui::SameLine(CellW-20);ImGui::Text("%u",Type.Count);ImGui::PopID();}ImGui::EndTable();}if(!C.Total)ImGui::TextDisabled("This folder is empty. Newly added children will appear here.");char ConstructedCount[24];CountText(ConstructedCount,sizeof(ConstructedCount),C.Constructed);char CompositionNote[192];std::snprintf(CompositionNote,sizeof(CompositionNote),"%s constructed preview markers · visibility includes ancestor folders. Counts are scene records, not render workload or memory usage.",ConstructedCount);ImGui::PushTextWrapPos(0);ImGui::TextDisabled("%s",CompositionNote);ImGui::PopTextWrapPos();EndCard();

    C.Traverse(Roster_,RosterCount_,Index);
    // The browse card grows with the page slice it draws: FolderInspector.jsx renders every entry on the page.
    const size_t Shown=std::min(C.Matches.size()-std::min(C.Matches.size(),size_t(C.Page)*C.PageSize),size_t(C.PageSize));
    Card("##collection-browser",381.f+67.f*float(Shown?Shown:1));D=ImGui::GetWindowDrawList();ImGui::PushStyleColor(ImGuiCol_FrameBg,IM_COL32(21,25,22,255));ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,IM_COL32(27,32,28,255));ImGui::PushStyleColor(ImGuiCol_FrameBgActive,IM_COL32(31,37,32,255));ImGui::PushStyleColor(ImGuiCol_Button,IM_COL32(21,25,22,255));ImGui::PushStyleColor(ImGuiCol_ButtonHovered,IM_COL32(27,32,28,255));ImGui::PushStyleColor(ImGuiCol_ButtonActive,IM_COL32(31,37,32,255));ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,10);ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{8,8});{const ImVec2 P=ImGui::GetWindowPos();D->AddRectFilled({P.x+23,P.y+26},{P.x+26,P.y+41},IM_COL32(176,199,207,255),2);}ImGui::SetCursorPosX(35);ImGui::TextUnformatted("Browse contents");ImGui::SameLine(W-86);char MatchText[48];char MatchCount[32];CountText(MatchCount,sizeof(MatchCount),uint32_t(C.Matches.size()));std::snprintf(MatchText,sizeof(MatchText),"%s matches",MatchCount);ImGui::TextDisabled("%s",MatchText);ImGui::Dummy({0,14});ImGui::TextDisabled("Find an entry");const ImVec2 SearchAt=ImGui::GetCursorScreenPos();D->AddRectFilled(SearchAt,{SearchAt.x+ImGui::GetContentRegionAvail().x,SearchAt.y+32},IM_COL32(21,25,22,255),10);ImGui::PushStyleColor(ImGuiCol_FrameBg,IM_COL32(0,0,0,0));ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,0);ImGui::SetNextItemWidth(-1);bool Changed=ImGui::InputTextWithHint("##search","Name, ID or description…",C.Search,sizeof(C.Search));ImGui::PopStyleVar();ImGui::PopStyleColor();ImGui::Dummy({0,8});
    const float FW=(ImGui::GetContentRegionAvail().x-8)*.5f;auto SelectBox=[&](const char* Id,const char* Value,float Width){const ImVec2 P=ImGui::GetCursorScreenPos();ImGui::PushID(Id);const bool Clicked=ImGui::InvisibleButton("##select",{Width,32});ImGui::PopID();D->AddRectFilled(P,{P.x+Width,P.y+32},IM_COL32(21,25,22,255),10);D->AddText(Controls_->QueryUi(),11,{P.x+10,P.y+9},kText,Value);D->AddLine({P.x+Width-15,P.y+13},{P.x+Width-10,P.y+18},IM_COL32(188,201,182,255),1.5f);D->AddLine({P.x+Width-10,P.y+18},{P.x+Width-5,P.y+13},IM_COL32(188,201,182,255),1.5f);if(Clicked)ImGui::OpenPopup(Id);return ImGui::BeginPopup(Id);};ImGui::TextDisabled("Scope");ImGui::SameLine(FW+8);ImGui::TextDisabled("Type");if(SelectBox("scope",C.DirectOnly?"Direct children":"All descendants",FW)){if(ImGui::Selectable("All descendants",!C.DirectOnly)){C.DirectOnly=false;Changed=true;}if(ImGui::Selectable("Direct children",C.DirectOnly)){C.DirectOnly=true;Changed=true;}ImGui::EndPopup();}ImGui::SameLine();const char* ActiveType="All types";for(const auto& Type:CompositionTypes)if((Type.GlyphFilter&&C.GlyphFilter==Type.GlyphFilter)||(Type.CategoryFilter&&C.Category==Type.CategoryFilter)){ActiveType=Type.Name;break;}if(SelectBox("type",ActiveType,FW)){if(ImGui::Selectable("All types",!C.Category&&!C.GlyphFilter)){C.Category=C.GlyphFilter=0;Changed=true;}for(const auto& Type:CompositionTypes){const bool Active=(Type.GlyphFilter&&C.GlyphFilter==Type.GlyphFilter)||(Type.CategoryFilter&&C.Category==Type.CategoryFilter);if(ImGui::Selectable(Type.Name,Active)){C.GlyphFilter=Type.GlyphFilter;C.Category=Type.CategoryFilter;Changed=true;}}ImGui::EndPopup();}ImGui::Dummy({0,8});ImGui::TextDisabled("Visibility");ImGui::SameLine(FW+8);ImGui::TextDisabled("Sort");const char* VisibilityNames[]={"All entries","Visible","Hidden"};if(SelectBox("visibility",VisibilityNames[std::clamp(C.Visibility,0,2)],FW)){for(int I=0;I<3;++I)if(ImGui::Selectable(VisibilityNames[I],C.Visibility==I)){C.Visibility=I;Changed=true;}ImGui::EndPopup();}ImGui::SameLine();const char* SortNames[]={"Name A–Z","Type","Hierarchy depth"};if(SelectBox("sort",SortNames[std::clamp(C.Sort,0,2)],FW)){for(int I=0;I<3;++I)if(ImGui::Selectable(SortNames[I],C.Sort==I)){C.Sort=I;Changed=true;}ImGui::EndPopup();}ImGui::Dummy({0,5});if(Changed)C.Page=0;C.Traverse(Roster_,RosterCount_,Index);
    const size_t Start=size_t(C.Page)*C.PageSize,Last=std::min(C.Matches.size(),Start+size_t(C.PageSize));for(size_t J=Start;J<Last;++J){const auto M=C.Matches[J];auto& R=Roster_[M.Index];ImGui::PushID(int(M.Index));const float RowW=ImGui::GetContentRegionAvail().x;ImGui::InvisibleButton("##entry",{RowW-44,67});if(ImGui::IsItemClicked())CollectionPick_=M.Index;const ImVec2 RM=ImGui::GetItemRectMin();D->AddLine({RM.x,RM.y+66},{RM.x+RowW,RM.y+66},IM_COL32(255,255,255,10));const ImU32 RC=R.Category==EditorInstanceCategory::Light?IM_COL32(209,184,152,255):R.Category==EditorInstanceCategory::Camera?IM_COL32(156,171,185,255):R.Category==EditorInstanceCategory::Folder?IM_COL32(185,199,174,255):IM_COL32(18,111,194,255);const ImVec2 CubeC={RM.x+67.5f,RM.y+33.5f};const ImVec2 Cube[]={ {CubeC.x,CubeC.y-7},{CubeC.x+6,CubeC.y-3.5f},{CubeC.x+6,CubeC.y+4},{CubeC.x,CubeC.y+7.5f},{CubeC.x-6,CubeC.y+4},{CubeC.x-6,CubeC.y-3.5f} };D->AddTriangleFilled(Cube[0],Cube[1],CubeC,IM_COL32(18,111,194,80));D->AddTriangleFilled(Cube[0],CubeC,Cube[5],IM_COL32(18,111,194,55));D->AddQuadFilled(CubeC,Cube[1],Cube[2],Cube[3],IM_COL32(18,111,194,42));D->AddQuadFilled(Cube[5],CubeC,Cube[3],Cube[4],IM_COL32(18,111,194,28));D->AddPolyline(Cube,6,RC,1.2f,ImDrawFlags_Closed);D->AddLine(Cube[0],CubeC,RC,1);D->AddLine(CubeC,Cube[3],RC,1);D->AddLine(CubeC,Cube[1],RC,1);D->AddLine(CubeC,Cube[5],RC,1);D->AddText(Controls_->QueryUi(),13,{RM.x+87,RM.y+11},kText,R.Label);char Meta[64];std::snprintf(Meta,sizeof(Meta),"%s · level %u",CollectionTypeName(PanelOfRow(R.Glyph,R.Category,R.Pinned)),R.Depth-Selected.Depth);D->AddText(Controls_->QuerySmall(),10,{RM.x+87,RM.y+31},kDim,Meta);const char* Parent="scene";for(uint32_t K=M.Index;K>0;){--K;if(Roster_[K].Depth<R.Depth){Parent=Roster_[K].Label;break;}}char VisibilityText[96];std::snprintf(VisibilityText,sizeof(VisibilityText),"%s · %s",M.Visible?"Visible":(M.Own?"Hidden by ancestor":"Hidden"),(!std::strcmp(Selected.Label,"Asset archive")&&R.Meta[0])?R.Meta:Parent);D->AddText(Controls_->QuerySmall(),9,{RM.x+87,RM.y+46},kFaint,VisibilityText);ImGui::SameLine();ImGui::InvisibleButton("##entry-visibility",{38,67});if(ImGui::IsItemClicked()&&!R.Pinned)R.Visible=!R.Visible;const ImVec2 ToggleMin=ImGui::GetItemRectMin(),ToggleC={ToggleMin.x+19,ToggleMin.y+33.5f};D->AddCircleFilled(ToggleC,14,M.Visible?IM_COL32(44,48,40,255):IM_COL32(48,48,48,255),24);D->AddCircleFilled(ToggleC,3.5f,M.Visible?IM_COL32(91,185,121,255):IM_COL32(122,122,122,255),16);ImGui::PopID();}
    if(C.Matches.empty()){ImGui::TextDisabled("No matching entries.");ImGui::SameLine(0,10);if(ImGui::Button("Clear filters")){C.Search[0]=0;C.Category=0;C.GlyphFilter=0;C.Visibility=0;C.DirectOnly=false;C.Page=0;}}const int Pages=std::max(1,(int(C.Matches.size())+C.PageSize-1)/C.PageSize);char PageRange[64],MatchTotal[24];CountText(MatchTotal,sizeof(MatchTotal),uint32_t(C.Matches.size()));std::snprintf(PageRange,sizeof(PageRange),"%zu–%zu of %s",C.Matches.empty()?0:Start+1,std::min(Last,C.Matches.size()),MatchTotal);ImGui::TextDisabled("%s",PageRange);ImGui::Dummy({0,6});auto PageArrow=[&](const char* Id,bool Forward,bool Enabled){const ImVec2 P=ImGui::GetCursorScreenPos();ImGui::PushID(Id);const bool Pressed=ImGui::InvisibleButton("##page",{28,26})&&Enabled;ImGui::PopID();D->AddRectFilled(P,{P.x+28,P.y+26},Enabled?IM_COL32(34,38,34,255):IM_COL32(25,25,25,255),8);const float Sign=Forward?1.f:-1.f;D->AddTriangleFilled({P.x+14+Sign*4,P.y+7},{P.x+14+Sign*4,P.y+19},{P.x+14-Sign*3,P.y+13},Enabled?IM_COL32(190,201,185,255):IM_COL32(80,80,80,255));return Pressed;};if(PageArrow("previous",false,C.Page>0))--C.Page;ImGui::SameLine(0,5);ImGui::TextDisabled("%d / %d",C.Page+1,Pages);ImGui::SameLine(0,5);if(PageArrow("next",true,Last<C.Matches.size()))++C.Page;ImGui::SameLine(0,5);char PageSizeText[8];std::snprintf(PageSizeText,sizeof(PageSizeText),"%d",C.PageSize);if(SelectBox("page-size",PageSizeText,50)){for(int Size: {25,50,100}){char SizeText[8];std::snprintf(SizeText,sizeof(SizeText),"%d",Size);if(ImGui::Selectable(SizeText,C.PageSize==Size)){C.PageSize=Size;C.Page=0;}}ImGui::EndPopup();}ImGui::PopStyleVar(2);ImGui::PopStyleColor(6);EndCard();
    Card("##collection-notes",190);D=ImGui::GetWindowDrawList();{const ImVec2 P=ImGui::GetWindowPos();D->AddRectFilled({P.x+23,P.y+26},{P.x+26,P.y+41},IM_COL32(210,167,112,255),2);}ImGui::SetCursorPosX(35);ImGui::TextUnformatted("Collection notes");ImGui::Dummy({0,16});ImGui::TextDisabled("Collection accent");ImGui::SameLine(W-62);const ImVec2 TintAt=ImGui::GetCursorScreenPos();if(ImGui::InvisibleButton("##collection-tint",{31,26}))ImGui::OpenPopup("##collection-tint-picker");D->AddRectFilled(TintAt,{TintAt.x+31,TintAt.y+26},IM_COL32(int(std::clamp(Selected.Tint[0],0.f,1.f)*255),int(std::clamp(Selected.Tint[1],0.f,1.f)*255),int(std::clamp(Selected.Tint[2],0.f,1.f)*255),255),5);if(ImGui::BeginPopup("##collection-tint-picker")){ImGui::ColorPicker3("##rgb",Selected.Tint,ImGuiColorEditFlags_NoAlpha|ImGuiColorEditFlags_NoSidePreview);ImGui::EndPopup();}ImGui::Dummy({0,12});ImGui::TextDisabled("Identity colour only · child objects are unchanged.");ImGui::TextDisabled("ID · %llu",static_cast<unsigned long long>(Selected.InspectorKey));EndCard();
    ImGui::Unindent(7);ImGui::PopFont();ImGui::PopID();
}
void InspectorPanel::RecordEmpty() noexcept
{
    const float RowWidth = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(RowWidth, 120.0f));
    const ImVec2 Min = ImGui::GetItemRectMin();
    const ImVec2 Max = ImGui::GetItemRectMax();

    ImDrawList* Draw = ImGui::GetWindowDrawList();
    Draw->AddRectFilled(Min, Max, kInset, 18.0f);
    Draw->AddRect(Min, Max, kStroke, 18.0f);

    ImFont* Ui    = Controls_->QueryUi();
    ImFont* Small = Controls_->QuerySmall();
    ImGui::PushFont(Ui);
    const ImVec2 TitleGlyph = Ui->CalcTextSizeA(Ui->LegacySize, FLT_MAX, 0.0f, "Nothing selected");
    Draw->AddText(ImVec2(Min.x + (RowWidth - TitleGlyph.x) * 0.5f, Min.y + 38.0f), kDim, "Nothing selected");
    ImGui::PopFont();
    ImGui::PushFont(Small);
    const ImVec2 HintGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, "Pick an instance in the outliner.");
    Draw->AddText(ImVec2(Min.x + (RowWidth - HintGlyph.x) * 0.5f, Min.y + 62.0f), kFaint,
        "Pick an instance in the outliner.");
    ImGui::PopFont();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                           IDENT
//------------------------------------------------------------------------------------------------------------------------

void InspectorPanel::RecordIdent(EditorInstance* Picked, uint32_t PickedIndex) noexcept
{
    const float RowWidth = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(RowWidth, 56.0f));
    const ImVec2 Cursor = ImGui::GetItemRectMin();

    ImDrawList* Draw  = ImGui::GetWindowDrawList();
    ImFont*     Ui    = Controls_->QueryUi();
    ImFont*     Small = Controls_->QuerySmall();

    const ImVec2 HeadPos = ImGui::GetWindowPos();
    const float  HeadX0  = HeadPos.x;
    const float  HeadX1  = HeadPos.x + ImGui::GetWindowSize().x;
    Draw->AddRectFilled(ImVec2(HeadX0, Cursor.y), ImVec2(HeadX1, Cursor.y + 56.0f), kWash);
    Draw->AddLine(ImVec2(HeadX0, Cursor.y + 56.0f), ImVec2(HeadX1, Cursor.y + 56.0f), kStroke);

    const int R = static_cast<int>(Picked->Tint[0] * 255.0f);
    const int G = static_cast<int>(Picked->Tint[1] * 255.0f);
    const int B = static_cast<int>(Picked->Tint[2] * 255.0f);
    const ImVec2 TileMin(Cursor.x, Cursor.y + 10.0f);
    const ImVec2 TileMax(Cursor.x + 36.0f, Cursor.y + 46.0f);
    Draw->AddRectFilled(TileMin, TileMax, IM_COL32(R, G, B, 36), 10.0f);
    Draw->AddRect(TileMin, TileMax, IM_COL32(R, G, B, 110), 10.0f);
    Draw->AddCircleFilled(ImVec2(Cursor.x + 18.0f, Cursor.y + 28.0f), 5.0f, IM_COL32(R, G, B, 255));

    if (NameFor_ != PickedIndex)
    {
        std::snprintf(NameText_, sizeof(NameText_), "%s", Picked->Label);
        NameFor_ = PickedIndex;
    }

    const float NameX  = Cursor.x + 46.0f;
    const float BtnX   = Cursor.x + RowWidth - 64.0f;
    const float NameW  = BtnX - 8.0f - NameX;
    ImGui::SetCursorScreenPos(ImVec2(NameX, Cursor.y + 4.0f));
    ImGui::PushItemWidth(NameW > 40.0f ? NameW : 40.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 2.0f));
    ImGui::PushFont(Ui);
    ImGui::BeginDisabled(Picked->Pinned);
    const bool NameDone = ImGui::InputText("##pickname", NameText_, sizeof(NameText_),
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    const bool NameEdited = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::EndDisabled();
    const ImVec2 NameMin = ImGui::GetItemRectMin();
    const ImVec2 NameMax = ImGui::GetItemRectMax();
    const bool NameHot = ImGui::IsItemFocused();
    ImGui::PopFont();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImGui::PopItemWidth();
    if (NameHot)
    {
        Draw->AddRect(NameMin, NameMax, kStrong, 6.0f);
    }
    if (!Picked->Pinned && (NameDone || NameEdited))
    {
        std::snprintf(Picked->Label, sizeof(Picked->Label), "%.*s", int(sizeof(Picked->Label) - 1u), NameText_);
    }

    char CategoryUpper[24] = {};
    const char* CategoryName = EditorInstanceLabel(Picked->Category);
    for (uint32_t i = 0u; i < sizeof(CategoryUpper) - 1u && CategoryName[i] != '\0'; ++i)
    {
        CategoryUpper[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(CategoryName[i])));
    }
    char Suffix[32] = {};
    if (Picked->Locked && !Picked->Visible)
    {
        std::snprintf(Suffix, sizeof(Suffix), " \xc2\xb7 locked \xc2\xb7 hidden");
    }
    else if (Picked->Locked)
    {
        std::snprintf(Suffix, sizeof(Suffix), " \xc2\xb7 locked");
    }
    else if (!Picked->Visible)
    {
        std::snprintf(Suffix, sizeof(Suffix), " \xc2\xb7 hidden");
    }
    const float CategoryAdvance = RecordCaps(CategoryUpper, ImVec2(NameX + 2.0f, Cursor.y + 30.0f), kDim);
    ImGui::PushFont(Small);
    Draw->AddText(ImVec2(NameX + 2.0f + CategoryAdvance, Cursor.y + 30.0f), kFaint, Suffix);
    ImGui::PopFont();

    const ImVec2 LockMin(BtnX, Cursor.y + 14.0f);
    ImGui::SetCursorScreenPos(LockMin);
    ImGui::InvisibleButton("##identlock", ImVec2(28.0f, 28.0f));
    const bool LockHot = ImGui::IsItemHovered();
    if (!Picked->Pinned && LockHot && ImGui::IsMouseClicked(0))
    {
        Picked->Locked = !Picked->Locked;
    }
    const ImVec2 VisMin(BtnX + 36.0f, Cursor.y + 14.0f);
    ImGui::SetCursorScreenPos(VisMin);
    ImGui::InvisibleButton("##identvis", ImVec2(28.0f, 28.0f));
    const bool VisHot = ImGui::IsItemHovered();
    if (!Picked->Pinned && VisHot && ImGui::IsMouseClicked(0))
    {
        Picked->Visible = !Picked->Visible;
    }

    const ImVec2 LockCentre(LockMin.x + 14.0f, LockMin.y + 14.0f);
    Draw->AddCircleFilled(LockCentre, 14.0f, LockHot ? IM_COL32(255, 255, 255, 24) : IM_COL32(255, 255, 255, 12));
    const ImU32 LockTint = Picked->Locked ? kText : kFaint;
    Draw->AddCircle(ImVec2(LockCentre.x, LockCentre.y - 1.5f), 3.2f, LockTint, 0, 1.6f);
    Draw->AddRectFilled(ImVec2(LockCentre.x - 3.8f, LockCentre.y - 1.0f),
        ImVec2(LockCentre.x + 3.8f, LockCentre.y + 5.0f), IM_COL32(30, 30, 30, 255), 2.0f);
    Draw->AddRect(ImVec2(LockCentre.x - 3.8f, LockCentre.y - 1.0f),
        ImVec2(LockCentre.x + 3.8f, LockCentre.y + 5.0f), LockTint, 2.0f, 0, 1.4f);

    const ImVec2 VisCentre(VisMin.x + 14.0f, VisMin.y + 14.0f);
    Draw->AddCircleFilled(VisCentre, 14.0f, VisHot ? IM_COL32(255, 255, 255, 24) : IM_COL32(255, 255, 255, 12));
    const ImU32 VisTint = Picked->Visible ? kDim : kFaint;
    Draw->AddBezierCubic(ImVec2(VisCentre.x - 6.0f, VisCentre.y),
        ImVec2(VisCentre.x - 2.5f, VisCentre.y - 4.5f), ImVec2(VisCentre.x + 2.5f, VisCentre.y - 4.5f),
        ImVec2(VisCentre.x + 6.0f, VisCentre.y), VisTint, 1.6f);
    Draw->AddBezierCubic(ImVec2(VisCentre.x - 6.0f, VisCentre.y),
        ImVec2(VisCentre.x - 2.5f, VisCentre.y + 4.5f), ImVec2(VisCentre.x + 2.5f, VisCentre.y + 4.5f),
        ImVec2(VisCentre.x + 6.0f, VisCentre.y), VisTint, 1.6f);
    if (Picked->Visible)
    {
        Draw->AddCircleFilled(VisCentre, 1.8f, VisTint);
    }
    else
    {
        Draw->AddLine(ImVec2(VisCentre.x - 6.5f, VisCentre.y + 6.0f),
            ImVec2(VisCentre.x + 6.5f, VisCentre.y - 6.0f), kFaint, 1.6f);
    }
    ImGui::SetCursorScreenPos(ImVec2(Cursor.x, Cursor.y + 56.0f));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      SCHEMA CARDS
//------------------------------------------------------------------------------------------------------------------------

void InspectorPanel::RecordCard(EditorPropertyGroup& Group, uint32_t Card) noexcept
{
    ImGui::PushID(static_cast<int>(Card));

    const float RowWidth = ImGui::GetContentRegionAvail().x;
    ImFont* CaptionFace = Controls_->QuerySmall();
    const float Wrap = ImMax(1.0f, RowWidth - 28.0f);
    const float CaptionH = Group.Caption[0] ? CaptionFace->CalcTextSizeA(CaptionFace->LegacySize, FLT_MAX, Wrap, Group.Caption).y + 12.0f : 0.0f;
    const float ClockH = Group.Clock24 ? 126.0f : 0.0f;
    const float LabelH = Group.StackedLabels ? 18.0f : 0.0f;
    float H = 12.0f + 24.0f + 10.0f;
    if (!CardShut_[Card])
    {
        H = 12.0f + 24.0f + 8.0f;
        for (uint32_t i = 0u; i < Group.PropertyCount; ++i)
        {
            H += ProwHeight(Group.Properties[i].Category) + 8.0f + LabelH;
        }
        H += 14.0f - 8.0f + CaptionH + ClockH;
    }

    ImGui::Dummy(ImVec2(RowWidth, H));
    const ImVec2 Min = ImGui::GetItemRectMin();
    const ImVec2 Max = ImGui::GetItemRectMax();

    ImDrawList* Draw = ImGui::GetWindowDrawList();
    Draw->AddRectFilled(Min, Max, kInset, 18.0f);
    Draw->AddRect(Min, Max, kStroke, 18.0f);

    const ImVec2 HeadMin(Min.x + 14.0f, Min.y + 12.0f);
    ImGui::SetCursorScreenPos(HeadMin);
    ImGui::InvisibleButton("##h4", ImVec2(RowWidth - 28.0f, 24.0f));
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0))
    {
        CardShut_[Card] = !CardShut_[Card];
    }

    const float ChevY = HeadMin.y + 12.0f;
    if (CardShut_[Card])
    {
        Draw->AddTriangleFilled(ImVec2(HeadMin.x + 1.0f, ChevY - 4.0f), ImVec2(HeadMin.x + 1.0f, ChevY + 4.0f),
            ImVec2(HeadMin.x + 7.0f, ChevY), kDim);
    }
    else
    {
        Draw->AddTriangleFilled(ImVec2(HeadMin.x - 1.0f, ChevY - 2.5f), ImVec2(HeadMin.x + 9.0f, ChevY - 2.5f),
            ImVec2(HeadMin.x + 4.0f, ChevY + 3.5f), kDim);
    }
    RecordCaps(Group.Title, ImVec2(HeadMin.x + 14.0f, HeadMin.y + 5.0f), kDim);

    if (!CardShut_[Card])
    {
        ImFont* Small = Controls_->QuerySmall();
        const float BodyX = Min.x + 14.0f;
        const float BodyW = RowWidth - 28.0f;
        const float ZoneX = Group.StackedLabels ? BodyX : BodyX + 78.0f;
        const float ZoneW = Group.StackedLabels ? BodyW : BodyW - 78.0f;
        float Y = Min.y + 12.0f + 24.0f + 8.0f;
        if (Group.Caption[0])
        {
            Draw->AddText(Small, Small->LegacySize, ImVec2(BodyX, Y), kDim, Group.Caption, nullptr, Wrap);
            Y += CaptionH;
        }
        if (Group.Clock24 && Group.PropertyCount)
        {
            const ImVec2 Centre(BodyX + BodyW * 0.5f, Y + 60.0f);
            Draw->AddCircle(Centre, 43.0f, kStroke, 48, 2.0f);
            for (int Hour = 0; Hour < 24; ++Hour)
            {
                const float A = Hour * 6.283185307f / 24.0f - 1.570796327f;
                const float Outer = 43.0f, Inner = Hour % 6 == 0 ? 35.0f : 39.0f;
                Draw->AddLine(ImVec2(Centre.x + std::cos(A)*Inner, Centre.y + std::sin(A)*Inner),
                    ImVec2(Centre.x + std::cos(A)*Outer, Centre.y + std::sin(A)*Outer), kDim);
            }
            const float A = Group.Properties[0].Figure * 6.283185307f / 24.0f - 1.570796327f;
            Draw->AddCircleFilled(ImVec2(Centre.x + std::cos(A)*43.0f, Centre.y + std::sin(A)*43.0f), 5.0f, IM_COL32(245, 183, 92, 255));
            Draw->AddText(Small, Small->LegacySize, ImVec2(Centre.x-5, Centre.y-60), kDim, "00");
            Draw->AddText(Small, Small->LegacySize, ImVec2(Centre.x+49, Centre.y-5), kDim, "06");
            Draw->AddText(Small, Small->LegacySize, ImVec2(Centre.x-5, Centre.y+47), kDim, "12");
            Draw->AddText(Small, Small->LegacySize, ImVec2(Centre.x-62, Centre.y-5), kDim, "18");
            Y += ClockH;
        }


        for (uint32_t i = 0u; i < Group.PropertyCount; ++i)
        {
            EditorProperty& Prop = Group.Properties[i];
            const float ProwH = ProwHeight(Prop.Category);

            ImGui::PushFont(Small);
            const ImVec2 LabelGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, Prop.Label);
            Draw->AddText(ImVec2(BodyX, Y + (Group.StackedLabels ? 0.0f : (ProwH - LabelGlyph.y) * 0.5f)), kDim, Prop.Label);
            ImGui::PopFont();

            Y += LabelH;
            ImGui::SetCursorScreenPos(ImVec2(ZoneX, Y));
            char ProwId[12] = {};
            std::snprintf(ProwId, sizeof(ProwId), "##p%u", i);
            ImGui::BeginChild(ProwId, ImVec2(ZoneW, ProwH), false,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            switch (Prop.Category)
            {
            case EditorPropertyCategory::Slider:
                Controls_->SliderPill("##s", &Prop.Figure, Prop.Minimum, Prop.Maximum, Prop.Decimals, Prop.Unit, Prop.Hi, false, true);
                break;
            case EditorPropertyCategory::Switch:
                ImGui::SetCursorScreenPos(ImVec2(ZoneX + ZoneW - 44.0f, Y + 3.5f));
                Controls_->Switch("##w", &Prop.On);
                break;
            case EditorPropertyCategory::AxisVec3:
                Controls_->AxisVec3("##v", Prop.Axes, Prop.AxisStep, Prop.Editable);
                break;
            case EditorPropertyCategory::Colour:
                if (Prop.Swatches)
                {
                    Controls_->SwatchRow("##t", Prop.ColourTint);
                }
                else
                {
                    Controls_->ColourChip("##c", Prop.ColourTint);
                }
                break;
            case EditorPropertyCategory::Select:
                Controls_->DropDown("##d", &Prop.Picked, Prop.Options, Prop.OptionCount);
                break;
            case EditorPropertyCategory::Readout:
                Controls_->Readout(Prop.Text);
                break;
            default:
                break;
            }
            ImGui::EndChild();

            Y += ProwH + 8.0f;
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(Min.x, Max.y));
    ImGui::Dummy(ImVec2(RowWidth, 10.0f));
    ImGui::PopID();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      RECORD STANDING
//------------------------------------------------------------------------------------------------------------------------

void InspectorPanel::RecordStanding(EditorInstance* Picked, uint32_t PickedIndex) noexcept
{
    ImGui::PushID(6);

    const float RowWidth = ImGui::GetContentRegionAvail().x;
    constexpr float kH = 130.0f;
    ImGui::Dummy(ImVec2(RowWidth, kH));
    const ImVec2 Min = ImGui::GetItemRectMin();
    const ImVec2 Max = ImGui::GetItemRectMax();

    ImDrawList* Draw = ImGui::GetWindowDrawList();
    Draw->AddRectFilled(Min, Max, kInset, 18.0f);
    Draw->AddRect(Min, Max, kStroke, 18.0f);

    RecordCaps("Instance", ImVec2(Min.x + 14.0f, Min.y + 14.0f), kDim);

    ImFont* Small = Controls_->QuerySmall();
    const char* Pills[4] = { "VISIBLE", "LOCKED", "DYNAMIC", "PHYSICS" };
    bool* Standing[4] = { &Picked->Visible, &Picked->Locked, &Picked->Dynamic, &Picked->Physics };
    float PX = Min.x + 14.0f;
    const float PY = Min.y + 40.0f;
    for (uint32_t i = 0u; i < 4u; ++i)
    {
        ImGui::PushFont(Small);
        const ImVec2 Glyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, Pills[i]);
        ImGui::PopFont();
        ImGui::SetCursorScreenPos(ImVec2(PX, PY));
        ImGui::PushID(static_cast<int>(10 + i));
        ImGui::BeginDisabled(Picked->Pinned);
        Controls_->PillToggle(Pills[i], Standing[i]);
        ImGui::EndDisabled();
        ImGui::PopID();
        PX += Glyph.x + 20.0f + 8.0f;
    }

    const float BodyX = Min.x + 14.0f;
    const float ZoneX = BodyX + 78.0f;
    const float ZoneW = RowWidth - 28.0f - 78.0f;
    float Y = Min.y + 72.0f;

    ImGui::PushFont(Small);
    Draw->AddText(ImVec2(BodyX, Y), kDim, "TYPE");
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(ZoneX, Y));
    ImGui::BeginChild("##spectype", ImVec2(ZoneW, 18.0f), false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    Controls_->Readout(EditorInstanceLabel(Picked->Category));
    ImGui::EndChild();
    Y += 24.0f;

    char IdText[8] = {};
    std::snprintf(IdText, sizeof(IdText), "#%03u", PickedIndex);
    ImGui::PushFont(Small);
    Draw->AddText(ImVec2(BodyX, Y), kDim, "ID");
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(ZoneX, Y));
    ImGui::BeginChild("##specid", ImVec2(ZoneW, 18.0f), false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    Controls_->Readout(IdText);
    ImGui::EndChild();

    ImGui::SetCursorScreenPos(ImVec2(Min.x, Max.y));
    ImGui::Dummy(ImVec2(RowWidth, 10.0f));
    ImGui::PopID();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          NOTES
//------------------------------------------------------------------------------------------------------------------------

void InspectorPanel::RecordNotes(EditorInstance* Picked) noexcept
{
    namespace EN = EntityNotes;
    ImGui::PushID(7);

    ImFont* Light = ImGui::GetFont();
    ImFont* Regular = Light;
    for (ImFont* Face : ImGui::GetIO().Fonts->Fonts)
    {
        if (!std::strcmp(Face->GetDebugName(), "Sun reference / light"))   Light = Face;
        if (!std::strcmp(Face->GetDebugName(), "Sun reference / regular")) Regular = Face;
    }

    // useState(Boolean(Value)): a subject that already carries a note opens showing it. CardShut_[7]
    //    remembers a deliberate Hide for as long as the selection lasts.
    if (Picked->Notes[0] && !NotesSeen_) { CardShut_[7] = false; NotesSeen_ = true; }
    if (!Picked->Notes[0] && !NotesSeen_) { CardShut_[7] = true; NotesSeen_ = true; }

    const float RowWidth = ImGui::GetContentRegionAvail().x;
    ImDrawList* Draw = ImGui::GetWindowDrawList();

    if (CardShut_[7])
    {
        // Closed: the dashed add button, pinned right as .inspector-heading > .entity-notes-add is.
        const float Wide = EN::AddWidth(Light);
        ImGui::Dummy(ImVec2(RowWidth, EN::AddTall));
        const ImVec2 Min = ImGui::GetItemRectMin();
        const ImVec2 Spot(Min.x + RowWidth - Wide, Min.y);
        ImGui::SetCursorScreenPos(Spot);
        const bool Hit = ImGui::InvisibleButton("##add-notes", ImVec2(Wide, EN::AddTall));
        EN::PaintAdd(Draw, Light, Spot, ImGui::IsItemHovered());
        if (Hit) CardShut_[7] = false;
    }
    else
    {
        const float Tall = EN::PanelHeight();
        ImGui::Dummy(ImVec2(RowWidth, Tall));
        const ImVec2 Min = ImGui::GetItemRectMin();
        EN::PaintPanel(Draw, Light, Regular, Min, RowWidth, Picked->Notes, NotesFocus_);

        const float HideSpan = EN::HideWidth(Light);
        ImGui::SetCursorScreenPos(ImVec2(Min.x + RowWidth - 1.0f - EN::PanelPad - HideSpan,
                                         Min.y + 1.0f + EN::PanelPad
                                         + (EN::RowHeight() - EN::HideTall) * 0.5f));
        if (ImGui::InvisibleButton("##hide-notes", ImVec2(HideSpan, EN::HideTall))) CardShut_[7] = true;

        // The field itself stays a real ImGui control so the note remains editable; it is drawn
        //    transparent over the painted box so the reference's own geometry is what shows.
        const float FieldX = Min.x + 1.0f + EN::PanelPad;
        const float FieldW = RowWidth - 2.0f - EN::PanelPad * 2.0f;
        ImGui::SetCursorScreenPos(ImVec2(FieldX, Min.y + 1.0f + EN::PanelPad + EN::RowHeight() + EN::RowDrop));
        ImGui::PushItemWidth(FieldW);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(EN::FieldPad, EN::FieldPad));
        ImGui::PushFont(Light, EN::FieldSize * Frontier::WindInstrument::EmScale);
        ImGui::InputTextMultiline("##notes", Picked->Notes, sizeof(Picked->Notes),
                                  ImVec2(FieldW, EN::FieldTall));
        NotesFocus_ = ImGui::IsItemFocused();
        ImGui::PopFont();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
        ImGui::PopItemWidth();
    }

    ImGui::Dummy(ImVec2(RowWidth, 10.0f));
    ImGui::PopID();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                           FOOTER
//------------------------------------------------------------------------------------------------------------------------

void InspectorPanel::RecordFooter(EditorInstance* Picked) noexcept
{
    const float RowWidth = ImGui::GetContentRegionAvail().x;
    // Pinned to the sill: whatever the content above ends at, the foot opens on the shared top row.
    const float FootTop = Controls_->QueryFootTop();
    if (ImGui::GetCursorScreenPos().y < FootTop)
    {
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, FootTop));
    }
    ImGui::Dummy(ImVec2(RowWidth, kEditorFooterH));
    const ImVec2 Cursor = ImGui::GetItemRectMin();

    ImDrawList* Draw = ImGui::GetWindowDrawList();
    const ImVec2 FootPos  = ImGui::GetWindowPos();
    const ImVec2 FootSize = ImGui::GetWindowSize();
    const float  FootX0   = FootPos.x;
    const float  FootX1   = FootPos.x + FootSize.x;
    const float  FootH    = kEditorFooterH;
    Draw->AddRectFilled(ImVec2(FootX0, Cursor.y), ImVec2(FootX1, Cursor.y + FootH), kWash);
    Draw->AddLine(ImVec2(FootX0, Cursor.y), ImVec2(FootX1, Cursor.y), kStroke);

    char Foot[64] = {};
    if (Picked == nullptr)
    {
        std::snprintf(Foot, sizeof(Foot), "\xe2\x80\x94");
    }
    else if (Picked->Locked)
    {
        std::snprintf(Foot, sizeof(Foot), "%s \xc2\xb7 %s \xc2\xb7 locked",
            EditorInstanceLabel(Picked->Category), Picked->Dynamic ? "dynamic" : "static");
    }
    else
    {
        std::snprintf(Foot, sizeof(Foot), "%s \xc2\xb7 %s",
            EditorInstanceLabel(Picked->Category), Picked->Dynamic ? "dynamic" : "static");
    }
    ImFont* Small = Controls_->QuerySmall();
    ImGui::PushFont(Small);
    const ImVec2 FootGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, Foot);
    Draw->AddText(ImVec2(FootX0 + 14.0f, Cursor.y + (FootH - FootGlyph.y) * 0.5f), kFaint, Foot);
    ImGui::PopFont();

    // The live figures, right-hung: the tinted realtime, the triangle total, and the warning
    //    triangle off a Poor band. Unseated they read dashes, like the outliner's resting figures.
    const char* Dash = "\xe2\x80\x94";
    char FpsFig[12] = {}, TrisFig[16] = {};
    const char*   FpsText  = Dash;
    const char*   TrisText = Dash;
    EditorFpsBand Band     = EditorFpsBand::Fair;
    if (Readout_ != nullptr)
    {
        std::snprintf(FpsFig, sizeof(FpsFig), "%.0f", static_cast<double>(Readout_->Fps));
        FpsText = FpsFig;
        Band    = EditorFpsBandFor(Readout_->Fps);
        if (Readout_->Triangles > 0u)
        {
            std::snprintf(TrisFig, sizeof(TrisFig), "%u", Readout_->Triangles);
            TrisText = TrisFig;
        }
    }
    const ImU32 FpsTint = (Readout_ == nullptr) ? kFaint
        : (Band == EditorFpsBand::Good ? kGreen : (Band == EditorFpsBand::Poor ? kAmber : kText));
    ImGui::PushFont(Small);
    const float FpsW  = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, FpsText).x;
    const float TrisW = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, TrisText).x;
    const float SepW  = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, " \xc2\xb7 ").x;
    ImGui::PopFont();
    const float FpsLabW  = CapsAdvance(Small, "FPS");
    const float TrisLabW = CapsAdvance(Small, "TRIS");
    const bool  Poor     = (Readout_ != nullptr) && (Band == EditorFpsBand::Poor);
    const float BlockW = FpsLabW + 6.0f + FpsW + SepW + TrisLabW + 6.0f + TrisW + (Poor ? 13.0f : 0.0f);
    float       RX     = FootX1 - 14.0f - BlockW;
    const float TextY  = Cursor.y + (FootH - FootGlyph.y) * 0.5f;
    RX += RecordCaps("FPS", ImVec2(RX, TextY), kFaint) + 6.0f;
    ImGui::PushFont(Small);
    Draw->AddText(ImVec2(RX, TextY), FpsTint, FpsText);
    RX += FpsW;
    Draw->AddText(ImVec2(RX, TextY), kFaint, " \xc2\xb7 ");
    RX += SepW;
    ImGui::PopFont();
    RX += RecordCaps("TRIS", ImVec2(RX, TextY), kFaint) + 6.0f;
    ImGui::PushFont(Small);
    Draw->AddText(ImVec2(RX, TextY), (TrisText == Dash) ? kFaint : kText, TrisText);
    ImGui::PopFont();
    RX += TrisW;
    if (Poor)
    {
        FootWarn(Draw, ImVec2(RX + 3.0f, Cursor.y + (FootH - 10.0f) * 0.5f), 10.0f, kAmber);
    }
}

} // namespace Frontier
