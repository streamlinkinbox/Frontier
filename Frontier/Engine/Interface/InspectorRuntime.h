/*==========================================================================================================================================
                                                         INSPECTORRUNTIME.H
==========================================================================================================================================*/
// 🧩 Central Editor runtime managing modular RecordInspector attachments, lifecycle slots, and *Editor dispatch.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Scene/RecordToken.h"
#include "Engine/Scene/SceneDirectory.h"
#include "Engine/Scene/SolarPlacement.h"
#include "Engine/Interface/EnvironmentEditAccess.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INSPECTOR SWITCH ENTRY
//------------------------------------------------------------------------------------------------------------------------

struct InspectorSwitchEntry
{
    const char* Label   = "";
    const char* SubNote = "";
    bool*       Target  = nullptr;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    RECORD INSPECTOR BASE
//------------------------------------------------------------------------------------------------------------------------

struct InspectorViewTarget
{
    std::array<double,3> Position{};
    double Radius = 5;
    bool Celestial = false;
    float Azimuth = 0;
    float Elevation = 0;
    float Diameter = 1;
};

class RecordInspector
{
  public:
    virtual ~RecordInspector() = default;
    virtual bool QueryViewTarget(InspectorViewTarget&) const noexcept { return false; }

    [[nodiscard]] virtual const char* QueryEyebrowText() const noexcept                     = 0;
    [[nodiscard]] virtual const char* QuerySectionNote() const noexcept                     { return "Live"; }
    [[nodiscard]] virtual const char* QueryEditorButtonLabel() const noexcept               { return nullptr; }
    virtual void                      ConstructEditor(RecordToken Token)                    { (void)Token; }
    [[nodiscard]] virtual bool        QueryToggleSupported() const noexcept                 { return false; }
    [[nodiscard]] virtual bool        QueryToggleEnabled() const noexcept                   { return true; }
    virtual void                      ConfigureToggle(bool Enabled)                         { (void)Enabled; }
    [[nodiscard]] virtual uint64_t    QueryRevision() const noexcept                        { return 0; }
    virtual void                      EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches) { (void)OutSwitches; }
    // 📝 True for a page that lays out the whole Inspector column itself (the demo's environment Inspector pages): the
    //    runtime then draws no crumb bar, record header or switch grid around it.
    [[nodiscard]] virtual bool        QueryOccupiesColumn() const noexcept                  { return false; }
    // 📝 A page's section tabs (the demo's Overview / Daylight / …): how many there are, and which one shows.
    [[nodiscard]] virtual int         QuerySectionCount() const noexcept                    { return 0; }
    virtual void                      ConfigureSection(int Section) noexcept                { (void)Section; }
    // 📝 The Outliner row (the demo's entity Meta, Glyph and State): a readout such as "38°" or "1/4", the glyph when the
    //    configuration decides it (a local cloud's artwork), and the state dot (live, warm, or standby grey).
    [[nodiscard]] virtual std::string                QueryDirectoryReadout() const                 { return {}; }
    [[nodiscard]] virtual const char*                QueryDirectoryGlyph() const noexcept          { return nullptr; }
    [[nodiscard]] virtual RecordOperationalCondition QueryDirectoryCondition() const noexcept      { return RecordOperationalCondition::Live; }
    virtual void                      ConstructInspector()                                  = 0;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    INSPECTOR RUNTIME
//------------------------------------------------------------------------------------------------------------------------

class InspectorRuntime
{
  public:
    [[nodiscard]] static InspectorRuntime& Instance() noexcept;

    InspectorRuntime(const InspectorRuntime&)            = delete;
    InspectorRuntime& operator=(const InspectorRuntime&) = delete;

    // Replaces all inspectors on Token with one primary inspector.
    bool BindInspector(RecordToken Token, std::unique_ptr<RecordInspector> Inspector);

    // Appends an additional modular RecordInspector (e.g. PhysicsInspector, ThermalInspector, ChemicalInspector).
    bool AttachInspector(RecordToken Token, std::unique_ptr<RecordInspector> Inspector);

    [[nodiscard]] RecordInspector*       FindInspector(RecordToken Token) noexcept;
    [[nodiscard]] const RecordInspector* FindInspector(RecordToken Token) const noexcept;
    [[nodiscard]] size_t                 QueryInspectorCount(RecordToken Token) const noexcept;
    [[nodiscard]] bool                   QueryHasEditor(RecordToken Token) const noexcept;
    [[nodiscard]] bool                   QuerySlotOpen(RecordToken Token) const noexcept;
    [[nodiscard]] uint64_t               QuerySlotCount() const noexcept;
    [[nodiscard]] uint64_t               QueryBoundCount() const noexcept;

    // Central Inspector dispatch: renders all modular inspectors attached to Token and signals deletion requests.
    void InspectRecord(RecordToken Token, bool& OutDeleteRequested);
    // 📝 The record InspectRecord is laying out (a page reads its record's name and metadata through it).
    [[nodiscard]] RecordToken QueryInspectedToken() const noexcept { return InspectedToken; }

    // Standalone *Editor activation (FractureEditor, FluidPlumeEditor, ParticleEditor, or custom).
    bool                      RequestEditorOpen(RecordToken Token) noexcept;
    void                      CloseActiveEditor() noexcept;
    [[nodiscard]] RecordToken QueryActiveEditorToken() const noexcept;
    void                      ConstructActiveEditor();

    // 📝 A page asking the Outliner to select the first record of a type ("Open Lens Flare ↗"); the workspace consumes it.
    void                      RequestSelection(std::string_view TypeIdentifier);
    [[nodiscard]] std::string ConsumeSelectionRequest();

    // 📝 A page asking the workspace for something only the workspace can do, as a short verb:argument line ("select:<token>",
    //    "add-force:<identifier>", "field-view:on", "field-span:<metres>"); the workspace consumes them once per frame.
    void                                   RequestWorkspaceAction(std::string Action);
    [[nodiscard]] std::vector<std::string> ConsumeWorkspaceActions();

    // 📝 The workspace's interface scale (Appearance → Interface scale / 100), configured once per frame.
    void                ConfigureInterfaceScale(float Scale) noexcept;
    [[nodiscard]] float ResolveInterfaceScale() const noexcept;

    // 📝 The celestial solver the Sun page asks for its day path; registered by the project that links the renderer.
    void                                     RegisterSolarPlacementSolver(SolarPlacementSolver Solver) noexcept;
    [[nodiscard]] SolarPlacementSolver       QuerySolarPlacementSolver() const noexcept;

    void RequestSkyFocus(float Azimuth, float Elevation, float Diameter) noexcept { SkyFocus = {Azimuth,Elevation,Diameter}; SkyFocusPending = true; }
    bool ConsumeSkyFocus(std::array<float,3>& Focus) noexcept
    {
        if (!SkyFocusPending) return false;
        Focus = SkyFocus; SkyFocusPending = false; return true;
    }
    void ConfigureEnvironmentAccess(const EnvironmentEditAccess& Access) noexcept { EnvironmentAccess = Access; }
    [[nodiscard]] const EnvironmentEditAccess& QueryEnvironmentAccess() const noexcept { return EnvironmentAccess; }

    // 📝 The level's sun elevation, published by the workspace each frame for pages on other records (the Atmosphere
    //    page's transmission dial reads the demo's S.SunElevation).
    void                PublishSolarElevation(float Degrees) noexcept { SolarElevation = Degrees; }
    [[nodiscard]] float QuerySolarElevation() const noexcept { return SolarElevation; }

  private:
    friend class RecordEntry;

    std::array<float,3> SkyFocus{};
    bool SkyFocusPending = false;
    InspectorRuntime()  = default;
    ~InspectorRuntime() = default;

    void OpenSlot(RecordToken Token);
    void Unregister(RecordToken Token) noexcept;

#if FRONTIER_EDITOR
    struct SlotEntry
    {
        std::vector<std::unique_ptr<RecordInspector>> Inspectors;
    };

    std::unordered_map<uint64_t, SlotEntry> Slots;
    RecordToken                             ActiveEditorToken{};
#endif
    std::string          PendingSelectionType{};
    std::vector<std::string> PendingWorkspaceActions{};
    RecordToken          InspectedToken{};
    SolarPlacementSolver SolarSolver    = nullptr;
    float                InterfaceScale = 1.0f;
    EnvironmentEditAccess EnvironmentAccess{};
    float                SolarElevation = 38.0f;   // [°]
};

} // namespace Frontier
