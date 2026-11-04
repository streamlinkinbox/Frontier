/*==========================================================================================================================================
                                                        ATMOSPHEREINTEGRATOR.CPP
==========================================================================================================================================*/
// 🧩 Light direction and sun transmittance at the observer, moonlight below the horizon, and the sky-view LUT refresh.

#include "AtmosphereIntegrator.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace Frontier::GlobalIllumination
{

namespace
{
constexpr uint32_t SkyViewWidth     = 192;   // [texel] azimuth
constexpr uint32_t SkyViewHeight    = 108;   // [texel] elevation
constexpr double   GroundRadius     = 6360000.0;
constexpr double   AtmosphereRadius = 6460000.0;
constexpr double   RayleighScatter[3] = {5.802e-6, 13.558e-6, 33.1e-6};
constexpr double   MieExtinction    = 4.40e-6;
constexpr double   OzoneAbsorption[3] = {0.650e-6, 1.881e-6, 0.085e-6};
constexpr double   ObserverAltitude = 100.0;   // [m]
constexpr double   Pi               = 3.141592653589793;

// 📝 The cache keys only need to change exactly when the demo's JSON keys change; %.17g round-trips every double.
std::string KeyOf(std::initializer_list<double> Values)
{
    std::string Key;
    char        Text[40];
    for (double Value : Values)
    {
        std::snprintf(Text, sizeof(Text), "%.17g,", Value);
        Key += Text;
    }
    return Key;
}

std::string FixedFour(double Value)
{
    char Text[40];
    std::snprintf(Text, sizeof(Text), "%.4f", Value);
    return Text;
}
} // namespace

// 📝 Transmittance from the observer toward the sun, 64-step quadrature of the extinction along the ray.
Vector3 ResolveSunTransmittance(const Vector3& Direction, const AtmosphereSettings& S)
{
    const Vector3 Origin = {0.0, 0.0, GroundRadius + ObserverAltitude};
    const double  Dot    = Origin[2] * Direction[2];
    const double  Square = Origin[2] * Origin[2];
    const double  GroundDiscriminant = Dot * Dot - (Square - GroundRadius * GroundRadius);
    if (GroundDiscriminant > 0.0 && (-Dot - std::sqrt(GroundDiscriminant)) > 0.0) return {0.0, 0.0, 0.0};
    const double Exit    = -Dot + std::sqrt(Dot * Dot - (Square - AtmosphereRadius * AtmosphereRadius));
    const int    Samples = 64;
    const double Step    = Exit / Samples;
    Vector3      Depth{0.0, 0.0, 0.0};
    for (int Sample = 0; Sample < Samples; ++Sample)
    {
        const double  Travel   = (Sample + 0.5) * Step;
        const Vector3 Point    = {Direction[0] * Travel, Direction[1] * Travel, Origin[2] + Direction[2] * Travel};
        const double  Altitude = ComputeHypot(Point[0], Point[1], Point[2]) - GroundRadius;
        const double  Rayleigh = std::exp(-Altitude / S.RayleighHeight);
        const double  Mie      = std::exp(-Altitude / S.MieHeight);
        const double  Ozone    = std::max(0.0, 1.0 - std::fabs(Altitude - 25000.0) / 15000.0);
        for (int Channel = 0; Channel < 3; ++Channel)
        {
            Depth[Channel] += (RayleighScatter[Channel] * Rayleigh * S.Rayleigh + MieExtinction * Mie * S.Mie + OzoneAbsorption[Channel] * Ozone * S.Ozone) * Step;
        }
    }
    return {std::exp(-Depth[0]), std::exp(-Depth[1]), std::exp(-Depth[2])};
}

AtmosphereIntegrator::AtmosphereIntegrator(DeviceExchange& Exchange, ShaderInterchange& Shaders) : Exchange_(Exchange), Shaders_(Shaders)
{
}

void AtmosphereIntegrator::ConstructResources()
{
    auto Construct = [this](const std::string& Label, uint32_t Width, uint32_t Height) {
        TextureSpecification Specification;
        Specification.Label  = Label;
        Specification.Width  = Width;
        Specification.Height = Height;
        Specification.Format = TextureFormat::Rgba16Float;
        Specification.Usage  = TextureUsage::Storage | TextureUsage::Sampled | TextureUsage::CopySource;
        return Exchange_.ConstructTexture(Specification);
    };
    SkyTexture             = Construct("Sky view", SkyViewWidth, SkyViewHeight);
    TransmittanceTexture   = Construct("Atmosphere transmittance 256×64", 256, 64);
    MultipleTexture        = Construct("Atmosphere multiple scattering 32×32", 32, 32);
    SkyTransmissionTexture = Construct("Sky-view transmission", SkyViewWidth, SkyViewHeight);
    SkyView             = Exchange_.ConstructView(SkyTexture);
    TransmittanceView   = Exchange_.ConstructView(TransmittanceTexture);
    MultipleView        = Exchange_.ConstructView(MultipleTexture);
    SkyTransmissionView = Exchange_.ConstructView(SkyTransmissionTexture);
    SamplerSpecification Sky;
    Sky.MagFilter = Sky.MinFilter = FilterMode::Linear;
    Sky.AddressU  = AddressMode::Repeat;
    SkySampler = Exchange_.ConstructSampler(Sky);
    SamplerSpecification Lut;
    Lut.MagFilter = Lut.MinFilter = FilterMode::Linear;
    LutSampler    = Exchange_.ConstructSampler(Lut);
    UniformRegion = Exchange_.ConstructRegion(96, "Atmosphere constants");
    using D = DescriptorCode;
    const std::vector<DescriptorSlot> TransmittanceSlots = {{D::Uniform}, {D::StorageTexture2D, TextureFormat::Rgba16Float}};
    const std::vector<DescriptorSlot> MultipleSlots = {{D::Uniform}, {D::StorageTexture2D, TextureFormat::Rgba16Float}, {D::Texture2D}, {D::Sampler}};
    const std::vector<DescriptorSlot> SkySlots = {{D::Uniform}, {D::StorageTexture2D, TextureFormat::Rgba16Float}, {D::Texture2D}, {D::Texture2D},
                                                  {D::Sampler}, {D::StorageTexture2D, TextureFormat::Rgba16Float}};
    TransmittanceLayout_ = Exchange_.ConstructDescriptorLayout(TransmittanceSlots, "Transmittance LUT");
    MultipleLayout_      = Exchange_.ConstructDescriptorLayout(MultipleSlots, "Multiple scattering LUT");
    Layout_              = Exchange_.ConstructDescriptorLayout(SkySlots, "Sky view LUT");
    TransmittanceGroup_  = Exchange_.ConstructDescriptorGroup(TransmittanceLayout_, {UniformRegion, TransmittanceView});
    MultipleGroup_       = Exchange_.ConstructDescriptorGroup(MultipleLayout_, {UniformRegion, MultipleView, TransmittanceView, LutSampler});
    Group_ = Exchange_.ConstructDescriptorGroup(Layout_, {UniformRegion, SkyView, TransmittanceView, MultipleView, LutSampler, SkyTransmissionView});
    TransmittanceProgram_ = Shaders_.ConstructComputeProgram("AtmosphereTransmittance", "IntegrateTransmittance", {TransmittanceLayout_});
    MultipleProgram_      = Shaders_.ConstructComputeProgram("AtmosphereMultiple", "IntegrateMultiple", {MultipleLayout_});
    Program_              = Shaders_.ConstructComputeProgram("AtmosphereScattering", "IntegrateSkyView", {Layout_});
}

// 📝 Below about −4° the key light hands over to a cool, dim moon; a faint night-sky floor keeps escapes from pure black.
LightSolution AtmosphereIntegrator::ResolveLight(double ElevationDegrees, double AzimuthDegrees, double SolarIlluminance, const AtmosphereSettings& S)
{
    Settings = S;
    const double  Elevation = ElevationDegrees * Pi / 180.0;
    const double  Azimuth   = AzimuthDegrees * Pi / 180.0;
    const Vector3 Sun = NormalizeVector({std::cos(Elevation) * std::cos(Azimuth), std::cos(Elevation) * std::sin(Azimuth), std::sin(Elevation)});
    const Vector3 Transmittance = !S.AirMass ? Vector3{1.0, 1.0, 1.0} : ResolveSunTransmittance(Sun, S);
    const double  Temperature   = std::max(20.0, std::min(100.0, S.SunTemperature / 100.0));
    Vector3 TemperatureRgb;
    if (S.SunColourIsRgb)
    {
        TemperatureRgb = S.SunTint;
    }
    else
    {
        const double Channels[3] = {
            Temperature <= 66 ? 255.0 : 329.698727 * std::pow(Temperature - 60, -0.13320476),
            Temperature <= 66 ? 99.470802 * std::log(Temperature) - 161.119568 : 288.12217 * std::pow(Temperature - 60, -0.07551485),
            Temperature >= 66 ? 255.0 : (Temperature <= 19 ? 0.0 : 138.517731 * std::log(Temperature - 10) - 305.044793)};
        for (int Channel = 0; Channel < 3; ++Channel)
        {
            TemperatureRgb[Channel] = std::pow(std::max(0.0, std::min(255.0, Channels[Channel])) / 255.0, 2.2);
        }
    }
    SolarTint = TemperatureRgb;
    const double SunFade  = (S.SunEnabled ? 1.0 : 0.0) * ClampScalar((ElevationDegrees + 1.0) / 3.0, 0.0, 1.0);
    const double MoonFade = !S.SunEnabled ? 1.0 : ClampScalar((-ElevationDegrees - 4.0) / 6.0, 0.0, 1.0);
    const double MoonElevation = S.MoonElevation * Pi / 180.0;
    const double MoonAzimuth   = S.MoonAzimuth * Pi / 180.0;
    const Vector3 Moon = NormalizeVector({std::cos(MoonElevation) * std::cos(MoonAzimuth), std::cos(MoonElevation) * std::sin(MoonAzimuth), std::sin(MoonElevation)});
    const std::string MoonKey = KeyOf({Moon[0], Moon[1], Moon[2], S.Rayleigh, S.Mie, S.Ozone, S.RayleighHeight, S.MieHeight});
    if (MoonTransmissionKey_ != MoonKey)
    {
        MoonTransmissionKey_ = MoonKey;
        MoonTransmission_    = ResolveSunTransmittance(Moon, S);
    }
    const double  MoonHorizon = ClampScalar((S.MoonElevation + 0.5) / 2.0, 0.0, 1.0);
    const double  MoonScale   = S.MoonLightEnabled ? S.MoonLightIntensity : 0.0;
    const Vector3 MoonIlluminance = {0.08 * MoonScale, 0.10 * MoonScale, 0.14 * MoonScale};
    if (MoonFade > SunFade)
    {
        Light.Source    = "Moon";
        Light.Direction = Moon;
        for (int Channel = 0; Channel < 3; ++Channel)
        {
            Light.Illuminance[Channel] = MoonIlluminance[Channel] * MoonTransmission_[Channel] * MoonHorizon * MoonFade * (S.MoonEnabled ? S.MoonBrightness : 0.0) *
                                         (0.5 - 0.5 * std::cos(S.MoonPhase / 29.53 * 2.0 * Pi));
        }
    }
    else
    {
        Light.Source    = "Sun";
        Light.Direction = Sun;
        for (int Channel = 0; Channel < 3; ++Channel)
        {
            Light.Illuminance[Channel] = Transmittance[Channel] * TemperatureRgb[Channel] * SolarIlluminance * SunFade;
        }
    }
    for (int Channel = 0; Channel < 3; ++Channel)
    {
        Light.SolarIlluminance[Channel] = Transmittance[Channel] * TemperatureRgb[Channel] * SolarIlluminance * SunFade;
    }
    Light.SunDirection = Sun;
    Light.SunVisible   = SunFade;
    const double Night[3] = {0.0012, 0.0016, 0.0030};
    for (int Channel = 0; Channel < 3; ++Channel) Light.NightSky[Channel] = Night[Channel] * (1.0 - SunFade) * SolarIlluminance / 10.0;
    return Light;
}

bool AtmosphereIntegrator::RecordSkyView(CommandSequence& Sequence, double SolarIlluminance)
{
    const Vector3& Sun = Light.SunDirection;
    const AtmosphereSettings& S = Settings;
    const double Medium[6] = {S.Rayleigh, S.Mie, S.Ozone, S.MieG, S.RayleighHeight, S.MieHeight};
    const std::string MediumKey = KeyOf({Medium[0], Medium[1], Medium[2], Medium[3], Medium[4], Medium[5], S.GroundAlbedo[0], S.GroundAlbedo[1], S.GroundAlbedo[2]});
    const std::string Key = MediumKey + KeyOf({SolarTint[0], SolarTint[1], SolarTint[2], S.MultipleScattering ? 1.0 : 0.0, S.SkyTint[0], S.SkyTint[1],
                                               S.SkyTint[2], S.HorizonGlow, S.AtmosphereEnabled ? 1.0 : 0.0, S.TierSamples}) +
                            FixedFour(Sun[0]) + "," + FixedFour(Sun[1]) + "," + FixedFour(Sun[2]) + "|" + FixedFour(SolarIlluminance);
    if (Key == LastKey_ && S.UseBakedAtmosphere) return false;
    LastKey_ = Key;
    float Constants[24] = {};
    Constants[0]  = static_cast<float>(Sun[0]);
    Constants[1]  = static_cast<float>(Sun[1]);
    Constants[2]  = static_cast<float>(Sun[2]);
    Constants[3]  = static_cast<float>(SolarIlluminance);
    Constants[4]  = static_cast<float>(ObserverAltitude);
    Constants[5]  = static_cast<float>(S.HorizonGlow);
    for (int Index = 0; Index < 4; ++Index) Constants[8 + Index] = static_cast<float>(Medium[Index]);
    Constants[12] = static_cast<float>(Medium[4]);
    Constants[13] = static_cast<float>(Medium[5]);
    for (int Channel = 0; Channel < 3; ++Channel) Constants[16 + Channel] = static_cast<float>(SolarTint[Channel] * S.SkyTint[Channel]);
    Constants[14] = S.MultipleScattering ? 1.0f : 0.0f;
    Constants[15] = S.AtmosphereEnabled ? 1.0f : 0.0f;
    for (int Channel = 0; Channel < 3; ++Channel) Constants[20 + Channel] = static_cast<float>(S.GroundAlbedo[Channel]);
    Constants[23] = static_cast<float>(S.TierSamples);
    Exchange_.WriteRegion(UniformRegion, 0, Constants, sizeof(Constants));
    if (MediumKey != LastMediumKey_)
    {
        Sequence.BindProgram(TransmittanceProgram_);
        Sequence.BindGroup(0, TransmittanceGroup_);
        Sequence.Dispatch((256 + 7) / 8, (64 + 7) / 8);
        Sequence.BindProgram(MultipleProgram_);
        Sequence.BindGroup(0, MultipleGroup_);
        Sequence.Dispatch((32 + 7) / 8, (32 + 7) / 8);
        LastMediumKey_ = MediumKey;
    }
    Sequence.BindProgram(Program_);
    Sequence.BindGroup(0, Group_);
    Sequence.Dispatch((SkyViewWidth + 7) / 8, (SkyViewHeight + 7) / 8);
    return true;
}

} // namespace Frontier::GlobalIllumination
