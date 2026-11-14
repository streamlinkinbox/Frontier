/*============================================================================================================================================
                                                   PARTICLEWORKSPACEUIVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Validation & PNG visual-proof suite for ParticleOutlinerPanel, ParticleInspectorPanel, 93 non-quarantined presets & C++ ForceFields reuse

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Appearance/ThemeRepository.h"
#include "Engine/Components/Appearance/TypographyRepository.h"
#include "Engine/Components/Controls/PlaybackScrubber.h"
#include "Engine/Components/Controls/ScalarSlider.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldDescriptors.h"
#include "Engine/Workspaces/Particle/ParticleInspectorPanel.h"
#include "Engine/Workspaces/Particle/ParticleOutlinerPanel.h"
#include "Engine/Workspaces/Particle/ParticlePresetRegistry.h"
#include "Engine/Workspaces/Particle/ParticleWorkspaceSession.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <imgui.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

namespace
{

using namespace Frontier;
using namespace Frontier::Workspaces;
using namespace Frontier::SimulationSpace::ForceFields;
using Frontier::Editor::Simulation::ForceInspectorTab;
using Frontier::Exhibits::ResolveRepositoryRootPath;
using Frontier::Exhibits::SoftwareRasterCanvas;

//------------------------------------------------------------------------------------------------------------------------
//                                               VERIFICATION ASSERTIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] bool VerifyAssertion(bool Condition, const char* Label) noexcept
{
    if (!Condition)
    {
        std::fprintf(stderr, "[ParticleWorkspaceUIValidation] FAILED: %s\n", Label);
        return false;
    }
    return true;
}

[[nodiscard]] bool CaptureTwoPanePng(
    ImGuiIO&                     Io,
    uint32_t                     WidthPx,
    uint32_t                     HeightPx,
    const std::filesystem::path& OutputPng,
    auto&&                       RenderCallback)
{
    Io.DisplaySize = ImVec2(static_cast<float>(WidthPx), static_cast<float>(HeightPx));
    Io.DeltaTime   = 1.0f / 60.0f;

    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(WidthPx), static_cast<float>(HeightPx)), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin(
        "##ParticleExhibitCanvas",
        nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs);

    PixelSpace Surface(ImGui::GetWindowDrawList(), { 0.0f, 0.0f }, 1.0f);
    Surface.FillRectangle(
        { 0.0f, 0.0f, static_cast<float>(WidthPx), static_cast<float>(HeightPx) },
        ColorRgba{ 12u, 12u, 13u, 255u },
        0.0f);
    RenderCallback(Surface);

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::Render();

    SoftwareRasterCanvas Canvas(WidthPx, HeightPx, { 12u, 12u, 13u, 255u });
    Canvas.RasterizeImGuiBatches(ImGui::GetDrawData());
    return VerifyAssertion(Canvas.WritePngFile(OutputPng), OutputPng.filename().string().c_str());
}

//------------------------------------------------------------------------------------------------------------------------
//                                           1. REGISTRY & QUARANTINE EXCLUSION
//------------------------------------------------------------------------------------------------------------------------

bool ValidatePresetRegistryAndQuarantineExclusion()
{
    const auto Categories = ParticlePresetRegistry::QueryActiveCategories();
    const auto Presets    = ParticlePresetRegistry::QueryActivePresets();

    if (!VerifyAssertion(Categories.size() == 14u, "Exactly 14 active non-quarantined effect categories"))
    {
        return false;
    }
    if (!VerifyAssertion(Presets.size() == 93u, "Exactly 93 active non-quarantined presets registered"))
    {
        return false;
    }

    uint32_t CategorySum = 0u;
    for (const ParticleCategoryDescriptor& Cat : Categories)
    {
        const uint32_t CatCount = ParticlePresetRegistry::CountPresetsInCategory(Cat.Category);
        if (!VerifyAssertion(CatCount > 0u, "Every active category contains at least 1 preset"))
        {
            return false;
        }
        CategorySum += CatCount;
    }
    if (!VerifyAssertion(CategorySum == 93u, "Sum of per-category preset counts equals 93"))
    {
        return false;
    }

    // Strictly verify all 9 retired Quarantine slugs are absent from C++ registry and rejected by session
    static constexpr std::array<const char*, 9u> RetiredQuarantineSlugs = {{
        "blossom-branch-detach",
        "jelly-slime-blob",
        "slime-tendril-cluster",
        "splatter-burst",
        "impact-dust-ring",
        "splash",
        "puddle-ripple-splash",
        "car-puddle-splash",
        "mud-splatter-heavy",
    }};

    ParticleWorkspaceSession GuardSession{};
    for (const char* Slug : RetiredQuarantineSlugs)
    {
        if (!VerifyAssertion(
                ParticlePresetRegistry::VerifySlugExcludedFromRegistry(Slug),
                "Quarantined preset slug strictly excluded from C++ ParticlePresetRegistry"))
        {
            return false;
        }
        if (!VerifyAssertion(
                GuardSession.AddEffectFromPreset(Slug) == 0u,
                "ParticleWorkspaceSession refuses retired Quarantine preset slug"))
        {
            return false;
        }
    }

    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                       2. REUSED C++ FORCEFIELDS & SESSION LOGIC
//------------------------------------------------------------------------------------------------------------------------

bool ValidateForceFieldsReuseAndSession()
{
    ParticleWorkspaceSession Session{};
    if (!VerifyAssertion(Session.QueryForceSources().size() == 5u, "Default scene seeds 5 C++ ForceSourceRecordEntry laws"))
    {
        return false;
    }
    if (!VerifyAssertion(Session.QueryEffects().size() == 8u, "Default scene seeds 8 representative non-quarantined effects"))
    {
        return false;
    }
    if (!VerifyAssertion(Session.QueryAnemometerSamples().size() >= 32u, "Rolling Beaufort Anemometer trace primed from ForceFieldAccumulator"))
    {
        return false;
    }

    const ForceDiagnosticsReading WindDiag = Session.QueryAmbientWindDiagnostics();
    const float WindSpeed                  = VectorLength(WindDiag.TotalForce.AdvectionVelocity);
    if (!VerifyAssertion(std::isfinite(WindSpeed) && WindSpeed > 1.0f, "Reused ForceFieldAccumulator produces finite positive wind reading"))
    {
        return false;
    }

    const uint32_t SplineToken = Session.AddForceSource(ForceModel::SplineForce);
    if (!VerifyAssertion(SplineToken != 0u && Session.QueryForceSources().size() == 6u, "Constructed C++ ForceModel::SplineForce source"))
    {
        return false;
    }

    ParticleInspectorPanel Inspector{};
    Inspector.AttachSession(&Session);
    if (!VerifyAssertion(Inspector.QueryForceInspectorDelegated(), "Selecting ForceSource delegates Inspector directly to C++ ForceInspectorView"))
    {
        return false;
    }

    const uint32_t FireworksToken = Session.AddEffectFromPreset("fireworks");
    if (!VerifyAssertion(FireworksToken != 0u && !Inspector.QueryForceInspectorDelegated(), "Selecting ParticleEffect activates 4-tab Particle Inspector"))
    {
        return false;
    }

    const std::string Toml = Session.SerializeSelectedEffectTomlPreview();
    if (!VerifyAssertion(
            Toml.find("[ParticleSpace]") != std::string::npos
                && Toml.find("SimulationSpace::ForceFields") != std::string::npos,
            ".ParticleSpace TOML preview includes ForceFields coupling header"))
    {
        return false;
    }

    return true;
}

bool ValidatePlaybackScrubberControl(ImGuiIO& Io)
{
    using namespace Frontier::PlaybackScrubber;

    char TimeBuf[24]{};
    FormatTimecode(71.400f, TimeBuf, static_cast<uint32_t>(sizeof(TimeBuf)));
    if (!VerifyAssertion(std::string(TimeBuf) == "01:11.400", "PlaybackScrubber::FormatTimecode produces MM:SS.mmm"))
    {
        return false;
    }

    if (!VerifyAssertion(
            std::fabs(QueryNextSpeedMultiplier(0.25f) - 0.5f) < 1.0e-4f
                && std::fabs(QueryNextSpeedMultiplier(0.5f) - 1.0f) < 1.0e-4f
                && std::fabs(QueryNextSpeedMultiplier(1.0f) - 2.0f) < 1.0e-4f
                && std::fabs(QueryNextSpeedMultiplier(2.0f) - 0.25f) < 1.0e-4f,
            "PlaybackScrubber speed cycle advances 0.25x -> 0.5x -> 1.0x -> 2.0x -> 0.25x"))
    {
        return false;
    }

    ParticleWorkspaceSession Session{};
    Session.SeekPlaybackSeconds(6.0f);
    if (!VerifyAssertion(
            std::fabs(Session.QueryPlaybackClock().ElapsedSeconds - 6.0f) < 1.0e-3f
                && std::fabs(Session.QuerySimulationTimeSec() - 6.0) < 1.0e-3,
            "ParticleWorkspaceSession::SeekPlaybackSeconds synchronizes ForceFields clock"))
    {
        return false;
    }

    const Rectangle2D ControlBounds{ 20.0f, 20.0f, 820.0f, FullTransportHeight };
    auto ExercisePointer = [&](const ControlPointer& Ptr, bool Dragging) -> PlaybackScrubberInteraction
    {
        Io.DisplaySize = ImVec2(900.0f, 200.0f);
        Io.DeltaTime   = 1.0f / 60.0f;
        ImGui::NewFrame();
        ImDrawList* DrawList = ImGui::GetBackgroundDrawList();
        PixelSpace  Surface(DrawList, { 0.0f, 0.0f }, 1.0f);
        const PlaybackScrubberInteraction Interaction = ConstructFullTransport(
            Surface, ControlBounds, Session.QueryPlaybackClock(), Dragging, Ptr, 1.0f);
        ImGui::Render();
        return Interaction;
    };

    // Click Play/Pause circular button (X = 20 + 16 + 36 + 17 = 89, Y = 20 + 26 = 46)
    const ControlPointer PlayClick{ 89.0f, 46.0f, false, false, true, true };
    const PlaybackScrubberInteraction PlayResult = ExercisePointer(PlayClick, false);
    if (!VerifyAssertion(PlayResult.PlayPauseToggled, "PlaybackScrubber Play/Pause button toggles cleanly"))
    {
        return false;
    }
    Session.ApplyPlaybackInteraction(PlayResult);
    if (!VerifyAssertion(!Session.QueryPlaybackClock().Playing, "Session pauses after Play/Pause toggle"))
    {
        return false;
    }

    // Click Single-Frame Step button (X = 20 + 16 + 36 + 40 + 15 = 127, Y = 46)
    const ControlPointer StepClick{ 127.0f, 46.0f, false, false, true, true };
    const PlaybackScrubberInteraction StepResult = ExercisePointer(StepClick, false);
    if (!VerifyAssertion(StepResult.StepForwardTriggered, "PlaybackScrubber Step button triggers single-frame advance"))
    {
        return false;
    }
    Session.ApplyPlaybackInteraction(StepResult);
    if (!VerifyAssertion(Session.QueryPlaybackClock().ElapsedSeconds > 6.01f, "Single-frame step advances playhead by 1/60s"))
    {
        return false;
    }

    // Click-and-drag scrub to midpoint of animation play slider ruler (RulerX = 20 + 16 + 90 + 14 = 140, RulerW = 684 -> MidX = 482)
    const ControlPointer ScrubDrag{ 482.0f, 20.0f + TransportRowHeight + 24.0f, true, true, false, true };
    const PlaybackScrubberInteraction ScrubResult = ExercisePointer(ScrubDrag, false);
    if (!VerifyAssertion(
            ScrubResult.Scrubbed && ScrubResult.DraggingPlayhead && std::fabs(ScrubResult.UpdatedSeconds - 15.0f) < 0.35f,
            "PlaybackScrubber animation play slider scrubs accurately to midpoint (15.0s)"))
    {
        return false;
    }
    Session.ApplyPlaybackInteraction(ScrubResult);
    if (!VerifyAssertion(
            Session.QueryScrubbingPlayhead() && std::fabs(Session.QueryPlaybackClock().ElapsedSeconds - 15.0f) < 0.35f,
            "Session applies animation play slider scrub position"))
    {
        return false;
    }

    // Click Reset button (X = 20 + 16 + 15 = 51, Y = 46)
    const ControlPointer ResetClick{ 51.0f, 46.0f, false, false, true, true };
    const PlaybackScrubberInteraction ResetResult = ExercisePointer(ResetClick, false);
    Session.ApplyPlaybackInteraction(ResetResult);
    if (!VerifyAssertion(
            ResetResult.ResetTriggered && std::fabs(Session.QueryPlaybackClock().ElapsedSeconds) < 1.0e-4f,
            "PlaybackScrubber Reset button rewinds clock to 0.0s"))
    {
        return false;
    }

    return true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    if (!ValidatePresetRegistryAndQuarantineExclusion())
    {
        return 1;
    }
    if (!ValidateForceFieldsReuseAndSession())
    {
        return 1;
    }

    const std::filesystem::path RepoRoot = ResolveRepositoryRootPath();
    const std::filesystem::path ImageDir = RepoRoot / "Exhibits/Images/Particles";
    std::filesystem::create_directories(ImageDir);

    IMGUI_CHECKVERSION();
    ImGuiContext* GuiCtx = ImGui::CreateContext();
    ImGui::SetCurrentContext(GuiCtx);
    ImGuiIO& Io    = ImGui::GetIO();
    Io.IniFilename = nullptr;
    Io.LogFilename = nullptr;

    TypographyRepository Typefaces{};
    (void)Typefaces.LoadRepository(RepoRoot / "EngineContent/FontsArchives");
    TypographyRepository::AssignCurrent(&Typefaces);
    if (ImFont* TabFont = Typefaces.QueryTabStripFont())
    {
        Io.FontDefault = TabFont;
    }
    else
    {
        Io.Fonts->AddFontDefault();
    }
    unsigned char* AtlasPixels = nullptr;
    int            AtlasW      = 0;
    int            AtlasH      = 0;
    Io.Fonts->GetTexDataAsRGBA32(&AtlasPixels, &AtlasW, &AtlasH);

    if (!ValidatePlaybackScrubberControl(Io))
    {
        ImGui::DestroyContext(GuiCtx);
        return 1;
    }

    ParticleWorkspaceSession Session{};
    ParticleOutlinerPanel    Outliner{};
    ParticleInspectorPanel   Inspector{};
    Outliner.AttachSession(&Session);
    Inspector.AttachSession(&Session);

    const PaneInputReading IdleInput{};

    // Snapshot 1: Outliner + Effect Inspector (Overview Tab on Magnetic Ferrofluid Dish)
    {
        Session.SelectParticleEffect(Session.QueryEffects().front().UniqueToken);
        Outliner.ConfigureFilterAll();
        Outliner.CloseConstructSheet();
        Inspector.ConfigureActiveTab(ParticleInspectorTab::Overview);

        const bool Ok = CaptureTwoPanePng(
            Io,
            880u,
            920u,
            ImageDir / "ParticleWorkspace_Outliner_And_EffectInspector_Overview.png",
            [&](PixelSpace& Surface)
            {
                Outliner.Render(Surface, { 12.0f, 12.0f, 360.0f, 896.0f }, IdleInput, 1.0 / 60.0);
                Surface.StrokeRectangle({ 12.0f, 12.0f, 360.0f, 896.0f }, ColorRgba{ 48u, 48u, 52u, 255u }, 0.0f, 1.0f);

                Inspector.Render(Surface, { 384.0f, 12.0f, 484.0f, 896.0f }, IdleInput, 1.0 / 60.0);
                Surface.StrokeRectangle({ 384.0f, 12.0f, 484.0f, 896.0f }, ColorRgba{ 48u, 48u, 52u, 255u }, 0.0f, 1.0f);
            });
        if (!Ok)
        {
            ImGui::DestroyContext(GuiCtx);
            return 1;
        }
    }

    // Snapshot 2: Effect Inspector Source Tab + Motion & FX Tab (New Year's Fireworks Spectacle)
    {
        const uint32_t FireworksToken = Session.QueryEffects()[1u].UniqueToken;
        Session.SelectParticleEffect(FireworksToken);

        ParticleInspectorPanel SourceView{};
        ParticleInspectorPanel MotionView{};
        SourceView.AttachSession(&Session);
        MotionView.AttachSession(&Session);
        SourceView.ConfigureActiveTab(ParticleInspectorTab::Source);
        MotionView.ConfigureActiveTab(ParticleInspectorTab::MotionAndFx);

        const bool Ok = CaptureTwoPanePng(
            Io,
            980u,
            920u,
            ImageDir / "ParticleWorkspace_EffectInspector_Source_And_Motion.png",
            [&](PixelSpace& Surface)
            {
                SourceView.Render(Surface, { 12.0f, 12.0f, 472.0f, 896.0f }, IdleInput, 1.0 / 60.0);
                Surface.StrokeRectangle({ 12.0f, 12.0f, 472.0f, 896.0f }, ColorRgba{ 48u, 48u, 52u, 255u }, 0.0f, 1.0f);

                MotionView.Render(Surface, { 496.0f, 12.0f, 472.0f, 896.0f }, IdleInput, 1.0 / 60.0);
                Surface.StrokeRectangle({ 496.0f, 12.0f, 472.0f, 896.0f }, ColorRgba{ 48u, 48u, 52u, 255u }, 0.0f, 1.0f);
            });
        if (!Ok)
        {
            ImGui::DestroyContext(GuiCtx);
            return 1;
        }
    }

    // Snapshot 3: Reused C++ ForceFields Inspector (Helical Column selected -> direct ForceInspectorView delegation)
    {
        const uint32_t VortexToken = Session.QueryForceSources()[3u].UniqueToken;
        Session.SelectForceSource(VortexToken);
        Outliner.ConfigureFilterForces();
        Inspector.ConfigureForceInspectorTab(ForceInspectorTab::Field);

        const bool Ok = CaptureTwoPanePng(
            Io,
            880u,
            920u,
            ImageDir / "ParticleWorkspace_ReusedForceFields_Inspector.png",
            [&](PixelSpace& Surface)
            {
                Outliner.Render(Surface, { 12.0f, 12.0f, 360.0f, 896.0f }, IdleInput, 1.0 / 60.0);
                Surface.StrokeRectangle({ 12.0f, 12.0f, 360.0f, 896.0f }, ColorRgba{ 48u, 48u, 52u, 255u }, 0.0f, 1.0f);

                Inspector.Render(Surface, { 384.0f, 12.0f, 484.0f, 896.0f }, IdleInput, 1.0 / 60.0);
                Surface.StrokeRectangle({ 384.0f, 12.0f, 484.0f, 896.0f }, ColorRgba{ 48u, 48u, 52u, 255u }, 0.0f, 1.0f);
            });
        if (!Ok)
        {
            ImGui::DestroyContext(GuiCtx);
            return 1;
        }
    }

    // Snapshot 4: Outliner '+' Construct Sheet (93 Non-Quarantined Presets) + Effect Inspector Spec & Export (.ParticleSpace TOML)
    {
        const uint32_t FireworksToken = Session.QueryEffects()[1u].UniqueToken;
        Session.SelectParticleEffect(FireworksToken);
        Outliner.ConfigureFilterAll();
        Outliner.OpenConstructSheet(ConstructSheetPage::ParticlePresets);
        Inspector.ConfigureActiveTab(ParticleInspectorTab::SpecAndExport);

        const bool Ok = CaptureTwoPanePng(
            Io,
            880u,
            920u,
            ImageDir / "ParticleWorkspace_Outliner_ConstructSheet_And_SpecExport.png",
            [&](PixelSpace& Surface)
            {
                Outliner.Render(Surface, { 12.0f, 12.0f, 360.0f, 896.0f }, IdleInput, 1.0 / 60.0);
                Surface.StrokeRectangle({ 12.0f, 12.0f, 360.0f, 896.0f }, ColorRgba{ 48u, 48u, 52u, 255u }, 0.0f, 1.0f);

                Inspector.Render(Surface, { 384.0f, 12.0f, 484.0f, 896.0f }, IdleInput, 1.0 / 60.0);
                Surface.StrokeRectangle({ 384.0f, 12.0f, 484.0f, 896.0f }, ColorRgba{ 48u, 48u, 52u, 255u }, 0.0f, 1.0f);
            });
        if (!Ok)
        {
            ImGui::DestroyContext(GuiCtx);
            return 1;
        }
    }

    // Snapshot 5: Frontier UI Controls — PlaybackScrubber (Animation Play Slider + 52px Transport) in Dark & Light Themes
    {
        ThemeRepository DarkTheme{};
        DarkTheme.ConfigureTheme(ThemePreset::Dark);
        ThemeRepository LightTheme{};
        LightTheme.ConfigureTheme(ThemePreset::Light);

        const bool Ok = CaptureTwoPanePng(
            Io,
            960u,
            520u,
            ImageDir / "ParticleWorkspace_PlaybackScrubber_Control.png",
            [&](PixelSpace& Surface)
            {
                using namespace Frontier::PlaybackScrubber;

                ControlAppearance::ConfigureTheme(DarkTheme);
                Surface.FillRectangle({ 0.0f, 0.0f, 960.0f, 520.0f }, ColorRgba{ 14u, 14u, 17u, 255u }, 0.0f);

                WorkspaceAppearance::ConstructWeightedText(
                    Surface,
                    FontWeightClass::SemiBold,
                    { 24.0f, 18.0f },
                    14.0f,
                    ColorRgba{ 238u, 238u, 242u, 255u },
                    "Engine/Components/Controls/PlaybackScrubber — Animation Play Slider & Transport");
                WorkspaceAppearance::ConstructWeightedText(
                    Surface,
                    FontWeightClass::Regular,
                    { 24.0f, 38.0f },
                    10.5f,
                    ColorRgba{ 142u, 142u, 152u, 255u },
                    "Reusable Frontier UI control: 52px playback transport + 44px draggable timeline scrubber (Dark & Light themes)");

                // Variant 1: Full Transport + Animation Play Slider (Playing, Live / uncached)
                PlaybackClockParameters PlayingClock{};
                PlayingClock.ElapsedSeconds      = 11.400f;
                PlayingClock.LoopDurationSeconds = 30.0f;
                PlayingClock.FrameRateHz         = 60.0f;
                PlayingClock.SpeedMultiplier     = 1.0f;
                PlayingClock.Playing             = true;
                PlayingClock.LiveUncached        = true;
                PlayingClock.TrackCaption        = "Active particle & force field — Magnetic Ferrofluid Dish";
                PlayingClock.ActionLabel         = "Burst";
                PlayingClock.ActionShortcut      = "B";

                WorkspaceAppearance::ConstructWeightedText(
                    Surface,
                    FontWeightClass::Medium,
                    { 24.0f, 68.0f },
                    10.5f,
                    ColorRgba{ 184u, 184u, 194u, 255u },
                    "01 / Full Transport + Animation Play Slider (Playing • Live / uncached)");
                (void)ConstructFullTransport(
                    Surface,
                    { 24.0f, 88.0f, 912.0f, FullTransportHeight },
                    PlayingClock,
                    false,
                    ControlPointer{},
                    1.0f);
                Surface.StrokeRectangle({ 24.0f, 88.0f, 912.0f, FullTransportHeight }, ColorRgba{ 54u, 54u, 60u, 255u }, 0.0f, 1.0f);

                // Variant 2: Full Transport + Animation Play Slider (Paused + Active Drag-Scrubbing)
                PlaybackClockParameters ScrubbingClock{};
                ScrubbingClock.ElapsedSeconds      = 22.750f;
                ScrubbingClock.LoopDurationSeconds = 30.0f;
                ScrubbingClock.FrameRateHz         = 60.0f;
                ScrubbingClock.SpeedMultiplier     = 0.5f;
                ScrubbingClock.Playing             = false;
                ScrubbingClock.LiveUncached        = false;
                ScrubbingClock.TrackCaption        = "Scrubbing cached simulation cache — New Year's Text & Shape Fireworks";
                ScrubbingClock.ActionLabel         = "Burst";
                ScrubbingClock.ActionShortcut      = "B";

                WorkspaceAppearance::ConstructWeightedText(
                    Surface,
                    FontWeightClass::Medium,
                    { 24.0f, 206.0f },
                    10.5f,
                    ColorRgba{ 184u, 184u, 194u, 255u },
                    "02 / Full Transport + Animation Play Slider (Paused • Active Drag-Scrubbing • 0.5x)");
                const ControlPointer HoverScrubPtr{ 750.0f, 226.0f + TransportRowHeight + 24.0f, false, false, false, true };
                (void)ConstructFullTransport(
                    Surface,
                    { 24.0f, 226.0f, 912.0f, FullTransportHeight },
                    ScrubbingClock,
                    false,
                    HoverScrubPtr,
                    1.0f);
                Surface.StrokeRectangle({ 24.0f, 226.0f, 912.0f, FullTransportHeight }, ColorRgba{ 54u, 54u, 60u, 255u }, 0.0f, 1.0f);

                // Variant 3: Standalone Compact Animation Play Slider (Dark Theme)
                WorkspaceAppearance::ConstructWeightedText(
                    Surface,
                    FontWeightClass::Medium,
                    { 24.0f, 344.0f },
                    10.5f,
                    ColorRgba{ 184u, 184u, 194u, 255u },
                    "03 / Standalone Compact Animation Play Slider (44px Rail • Dark Theme)");
                PlaybackClockParameters CompactDarkClock{};
                CompactDarkClock.ElapsedSeconds      = 15.0f;
                CompactDarkClock.LoopDurationSeconds = 30.0f;
                CompactDarkClock.RulerHeading        = "ANIMATION";
                CompactDarkClock.TrackCaption        = "Helical Column force motion rail • Catmull spline loop";
                (void)ConstructScrubberRail(
                    Surface,
                    { 24.0f, 364.0f, 912.0f, ScrubberRailHeight },
                    CompactDarkClock,
                    false,
                    ControlPointer{},
                    1.0f);
                Surface.StrokeRectangle({ 24.0f, 364.0f, 912.0f, ScrubberRailHeight }, ColorRgba{ 54u, 54u, 60u, 255u }, 0.0f, 1.0f);

                // Variant 4: Standalone Compact Animation Play Slider (Light Theme)
                WorkspaceAppearance::ConstructWeightedText(
                    Surface,
                    FontWeightClass::Medium,
                    { 24.0f, 428.0f },
                    10.5f,
                    ColorRgba{ 184u, 184u, 194u, 255u },
                    "04 / Standalone Compact Animation Play Slider (44px Rail • Light Theme)");
                ControlAppearance::ConfigureTheme(LightTheme);
                PlaybackClockParameters CompactLightClock{};
                CompactLightClock.ElapsedSeconds      = 9.25f;
                CompactLightClock.LoopDurationSeconds = 30.0f;
                CompactLightClock.RulerHeading        = "PLAYBACK";
                CompactLightClock.TrackCaption        = "Coupled ForceFields + Particle Workspace timeline";
                (void)ConstructScrubberRail(
                    Surface,
                    { 24.0f, 448.0f, 912.0f, ScrubberRailHeight },
                    CompactLightClock,
                    false,
                    ControlPointer{},
                    1.0f);
                Surface.StrokeRectangle({ 24.0f, 448.0f, 912.0f, ScrubberRailHeight }, ColorRgba{ 192u, 196u, 204u, 255u }, 0.0f, 1.0f);

                ControlAppearance::ConfigureTheme(DarkTheme);
            });
        if (!Ok)
        {
            ImGui::DestroyContext(GuiCtx);
            return 1;
        }
    }

    ImGui::DestroyContext(GuiCtx);
    std::printf(
        "[ParticleWorkspaceUIValidation] PASS — 93 active presets, 0 quarantined slugs, C++ ForceFields & ForceInspectorView reused, PlaybackScrubber verified, 5 PNG proofs written.\n");
    return 0;
}
