/*==========================================================================================================================================
                                                         ENVIRONMENTEDITACCESS.H
==========================================================================================================================================*/
// 🧩 Non-owning environment edit references, refreshed by the workspace after scene mutations and before inspection.

#pragma once

#include "Engine/Scene/EngineConfigurations.h"
#include "Engine/Scene/EnvironmentDiagnostics.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    FORCE FIELD ACCESS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One localized force under the Force Field, as the Force Field page lists it (its record's token selects it).
struct ForceSourceAccess
{
    uint64_t                  Token = 0;
    std::string               Name;
    ForceSourceConfiguration* Configuration = nullptr;
};

// 📝 What the Field view measured, published by the workspace while it shows (the Force Field page's hero reads it): the
//    peak of the shown quantity across the view, the span and cell, and the last 120 peaks at about 10 per second.
struct ForceFieldReadings
{
    bool                    Measured     = false;
    bool                    FieldShown   = false;
    double                  Peak         = 0.0;
    double                  Span         = 0.0;   // [m]
    double                  Cell         = 0.0;   // [m]
    uint32_t                Samples      = 0;
    double                  Milliseconds = 0.0;
    const char*             Unit         = "m/s";
    std::array<float, 120>  History{};
    uint32_t                HistoryCount  = 0;
    uint32_t                HistoryCursor = 0;    // [-] where the next peak goes
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENVIRONMENT EDIT ACCESS
//------------------------------------------------------------------------------------------------------------------------

struct EnvironmentEditAccess
{
    AtmosphereConfiguration* Atmosphere = nullptr;
    ForceFieldConfiguration* Forces = nullptr;
    std::shared_ptr<const EnvironmentRenderReadout> Rendering;
    SolarConfiguration                  *Solar      = nullptr;
    CloudConfiguration                  *Clouds     = nullptr;
    CloudConfiguration                  *LocalCloud = nullptr;
    FogConfiguration                    *Fog        = nullptr;
    FogConfiguration                    *LocalFog   = nullptr;
    WindConfiguration                   *Wind       = nullptr;
    LightningConfiguration              *Lightning  = nullptr;
    std::array<PlanetConfiguration *, 4> Planets{};
    bool                                 CelestialReady = false;
    std::array<double, 3>                LunarPlacement{}; // [° azimuth, ° elevation, days]
    std::array<std::array<double, 2>, 4> PlanetPlacements{};
    bool                                 CameraReady   = false;
    std::array<double, 3>                CameraEye     = {0.0, 0.0, 2.0};
    std::array<double, 3>                CameraForward = {1.0, 0.0, 0.0};
    std::array<uint32_t, 2>              ViewExtent    = {0, 0};
    std::vector<ForceSourceAccess>       ForceSources;
    ForceFieldReadings                   FieldReadings{};
};

} // namespace Frontier
