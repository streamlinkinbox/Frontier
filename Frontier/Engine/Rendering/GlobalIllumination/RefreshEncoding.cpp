/*==========================================================================================================================================
                                                          REFRESHENCODING.CPP
==========================================================================================================================================*/
// 🧩 The 1072-byte RefreshConstants every program reads: camera, jitter, light, toggles, focus (IlluminationHost.EncodeFrameRecord).

#include "RefreshEncoding.h"

#include "IlluminationHost.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Frontier::GlobalIllumination
{

std::vector<uint8_t> RefreshEncoder::Encode(const CameraPose& Camera, const LightSolution& Light, const RefreshToggles& Toggles,
                                            const RefreshExternalInputs& External, std::array<uint32_t, 2> ScreenExtent,
                                            std::array<uint32_t, 2> TraceExtent, uint32_t TraceScale, uint32_t RefreshIndex, const Vector3& Focus,
                                            bool PriorValid)
{
    std::vector<uint8_t> Bytes(1072, 0u);
    auto Scalar   = [&Bytes](size_t Index, double Value) { const float Single = static_cast<float>(Value); std::memcpy(Bytes.data() + Index * 4, &Single, 4); };
    auto Unsigned = [&Bytes](size_t Index, uint32_t Value) { std::memcpy(Bytes.data() + Index * 4, &Value, 4); };
    auto Matrix   = [&Scalar](size_t Index, const Transform& Value) { for (int Element = 0; Element < 16; ++Element) Scalar(Index + Element, Value[Element]); };
    const double Width  = ScreenExtent[0];
    const double Height = ScreenExtent[1];
    const bool     Jittered = Toggles.Antialias;
    const uint32_t Sequence = (RefreshIndex % 16) + 1;
    const double JitterX = Jittered ? (ProjectRadicalInverse(Sequence, 2) - 0.5) * 2.0 / Width : 0.0;
    const double JitterY = Jittered ? (ProjectRadicalInverse(Sequence, 3) - 0.5) * 2.0 / Height : 0.0;
    const Transform View       = ConstructViewTransform(Camera.Position, Camera.Target, Camera.Up);
    const Transform Projection = ConstructPerspectiveTransform(Camera.VerticalFieldOfView, Camera.Aspect, Camera.Near, JitterX, JitterY);
    const Transform Unjittered = MultiplyTransform(ConstructPerspectiveTransform(Camera.VerticalFieldOfView, Camera.Aspect, Camera.Near), View);
    const Transform ViewProjection = MultiplyTransform(Projection, View);
    const Transform Previous   = PreviousUnjittered ? *PreviousUnjittered : Unjittered;
    PreviousUnjittered = Unjittered;
    Matrix(0, ViewProjection);
    Matrix(16, InvertTransform(ViewProjection));
    Matrix(32, Unjittered);
    Matrix(48, Previous);
    Matrix(64, View);
    for (int Axis = 0; Axis < 3; ++Axis) Scalar(80 + Axis, Camera.Position[Axis]);
    Unsigned(83, RefreshIndex);
    for (int Axis = 0; Axis < 3; ++Axis) Scalar(84 + Axis, Light.Direction[Axis]);
    Scalar(87, External.ElapsedSeconds);
    for (int Channel = 0; Channel < 3; ++Channel) Scalar(88 + Channel, Light.Illuminance[Channel]);
    Scalar(91, Toggles.SkyScale);
    Scalar(92, Width);
    Scalar(93, Height);
    Scalar(94, 1.0 / Width);
    Scalar(95, 1.0 / Height);
    Scalar(96, TraceExtent[0]);
    Scalar(97, TraceExtent[1]);
    Scalar(98, 1.0 / TraceExtent[0]);
    Scalar(99, 1.0 / TraceExtent[1]);
    Scalar(100, JitterX);
    Scalar(101, JitterY);
    Unsigned(102, TraceScale);
    uint32_t Features = 0;
    const std::pair<bool, uint32_t> Bits[] = {
        {Toggles.Indirect, FeatureBit::Indirect}, {Toggles.TemporalReuse, FeatureBit::TemporalReuse}, {Toggles.SpatialReuse, FeatureBit::SpatialReuse},
        {Toggles.VisibilityReuse, FeatureBit::VisibilityReuse}, {Toggles.VarianceFilter, FeatureBit::VarianceFilter}, {Toggles.Reflection, FeatureBit::Reflection},
        {Toggles.CardRadiosity, FeatureBit::CardRadiosity}, {Toggles.Antialias, FeatureBit::Antialias}, {Toggles.DirectShadow, FeatureBit::DirectShadow},
        {Toggles.AutoExposure, FeatureBit::AutoExposure}, {Toggles.RadianceRefresh, FeatureBit::RadianceRefresh}, {Toggles.RayTracing, FeatureBit::RayTracing},
        {Toggles.RayTracedShadows, FeatureBit::RayTracedShadows}, {Toggles.RussianRoulette, FeatureBit::RussianRoulette},
        {Toggles.FireflyFilter, FeatureBit::FireflyFilter}, {Toggles.MultiBounceNee, FeatureBit::MultiBounceNee},
        {Toggles.RayTracedReflections, FeatureBit::RayTracedReflections}};
    for (const auto& [Enabled, Bit] : Bits) if (Enabled) Features |= Bit;
    const uint32_t Bounces     = std::max(1u, std::min(7u, Toggles.MaxBounces ? Toggles.MaxBounces : 3u));
    const uint32_t Candidates  = std::max(1u, std::min(7u, Toggles.InitialCandidates ? Toggles.InitialCandidates : 2u));
    const uint32_t ReflBounces = std::max(1u, std::min(7u, Toggles.ReflectionBounces ? Toggles.ReflectionBounces : 2u));
    Features |= (Bounces << 20) | (Candidates << 24) | (ReflBounces << 28);
    if (!PriorValid)
    {
        Features &= ~FeatureBit::TemporalReuse;
        Features |= FeatureBit::TemporalRestart;
    }
    if (External.SmokeResident) Features |= 4096u;
    Unsigned(103, Features);
    Unsigned(104, Toggles.Inspection);
    Scalar(105, Toggles.TemporalTallyLimit);
    Scalar(106, Toggles.SpatialRadius);
    Unsigned(107, Toggles.SpatialCount);
    for (int Channel = 0; Channel < 3; ++Channel) Scalar(108 + Channel, Light.NightSky[Channel]);
    Scalar(111, Toggles.IncandescenceScale);
    Unsigned(112, Toggles.Refinements);
    Scalar(113, Toggles.ExposureCompensation);
    Scalar(114, Toggles.IndirectScale);
    // 📝 JS Math.round rounds halfway cases towards +∞.
    const uint32_t DistanceMode = Toggles.DistanceInspection & 15u;
    const uint32_t RoughByte    = static_cast<uint32_t>(std::max(1.0, std::min(255.0, std::floor(Toggles.ReflectionRoughness * 255.0 + 0.5))));
    const uint32_t ScaleShort   = static_cast<uint32_t>(std::max(1.0, std::min(65535.0, std::floor(Toggles.ReflectionScale * 1000.0 + 0.5) + 1.0)));
    Unsigned(115, DistanceMode | (RoughByte << 8) | (ScaleShort << 16));
    for (int Axis = 0; Axis < 3; ++Axis) Scalar(116 + Axis, Focus[Axis]);
    Unsigned(119, External.PresentationControl);
    std::memcpy(Bytes.data() + 120 * 4, External.FireBlock.data(), 32);
    const size_t EnvironmentCount = std::min<size_t>(External.EnvironmentBlock.size(), 140);
    if (EnvironmentCount > 0) std::memcpy(Bytes.data() + 128 * 4, External.EnvironmentBlock.data(), EnvironmentCount * 4);
    return Bytes;
}

} // namespace Frontier::GlobalIllumination
