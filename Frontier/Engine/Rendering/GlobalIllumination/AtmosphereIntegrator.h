/*==========================================================================================================================================
                                                         ATMOSPHEREINTEGRATOR.H
==========================================================================================================================================*/
// 🧩 Light direction and sun transmittance at the observer, moonlight below the horizon, and the sky-view LUT refresh.

#pragma once

#include "ShaderInterchange.h"
#include "TransformProjection.h"

#include <optional>
#include <string>

namespace Frontier::GlobalIllumination
{

// 📝 The EnvironmentSettings keys AtmosphereIntegrator.js reads, with the demo's EnvironmentDefaults.
struct AtmosphereSettings
{
    bool    AirMass            = true;
    double  SunTemperature     = 5778.0;   // [K]
    bool    SunColourIsRgb     = false;
    Vector3 SunTint{1.0, 1.0, 1.0};
    bool    SunEnabled         = true;
    double  MoonElevation      = 38.0;     // [°]
    double  MoonAzimuth        = 0.0;      // [°]
    bool    MoonLightEnabled   = true;
    double  MoonLightIntensity = 1.0;
    bool    MoonEnabled        = true;
    double  MoonBrightness     = 1.0;
    double  MoonPhase          = 14.765;   // [day]
    double  Rayleigh           = 1.0;
    double  Mie                = 1.0;
    double  Ozone              = 1.0;
    double  MieG               = 0.8;
    double  RayleighHeight     = 8000.0;   // [m]
    double  MieHeight          = 1200.0;   // [m]
    Vector3 GroundAlbedo{0.3, 0.3, 0.3};
    Vector3 SkyTint{1.0, 1.0, 1.0};
    double  HorizonGlow        = 1.0;
    bool    MultipleScattering = true;
    bool    AtmosphereEnabled  = true;
    double  TierSamples        = 40.0;
    bool    UseBakedAtmosphere = true;
};

struct LightSolution
{
    Vector3     Direction{0.0, 0.0, 1.0};
    Vector3     Illuminance{1.0, 1.0, 1.0};
    Vector3     SolarIlluminance{0.0, 0.0, 0.0};
    Vector3     SunDirection{0.0, 0.0, 1.0};
    Vector3     NightSky{0.0, 0.0, 0.0};
    double      SunVisible = 1.0;
    std::string Source     = "Sun";
};

[[nodiscard]] Vector3 ResolveSunTransmittance(const Vector3& Direction, const AtmosphereSettings& Settings);

class AtmosphereIntegrator
{
  public:
    AtmosphereIntegrator(DeviceExchange& Exchange, ShaderInterchange& Shaders);

    void       ConstructResources();
    LightSolution ResolveLight(double ElevationDegrees, double AzimuthDegrees, double SolarIlluminance, const AtmosphereSettings& Settings);
    bool       RecordSkyView(CommandSequence& Sequence, double SolarIlluminance);

    LightSolution                         Light{};
    AtmosphereSettings                 Settings{};
    Vector3                            SolarTint{1.0, 1.0, 1.0};
    std::shared_ptr<DeviceTexture>     SkyTexture{}, TransmittanceTexture{}, MultipleTexture{}, SkyTransmissionTexture{};
    std::shared_ptr<DeviceTextureView> SkyView{}, TransmittanceView{}, MultipleView{}, SkyTransmissionView{};
    std::shared_ptr<DeviceSampler>     SkySampler{}, LutSampler{};
    std::shared_ptr<DeviceRegion>      UniformRegion{};

  private:
    DeviceExchange&                   Exchange_;
    ShaderInterchange&                Shaders_;
    std::shared_ptr<DescriptorLayout> TransmittanceLayout_{}, MultipleLayout_{}, Layout_{};
    std::shared_ptr<DescriptorGroup>  TransmittanceGroup_{}, MultipleGroup_{}, Group_{};
    std::shared_ptr<DeviceProgram>    TransmittanceProgram_{}, MultipleProgram_{}, Program_{};
    std::string                       LastKey_{};
    std::string                       LastMediumKey_{};
    std::string                       MoonTransmissionKey_{};
    Vector3                           MoonTransmission_{};
};

} // namespace Frontier::GlobalIllumination
