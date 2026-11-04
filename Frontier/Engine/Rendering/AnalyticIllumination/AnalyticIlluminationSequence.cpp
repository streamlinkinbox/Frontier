/*==========================================================================================================================================
                                                   ANALYTICILLUMINATIONSEQUENCE.CPP
==========================================================================================================================================*/
// 🧩 Bring-up and refresh of the analytic illumination: shared bindings, the refresh profile (camera, sun, sky gradient, the sun's
//    orthographic box around the camera) and the two raster sequences in one submission.
#include "AnalyticIlluminationSequence.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
namespace Frontier::AnalyticIllumination
{
using namespace Frontier::GlobalIllumination;
namespace
{
constexpr uint32_t SolarDepthExtent  = 2048;    // [texel]
constexpr double   SolarHalfExtent   = 90.0;    // [m] half side of the sun's orthographic box
constexpr double   SolarReach        = 400.0;   // [m] depth range of that box
constexpr double   DegreesToRadian   = 3.14159265358979323846 / 180.0;

// 📝 Linear Rec.709 colour of a black body (Tanner Helland's fit, normalised to its brightest channel).
Vector3 ResolveBlackBodyColour(double Kelvin)
{
    const double T = std::clamp(Kelvin, 1000.0, 40000.0) / 100.0;
    auto Linear = [](double Srgb) {
        const double V = std::clamp(Srgb / 255.0, 0.0, 1.0);
        return V <= 0.04045 ? V / 12.92 : std::pow((V + 0.055) / 1.055, 2.4);
    };
    const double Red   = T <= 66 ? 255.0 : 329.698727446 * std::pow(T - 60, -0.1332047592);
    const double Green = T <= 66 ? 99.4708025861 * std::log(T) - 161.1195681661 : 288.1221695283 * std::pow(T - 60, -0.0755148492);
    const double Blue  = T >= 66 ? 255.0 : (T <= 19 ? 0.0 : 138.5177312231 * std::log(T - 10) - 305.0447927307);
    const Vector3 Colour = {Linear(Red), Linear(Green), Linear(Blue)};
    const double  Peak   = std::max({Colour[0], Colour[1], Colour[2], 1e-6});
    return {Colour[0] / Peak, Colour[1] / Peak, Colour[2] / Peak};
}

// 📝 The refresh profile, 76 floats in float4 rows (AnalyticLayouts.slang AnalyticConstants).
std::vector<uint8_t> EncodeAnalyticConstants(const AnalyticCamera& Camera, const AnalyticSun& Sun, std::array<uint32_t, 2> Extent)
{
    std::vector<uint8_t> Bytes(76 * 4, 0u);
    auto Scalar = [&Bytes](size_t Index, double Value) {
        const float Single = static_cast<float>(Value);
        std::memcpy(Bytes.data() + Index * 4, &Single, 4);
    };
    auto Matrix = [&Scalar](size_t Index, const Transform& Value) {
        for (int Element = 0; Element < 16; ++Element) Scalar(Index + Element, Value[Element]);
    };
    auto Row = [&Scalar](size_t Index, const Vector3& Value, double Fourth) {
        for (int Axis = 0; Axis < 3; ++Axis) Scalar(Index + Axis, Value[Axis]);
        Scalar(Index + 3, Fourth);
    };
    const double    Aspect = static_cast<double>(Extent[0]) / std::max(1u, Extent[1]);
    const Transform View   = ConstructViewTransform(Camera.Position, Camera.Target, Camera.Up);
    const Transform ViewProjection = MultiplyTransform(ConstructPerspectiveTransform(Camera.VerticalFieldOfView, Aspect, Camera.Near), View);
    // ① the sun's box: centred a third of its side ahead of the camera, so the view in front gets most of the map's texels
    const Vector3 Forward   = NormalizeVector(SubtractVector(Camera.Target, Camera.Position));
    const Vector3 SunToward = NormalizeVector({std::cos(Sun.ElevationDegrees * DegreesToRadian) * std::cos(Sun.AzimuthDegrees * DegreesToRadian),
                                               std::cos(Sun.ElevationDegrees * DegreesToRadian) * std::sin(Sun.AzimuthDegrees * DegreesToRadian),
                                               std::sin(Sun.ElevationDegrees * DegreesToRadian)});
    const Vector3 Focus     = MultiplyAddVector(Camera.Position, Forward, SolarHalfExtent * 0.66);
    const Vector3 Texel     = {2.0 * SolarHalfExtent / SolarDepthExtent, 0.0, 0.0};
    const Vector3 SolarEye  = MultiplyAddVector(Focus, SunToward, SolarReach * 0.5);
    const Vector3 SolarUp   = std::fabs(SunToward[2]) > 0.95 ? Vector3{1.0, 0.0, 0.0} : Vector3{0.0, 0.0, 1.0};
    Transform SolarView     = ConstructViewTransform(SolarEye, Focus, SolarUp);
    // 📝 Snap the box to whole texels so the shadow edges do not crawl as the camera moves.
    SolarView[12] = std::floor(SolarView[12] / Texel[0]) * Texel[0];
    SolarView[13] = std::floor(SolarView[13] / Texel[0]) * Texel[0];
    const Transform SolarViewProjection =
        MultiplyTransform(ConstructOrthographicTransform(-SolarHalfExtent, SolarHalfExtent, -SolarHalfExtent, SolarHalfExtent, 0.1, SolarReach), SolarView);
    Matrix(0, ViewProjection);
    Matrix(16, InvertTransform(ViewProjection));
    Matrix(32, SolarViewProjection);
    // ② the sun from the level: colour from its temperature, irradiance scaled so 98 klx on a white facing surface reads bright
    //    but unclipped after the filmic curve; below the horizon it fades out over the first degrees and the sky dims to night.
    const double Exposure  = std::exp2(Sun.ExposureCompensation);
    const double Daylight  = std::clamp((Sun.ElevationDegrees + 6.0) / 12.0, 0.0, 1.0);
    const double Strength  = Sun.Enabled ? 4.0 * std::max(0.0, Sun.IlluminanceKilolux) / 98.0 * std::clamp(Sun.ElevationDegrees / 3.0 + 0.5, 0.0, 1.0) : 0.0;
    const Vector3 Colour   = ResolveBlackBodyColour(Sun.KelvinTemperature);
    Row(48, Camera.Position, Exposure);
    Row(52, SunToward, std::cos(0.5 * std::max(0.05, Sun.AngularDiameter) * DegreesToRadian));
    Row(56, ScaleVector(Colour, Strength), 1.0 / SolarDepthExtent);
    const double Sky = 0.04 + 0.96 * Daylight;
    Row(60, ScaleVector(Vector3{0.20, 0.38, 0.80}, 0.9 * Sky), 0.0005);                                       // zenith, depth bias
    Row(64, ScaleVector(Vector3{0.62 + 0.25 * (1 - Daylight), 0.72, 0.86 - 0.2 * (1 - Daylight)}, 0.9 * Sky), 0.55);   // horizon, fill
    Row(68, Vector3{0.30, 0.27, 0.22}, 1.0);                                                                  // ground, specular
    Scalar(72, Extent[0]);
    Scalar(73, Extent[1]);
    Scalar(74, 1.0 / std::max(1u, Extent[0]));
    Scalar(75, 1.0 / std::max(1u, Extent[1]));
    return Bytes;
}
} // namespace

AnalyticIlluminationSequence::AnalyticIlluminationSequence(DeviceExchange& Exchange, std::filesystem::path SpirvFolder)
    : Exchange_(Exchange), Shaders_(Exchange, std::move(SpirvFolder))
{
}

bool AnalyticIlluminationSequence::Construct(const SceneStructure& Scene, const std::shared_ptr<DeviceRegion>& VertexRegion,
                                             const std::shared_ptr<DeviceRegion>& OccupantRegion, const std::shared_ptr<DeviceRegion>& MaterialRegion,
                                             std::string* OutDiagnostic)
{
    try
    {
        Scene_          = &Scene;
        VertexRegion_   = VertexRegion;
        OccupantRegion_ = OccupantRegion;
        MaterialRegion_ = MaterialRegion;
        using D = DescriptorCode;
        const std::vector<DescriptorSlot> Slots = {{D::Uniform}, {D::StorageRead}, {D::StorageRead}, {D::TextureDepth2D}, {D::SamplerComparison}};
        Layout_ = Exchange_.ConstructDescriptorLayout(Slots, "Analytic illumination");
        ConstantRegion_ = Exchange_.ConstructRegion(76 * 4, "Analytic refresh profile");
        std::vector<uint32_t> Identities(std::max<size_t>(1, Scene.Occupants.size()));
        for (uint32_t Index = 0; Index < Identities.size(); ++Index) Identities[Index] = Index;
        Instances_ = Exchange_.ConstructRegionFrom(Identities.data(), Identities.size() * 4, "Analytic instance identities");
        SamplerSpecification Comparison;
        Comparison.MagFilter = FilterMode::Linear;
        Comparison.MinFilter = FilterMode::Linear;
        Comparison.Compare   = CompareFunction::LessEqual;
        Comparison_ = Exchange_.ConstructSampler(Comparison);
        Solar_ = std::make_unique<SolarDepthRasterization>(Exchange_, Shaders_);
        Solar_->ConstructPrograms(Layout_);
        Solar_->ConstructTargets(SolarDepthExtent);
        Shading_ = std::make_unique<SurfaceShadingRasterization>(Exchange_, Shaders_);
        Shading_->ConstructPrograms(Layout_);
        Group_ = Exchange_.ConstructDescriptorGroup(Layout_, {ConstantRegion_, OccupantRegion_, MaterialRegion_, Solar_->DepthView, Comparison_},
                                                    "Analytic illumination");
    }
    catch (const std::exception& Error)
    {
        if (OutDiagnostic) *OutDiagnostic = std::string("analytic illumination: ") + Error.what();
        return false;
    }
    return true;
}

bool AnalyticIlluminationSequence::Refresh(const AnalyticCamera& Camera, const AnalyticSun& Sun, std::array<uint32_t, 2> Extent,
                                           const std::shared_ptr<DeviceTextureView>& Target, std::string* OutDiagnostic)
{
    if (!Scene_ || !Target) return false;
    const auto Start = std::chrono::steady_clock::now();
    if (Shading_->Extent != Extent) Shading_->ConstructTargets(Extent[0], Extent[1]);
    const std::vector<uint8_t> Profile = EncodeAnalyticConstants(Camera, Sun, Extent);
    Exchange_.WriteRegion(ConstantRegion_, 0, Profile.data(), Profile.size());
    CommandSequence Sequence = Exchange_.BeginSequence();
    Solar_->Record(Sequence, Group_, *Scene_, VertexRegion_, Instances_);
    Shading_->Record(Sequence, Group_, *Scene_, VertexRegion_, Instances_, Target);
    Sequence.OrderAfterEarlierWork();
    Exchange_.LabelSubmissions("analytic illumination refresh");
    const bool Submitted = Exchange_.Submit(Sequence, OutDiagnostic);
    LastRefreshMilliseconds_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
    return Submitted;
}
} // namespace Frontier::AnalyticIllumination
