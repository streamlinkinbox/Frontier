/*============================================================================================================================================
                                                       PARTICLEPRESETREGISTRY.CPP
============================================================================================================================================*/
// 🧩 Implementation of the active non-quarantined 93-preset Particle Studio registry (all 9 Quarantine slugs strictly excluded)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Particle/ParticlePresetRegistry.h"

#include <array>

namespace Frontier::Workspaces
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 ACTIVE CATEGORIES (14)
//------------------------------------------------------------------------------------------------------------------------

constexpr std::array<ParticleCategoryDescriptor, static_cast<std::size_t>(ParticleEffectCategory::Count)> ActiveCategories = {{
    { ParticleEffectCategory::ClothRigging,     "Cloth & Rigging",     "Cloth",    "CLOTH",     { 218u, 152u, 114u, 255u } },
    { ParticleEffectCategory::BotanicalFlora,     "Foliage & Flora",     "Flora",    "FLORA",     { 126u, 198u, 118u, 255u } },
    { ParticleEffectCategory::FerrofluidFields, "Ferrofluid & Fields", "Ferro",    "FERRO",     { 158u, 192u, 224u, 255u } },
    { ParticleEffectCategory::FractureSeismic,   "Fracture & Cracks",   "Fracture", "CRACK",     { 126u, 206u, 240u, 255u } },
    { ParticleEffectCategory::MotionLattice,      "Motion & Grids",      "MoGraph",  "MOGRAPH",   { 244u, 118u, 102u, 255u } },
    { ParticleEffectCategory::CurlFilaments,         "Silk & Curl",         "Curl",     "CURL",      { 118u, 186u, 255u, 255u } },
    { ParticleEffectCategory::DerezTransitions, "Transitions & Derez", "Derez",    "DEREZ",     { 102u, 218u, 244u, 255u } },
    { ParticleEffectCategory::SpaceDistortion,  "Space & Distortion",  "Lens",     "LENS",      { 255u, 174u, 96u,  255u } },
    { ParticleEffectCategory::ShieldTech,      "Shields & Tech",      "Shield",   "TECH",      { 104u, 226u, 174u, 255u } },
    { ParticleEffectCategory::AtmosphericSky,    "Atmosphere & Sky",    "Sky",      "SKY",       { 244u, 214u, 128u, 255u } },
    { ParticleEffectCategory::BallisticSparks, "Ballistics & Sparks", "Sparks",   "BALLISTIC", { 255u, 198u, 114u, 255u } },
    { ParticleEffectCategory::PyroFluid,        "Pyro & Water",        "Pyro",     "PYRO",      { 255u, 134u, 78u,  255u } },
    { ParticleEffectCategory::WindBlown,           "Wind-Blown",          "Wind",     "WIND",      { 166u, 214u, 168u, 255u } },
    { ParticleEffectCategory::StormWeather,    "Storms & Weather",    "Weather",  "WEATHER",   { 168u, 204u, 232u, 255u } },
}};

//------------------------------------------------------------------------------------------------------------------------
//                                           ACTIVE NON-QUARANTINED PRESETS (93)
//------------------------------------------------------------------------------------------------------------------------

constexpr std::array<ParticlePresetDescriptor, 93u> ActivePresets = {{
    { "coin-fountain", "Coin fountain", "Coin", ParticleEffectCategory::BallisticSparks, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(5u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(0u), 400u, 0.0f, 0.1f, 5.0f, 8.0f, 6.0f, 9.0f, 0.16f, 0.16f, 0.08f, 1.0f, 0u, 0u, 0u, 0u, 0u, { 255u, 147u, 12u, 255u }, { 255u, 111u, 4u, 255u }, { 255u, 229u, 111u, 255u } },
    { "magnetic-particles", "Magnetic particles", "Dipole Guided", ParticleEffectCategory::BallisticSparks, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(3u), ParticleBlendCategory::AdditiveGlow, static_cast<ParticleReleaseVolume>(1u), 1200u, 220.0f, 2.4f, 0.0f, 0.4f, 6.0f, 10.0f, 0.07f, 0.05f, 0.1f, 0.0f, 0u, 0u, 0u, 0u, 0u, { 255u, 65u, 18u, 255u }, { 255u, 173u, 90u, 255u }, { 255u, 255u, 255u, 255u } },
    { "bubbles", "Bubbles", "Bubble", ParticleEffectCategory::PyroFluid, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(6u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(2u), 400u, 25.0f, 0.8f, 0.2f, 0.6f, 5.0f, 8.0f, 0.12f, 0.2f, 0.15f, 0.0f, 0u, 0u, 0u, 0u, 0u, { 133u, 208u, 255u, 255u }, { 206u, 239u, 255u, 255u }, { 255u, 255u, 255u, 255u } },
    { "foam", "Foam", "Bubble", ParticleEffectCategory::PyroFluid, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(6u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(2u), 1500u, 500.0f, 1.8f, 0.2f, 1.0f, 4.0f, 7.0f, 0.05f, 0.06f, 1.5f, 1.0f, 0u, 0u, 0u, 0u, 0u, { 231u, 246u, 255u, 255u }, { 171u, 218u, 248u, 255u }, { 255u, 255u, 255u, 255u } },
    { "sparks", "Sparks", "Streak", ParticleEffectCategory::BallisticSparks, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(0u), ParticleBlendCategory::AdditiveGlow, static_cast<ParticleReleaseVolume>(0u), 3000u, 160.0f, 0.1f, 2.0f, 6.0f, 0.6f, 1.6f, 0.035f, 0.01f, 0.5f, 1.0f, 0u, 0u, 0u, 0u, 0u, { 255u, 235u, 140u, 255u }, { 255u, 89u, 13u, 0u }, { 255u, 255u, 77u, 255u } },
    { "rain", "Rain streaks", "Streak", ParticleEffectCategory::StormWeather, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(0u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(3u), 5500u, 850.0f, 0.1f, 8.5f, 11.5f, 1.0f, 1.6f, 0.055f, 0.045f, 0.04f, 0.95f, 0u, 0u, 0u, 0u, 0u, { 194u, 230u, 255u, 224u }, { 163u, 209u, 250u, 64u }, { 255u, 255u, 77u, 0u } },
    { "hail", "Hail", "Ice Hail", ParticleEffectCategory::StormWeather, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(8u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(3u), 2000u, 160.0f, 0.1f, 7.2f, 10.5f, 2.5f, 4.0f, 0.09f, 0.085f, 0.03f, 1.15f, 0u, 0u, 0u, 0u, 0u, { 235u, 247u, 255u, 255u }, { 189u, 224u, 255u, 71u }, { 255u, 255u, 77u, 0u } },
    { "leaves", "Leaves", "Wind-driven", ParticleEffectCategory::WindBlown, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(2u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(2u), 1200u, 20.0f, 4.5f, 0.2f, 0.9f, 7.0f, 12.0f, 0.14f, 0.12f, 1.4f, 0.35f, 0u, 0u, 0u, 0u, 0u, { 107u, 184u, 56u, 255u }, { 204u, 133u, 41u, 255u }, { 255u, 255u, 77u, 0u } },
    { "paper", "Paper & debris", "Wind-driven", ParticleEffectCategory::WindBlown, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(2u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(1u), 600u, 8.0f, 2.5f, 0.1f, 0.5f, 6.0f, 10.0f, 0.12f, 0.1f, 0.6f, 0.25f, 0u, 0u, 0u, 0u, 0u, { 242u, 242u, 230u, 255u }, { 204u, 209u, 217u, 255u }, { 255u, 255u, 77u, 0u } },
    { "fire", "Fire", "Pyro / Glow", ParticleEffectCategory::PyroFluid, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(3u), ParticleBlendCategory::AdditiveGlow, static_cast<ParticleReleaseVolume>(1u), 2500u, 280.0f, 0.12f, 0.6f, 1.7f, 0.7f, 1.5f, 0.35f, 0.12f, 0.8f, 0.0f, 0u, 0u, 0u, 0u, 0u, { 255u, 217u, 89u, 255u }, { 242u, 51u, 13u, 0u }, { 255u, 255u, 77u, 0u } },
    { "smoke", "Smoke", "Pyro / Glow", ParticleEffectCategory::PyroFluid, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(3u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(1u), 1500u, 70.0f, 0.2f, 0.3f, 0.8f, 3.0f, 6.0f, 0.2f, 1.3f, 0.5f, 0.0f, 0u, 0u, 0u, 0u, 0u, { 153u, 153u, 163u, 128u }, { 64u, 64u, 71u, 0u }, { 255u, 255u, 77u, 0u } },
    { "explosion", "Explosion", "Pyro / Glow", ParticleEffectCategory::PyroFluid, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(3u), ParticleBlendCategory::AdditiveGlow, static_cast<ParticleReleaseVolume>(1u), 2000u, 0.0f, 0.1f, 3.0f, 9.0f, 0.5f, 1.2f, 0.15f, 0.6f, 2.6f, 0.4f, 0u, 0u, 0u, 0u, 0u, { 255u, 242u, 179u, 255u }, { 255u, 89u, 20u, 0u }, { 255u, 255u, 77u, 0u } },
    { "tornado", "Tornado debris", "Wind-driven", ParticleEffectCategory::StormWeather, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(2u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(2u), 1500u, 90.0f, 1.6f, 1.0f, 3.0f, 4.0f, 7.0f, 0.14f, 0.1f, 0.4f, 0.3f, 0u, 0u, 0u, 0u, 0u, { 92u, 79u, 66u, 255u }, { 133u, 117u, 97u, 0u }, { 255u, 255u, 77u, 0u } },
    { "sandstorm", "Sandstorm", "Wind-driven", ParticleEffectCategory::StormWeather, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(3u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(3u), 6000u, 900.0f, 0.1f, 5.0f, 8.0f, 4.0f, 7.0f, 0.07f, 0.05f, 1.0f, 0.2f, 0u, 0u, 0u, 0u, 0u, { 209u, 168u, 107u, 140u }, { 179u, 140u, 87u, 0u }, { 255u, 255u, 77u, 0u } },
    { "snow", "Snow", "Snowflake", ParticleEffectCategory::StormWeather, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(7u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(3u), 3500u, 280.0f, 0.1f, 0.45f, 1.0f, 8.0f, 12.0f, 0.095f, 0.07f, 0.75f, 0.14f, 0u, 0u, 0u, 0u, 0u, { 245u, 252u, 255u, 240u }, { 209u, 235u, 255u, 0u }, { 255u, 255u, 77u, 0u } },
    { "embers", "Embers", "Pyro / Glow", ParticleEffectCategory::PyroFluid, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(3u), ParticleBlendCategory::AdditiveGlow, static_cast<ParticleReleaseVolume>(1u), 900u, 45.0f, 0.5f, 0.8f, 2.2f, 2.0f, 4.2f, 0.035f, 0.012f, 0.7f, 0.0f, 0u, 0u, 0u, 0u, 0u, { 255u, 184u, 77u, 255u }, { 230u, 64u, 13u, 0u }, { 255u, 255u, 77u, 0u } },
    { "petals", "Cherry petals", "Wind-driven", ParticleEffectCategory::WindBlown, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(2u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(2u), 700u, 14.0f, 2.2f, 0.2f, 0.7f, 7.0f, 11.0f, 0.1f, 0.09f, 1.3f, 0.3f, 0u, 0u, 0u, 0u, 0u, { 255u, 189u, 209u, 255u }, { 245u, 148u, 184u, 255u }, { 255u, 255u, 77u, 0u } },
    { "sand", "Sand grains", "Sphere", ParticleEffectCategory::StormWeather, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(1u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(3u), 4000u, 350.0f, 0.1f, 0.8f, 2.2f, 2.5f, 4.0f, 0.025f, 0.022f, 0.1f, 1.0f, 0u, 0u, 0u, 0u, 0u, { 219u, 189u, 133u, 255u }, { 179u, 148u, 102u, 0u }, { 255u, 255u, 77u, 0u } },
    { "ash", "Ash flakes", "Wind-driven", ParticleEffectCategory::StormWeather, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(3u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(1u), 1500u, 30.0f, 0.6f, 0.3f, 1.0f, 6.0f, 10.0f, 0.05f, 0.08f, 1.2f, 0.12f, 0u, 0u, 0u, 0u, 0u, { 140u, 140u, 140u, 179u }, { 89u, 89u, 92u, 0u }, { 255u, 255u, 77u, 0u } },
    { "debris", "Small debris", "Streak", ParticleEffectCategory::StormWeather, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(0u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(1u), 800u, 30.0f, 0.6f, 2.0f, 5.0f, 3.0f, 5.0f, 0.035f, 0.03f, 0.3f, 1.0f, 0u, 0u, 0u, 0u, 0u, { 117u, 82u, 46u, 255u }, { 77u, 56u, 36u, 0u }, { 255u, 255u, 77u, 0u } },
    { "flares", "Countermeasure flares", "IR Countermeasure Flares", ParticleEffectCategory::AtmosphericSky, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(3u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(1u), 480u, 0.0f, 0.25f, 8.5f, 13.5f, 3.4f, 5.2f, 0.34f, 0.14f, 1.55f, 0.68f, 0u, 0u, 0u, 0u, 0u, { 255u, 252u, 235u, 255u }, { 214u, 224u, 240u, 0u }, { 255u, 168u, 51u, 255u } },
    { "pollen", "Pollen & dust motes", "Wind-driven", ParticleEffectCategory::AtmosphericSky, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(3u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(3u), 2000u, 90.0f, 0.1f, 0.05f, 0.2f, 8.0f, 12.0f, 0.03f, 0.03f, 0.6f, 0.02f, 0u, 0u, 0u, 0u, 0u, { 255u, 242u, 153u, 153u }, { 255u, 230u, 128u, 0u }, { 255u, 255u, 77u, 0u } },
    { "watermist", "Water mist", "Volumetric Water Mist", ParticleEffectCategory::PyroFluid, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(19u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(2u), 1800u, 220.0f, 0.95f, 0.9f, 2.6f, 1.9f, 3.8f, 0.3f, 1.55f, 1.9f, 0.08f, 0u, 0u, 0u, 0u, 0u, { 230u, 247u, 255u, 82u }, { 224u, 245u, 255u, 0u }, { 194u, 235u, 255u, 56u } },
    { "glass", "Glass shards", "3D Glass Shard", ParticleEffectCategory::BallisticSparks, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(18u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(1u), 420u, 26.0f, 0.42f, 2.2f, 5.4f, 2.2f, 3.8f, 0.24f, 0.16f, 0.14f, 1.0f, 0u, 0u, 0u, 0u, 0u, { 204u, 240u, 255u, 214u }, { 143u, 214u, 250u, 0u }, { 255u, 255u, 77u, 0u } },
    { "lapilli", "Volcanic ash & lapilli", "Sphere", ParticleEffectCategory::PyroFluid, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(1u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(1u), 500u, 25.0f, 0.4f, 6.0f, 12.0f, 3.0f, 5.0f, 0.08f, 0.06f, 0.1f, 1.0f, 0u, 0u, 0u, 0u, 0u, { 255u, 115u, 38u, 255u }, { 64u, 56u, 51u, 0u }, { 255u, 255u, 77u, 0u } },
    { "seeds", "Dandelion seeds", "Wind-driven", ParticleEffectCategory::WindBlown, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(3u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(1u), 400u, 15.0f, 0.6f, 0.1f, 0.4f, 8.0f, 12.0f, 0.04f, 0.035f, 1.4f, 0.08f, 0u, 0u, 0u, 0u, 0u, { 255u, 255u, 247u, 204u }, { 255u, 255u, 255u, 0u }, { 255u, 255u, 77u, 0u } },
    { "feathers", "Feathers", "Wind-driven", ParticleEffectCategory::WindBlown, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(2u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(1u), 300u, 6.0f, 0.6f, 0.1f, 0.3f, 8.0f, 12.0f, 0.13f, 0.12f, 1.2f, 0.2f, 0u, 0u, 0u, 0u, 0u, { 247u, 247u, 240u, 255u }, { 191u, 191u, 199u, 255u }, { 255u, 255u, 77u, 0u } },
    { "confetti", "Confetti & streamers", "Wind-driven", ParticleEffectCategory::WindBlown, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(2u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(1u), 1200u, 25.0f, 0.5f, 1.0f, 3.0f, 5.0f, 8.0f, 0.09f, 0.08f, 0.8f, 0.25f, 0u, 0u, 0u, 0u, 0u, { 255u, 209u, 77u, 255u }, { 255u, 102u, 153u, 255u }, { 255u, 255u, 77u, 0u } },
    { "steam", "Steam vent", "Pyro / Glow", ParticleEffectCategory::PyroFluid, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(3u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(2u), 1600u, 60.0f, 0.35f, 0.8f, 1.6f, 3.0f, 5.0f, 0.15f, 1.6f, 1.5f, 0.0f, 0u, 0u, 0u, 0u, 0u, { 235u, 240u, 245u, 115u }, { 204u, 209u, 217u, 0u }, { 255u, 255u, 77u, 0u } },
    { "fireworks", "Fireworks", "Multi-Tier Fireworks", ParticleEffectCategory::AtmosphericSky, ParticleAuthoringDomain::FireworksSpectacle, static_cast<ParticleVisualSilhouette>(0u), ParticleBlendCategory::AdditiveGlow, static_cast<ParticleReleaseVolume>(1u), 1200u, 0.0f, 0.15f, 4.0f, 9.0f, 1.8f, 2.9f, 0.15f, 0.035f, 1.45f, 0.42f, 0u, 0u, 0u, 0u, 0u, { 255u, 240u, 166u, 255u }, { 255u, 56u, 36u, 0u }, { 255u, 122u, 56u, 255u } },
    { "fireworks-spectacle", "New Year's Text & Shape Fireworks", "New Year's Pyro Spectacle", ParticleEffectCategory::AtmosphericSky, ParticleAuthoringDomain::FireworksSpectacle, static_cast<ParticleVisualSilhouette>(0u), ParticleBlendCategory::AdditiveGlow, static_cast<ParticleReleaseVolume>(1u), 1400u, 0.0f, 0.15f, 4.0f, 9.0f, 2.0f, 3.2f, 0.16f, 0.04f, 1.65f, 0.32f, 0u, 0u, 0u, 0u, 9u, { 255u, 235u, 107u, 255u }, { 255u, 61u, 163u, 0u }, { 82u, 235u, 255u, 255u } },
    { "curl-fur-field", "Curl Wave Field", "Curl Wave", ParticleEffectCategory::CurlFilaments, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(0u), ParticleBlendCategory::AdditiveGlow, static_cast<ParticleReleaseVolume>(3u), 10000u, 1800.0f, 0.1f, 0.3f, 1.1f, 2.8f, 4.6f, 0.042f, 0.036f, 2.6f, 0.0f, 0u, 0u, 0u, 0u, 0u, { 11u, 17u, 255u, 255u }, { 67u, 233u, 255u, 255u }, { 5u, 80u, 255u, 255u } },
    { "curl-sphere-plasma", "Spherical Curl Plasma", "Curl Sphere", ParticleEffectCategory::CurlFilaments, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(0u), ParticleBlendCategory::AdditiveGlow, static_cast<ParticleReleaseVolume>(1u), 9000u, 1500.0f, 1.65f, 0.4f, 1.6f, 3.0f, 5.2f, 0.038f, 0.03f, 2.2f, 0.0f, 0u, 0u, 0u, 0u, 0u, { 1u, 23u, 255u, 255u }, { 164u, 239u, 255u, 255u }, { 5u, 119u, 255u, 255u } },
    { "bokeh-stream-ribbons", "Bokeh Stream Ribbons", "Ribbon Stream", ParticleEffectCategory::CurlFilaments, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(9u), ParticleBlendCategory::AdditiveGlow, static_cast<ParticleReleaseVolume>(3u), 3200u, 580.0f, 0.1f, 0.3f, 1.0f, 4.5f, 7.5f, 0.046f, 0.032f, 1.8f, 0.0f, 0u, 0u, 0u, 0u, 0u, { 11u, 26u, 27u, 255u }, { 220u, 255u, 255u, 255u }, { 69u, 146u, 146u, 255u } },
    { "bokeh-object-pulse", "Surface Pour Ribbons", "Surface Pour", ParticleEffectCategory::CurlFilaments, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(9u), ParticleBlendCategory::AdditiveGlow, static_cast<ParticleReleaseVolume>(1u), 3400u, 620.0f, 1.65f, 0.4f, 1.1f, 3.4f, 5.2f, 0.048f, 0.032f, 1.4f, 0.0f, 0u, 0u, 0u, 0u, 0u, { 7u, 19u, 20u, 255u }, { 231u, 255u, 255u, 255u }, { 53u, 152u, 149u, 255u } },
    { "tesla-sphere-lightning", "Tesla Sphere Lightning", "Tesla Lightning", ParticleEffectCategory::CurlFilaments, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(0u), ParticleBlendCategory::AdditiveGlow, static_cast<ParticleReleaseVolume>(1u), 600u, 0.0f, 0.46f, 1.8f, 5.2f, 0.16f, 0.42f, 0.026f, 0.004f, 2.2f, 0.65f, 0u, 0u, 0u, 0u, 0u, { 11u, 51u, 255u, 255u }, { 255u, 255u, 255u, 255u }, { 67u, 198u, 255u, 255u } },
    { "static-sphere-shock", "Static Shock Arcs", "Static Shock", ParticleEffectCategory::CurlFilaments, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(9u), ParticleBlendCategory::AdditiveGlow, static_cast<ParticleReleaseVolume>(1u), 600u, 0.0f, 0.4f, 1.2f, 3.4f, 0.14f, 0.36f, 0.022f, 0.004f, 2.6f, 0.35f, 0u, 0u, 0u, 0u, 0u, { 85u, 19u, 255u, 255u }, { 233u, 253u, 255u, 255u }, { 29u, 226u, 255u, 255u } },
    { "shockwave-ring", "Blast & Sonic Shockwave", "Mach Shockwave", ParticleEffectCategory::BallisticSparks, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(10u), ParticleBlendCategory::AdditiveGlow, static_cast<ParticleReleaseVolume>(0u), 96u, 2.2f, 0.1f, 0.02f, 0.14f, 0.72f, 1.05f, 0.38f, 6.4f, 0.8f, 0.0f, 0u, 0u, 0u, 0u, 0u, { 255u, 255u, 255u, 255u }, { 6u, 51u, 255u, 255u }, { 67u, 190u, 255u, 255u } },
    { "emp-pulse-dome", "EMP Energy Pulse Dome", "EMP Pulse Dome", ParticleEffectCategory::ShieldTech, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(11u), ParticleBlendCategory::AdditiveGlow, static_cast<ParticleReleaseVolume>(0u), 96u, 1.1f, 0.1f, 0.0f, 0.05f, 0.9f, 1.25f, 0.36f, 6.8f, 0.4f, 0.0f, 0u, 0u, 0u, 0u, 0u, { 255u, 255u, 255u, 255u }, { 34u, 12u, 255u, 255u }, { 17u, 206u, 255u, 255u } },
    { "radar-sweep", "Tactical Radar Sweep & Blips", "Radar Sweep", ParticleEffectCategory::ShieldTech, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(15u), ParticleBlendCategory::AdditiveGlow, static_cast<ParticleReleaseVolume>(0u), 16u, 0.25f, 0.1f, 0.0f, 0.0f, 7.5f, 8.5f, 6.2f, 6.2f, 0.0f, 0.0f, 0u, 0u, 0u, 0u, 0u, { 19u, 255u, 122u, 255u }, { 1u, 87u, 37u, 255u }, { 2u, 168u, 73u, 255u } },
    { "sonar-ping", "Acoustic Sonar Pulse Ping", "Sonar Ping", ParticleEffectCategory::ShieldTech, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(15u), ParticleBlendCategory::AdditiveGlow, static_cast<ParticleReleaseVolume>(0u), 64u, 1.2f, 0.1f, 0.0f, 0.04f, 1.1f, 1.5f, 0.45f, 7.4f, 0.2f, 0.0f, 0u, 0u, 0u, 0u, 0u, { 164u, 255u, 235u, 255u }, { 1u, 52u, 65u, 255u }, { 8u, 233u, 168u, 255u } },
    { "plasma-laser-beam", "Plasma & Laser Beam", "Laser / Plasma Beam", ParticleEffectCategory::BallisticSparks, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(0u), ParticleBlendCategory::AdditiveGlow, static_cast<ParticleReleaseVolume>(0u), 1200u, 260.0f, 0.22f, 3.2f, 9.5f, 0.18f, 0.55f, 0.036f, 0.008f, 1.4f, 0.65f, 0u, 0u, 0u, 0u, 0u, { 255u, 255u, 255u, 255u }, { 7u, 40u, 255u, 255u }, { 12u, 222u, 255u, 255u } },
    { "plasma-tracers", "Bullet & Plasma Tracers", "Streak", ParticleEffectCategory::BallisticSparks, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(0u), ParticleBlendCategory::AdditiveGlow, static_cast<ParticleReleaseVolume>(0u), 1200u, 36.0f, 0.04f, 19.0f, 25.0f, 0.55f, 0.95f, 0.068f, 0.045f, 0.04f, 0.22f, 0u, 0u, 0u, 0u, 0u, { 255u, 239u, 171u, 255u }, { 255u, 17u, 1u, 255u }, { 255u, 103u, 6u, 255u } },
    { "projectile-trails", "Glowing Projectile Trails", "Projectile Trail", ParticleEffectCategory::BallisticSparks, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(9u), ParticleBlendCategory::AdditiveGlow, static_cast<ParticleReleaseVolume>(1u), 256u, 14.0f, 0.12f, 6.4f, 9.2f, 2.0f, 3.1f, 0.075f, 0.03f, 0.08f, 0.85f, 0u, 0u, 0u, 0u, 0u, { 5u, 27u, 255u, 255u }, { 255u, 255u, 255u, 255u }, { 22u, 222u, 255u, 255u } },
    { "welding-grinder-sparks", "Welding & Grinder Arc Spray", "Streak", ParticleEffectCategory::BallisticSparks, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(0u), ParticleBlendCategory::AdditiveGlow, static_cast<ParticleReleaseVolume>(0u), 3200u, 420.0f, 0.04f, 7.0f, 13.8f, 0.65f, 1.45f, 0.042f, 0.012f, 0.36f, 1.28f, 0u, 0u, 0u, 0u, 0u, { 255u, 246u, 206u, 255u }, { 255u, 20u, 1u, 255u }, { 255u, 128u, 9u, 255u } },
    { "hologram-data-glitch", "Hologram & Signal Glitch", "Holo Glitch", ParticleEffectCategory::ShieldTech, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(12u), ParticleBlendCategory::AdditiveGlow, static_cast<ParticleReleaseVolume>(3u), 900u, 110.0f, 0.1f, 0.85f, 1.95f, 1.8f, 3.3f, 0.2f, 0.16f, 0.05f, 0.0f, 0u, 0u, 0u, 0u, 0u, { 2u, 255u, 97u, 255u }, { 67u, 255u, 214u, 255u }, { 2u, 183u, 255u, 255u } },
    { "boids-flock", "Boids Flock (Birds / Bats / Fish)", "Boids Flock", ParticleEffectCategory::WindBlown, ParticleAuthoringDomain::BoidsFlock, static_cast<ParticleVisualSilhouette>(13u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(1u), 220u, 48.0f, 1.65f, 2.2f, 3.8f, 12.0f, 18.0f, 0.25f, 0.25f, 0.05f, 0.0f, 0u, 0u, 0u, 0u, 0u, { 5u, 8u, 14u, 255u }, { 76u, 107u, 149u, 255u }, { 17u, 32u, 60u, 255u } },
    { "boids-butterflies", "Butterfly & Wisp Swarm", "Butterfly Flock", ParticleEffectCategory::WindBlown, ParticleAuthoringDomain::BoidsFlock, static_cast<ParticleVisualSilhouette>(13u), ParticleBlendCategory::AdditiveGlow, static_cast<ParticleReleaseVolume>(1u), 180u, 36.0f, 1.8f, 1.2f, 2.3f, 10.0f, 16.0f, 0.26f, 0.24f, 0.05f, 0.0f, 0u, 0u, 0u, 0u, 0u, { 255u, 87u, 15u, 255u }, { 29u, 222u, 255u, 255u }, { 255u, 190u, 57u, 255u } },
    { "boids-fish-school", "Bioluminescent Fish School", "Fish School", ParticleEffectCategory::WindBlown, ParticleAuthoringDomain::BoidsFlock, static_cast<ParticleVisualSilhouette>(13u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(1u), 260u, 64.0f, 1.75f, 2.1f, 3.4f, 14.0f, 22.0f, 0.22f, 0.21f, 0.06f, 0.0f, 0u, 0u, 0u, 0u, 0u, { 3u, 67u, 255u, 255u }, { 255u, 70u, 27u, 255u }, { 12u, 233u, 255u, 255u } },
    { "viscous-dripping", "Viscous Dripping & Trails", "Viscous Drip", ParticleEffectCategory::PyroFluid, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(14u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(2u), 320u, 26.0f, 0.95f, 0.04f, 0.24f, 1.8f, 2.9f, 0.13f, 0.1f, 0.14f, 0.96f, 0u, 0u, 0u, 0u, 0u, { 8u, 190u, 73u, 255u }, { 2u, 67u, 25u, 255u }, { 97u, 255u, 183u, 255u } },
    { "derez-cube", "Derez Cube", "Shatter Derez", ParticleEffectCategory::DerezTransitions, ParticleAuthoringDomain::TransitionDerez, static_cast<ParticleVisualSilhouette>(4u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(4u), 1536u, 0.0f, 0.0f, 0.6f, 2.6f, 3.0f, 4.0f, 0.075f, 0.075f, 0.4f, 1.0f, 0u, 0u, 0u, 0u, 0u, { 9u, 171u, 255u, 255u }, { 9u, 171u, 255u, 255u }, { 255u, 255u, 255u, 255u } },
    { "cube-coins", "Cube to Coins", "Surface Transition", ParticleEffectCategory::DerezTransitions, ParticleAuthoringDomain::TransitionDerez, static_cast<ParticleVisualSilhouette>(5u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(4u), 384u, 0.0f, 0.0f, 1.5f, 4.0f, 4.0f, 6.0f, 0.14f, 0.14f, 0.2f, 1.0f, 0u, 0u, 0u, 0u, 0u, { 255u, 147u, 12u, 255u }, { 255u, 147u, 12u, 255u }, { 255u, 229u, 111u, 255u } },
    { "melt-away", "Melt Away", "Melt / Dissolve", ParticleEffectCategory::DerezTransitions, ParticleAuthoringDomain::TransitionDerez, static_cast<ParticleVisualSilhouette>(4u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(4u), 1536u, 0.0f, 0.0f, 0.2f, 0.9f, 6.0f, 8.0f, 0.075f, 0.05f, 0.6f, 0.2f, 0u, 0u, 0u, 0u, 0u, { 255u, 115u, 16u, 255u }, { 255u, 26u, 3u, 255u }, { 255u, 224u, 171u, 255u } },
    { "reverse-derez", "Reverse Derez", "Reverse Derez", ParticleEffectCategory::DerezTransitions, ParticleAuthoringDomain::TransitionDerez, static_cast<ParticleVisualSilhouette>(4u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(4u), 1536u, 0.0f, 0.0f, 0.5f, 2.0f, 3.0f, 4.0f, 0.075f, 0.075f, 0.4f, 0.0f, 0u, 0u, 0u, 0u, 0u, { 9u, 171u, 255u, 255u }, { 9u, 171u, 255u, 255u }, { 255u, 255u, 255u, 255u } },
    { "voxel-explosion", "Voxel Explosion", "Surface Transition", ParticleEffectCategory::DerezTransitions, ParticleAuthoringDomain::TransitionDerez, static_cast<ParticleVisualSilhouette>(4u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(1u), 1024u, 0.0f, 0.9f, 2.0f, 4.5f, 2.5f, 4.0f, 0.12f, 0.1f, 0.9f, 0.12f, 0u, 0u, 0u, 0u, 0u, { 255u, 26u, 12u, 255u }, { 255u, 163u, 34u, 255u }, { 255u, 255u, 255u, 255u } },
    { "coin-stack", "Coin Stack", "Coin Stack", ParticleEffectCategory::DerezTransitions, ParticleAuthoringDomain::TransitionDerez, static_cast<ParticleVisualSilhouette>(5u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(0u), 64u, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.4f, 0.4f, 0.0f, 0.0f, 0u, 0u, 0u, 0u, 0u, { 255u, 147u, 12u, 255u }, { 255u, 147u, 12u, 255u }, { 255u, 229u, 111u, 255u } },
    { "dissolve-nothing", "Dissolve to Nothing", "Melt / Dissolve", ParticleEffectCategory::DerezTransitions, ParticleAuthoringDomain::TransitionDerez, static_cast<ParticleVisualSilhouette>(4u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(4u), 1536u, 0.0f, 0.0f, 0.1f, 0.6f, 2.5f, 4.0f, 0.075f, 0.0f, 0.8f, 0.1f, 0u, 0u, 0u, 0u, 0u, { 54u, 171u, 255u, 255u }, { 147u, 222u, 255u, 255u }, { 255u, 255u, 255u, 255u } },
    { "dissolve-butterflies", "Dissolve into Butterflies", "Butterfly Dissolve", ParticleEffectCategory::DerezTransitions, ParticleAuthoringDomain::TransitionDerez, static_cast<ParticleVisualSilhouette>(4u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(4u), 1536u, 0.0f, 0.0f, 0.2f, 0.9f, 6.0f, 9.0f, 0.075f, 0.075f, 0.8f, 0.0f, 0u, 0u, 0u, 0u, 0u, { 255u, 82u, 12u, 255u }, { 12u, 147u, 255u, 255u }, { 255u, 255u, 255u, 255u } },
    { "glitch-dissolve", "Glitch Dissolve", "Glitch Dissolve", ParticleEffectCategory::DerezTransitions, ParticleAuthoringDomain::TransitionDerez, static_cast<ParticleVisualSilhouette>(4u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(4u), 1536u, 0.0f, 0.0f, 0.1f, 0.6f, 2.5f, 4.0f, 0.075f, 0.0f, 0.8f, 0.1f, 0u, 0u, 0u, 0u, 0u, { 0u, 255u, 161u, 255u }, { 255u, 7u, 41u, 255u }, { 255u, 255u, 255u, 255u } },
    { "black-hole", "Black Hole Accretion", "Black Hole Lens", ParticleEffectCategory::SpaceDistortion, ParticleAuthoringDomain::SpaceDistortion, static_cast<ParticleVisualSilhouette>(3u), ParticleBlendCategory::AdditiveGlow, static_cast<ParticleReleaseVolume>(2u), 3000u, 400.0f, 2.6f, 0.0f, 0.2f, 6.0f, 10.0f, 0.075f, 0.02f, 0.2f, 0.0f, 0u, 0u, 0u, 0u, 0u, { 255u, 161u, 65u, 255u }, { 255u, 37u, 3u, 255u }, { 255u, 255u, 255u, 255u } },
    { "heat-shimmer", "Heat Shimmer Distortion", "Heat Shimmer", ParticleEffectCategory::SpaceDistortion, ParticleAuthoringDomain::SpaceDistortion, static_cast<ParticleVisualSilhouette>(3u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(1u), 200u, 30.0f, 0.5f, 0.6f, 1.4f, 2.0f, 3.5f, 0.3f, 0.9f, 0.5f, 0.0f, 0u, 0u, 0u, 0u, 0u, { 6u, 8u, 11u, 255u }, { 12u, 17u, 23u, 255u }, { 255u, 255u, 255u, 255u } },
    { "strikeSparks", "Arc Strike Sparks", "Streak", ParticleEffectCategory::BallisticSparks, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(0u), ParticleBlendCategory::AdditiveGlow, static_cast<ParticleReleaseVolume>(0u), 1600u, 0.0f, 0.1f, 1.8f, 7.5f, 0.45f, 1.15f, 0.034f, 0.008f, 0.3f, 1.1f, 0u, 0u, 0u, 0u, 0u, { 217u, 240u, 255u, 255u }, { 89u, 158u, 255u, 0u }, { 255u, 255u, 255u, 255u } },
    { "energy-shield-hex", "Energy Shield & Hex Ripples", "Hex Energy Shield", ParticleEffectCategory::ShieldTech, ParticleAuthoringDomain::EnergyShield, static_cast<ParticleVisualSilhouette>(16u), ParticleBlendCategory::AdditiveGlow, static_cast<ParticleReleaseVolume>(0u), 24u, 0.28f, 0.1f, 0.0f, 0.0f, 6.5f, 7.5f, 3.1f, 3.1f, 0.0f, 0.0f, 0u, 0u, 0u, 0u, 0u, { 4u, 122u, 255u, 255u }, { 12u, 37u, 255u, 255u }, { 67u, 233u, 255u, 255u } },
    { "energy-shield-prism", "Multi-Shape Prism Barrier", "Multi-Shape Shield", ParticleEffectCategory::ShieldTech, ParticleAuthoringDomain::EnergyShield, static_cast<ParticleVisualSilhouette>(16u), ParticleBlendCategory::AdditiveGlow, static_cast<ParticleReleaseVolume>(0u), 24u, 0.28f, 0.1f, 0.0f, 0.0f, 6.5f, 7.5f, 3.1f, 3.1f, 0.0f, 0.0f, 0u, 0u, 0u, 0u, 0u, { 51u, 19u, 255u, 255u }, { 255u, 19u, 156u, 255u }, { 10u, 242u, 173u, 255u } },
    { "aurora-borealis", "Aurora Borealis Curtains", "Aurora Curtain", ParticleEffectCategory::AtmosphericSky, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(17u), ParticleBlendCategory::AdditiveGlow, static_cast<ParticleReleaseVolume>(3u), 180u, 16.0f, 0.1f, 0.08f, 0.24f, 6.5f, 10.5f, 2.4f, 2.75f, 0.85f, 0.0f, 0u, 0u, 0u, 0u, 0u, { 7u, 255u, 87u, 255u }, { 85u, 12u, 255u, 255u }, { 3u, 175u, 255u, 255u } },
    { "spiderweb-orb", "Orb-Weave Spiderweb", "Orb-Weave Web", ParticleEffectCategory::CurlFilaments, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(3u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(2u), 320u, 0.0f, 2.0f, 0.0f, 0.0f, 9999.0f, 9999.0f, 0.015f, 0.015f, 0.6f, 0.45f, 0u, 0u, 0u, 0u, 0u, { 226u, 237u, 255u, 255u }, { 40u, 76u, 125u, 255u }, { 122u, 177u, 239u, 255u } },
    { "spiderweb-cobweb", "Messy Attic Cobwebs", "Messy Cobwebs", ParticleEffectCategory::CurlFilaments, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(3u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(3u), 300u, 0.0f, 0.1f, 0.0f, 0.0f, 9999.0f, 9999.0f, 0.016f, 0.016f, 0.45f, 0.75f, 0u, 0u, 0u, 0u, 0u, { 198u, 208u, 218u, 255u }, { 43u, 52u, 65u, 255u }, { 121u, 134u, 151u, 255u } },
    { "cables-chains", "Swinging Cables & Chains", "Chain Links", ParticleEffectCategory::ClothRigging, ParticleAuthoringDomain::StrandBotanical, static_cast<ParticleVisualSilhouette>(3u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(3u), 64u, 0.0f, 0.1f, 0.0f, 0.0f, 9999.0f, 9999.0f, 0.045f, 0.045f, 0.35f, 1.0f, 1u, 0u, 0u, 0u, 0u, { 25u, 35u, 46u, 255u }, { 164u, 183u, 202u, 255u }, { 81u, 100u, 122u, 255u } },
    { "vines-ropes", "Hanging Vines & Ropes", "Leafy Vines", ParticleEffectCategory::ClothRigging, ParticleAuthoringDomain::StrandBotanical, static_cast<ParticleVisualSilhouette>(2u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(3u), 96u, 0.0f, 0.1f, 0.0f, 0.0f, 9999.0f, 9999.0f, 0.036f, 0.032f, 0.4f, 1.0f, 2u, 0u, 0u, 0u, 0u, { 11u, 26u, 7u, 255u }, { 70u, 163u, 25u, 255u }, { 24u, 67u, 10u, 255u } },
    { "flag-banner", "Wind Banner & Tearing Cloth", "Wind Banner", ParticleEffectCategory::ClothRigging, ParticleAuthoringDomain::StrandBotanical, static_cast<ParticleVisualSilhouette>(3u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(3u), 64u, 0.0f, 0.1f, 0.0f, 0.0f, 9999.0f, 9999.0f, 0.065f, 0.065f, 0.35f, 0.4f, 3u, 0u, 0u, 0u, 0u, { 171u, 6u, 6u, 255u }, { 65u, 2u, 4u, 255u }, { 226u, 119u, 9u, 255u } },
    { "ship-sail", "Full-Rigged Ship Sail", "Ship Sail", ParticleEffectCategory::ClothRigging, ParticleAuthoringDomain::StrandBotanical, static_cast<ParticleVisualSilhouette>(3u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(3u), 64u, 0.0f, 0.1f, 0.0f, 0.0f, 9999.0f, 9999.0f, 0.075f, 0.075f, 0.35f, 0.2f, 3u, 0u, 0u, 0u, 0u, { 229u, 210u, 173u, 255u }, { 87u, 70u, 46u, 255u }, { 183u, 161u, 119u, 255u } },
    { "grass-wheat-field", "Grass & Golden Wheat Field", "Wheat & Grass", ParticleEffectCategory::BotanicalFlora, ParticleAuthoringDomain::StrandBotanical, static_cast<ParticleVisualSilhouette>(2u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(3u), 520u, 0.0f, 0.1f, 0.0f, 0.0f, 9999.0f, 9999.0f, 0.048f, 0.015f, 0.4f, 0.0f, 4u, 0u, 0u, 0u, 0u, { 5u, 25u, 4u, 255u }, { 206u, 128u, 17u, 255u }, { 40u, 88u, 9u, 255u } },
    { "tall-reeds", "Tall Lakeside Reeds", "Tall Reeds", ParticleEffectCategory::BotanicalFlora, ParticleAuthoringDomain::StrandBotanical, static_cast<ParticleVisualSilhouette>(3u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(3u), 220u, 0.0f, 0.1f, 0.0f, 0.0f, 9999.0f, 9999.0f, 0.056f, 0.018f, 0.4f, 0.0f, 5u, 0u, 0u, 0u, 0u, { 3u, 19u, 5u, 255u }, { 43u, 16u, 6u, 255u }, { 23u, 57u, 10u, 255u } },
    { "interactive-fern", "Lush Interactive Fern", "Pinnate Fern", ParticleEffectCategory::BotanicalFlora, ParticleAuthoringDomain::StrandBotanical, static_cast<ParticleVisualSilhouette>(2u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(3u), 128u, 0.0f, 0.1f, 0.0f, 0.0f, 9999.0f, 9999.0f, 0.34f, 0.06f, 0.35f, 0.0f, 6u, 0u, 0u, 0u, 0u, { 3u, 22u, 4u, 255u }, { 69u, 177u, 22u, 255u }, { 14u, 80u, 9u, 255u } },
    { "interactive-tree", "Swaying Interactive Canopy Tree", "Swaying Tree", ParticleEffectCategory::BotanicalFlora, ParticleAuthoringDomain::StrandBotanical, static_cast<ParticleVisualSilhouette>(2u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(3u), 520u, 0.0f, 0.1f, 0.0f, 0.0f, 9999.0f, 9999.0f, 0.28f, 0.24f, 0.4f, 0.55f, 7u, 0u, 0u, 0u, 0u, { 19u, 9u, 4u, 255u }, { 57u, 159u, 19u, 255u }, { 11u, 62u, 8u, 255u } },
    { "effector-grid-cubes", "Effector Cube Grid · Height Up/Down", "Height Cubes", ParticleEffectCategory::MotionLattice, ParticleAuthoringDomain::MoGraphEffector, static_cast<ParticleVisualSilhouette>(4u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(3u), 576u, 0.0f, 0.1f, 0.0f, 0.0f, 9999.0f, 9999.0f, 0.125f, 0.125f, 0.0f, 0.0f, 0u, 0u, 0u, 1u, 0u, { 3u, 3u, 5u, 255u }, { 255u, 63u, 15u, 255u }, { 255u, 4u, 9u, 255u } },
    { "effector-sphere-dots", "Halftone Effector Sphere · Circles", "Halftone Circles", ParticleEffectCategory::MotionLattice, ParticleAuthoringDomain::MoGraphEffector, static_cast<ParticleVisualSilhouette>(1u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(1u), 720u, 0.0f, 1.32f, 0.0f, 0.0f, 9999.0f, 9999.0f, 0.12f, 0.12f, 0.0f, 0.0f, 0u, 0u, 0u, 2u, 0u, { 3u, 3u, 4u, 255u }, { 255u, 30u, 48u, 255u }, { 255u, 3u, 8u, 255u } },
    { "effector-hand-halftone", "Obsidian Hand · Halftone & Tiles", "Halftone & Tiles", ParticleEffectCategory::MotionLattice, ParticleAuthoringDomain::MoGraphEffector, static_cast<ParticleVisualSilhouette>(1u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(1u), 1200u, 0.0f, 1.35f, 0.0f, 0.0f, 9999.0f, 9999.0f, 0.068f, 0.068f, 0.0f, 0.0f, 0u, 0u, 0u, 3u, 0u, { 3u, 3u, 4u, 255u }, { 255u, 17u, 40u, 255u }, { 255u, 3u, 7u, 255u } },
    { "effector-torus-hex", "Kinetic Hex Torus Grid", "Hex Grid", ParticleEffectCategory::MotionLattice, ParticleAuthoringDomain::MoGraphEffector, static_cast<ParticleVisualSilhouette>(1u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(1u), 760u, 0.0f, 1.35f, 0.0f, 0.0f, 9999.0f, 9999.0f, 0.072f, 0.072f, 0.0f, 0.0f, 0u, 0u, 0u, 4u, 0u, { 2u, 2u, 6u, 255u }, { 255u, 190u, 34u, 255u }, { 17u, 164u, 255u, 255u } },
    { "effector-object-placer", "Placeable 3D Specimen · MoGraph & Effectors", "Halftone & Tiles", ParticleEffectCategory::MotionLattice, ParticleAuthoringDomain::MoGraphEffector, static_cast<ParticleVisualSilhouette>(1u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(1u), 1400u, 0.0f, 1.55f, 0.0f, 0.0f, 9999.0f, 9999.0f, 0.068f, 0.068f, 0.0f, 0.0f, 0u, 0u, 0u, 5u, 0u, { 2u, 3u, 5u, 255u }, { 222u, 175u, 48u, 255u }, { 12u, 141u, 39u, 255u } },
    { "ferrofluid-spikes", "Magnetic Ferrofluid Dish", "Ferrofluid Dish", ParticleEffectCategory::FerrofluidFields, ParticleAuthoringDomain::FerrofluidFlux, static_cast<ParticleVisualSilhouette>(1u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(2u), 220u, 0.0f, 2.05f, 0.0f, 0.0f, 9999.0f, 9999.0f, 0.235f, 0.045f, 0.0f, 0.0f, 0u, 1u, 0u, 0u, 0u, { 0u, 0u, 1u, 255u }, { 231u, 244u, 255u, 255u }, { 7u, 14u, 32u, 255u } },
    { "ferrofluid-orb", "Levitated Ferrofluid Crown", "Levitated Ferrofluid", ParticleEffectCategory::FerrofluidFields, ParticleAuthoringDomain::FerrofluidFlux, static_cast<ParticleVisualSilhouette>(1u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(1u), 220u, 0.0f, 1.22f, 0.0f, 0.0f, 9999.0f, 9999.0f, 0.205f, 0.038f, 0.0f, 0.0f, 0u, 2u, 0u, 0u, 0u, { 0u, 0u, 1u, 255u }, { 206u, 237u, 255u, 255u }, { 6u, 15u, 35u, 255u } },
    { "cracked-ice-sheet", "Movie Cracked Glacial Ice", "Cracked Ice", ParticleEffectCategory::FractureSeismic, ParticleAuthoringDomain::FractureSeismic, static_cast<ParticleVisualSilhouette>(18u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(3u), 640u, 0.0f, 0.1f, 1.4f, 3.8f, 0.65f, 1.65f, 0.155f, 0.09f, 0.85f, 0.95f, 0u, 0u, 1u, 0u, 0u, { 100u, 218u, 255u, 255u }, { 255u, 255u, 255u, 255u }, { 188u, 239u, 255u, 255u } },
    { "ground-crack-quake", "Seismic Ground Crack Fissure", "Ground Fracture", ParticleEffectCategory::FractureSeismic, ParticleAuthoringDomain::FractureSeismic, static_cast<ParticleVisualSilhouette>(18u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(3u), 640u, 0.0f, 0.1f, 1.8f, 4.5f, 0.7f, 1.65f, 0.165f, 0.09f, 0.75f, 1.05f, 0u, 0u, 2u, 0u, 0u, { 11u, 9u, 8u, 255u }, { 255u, 163u, 34u, 255u }, { 255u, 26u, 3u, 255u } },
    { "plasma-cage-arcs", "Plasma Cage & Arc Collector", "Plasma Cage", ParticleEffectCategory::CurlFilaments, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(3u), ParticleBlendCategory::AdditiveGlow, static_cast<ParticleReleaseVolume>(1u), 640u, 45.0f, 1.75f, 1.4f, 4.8f, 0.14f, 0.36f, 0.095f, 0.014f, 3.0f, 0.1f, 0u, 0u, 0u, 0u, 0u, { 43u, 194u, 255u, 255u }, { 22u, 41u, 255u, 255u }, { 255u, 255u, 255u, 255u } },
    { "lightning-web-mesh", "Dynamic Lightning Web & Storm Grid", "Lightning Web", ParticleEffectCategory::CurlFilaments, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(3u), ParticleBlendCategory::AdditiveGlow, static_cast<ParticleReleaseVolume>(3u), 680u, 52.0f, 2.05f, 1.6f, 5.4f, 0.12f, 0.34f, 0.09f, 0.012f, 2.8f, 0.06f, 0u, 0u, 0u, 0u, 0u, { 81u, 206u, 255u, 255u }, { 65u, 34u, 255u, 255u }, { 206u, 237u, 255u, 255u } },
    { "kelp-forest", "Underwater Giant Kelp Forest", "Kelp Forest", ParticleEffectCategory::BotanicalFlora, ParticleAuthoringDomain::StrandBotanical, static_cast<ParticleVisualSilhouette>(1u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(3u), 240u, 0.0f, 0.1f, 0.0f, 0.0f, 9999.0f, 9999.0f, 0.16f, 0.04f, 0.0f, 0.0f, 8u, 0u, 0u, 0u, 0u, { 3u, 14u, 3u, 255u }, { 190u, 97u, 8u, 255u }, { 19u, 65u, 7u, 255u } },
    { "sea-anemone", "Bioluminescent Sea Anemone Colony", "Sea Anemone", ParticleEffectCategory::BotanicalFlora, ParticleAuthoringDomain::StrandBotanical, static_cast<ParticleVisualSilhouette>(1u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(2u), 220u, 0.0f, 1.55f, 0.0f, 0.0f, 9999.0f, 9999.0f, 0.072f, 0.015f, 0.0f, 0.0f, 9u, 0u, 0u, 0u, 0u, { 23u, 3u, 37u, 255u }, { 8u, 255u, 202u, 255u }, { 255u, 12u, 65u, 255u } },
    { "beaded-door-curtain", "Crystal Bead Door Curtain", "Bead Curtain", ParticleEffectCategory::ClothRigging, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(1u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(3u), 280u, 0.0f, 0.1f, 0.0f, 0.0f, 9999.0f, 9999.0f, 0.072f, 0.072f, 0.0f, 0.9f, 0u, 0u, 0u, 0u, 0u, { 10u, 4u, 30u, 255u }, { 255u, 63u, 171u, 255u }, { 27u, 192u, 255u, 255u } },
    { "bamboo-wind-chimes", "Bamboo & Brass Wind Chimes", "Bamboo Chimes", ParticleEffectCategory::ClothRigging, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(1u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(2u), 120u, 0.0f, 0.56f, 0.0f, 0.0f, 9999.0f, 9999.0f, 0.088f, 0.088f, 0.0f, 1.0f, 0u, 0u, 0u, 0u, 0u, { 55u, 22u, 3u, 255u }, { 255u, 233u, 130u, 255u }, { 188u, 101u, 17u, 255u } },
    { "velvet-theater-drapes", "Velvet Theatre Drape Strips", "Doorway Drapes", ParticleEffectCategory::ClothRigging, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(1u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(3u), 180u, 0.0f, 0.1f, 0.0f, 0.0f, 9999.0f, 9999.0f, 0.115f, 0.115f, 0.0f, 1.0f, 0u, 0u, 0u, 0u, 0u, { 19u, 1u, 3u, 255u }, { 255u, 170u, 50u, 255u }, { 103u, 2u, 13u, 255u } },
    { "dandelion-puff", "Dandelion Puff & Wish Seeds", "Dandelion Seeds", ParticleEffectCategory::BotanicalFlora, ParticleAuthoringDomain::GeneralParticle, static_cast<ParticleVisualSilhouette>(2u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(1u), 460u, 0.0f, 0.54f, 0.6f, 2.2f, 12.0f, 18.0f, 0.088f, 0.085f, 1.35f, 0.18f, 0u, 0u, 0u, 0u, 0u, { 8u, 37u, 4u, 255u }, { 255u, 255u, 255u, 255u }, { 218u, 237u, 196u, 255u } },
    { "starling-murmuration", "Starling Murmuration & Predator Scatter", "Murmuration Flock", ParticleEffectCategory::WindBlown, ParticleAuthoringDomain::BoidsFlock, static_cast<ParticleVisualSilhouette>(13u), ParticleBlendCategory::PremultipliedAlpha, static_cast<ParticleReleaseVolume>(1u), 520u, 95.0f, 2.35f, 3.4f, 5.4f, 16.0f, 24.0f, 0.21f, 0.2f, 0.04f, 0.0f, 0u, 0u, 0u, 0u, 0u, { 3u, 4u, 9u, 255u }, { 255u, 103u, 40u, 255u }, { 24u, 40u, 78u, 255u } },
}};

//------------------------------------------------------------------------------------------------------------------------
//                                                  STATIC LABEL ARRAYS
//------------------------------------------------------------------------------------------------------------------------

constexpr std::array<const char*, static_cast<std::size_t>(ParticleVisualSilhouette::Count)> SilhouetteLabels = {{
    "Streak",
    "Sphere",
    "Leaf / Paper",
    "Soft Glow",
    "Cube Shard",
    "Coin Disc",
    "Bubble Dome",
    "Snowflake",
    "Hailstone",
    "Bokeh Bead",
    "Shockwave Ring",
    "EMP Pulse Dome",
    "Hologram Scanline",
    "Winged Boid",
    "Viscous Droplet",
    "Radar / Sonar Sweep",
    "Energy Shield Dome",
    "Aurora Curtain",
    "3D Glass Shard",
    "Volumetric Water Mist",
}};

constexpr std::array<const char*, static_cast<std::size_t>(ParticleReleaseVolume::Count)> ReleaseVolumeLabels = {{
    "Point Origin",
    "Sphere Volume",
    "Disc Plane",
    "Box Volume",
    "Surface Sample",
}};

constexpr std::array<const char*, static_cast<std::size_t>(ParticleColorDriver::Count)> ColorDriverLabels = {{
    "Preset Default",
    "Lifetime Ratio",
    "Elapsed Age",
    "Speed Magnitude",
    "Vertical Speed",
    "Azimuth Angle",
    "Heading Angle",
    "Origin Distance",
    "Altitude Height",
    "3D Spatial Noise",
    "Per-Particle Hash",
    "Spin Orientation",
    "Camera Distance",
    "Particle Scale",
    "Life Duration",
}};

constexpr std::array<const char*, 3u> RampWrapLabels = {{
    "Clamp",
    "Repeat",
    "Mirror",
}};

constexpr std::array<const char*, 3u> RampInterpLabels = {{
    "Linear",
    "Smooth",
    "Stepped",
}};

constexpr std::array<const char*, static_cast<std::size_t>(FireworkPatternCategory::Count)> FireworkPatternLabels = {{
    "Classic Peony",
    "Sky Text",
    "3D Heart",
    "Saturn Rings",
    "Five-Point Star",
    "Smiley Burst",
    "Royal Crown",
    "Butterfly Wings",
    "Double-Ring Willow",
    "Grand Finale",
}};

constexpr std::array<const char*, 10u> RiggingArchitectureLabels = {{
    "Off",
    "Cables & Chains",
    "Hanging Vines & Ivy",
    "Cloth Banner / Sail",
    "Grass & Wheat",
    "Reeds & Cattails",
    "Broad Ferns",
    "Canopy Tree",
    "Giant Kelp Forest",
    "Sea Anemone Tentacles",
}};

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                               REGISTRY QUERY FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace ParticlePresetRegistry
{

std::span<const ParticleCategoryDescriptor> QueryActiveCategories() noexcept
{
    return ActiveCategories;
}

const ParticleCategoryDescriptor& QueryCategoryDescriptor(ParticleEffectCategory Category) noexcept
{
    const auto Ordinal = static_cast<std::size_t>(Category);
    return ActiveCategories[Ordinal < ActiveCategories.size() ? Ordinal : 0u];
}

std::span<const ParticlePresetDescriptor> QueryActivePresets() noexcept
{
    return ActivePresets;
}

const ParticlePresetDescriptor* FindPreset(std::string_view Identifier) noexcept
{
    for (const ParticlePresetDescriptor& Descriptor : ActivePresets)
    {
        if (Identifier == Descriptor.Identifier)
        {
            return &Descriptor;
        }
    }
    return nullptr;
}

uint32_t CountCategoryPresets(ParticleEffectCategory Category) noexcept
{
    uint32_t Total = 0u;
    for (const ParticlePresetDescriptor& Descriptor : ActivePresets)
    {
        if (Descriptor.Category == Category)
        {
            ++Total;
        }
    }
    return Total;
}

bool VerifySlugRetired(std::string_view RetiredSlug) noexcept
{
    return FindPreset(RetiredSlug) == nullptr;
}

const char* QuerySilhouetteLabel(ParticleVisualSilhouette Silhouette) noexcept
{
    const auto Ordinal = static_cast<std::size_t>(Silhouette);
    return SilhouetteLabels[Ordinal < SilhouetteLabels.size() ? Ordinal : 0u];
}

const char* QueryReleaseVolumeLabel(ParticleReleaseVolume Volume) noexcept
{
    const auto Ordinal = static_cast<std::size_t>(Volume);
    return ReleaseVolumeLabels[Ordinal < ReleaseVolumeLabels.size() ? Ordinal : 0u];
}

const char* QueryColorDriverLabel(ParticleColorDriver Driver) noexcept
{
    const auto Ordinal = static_cast<std::size_t>(Driver);
    return ColorDriverLabels[Ordinal < ColorDriverLabels.size() ? Ordinal : 0u];
}

const char* QueryRampWrapLabel(ParticleRampWrapCategory Wrap) noexcept
{
    const auto Ordinal = static_cast<std::size_t>(Wrap);
    return RampWrapLabels[Ordinal < RampWrapLabels.size() ? Ordinal : 0u];
}

const char* QueryRampInterpLabel(ParticleRampInterpCategory Interp) noexcept
{
    const auto Ordinal = static_cast<std::size_t>(Interp);
    return RampInterpLabels[Ordinal < RampInterpLabels.size() ? Ordinal : 0u];
}

const char* QueryFireworkPatternLabel(FireworkPatternCategory Pattern) noexcept
{
    const auto Ordinal = static_cast<std::size_t>(Pattern);
    return FireworkPatternLabels[Ordinal < FireworkPatternLabels.size() ? Ordinal : 0u];
}

const char* QueryRiggingArchitectureLabel(uint8_t Architecture) noexcept
{
    return RiggingArchitectureLabels[Architecture < RiggingArchitectureLabels.size() ? Architecture : 0u];
}

} // namespace ParticlePresetRegistry

} // namespace Frontier::Workspaces
