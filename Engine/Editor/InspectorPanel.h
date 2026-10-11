//============================================================================================================================================
//                                                    INSPECTORPANEL.H
//============================================================================================================================================
// 🧩 Development editor inspector — the picked instance as a property sheet. Ident strip, schema cards drawn from
//    the project's sheet, the instance standing, the notes card. Every control edits the project's own figures.

#pragma once

#include "EditorInstance.h"
#include "CollectionSequence.h"
#include "FractureCardSurface.h"
#include "GasCardSurface.h"
#include "ForceFieldCardSurface.h"

#include <imgui.h>

#include <cstdint>

namespace Frontier {

class ControlPanel;

// One slot of per-object fracture authoring, matched to its instance. The browser keeps these in
//    localStorage under a scene ID; the panel keeps them beside the instance until the engine owns a
//    fracture component of its own.
struct FractureRecord
{
    uint32_t           For = kNoEditorInstance;
    Fracture::Settings Recipe {};
};

// One gas domain as the inspector is editing it, matched to its instance. The browser keeps this in the
//    scene's values; the panel keeps it beside the instance until the engine owns a gas component of its
//    own — the same arrangement FractureRecord is in, and for the same reason.
struct GasRecord
{
    uint32_t                 For = kNoEditorInstance;
    GasCards::GasCardSubject Domain {};
};

class InspectorPanel final
{
public:
    void AssignControls(ControlPanel* Controls) noexcept;
    // The tab's close mark writes through this; null leaves the tab without one.
    void AssignTabOpen(bool* Open) noexcept;
    // Lets SolidArc seat this exact inspector beside Project-Zero without sharing the same ImGui title/id.
    void AssignWindowTitle(const char* Title) noexcept { WindowTitle_ = (Title != nullptr && Title[0] != '\0') ? Title : "Inspector"; }

    // SolidArc seats its glass-card look on the empty state too; its sheets select it themselves by Appearance.
    void AssignGlassCards(bool Glass) noexcept { GlassCards_ = Glass; }

    // The foot strip's live figures (realtime, triangle total); without a readout the strip prints its dashes.
    void AssignReadout(const EditorReadout* Readout) noexcept;

    void AssignRoster(EditorInstance* Rows, uint32_t Count) noexcept { Roster_ = Rows; RosterCount_ = Count; }
    uint32_t ConsumeCollectionPick() noexcept { const auto Selected = CollectionPick_; CollectionPick_ = kNoEditorInstance; return Selected; }
    void Record(EditorInstance* Picked, uint32_t PickedIndex, EditorSheet* Sheet, bool Embedded=false) noexcept;

    // ↗ on the gas card. The browser opens the Fluid editor in a drawer; natively the host owns that
    //    window, so the card raises a request and whoever drives the editor answers it. Returns the
    //    instance that asked, once, and then forgets — a flag that is never consumed is a flag that fires
    //    for the rest of the session.
    uint32_t ConsumeFluidEditorRequest() noexcept
    { const uint32_t Asked = FluidEditorFor_; FluidEditorFor_ = kNoEditorInstance; return Asked; }

    // ↗ on a gas *emitter's* card. The same button, a different page: Experimental/ParticleEditor rather
    //    than Experimental/Fluid. Kept as its own request rather than one request with a kind, because a
    //    host that has only built one of the two editors must be able to answer one and ignore the other.
    uint32_t ConsumeParticleEditorRequest() noexcept
    { const uint32_t Asked = ParticleEditorFor_; ParticleEditorFor_ = kNoEditorInstance; return Asked; }

    // The gas domain the panel is holding for an instance, for a host that wants to read or seed it.
    GasCards::GasCardSubject* QueryGasDomain(uint32_t PickedIndex) noexcept
    { for (GasRecord& One : Gas_) if (One.For == PickedIndex) return &One.Domain; return nullptr; }

    // The scene's force fields. 🔴 ONE SET, NOT ONE PER INSTANCE. A field is a thing in the world, not a
    //    property of whatever row happens to be selected, so the row named "Force fields" shows the same
    //    set whichever instance carries it — exactly as the browser's single outliner row does.
    [[nodiscard]] ForceCards::ForceCardSubject& QueryForceFields() noexcept { return Forces_; }
    [[nodiscard]] const ForceCards::ForceHitRegions& QueryForceRegions() const noexcept { return ForceWhere_; }

    // Where the gas card last painted its controls, in screen coordinates. A harness that wants to press a
    //    button needs the panel's own answer rather than a second guess at where the card begins — the
    //    ident strip above it is exactly the sort of thing a guess gets wrong.
    [[nodiscard]] const GasCards::GasHitRegions& QueryGasRegions() const noexcept { return GasWhere_; }
#ifdef FRONTIER_DEVELOPMENT
    // CPU visual-proof seam for the collection card; production selection still enters through Record().
    void RecordCollectionProof(ControlPanel& Controls,EditorInstance* Rows,uint32_t Count,uint32_t Selected,const char* Search=nullptr) noexcept;
#endif

private:
    void  RecordCollection(EditorInstance& Selected, uint32_t Index) noexcept;
    void  RecordEmpty() noexcept;
    void  RecordIdent(EditorInstance* Picked, uint32_t PickedIndex) noexcept;
    void  RecordCard(EditorPropertyGroup& Group, uint32_t Card) noexcept;
    void  RecordStanding(EditorInstance* Picked, uint32_t PickedIndex) noexcept;
    void  RecordFracture(EditorInstance& Picked, uint32_t PickedIndex) noexcept;
    void  RecordGas(EditorInstance& Picked, uint32_t PickedIndex, bool Emitter) noexcept;
    void  RecordForceFields() noexcept;
    void  RecordNotes(EditorInstance* Picked) noexcept;
    void  RecordFooter(EditorInstance* Picked) noexcept;
    float RecordCaps(const char* Text, const ImVec2& At, ImU32 Tint) noexcept;

    CollectionSequence   Collection_;
    EditorInstance*      Roster_ = nullptr;
    uint32_t             RosterCount_ = 0;
    uint32_t             CollectionPick_ = kNoEditorInstance;
    ControlPanel*        Controls_ = nullptr;
    bool*                TabOpen_ = nullptr;
    const EditorReadout* Readout_  = nullptr;
    const char*          WindowTitle_ = "Inspector";

    bool     GlassCards_  = false;
    bool     CardShut_[8] = {};                        // false reads open; sheet cards, then the notes card
    uint32_t SheetFor_    = kNoEditorInstance;
    uint32_t NameFor_     = kNoEditorInstance;
    char     NameText_[48] = {};
    bool     NotesFocus_  = false;   // the notes ring lags one tick (the push precedes the field)
    FractureRecord Fracture_[16] = {};
    GasRecord      Gas_[8]        = {};
    uint32_t       FluidEditorFor_ = kNoEditorInstance;
    uint32_t       ParticleEditorFor_ = kNoEditorInstance;
    GasCards::GasHitRegions GasWhere_ = {};
    ForceCards::ForceCardSubject  Forces_      = {};
    ForceCards::ForceHitRegions   ForceWhere_  = {};
    bool     NotesSeen_   = false;   // EntityNotes opens itself once, from whether a note exists
};

} // namespace Frontier
