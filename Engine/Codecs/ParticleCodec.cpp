/*============================================================================================================================================
                                                             PARTICLECODEC.CPP
============================================================================================================================================*/
// 🧩 Two-Tier Hybrid .ParticleSpace encoder & decoder delegating colour to ColourCodec and forces to VectorFieldCodec

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/ParticleCodec.h"

#include "Engine/Codecs/ColourCodec.h"
#include "Engine/Codecs/VectorFieldCodec.h"
#include "Engine/ParticleSpace/Origins/StructuralOrigins.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>
#include <type_traits>

namespace Frontier::Codecs
{

namespace
{

using namespace Frontier::ParticleSpace;

[[nodiscard]] toml::array PackAxis3D(const Mathematics::Axis3D& Axis)
{
    toml::array Arr;
    Arr.push_back(static_cast<double>(Axis.X));
    Arr.push_back(static_cast<double>(Axis.Y));
    Arr.push_back(static_cast<double>(Axis.Z));
    return Arr;
}

[[nodiscard]] std::string LowercaseToken(std::string_view Text)
{
    std::string Out;
    Out.reserve(Text.size());
    for (char Ch : Text)
    {
        Out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(Ch))));
    }
    return Out;
}

[[nodiscard]] const toml::node* LookupCaseInsensitive(const toml::table& Table, std::string_view Key) noexcept
{
    if (const toml::node* Direct = Table.get(Key))
    {
        return Direct;
    }
    const std::string Target = LowercaseToken(Key);
    for (const auto& [EntryKey, EntryNode] : Table)
    {
        if (LowercaseToken(EntryKey.str()) == Target)
        {
            return &EntryNode;
        }
    }
    return nullptr;
}

[[nodiscard]] std::string ReadStringField(const toml::table& Table, std::string_view Key, std::string_view Fallback = {})
{
    if (const toml::node* EntryNode = LookupCaseInsensitive(Table, Key))
    {
        if (auto Str = EntryNode->value<std::string>())
        {
            return *Str;
        }
    }
    return std::string(Fallback);
}

[[nodiscard]] float ReadFloatField(const toml::table& Table, std::string_view Key, float Fallback) noexcept
{
    if (const toml::node* EntryNode = LookupCaseInsensitive(Table, Key))
    {
        if (auto Num = EntryNode->value<double>())
        {
            return static_cast<float>(*Num);
        }
    }
    return Fallback;
}

[[nodiscard]] uint32_t ReadUintField(const toml::table& Table, std::string_view Key, uint32_t Fallback) noexcept
{
    if (const toml::node* EntryNode = LookupCaseInsensitive(Table, Key))
    {
        if (auto Num = EntryNode->value<int64_t>())
        {
            return static_cast<uint32_t>(std::max<int64_t>(0, *Num));
        }
    }
    return Fallback;
}

[[nodiscard]] bool ReadBoolField(const toml::table& Table, std::string_view Key, bool Fallback) noexcept
{
    if (const toml::node* EntryNode = LookupCaseInsensitive(Table, Key))
    {
        if (auto B = EntryNode->value<bool>())
        {
            return *B;
        }
    }
    return Fallback;
}

[[nodiscard]] Mathematics::Axis3D ReadAxis3DField(
    const toml::table&  Table,
    std::string_view    Key,
    Mathematics::Axis3D Fallback) noexcept
{
    if (const toml::node* EntryNode = LookupCaseInsensitive(Table, Key))
    {
        if (const toml::array* Arr = EntryNode->as_array(); Arr != nullptr && Arr->size() >= 3u)
        {
            const double X = Arr->get(0)->value_or(static_cast<double>(Fallback.X));
            const double Y = Arr->get(1)->value_or(static_cast<double>(Fallback.Y));
            const double Z = Arr->get(2)->value_or(static_cast<double>(Fallback.Z));
            return { static_cast<float>(X), static_cast<float>(Y), static_cast<float>(Z) };
        }
    }
    return Fallback;
}

[[nodiscard]] const toml::table* ReadSubTable(const toml::table& Table, std::string_view Key) noexcept
{
    if (const toml::node* EntryNode = LookupCaseInsensitive(Table, Key))
    {
        return EntryNode->as_table();
    }
    return nullptr;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              MODULAR RIG SERIALIZATION
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] toml::table SerializeOrigin(const ReleaseOriginSpec& Origin)
{
    toml::table Out;
    Out.insert("Category", std::string(QueryReleaseOriginCategoryToken(QueryReleaseOriginCategory(Origin))));
    std::visit(
        [&Out](const auto& Entry)
        {
            using T = std::decay_t<decltype(Entry)>;
            if constexpr (std::is_same_v<T, PointOriginSpec>)
            {
                Out.insert("OriginMeters", PackAxis3D(Entry.OriginMeters));
            }
            else if constexpr (std::is_same_v<T, SphereVolumeOriginSpec>)
            {
                Out.insert("OriginMeters", PackAxis3D(Entry.OriginMeters));
                Out.insert("RadiusMeters", static_cast<double>(Entry.RadiusMeters));
                Out.insert("ShellOnly", Entry.ShellOnly);
            }
            else if constexpr (std::is_same_v<T, DiscOriginSpec>)
            {
                Out.insert("OriginMeters", PackAxis3D(Entry.OriginMeters));
                Out.insert("NormalAxis", PackAxis3D(Entry.NormalAxis));
                Out.insert("RadiusMeters", static_cast<double>(Entry.RadiusMeters));
            }
            else if constexpr (std::is_same_v<T, BoxVolumeOriginSpec>)
            {
                Out.insert("OriginMeters", PackAxis3D(Entry.OriginMeters));
                Out.insert("HalfExtentsMeters", PackAxis3D(Entry.HalfExtentsMeters));
            }
            else if constexpr (std::is_same_v<T, SurfaceLatticeOriginSpec>)
            {
                Out.insert("OriginMeters", PackAxis3D(Entry.OriginMeters));
                Out.insert("Columns", static_cast<int64_t>(Entry.Columns));
                Out.insert("Rows", static_cast<int64_t>(Entry.Rows));
                Out.insert("ExtentMeters", static_cast<double>(Entry.ExtentMeters));
            }
            else if constexpr (std::is_same_v<T, DualElectrodeOriginSpec>)
            {
                Out.insert("CathodeMeters", PackAxis3D(Entry.CathodeMeters));
                Out.insert("AnodeMeters", PackAxis3D(Entry.AnodeMeters));
                Out.insert("SpreadRadius", static_cast<double>(Entry.SpreadRadius));
                Out.insert("TargetRadius", static_cast<double>(Entry.TargetRadius));
                Out.insert("ArcMode", static_cast<int64_t>(Entry.ArcMode));
                Out.insert("ShowElectrodes", Entry.ShowElectrodes);
            }
            else if constexpr (std::is_same_v<T, SpiderwebOriginSpec>)
            {
                Out.insert("OriginMeters", PackAxis3D(Entry.OriginMeters));
                Out.insert("WebMode", static_cast<int64_t>(Entry.WebMode));
                Out.insert("SpokeCount", static_cast<int64_t>(Entry.SpokeCount));
                Out.insert("RingCount", static_cast<int64_t>(Entry.RingCount));
                Out.insert("RadiusMeters", static_cast<double>(Entry.RadiusMeters));
            }
            else if constexpr (std::is_same_v<T, ClothGridOriginSpec>)
            {
                Out.insert("OriginMeters", PackAxis3D(Entry.OriginMeters));
                Out.insert("RowCount", static_cast<int64_t>(Entry.RowCount));
                Out.insert("SegmentCount", static_cast<int64_t>(Entry.SegmentCount));
                Out.insert("WidthMeters", static_cast<double>(Entry.WidthMeters));
                Out.insert("HeightMeters", static_cast<double>(Entry.HeightMeters));
            }
            else if constexpr (std::is_same_v<T, BotanicalRootOriginSpec>)
            {
                Out.insert("OriginMeters", PackAxis3D(Entry.OriginMeters));
                Out.insert("StemCount", static_cast<int64_t>(Entry.StemCount));
                Out.insert("SegmentCount", static_cast<int64_t>(Entry.SegmentCount));
                Out.insert("PatchRadius", static_cast<double>(Entry.PatchRadius));
                Out.insert("HeightMeters", static_cast<double>(Entry.HeightMeters));
            }
            else if constexpr (std::is_same_v<T, CanopySkeletonOriginSpec>)
            {
                Out.insert("OriginMeters", PackAxis3D(Entry.OriginMeters));
                Out.insert("BranchCount", static_cast<int64_t>(Entry.BranchCount));
                Out.insert("SegmentCount", static_cast<int64_t>(Entry.SegmentCount));
                Out.insert("CanopyHeight", static_cast<double>(Entry.CanopyHeight));
                Out.insert("CrownSpread", static_cast<double>(Entry.CrownSpread));
            }
            else if constexpr (std::is_same_v<T, SurfaceAnchorOriginSpec>)
            {
                Out.insert("SurfacePreset", static_cast<int64_t>(Entry.SurfacePreset));
                Out.insert("SampleMode", static_cast<int64_t>(Entry.SampleMode));
                Out.insert("OriginMeters", PackAxis3D(Entry.OriginMeters));
                Out.insert("EulerRotationDeg", PackAxis3D(Entry.EulerRotationDeg));
                Out.insert("ScaleMeters", static_cast<double>(Entry.ScaleMeters));
                Out.insert("Columns", static_cast<int64_t>(Entry.Columns));
                Out.insert("Rows", static_cast<int64_t>(Entry.Rows));
                Out.insert("CustomAssetIdentifier", Entry.CustomAssetIdentifier);
            }
            else if constexpr (std::is_same_v<T, FireworkPatternOriginSpec>)
            {
                Out.insert("Shape", static_cast<int64_t>(Entry.Shape));
                Out.insert("OriginMeters", PackAxis3D(Entry.OriginMeters));
                Out.insert("SkyText", Entry.SkyText);
                Out.insert("PatternScale", static_cast<double>(Entry.PatternScale));
                Out.insert("ShellMode", static_cast<int64_t>(Entry.ShellMode));
            }
        },
        Origin);
    return Out;
}

[[nodiscard]] ReleaseOriginSpec DeserializeOrigin(const toml::table& Table)
{
    const std::string CatStr = ReadStringField(Table, "Category", "Point");
    const ReleaseOriginCategory Cat = ParseReleaseOriginCategoryToken(CatStr);
    switch (Cat)
    {
    case ReleaseOriginCategory::SphereVolume:
        return SphereVolumeOriginSpec{
            ReadAxis3DField(Table, "OriginMeters", { 0.0f, 0.0f, 1.2f }),
            ReadFloatField(Table, "RadiusMeters", 0.5f),
            ReadBoolField(Table, "ShellOnly", false),
        };
    case ReleaseOriginCategory::Disc:
        return DiscOriginSpec{
            ReadAxis3DField(Table, "OriginMeters", { 0.0f, 0.0f, 0.2f }),
            ReadAxis3DField(Table, "NormalAxis", { 0.0f, 0.0f, 1.0f }),
            ReadFloatField(Table, "RadiusMeters", 1.0f),
        };
    case ReleaseOriginCategory::BoxVolume:
        return BoxVolumeOriginSpec{
            ReadAxis3DField(Table, "OriginMeters", { 0.0f, 0.0f, 1.0f }),
            ReadAxis3DField(Table, "HalfExtentsMeters", { 1.0f, 1.0f, 0.5f }),
        };
    case ReleaseOriginCategory::SurfaceLattice:
        return SurfaceLatticeOriginSpec{
            ReadAxis3DField(Table, "OriginMeters", { 0.0f, 0.0f, 0.0f }),
            ReadUintField(Table, "Columns", 24u),
            ReadUintField(Table, "Rows", 24u),
            ReadFloatField(Table, "ExtentMeters", 3.0f),
        };
    case ReleaseOriginCategory::DualElectrode:
        return DualElectrodeOriginSpec{
            ReadAxis3DField(Table, "CathodeMeters", { -2.0f, 0.0f, 1.2f }),
            ReadAxis3DField(Table, "AnodeMeters", { 2.0f, 0.0f, 1.2f }),
            ReadFloatField(Table, "SpreadRadius", 0.08f),
            ReadFloatField(Table, "TargetRadius", 0.25f),
            ReadUintField(Table, "ArcMode", 0u),
            ReadBoolField(Table, "ShowElectrodes", false),
        };
    case ReleaseOriginCategory::SpiderwebWeave:
        return SpiderwebOriginSpec{
            ReadAxis3DField(Table, "OriginMeters", { 0.0f, 0.0f, 1.6f }),
            ReadUintField(Table, "WebMode", 1u),
            ReadUintField(Table, "SpokeCount", 18u),
            ReadUintField(Table, "RingCount", 14u),
            ReadFloatField(Table, "RadiusMeters", 2.2f),
        };
    case ReleaseOriginCategory::ClothGrid:
        return ClothGridOriginSpec{
            ReadAxis3DField(Table, "OriginMeters", { 0.0f, 0.0f, 2.4f }),
            ReadUintField(Table, "RowCount", 28u),
            ReadUintField(Table, "SegmentCount", 16u),
            ReadFloatField(Table, "WidthMeters", 2.6f),
            ReadFloatField(Table, "HeightMeters", 2.4f),
        };
    case ReleaseOriginCategory::BotanicalRoot:
        return BotanicalRootOriginSpec{
            ReadAxis3DField(Table, "OriginMeters", { 0.0f, 0.0f, 0.0f }),
            ReadUintField(Table, "StemCount", 28u),
            ReadUintField(Table, "SegmentCount", 16u),
            ReadFloatField(Table, "PatchRadius", 2.4f),
            ReadFloatField(Table, "HeightMeters", 1.4f),
        };
    case ReleaseOriginCategory::CanopySkeleton:
        return CanopySkeletonOriginSpec{
            ReadAxis3DField(Table, "OriginMeters", { 0.0f, 0.0f, 0.0f }),
            ReadUintField(Table, "BranchCount", 28u),
            ReadUintField(Table, "SegmentCount", 16u),
            ReadFloatField(Table, "CanopyHeight", 2.8f),
            ReadFloatField(Table, "CrownSpread", 1.8f),
        };
    case ReleaseOriginCategory::SurfaceAnchor:
        return SurfaceAnchorOriginSpec{
            static_cast<SurfacePresetCategory>(std::min(16u, ReadUintField(Table, "SurfacePreset", 0u))),
            static_cast<SurfaceSampleMode>(std::min(2u, ReadUintField(Table, "SampleMode", 0u))),
            ReadAxis3DField(Table, "OriginMeters", { 0.0f, 0.0f, 0.0f }),
            ReadAxis3DField(Table, "EulerRotationDeg", { 0.0f, 0.0f, 0.0f }),
            ReadFloatField(Table, "ScaleMeters", 1.5f),
            ReadUintField(Table, "Columns", 24u),
            ReadUintField(Table, "Rows", 24u),
            ReadStringField(Table, "CustomAssetIdentifier", ""),
        };
    case ReleaseOriginCategory::FireworkPattern:
        return FireworkPatternOriginSpec{
            static_cast<FireworkShapeCategory>(std::min(9u, ReadUintField(Table, "Shape", 0u))),
            ReadAxis3DField(Table, "OriginMeters", { 0.0f, 0.0f, 0.2f }),
            ReadStringField(Table, "SkyText", "2026"),
            ReadFloatField(Table, "PatternScale", 2.6f),
            ReadUintField(Table, "ShellMode", 1u),
        };
    case ReleaseOriginCategory::Point:
    default:
        return PointOriginSpec{
            ReadAxis3DField(Table, "OriginMeters", { 0.0f, 0.0f, 0.5f }),
        };
    }
}

[[nodiscard]] toml::table SerializeCarrier(const VisualCarrierSpec& Carrier)
{
    toml::table Out;
    Out.insert("Category", std::string(QueryVisualCarrierCategoryToken(QueryVisualCarrierCategory(Carrier))));
    std::visit(
        [&Out](const auto& Entry)
        {
            using T = std::decay_t<decltype(Entry)>;
            if constexpr (std::is_same_v<T, SpriteBillboardCarrier>)
            {
                Out.insert("CutoutShape", static_cast<int64_t>(Entry.CutoutShape));
                Out.insert("LeafStyle", static_cast<int64_t>(Entry.LeafStyle));
                Out.insert("BokehAperture", static_cast<int64_t>(Entry.BokehAperture));
                Out.insert("HeadScale", static_cast<double>(Entry.HeadScale));
                Out.insert("StreakThinness", static_cast<double>(Entry.StreakThinness));
                Out.insert("BlendEquation", std::string(ColourSpace::QueryOpticalBlendToken(Entry.BlendEquation)));
            }
            else if constexpr (std::is_same_v<T, RibbonStrandCarrier>)
            {
                Out.insert("Profile", static_cast<int64_t>(Entry.Profile));
                Out.insert("SegmentCount", static_cast<int64_t>(Entry.SegmentCount));
                Out.insert("WidthMeters", static_cast<double>(Entry.WidthMeters));
                Out.insert("ThicknessScale", static_cast<double>(Entry.ThicknessScale));
                Out.insert("CurveDebugPins", Entry.CurveDebugPins);
                Out.insert("BlendEquation", std::string(ColourSpace::QueryOpticalBlendToken(Entry.BlendEquation)));
            }
            else if constexpr (std::is_same_v<T, OrientedSolidCarrier>)
            {
                Out.insert("GeometryKind", static_cast<int64_t>(Entry.GeometryKind));
                Out.insert("ExtrudeGain", static_cast<double>(Entry.ExtrudeGain));
                Out.insert("MetallicRough", static_cast<double>(Entry.MetallicRough));
                Out.insert("BlendEquation", std::string(ColourSpace::QueryOpticalBlendToken(Entry.BlendEquation)));
            }
            else if constexpr (std::is_same_v<T, WingedCreatureCarrier>)
            {
                Out.insert("Species", static_cast<int64_t>(Entry.Species));
                Out.insert("FlapFrequencyHz", static_cast<double>(Entry.FlapFrequencyHz));
                Out.insert("WingspanMeters", static_cast<double>(Entry.WingspanMeters));
                Out.insert("BlendEquation", std::string(ColourSpace::QueryOpticalBlendToken(Entry.BlendEquation)));
            }
            else if constexpr (std::is_same_v<T, DielectricBeamCarrier>)
            {
                Out.insert("ArcCategory", static_cast<int64_t>(Entry.ArcCategory));
                Out.insert("CoreRadiusMeters", static_cast<double>(Entry.CoreRadiusMeters));
                Out.insert("PulseSpeedHz", static_cast<double>(Entry.PulseSpeedHz));
                Out.insert("DewIntensity", static_cast<double>(Entry.DewIntensity));
                Out.insert("BlendEquation", std::string(ColourSpace::QueryOpticalBlendToken(Entry.BlendEquation)));
            }
            else if constexpr (std::is_same_v<T, RefractionLensCarrier>)
            {
                Out.insert("LensCategory", static_cast<int64_t>(Entry.LensCategory));
                Out.insert("DistortionStrength", static_cast<double>(Entry.DistortionStrength));
                Out.insert("EventHorizonMeters", static_cast<double>(Entry.EventHorizonMeters));
                Out.insert("AccretionSwirlSpeed", static_cast<double>(Entry.AccretionSwirlSpeed));
            }
            else if constexpr (std::is_same_v<T, BackingSurfaceCarrier>)
            {
                Out.insert("FinishStyle", static_cast<int64_t>(Entry.FinishStyle));
                Out.insert("SurfacePreset", static_cast<int64_t>(Entry.SurfacePreset));
                Out.insert("SampleMode", static_cast<int64_t>(Entry.SampleMode));
                Out.insert("EulerDegrees", PackAxis3D(Entry.EulerDegrees));
                Out.insert("CustomAssetPath", Entry.CustomAssetPath);
                Out.insert("VehicleHullMode", Entry.VehicleHullMode);
            }
        },
        Carrier);
    return Out;
}

[[nodiscard]] VisualCarrierSpec DeserializeCarrier(const toml::table& Table)
{
    const std::string Cat = LowercaseToken(ReadStringField(Table, "Category", "SpriteBillboard"));
    const auto Blend = ColourSpace::ParseOpticalBlendToken(ReadStringField(Table, "BlendEquation", "Additive"));

    if (Cat == "ribbonstrand")
    {
        return RibbonStrandCarrier{
            static_cast<RibbonProfileCategory>(ReadUintField(Table, "Profile", 0u)),
            ReadUintField(Table, "SegmentCount", 16u),
            ReadFloatField(Table, "WidthMeters", 0.04f),
            ReadFloatField(Table, "ThicknessScale", 1.0f),
            ReadBoolField(Table, "CurveDebugPins", false),
            Blend,
        };
    }
    if (Cat == "orientedsolid")
    {
        return OrientedSolidCarrier{
            static_cast<SolidGeometryCategory>(ReadUintField(Table, "GeometryKind", 0u)),
            ReadFloatField(Table, "ExtrudeGain", 1.0f),
            ReadFloatField(Table, "MetallicRough", 0.22f),
            Blend,
        };
    }
    if (Cat == "wingedcreature")
    {
        return WingedCreatureCarrier{
            static_cast<WingedSpeciesCategory>(ReadUintField(Table, "Species", 0u)),
            ReadFloatField(Table, "FlapFrequencyHz", 5.5f),
            ReadFloatField(Table, "WingspanMeters", 0.22f),
            Blend,
        };
    }
    if (Cat == "dielectricbeam")
    {
        return DielectricBeamCarrier{
            static_cast<DielectricArcCategory>(ReadUintField(Table, "ArcCategory", 2u)),
            ReadFloatField(Table, "CoreRadiusMeters", 0.048f),
            ReadFloatField(Table, "PulseSpeedHz", 6.0f),
            ReadFloatField(Table, "DewIntensity", 0.85f),
            Blend,
        };
    }
    if (Cat == "refractionlens")
    {
        return RefractionLensCarrier{
            static_cast<RefractionLensCategory>(ReadUintField(Table, "LensCategory", 0u)),
            ReadFloatField(Table, "DistortionStrength", 1.0f),
            ReadFloatField(Table, "EventHorizonMeters", 0.85f),
            ReadFloatField(Table, "AccretionSwirlSpeed", 2.4f),
        };
    }
    if (Cat == "backingsurface")
    {
        return BackingSurfaceCarrier{
            static_cast<BackingFinishCategory>(ReadUintField(Table, "FinishStyle", 0u)),
            static_cast<SurfacePresetCategory>(ReadUintField(Table, "SurfacePreset", 0u)),
            static_cast<SurfaceSampleMode>(ReadUintField(Table, "SampleMode", 0u)),
            ReadAxis3DField(Table, "EulerDegrees", { 0.0f, 0.0f, 0.0f }),
            ReadStringField(Table, "CustomAssetPath", ""),
            ReadBoolField(Table, "VehicleHullMode", false),
        };
    }

    return SpriteBillboardCarrier{
        static_cast<SpriteCutoutCategory>(std::min(19u, ReadUintField(Table, "CutoutShape", 0u))),
        static_cast<LeafSubMode>(std::min(3u, ReadUintField(Table, "LeafStyle", 0u))),
        static_cast<BokehApertureCategory>(std::min(3u, ReadUintField(Table, "BokehAperture", 0u))),
        ReadFloatField(Table, "HeadScale", 1.0f),
        ReadFloatField(Table, "StreakThinness", 1.0f),
        Blend,
    };
}

[[nodiscard]] toml::table SerializeDynamics(const SimulationDynamicsProfile& Dyn)
{
    toml::table Out;
    Out.insert("Family", static_cast<int64_t>(Dyn.Family));

    {
        toml::table Bal;
        Bal.insert("DirectionAxis", PackAxis3D(Dyn.Ballistics.DirectionAxis));
        Bal.insert("SpreadRadians", static_cast<double>(Dyn.Ballistics.SpreadRadians));
        Bal.insert("SpeedMin", static_cast<double>(Dyn.Ballistics.SpeedMin));
        Bal.insert("SpeedMax", static_cast<double>(Dyn.Ballistics.SpeedMax));
        Bal.insert("DragPerSecond", static_cast<double>(Dyn.Ballistics.DragPerSecond));
        Bal.insert("GravityFactor", static_cast<double>(Dyn.Ballistics.GravityFactor));
        Bal.insert("BuoyancyAccel", static_cast<double>(Dyn.Ballistics.BuoyancyAccel));
        Bal.insert("BounceRestitution", static_cast<double>(Dyn.Ballistics.BounceRestitution));
        Bal.insert("FlutterAmp", static_cast<double>(Dyn.Ballistics.FlutterAmp));
        Bal.insert("BurstIntervalSec", static_cast<double>(Dyn.Ballistics.BurstIntervalSec));
        Bal.insert("BurstCount", static_cast<int64_t>(Dyn.Ballistics.BurstCount));
        Bal.insert("BurstAtOrigin", Dyn.Ballistics.BurstAtOrigin);
        Out.insert("Ballistics", std::move(Bal));
    }

    {
        toml::table Link;
        Link.insert("WindCouplingRate", static_cast<double>(Dyn.ForceLink.WindCouplingRate));
        Link.insert("ReceiverChannels", static_cast<int64_t>(Dyn.ForceLink.ReceiverChannels));
        Link.insert("LocalMagneticActive", Dyn.ForceLink.LocalMagneticActive);
        Link.insert("LocalMagneticStrength", static_cast<double>(Dyn.ForceLink.LocalMagneticStrength));
        Link.insert("LocalMagneticGuide", static_cast<double>(Dyn.ForceLink.LocalMagneticGuide));
        Link.insert("LocalMagneticRadius", static_cast<double>(Dyn.ForceLink.LocalMagneticRadius));
        Out.insert("ForceLink", std::move(Link));
    }

    if (Dyn.Curl.Mode != CurlManifoldMode::Off)
    {
        toml::table T;
        T.insert("Mode", static_cast<int64_t>(Dyn.Curl.Mode));
        T.insert("SpatialFreq", static_cast<double>(Dyn.Curl.SpatialFreq));
        T.insert("DriveSpeed", static_cast<double>(Dyn.Curl.DriveSpeed));
        T.insert("EvolutionRate", static_cast<double>(Dyn.Curl.EvolutionRate));
        T.insert("ShellRadius", static_cast<double>(Dyn.Curl.ShellRadius));
        Out.insert("Curl", std::move(T));
    }

    if (Dyn.Strands.Mode != StrandDynamicsMode::Off || Dyn.Strands.BeadsDrapeMode != 0u || Dyn.Strands.JellyTendrilMode != 0u)
    {
        toml::table T;
        T.insert("Mode", static_cast<int64_t>(Dyn.Strands.Mode));
        T.insert("SubVariant", static_cast<int64_t>(Dyn.Strands.SubVariant));
        T.insert("RowCount", static_cast<int64_t>(Dyn.Strands.RowCount));
        T.insert("SegmentCount", static_cast<int64_t>(Dyn.Strands.SegmentCount));
        T.insert("StiffnessRate", static_cast<double>(Dyn.Strands.StiffnessRate));
        T.insert("TearThresholdSpeed", static_cast<double>(Dyn.Strands.TearThresholdSpeed));
        T.insert("HeightMeters", static_cast<double>(Dyn.Strands.HeightMeters));
        T.insert("SagMeters", static_cast<double>(Dyn.Strands.SagMeters));
        T.insert("WaveSpeedHz", static_cast<double>(Dyn.Strands.WaveSpeedHz));
        T.insert("AnemoneThickness", static_cast<double>(Dyn.Strands.AnemoneThickness));
        T.insert("BeadsDrapeMode", static_cast<int64_t>(Dyn.Strands.BeadsDrapeMode));
        T.insert("JellyTendrilMode", static_cast<int64_t>(Dyn.Strands.JellyTendrilMode));
        T.insert("CurveDebugEnabled", Dyn.Strands.CurveDebugEnabled);
        Out.insert("Strands", std::move(T));
    }

    if (Dyn.Botanical.SeedHeadMode != 0u)
    {
        toml::table T;
        T.insert("SeedHeadMode", static_cast<int64_t>(Dyn.Botanical.SeedHeadMode));
        T.insert("DetachmentWindMin", static_cast<double>(Dyn.Botanical.DetachmentWindMin));
        T.insert("SwirlFlutter", static_cast<double>(Dyn.Botanical.SwirlFlutter));
        Out.insert("Botanical", std::move(T));
    }

    if (Dyn.Spiderweb.WebMode != 0u)
    {
        toml::table T;
        T.insert("WebMode", static_cast<int64_t>(Dyn.Spiderweb.WebMode));
        T.insert("SpokeCount", static_cast<int64_t>(Dyn.Spiderweb.SpokeCount));
        T.insert("RingCount", static_cast<int64_t>(Dyn.Spiderweb.RingCount));
        T.insert("SpinRate", static_cast<double>(Dyn.Spiderweb.SpinRate));
        T.insert("SagMeters", static_cast<double>(Dyn.Spiderweb.SagMeters));
        T.insert("DewHighlight", static_cast<double>(Dyn.Spiderweb.DewHighlight));
        Out.insert("Spiderweb", std::move(T));
    }

    if (Dyn.Ferrofluid.FerroMode != 0u)
    {
        toml::table T;
        T.insert("FerroMode", static_cast<int64_t>(Dyn.Ferrofluid.FerroMode));
        T.insert("SpikeCount", static_cast<int64_t>(Dyn.Ferrofluid.SpikeCount));
        T.insert("MagnetOrbitSpeed", static_cast<double>(Dyn.Ferrofluid.MagnetOrbitSpeed));
        T.insert("FreeDraggableMagnet", Dyn.Ferrofluid.FreeDraggableMagnet);
        Out.insert("Ferrofluid", std::move(T));
    }

    if (Dyn.Fracture.CrackMode != 0u)
    {
        toml::table T;
        T.insert("CrackMode", static_cast<int64_t>(Dyn.Fracture.CrackMode));
        T.insert("PropagationSpeed", static_cast<double>(Dyn.Fracture.PropagationSpeed));
        T.insert("FissureHeaveMeters", static_cast<double>(Dyn.Fracture.FissureHeaveMeters));
        Out.insert("Fracture", std::move(T));
    }

    if (Dyn.WaterSplash.SplashMode != 0u)
    {
        toml::table T;
        T.insert("SplashMode", static_cast<int64_t>(Dyn.WaterSplash.SplashMode));
        T.insert("VehicleSpeed", static_cast<double>(Dyn.WaterSplash.VehicleSpeed));
        T.insert("TrackRadiusMeters", static_cast<double>(Dyn.WaterSplash.TrackRadiusMeters));
        Out.insert("WaterSplash", std::move(T));
    }

    if (Dyn.EffectorGrid.Active)
    {
        toml::table T;
        T.insert("Active", true);
        T.insert("GridShape", static_cast<int64_t>(Dyn.EffectorGrid.GridShape));
        T.insert("Driver", static_cast<int64_t>(Dyn.EffectorGrid.Driver));
        T.insert("Columns", static_cast<int64_t>(Dyn.EffectorGrid.Columns));
        T.insert("Rows", static_cast<int64_t>(Dyn.EffectorGrid.Rows));
        T.insert("MinScale", static_cast<double>(Dyn.EffectorGrid.MinScale));
        T.insert("MaxHeightMeters", static_cast<double>(Dyn.EffectorGrid.MaxHeightMeters));
        T.insert("FalloffContrast", static_cast<double>(Dyn.EffectorGrid.FalloffContrast));
        T.insert("CameraBillboarded", Dyn.EffectorGrid.CameraBillboarded);
        T.insert("BlendPlaceable", Dyn.EffectorGrid.BlendPlaceable);
        T.insert("PrimaryCentreMeters", PackAxis3D(Dyn.EffectorGrid.PrimaryEffector.CentreMeters));
        T.insert("PrimaryShape", static_cast<int64_t>(Dyn.EffectorGrid.PrimaryEffector.Shape));
        T.insert("PrimarySizeMeters", static_cast<double>(Dyn.EffectorGrid.PrimaryEffector.SizeMeters));
        T.insert("SecondaryCentreMeters", PackAxis3D(Dyn.EffectorGrid.SecondaryEffector.CentreMeters));
        T.insert("SecondaryShape", static_cast<int64_t>(Dyn.EffectorGrid.SecondaryEffector.Shape));
        T.insert("SecondarySizeMeters", static_cast<double>(Dyn.EffectorGrid.SecondaryEffector.SizeMeters));
        Out.insert("EffectorGrid", std::move(T));
    }

    if (Dyn.Boids.Active || Dyn.Boids.PulseHz > 0.0f)
    {
        toml::table T;
        T.insert("Active", Dyn.Boids.Active);
        T.insert("NeighbourRadius", static_cast<double>(Dyn.Boids.NeighbourRadius));
        T.insert("CohesionWeight", static_cast<double>(Dyn.Boids.CohesionWeight));
        T.insert("AlignmentWeight", static_cast<double>(Dyn.Boids.AlignmentWeight));
        T.insert("SeparationWeight", static_cast<double>(Dyn.Boids.SeparationWeight));
        T.insert("TargetSpeed", static_cast<double>(Dyn.Boids.TargetSpeed));
        T.insert("PulseHz", static_cast<double>(Dyn.Boids.PulseHz));
        T.insert("PulseDepth", static_cast<double>(Dyn.Boids.PulseDepth));
        Out.insert("Boids", std::move(T));
    }

    if (Dyn.Molecular.Active || Dyn.Family == ParticleSimulationFamily::MolecularAtoms ||
        Dyn.Family == ParticleSimulationFamily::ChemicalReaction)
    {
        toml::table T;
        T.insert("Active", Dyn.Molecular.Active);
        T.insert("Temperature", static_cast<double>(Dyn.Molecular.Temperature));
        T.insert("Epsilon", static_cast<double>(Dyn.Molecular.Epsilon));
        T.insert("Sigma", static_cast<double>(Dyn.Molecular.Sigma));
        T.insert("LangevinDamping", static_cast<double>(Dyn.Molecular.LangevinDamping));
        T.insert("ReactionRate", static_cast<double>(Dyn.Molecular.ReactionRate));
        T.insert("DissociationRate", static_cast<double>(Dyn.Molecular.DissociationRate));
        T.insert("ReactionRadius", static_cast<double>(Dyn.Molecular.ReactionRadius));
        T.insert("InitialFractionA", static_cast<double>(Dyn.Molecular.InitialFractionA));
        T.insert("FixedSubstepSec", static_cast<double>(Dyn.Molecular.FixedSubstepSec));
        Out.insert("Molecular", std::move(T));
    }

    if (Dyn.Transition.Mode != TransitionDerezMode::Off)
    {
        toml::table T;
        T.insert("Mode", static_cast<int64_t>(Dyn.Transition.Mode));
        T.insert("HoldBaseSec", static_cast<double>(Dyn.Transition.HoldBaseSec));
        T.insert("HoldDurationSec", static_cast<double>(Dyn.Transition.HoldDurationSec));
        T.insert("BurstShare", static_cast<double>(Dyn.Transition.BurstShare));
        T.insert("FragmentOnFloor", Dyn.Transition.FragmentOnFloor);
        T.insert("ChildLifeSec", static_cast<double>(Dyn.Transition.ChildLifeSec));
        T.insert("WavePattern", static_cast<int64_t>(Dyn.Transition.WavePattern));
        T.insert("AssembleMode", static_cast<int64_t>(Dyn.Transition.AssembleMode));
        T.insert("ShellRadiusMeters", static_cast<double>(Dyn.Transition.ShellRadiusMeters));
        T.insert("AfterTransform", static_cast<int64_t>(Dyn.Transition.AfterTransform));
        T.insert("GlitchRateHz", static_cast<double>(Dyn.Transition.GlitchRateHz));
        Out.insert("Transition", std::move(T));
    }

    if (Dyn.LocalAttractor.Enabled)
    {
        toml::table T;
        T.insert("Enabled", true);
        T.insert("Strength", static_cast<double>(Dyn.LocalAttractor.Strength));
        T.insert("RadiusMeters", static_cast<double>(Dyn.LocalAttractor.RadiusMeters));
        T.insert("SwirlSpeed", static_cast<double>(Dyn.LocalAttractor.SwirlSpeed));
        T.insert("SwallowRadius", static_cast<double>(Dyn.LocalAttractor.SwallowRadius));
        Out.insert("LocalAttractor", std::move(T));
    }

    if (Dyn.HeatShimmer.Enabled)
    {
        toml::table T;
        T.insert("Enabled", true);
        T.insert("RadiusMeters", static_cast<double>(Dyn.HeatShimmer.RadiusMeters));
        T.insert("Strength", static_cast<double>(Dyn.HeatShimmer.Strength));
        T.insert("SpatialFreq", static_cast<double>(Dyn.HeatShimmer.SpatialFreq));
        T.insert("DriftRate", static_cast<double>(Dyn.HeatShimmer.DriftRate));
        Out.insert("HeatShimmer", std::move(T));
    }

    if (Dyn.BlackHole.Enabled)
    {
        toml::table T;
        T.insert("Enabled", true);
        T.insert("HorizonRadius", static_cast<double>(Dyn.BlackHole.HorizonRadius));
        T.insert("EinsteinGain", static_cast<double>(Dyn.BlackHole.EinsteinGain));
        Out.insert("BlackHole", std::move(T));
    }

    if (Dyn.Shield.Active)
    {
        toml::table T;
        T.insert("Active", true);
        T.insert("Form", static_cast<int64_t>(Dyn.Shield.Form));
        T.insert("MotifBitmask", static_cast<int64_t>(Dyn.Shield.MotifBitmask));
        T.insert("SpinRate", static_cast<double>(Dyn.Shield.SpinRate));
        T.insert("GlitchRateHz", static_cast<double>(Dyn.Shield.GlitchRateHz));
        Out.insert("Shield", std::move(T));
    }

    if (Dyn.Pyro.FireworkMode != 0u || Dyn.Pyro.FlareSalvoMode != 0u ||
        Dyn.Pyro.TrailLengthSec > 0.0f || Dyn.Pyro.BeamModeActive > 0.0f)
    {
        toml::table T;
        T.insert("FireworkMode", static_cast<int64_t>(Dyn.Pyro.FireworkMode));
        T.insert("FlareSalvoMode", static_cast<int64_t>(Dyn.Pyro.FlareSalvoMode));
        T.insert("TrailLengthSec", static_cast<double>(Dyn.Pyro.TrailLengthSec));
        T.insert("BeamModeActive", static_cast<double>(Dyn.Pyro.BeamModeActive));
        Out.insert("Pyro", std::move(T));
    }

    return Out;
}

void DeserializeDynamics(const toml::table& Table, SimulationDynamicsProfile& Out)
{
    Out.Family = static_cast<ParticleSimulationFamily>(ReadUintField(Table, "Family", 0u));

    if (const toml::table* Bal = ReadSubTable(Table, "Ballistics"))
    {
        Out.Ballistics.DirectionAxis     = ReadAxis3DField(*Bal, "DirectionAxis", { 0.0f, 0.0f, 1.0f });
        Out.Ballistics.SpreadRadians     = ReadFloatField(*Bal, "SpreadRadians", 0.5f);
        Out.Ballistics.SpeedMin          = ReadFloatField(*Bal, "SpeedMin", 1.0f);
        Out.Ballistics.SpeedMax          = ReadFloatField(*Bal, "SpeedMax", 3.0f);
        Out.Ballistics.DragPerSecond     = ReadFloatField(*Bal, "DragPerSecond", 0.2f);
        Out.Ballistics.GravityFactor     = ReadFloatField(*Bal, "GravityFactor", 1.0f);
        Out.Ballistics.BuoyancyAccel     = ReadFloatField(*Bal, "BuoyancyAccel", 0.0f);
        Out.Ballistics.BounceRestitution = ReadFloatField(*Bal, "BounceRestitution", 0.0f);
        Out.Ballistics.FlutterAmp        = ReadFloatField(*Bal, "FlutterAmp", 0.0f);
        Out.Ballistics.BurstIntervalSec  = ReadFloatField(*Bal, "BurstIntervalSec", 0.0f);
        Out.Ballistics.BurstCount        = ReadUintField(*Bal, "BurstCount", 120u);
        Out.Ballistics.BurstAtOrigin     = ReadBoolField(*Bal, "BurstAtOrigin", false);
    }

    if (const toml::table* Link = ReadSubTable(Table, "ForceLink"))
    {
        Out.ForceLink.WindCouplingRate      = ReadFloatField(*Link, "WindCouplingRate", 0.0f);
        Out.ForceLink.ReceiverChannels      = static_cast<ForceChannelMask>(ReadUintField(*Link, "ReceiverChannels", 1u));
        Out.ForceLink.LocalMagneticActive   = ReadBoolField(*Link, "LocalMagneticActive", false);
        Out.ForceLink.LocalMagneticStrength = ReadFloatField(*Link, "LocalMagneticStrength", 3.0f);
        Out.ForceLink.LocalMagneticGuide    = ReadFloatField(*Link, "LocalMagneticGuide", 4.0f);
        Out.ForceLink.LocalMagneticRadius   = ReadFloatField(*Link, "LocalMagneticRadius", 6.0f);
    }

    if (const toml::table* T = ReadSubTable(Table, "Curl"))
    {
        Out.Curl.Mode          = static_cast<CurlManifoldMode>(ReadUintField(*T, "Mode", 0u));
        Out.Curl.SpatialFreq   = ReadFloatField(*T, "SpatialFreq", 0.85f);
        Out.Curl.DriveSpeed    = ReadFloatField(*T, "DriveSpeed", 2.2f);
        Out.Curl.EvolutionRate = ReadFloatField(*T, "EvolutionRate", 0.24f);
        Out.Curl.ShellRadius   = ReadFloatField(*T, "ShellRadius", 1.75f);
    }

    if (const toml::table* T = ReadSubTable(Table, "Strands"))
    {
        Out.Strands.Mode               = static_cast<StrandDynamicsMode>(ReadUintField(*T, "Mode", 0u));
        Out.Strands.SubVariant         = ReadUintField(*T, "SubVariant", 0u);
        Out.Strands.RowCount           = ReadUintField(*T, "RowCount", 28u);
        Out.Strands.SegmentCount       = ReadUintField(*T, "SegmentCount", 16u);
        Out.Strands.StiffnessRate      = ReadFloatField(*T, "StiffnessRate", 1.25f);
        Out.Strands.TearThresholdSpeed = ReadFloatField(*T, "TearThresholdSpeed", 0.0f);
        Out.Strands.HeightMeters       = ReadFloatField(*T, "HeightMeters", 2.4f);
        Out.Strands.SagMeters          = ReadFloatField(*T, "SagMeters", 0.35f);
        Out.Strands.WaveSpeedHz        = ReadFloatField(*T, "WaveSpeedHz", 2.2f);
        Out.Strands.AnemoneThickness   = ReadFloatField(*T, "AnemoneThickness", 1.0f);
        Out.Strands.BeadsDrapeMode     = ReadUintField(*T, "BeadsDrapeMode", 0u);
        Out.Strands.JellyTendrilMode   = ReadUintField(*T, "JellyTendrilMode", 0u);
        Out.Strands.CurveDebugEnabled  = ReadBoolField(*T, "CurveDebugEnabled", false);
    }

    if (const toml::table* T = ReadSubTable(Table, "Botanical"))
    {
        Out.Botanical.SeedHeadMode      = ReadUintField(*T, "SeedHeadMode", 0u);
        Out.Botanical.DetachmentWindMin = ReadFloatField(*T, "DetachmentWindMin", 2.8f);
        Out.Botanical.SwirlFlutter      = ReadFloatField(*T, "SwirlFlutter", 0.6f);
    }

    if (const toml::table* T = ReadSubTable(Table, "Spiderweb"))
    {
        Out.Spiderweb.WebMode      = ReadUintField(*T, "WebMode", 0u);
        Out.Spiderweb.SpokeCount   = ReadUintField(*T, "SpokeCount", 18u);
        Out.Spiderweb.RingCount    = ReadUintField(*T, "RingCount", 14u);
        Out.Spiderweb.SpinRate     = ReadFloatField(*T, "SpinRate", 1.0f);
        Out.Spiderweb.SagMeters    = ReadFloatField(*T, "SagMeters", 0.16f);
        Out.Spiderweb.DewHighlight = ReadFloatField(*T, "DewHighlight", 0.85f);
    }

    if (const toml::table* T = ReadSubTable(Table, "Ferrofluid"))
    {
        Out.Ferrofluid.FerroMode           = ReadUintField(*T, "FerroMode", 0u);
        Out.Ferrofluid.SpikeCount          = ReadUintField(*T, "SpikeCount", 520u);
        Out.Ferrofluid.MagnetOrbitSpeed    = ReadFloatField(*T, "MagnetOrbitSpeed", 1.35f);
        Out.Ferrofluid.FreeDraggableMagnet = ReadBoolField(*T, "FreeDraggableMagnet", false);
    }

    if (const toml::table* T = ReadSubTable(Table, "Fracture"))
    {
        Out.Fracture.CrackMode          = ReadUintField(*T, "CrackMode", 0u);
        Out.Fracture.PropagationSpeed   = ReadFloatField(*T, "PropagationSpeed", 2.6f);
        Out.Fracture.FissureHeaveMeters = ReadFloatField(*T, "FissureHeaveMeters", 0.44f);
    }

    if (const toml::table* T = ReadSubTable(Table, "WaterSplash"))
    {
        Out.WaterSplash.SplashMode        = ReadUintField(*T, "SplashMode", 0u);
        Out.WaterSplash.VehicleSpeed      = ReadFloatField(*T, "VehicleSpeed", 4.2f);
        Out.WaterSplash.TrackRadiusMeters = ReadFloatField(*T, "TrackRadiusMeters", 3.15f);
    }

    if (const toml::table* T = ReadSubTable(Table, "EffectorGrid"))
    {
        Out.EffectorGrid.Active            = ReadBoolField(*T, "Active", false);
        Out.EffectorGrid.GridShape         = static_cast<MoGraphGridShape>(ReadUintField(*T, "GridShape", 2u));
        Out.EffectorGrid.Driver            = static_cast<MoGraphEffectorDriver>(ReadUintField(*T, "Driver", 0u));
        Out.EffectorGrid.Columns           = ReadUintField(*T, "Columns", 24u);
        Out.EffectorGrid.Rows              = ReadUintField(*T, "Rows", 24u);
        Out.EffectorGrid.MinScale          = ReadFloatField(*T, "MinScale", 0.10f);
        Out.EffectorGrid.MaxHeightMeters   = ReadFloatField(*T, "MaxHeightMeters", 0.95f);
        Out.EffectorGrid.FalloffContrast   = ReadFloatField(*T, "FalloffContrast", 1.85f);
        Out.EffectorGrid.CameraBillboarded = ReadBoolField(*T, "CameraBillboarded", false);
        Out.EffectorGrid.BlendPlaceable    = ReadBoolField(*T, "BlendPlaceable", false);
        Out.EffectorGrid.PrimaryEffector.CentreMeters   = ReadAxis3DField(*T, "PrimaryCentreMeters", { 0.0f, 0.0f, 1.5f });
        Out.EffectorGrid.PrimaryEffector.Shape          = static_cast<EffectorVolumeShape>(ReadUintField(*T, "PrimaryShape", 0u));
        Out.EffectorGrid.PrimaryEffector.SizeMeters     = ReadFloatField(*T, "PrimarySizeMeters", 1.2f);
        Out.EffectorGrid.SecondaryEffector.CentreMeters = ReadAxis3DField(*T, "SecondaryCentreMeters", { 2.5f, 0.0f, 1.5f });
        Out.EffectorGrid.SecondaryEffector.Shape        = static_cast<EffectorVolumeShape>(ReadUintField(*T, "SecondaryShape", 0u));
        Out.EffectorGrid.SecondaryEffector.SizeMeters   = ReadFloatField(*T, "SecondarySizeMeters", 0.0f);
    }

    if (const toml::table* T = ReadSubTable(Table, "Boids"))
    {
        Out.Boids.Active           = ReadBoolField(*T, "Active", false);
        Out.Boids.NeighbourRadius  = ReadFloatField(*T, "NeighbourRadius", 1.4f);
        Out.Boids.CohesionWeight   = ReadFloatField(*T, "CohesionWeight", 1.15f);
        Out.Boids.AlignmentWeight  = ReadFloatField(*T, "AlignmentWeight", 1.65f);
        Out.Boids.SeparationWeight = ReadFloatField(*T, "SeparationWeight", 2.85f);
        Out.Boids.TargetSpeed      = ReadFloatField(*T, "TargetSpeed", 3.4f);
        Out.Boids.PulseHz          = ReadFloatField(*T, "PulseHz", 0.0f);
        Out.Boids.PulseDepth       = ReadFloatField(*T, "PulseDepth", 0.0f);
    }

    if (const toml::table* T = ReadSubTable(Table, "Molecular"))
    {
        Out.Molecular.Active           = ReadBoolField(*T, "Active", false);
        Out.Molecular.Temperature      = ReadFloatField(*T, "Temperature", 0.6f);
        Out.Molecular.Epsilon          = ReadFloatField(*T, "Epsilon", 0.15f);
        Out.Molecular.Sigma            = ReadFloatField(*T, "Sigma", 0.08f);
        Out.Molecular.LangevinDamping  = ReadFloatField(*T, "LangevinDamping", 2.0f);
        Out.Molecular.ReactionRate     = ReadFloatField(*T, "ReactionRate", 4.0f);
        Out.Molecular.DissociationRate = ReadFloatField(*T, "DissociationRate", 0.3f);
        Out.Molecular.ReactionRadius   = ReadFloatField(*T, "ReactionRadius", 0.12f);
        Out.Molecular.InitialFractionA = ReadFloatField(*T, "InitialFractionA", 0.5f);
        Out.Molecular.FixedSubstepSec  = ReadFloatField(*T, "FixedSubstepSec", 0.004f);
    }

    if (const toml::table* T = ReadSubTable(Table, "Transition"))
    {
        Out.Transition.Mode              = static_cast<TransitionDerezMode>(ReadUintField(*T, "Mode", 0u));
        Out.Transition.HoldBaseSec       = ReadFloatField(*T, "HoldBaseSec", 0.2f);
        Out.Transition.HoldDurationSec   = ReadFloatField(*T, "HoldDurationSec", 1.2f);
        Out.Transition.BurstShare        = ReadFloatField(*T, "BurstShare", 0.5f);
        Out.Transition.FragmentOnFloor   = ReadBoolField(*T, "FragmentOnFloor", false);
        Out.Transition.ChildLifeSec      = ReadFloatField(*T, "ChildLifeSec", 0.0f);
        Out.Transition.WavePattern       = ReadUintField(*T, "WavePattern", 0u);
        Out.Transition.AssembleMode      = ReadUintField(*T, "AssembleMode", 0u);
        Out.Transition.ShellRadiusMeters = ReadFloatField(*T, "ShellRadiusMeters", 0.0f);
        Out.Transition.AfterTransform    = ReadUintField(*T, "AfterTransform", 0u);
        Out.Transition.GlitchRateHz      = ReadFloatField(*T, "GlitchRateHz", 14.0f);
    }

    if (const toml::table* T = ReadSubTable(Table, "LocalAttractor"))
    {
        Out.LocalAttractor.Enabled       = ReadBoolField(*T, "Enabled", false);
        Out.LocalAttractor.Strength      = ReadFloatField(*T, "Strength", 7.0f);
        Out.LocalAttractor.RadiusMeters  = ReadFloatField(*T, "RadiusMeters", 16.0f);
        Out.LocalAttractor.SwirlSpeed    = ReadFloatField(*T, "SwirlSpeed", 3.5f);
        Out.LocalAttractor.SwallowRadius = ReadFloatField(*T, "SwallowRadius", 0.45f);
    }

    if (const toml::table* T = ReadSubTable(Table, "HeatShimmer"))
    {
        Out.HeatShimmer.Enabled      = ReadBoolField(*T, "Enabled", false);
        Out.HeatShimmer.RadiusMeters = ReadFloatField(*T, "RadiusMeters", 1.6f);
        Out.HeatShimmer.Strength     = ReadFloatField(*T, "Strength", 0.03f);
        Out.HeatShimmer.SpatialFreq  = ReadFloatField(*T, "SpatialFreq", 14.0f);
        Out.HeatShimmer.DriftRate    = ReadFloatField(*T, "DriftRate", 1.5f);
    }

    if (const toml::table* T = ReadSubTable(Table, "BlackHole"))
    {
        Out.BlackHole.Enabled       = ReadBoolField(*T, "Enabled", false);
        Out.BlackHole.HorizonRadius = ReadFloatField(*T, "HorizonRadius", 0.45f);
        Out.BlackHole.EinsteinGain  = ReadFloatField(*T, "EinsteinGain", 3.0f);
    }

    if (const toml::table* T = ReadSubTable(Table, "Shield"))
    {
        Out.Shield.Active       = ReadBoolField(*T, "Active", false);
        Out.Shield.Form         = static_cast<ShieldFormCategory>(ReadUintField(*T, "Form", 0u));
        Out.Shield.MotifBitmask = ReadUintField(*T, "MotifBitmask", 1u);
        Out.Shield.SpinRate     = ReadFloatField(*T, "SpinRate", 0.20f);
        Out.Shield.GlitchRateHz = ReadFloatField(*T, "GlitchRateHz", 14.0f);
    }

    if (const toml::table* T = ReadSubTable(Table, "Pyro"))
    {
        Out.Pyro.FireworkMode   = ReadUintField(*T, "FireworkMode", 0u);
        Out.Pyro.FlareSalvoMode = ReadUintField(*T, "FlareSalvoMode", 0u);
        Out.Pyro.TrailLengthSec = ReadFloatField(*T, "TrailLengthSec", 0.0f);
        Out.Pyro.BeamModeActive = ReadFloatField(*T, "BeamModeActive", 0.0f);
    }
}

struct alignas(16) SerializedAnchorRecord
{
    uint32_t RigId          = 0u;
    float    PosX           = 0.0f;
    float    PosY           = 0.0f;
    float    PosZ           = 0.0f;
    float    NormalX        = 0.0f;
    float    NormalY        = 0.0f;
    float    NormalZ        = 1.0f;
    float    PitchMeters    = 0.08f;
};
static_assert(sizeof(SerializedAnchorRecord) == 32u);

struct alignas(16) SerializedStrandRestRecord
{
    uint32_t RigId             = 0u;
    uint32_t StrandIndex       = 0u;
    uint32_t VertexCount       = 0u;
    float    RestSegmentLength = 0.12f;
    float    RootX             = 0.0f;
    float    RootY             = 0.0f;
    float    RootZ             = 0.0f;
    uint32_t Flags             = 0u;
};
static_assert(sizeof(SerializedStrandRestRecord) == 32u);

template <typename T>
void AppendPodBytes(std::vector<std::byte>& Buffer, const T& Record)
{
    const auto* Ptr = reinterpret_cast<const std::byte*>(&Record);
    Buffer.insert(Buffer.end(), Ptr, Ptr + sizeof(T));
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PARTICLEENCODER
//------------------------------------------------------------------------------------------------------------------------

SpaceContainerPayload ParticleEncoder::BuildPayload(
    const ParticleDocument&            Document,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    SpaceContainerPayload Payload{};
    Payload.ExtensionCategory = SpaceExtensionCategory::ParticleSpace;
    Payload.DocumentTitle     = Document.DocumentTitle.empty() ? "ParticleStudio" : Document.DocumentTitle;
    Payload.SchemaVersion     = 1u;

    // [Document]
    {
        toml::table DocTable;
        DocTable.insert("Title", Payload.DocumentTitle);
        DocTable.insert("SchemaVersion", 1);
        DocTable.insert("CoordinateSystem", "RightHandZUp");
        DocTable.insert("ActivePresetId", Document.ActivePresetId);
        DocTable.insert("RandomSeed", static_cast<int64_t>(Document.RandomSeed));
        DocTable.insert("TimeScale", static_cast<double>(Document.TimeScale));
        DocTable.insert("PlaybackLoopSec", static_cast<double>(Document.PlaybackLoopSec));
        DocTable.insert("PlaybackPaused", Document.PlaybackPaused);
        DocTable.insert("LoopPlayback", Document.LoopPlayback);
        DocTable.insert("AmbientWindMps", PackAxis3D(Document.AmbientWindMps));
        Payload.DomainManifest.insert_or_assign("Document", std::move(DocTable));
    }

    // [InteractiveCollider]
    {
        toml::table ColTable;
        ColTable.insert("BallPositionMeters", PackAxis3D(Document.Collider.BallPositionMeters));
        ColTable.insert("BallVelocityMps", PackAxis3D(Document.Collider.BallVelocityMps));
        ColTable.insert("BallRadiusMeters", static_cast<double>(Document.Collider.BallRadiusMeters));
        ColTable.insert("ImpactImpulseScale", static_cast<double>(Document.Collider.ImpactImpulseScale));
        ColTable.insert("FloorElevationMeters", static_cast<double>(Document.Collider.FloorElevationMeters));
        ColTable.insert("SignedDistanceActive", Document.Collider.SignedDistanceActive);
        Payload.DomainManifest.insert_or_assign("InteractiveCollider", std::move(ColTable));
    }

#if FRONTIER_EDITOR_ENABLED
    {
        toml::table CamTable;
        CamTable.insert("EyePositionMeters", PackAxis3D(Document.ViewportCamera.EyePositionMeters));
        CamTable.insert("TargetMeters", PackAxis3D(Document.ViewportCamera.TargetMeters));
        CamTable.insert("FieldOfViewDeg", static_cast<double>(Document.ViewportCamera.FieldOfViewDeg));
        CamTable.insert("ShowFloorGrid", Document.ViewportCamera.ShowFloorGrid);
        CamTable.insert("ShowVectorOverlay", Document.ViewportCamera.ShowVectorOverlay);
        CamTable.insert("ShowMinimap", Document.ViewportCamera.ShowMinimap);
        Payload.DomainManifest.insert_or_assign("EditorViewport", std::move(CamTable));
    }
#endif

    // [[ParticleRig]] + Tier 2 binary tables
    toml::array            RigArray;
    std::vector<std::byte> ColourLutBytes;
    std::vector<std::byte> AnchorBytes;
    std::vector<std::byte> StrandRestBytes;

    for (const ParticleRig& Rig : Document.Rigs)
    {
        toml::table RigTable;
        RigTable.insert("RigId", static_cast<int64_t>(Rig.RigId));
        RigTable.insert("RigTitle", Rig.RigTitle);
        RigTable.insert("PresetOriginId", Rig.PresetOriginId);
        RigTable.insert("Enabled", Rig.Enabled);
        RigTable.insert("Solo", Rig.Solo);
        RigTable.insert("Quarantined", Rig.Quarantined);

        toml::table SizingTable;
        SizingTable.insert("CapacitySlots", static_cast<int64_t>(Rig.Sizing.CapacitySlots));
        SizingTable.insert("ReleaseRatePerSec", static_cast<double>(Rig.Sizing.ReleaseRatePerSec));
        SizingTable.insert("LifetimeMinSec", static_cast<double>(Rig.Sizing.LifetimeMinSec));
        SizingTable.insert("LifetimeMaxSec", static_cast<double>(Rig.Sizing.LifetimeMaxSec));
        SizingTable.insert("SizeStartMeters", static_cast<double>(Rig.Sizing.SizeStartMeters));
        SizingTable.insert("SizeEndMeters", static_cast<double>(Rig.Sizing.SizeEndMeters));
        SizingTable.insert("OpacityPeak", static_cast<double>(Rig.Sizing.OpacityPeak));
        SizingTable.insert("AngularSpinRate", static_cast<double>(Rig.Sizing.AngularSpinRate));
        SizingTable.insert("TurbulenceAmp", static_cast<double>(Rig.Sizing.TurbulenceAmp));
        SizingTable.insert("TurbulenceFreq", static_cast<double>(Rig.Sizing.TurbulenceFreq));
        SizingTable.insert("SizeByVelocity", Rig.Sizing.SizeByVelocity);
        RigTable.insert("Sizing", std::move(SizingTable));

        RigTable.insert("Origin", SerializeOrigin(Rig.Origin));
        RigTable.insert("Carrier", SerializeCarrier(Rig.Carrier));

        toml::table ColourTable = ColourEncoder::SerializeSourceToManifest(Rig.TintBinding.SourceProfile);
        ColourTable.insert("ScalarChannel", std::string(QueryParticleScalarChannelToken(Rig.TintBinding.ScalarChannel)));
        RigTable.insert("Colour", std::move(ColourTable));

        RigTable.insert("Dynamics", SerializeDynamics(Rig.Dynamics));

        toml::table BackingTable;
        BackingTable.insert("FinishStyle", static_cast<int64_t>(Rig.BackingSurface.FinishStyle));
        BackingTable.insert("SurfacePreset", static_cast<int64_t>(Rig.BackingSurface.SurfacePreset));
        BackingTable.insert("SampleMode", static_cast<int64_t>(Rig.BackingSurface.SampleMode));
        BackingTable.insert("EulerDegrees", PackAxis3D(Rig.BackingSurface.EulerDegrees));
        BackingTable.insert("CustomAssetPath", Rig.BackingSurface.CustomAssetPath);
        BackingTable.insert("VehicleHullMode", Rig.BackingSurface.VehicleHullMode);
        RigTable.insert("BackingSurface", std::move(BackingTable));

        RigArray.push_back(std::move(RigTable));

        // Tier 2 .ColourLut
        const ColourSpace::ColourRampLut64 Lut = ColourSpace::PackColourRampLut64(Rig.TintBinding.SourceProfile);
        const auto* LutPtr = reinterpret_cast<const std::byte*>(Lut.Samples.data());
        ColourLutBytes.insert(ColourLutBytes.end(), LutPtr, LutPtr + sizeof(Lut.Samples));

        // Tier 2 .SurfaceAnchor
        for (const SurfaceAnchorPoint& Anchor : Rig.SampledAnchors.Anchors)
        {
            SerializedAnchorRecord Rec{};
            Rec.RigId       = Rig.RigId;
            Rec.PosX        = Anchor.PositionMeters.X;
            Rec.PosY        = Anchor.PositionMeters.Y;
            Rec.PosZ        = Anchor.PositionMeters.Z;
            Rec.NormalX     = Anchor.NormalAxis.X;
            Rec.NormalY     = Anchor.NormalAxis.Y;
            Rec.NormalZ     = Anchor.NormalAxis.Z;
            Rec.PitchMeters = Anchor.PitchMeters;
            AppendPodBytes(AnchorBytes, Rec);
        }

        // Tier 2 .StrandRest
        if (const auto* ClothOrigin = std::get_if<ClothGridOriginSpec>(&Rig.Origin))
        {
            for (const StructuralStrandAnchor& S : BuildClothGridAnchors(*ClothOrigin))
            {
                SerializedStrandRestRecord Rec{};
                Rec.RigId             = Rig.RigId;
                Rec.StrandIndex       = S.StrandIndex;
                Rec.VertexCount       = S.VertexCount;
                Rec.RestSegmentLength = S.RestSegmentLength;
                Rec.RootX             = S.RootPosition.X;
                Rec.RootY             = S.RootPosition.Y;
                Rec.RootZ             = S.RootPosition.Z;
                Rec.Flags             = (S.RootPinned ? 1u : 0u) | (S.TipPinned ? 2u : 0u);
                AppendPodBytes(StrandRestBytes, Rec);
            }
        }
    }
    Payload.DomainManifest.insert_or_assign("ParticleRig", std::move(RigArray));

    // Inline ForceSources delegated directly to VectorFieldCodec
    SerializeInlineForceSourcesToManifest(Document.ForceSources, Document.LinkedVectorFields, Payload.DomainManifest);

    if (!ColourLutBytes.empty())
    {
        BinarySectionBlock Block{};
        Block.SectionIdentifier = "ParticleColourLuts";
        Block.SubFormatTag      = ".ColourLut";
        Block.PayloadBytes      = std::move(ColourLutBytes);
        Block.Checksum64        = SpaceContainerEncoder::ComputeFnv1aChecksum64(Block.PayloadBytes);
        Payload.BinarySections.push_back(std::move(Block));
    }
    if (!AnchorBytes.empty())
    {
        BinarySectionBlock Block{};
        Block.SectionIdentifier = "ParticleSurfaceAnchors";
        Block.SubFormatTag      = ".SurfaceAnchor";
        Block.PayloadBytes      = std::move(AnchorBytes);
        Block.Checksum64        = SpaceContainerEncoder::ComputeFnv1aChecksum64(Block.PayloadBytes);
        Payload.BinarySections.push_back(std::move(Block));
    }
    if (!StrandRestBytes.empty())
    {
        BinarySectionBlock Block{};
        Block.SectionIdentifier = "ParticleStrandRestTables";
        Block.SubFormatTag      = ".StrandRest";
        Block.PayloadBytes      = std::move(StrandRestBytes);
        Block.Checksum64        = SpaceContainerEncoder::ComputeFnv1aChecksum64(Block.PayloadBytes);
        Payload.BinarySections.push_back(std::move(Block));
    }

    if (OptionalRevision != nullptr)
    {
        RevisionEncoder::AttachRevisionSections(*OptionalRevision, Payload);
    }

    return Payload;
}

bool ParticleEncoder::EncodeToFile(
    const std::filesystem::path&       TargetPath,
    const ParticleDocument&            Document,
    std::string&                       OutDiagnostic,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    const SpaceContainerPayload Payload = BuildPayload(Document, OptionalRevision);
    return SpaceContainerEncoder::EncodeToFile(TargetPath, Payload, OutDiagnostic);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PARTICLEDECODER
//------------------------------------------------------------------------------------------------------------------------

bool ParticleDecoder::ExtractDocument(
    const SpaceContainerPayload& Payload,
    ParticleDocument&            OutDocument,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalOutRevision)
{
    if (Payload.ExtensionCategory != SpaceExtensionCategory::ParticleSpace)
    {
        OutDiagnostic = "Payload is not a .ParticleSpace container";
        return false;
    }

    OutDocument = ParticleDocument{};
    if (const toml::table* DocTable = ReadSubTable(Payload.DomainManifest, "Document"))
    {
        OutDocument.DocumentTitle   = ReadStringField(*DocTable, "Title", Payload.DocumentTitle);
        OutDocument.ActivePresetId  = ReadStringField(*DocTable, "ActivePresetId", "sparks");
        OutDocument.RandomSeed      = ReadUintField(*DocTable, "RandomSeed", 1337u);
        OutDocument.TimeScale       = ReadFloatField(*DocTable, "TimeScale", 1.0f);
        OutDocument.PlaybackLoopSec = ReadFloatField(*DocTable, "PlaybackLoopSec", 12.0f);
        OutDocument.PlaybackPaused  = ReadBoolField(*DocTable, "PlaybackPaused", false);
        OutDocument.LoopPlayback    = ReadBoolField(*DocTable, "LoopPlayback", true);
        OutDocument.AmbientWindMps  = ReadAxis3DField(*DocTable, "AmbientWindMps", { 0.0f, 0.0f, 0.0f });
    }
    else
    {
        OutDocument.DocumentTitle = Payload.DocumentTitle;
    }

    if (const toml::table* ColTable = ReadSubTable(Payload.DomainManifest, "InteractiveCollider"))
    {
        OutDocument.Collider.BallPositionMeters   = ReadAxis3DField(*ColTable, "BallPositionMeters", { 0.0f, 0.0f, -10.0f });
        OutDocument.Collider.BallVelocityMps      = ReadAxis3DField(*ColTable, "BallVelocityMps", { 0.0f, 0.0f, 0.0f });
        OutDocument.Collider.BallRadiusMeters     = ReadFloatField(*ColTable, "BallRadiusMeters", 0.0f);
        OutDocument.Collider.ImpactImpulseScale   = ReadFloatField(*ColTable, "ImpactImpulseScale", 1.0f);
        OutDocument.Collider.FloorElevationMeters = ReadFloatField(*ColTable, "FloorElevationMeters", 0.0f);
        OutDocument.Collider.SignedDistanceActive = ReadBoolField(*ColTable, "SignedDistanceActive", false);
    }

#if FRONTIER_EDITOR_ENABLED
    if (const toml::table* CamTable = ReadSubTable(Payload.DomainManifest, "EditorViewport"))
    {
        OutDocument.ViewportCamera.EyePositionMeters = ReadAxis3DField(*CamTable, "EyePositionMeters", { 0.0f, -6.5f, 2.2f });
        OutDocument.ViewportCamera.TargetMeters      = ReadAxis3DField(*CamTable, "TargetMeters", { 0.0f, 0.0f, 1.0f });
        OutDocument.ViewportCamera.FieldOfViewDeg    = ReadFloatField(*CamTable, "FieldOfViewDeg", 48.0f);
        OutDocument.ViewportCamera.ShowFloorGrid     = ReadBoolField(*CamTable, "ShowFloorGrid", true);
        OutDocument.ViewportCamera.ShowVectorOverlay = ReadBoolField(*CamTable, "ShowVectorOverlay", false);
        OutDocument.ViewportCamera.ShowMinimap       = ReadBoolField(*CamTable, "ShowMinimap", false);
    }
#endif

    if (const toml::node* RigArrayNode = LookupCaseInsensitive(Payload.DomainManifest, "ParticleRig"))
    {
        if (const toml::array* RigArray = RigArrayNode->as_array())
        {
            for (const toml::node& Elem : *RigArray)
            {
                const toml::table* RigTable = Elem.as_table();
                if (RigTable == nullptr)
                {
                    continue;
                }
                ParticleRig Rig{};
                Rig.RigId          = ReadUintField(*RigTable, "RigId", 1u);
                Rig.RigTitle       = ReadStringField(*RigTable, "RigTitle", "Particle Rig");
                Rig.PresetOriginId = ReadStringField(*RigTable, "PresetOriginId", "");
                Rig.Enabled        = ReadBoolField(*RigTable, "Enabled", true);
                Rig.Solo           = ReadBoolField(*RigTable, "Solo", false);
                Rig.Quarantined    = ReadBoolField(*RigTable, "Quarantined", false);

                if (const toml::table* S = ReadSubTable(*RigTable, "Sizing"))
                {
                    Rig.Sizing.CapacitySlots     = ReadUintField(*S, "CapacitySlots", 4096u);
                    Rig.Sizing.ReleaseRatePerSec = ReadFloatField(*S, "ReleaseRatePerSec", 400.0f);
                    Rig.Sizing.LifetimeMinSec    = ReadFloatField(*S, "LifetimeMinSec", 1.2f);
                    Rig.Sizing.LifetimeMaxSec    = ReadFloatField(*S, "LifetimeMaxSec", 2.6f);
                    Rig.Sizing.SizeStartMeters   = ReadFloatField(*S, "SizeStartMeters", 0.05f);
                    Rig.Sizing.SizeEndMeters     = ReadFloatField(*S, "SizeEndMeters", 0.01f);
                    Rig.Sizing.OpacityPeak       = ReadFloatField(*S, "OpacityPeak", 1.0f);
                    Rig.Sizing.AngularSpinRate   = ReadFloatField(*S, "AngularSpinRate", 0.0f);
                    Rig.Sizing.TurbulenceAmp     = ReadFloatField(*S, "TurbulenceAmp", 0.35f);
                    Rig.Sizing.TurbulenceFreq    = ReadFloatField(*S, "TurbulenceFreq", 1.1f);
                    Rig.Sizing.SizeByVelocity    = ReadBoolField(*S, "SizeByVelocity", false);
                }

                if (const toml::table* O = ReadSubTable(*RigTable, "Origin"))
                {
                    Rig.Origin = DeserializeOrigin(*O);
                }
                if (const toml::table* C = ReadSubTable(*RigTable, "Carrier"))
                {
                    Rig.Carrier = DeserializeCarrier(*C);
                }
                if (const toml::table* Col = ReadSubTable(*RigTable, "Colour"))
                {
                    std::string ColDiag;
                    if (!ColourDecoder::ExtractSourceFromManifest(*Col, Rig.TintBinding.SourceProfile, ColDiag))
                    {
                        OutDiagnostic = "Failed decoding rig colour: " + ColDiag;
                        return false;
                    }
                    Rig.TintBinding.ScalarChannel =
                        ParseParticleScalarChannelToken(ReadStringField(*Col, "ScalarChannel", "NormalizedLifetime"));
                }
                if (const toml::table* D = ReadSubTable(*RigTable, "Dynamics"))
                {
                    DeserializeDynamics(*D, Rig.Dynamics);
                }
                if (const toml::table* B = ReadSubTable(*RigTable, "BackingSurface"))
                {
                    Rig.BackingSurface.FinishStyle     = static_cast<BackingFinishCategory>(ReadUintField(*B, "FinishStyle", 0u));
                    Rig.BackingSurface.SurfacePreset   = static_cast<SurfacePresetCategory>(ReadUintField(*B, "SurfacePreset", 0u));
                    Rig.BackingSurface.SampleMode      = static_cast<SurfaceSampleMode>(ReadUintField(*B, "SampleMode", 0u));
                    Rig.BackingSurface.EulerDegrees    = ReadAxis3DField(*B, "EulerDegrees", { 0.0f, 0.0f, 0.0f });
                    Rig.BackingSurface.CustomAssetPath = ReadStringField(*B, "CustomAssetPath", "");
                    Rig.BackingSurface.VehicleHullMode = ReadBoolField(*B, "VehicleHullMode", false);
                }

                OutDocument.Rigs.push_back(std::move(Rig));
            }
        }
    }

    DeserializeInlineForceSourcesFromManifest(Payload.DomainManifest, OutDocument.ForceSources, OutDocument.LinkedVectorFields);

    // Hydrate Tier 2 .ColourLut & .SurfaceAnchor
    for (const BinarySectionBlock& Section : Payload.BinarySections)
    {
        if (Section.SubFormatTag == ".ColourLut")
        {
            const std::size_t EntryBytes = sizeof(ColourSpace::ColourRampLut64);
            const std::size_t Count      = Section.PayloadBytes.size() / EntryBytes;
            for (std::size_t I = 0u; I < Count && I < OutDocument.Rigs.size(); ++I)
            {
                std::memcpy(
                    OutDocument.Rigs[I].BakedColourLut.Samples.data(),
                    Section.PayloadBytes.data() + I * EntryBytes,
                    EntryBytes);
            }
        }
        else if (Section.SubFormatTag == ".SurfaceAnchor")
        {
            const std::size_t Count = Section.PayloadBytes.size() / sizeof(SerializedAnchorRecord);
            for (std::size_t I = 0u; I < Count; ++I)
            {
                SerializedAnchorRecord Rec{};
                std::memcpy(&Rec, Section.PayloadBytes.data() + I * sizeof(SerializedAnchorRecord), sizeof(SerializedAnchorRecord));
                if (ParticleRig* Rig = OutDocument.FindRigMutable(Rec.RigId))
                {
                    SurfaceAnchorPoint Pt{};
                    Pt.PositionMeters = { Rec.PosX, Rec.PosY, Rec.PosZ };
                    Pt.NormalAxis     = { Rec.NormalX, Rec.NormalY, Rec.NormalZ };
                    Pt.PitchMeters    = Rec.PitchMeters;
                    Rig->SampledAnchors.Anchors.push_back(Pt);
                }
            }
        }
    }

    if (OptionalOutRevision != nullptr)
    {
        std::string RevDiag;
        (void)RevisionDecoder::ExtractRevisionSections(Payload, *OptionalOutRevision, RevDiag);
    }

    // Ensure BakedColourLut & OriginCaches are valid even if optional Tier 2 sections were omitted
    for (ParticleRig& Rig : OutDocument.Rigs)
    {
        if (Rig.SampledAnchors.Anchors.empty())
        {
            Rig.RebuildOriginCaches(OutDocument.RandomSeed);
        }
        if (Rig.BakedColourLut.Samples[0].Alpha == 0.0f && Rig.BakedColourLut.Samples[63].Alpha == 0.0f)
        {
            Rig.RebuildBakedColourLut();
        }
    }

    OutDiagnostic.clear();
    return true;
}

bool ParticleDecoder::DecodeFromFile(
    const std::filesystem::path& SourcePath,
    ParticleDocument&            OutDocument,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalOutRevision)
{
    SpaceContainerPayload Payload{};
    if (!SpaceContainerDecoder::DecodeFromFile(SourcePath, Payload, OutDiagnostic))
    {
        return false;
    }
    return ExtractDocument(Payload, OutDocument, OutDiagnostic, OptionalOutRevision);
}

} // namespace Frontier::Codecs
