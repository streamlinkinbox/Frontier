/*============================================================================================================================================
                                                     VECTORAUTHORINGDOCUMENT.CPP
============================================================================================================================================*/
// 🧩 TOML authoring codec for retained coverage sources, expressions, layer identity and masks; GPU pixels are never serialized.

#include "VectorAuthoringDocument.h"
#include <toml++/toml.hpp>
#include <sstream>
#include <type_traits>

namespace Frontier
{
namespace
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     STRICT OPTIONAL AUTHORING FIELDS
//------------------------------------------------------------------------------------------------------------------------

constexpr std::array<std::string_view, 9> ChannelCaptions{"colour", "height", "normal", "roughness", "metalness", "opacity", "occlusion", "transmission", "clearcoat"};

void ValidateFields(const toml::table &Source, std::initializer_list<std::string_view> Captions)
{
    for (const auto &[Caption, Entry] : Source)
    {
        (void)Entry;
        if (std::find(Captions.begin(), Captions.end(), Caption.str()) == Captions.end())
            throw std::runtime_error("Unknown native vector authoring field: " + std::string(Caption.str()));
    }
}

template<typename Quantity>
Quantity DecodeOptional(const toml::table &Source, std::string_view Caption, Quantity Fallback)
{
    if (!Source.contains(Caption)) return Fallback;
    const auto Field = Source[Caption];
    if constexpr (std::is_same_v<Quantity, bool>)
    {
        if (!Field.is_boolean()) throw std::runtime_error("Expected vector boolean.");
    }
    else if constexpr (std::is_integral_v<Quantity>)
    {
        if (!Field.is_integer()) throw std::runtime_error("Expected vector integer.");
    }
    const auto Decoded = Field.value<Quantity>();
    if (!Decoded) throw std::runtime_error("Invalid native vector field type: " + std::string(Caption));
    return *Decoded;
}

uint32_t DecodeEnclosureToken(const toml::table &Source)
{
    const auto Token = DecodeOptional<int64_t>(Source, "enclosure_token", 0);
    if (Token < 0 || Token > UINT32_MAX) throw std::runtime_error("Invalid vector enclosure token.");
    return uint32_t(Token);
}

toml::array EncodeCoordinates(const std::array<float, 4> &Coordinates)
{
    toml::array Result; for (auto Coordinate : Coordinates) Result.push_back(double(Coordinate)); return Result;
}
toml::table EncodeInstruction(const VectorCoverageInstruction &Instruction)
{
    toml::array Topology; for (auto Token : Instruction.Topology) Topology.push_back(int64_t(Token));
    toml::table Result{{"topology", std::move(Topology)}, {"bounds", EncodeCoordinates(Instruction.Bounds)},
        {"transform", EncodeCoordinates(Instruction.Transform)}, {"colour", EncodeCoordinates(Instruction.Colour)},
        {"colour_end", EncodeCoordinates(Instruction.ColourEnd)}, {"paint", EncodeCoordinates(Instruction.Paint)}};
    if (Instruction.Topology[0] <= 6 &&
        ((Instruction.Topology[3] & VectorCoverageStrokeColourExplicit) != 0 || Instruction.StrokeColour != Instruction.Colour))
        Result.insert("stroke_colour", EncodeCoordinates(Instruction.StrokeColour));
    // 📝 Only a gradient fill (Transform.w != 0) ever reads these, so a plain solid-colour shape's saved
    // file stays exactly as before - but when it IS a gradient, every one of its up-to-4 colour stops and
    // its own on-canvas axis/radius handles (see VectorCoverage.h's GradientAxis) must round-trip, or a
    // reopened document would silently collapse back to a plain 2-stop, box-cornered ramp.
    if (Instruction.Topology[0] <= 6 && Instruction.Transform[3] != 0)
    {
        Result.insert("gradient_stop_positions", EncodeCoordinates(Instruction.GradientStopPositions));
        Result.insert("gradient_stop_colour2", EncodeCoordinates(Instruction.GradientStopColour2));
        Result.insert("gradient_stop_colour3", EncodeCoordinates(Instruction.GradientStopColour3));
        Result.insert("gradient_axis", EncodeCoordinates(Instruction.GradientAxis));
    }
    if (Instruction.Topology[0] <= 6 && Instruction.ShapeParameters != std::array<float, 4>{})
        Result.insert("shape_parameters", EncodeCoordinates(Instruction.ShapeParameters));
    if (Instruction.Topology[0] <= 6 && (Instruction.ShapeParameters[1] != 0 ||
        Instruction.StrokeColourEnd != std::array<float, 4>{1, 1, 1, 1} ||
        Instruction.StrokeGradientStopPositions != std::array<float, 4>{0, 1, -1, -1} ||
        Instruction.StrokeGradientStopColour2 != std::array<float, 4>{1, 1, 1, 1} ||
        Instruction.StrokeGradientStopColour3 != std::array<float, 4>{1, 1, 1, 1} ||
        Instruction.StrokeGradientAxis != std::array<float, 4>{-.5f, 0, .5f, 0}))
    {
        Result.insert("stroke_colour_end", EncodeCoordinates(Instruction.StrokeColourEnd));
        Result.insert("stroke_gradient_stop_positions", EncodeCoordinates(Instruction.StrokeGradientStopPositions));
        Result.insert("stroke_gradient_stop_colour2", EncodeCoordinates(Instruction.StrokeGradientStopColour2));
        Result.insert("stroke_gradient_stop_colour3", EncodeCoordinates(Instruction.StrokeGradientStopColour3));
        Result.insert("stroke_gradient_axis", EncodeCoordinates(Instruction.StrokeGradientAxis));
    }
    const VectorCoverageInstruction DefaultInstruction{};
    if (Instruction.Topology[0] <= 6 && (Instruction.Transform[3] == 3 ||
        Instruction.FreeformPointPositions01 != DefaultInstruction.FreeformPointPositions01 ||
        Instruction.FreeformPointPositions23 != DefaultInstruction.FreeformPointPositions23 ||
        Instruction.FreeformPointPositions45 != DefaultInstruction.FreeformPointPositions45 ||
        Instruction.FreeformPointPositions67 != DefaultInstruction.FreeformPointPositions67 ||
        Instruction.FreeformPointColour4 != DefaultInstruction.FreeformPointColour4 ||
        Instruction.FreeformPointColour5 != DefaultInstruction.FreeformPointColour5 ||
        Instruction.FreeformPointColour6 != DefaultInstruction.FreeformPointColour6 ||
        Instruction.FreeformPointColour7 != DefaultInstruction.FreeformPointColour7 ||
        Instruction.FreeformParameters != DefaultInstruction.FreeformParameters))
    {
        Result.insert("freeform_point_positions_01", EncodeCoordinates(Instruction.FreeformPointPositions01));
        Result.insert("freeform_point_positions_23", EncodeCoordinates(Instruction.FreeformPointPositions23));
        Result.insert("freeform_point_positions_45", EncodeCoordinates(Instruction.FreeformPointPositions45));
        Result.insert("freeform_point_positions_67", EncodeCoordinates(Instruction.FreeformPointPositions67));
        Result.insert("freeform_point_colour4", EncodeCoordinates(Instruction.FreeformPointColour4));
        Result.insert("freeform_point_colour5", EncodeCoordinates(Instruction.FreeformPointColour5));
        Result.insert("freeform_point_colour6", EncodeCoordinates(Instruction.FreeformPointColour6));
        Result.insert("freeform_point_colour7", EncodeCoordinates(Instruction.FreeformPointColour7));
        Result.insert("freeform_parameters", EncodeCoordinates(Instruction.FreeformParameters));
    }
    return Result;
}
std::array<float, 4> DecodeCoordinates(const toml::node_view<const toml::node> &Field)
{
    const auto *Sequence = Field.as_array(); if (!Sequence || Sequence->size() != 4) throw std::runtime_error("Expected four vector coordinates.");
    std::array<float, 4> Result{};
    for (size_t Index = 0; Index < 4; ++Index)
    {
        const auto Coordinate = (*Sequence)[Index].value<double>();
        if (!Coordinate || !std::isfinite(*Coordinate) || std::abs(*Coordinate) > 1e9) throw std::runtime_error("Invalid vector coordinate.");
        Result[Index] = float(*Coordinate);
    }
    return Result;
}
toml::table EncodeArraySpecification(const VectorAuthoringArraySpecification &Specification)
{
    constexpr const char *CategoryNames[]{"linear", "grid", "circular", "radial"};
    constexpr const char *ProfileNames[]{"uniform", "linear", "circular", "radial", "noise"};
    return toml::table{
        {"category", CategoryNames[uint32_t(Specification.Category)]},
        {"scale_profile", ProfileNames[uint32_t(Specification.ScaleProfile)]},
        {"count", int64_t(Specification.Count)}, {"columns", int64_t(Specification.Columns)}, {"rows", int64_t(Specification.Rows)},
        {"rings", int64_t(Specification.Rings)}, {"seed", int64_t(Specification.Seed)},
        {"start_scale", double(Specification.StartScale)}, {"end_scale", double(Specification.EndScale)},
        {"step_x", double(Specification.StepX)}, {"step_y", double(Specification.StepY)},
        {"centre_x", double(Specification.CentreX)}, {"centre_y", double(Specification.CentreY)},
        {"radius", double(Specification.Radius)}, {"radius_step", double(Specification.RadiusStep)},
        {"start_angle", double(Specification.StartAngle)}, {"sweep", double(Specification.Sweep)},
        {"rotate_copies", Specification.RotateCopies}};
}
VectorAuthoringArraySpecification DecodeArraySpecification(const toml::table &Source)
{
    ValidateFields(Source, {"category", "scale_profile", "count", "columns", "rows", "rings", "seed",
        "start_scale", "end_scale", "step_x", "step_y", "centre_x", "centre_y", "radius", "radius_step", "start_angle", "sweep", "rotate_copies"});
    VectorAuthoringArraySpecification Specification;
    const auto Category = DecodeOptional<std::string>(Source, "category", "linear");
    if (Category == "linear") Specification.Category = VectorAuthoringArrayCategory::Linear;
    else if (Category == "grid") Specification.Category = VectorAuthoringArrayCategory::Grid;
    else if (Category == "circular") Specification.Category = VectorAuthoringArrayCategory::Circular;
    else if (Category == "radial") Specification.Category = VectorAuthoringArrayCategory::Radial;
    else throw std::runtime_error("Unknown native array modifier category.");
    const auto Profile = DecodeOptional<std::string>(Source, "scale_profile", "uniform");
    if (Profile == "uniform") Specification.ScaleProfile = VectorAuthoringScaleProfile::Uniform;
    else if (Profile == "linear") Specification.ScaleProfile = VectorAuthoringScaleProfile::Linear;
    else if (Profile == "circular") Specification.ScaleProfile = VectorAuthoringScaleProfile::Circular;
    else if (Profile == "radial") Specification.ScaleProfile = VectorAuthoringScaleProfile::Radial;
    else if (Profile == "noise") Specification.ScaleProfile = VectorAuthoringScaleProfile::Noise;
    else throw std::runtime_error("Unknown native array modifier scale profile.");
    const auto Unsigned = [&](std::string_view Caption, uint32_t Fallback) -> uint32_t
    {
        const auto Decoded = DecodeOptional<int64_t>(Source, Caption, Fallback);
        if (Decoded < 0 || Decoded > UINT32_MAX) throw std::runtime_error("Invalid native array modifier count.");
        return uint32_t(Decoded);
    };
    Specification.Count = Unsigned("count", Specification.Count);
    Specification.Columns = Unsigned("columns", Specification.Columns);
    Specification.Rows = Unsigned("rows", Specification.Rows);
    Specification.Rings = Unsigned("rings", Specification.Rings);
    Specification.Seed = Unsigned("seed", Specification.Seed);
    const auto Finite = [&](std::string_view Caption, float Fallback) -> float
    {
        const auto Decoded = float(DecodeOptional<double>(Source, Caption, double(Fallback)));
        if (!std::isfinite(Decoded) || std::abs(Decoded) > 1e9f) throw std::runtime_error("Invalid native array modifier coordinate.");
        return Decoded;
    };
    Specification.StartScale = Finite("start_scale", Specification.StartScale);
    Specification.EndScale = Finite("end_scale", Specification.EndScale);
    Specification.StepX = Finite("step_x", Specification.StepX);
    Specification.StepY = Finite("step_y", Specification.StepY);
    Specification.CentreX = Finite("centre_x", Specification.CentreX);
    Specification.CentreY = Finite("centre_y", Specification.CentreY);
    Specification.Radius = Finite("radius", Specification.Radius);
    Specification.RadiusStep = Finite("radius_step", Specification.RadiusStep);
    Specification.StartAngle = Finite("start_angle", Specification.StartAngle);
    Specification.Sweep = Finite("sweep", Specification.Sweep);
    Specification.RotateCopies = DecodeOptional<bool>(Source, "rotate_copies", Specification.RotateCopies);
    return Specification;
}
float DecodeFiniteField(const toml::table &Source, std::string_view Caption, float Fallback, std::string_view Grievance)
{
    const auto Decoded = float(DecodeOptional<double>(Source, Caption, double(Fallback)));
    if (!std::isfinite(Decoded) || std::abs(Decoded) > 1e9f) throw std::runtime_error(std::string(Grievance));
    return Decoded;
}
toml::table EncodeMirrorSpecification(const VectorAuthoringMirrorSpecification &Specification)
{
    return toml::table{{"pivot_x", double(Specification.PivotX)}, {"pivot_y", double(Specification.PivotY)},
        {"axis_angle", double(Specification.AxisAngle)}, {"keep_original", Specification.KeepOriginal}};
}
VectorAuthoringMirrorSpecification DecodeMirrorSpecification(const toml::table &Source)
{
    ValidateFields(Source, {"pivot_x", "pivot_y", "axis_angle", "keep_original"});
    VectorAuthoringMirrorSpecification Specification;
    Specification.PivotX = DecodeFiniteField(Source, "pivot_x", Specification.PivotX, "Invalid native mirror modifier pivot.");
    Specification.PivotY = DecodeFiniteField(Source, "pivot_y", Specification.PivotY, "Invalid native mirror modifier pivot.");
    Specification.AxisAngle = DecodeFiniteField(Source, "axis_angle", Specification.AxisAngle, "Invalid native mirror modifier axis angle.");
    Specification.KeepOriginal = DecodeOptional<bool>(Source, "keep_original", Specification.KeepOriginal);
    return Specification;
}
toml::table EncodeNoiseSpecification(const VectorAuthoringNoiseSpecification &Specification)
{
    return toml::table{{"position_jitter", double(Specification.PositionJitter)}, {"rotation_jitter", double(Specification.RotationJitter)},
        {"scale_jitter", double(Specification.ScaleJitter)}, {"seed", int64_t(Specification.Seed)}};
}
VectorAuthoringNoiseSpecification DecodeNoiseSpecification(const toml::table &Source)
{
    ValidateFields(Source, {"position_jitter", "rotation_jitter", "scale_jitter", "seed"});
    VectorAuthoringNoiseSpecification Specification;
    Specification.PositionJitter = DecodeFiniteField(Source, "position_jitter", Specification.PositionJitter, "Invalid native noise modifier jitter.");
    Specification.RotationJitter = DecodeFiniteField(Source, "rotation_jitter", Specification.RotationJitter, "Invalid native noise modifier jitter.");
    Specification.ScaleJitter = DecodeFiniteField(Source, "scale_jitter", Specification.ScaleJitter, "Invalid native noise modifier jitter.");
    const auto Seed = DecodeOptional<int64_t>(Source, "seed", Specification.Seed);
    if (Seed < 0 || Seed > UINT32_MAX) throw std::runtime_error("Invalid native noise modifier seed.");
    Specification.Seed = uint32_t(Seed);
    return Specification;
}
toml::table EncodeOffsetSpecification(const VectorAuthoringOffsetSpecification &Specification)
{
    return toml::table{{"distance", double(Specification.Distance)}};
}
VectorAuthoringOffsetSpecification DecodeOffsetSpecification(const toml::table &Source)
{
    ValidateFields(Source, {"distance"});
    VectorAuthoringOffsetSpecification Specification;
    Specification.Distance = DecodeFiniteField(Source, "distance", Specification.Distance, "Invalid native offset modifier distance.");
    return Specification;
}
toml::table EncodeColourSpecification(const VectorAuthoringColourSpecification &Specification)
{
    constexpr const char *GeneratorNames[]{"linear", "radial", "ramp"};
    constexpr const char *TargetNames[]{"fill", "stroke", "both"};
    return toml::table{
        {"generator", GeneratorNames[uint32_t(Specification.Generator)]},
        {"target", TargetNames[uint32_t(Specification.Target)]},
        {"axis_angle", double(Specification.AxisAngle)},
        {"centre_x", double(Specification.CentreX)}, {"centre_y", double(Specification.CentreY)}, {"radius", double(Specification.Radius)},
        {"stop_count", int64_t(Specification.StopCount)},
        {"stop_1", EncodeCoordinates(Specification.Stops[0])}, {"stop_2", EncodeCoordinates(Specification.Stops[1])},
        {"stop_3", EncodeCoordinates(Specification.Stops[2])}, {"stop_4", EncodeCoordinates(Specification.Stops[3])},
        {"auto_group", Specification.AutoGroup}, {"group_size", int64_t(Specification.GroupSize)}};
}
VectorAuthoringColourSpecification DecodeColourSpecification(const toml::table &Source)
{
    ValidateFields(Source, {"generator", "target", "axis_angle", "centre_x", "centre_y", "radius", "stop_count",
        "stop_1", "stop_2", "stop_3", "stop_4", "auto_group", "group_size"});
    VectorAuthoringColourSpecification Specification;
    const auto Generator = DecodeOptional<std::string>(Source, "generator", "ramp");
    if (Generator == "linear") Specification.Generator = VectorAuthoringColourGenerator::Linear;
    else if (Generator == "radial") Specification.Generator = VectorAuthoringColourGenerator::Radial;
    else if (Generator == "ramp") Specification.Generator = VectorAuthoringColourGenerator::Ramp;
    else throw std::runtime_error("Unknown native colour modifier generator.");
    const auto Target = DecodeOptional<std::string>(Source, "target", "fill");
    if (Target == "fill") Specification.Target = VectorAuthoringColourTarget::Fill;
    else if (Target == "stroke") Specification.Target = VectorAuthoringColourTarget::Stroke;
    else if (Target == "both") Specification.Target = VectorAuthoringColourTarget::Both;
    else throw std::runtime_error("Unknown native colour modifier target.");
    Specification.AxisAngle = DecodeFiniteField(Source, "axis_angle", Specification.AxisAngle, "Invalid native colour modifier axis angle.");
    Specification.CentreX = DecodeFiniteField(Source, "centre_x", Specification.CentreX, "Invalid native colour modifier centre.");
    Specification.CentreY = DecodeFiniteField(Source, "centre_y", Specification.CentreY, "Invalid native colour modifier centre.");
    Specification.Radius = DecodeFiniteField(Source, "radius", Specification.Radius, "Invalid native colour modifier radius.");
    const auto StopCount = DecodeOptional<int64_t>(Source, "stop_count", 2);
    if (StopCount < 2 || StopCount > 4) throw std::runtime_error("Native colour modifier stop count must be 2-4.");
    Specification.StopCount = uint32_t(StopCount);
    Specification.Stops[0] = DecodeCoordinates(Source["stop_1"]); Specification.Stops[1] = DecodeCoordinates(Source["stop_2"]);
    Specification.Stops[2] = DecodeCoordinates(Source["stop_3"]); Specification.Stops[3] = DecodeCoordinates(Source["stop_4"]);
    for (const auto &Stop : Specification.Stops)
        for (float Channel : Stop) if (Channel < 0 || Channel > 1) throw std::runtime_error("Native colour modifier stop channel must be normalized.");
    Specification.AutoGroup = DecodeOptional<bool>(Source, "auto_group", Specification.AutoGroup);
    const auto GroupSize = DecodeOptional<int64_t>(Source, "group_size", Specification.GroupSize);
    if (GroupSize < 1 || GroupSize > 64) throw std::runtime_error("Native colour modifier group size must be 1-64.");
    Specification.GroupSize = uint32_t(GroupSize);
    return Specification;
}
toml::table EncodeArrayModifier(const VectorAuthoringModifier &Modifier)
{
    constexpr const char *OperationNames[]{"array", "mirror", "noise", "offset", "colour"};
    toml::table Result{{"enabled", Modifier.Enabled}, {"operation", OperationNames[uint32_t(Modifier.Operation)]}};
    switch (Modifier.Operation)
    {
    case VectorAuthoringModifierOperation::Array:  Result.insert("array", EncodeArraySpecification(Modifier.Array)); break;
    case VectorAuthoringModifierOperation::Mirror: Result.insert("mirror", EncodeMirrorSpecification(Modifier.Mirror)); break;
    case VectorAuthoringModifierOperation::Noise:  Result.insert("noise", EncodeNoiseSpecification(Modifier.Noise)); break;
    case VectorAuthoringModifierOperation::Offset: Result.insert("offset", EncodeOffsetSpecification(Modifier.Offset)); break;
    case VectorAuthoringModifierOperation::Colour: Result.insert("colour", EncodeColourSpecification(Modifier.Colour)); break;
    }
    return Result;
}
VectorAuthoringModifier DecodeArrayModifier(const toml::table &Source)
{
    ValidateFields(Source, {"enabled", "operation", "array", "mirror", "noise", "offset", "colour"});
    VectorAuthoringModifier Modifier;
    Modifier.Enabled = DecodeOptional<bool>(Source, "enabled", true);
    const auto Operation = DecodeOptional<std::string>(Source, "operation", "array");
    if (Operation == "array")
    {
        Modifier.Operation = VectorAuthoringModifierOperation::Array;
        const auto *Specification = Source["array"].as_table();
        if (!Specification) throw std::runtime_error("Expected native array modifier specification.");
        Modifier.Array = DecodeArraySpecification(*Specification);
    }
    else if (Operation == "mirror")
    {
        Modifier.Operation = VectorAuthoringModifierOperation::Mirror;
        const auto *Specification = Source["mirror"].as_table();
        if (!Specification) throw std::runtime_error("Expected native mirror modifier specification.");
        Modifier.Mirror = DecodeMirrorSpecification(*Specification);
    }
    else if (Operation == "noise")
    {
        Modifier.Operation = VectorAuthoringModifierOperation::Noise;
        const auto *Specification = Source["noise"].as_table();
        if (!Specification) throw std::runtime_error("Expected native noise modifier specification.");
        Modifier.Noise = DecodeNoiseSpecification(*Specification);
    }
    else if (Operation == "offset")
    {
        Modifier.Operation = VectorAuthoringModifierOperation::Offset;
        const auto *Specification = Source["offset"].as_table();
        if (!Specification) throw std::runtime_error("Expected native offset modifier specification.");
        Modifier.Offset = DecodeOffsetSpecification(*Specification);
    }
    else if (Operation == "colour")
    {
        Modifier.Operation = VectorAuthoringModifierOperation::Colour;
        const auto *Specification = Source["colour"].as_table();
        if (!Specification) throw std::runtime_error("Expected native colour modifier specification.");
        Modifier.Colour = DecodeColourSpecification(*Specification);
    }
    else throw std::runtime_error("Unknown native vector modifier operation.");
    return Modifier;
}
VectorCoverageInstruction DecodeInstruction(const toml::table &Source)
{
    ValidateFields(Source, {"topology", "bounds", "transform", "colour", "colour_end", "stroke_colour", "paint",
        "gradient_stop_positions", "gradient_stop_colour2", "gradient_stop_colour3", "gradient_axis", "shape_parameters",
        "stroke_colour_end", "stroke_gradient_stop_positions", "stroke_gradient_stop_colour2", "stroke_gradient_stop_colour3", "stroke_gradient_axis",
        "freeform_point_positions_01", "freeform_point_positions_23", "freeform_point_positions_45", "freeform_point_positions_67",
        "freeform_point_colour4", "freeform_point_colour5", "freeform_point_colour6", "freeform_point_colour7", "freeform_parameters"});
    VectorCoverageInstruction Result;
    const auto *Topology = Source["topology"].as_array(); if (!Topology || Topology->size() != 4) throw std::runtime_error("Expected four vector topology tokens.");
    for (size_t Index = 0; Index < 4; ++Index)
    {
        if (!(*Topology)[Index].is_integer()) throw std::runtime_error("Expected integer vector topology token.");
        auto Token = (*Topology)[Index].value<int64_t>();
        if (!Token || *Token < 0 || *Token > UINT32_MAX) throw std::runtime_error("Invalid vector topology token.");
        Result.Topology[Index] = uint32_t(*Token);
    }
    Result.Bounds = DecodeCoordinates(Source["bounds"]); Result.Transform = DecodeCoordinates(Source["transform"]);
    Result.Colour = DecodeCoordinates(Source["colour"]); Result.ColourEnd = DecodeCoordinates(Source["colour_end"]);
    if (Source.contains("stroke_colour"))
    {

        Result.StrokeColour = DecodeCoordinates(Source["stroke_colour"]);
        if (Result.Topology[0] <= 6) Result.Topology[3] |= VectorCoverageStrokeColourExplicit;
    }
    else Result.StrokeColour = Result.Colour;
    Result.Paint = DecodeCoordinates(Source["paint"]);
    // 📝 Older documents saved before multi-stop/free-placed gradients existed simply won't have these
    // keys - fall back to this struct's own (linear) defaults, or the radial-correct equivalent for a
    // gradient that was already radial, so an old 2-stop file re-opens looking exactly as it did before.
    const bool Radial = Result.Transform[3] == 2;
    Result.GradientStopPositions = Source.contains("gradient_stop_positions") ? DecodeCoordinates(Source["gradient_stop_positions"]) : std::array<float, 4>{0.f, 1.f, -1.f, -1.f};
    Result.GradientStopColour2 = Source.contains("gradient_stop_colour2") ? DecodeCoordinates(Source["gradient_stop_colour2"]) : std::array<float, 4>{1, 1, 1, 1};
    Result.GradientStopColour3 = Source.contains("gradient_stop_colour3") ? DecodeCoordinates(Source["gradient_stop_colour3"]) : std::array<float, 4>{1, 1, 1, 1};
    Result.GradientAxis = Source.contains("gradient_axis") ? DecodeCoordinates(Source["gradient_axis"]) : (Radial ? std::array<float, 4>{0.f, 0.f, .5f, 0.f} : std::array<float, 4>{-.5f, 0.f, .5f, 0.f});
    if (Source.contains("shape_parameters")) Result.ShapeParameters = DecodeCoordinates(Source["shape_parameters"]);
    if (Source.contains("stroke_colour_end")) Result.StrokeColourEnd = DecodeCoordinates(Source["stroke_colour_end"]);
    if (Source.contains("stroke_gradient_stop_positions")) Result.StrokeGradientStopPositions = DecodeCoordinates(Source["stroke_gradient_stop_positions"]);
    if (Source.contains("stroke_gradient_stop_colour2")) Result.StrokeGradientStopColour2 = DecodeCoordinates(Source["stroke_gradient_stop_colour2"]);
    if (Source.contains("stroke_gradient_stop_colour3")) Result.StrokeGradientStopColour3 = DecodeCoordinates(Source["stroke_gradient_stop_colour3"]);
    if (Source.contains("stroke_gradient_axis")) Result.StrokeGradientAxis = DecodeCoordinates(Source["stroke_gradient_axis"]);
    if (Source.contains("freeform_point_positions_01")) Result.FreeformPointPositions01 = DecodeCoordinates(Source["freeform_point_positions_01"]);
    if (Source.contains("freeform_point_positions_23")) Result.FreeformPointPositions23 = DecodeCoordinates(Source["freeform_point_positions_23"]);
    if (Source.contains("freeform_point_positions_45")) Result.FreeformPointPositions45 = DecodeCoordinates(Source["freeform_point_positions_45"]);
    if (Source.contains("freeform_point_positions_67")) Result.FreeformPointPositions67 = DecodeCoordinates(Source["freeform_point_positions_67"]);
    if (Source.contains("freeform_point_colour4")) Result.FreeformPointColour4 = DecodeCoordinates(Source["freeform_point_colour4"]);
    if (Source.contains("freeform_point_colour5")) Result.FreeformPointColour5 = DecodeCoordinates(Source["freeform_point_colour5"]);
    if (Source.contains("freeform_point_colour6")) Result.FreeformPointColour6 = DecodeCoordinates(Source["freeform_point_colour6"]);
    if (Source.contains("freeform_point_colour7")) Result.FreeformPointColour7 = DecodeCoordinates(Source["freeform_point_colour7"]);
    if (Source.contains("freeform_parameters")) Result.FreeformParameters = DecodeCoordinates(Source["freeform_parameters"]);
    return Result;
}
}
std::string EncodeVectorAuthoring(const VectorAuthoringDocument &Document)
{
    (void)ConstructVectorAuthoringCoverage(Document);
    toml::array Instructions, Segments, Layers, Enclosures;
    for (const auto &Instruction : Document.Instructions) Instructions.push_back(EncodeInstruction(Instruction));
    for (const auto &Segment : Document.Segments) Segments.push_back(toml::table{{"start_control", EncodeCoordinates(Segment.StartControl)}, {"control_end", EncodeCoordinates(Segment.ControlEnd)}});
    for (const auto &Layer : Document.Layers)
    {
        toml::table Encoded{{"token", int64_t(Layer.Token)}, {"root", int64_t(Layer.Root)}, {"name", Layer.Name}, {"visible", Layer.Visible}, {"locked", Layer.Locked}, {"mask_enabled", Layer.MaskEnabled}, {"mask_inverted", Layer.MaskInverted}, {"mask_luminance", Layer.MaskLuminance}};
        Encoded.insert("enclosure_token", int64_t(Layer.EnclosureToken));
        Encoded.insert("surface_relief", double(Layer.SurfaceRelief));
        if (Layer.ExpressionOpacity != 1.f) Encoded.insert("expression_opacity", double(Layer.ExpressionOpacity));
        const char *Structure = Layer.SurfaceStructure == VectorAuthoringSurfaceStructure::Stitch ? "stitch" : Layer.SurfaceStructure == VectorAuthoringSurfaceStructure::Weave ? "weave" : "freeform";
        Encoded.insert("surface_structure", Structure);
        constexpr const char *TextileGeometryNames[]{"unclassified", "needle_entry", "stitch_thread", "upper_thread_pass", "weave_yarn"};
        Encoded.insert("textile_geometry", TextileGeometryNames[uint32_t(Layer.TextileGeometry)]);
        if (Layer.TextileGeometry == VectorAuthoringTextileGeometry::NeedleEntry) Encoded.insert("stitch_role", "holes");
        else if (Layer.TextileGeometry == VectorAuthoringTextileGeometry::StitchThread) Encoded.insert("stitch_role", "thread");
        else if (Layer.TextileGeometry == VectorAuthoringTextileGeometry::UpperThreadPass) Encoded.insert("stitch_role", "highlight");
        else if (Layer.TextileGeometry == VectorAuthoringTextileGeometry::WeaveYarn) Encoded.insert("weave_role", "yarn");
        toml::table Channels;
        for (size_t Index = 0; Index < ChannelCaptions.size(); ++Index)
            Channels.insert(ChannelCaptions[Index], toml::table{{"enabled", Layer.SurfaceChannels[Index].Enabled}, {"strength", double(Layer.SurfaceChannels[Index].Strength)}});
        Encoded.insert("surface_channels", std::move(Channels));
        if (Layer.Mask) Encoded.insert("mask", EncodeInstruction(*Layer.Mask));
        if (Layer.ComponentKind != VectorAuthoringComponentKind::Freeform)
        {
            constexpr const char *ComponentKindNames[]{"freeform", "text", "imported_vector", "imported_image"};
            Encoded.insert("component_kind", ComponentKindNames[uint32_t(Layer.ComponentKind)]);
            if (!Layer.SourceMarkup.empty()) Encoded.insert("source_markup", Layer.SourceMarkup);
            if (!Layer.EmbeddedImage.empty()) Encoded.insert("embedded_image", Layer.EmbeddedImage);
            if (Layer.ComponentKind == VectorAuthoringComponentKind::Text) Encoded.insert("text_point_size", double(Layer.TextPointSize));
            if (Layer.ComponentKind == VectorAuthoringComponentKind::Text && !Layer.TextTypeface.empty()) Encoded.insert("text_typeface", Layer.TextTypeface);
        }
        if (!Layer.Modifiers.empty())
        {
            toml::array Modifiers;
            for (const auto &Modifier : Layer.Modifiers) Modifiers.push_back(EncodeArrayModifier(Modifier));
            Encoded.insert("modifiers", std::move(Modifiers));
        }
        Layers.push_back(std::move(Encoded));
    }
    for (const auto &Entry : Document.Enclosures)
        Enclosures.push_back(toml::table{{"token", int64_t(Entry.Token)}, {"enclosure_token", int64_t(Entry.EnclosureToken)}, {"name", Entry.Name}, {"folder_enabled", Entry.FolderEnabled}, {"visible", Entry.VisibilityEnabled}, {"locked", Entry.LockEnabled}, {"group_wide_editing", Entry.GroupWideEditingEnabled}});
    toml::table Encoded{{"format", "frontier.vector_artwork"}, {"version", 1}, {"name", Document.Name}, {"stamp", Document.Stamp}, {"token_clock", int64_t(Document.TokenClock)}, {"instructions", std::move(Instructions)}, {"segments", std::move(Segments)}, {"layers", std::move(Layers)}, {"enclosures", std::move(Enclosures)}};
    if (Document.CanvasExtent != 512.f) Encoded.insert("canvas_extent", double(Document.CanvasExtent));
    std::ostringstream Output; Output << Encoded;
    if (Output.str().size() > 4000000) throw std::runtime_error("Native vector authoring source exceeds 4 MB.");
    return Output.str();
}
VectorAuthoringDocument DecodeVectorAuthoring(std::string_view Source)
{
    if (Source.size() > 4000000) throw std::runtime_error("Native vector source exceeds 4 MB.");
    const auto Table = toml::parse(Source);
    if (!Table["format"].is_string() || !Table["version"].is_integer()) throw std::runtime_error("Expected typed native vector format and version.");
    if (Table["format"].value_or("") != std::string("frontier.vector_artwork") || Table["version"].value_or(0) != 1) throw std::runtime_error("Unsupported native vector format.");
    ValidateFields(Table, {"format", "version", "name", "stamp", "canvas_extent", "token_clock", "instructions", "segments", "layers", "enclosures"});
    VectorAuthoringDocument Document;
    Document.Name = DecodeOptional<std::string>(Table, "name", "Untitled vector");
    Document.Stamp = DecodeOptional<bool>(Table, "stamp", false);
    Document.CanvasExtent = float(DecodeOptional<double>(Table, "canvas_extent", 512.0));
    const auto Clock = Table["token_clock"].is_integer() ? Table["token_clock"].value<int64_t>() : std::nullopt; if (!Clock || *Clock < 0 || *Clock > UINT32_MAX) throw std::runtime_error("Invalid vector identity clock."); Document.TokenClock = uint32_t(*Clock);
    const auto *Instructions = Table["instructions"].as_array(), *Segments = Table["segments"].as_array(), *Layers = Table["layers"].as_array();
    if (!Instructions || !Segments || !Layers || Instructions->size() > 128 || Segments->size() > 32768 || Layers->size() > 64) throw std::runtime_error("Invalid vector sequence bounds.");
    for (const auto &Instruction : *Instructions) { if (!Instruction.as_table()) throw std::runtime_error("Invalid vector instruction."); Document.Instructions.push_back(DecodeInstruction(*Instruction.as_table())); }
    for (const auto &Segment : *Segments)
    {
        if (!Segment.as_table()) throw std::runtime_error("Invalid vector segment.");
        const auto &Coordinates = *Segment.as_table();
        ValidateFields(Coordinates, {"start_control", "control_end"});
        Document.Segments.push_back({DecodeCoordinates(Coordinates["start_control"]), DecodeCoordinates(Coordinates["control_end"])});
    }
    for (const auto &Encoded : *Layers)
    {
        if (!Encoded.as_table()) throw std::runtime_error("Invalid vector layer.");
        const auto &Entry = *Encoded.as_table();
        ValidateFields(Entry, {"token", "root", "name", "visible", "locked", "mask_enabled", "mask_inverted", "mask_luminance", "mask", "enclosure_token", "surface_channels", "surface_structure", "textile_geometry", "stitch_role", "weave_role", "surface_relief", "expression_opacity", "component_kind", "source_markup", "embedded_image", "text_point_size", "text_typeface", "modifiers"});
        VectorAuthoringLayer Layer;
        if (!Entry["token"].is_integer() || !Entry["root"].is_integer()) throw std::runtime_error("Expected integer vector layer identity.");
        const auto Token = Entry["token"].value<int64_t>(), Root = Entry["root"].value<int64_t>();
        if (!Token || !Root || *Token <= 0 || *Token > Document.TokenClock || *Root < 0 || *Root > UINT32_MAX) throw std::runtime_error("Invalid vector layer token.");
        Layer.Token = uint32_t(*Token); Layer.Root = uint32_t(*Root); Layer.Name = DecodeOptional<std::string>(Entry, "name", "VectorAuthoring");
        Layer.Visible = DecodeOptional<bool>(Entry, "visible", true); Layer.Locked = DecodeOptional<bool>(Entry, "locked", false);
        Layer.MaskEnabled = DecodeOptional<bool>(Entry, "mask_enabled", true);
        Layer.MaskInverted = DecodeOptional<bool>(Entry, "mask_inverted", false);
        Layer.MaskLuminance = DecodeOptional<bool>(Entry, "mask_luminance", false);
        Layer.EnclosureToken = DecodeEnclosureToken(Entry);
        Layer.SurfaceRelief = float(DecodeOptional<double>(Entry, "surface_relief", 1));
        Layer.ExpressionOpacity = float(DecodeOptional<double>(Entry, "expression_opacity", 1));
        const auto Structure = DecodeOptional<std::string>(Entry, "surface_structure", "freeform");
        if (Structure == "stitch") Layer.SurfaceStructure = VectorAuthoringSurfaceStructure::Stitch;
        else if (Structure == "weave") Layer.SurfaceStructure = VectorAuthoringSurfaceStructure::Weave;
        else if (Structure != "freeform") throw std::runtime_error("Unknown vector surface structure.");
        const auto TextileGeometry = DecodeOptional<std::string>(Entry, "textile_geometry", "unclassified");
        if (TextileGeometry == "needle_entry") Layer.TextileGeometry = VectorAuthoringTextileGeometry::NeedleEntry;
        else if (TextileGeometry == "stitch_thread") Layer.TextileGeometry = VectorAuthoringTextileGeometry::StitchThread;
        else if (TextileGeometry == "upper_thread_pass") Layer.TextileGeometry = VectorAuthoringTextileGeometry::UpperThreadPass;
        else if (TextileGeometry == "weave_yarn") Layer.TextileGeometry = VectorAuthoringTextileGeometry::WeaveYarn;
        else if (TextileGeometry != "unclassified") throw std::runtime_error("Unknown native textile geometry.");
        if (Entry.contains("stitch_role") && Entry.contains("weave_role"))
            throw std::runtime_error("A vector layer cannot carry both stitch and weave geometry.");
        if (Entry.contains("stitch_role"))
        {
            const auto StitchGeometry = DecodeOptional<std::string>(Entry, "stitch_role", "");
            const auto DerivedGeometry = StitchGeometry == "holes" ? VectorAuthoringTextileGeometry::NeedleEntry :
                StitchGeometry == "thread" ? VectorAuthoringTextileGeometry::StitchThread :
                StitchGeometry == "highlight" ? VectorAuthoringTextileGeometry::UpperThreadPass :
                VectorAuthoringTextileGeometry::Unclassified;
            if (DerivedGeometry == VectorAuthoringTextileGeometry::Unclassified ||
                (Layer.TextileGeometry != VectorAuthoringTextileGeometry::Unclassified && Layer.TextileGeometry != DerivedGeometry) ||
                (Layer.SurfaceStructure != VectorAuthoringSurfaceStructure::Freeform && Layer.SurfaceStructure != VectorAuthoringSurfaceStructure::Stitch))
                throw std::runtime_error("Unknown or conflicting native stitch geometry.");
            Layer.TextileGeometry = DerivedGeometry;
            Layer.SurfaceStructure = VectorAuthoringSurfaceStructure::Stitch;
        }
        if (Entry.contains("weave_role"))
        {
            if (DecodeOptional<std::string>(Entry, "weave_role", "") != "yarn" ||
                (Layer.TextileGeometry != VectorAuthoringTextileGeometry::Unclassified && Layer.TextileGeometry != VectorAuthoringTextileGeometry::WeaveYarn) ||
                (Layer.SurfaceStructure != VectorAuthoringSurfaceStructure::Freeform && Layer.SurfaceStructure != VectorAuthoringSurfaceStructure::Weave))
                throw std::runtime_error("Unknown or conflicting native weave geometry.");
            Layer.TextileGeometry = VectorAuthoringTextileGeometry::WeaveYarn;
            Layer.SurfaceStructure = VectorAuthoringSurfaceStructure::Weave;
        }
        if (Entry.contains("surface_channels"))
        {
            const auto *Channels = Entry["surface_channels"].as_table();
            if (!Channels) throw std::runtime_error("Expected vector surface channel table.");
            ValidateFields(*Channels, {"colour", "height", "normal", "roughness", "metalness", "opacity", "occlusion", "transmission", "clearcoat"});
            for (size_t Index = 0; Index < ChannelCaptions.size(); ++Index)
            {
                if (!Channels->contains(ChannelCaptions[Index])) continue;
                const auto *Channel = (*Channels)[ChannelCaptions[Index]].as_table();
                if (!Channel) throw std::runtime_error("Expected vector channel contribution table.");
                ValidateFields(*Channel, {"enabled", "strength"});
                Layer.SurfaceChannels[Index].Enabled = DecodeOptional<bool>(*Channel, "enabled", Layer.SurfaceChannels[Index].Enabled);
                Layer.SurfaceChannels[Index].Strength = float(DecodeOptional<double>(*Channel, "strength", 1));
            }
        }
        if (Entry.contains("mask") && !Entry["mask"].is_table()) throw std::runtime_error("Expected vector mask instruction table.");
        if (const auto *Mask = Entry["mask"].as_table()) Layer.Mask = DecodeInstruction(*Mask);
        const auto ComponentKind = DecodeOptional<std::string>(Entry, "component_kind", "freeform");
        if (ComponentKind == "text") Layer.ComponentKind = VectorAuthoringComponentKind::Text;
        else if (ComponentKind == "imported_vector") Layer.ComponentKind = VectorAuthoringComponentKind::ImportedVector;
        else if (ComponentKind == "imported_image") Layer.ComponentKind = VectorAuthoringComponentKind::ImportedImage;
        else if (ComponentKind != "freeform") throw std::runtime_error("Unknown vector component kind.");
        Layer.SourceMarkup = DecodeOptional<std::string>(Entry, "source_markup", "");
        Layer.EmbeddedImage = DecodeOptional<std::string>(Entry, "embedded_image", "");
        Layer.TextPointSize = float(DecodeOptional<double>(Entry, "text_point_size", 64));
        Layer.TextTypeface = DecodeOptional<std::string>(Entry, "text_typeface", "");
        if (Entry.contains("modifiers"))
        {
            const auto *Modifiers = Entry["modifiers"].as_array();
            if (!Modifiers || Modifiers->size() > 4) throw std::runtime_error("Expected at most 4 native array modifiers per layer.");
            for (const auto &Encoded : *Modifiers)
            {
                if (!Encoded.as_table()) throw std::runtime_error("Invalid native array modifier.");
                Layer.Modifiers.push_back(DecodeArrayModifier(*Encoded.as_table()));
            }
        }
        Document.Layers.push_back(std::move(Layer));
    }
    if (Table.contains("enclosures"))
    {
        const auto *Enclosures = Table["enclosures"].as_array();
        if (!Enclosures || Enclosures->size() > 64) throw std::runtime_error("Expected at most 64 vector enclosures.");
        for (const auto &Encoded : *Enclosures)
        {
            const auto *Entry = Encoded.as_table();
            if (!Entry) throw std::runtime_error("Expected vector enclosure table.");
            ValidateFields(*Entry, {"token", "enclosure_token", "name", "folder_enabled", "visible", "locked", "group_wide_editing"});
            VectorAuthoringEnclosure Enclosure;
            if (!(*Entry)["token"].is_integer()) throw std::runtime_error("Expected integer vector enclosure identity.");
            const auto Token = (*Entry)["token"].value<int64_t>();
            if (!Token || *Token <= 0 || *Token > UINT32_MAX) throw std::runtime_error("Invalid vector enclosure identity.");
            Enclosure.Token = uint32_t(*Token);
            Enclosure.EnclosureToken = DecodeEnclosureToken(*Entry);
            Enclosure.Name = DecodeOptional<std::string>(*Entry, "name", "Group");
            Enclosure.FolderEnabled = DecodeOptional<bool>(*Entry, "folder_enabled", false);
            Enclosure.VisibilityEnabled = DecodeOptional<bool>(*Entry, "visible", true);
            Enclosure.LockEnabled = DecodeOptional<bool>(*Entry, "locked", false);
            Enclosure.GroupWideEditingEnabled = DecodeOptional<bool>(*Entry, "group_wide_editing", false);
            Document.Enclosures.push_back(std::move(Enclosure));
        }
    }
    (void)ConstructVectorAuthoringCoverage(Document); return Document;
}
}
