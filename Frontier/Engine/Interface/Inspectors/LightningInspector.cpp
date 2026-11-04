/*==========================================================================================================================================
                                                           LIGHTNINGINSPECTOR.CPP
==========================================================================================================================================*/
// 🧩 NativeLightningPanel.js controls, reused on the Clouds weather tab and the Lightning record without fake strikes.

#include "Engine/Interface/Inspectors/LightningInspector.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/InspectorControls.h"

#include <cstdio>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    LIGHTNING CARD
//------------------------------------------------------------------------------------------------------------------------

void LightningInspector::ConstructControls(ControlColumn &Page, LightningConfiguration &Parameters)
{
    CardLook Look;
    Look.Fill            = ResolveCssColour(0x252525FF);
    Look.FillEnd         = ResolveCssColour(0x202020FF);
    Look.GradientDegrees = 135;
    Look.GradientStop    = 1;
    Look.PaddingTop      = 22;
    Look.PaddingSide = Look.PaddingBottom = 24;
    Look.HeadingGlyph                     = "Sparkles";
    Look.HeadingMargin                    = 28;
    Look.Border                           = ResolveCssColour(0x343434FF);
    Look.Radius                           = 22;
    Page.BeginCard(Look, "Lightning & thunder", nullptr);
    CaptionText::Construct(Page, "Opt-in weather effect · bright flashes. Cloud-spanning illumination and branching particle channels; "
                                 "exposure and sun settings are unchanged.");
    CaptionText::Construct(
        Page, "Authoring controls only: native storm execution, thunder audio and lightning-particle authoring are not connected.");
    const SwitchTile      Switches[] = {{"Lightning", &Parameters.Enabled, TileGlyph::Sun, false, false, nullptr, "Power"},
                                        {"Automatic strikes", &Parameters.AutoStormDischarge, TileGlyph::Sun, false, false, nullptr, "Power"}};
    bool                  Changed    = SwitchTiles::Construct(Page, "LightningSwitches", Switches, 2, 2);
    constexpr const char *Modes[]    = {"Cloud → ground", "Inside cloud"};
    constexpr const char *Sources[]  = {"Auto · global, or local if no global", "Global cloud layer", "Local cloud volume"};
    int                   Mode = static_cast<int>(Parameters.Discharge), Source = static_cast<int>(Parameters.CloudSource);
    Changed |= SelectRow::Construct(Page, "LightningMode", "Discharge", Modes, 2, Mode);
    Changed |= SelectRow::Construct(Page, "LightningCloudTarget", "Cloud source", Sources, 3, Source);
    Parameters.Discharge        = Mode;
    Parameters.CloudSource      = Source;
    const ActionEntry Actions[] = {
        {"StrikeAhead", "Strike ahead", false, "No native storm executor is attached."},
        {"ThunderSound", "Enable thunder sound", false, "Native thunder playback is not connected."},
        {"LightningAuthor", "Author lightning in Particle Editor", false, "Native lightning-particle authoring is not connected."}};
    ActionRowLook ActionsLook;
    ActionsLook.PaddingSide = 12;
    ActionsLook.MarginTop = ActionsLook.MarginBottom = 0;
    ActionRow::Construct(Page, Actions, ActionsLook);
    Changed |= SliderRow::Construct(Page, "LightningDistance", "Approx. strike distance", Parameters.DistanceMetres, 50, 6000, 50, "m");
    if (SliderRow::Construct(Page, "LightningIntervalMin", "Minimum interval", Parameters.MinimumIntervalSeconds, 2, 60, 0.5f, "s"))
    {
        Parameters.MaximumIntervalSeconds = std::max(Parameters.MinimumIntervalSeconds, Parameters.MaximumIntervalSeconds);
        Changed                           = true;
    }
    if (SliderRow::Construct(Page, "LightningIntervalMax", "Maximum interval", Parameters.MaximumIntervalSeconds, 2, 60, 0.5f, "s"))
    {
        Parameters.MinimumIntervalSeconds = std::min(Parameters.MinimumIntervalSeconds, Parameters.MaximumIntervalSeconds);
        Changed                           = true;
    }
    Changed |=
        SliderRow::Construct(Page, "LightningSpread", "Horizontal cloud channel span", Parameters.ChannelSpanMetres, 400, 10000, 100, "m");
    Changed |= SliderRow::Construct(Page, "LightningBrightness", "Bolt brightness", Parameters.BoltBrightness, 0, 3, 0.05f, "");
    Changed |= SliderRow::Construct(Page, "LightningCloudGlow", "Cloud flash strength", Parameters.CloudFlashStrength, 0, 3, 0.05f, "");
    Changed |= SliderRow::Construct(Page, "LightningRadius", "Cloud flash radius", Parameters.FlashRadiusMetres, 200, 6000, 100, "m");
    char Readout[160]{};
    std::snprintf(Readout, sizeof(Readout), "Storm unavailable · thunder ≈ %.1f s at ground-level strike distance",
                  static_cast<double>(Parameters.DistanceMetres / 343.0f));
    CaptionText::Construct(Page, Readout);
    CaptionText::Construct(Page, "Sound requires a click; distance determines delay, filtering and stereo position. In-cloud thunder uses "
                                 "3D distance. Ground endpoints use Z=0.");
    Page.EndCard();
    if (Changed) Parameters.AdvanceRevision();
}

void LightningInspector::ConstructInspector()
{
    const auto    Theme = ControlTheme::ResolveNative();
    const auto    Faces = EditorTypefaces::Resolve();
    ControlColumn Page(Theme, Faces, InspectorRuntime::Instance().ResolveInterfaceScale());
    ConstructControls(Page, Parameters);
    Page.Finalize();
}

void LightningInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry> &OutSwitches)
{
    OutSwitches.push_back({"Auto Discharge", "Authoring", &Parameters.AutoStormDischarge});
}

} // namespace Frontier

#elif FRONTIER_EDITOR

namespace Frontier
{
void LightningInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry> &) {}
void LightningInspector::ConstructInspector() {}
} // namespace Frontier

#endif
