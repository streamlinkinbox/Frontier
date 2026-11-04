/*==========================================================================================================================================
                                                          ENVIRONMENTDIAGNOSTICS.H
==========================================================================================================================================*/
// 🧩 Owned scene-renderer readbacks for baked atmosphere inputs, real LUT pixels, local-medium quality and evaluated flow vectors.

#pragma once

#include "Engine/Scene/EngineConfigurations.h"
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    BAKED ATMOSPHERE
//------------------------------------------------------------------------------------------------------------------------

struct AtmosphereBakeReadout
{
    bool                           Ready         = false;
    uint64_t                       SourceRecord  = 0;
    uint64_t                       RequestSerial = 0;
    uint64_t                       Generation    = 0;
    AtmosphereOpticalConfiguration Inputs{};
    float ObserverAltitudeKm = .05f;
    std::array<float, 3>           SolarDirection{};
    std::vector<float>             Transmittance;
    std::vector<float>             MultipleScattering;
    std::vector<float>             SkyView;
    std::string                    Diagnostic;
};

inline bool ConfirmAtmosphereBake(AtmosphereConfiguration &Parameters, uint64_t Token, const AtmosphereBakeReadout &Bake)
{
    if (!Bake.Ready || Bake.SourceRecord != Token || Parameters.BakeRequested == 0 || Bake.RequestSerial != Parameters.BakeRequested ||
        Parameters.BakeAcknowledged == Parameters.BakeRequested)
        return false;
    Parameters.Baked            = Bake.Inputs;
    Parameters.BakedAvailable   = true;
    Parameters.BakeAcknowledged = Bake.RequestSerial;
    Parameters.AdvanceRevision();
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ATMOSPHERIC FLOW
//------------------------------------------------------------------------------------------------------------------------

struct FieldVectorSample
{
    std::array<double, 3> Position{}; // [m] world Z up
    std::array<double, 3> Velocity{}; // [m/s] world Z up, never force in newtons
};

struct ReceiverMotionReadout
{
    uint64_t              Token = 0;
    std::array<double, 3> DisplacementMetres{};
};

struct EnvironmentRenderReadout
{
    std::vector<ReceiverMotionReadout>           ReceiverMotion;
    bool                                         Ready           = false;
    uint64_t                                     Refresh         = 0;
    uint32_t                                     LocalClouds     = 0;
    uint32_t                                     LocalFogVolumes = 0;
    std::array<uint32_t, 2>                      CloudTraceExtent{};
    uint32_t                                     FogMarchSamples = 0;
    std::array<double, 3>                        WindAtCamera{};
    std::vector<FieldVectorSample>               Vectors;
    std::vector<std::string>                     Contributions;
    std::shared_ptr<const AtmosphereBakeReadout> AtmosphereBake;
};

} // namespace Frontier
