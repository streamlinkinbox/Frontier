/*============================================================================================================================================
                                                            EDITORLIFECYCLE.CPP
============================================================================================================================================*/
// 🧩 Editor-only authoring loop implementation

#include "Engine/Lifecycle/EditorLifecycle.h"
#include "Engine/SimulationSpace/ForceFields/FieldFormulaCompiler.h"
#include "Engine/SimulationSpace/ForceFields/ForceMotionPath.h"

#if FRONTIER_EDITOR_ENABLED

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Frontier::Lifecycle
{

namespace
{

[[nodiscard]] const char* QueryForceDomainLabel(SimulationSpace::ForceFields::ForceDomainCategory Domain) noexcept
{
    using namespace SimulationSpace::ForceFields;
    switch (Domain)
    {
    case ForceDomainCategory::VectorField:     return "VectorField";
    case ForceDomainCategory::FlowField:       return "FlowField";
    case ForceDomainCategory::TurbulenceField: return "Turbulence";
    default:                                   return "ForceField";
    }
}

[[nodiscard]] SimulationSpace::ForceFields::ForceSourceRecordEntry BuildEditorForcePreset(
    uint32_t         ForceModelOrdinal,
    std::string_view Title)
{
    using namespace SimulationSpace::ForceFields;
    ForceSourceRecordEntry Desc{};
    Desc.RadiusMeters          = 18.0f;
    Desc.Strength              = 16.0f;
    Desc.FalloffExponent       = 1.35f;
    Desc.CentreMeters          = { 0.0f, 0.0f, 2.4f };
    Desc.EvaluatedCentreMeters = Desc.CentreMeters;
    Desc.Label                 = std::string(Title);

    if (ForceModelOrdinal < static_cast<uint32_t>(ForceModel::Count))
    {
        Desc.Model = static_cast<ForceModel>(ForceModelOrdinal);
    }
    else if (Title.find("Thermal") != std::string_view::npos || Title.find("Updraft") != std::string_view::npos)
    {
        Desc.Model = ForceModel::Buoyancy;
    }
    else if (Title.find("Cyclone") != std::string_view::npos || Title.find("Vortex") != std::string_view::npos)
    {
        Desc.Model      = ForceModel::RotationalColumn;
        Desc.SwirlRatio = 1.25f;
    }
    else if (Title.find("Containment") != std::string_view::npos)
    {
        Desc.Model            = ForceModel::Containment;
        Desc.CoreRadiusMeters = 6.0f;
    }
    else if (Title.find("Curl") != std::string_view::npos || Title.find("Turbulent") != std::string_view::npos)
    {
        Desc.Model                 = ForceModel::CurlNoise;
        Desc.TurbulenceScaleMeters = 10.0f;
        Desc.OctaveCount           = 3u;
    }
    else if (Title.find("Attractor") != std::string_view::npos)
    {
        Desc.Model            = ForceModel::Attractor;
        Desc.CoreRadiusMeters = 2.8f;
    }
    else if (Title.find("Spline") != std::string_view::npos || Title.find("Jetstream") != std::string_view::npos)
    {
        Desc.Model = ForceModel::SplineForce;
    }
    else if (Title.find("Lorentz") != std::string_view::npos || Title.find("Plasma") != std::string_view::npos)
    {
        Desc.Model      = ForceModel::LorentzField;
        Desc.SwirlRatio = 1.1f;
    }
    else if (Title.find("Formula") != std::string_view::npos)
    {
        Desc.Model             = ForceModel::FlowStream;
        Desc.FormulaEnabled    = true;
        Desc.FormulaSourceText = QueryBuiltInFormulaPresets()[0].FormulaSource;
    }
    else
    {
        Desc.Model          = ForceModel::LinearWind;
        Desc.EulerDegrees.Z = 228.0f;
    }

    Desc.Domain = QueryDomainForModel(Desc.Model);
    return Desc;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

EditorLifecycle::~EditorLifecycle() noexcept
{
    Finalize();
}

bool EditorLifecycle::Initialize(const EditorStartupConfiguration& Configuration) noexcept
{
    Finalize();

    StartupConfig        = Configuration;
    TerminationRequested = false;
    CurrentStepSeconds   = 1.0 / 60.0;

    if (!DockContainer.Initialize(StartupConfig.WindowDescriptor))
    {
        return false;
    }

    InputCoordinator.Initialize();

    if (!Workspaces.Initialize(false))
    {
        InputCoordinator.Finalize();
        DockContainer.Finalize();
        return false;
    }

    WorkspaceDocumentDescriptor DefaultDocument{};
    DefaultDocument.DocumentTitle   = "Scene";
    DefaultDocument.OutlinerVariant = StartupConfig.OutlinerHeading;
    DefaultDocument.InitialMode     = ViewportExecutionMode::Edit;
    if (!Workspaces.OpenSession(DefaultDocument).Valid())
    {
        Workspaces.Finalize();
        InputCoordinator.Finalize();
        DockContainer.Finalize();
        return false;
    }

    if (StartupConfig.ProjectSpacePath.empty())
    {
        ControlCentre.AccessDraftAppearance().Theme = ThemePreset::Dark;
        ControlCentre.ApplyDraftAppearanceImmediate();
        ControlCentre.AccessNotificationStream().ClearPending();
    }
    else
    {
        (void)ControlCentre.AttachProjectSpace(StartupConfig.ProjectSpacePath);
    }

    if (ImGui::GetCurrentContext() != nullptr)
    {
        ImGuiIO& Io = ImGui::GetIO();
        Io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        if (!StartupConfig.FontRepositoryRoot.empty() && Typography.LoadRepository(StartupConfig.FontRepositoryRoot) > 0u)
        {
            TypographyRepository::AssignCurrent(&Typography);
            ControlCentre.AttachTypefaces(&Typography);
            if (ImFont* TabFont = Typography.QueryTabStripFont())
            {
                Io.FontDefault = TabFont;
            }
        }

        Components::ConfigureImGuiWorkspaceTheme(DockContainer.ActiveTabMetrics(), ControlCentre.QueryThemeRepository());
    }

    ControlCentre.ConfigureWorkspaceCallbacks(
        [this](bool Allowed) noexcept
        {
            DockContainer.ConfigureDetachedWindowsAllowed(Allowed);
        },
        [this]() noexcept
        {
            DockContainer.ResetPrimaryLayout();
        });
    DockContainer.ConfigureDetachedWindowsAllowed(ControlCentre.QueryDetachedWindowsAllowed());

    const char* LeftPaneTitle = (StartupConfig.OutlinerHeading == OutlinerTitleVariant::LayerStack) ? "LayerStack" : "Outliner";

    Components::WorkspaceDockPaneDescriptor LeftDesc{};
    LeftDesc.PaneTitle         = LeftPaneTitle;
    LeftDesc.InitialDirection  = Components::DockPlacementDirection::SplitLeft;
    LeftDesc.InitialSplitRatio = 0.21f;
    LeftDesc.Directives        = Components::PaneDirectiveMask::StandardPaneDirectives;
    LeftDesc.ContentPresenter  = [this]()
    {
        if (WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes())
        {
            Panes->Outliner.PresentInImGuiPane(CurrentStepSeconds, ControlCentre.QueryInterfaceScaleFactor());
        }
    };
    OutlinerPaneToken = DockContainer.RegisterPane(LeftDesc);

    Components::WorkspaceDockPaneDescriptor CenterDesc{};
    CenterDesc.PaneTitle         = "Viewport";
    CenterDesc.InitialDirection  = Components::DockPlacementDirection::CenterTabGroup;
    CenterDesc.InitialSplitRatio = 0.58f;
    CenterDesc.Directives        = Components::PaneDirectiveMask::CentralViewportDirectives;
    CenterDesc.ContentPresenter  = [this]()
    {
        if (WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes())
        {
            Panes->Viewport.PresentInImGuiPane(CurrentStepSeconds, ControlCentre.QueryInterfaceScaleFactor());
        }
    };
    ViewportPaneToken = DockContainer.RegisterPane(CenterDesc);

    Components::WorkspaceDockPaneDescriptor RightDesc{};
    RightDesc.PaneTitle         = "Inspector";
    RightDesc.InitialDirection  = Components::DockPlacementDirection::SplitRight;
    RightDesc.InitialSplitRatio = 0.21f;
    RightDesc.Directives        = Components::PaneDirectiveMask::StandardPaneDirectives;
    RightDesc.ContentPresenter  = [this]()
    {
        if (WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes())
        {
            Panes->Inspector.PresentInImGuiPane(CurrentStepSeconds, ControlCentre.QueryInterfaceScaleFactor());
        }
    };
    InspectorPaneToken = DockContainer.RegisterPane(RightDesc);

    if (!EditorSequence.Initialize())
    {
        Workspaces.Finalize();
        InputCoordinator.Finalize();
        DockContainer.Finalize();
        return false;
    }

    if (ImGui::GetCurrentContext() != nullptr)
    {
        Components::ConfigureImGuiWorkspaceStyle(DockContainer.ActiveTabMetrics());
        Components::ConfigureImGuiWorkspaceTheme(DockContainer.ActiveTabMetrics(), ControlCentre.QueryThemeRepository());
    }

    Initialized = true;
    AttachForceFieldAuthoringToActivePanes();
    if (StartupConfig.PopulateShowcaseScene)
    {
        PopulateShowcaseGallerySession();
    }
    return true;
}

void EditorLifecycle::AttachForceFieldAuthoringToActivePanes()
{
    WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes();
    if (Panes == nullptr)
    {
        return;
    }

    ForcePresetCatalog.PopulateDefaultForcePresets();
    AuthoredForces.clear();
    AnemometerHistory.fill(0.0f);
    ForceClockSeconds = 0.0;
    NextForceToken    = 1u;

    std::vector<OutlinerConstructTemplate> Templates;
    Templates.reserve(24u);
    for (uint32_t Ord = 0u; Ord < static_cast<uint32_t>(SimulationSpace::ForceFields::ForceModel::Count); ++Ord)
    {
        const auto  Model   = static_cast<SimulationSpace::ForceFields::ForceModel>(Ord);
        const auto  Domain  = SimulationSpace::ForceFields::QueryDomainForModel(Model);
        const char* Name    = SimulationSpace::ForceFields::QueryForceModelDisplayName(Model);
        const char* DomName = QueryForceDomainLabel(Domain);
        Templates.push_back({
            std::string("Add ") + Name,
            Name,
            DomName,
            "Analytical ForceModel",
            ConstructSlotCategory::ForceField,
            Ord,
        });
    }
    Templates.push_back({
        "Add Custom Formula",
        "Custom Formula",
        "VectorFieldLang",
        "Symbolic GPU formula",
        ConstructSlotCategory::ForceField,
        static_cast<uint32_t>(SimulationSpace::ForceFields::ForceModel::FlowStream),
    });
    Templates.push_back({ "Add Cube Mesh",   "Cube",        "Static Mesh", "Position 0, 0, 0 m", ConstructSlotCategory::CategoryOne,   0xFFFFFFFFu });
    Templates.push_back({ "Add Sphere Mesh", "Sphere",      "Static Mesh", "Position 0, 0, 1 m", ConstructSlotCategory::CategoryOne,   0xFFFFFFFFu });
    Templates.push_back({ "Add Point Light", "Point Light", "Light",       "Position 0, 0, 3 m", ConstructSlotCategory::CategoryThree, 0xFFFFFFFFu });
    Panes->Outliner.ConfigureConstructTemplates(std::move(Templates));

    std::vector<ViewportConstructPresetEntry>     ViewportPresets;
    std::vector<Codecs::ForceModelPresetDocument> CapturedPresetDocs;
    for (const auto& Asset : ForcePresetCatalog.QueryAllAssets())
    {
        if (Asset.Category == ContentAssetCategory::ForcePresets || Asset.Category == ContentAssetCategory::FormulaPresets)
        {
            ViewportPresets.push_back({
                Asset.Title,
                Asset.CategoryCaption,
                static_cast<uint32_t>(Asset.PresetDocument.ForceSource.Model),
                Asset.FormulaOverride,
                static_cast<uint32_t>(Asset.PresetDocument.ForceSource.Domain),
            });
            CapturedPresetDocs.push_back(Asset.PresetDocument);
        }
    }
    Panes->Viewport.ConfigureForceOverlayToolsVisible(true);
    Panes->Viewport.ConfigureConstructPresets(std::move(ViewportPresets));
    Panes->Viewport.ConfigureConstructPresetCallback(
        [this, CapturedPresetDocs = std::move(CapturedPresetDocs)](uint32_t PresetIdx)
        {
            if (PresetIdx < CapturedPresetDocs.size())
            {
                (void)SpawnForceFieldEntry(CapturedPresetDocs[PresetIdx].ForceSource);
            }
        });
    Panes->Viewport.ConfigureOpenContentBrowserCallback(
        [this]()
        {
            ContentBrowserDrawer.AccessPanel().ConfigureOriginFilter(ContentOriginFilter::All);
            (void)ContentBrowserDrawer.AccessPanel().ActivateCategory(ContentCategory::Simulations);
            ContentBrowserDrawer.AccessPanel().ConfigureShelfExtent(ContentShelfExtent::SplitHalf, false);
        });

    Panes->Inspector.ConfigureFullOverrideCallback(
        [this](PixelSpace& Surface, Rectangle2D Bounds, OutlinerPanel* OutlinerPtr, OutlinerRecordEntry* PrimaryEntry, const PaneInputReading& Input, double Δt) -> bool
        {
            if (PrimaryEntry == nullptr || PrimaryEntry->Category != ConstructSlotCategory::ForceField)
            {
                return false;
            }
            WorkspaceSessionPanes* ActivePanes = Workspaces.AccessActivePanes();
            if (ActivePanes == nullptr)
            {
                return false;
            }
            SimulationSpace::ForceFields::ForceSourceRecordEntry* ActiveForce = nullptr;
            for (auto& Item : AuthoredForces)
            {
                if (Item.OutlinerSlot == PrimaryEntry->Token)
                {
                    ActiveForce = &Item.Source;
                    break;
                }
            }
            if (ActiveForce == nullptr)
            {
                MappedEditorForceSource Synthesized{};
                Synthesized.OutlinerSlot       = PrimaryEntry->Token;
                Synthesized.Source             = BuildEditorForcePreset(PrimaryEntry->ForceModelOrdinal, PrimaryEntry->Title);
                Synthesized.Source.UniqueToken = NextForceToken++;
                AuthoredForces.push_back(std::move(Synthesized));
                ActiveForce = &AuthoredForces.back().Source;
            }

            Editor::Simulation::ForceInspectorViewTargets Targets{};
            Targets.Outliner          = OutlinerPtr != nullptr ? OutlinerPtr : &ActivePanes->Outliner;
            Targets.OutlinerEntry     = PrimaryEntry;
            Targets.Accumulator       = &ForceAccumulator;
            Targets.LatticeCascade    = &ForceLatticeCascade;
            Targets.ActiveForce       = ActiveForce;
            Targets.OverlaySettings   = &ActivePanes->Viewport.AccessForceOverlaySettings();
            Targets.AnemometerSamples = AnemometerHistory;
            Targets.SimulationTimeSec = ForceClockSeconds;
            Targets.SelectedWaypoint  = &SelectedWaypointIdx;

            const bool Changed = ForceInspector.Render(Surface, Bounds, Targets, Input, Δt);
            if (Changed)
            {
                PrimaryEntry->ForceModelOrdinal      = static_cast<uint32_t>(ActiveForce->Model);
                PrimaryEntry->ForceFormulaOverride = ActiveForce->FormulaEnabled;
                PrimaryEntry->CategoryLabel        = SimulationSpace::ForceFields::QueryForceModelDisplayName(ActiveForce->Model);
                char SubBuf[96]{};
                std::snprintf(SubBuf, sizeof(SubBuf), "r %.0f m  |  %.1f m/s", ActiveForce->RadiusMeters, ActiveForce->Strength);
                PrimaryEntry->Subtitle  = SubBuf;
                PrimaryEntry->PositionX = ActiveForce->CentreMeters.X;
                PrimaryEntry->PositionY = ActiveForce->CentreMeters.Y;
                PrimaryEntry->PositionZ = ActiveForce->CentreMeters.Z;
                PrimaryEntry->RotationX = ActiveForce->EulerDegrees.X;
                PrimaryEntry->RotationY = ActiveForce->EulerDegrees.Y;
                PrimaryEntry->RotationZ = ActiveForce->EulerDegrees.Z;
                SynchronizeForceFieldsWithOutliner(0.0);
            }
            return true;
        });

    ContentBrowserDrawer.AccessPanel().AssignContentActivationCallback(
        [this](const ContentRepositoryEntry& Entry)
        {
            const auto Category = static_cast<ContentCategory>(Entry.IndexHeader.CategoryCode);
            if (Category == ContentCategory::Simulations)
            {
                for (const auto& PresetAsset : ForcePresetCatalog.QueryAllAssets())
                {
                    if (PresetAsset.Title == Entry.DisplayTitle)
                    {
                        (void)SpawnForceFieldEntry(PresetAsset.PresetDocument.ForceSource);
                        return;
                    }
                }
                const auto Built = BuildEditorForcePreset(0xFFFFFFFFu, Entry.DisplayTitle);
                (void)SpawnForceFieldEntry(Built);
            }
        });
}

OutlinerSlotToken EditorLifecycle::SpawnForceFieldEntry(
    const SimulationSpace::ForceFields::ForceSourceRecordEntry& SourcePreset)
{
    WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes();
    if (Panes == nullptr)
    {
        return {};
    }

    if (!DefaultForcesFolder.Valid() || Panes->Outliner.FindEntry(DefaultForcesFolder) == nullptr)
    {
        DefaultForcesFolder = Panes->Outliner.ConstructFolder("Forces & VectorFields");
    }

    SimulationSpace::ForceFields::ForceSourceRecordEntry Instance = SourcePreset;
    Instance.UniqueToken = NextForceToken++;
    Instance.Domain      = SimulationSpace::ForceFields::QueryDomainForModel(Instance.Model);
    if (Instance.Label.empty())
    {
        Instance.Label = SimulationSpace::ForceFields::QueryForceModelDisplayName(Instance.Model);
    }

    const OutlinerSlotToken Slot = Panes->Outliner.ConstructFromTemplate(
        static_cast<uint32_t>(Instance.Model),
        DefaultForcesFolder);
    if (OutlinerRecordEntry* Record = Panes->Outliner.AccessEntry(Slot))
    {
        Record->Category             = ConstructSlotCategory::ForceField;
        Record->Title                = Instance.Label;
        Record->CategoryLabel        = SimulationSpace::ForceFields::QueryForceModelDisplayName(Instance.Model);
        Record->ForceModelOrdinal      = static_cast<uint32_t>(Instance.Model);
        Record->ForceFormulaOverride = Instance.FormulaEnabled;
        char SubBuf[96]{};
        std::snprintf(SubBuf, sizeof(SubBuf), "r %.0f m  |  %.1f m/s", Instance.RadiusMeters, Instance.Strength);
        Record->Subtitle  = SubBuf;
        Record->PositionX = Instance.CentreMeters.X;
        Record->PositionY = Instance.CentreMeters.Y;
        Record->PositionZ = Instance.CentreMeters.Z;
        Record->RotationX = Instance.EulerDegrees.X;
        Record->RotationY = Instance.EulerDegrees.Y;
        Record->RotationZ = Instance.EulerDegrees.Z;
    }

    Instance.OutlinerTokenId = Slot.Identifier;
    AuthoredForces.push_back(MappedEditorForceSource{ Slot, std::move(Instance) });
    Panes->Outliner.SelectSingle(Slot);
    SynchronizeForceFieldsWithOutliner(0.0);
    return Slot;
}

void EditorLifecycle::SynchronizeForceFieldsWithOutliner(double Δt)
{
    WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes();
    if (Panes == nullptr)
    {
        return;
    }

    const bool Advance =
        (Panes->Viewport.QueryExecutionMode() == ViewportExecutionMode::Simulate) ||
        (Panes->Viewport.QueryExecutionMode() == ViewportExecutionMode::Play && !Panes->Viewport.QueryPlayPaused());
    if (Advance && Δt > 0.0)
    {
        ForceClockSeconds += Δt;
    }
    Panes->Viewport.ConfigureSimulationTimeSeconds(ForceClockSeconds);

    // Adopt newly constructed ForceField rows created via Outliner '+' or Viewport Construct ('Ctrl+A')
    for (const OutlinerRecordEntry& Entry : Panes->Outliner.QueryEntries())
    {
        if (Entry.Category != ConstructSlotCategory::ForceField)
        {
            continue;
        }
        const bool AlreadyMapped = std::any_of(
            AuthoredForces.begin(),
            AuthoredForces.end(),
            [&](const MappedEditorForceSource& Item) { return Item.OutlinerSlot == Entry.Token; });
        if (!AlreadyMapped)
        {
            MappedEditorForceSource NewItem{};
            NewItem.OutlinerSlot           = Entry.Token;
            NewItem.Source                 = BuildEditorForcePreset(Entry.ForceModelOrdinal, Entry.Title);
            NewItem.Source.UniqueToken     = NextForceToken++;
            NewItem.Source.OutlinerTokenId = Entry.Token.Identifier;
            AuthoredForces.push_back(std::move(NewItem));
        }
    }

    // Prune deleted Outliner ForceField rows & sync transforms
    std::vector<SimulationSpace::ForceFields::ForceSourceRecordEntry> ActiveList;
    ActiveList.reserve(AuthoredForces.size());
    for (auto It = AuthoredForces.begin(); It != AuthoredForces.end();)
    {
        OutlinerRecordEntry* Record = Panes->Outliner.AccessEntry(It->OutlinerSlot);
        if (Record == nullptr)
        {
            It = AuthoredForces.erase(It);
            continue;
        }
        It->Source.Label          = Record->Title;
        It->Source.Enabled        = Panes->Outliner.QueryEffectiveVisibility(It->OutlinerSlot);
        It->Source.CentreMeters.X = Record->PositionX;
        It->Source.CentreMeters.Y = Record->PositionY;
        It->Source.CentreMeters.Z = Record->PositionZ;
        It->Source.EulerDegrees.X = Record->RotationX;
        It->Source.EulerDegrees.Y = Record->RotationY;
        It->Source.EulerDegrees.Z = Record->RotationZ;
        ActiveList.push_back(It->Source);
        ++It;
    }

    ForceAccumulator.AssignSources(ActiveList);
    ForceAccumulator.StepAnimation(ForceClockSeconds, Advance);

    const auto ProbePos = Panes->Viewport.QueryForceOverlaySettings().AnemometerWorldMeters;
    const auto Diag     = ForceAccumulator.QueryAnemometerDiagnostics(ProbePos, ForceClockSeconds);
    const float WindSpd = SimulationSpace::ForceFields::VectorLength(Diag.TotalForce.AdvectionVelocity);
    const float AccSpd  = SimulationSpace::ForceFields::VectorLength(Diag.TotalForce.LinearAcceleration);
    const float Speed   = (WindSpd > 0.01f) ? WindSpd : AccSpd;
    std::rotate(AnemometerHistory.begin(), AnemometerHistory.begin() + 1, AnemometerHistory.end());
    AnemometerHistory.back() = Speed;
}

void EditorLifecycle::PopulateShowcaseGallerySession(uint32_t RenderWidthPx, uint32_t RenderHeightPx)
{
    WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes();
    if (Panes == nullptr)
    {
        return;
    }

    ShowcaseBundle   = std::make_unique<GeometricRaster::ShowcaseWorldSceneBundle>();
    ShowcaseRenderer = std::make_unique<GeometricRaster::TieredWorldRenderSequence>();

    GeometricRaster::BuildShowcaseGalleryScene(*ShowcaseBundle, RenderWidthPx, RenderHeightPx);
    ShowcaseRenderer->PrepareWorldScene(ShowcaseBundle->Scene);
    GeometricRaster::RegisterShowcaseGalleryInOutliner(Panes->Outliner, *ShowcaseBundle);
    Panes->Viewport.ConfigureSceneTitle("Cornell-Studio Gallery");
    RefreshShowcaseViewportRaster();
}

void EditorLifecycle::RefreshShowcaseViewportRaster()
{
    WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes();
    if (Panes == nullptr || ShowcaseBundle == nullptr || ShowcaseRenderer == nullptr)
    {
        return;
    }

    const auto GtxFrame = ShowcaseRenderer->RenderWorldFrame(
        GeometricRaster::WorldRenderTier::Tier1GtxCalibrated,
        ShowcaseBundle->EditorCamera,
        ShowcaseBundle->EditorCamera,
        ShowcaseBundle->Scene);

    Panes->Viewport.AssignCanvasRasterImage(
        GtxFrame.DisplayRgba8.data(),
        GtxFrame.WidthPx,
        GtxFrame.HeightPx);

    OutlinerFooterReadings Footer {};
    Footer.RealtimeText  = "60 fps";
    Footer.QualityText   = "GTX Tier 1";
    Footer.PrimarySlot   = "Sun 46°";
    Footer.SecondarySlot = "LTC 1";
    char CamBuffer[48]{};
    std::snprintf(
        CamBuffer,
        sizeof(CamBuffer),
        "%.1f, %.1f, %.1f",
        ShowcaseBundle->EditorCamera.CameraOrigin.X,
        ShowcaseBundle->EditorCamera.CameraOrigin.Y,
        ShowcaseBundle->EditorCamera.CameraOrigin.Z);
    Footer.CameraText = CamBuffer;
    Panes->Outliner.ConfigureFooterReadings(Footer);
}

bool EditorLifecycle::ExecuteAuthoringTick(double Δt)
{
    if (!Initialized || TerminationRequested)
    {
        return false;
    }

    const double MaxStep  = StartupConfig.MaximumStep > 0.0 ? StartupConfig.MaximumStep : 0.100;
    const double SafeStep = (std::isfinite(Δt) && Δt > 0.0) ? std::min(Δt, MaxStep) : 0.0;
    CurrentStepSeconds    = SafeStep;

    InputCoordinator.BeginTick();
    DockContainer.PollOperatingWindows(InputCoordinator);
    (void)InputCoordinator.TickExchange();

    if (Windowing::HeadlessPlatformWindow* Primary = DockContainer.AccessOperatingSurface(Components::HostWindowToken{ 0u }))
    {
        if (Primary->CloseRequested())
        {
            TerminationRequested = true;
        }
        else
        {
            (void)InputCoordinator.FlushOutboundDirectives(*Primary);
        }
    }

    if (!TerminationRequested)
    {
        Workspaces.Tick(SafeStep);
        SynchronizeForceFieldsWithOutliner(SafeStep);
        if (ShowcaseBundle != nullptr)
        {
            if (WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes())
            {
                if (GeometricRaster::SynchronizeShowcaseGalleryFromOutliner(Panes->Outliner, *ShowcaseBundle))
                {
                    RefreshShowcaseViewportRaster();
                }
            }
        }
        EditorSequence.Tick(SafeStep);
    }

    return !TerminationRequested;
}

void EditorLifecycle::PresentPrimaryEditorImGui(double Δt)
{
    if (!Initialized || ImGui::GetCurrentContext() == nullptr)
    {
        return;
    }

    const double SafeStep = (std::isfinite(Δt) && Δt > 0.0) ? Δt : CurrentStepSeconds;
    CurrentStepSeconds    = SafeStep;

    Components::ConfigureImGuiWorkspaceTheme(DockContainer.ActiveTabMetrics(), ControlCentre.QueryThemeRepository());
    ContentBrowserDrawer.AttachThemeRepository(&ControlCentre.QueryThemeRepository());
    DockContainer.PresentWindowImGuiPanes(Components::HostWindowToken{ 0u });

    if (WorkspaceSessionPanes* ActivePanes = Workspaces.AccessActivePanes())
    {
        ContentBrowserDrawer.SynchronizeWorkspaceTargets(&ActivePanes->Outliner, &ActivePanes->Inspector);
    }

    const Components::HostWindowReading HostReading = DockContainer.InspectHostWindow(Components::HostWindowToken{ 0u });
    if (HostReading.Active && HostReading.SurfaceExtent.PixelWidth > 0u && HostReading.SurfaceExtent.PixelHeight > 0u)
    {
        const Rectangle2D Bounds{
            0.0f,
            0.0f,
            static_cast<float>(HostReading.SurfaceExtent.PixelWidth),
            static_cast<float>(HostReading.SurfaceExtent.PixelHeight),
        };
        const float RefreshHz = HostReading.SurfaceExtent.RefreshRateHz > 0u
                                    ? static_cast<float>(HostReading.SurfaceExtent.RefreshRateHz)
                                    : 60.0f;
        const float ScaleFactor = HostReading.SurfaceExtent.DpiScaleFactor > 0.5f
                                      ? HostReading.SurfaceExtent.DpiScaleFactor
                                      : 1.0f;
        ContentBrowserDrawer.Render(Bounds, SafeStep, ScaleFactor);
        ControlCentre.Render(Bounds, SafeStep, RefreshHz);
    }
}

bool EditorLifecycle::Suspend() noexcept
{
    if (!Initialized)
    {
        return false;
    }
    const bool OverlaySuspended   = EditorSequence.Suspend();
    const bool WorkspaceSuspended = Workspaces.Suspend();
    return OverlaySuspended && WorkspaceSuspended;
}

bool EditorLifecycle::Resume() noexcept
{
    if (!Initialized)
    {
        return false;
    }
    const bool WorkspaceResumed = Workspaces.Resume();
    const bool OverlayResumed   = EditorSequence.Resume();
    return WorkspaceResumed && OverlayResumed;
}

void EditorLifecycle::RequestTermination() noexcept
{
    TerminationRequested = true;
}

void EditorLifecycle::Finalize() noexcept
{
    if (!Initialized)
    {
        EditorSequence.Finalize();
        return;
    }

    // 🔴 CRITICAL: Tear down GPU overlay hooks, then workspace sessions, then input, then dock windows.
    EditorSequence.Finalize();
    AuthoredForces.clear();
    DefaultForcesFolder = {};
    ShowcaseRenderer.reset();
    ShowcaseBundle.reset();
    Workspaces.Finalize();

    if (TypographyRepository::QueryCurrent() == &Typography)
    {
        TypographyRepository::AssignCurrent(nullptr);
    }

    InputCoordinator.Finalize();
    DockContainer.Finalize();

    OutlinerPaneToken    = {};
    ViewportPaneToken    = {};
    InspectorPaneToken   = {};
    Initialized          = false;
    TerminationRequested = false;
}

EditorLifecycleReading EditorLifecycle::InspectReading() const noexcept
{
    EditorLifecycleReading Reading{};
    Reading.SequencePhase       = EditorSequence.QueryPhase();
    Reading.SequenceMetrics     = EditorSequence.InspectMetrics();
    Reading.ActiveWindowCount   = DockContainer.ActiveOperatingWindowCount();
    Reading.RegisteredPaneCount = static_cast<uint32_t>(DockContainer.RegisteredPaneCount());
    Reading.OpenWorkspaceCount  = Workspaces.OpenSessionCount();
    Reading.GpuOverlayCount     = RegisteredGpuOverlays;
    Reading.ControlCentreOpen   = ControlCentre.QueryOpen();
    Reading.ContentBrowserOpen  = ContentBrowserDrawer.QueryPanel().QueryOpen();
    Reading.AuthoringActive     = QueryActive();
    return Reading;
}

} // namespace Frontier::Lifecycle

#endif // FRONTIER_EDITOR_ENABLED
