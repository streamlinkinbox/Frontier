/*==========================================================================================================================================
                                                          SURFACEMATERIAL.CPP
==========================================================================================================================================*/
// 🧩 Strict TOML material authoring, contained PNG/JPEG ingestion and colour-correct/normal-aware mip compilation.

#include "SurfaceMaterial.h"
#include "MaterialProceduralBaking.h"
#include "Engine/Diagnostics/DiagnosticJournal.h"
#include <toml++/toml.hpp>
#include <cctype>
#include <fstream>
#include <sstream>
#include <memory>
// 📝 The PNG/JPEG implementation lives in StbImageImplementation.cpp so every static-library member shares one.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif
#include <stb_image.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace Frontier
{
namespace
{
void ValidateKeys(const toml::table &Table,std::initializer_list<std::string_view> Allowed)
{
    for (const auto &[Key,Content] : Table)
    {
        (void)Content;
        if (std::find(Allowed.begin(),Allowed.end(),Key.str()) == Allowed.end())
            throw std::runtime_error("Unsupported material field: "+std::string(Key.str()));
    }
}

template<size_t Count>
std::array<float,Count> DecodeSpectrum(const toml::node_view<toml::node> &Field,std::array<float,Count> Fallback)
{
    if (!Field) return Fallback;
    const auto *Channels = Field.as_array();
    if (!Channels || Channels->size() != Count) throw std::runtime_error("Material vector has the wrong channel count.");
    for (size_t Index = 0; Index < Count; ++Index)
    {
        const auto Coefficient = Channels->get(Index)->value<double>();
        if (!Coefficient || !std::isfinite(*Coefficient) || std::abs(*Coefficient) > 1e6)
            throw std::runtime_error("Material vector channels must be finite bounded numbers.");
        Fallback[Index] = float(*Coefficient);
    }
    return Fallback;
}

float DecodeCoefficient(const toml::node_view<toml::node> &Field,float Fallback)
{
    if (!Field) return Fallback;
    const auto Coefficient = Field.value<double>();
    if (!Coefficient || !std::isfinite(*Coefficient) || std::abs(*Coefficient) > 1e6)
        throw std::runtime_error("Material coefficient must be a finite bounded number.");
    return float(*Coefficient);
}

template<size_t Count>
toml::array EncodeSpectrum(const std::array<float,Count> &Channels)
{
    toml::array Result;
    for (const auto Channel : Channels) Result.push_back(double(Channel));
    return Result;
}

double DecodeSrgb(double Encoded)
{
    return Encoded <= .04045 ? Encoded/12.92 : std::pow((Encoded+.055)/1.055,2.4);
}
double EncodeSrgb(double Linear)
{
    return Linear <= .0031308 ? Linear*12.92 : 1.055*std::pow(Linear,1/2.4)-.055;
}

std::string EncodeHexFromLinearRgb(const float Linear[3])
{
    char Buffer[8];
    const auto Byte = [](float Channel) {
        return unsigned(std::lround(std::clamp(EncodeSrgb(std::clamp(double(Channel), 0., 1.)), 0., 1.) * 255.));
    };
    std::snprintf(Buffer, sizeof(Buffer), "#%02x%02x%02x", Byte(Linear[0]), Byte(Linear[1]), Byte(Linear[2]));
    return Buffer;
}

std::vector<uint8_t> DecodeBase64DataUrl(std::string_view DataUrl)
{
    const size_t Comma = DataUrl.find(',');
    const std::string_view Payload = Comma == std::string_view::npos ? DataUrl : DataUrl.substr(Comma + 1);
    std::vector<uint8_t> Bytes;
    Bytes.reserve(Payload.size() * 3 / 4);
    uint32_t Accumulator = 0;
    int Bits = -8;
    for (unsigned char Character : Payload)
    {
        int Digit = -1;
        if (Character >= 'A' && Character <= 'Z') Digit = Character - 'A';
        else if (Character >= 'a' && Character <= 'z') Digit = Character - 'a' + 26;
        else if (Character >= '0' && Character <= '9') Digit = Character - '0' + 52;
        else if (Character == '+') Digit = 62;
        else if (Character == '/') Digit = 63;
        else if (Character == '=') break;
        else continue;
        Accumulator = (Accumulator << 6) | uint32_t(Digit);
        Bits += 6;
        if (Bits >= 0)
        {
            Bytes.push_back(uint8_t((Accumulator >> Bits) & 0xffu));
            Bits -= 8;
        }
    }
    return Bytes;
}

void EvaluateAuthoringLayerStack(const MaterialDefinition &Material, const MaterialAuthoringDocument &Document,
    CompiledSurfaceMaterial &Compiled)
{
    MaterialAuthoringStore Store;
    auto &Illumination = Compiled.Scattering.Illumination;
    auto &Paint = Compiled.Scattering.Paint;
    if (Illumination.Configuration[0] > 0)
    {
        const auto &Base = Illumination.Strata[0];
        Store["color"] = EncodeHexFromLinearRgb(Base.AlbedoRoughness.data());
        Store["roughness"] = double(Base.AlbedoRoughness[3]);
        Store["metalness"] = double(Base.Optics[0]);
        Store["ior"] = double(Base.Optics[1]);
        Store["specular"] = double(Compiled.Scattering.DirectionalOptics[0][3]);
        Store["anisotropy"] = double(Compiled.Scattering.DirectionalOptics[0][0]);
        Store["sheen"] = double(Base.Sheen[3]);
        Store["sheenColor"] = EncodeHexFromLinearRgb(Base.Sheen.data());
        Store["sheenRoughness"] = double(Base.SheenFinish[0]);
        Store["heightStrength"] = double(Compiled.Scattering.StratumNormal[0][1]);
        Store["normalStrength"] = double(Compiled.Scattering.RadianceNormal[3]);
    }
    if (Illumination.Configuration[0] > 1 && Illumination.Strata[1].Classification[0] == 1)
    {
        Store["coat"] = double(Illumination.Strata[1].Optics[2]);
        Store["coatRoughness"] = double(Illumination.Strata[1].AlbedoRoughness[3]);
        Store["coatIor"] = double(Illumination.Strata[1].Optics[1]);
    }
    else
    {
        Store["coat"] = 0.;
        Store["coatRoughness"] = .1;
        Store["coatIor"] = 1.5;
    }
    Store["flakeLayers"] = double(Paint.Configuration[1]);
    Store["flakes"] = double(Paint.Distribution[0]);
    Store["flakeSize"] = double(Paint.Distribution[1]);
    Store["flakeScale"] = double(Paint.Distribution[2]);
    Store["depth"] = double(Paint.Distribution[3]);
    Store["flakeRoughness"] = double(Paint.Finish[0]);
    Store["flakeRoughnessMin"] = double(Paint.Finish[0]);
    Store["flakeRoughnessMax"] = double(Paint.Finish[1]);
    Store["flakeMetalnessMin"] = double(Paint.Finish[2]);
    Store["flakeMetalnessMax"] = double(Paint.Finish[3]);
    Store["flakeTilt"] = double(Paint.Relief[0]);
    Store["flakeLayerDepth"] = double(Paint.Relief[1]);
    Store["orangePeel"] = double(Paint.Relief[2]);
    Store["orangePeelScale"] = double(Paint.Relief[3]);
    Store["flakeTint"] = EncodeHexFromLinearRgb(Paint.Palette[0].data());

    const auto ReadQuantity = [&](const std::string &Address) -> std::optional<MaterialAuthoringQuantity>
    {
        try { return ReadMaterialParameter(Document, Store, Address); }
        catch (const std::exception &) { return std::nullopt; }
    };
    const auto ReadScalar = [&](const std::string &Address, double Fallback) -> double
    {
        const auto Quantity = ReadQuantity(Address);
        return Quantity && std::holds_alternative<double>(*Quantity) && std::isfinite(std::get<double>(*Quantity)) ?
            std::get<double>(*Quantity) : Fallback;
    };
    const auto HasExplicit = [&](const std::string &Address) -> bool
    {
        std::string Root = Address;
        try { Root = ResolveMaterialAddress(Document, Address); } catch (const std::exception &) {}
        return Document.Inputs.contains(Address) || Document.Inputs.contains(Root) || Document.Links.contains(Address);
    };
    const auto HasChannel = [](const MaterialAuthoringLayer &Layer, std::string_view Channel) -> bool
    {
        return std::find(Layer.Channels.begin(), Layer.Channels.end(), Channel) != Layer.Channels.end();
    };

    // Pass 1: Structural layer state and flat scalar/colour overrides (surface, flakes, coat).
    for (const auto &Layer : Document.Layers)
    {
        if (Layer.Model == "surface" && Illumination.Configuration[0] > 0)
        {
            auto &Base = Illumination.Strata[0];
            if (!Layer.Enabled)
            {
                Base.AlbedoRoughness = {.5f, .5f, .5f, .5f};
                Base.Optics = {0.f, 1.5f, 1.f, 0.f};
                Base.Sheen = {0.f, 0.f, 0.f, 0.f};
                Compiled.Scattering.TextureUse[0][0] = 0;
            }
            else
            {
                if (HasExplicit("color"))
                    if (const auto Quantity = ReadQuantity("color"); Quantity && std::holds_alternative<std::string>(*Quantity))
                    {
                        float Linear[3]{};
                        if (DecodeHexLinearRgb(std::get<std::string>(*Quantity), Linear))
                        {
                            Base.AlbedoRoughness[0] = Linear[0];
                            Base.AlbedoRoughness[1] = Linear[1];
                            Base.AlbedoRoughness[2] = Linear[2];
                        }
                    }
                if (HasExplicit("roughness")) Base.AlbedoRoughness[3] = float(std::clamp(ReadScalar("roughness", Base.AlbedoRoughness[3]), .045, 1.));
                if (!HasChannel(Layer, "metallic")) Base.Optics[0] = 0.f;
                else if (HasExplicit("metalness")) Base.Optics[0] = float(std::clamp(ReadScalar("metalness", Base.Optics[0]), 0., 1.));
                if (HasExplicit("ior")) Base.Optics[1] = float(std::clamp(ReadScalar("ior", Base.Optics[1]), 1.01, 3.));
                if (HasExplicit("specular")) Compiled.Scattering.DirectionalOptics[0][3] = float(std::clamp(ReadScalar("specular", Compiled.Scattering.DirectionalOptics[0][3]), 0., 1.));
                if (!HasChannel(Layer, "anisotropy")) Compiled.Scattering.DirectionalOptics[0][0] = 0.f;
                else if (HasExplicit("anisotropy")) Compiled.Scattering.DirectionalOptics[0][0] = float(std::clamp(ReadScalar("anisotropy", 0.), -1., 1.));
                if (!HasChannel(Layer, "sheen")) Base.Sheen[3] = 0.f;
                else
                {
                    if (HasExplicit("sheen")) Base.Sheen[3] = float(std::clamp(ReadScalar("sheen", Base.Sheen[3]), 0., 1.));
                    if (HasExplicit("sheenRoughness")) Base.SheenFinish[0] = float(std::clamp(ReadScalar("sheenRoughness", Base.SheenFinish[0]), .045, 1.));
                    if (HasExplicit("sheenColor"))
                        if (const auto Quantity = ReadQuantity("sheenColor"); Quantity && std::holds_alternative<std::string>(*Quantity))
                        {
                            float Linear[3]{};
                            if (DecodeHexLinearRgb(std::get<std::string>(*Quantity), Linear))
                                std::copy(std::begin(Linear), std::end(Linear), Base.Sheen.begin());
                        }
                }
                if (!HasChannel(Layer, "normal")) Compiled.Scattering.TextureUse[0][0] &= ~4u;
            }
        }
        else if (Layer.Model == "flakes")
        {
            const MaterialAuthoringField *Generator = nullptr;
            for (const auto &Candidate : Document.Generators)
                if (Candidate.Identity == Layer.Generator || (Layer.Generator.empty() && Candidate.Procedure == "flakes"))
                    Generator = &Candidate;
            if (!Layer.Enabled || (Generator && !Generator->Enabled) || !HasChannel(Layer, "coverage"))
            {
                Paint.Configuration[0] = 0;
            }
            else
            {
                if (Paint.Configuration[3] == 0)
                {
                    Paint.Configuration = {1, 2, 2, 1};
                    Paint.Distribution = {.38f, 180.f, 1.f, .42f};
                    Paint.Finish = {.14f, .28f, .85f, 1.f};
                    Paint.Relief = {.2f, .5f, 0.f, 24.f};
                    Paint.Palette[0] = {.92f, .90f, .86f, 0.f};
                }
                else Paint.Configuration[0] = 1;
                if (HasExplicit("flakeLayers"))
                    Paint.Configuration[1] = uint32_t(std::clamp(std::lround(ReadScalar("flakeLayers", Paint.Configuration[1])), 1l, 4l));
                if (HasExplicit("flakes")) Paint.Distribution[0] = float(std::clamp(ReadScalar("flakes", Paint.Distribution[0]), 0., 1.));
                if (HasExplicit("flakeSize")) Paint.Distribution[1] = float(std::clamp(ReadScalar("flakeSize", Paint.Distribution[1]), .001, 1000000.));
                if (HasExplicit("flakeScale")) Paint.Distribution[2] = float(std::clamp(ReadScalar("flakeScale", Paint.Distribution[2]), .001, 1000.));
                if (HasExplicit("depth")) Paint.Distribution[3] = float(std::clamp(ReadScalar("depth", Paint.Distribution[3]), 0., 4.));
                if (HasExplicit("flakeRoughnessMin")) Paint.Finish[0] = float(std::clamp(ReadScalar("flakeRoughnessMin", Paint.Finish[0]), 0., 1.));
                if (HasExplicit("flakeRoughnessMax")) Paint.Finish[1] = float(std::clamp(ReadScalar("flakeRoughnessMax", Paint.Finish[1]), 0., 1.));
                if (HasExplicit("flakeRoughness"))
                {
                    const float Rough = float(std::clamp(ReadScalar("flakeRoughness", Paint.Finish[0]), 0., 1.));
                    Paint.Finish[0] = Rough;
                    Paint.Finish[1] = std::max(Rough, Paint.Finish[1]);
                }
                if (HasExplicit("flakeMetalnessMin")) Paint.Finish[2] = float(std::clamp(ReadScalar("flakeMetalnessMin", Paint.Finish[2]), 0., 1.));
                if (HasExplicit("flakeMetalnessMax")) Paint.Finish[3] = float(std::clamp(ReadScalar("flakeMetalnessMax", Paint.Finish[3]), 0., 1.));
                if (HasExplicit("flakeTilt")) Paint.Relief[0] = float(std::clamp(ReadScalar("flakeTilt", Paint.Relief[0]), 0., 1.));
                if (HasExplicit("flakeLayerDepth")) Paint.Relief[1] = float(std::clamp(ReadScalar("flakeLayerDepth", Paint.Relief[1]), 0., 1.));
                if (HasExplicit("orangePeel")) Paint.Relief[2] = float(std::clamp(ReadScalar("orangePeel", Paint.Relief[2]), 0., 1.));
                if (HasExplicit("orangePeelScale")) Paint.Relief[3] = float(std::clamp(ReadScalar("orangePeelScale", Paint.Relief[3]), 0., 10000.));
                const std::string CoverageAddr = MaterialChannelAddress(Layer, "coverage");
                if (HasExplicit(CoverageAddr))
                    Paint.Distribution[0] = float(std::clamp(Paint.Distribution[0] * ReadScalar(CoverageAddr, 1.), 0., 1.));
                const std::string ColourAddr = MaterialChannelAddress(Layer, "colour");
                if (HasExplicit(ColourAddr) || HasExplicit("flakeTint"))
                {
                    const auto Quantity = HasExplicit(ColourAddr) ? ReadQuantity(ColourAddr) : ReadQuantity("flakeTint");
                    if (Quantity && std::holds_alternative<std::string>(*Quantity))
                    {
                        float Linear[3]{};
                        if (DecodeHexLinearRgb(std::get<std::string>(*Quantity), Linear))
                        {
                            Paint.Configuration[2] = 2;
                            Paint.Configuration[3] = std::max(1u, Paint.Configuration[3]);
                            for (size_t Index = 0; Index < 3; ++Index) Paint.Palette[0][Index] = Linear[Index];
                        }
                    }
                }
                const std::string RoughAddr = MaterialChannelAddress(Layer, "roughness");
                if (HasExplicit(RoughAddr))
                {
                    const float Rough = float(std::clamp(ReadScalar(RoughAddr, Paint.Finish[0]), 0., 1.));
                    Paint.Finish[0] = Rough;
                    Paint.Finish[1] = Rough;
                }
                const std::string MetalAddr = MaterialChannelAddress(Layer, "metallic");
                if (!HasChannel(Layer, "metallic"))
                {
                    Paint.Finish[2] = 0.f;
                    Paint.Finish[3] = 0.f;
                }
                else if (HasExplicit(MetalAddr))
                {
                    const float Metal = float(std::clamp(ReadScalar(MetalAddr, Paint.Finish[2]), 0., 1.));
                    Paint.Finish[2] = Metal;
                    Paint.Finish[3] = Metal;
                }
                Paint.Finish[1] = std::max(Paint.Finish[0], Paint.Finish[1]);
                Paint.Finish[3] = std::max(Paint.Finish[2], Paint.Finish[3]);
                if (!HasChannel(Layer, "normal")) Paint.Relief[0] = 0.f;
            }
        }
        else if (Layer.Model == "coat")
        {
            const bool HasCoatStratum = Illumination.Configuration[0] > 1 && Illumination.Strata[1].Classification[0] == 1;
            if (!Layer.Enabled || !HasChannel(Layer, "coverage"))
            {
                if (HasCoatStratum) Illumination.Strata[1].Optics[2] = 0.f;
            }
            else
            {
                const double Coverage = std::clamp(ReadScalar("coat", HasCoatStratum ? Illumination.Strata[1].Optics[2] : .85), 0., 1.);
                const double Roughness = std::clamp(ReadScalar("coatRoughness", HasCoatStratum ? Illumination.Strata[1].AlbedoRoughness[3] : .08), .045, 1.);
                const double Ior = std::clamp(ReadScalar("coatIor", HasCoatStratum ? Illumination.Strata[1].Optics[1] : 1.5), 1.01, 3.);
                if (!HasCoatStratum && Illumination.Configuration[0] == 1 && Coverage > 0. && HasExplicit("coat"))
                {
                    Illumination.Configuration[0] = 2;
                    auto &Coat = Illumination.Strata[1];
                    Coat = {};
                    Coat.Classification[0] = 1;
                    Coat.AlbedoRoughness = {1.f, 1.f, 1.f, float(Roughness)};
                    Coat.Optics = {0.f, float(Ior), float(Coverage), 0.f};
                    Compiled.Scattering.DirectionalOptics[1] = {0.f, 0.f, 0.f, .5f};
                }
                else if (HasCoatStratum && (HasExplicit("coat") || HasExplicit("coatRoughness") || HasExplicit("coatIor")))
                {
                    Illumination.Strata[1].Optics[2] = float(Coverage);
                    Illumination.Strata[1].AlbedoRoughness[3] = float(Roughness);
                    Illumination.Strata[1].Optics[1] = float(Ior);
                }
            }
        }
    }

    // Pass 2: Bottom-to-top PBR channel compositing for Fill ("detail") layers and spatial sources.
    struct DecodedInlineRaster
    {
        uint32_t Width = 0, Height = 0;
        std::vector<uint8_t> Rgba;
    };
    struct EvaluatedChannelSpec
    {
        std::string Channel;
        std::string Address;
        const MaterialPbrChannel *Specification = nullptr;
        double DomainMin = 0., DomainMax = 1.;
        MaterialAuthoringSource Source;
        std::optional<MaterialAuthoringQuantity> Constant;
        const MaterialAuthoringField *Generator = nullptr;
        CodedFieldParameters FieldParams{};
        double FieldAngle = 0., FieldAmount = 1.;
        std::vector<std::array<float, 3>> CamouflagePaletteLinear;
        std::vector<std::array<float, 3>> StopsLinear;
        std::optional<DecodedInlineRaster> Raster;
        bool Spatial = false;
    };

    const auto BuildChannelSpec = [&](const MaterialAuthoringLayer &Layer, const std::string &ChannelName) -> std::optional<EvaluatedChannelSpec>
    {
        const auto *Spec = QueryMaterialPbrChannel(ChannelName);
        if (!Spec) return std::nullopt;
        EvaluatedChannelSpec Entry;
        Entry.Channel = ChannelName;
        const std::string RawAddress = MaterialChannelAddress(Layer, ChannelName);
        Entry.Address = RawAddress;
        try { Entry.Address = ResolveMaterialAddress(Document, RawAddress); } catch (const std::exception &) {}
        Entry.Specification = Spec;
        Entry.DomainMin = Spec->Minimum;
        Entry.DomainMax = Spec->Maximum;
        if (const auto LinkFound = Document.Links.find(RawAddress); LinkFound != Document.Links.end())
            for (const auto &Variable : Document.Variables)
                if (Variable.Identity == LinkFound->second)
                {
                    Entry.DomainMin = Variable.Minimum;
                    Entry.DomainMax = Variable.Maximum;
                    break;
                }
        Entry.Source.Mode = "value";
        Entry.Source.Stops = {"#000000", "#ffffff"};
        if (const auto Found = Document.Sources.find(Entry.Address); Found != Document.Sources.end()) Entry.Source = Found->second;
        if (Entry.Source.Stops.size() < 2) Entry.Source.Stops = {"#000000", "#ffffff"};
        for (const auto &Hex : Entry.Source.Stops)
        {
            std::array<float, 3> Linear{1.f, 1.f, 1.f};
            (void)DecodeHexLinearRgb(Hex, Linear.data());
            Entry.StopsLinear.push_back(Linear);
        }
        Entry.Constant = ReadQuantity(Entry.Address);
        if (!Entry.Source.Generator.empty())
            for (const auto &Candidate : Document.Generators)
                if (Candidate.Identity == Entry.Source.Generator) Entry.Generator = &Candidate;
        if (Entry.Generator && Entry.Generator->Coded)
        {
            std::vector<std::pair<int, std::array<float, 3>>> IndexedColours;
            for (const auto &Input : Entry.Generator->Inputs)
            {
                const size_t Colon = Input.rfind(':');
                const std::string Suffix = Colon == std::string::npos ? Input : Input.substr(Colon + 1);
                if (Suffix == "scale") Entry.FieldParams.Scale = ReadScalar(Input, 8.);
                else if (Suffix == "seed") Entry.FieldParams.Seed = ReadScalar(Input, 12.);
                else if (Suffix == "softness") Entry.FieldParams.Softness = ReadScalar(Input, .012);
                else if (Suffix == "pixelSize") Entry.FieldParams.PixelSize = ReadScalar(Input, 4.);
                else if (Suffix == "angle") Entry.FieldAngle = ReadScalar(Input, 0.);
                else if (Suffix == "amount") Entry.FieldAmount = ReadScalar(Input, 1.);
                else if (Suffix.rfind("colour", 0) == 0 && Suffix.size() > 6)
                {
                    int SlotIndex = 0;
                    try { SlotIndex = std::stoi(Suffix.substr(6)); } catch (const std::exception &) { continue; }
                    if (const auto Quantity = ReadQuantity(Input); Quantity && std::holds_alternative<std::string>(*Quantity))
                    {
                        std::array<float, 3> Linear{.5f, .5f, .5f};
                        if (DecodeHexLinearRgb(std::get<std::string>(*Quantity), Linear.data()))
                            IndexedColours.push_back({SlotIndex, Linear});
                    }
                }
            }
            std::sort(IndexedColours.begin(), IndexedColours.end(),
                [](const auto &Left, const auto &Right) { return Left.first < Right.first; });
            for (const auto &[SlotIndex, Colour] : IndexedColours)
            {
                (void)SlotIndex;
                Entry.CamouflagePaletteLinear.push_back(Colour);
            }
            if (Entry.CamouflagePaletteLinear.size() < 2)
            {
                constexpr std::array<const char *, 4> DefaultCamo{"#6b7456", "#3d4633", "#7d6b52", "#232921"};
                Entry.CamouflagePaletteLinear.clear();
                for (const char *Hex : DefaultCamo)
                {
                    std::array<float, 3> Linear{};
                    (void)DecodeHexLinearRgb(Hex, Linear.data());
                    Entry.CamouflagePaletteLinear.push_back(Linear);
                }
            }
            Entry.FieldParams.ColourCount = uint32_t(Entry.CamouflagePaletteLinear.size());
        }
        if (Entry.Source.Mode == "texture" && Entry.Source.Texture && !Entry.Source.Texture->DataUrl.empty())
        {
            const auto Encoded = DecodeBase64DataUrl(Entry.Source.Texture->DataUrl);
            int Width = 0, Height = 0, Channels = 0;
            if (!Encoded.empty())
            {
                std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> Pixels(
                    stbi_load_from_memory(Encoded.data(), int(Encoded.size()), &Width, &Height, &Channels, 4), stbi_image_free);
                if (Pixels && Width >= 1 && Height >= 1 && Width <= 1024 && Height <= 1024)
                    Entry.Raster = DecodedInlineRaster{uint32_t(Width), uint32_t(Height),
                        std::vector<uint8_t>(Pixels.get(), Pixels.get() + size_t(Width) * Height * 4)};
            }
        }
        Entry.Spatial = Entry.Source.Mode != "value" && Entry.Source.Mode != "solid";
        return Entry;
    };

    const auto AlreadyBakedDirectlyInSurface = [&](const EvaluatedChannelSpec &Spec) -> bool
    {
        if (Spec.Source.Direct != std::optional<bool>(true) || Spec.Source.Mode != "field" || !Spec.Generator) return false;
        const std::string ExpectedSlot = Spec.Channel == "colour" ? "Albedo" :
            (Spec.Channel == "roughness" || Spec.Channel == "metallic") ? "Orm" :
            (Spec.Channel == "normal" || Spec.Channel == "microNormal") ? "Normal" : "";
        if (ExpectedSlot.empty()) return false;
        for (const auto &Baked : Material.StratumChannels[0])
        {
            if (Baked.Name != ExpectedSlot || Baked.Op != Spec.Generator->Procedure) continue;
            for (const auto &Input : Spec.Generator->Inputs)
            {
                const size_t Colon = Input.rfind(':');
                const std::string Key = Colon == std::string::npos ? Input : Input.substr(Colon + 1);
                const auto Quantity = ReadQuantity(Input);
                if (!Quantity) continue;
                if (const auto *Scalar = std::get_if<double>(&*Quantity))
                {
                    const auto Found = Baked.Params.find(Key);
                    if (Found != Baked.Params.end() && std::abs(Found->second - *Scalar) > 1e-5) return false;
                }
                else if (const auto *Text = std::get_if<std::string>(&*Quantity))
                {
                    const auto Found = Baked.Texts.find(Key);
                    if (Found != Baked.Texts.end() && Found->second != *Text) return false;
                }
            }
            return true;
        }
        return false;
    };

    struct LayerEvaluationPlan
    {
        std::string Model;
        std::optional<EvaluatedChannelSpec> Coverage;
        std::vector<EvaluatedChannelSpec> Channels;
    };

    std::vector<LayerEvaluationPlan> Plans;
    bool AnySpatial = false;
    for (auto Iterator = Document.Layers.rbegin(); Iterator != Document.Layers.rend(); ++Iterator)
    {
        const auto &Layer = *Iterator;
        if (!Layer.Enabled || (Layer.Model != "detail" && Layer.Model != "surface")) continue;
        LayerEvaluationPlan Plan;
        Plan.Model = Layer.Model;
        if (HasChannel(Layer, "coverage"))
        {
            Plan.Coverage = BuildChannelSpec(Layer, "coverage");
            if (Plan.Coverage && Plan.Coverage->Spatial) AnySpatial = true;
        }
        else if (Layer.Model == "detail") continue; // Fill layer without coverage channel has 0 coverage (Alloy parity).

        for (const auto &ChannelName : Layer.Channels)
        {
            if (ChannelName == "coverage") continue;
            auto Spec = BuildChannelSpec(Layer, ChannelName);
            if (!Spec) continue;
            if (Layer.Model == "surface")
            {
                if (!Spec->Spatial) continue;
                if (AlreadyBakedDirectlyInSurface(*Spec)) continue;
            }
            if (Spec->Spatial) AnySpatial = true;
            Plan.Channels.push_back(std::move(*Spec));
        }
        if (!Plan.Channels.empty()) Plans.push_back(std::move(Plan));
    }

    if (Plans.empty()) return;

    const auto SampleRawScalarOrColour = [&](const EvaluatedChannelSpec &Spec, double U, double V,
        std::array<float, 3> &OutComponents, float &OutAlpha, bool ForceScalarGradient) -> bool
    {
        OutAlpha = 1.f;
        const std::string_view Category = ForceScalarGradient ? std::string_view("scalar") : std::string_view(Spec.Specification->Category);
        if (Spec.Source.Mode == "value" || Spec.Source.Mode == "solid")
        {
            if (Category == "vector")
            {
                OutComponents = {0.f, 0.f, 1.f};
                if (Spec.Constant && std::holds_alternative<std::array<double, 3>>(*Spec.Constant))
                {
                    const auto &Vector = std::get<std::array<double, 3>>(*Spec.Constant);
                    OutComponents = {float(Vector[0]), float(Vector[1]), float(Vector[2])};
                }
            }
            else if (Category == "colour")
            {
                OutComponents = {1.f, 1.f, 1.f};
                if (Spec.Constant && std::holds_alternative<std::string>(*Spec.Constant))
                    (void)DecodeHexLinearRgb(std::get<std::string>(*Spec.Constant), OutComponents.data());
            }
            else
            {
                const double Fallback = Spec.Channel == "roughness" || Spec.Channel == "sheenRoughness" ? .5 :
                    Spec.Channel == "ior" ? 1.5 :
                    (Spec.Channel == "specular" || Spec.Channel == "coverage") ? 1. : Spec.DomainMin;
                const double Value = Spec.Constant && std::holds_alternative<double>(*Spec.Constant) ?
                    std::get<double>(*Spec.Constant) : Fallback;
                OutComponents = {float(Value), float(Value), float(Value)};
            }
            return true;
        }
        if (Spec.Source.Mode == "texture")
        {
            if (!Spec.Raster || Spec.Raster->Rgba.empty()) { OutAlpha = 0.f; return false; }
            const uint32_t X = std::min(Spec.Raster->Width - 1, uint32_t(std::clamp(U, 0., .999999) * Spec.Raster->Width));
            const uint32_t Y = std::min(Spec.Raster->Height - 1, uint32_t(std::clamp(V, 0., .999999) * Spec.Raster->Height));
            const size_t Offset = (size_t(Y) * Spec.Raster->Width + X) * 4;
            for (size_t Index = 0; Index < 3; ++Index)
            {
                const double Raw = Spec.Raster->Rgba[Offset + Index] / 255.;
                OutComponents[Index] = float(Category == "colour" ? DecodeSrgb(Raw) : Raw);
            }
            OutAlpha = Spec.Raster->Rgba[Offset + 3] / 255.f;
        }
        else
        {
            double Position = U;
            if (Spec.Source.Mode == "field")
            {
                if (!Spec.Generator || !Spec.Generator->Enabled || !Spec.Generator->Coded) { OutAlpha = 0.f; return false; }
                Position = EvaluateCodedFieldPosition(Spec.Generator->Procedure, Spec.FieldParams,
                    U, V, Spec.FieldAngle, Spec.FieldAmount);
            }
            else if (Spec.Source.Mode == "radial")
                Position = std::clamp(std::hypot(U - .5, V - .5) * 2., 0., 1.);
            else if (Spec.Source.Mode == "conic")
            {
                const double Turns = std::atan2(V - .5, U - .5) / (2. * 3.14159265358979323846) + 1.;
                Position = Turns - std::floor(Turns);
            }
            else Position = std::clamp(U, 0., 1.);

            const double Clamped = std::clamp(Position, 0., 1.);
            if (Spec.Source.Mode == "field" && Spec.Generator && Spec.Generator->Procedure.rfind("camouflage", 0) == 0 && !ForceScalarGradient)
            {
                const auto &Palette = Spec.CamouflagePaletteLinear;
                const double Scaled = Clamped * double(Palette.size() - 1);
                if (Spec.Generator->Procedure == "camouflage-smooth")
                {
                    const size_t First = std::min(Palette.size() - 2, size_t(std::floor(Scaled)));
                    const float Blend = float(Scaled - double(First));
                    for (size_t Index = 0; Index < 3; ++Index)
                        OutComponents[Index] = std::lerp(Palette[First][Index], Palette[First + 1][Index], Blend);
                }
                else
                {
                    const size_t Slot = std::min(Palette.size() - 1, size_t(std::lround(Scaled)));
                    OutComponents = Palette[Slot];
                }
            }
            else
            {
                const auto &Stops = Spec.StopsLinear;
                const double Segment = Clamped * double(Stops.size() - 1);
                const size_t First = std::min(Stops.size() - 2, size_t(std::floor(Segment)));
                const float Blend = float(Segment - double(First));
                for (size_t Index = 0; Index < 3; ++Index)
                    OutComponents[Index] = std::lerp(Stops[First][Index], Stops[First + 1][Index], Blend);
            }
        }
        if (Category == "scalar")
        {
            const double Maximum = (Spec.Source.Direct != std::optional<bool>(true) &&
                Spec.Constant && std::holds_alternative<double>(*Spec.Constant) && std::isfinite(std::get<double>(*Spec.Constant))) ?
                std::clamp(std::get<double>(*Spec.Constant), Spec.DomainMin, Spec.DomainMax) : Spec.DomainMax;
            const double Luminance = .2126 * OutComponents[0] + .7152 * OutComponents[1] + .0722 * OutComponents[2];
            const float Quantity = float(Spec.DomainMin + (ForceScalarGradient ? Luminance : Luminance * (Maximum - Spec.DomainMin)));
            OutComponents = {Quantity, Quantity, Quantity};
        }
        else if (Category == "colour" && Spec.Source.Direct != std::optional<bool>(true) &&
                 Spec.Constant && std::holds_alternative<std::string>(*Spec.Constant))
        {
            float Tint[3]{1.f, 1.f, 1.f};
            if (DecodeHexLinearRgb(std::get<std::string>(*Spec.Constant), Tint))
                for (size_t Index = 0; Index < 3; ++Index) OutComponents[Index] *= Tint[Index];
        }
        return true;
    };

    const auto SampleChannel = [&](const EvaluatedChannelSpec &Spec, double U, double V,
        std::array<float, 3> &OutComponents, float &OutAlpha) -> bool
    {
        if (std::string_view(Spec.Specification->Category) != "vector" ||
            Spec.Source.Mode == "value" || Spec.Source.Mode == "solid")
            return SampleRawScalarOrColour(Spec, U, V, OutComponents, OutAlpha, false);
        if (Spec.Source.Mode == "texture")
        {
            if (!SampleRawScalarOrColour(Spec, U, V, OutComponents, OutAlpha, false)) return false;
            for (float &Component : OutComponents) Component = Component * 2.f - 1.f;
        }
        else
        {
            constexpr double Step = 1. / 256.;
            std::array<float, 3> Centre{}, Right{}, Down{};
            float AlphaC = 0.f, AlphaR = 0.f, AlphaD = 0.f;
            if (!SampleRawScalarOrColour(Spec, U, V, Centre, AlphaC, true)) { OutAlpha = 0.f; return false; }
            double WrapU = U + Step; WrapU -= std::floor(WrapU);
            double WrapV = V + Step; WrapV -= std::floor(WrapV);
            (void)SampleRawScalarOrColour(Spec, WrapU, V, Right, AlphaR, true);
            (void)SampleRawScalarOrColour(Spec, U, WrapV, Down, AlphaD, true);
            const float SlopeX = float((Right[0] - Centre[0]) / Step);
            const float SlopeY = float((Down[0] - Centre[0]) / Step);
            OutComponents = {-SlopeX * .03f, SlopeY * .03f, 1.f};
            OutAlpha = AlphaC;
        }
        if (Spec.Source.Direct != std::optional<bool>(true) &&
            Spec.Constant && std::holds_alternative<std::array<double, 3>>(*Spec.Constant))
        {
            const auto &ConstantVec = std::get<std::array<double, 3>>(*Spec.Constant);
            OutComponents[0] += float(ConstantVec[0]);
            OutComponents[1] += float(ConstantVec[1]);
            OutComponents[2] += float(ConstantVec[2]) - 1.f;
        }
        const float Length = std::sqrt(OutComponents[0] * OutComponents[0] +
            OutComponents[1] * OutComponents[1] + OutComponents[2] * OutComponents[2]);
        if (Length > 1e-6f) for (float &Component : OutComponents) Component /= Length;
        else OutComponents = {0.f, 0.f, 1.f};
        return true;
    };

    const uint32_t Extent = AnySpatial ? 256u : 1u;
    const size_t Texels = size_t(Extent) * Extent;
    // Slots: 0=Colour, 1=Normal, 2=Height, 3=Roughness, 4=Metalness, 5=Occlusion
    std::array<std::vector<float>, 6> Premultiplied;
    std::array<std::vector<float>, 6> Alphas;
    std::array<bool, 6> SlotTouched{};
    for (size_t Slot = 0; Slot < 6; ++Slot)
    {
        Premultiplied[Slot].assign(Texels * 3, 0.f);
        Alphas[Slot].assign(Texels, 0.f);
    }

    double DetailIorSum = 0., DetailIorWeight = 0.;
    double DetailSpecularSum = 0., DetailSpecularWeight = 0.;
    double DetailSheenSum = 0., DetailSheenWeight = 0.;
    double DetailAnisoSum = 0., DetailAnisoWeight = 0.;

    for (const auto &Plan : Plans)
    {
        for (uint32_t Y = 0; Y < Extent; ++Y)
        {
            const double V = Extent == 1 ? .5 : (double(Y) + .5) / double(Extent);
            for (uint32_t X = 0; X < Extent; ++X)
            {
                const double U = Extent == 1 ? .5 : (double(X) + .5) / double(Extent);
                const size_t Pixel = size_t(Y) * Extent + X;
                float LayerCoverage = 1.f;
                if (Plan.Coverage)
                {
                    std::array<float, 3> CovSample{1.f, 1.f, 1.f};
                    float CovAlpha = 1.f;
                    if (!SampleChannel(*Plan.Coverage, U, V, CovSample, CovAlpha)) continue;
                    LayerCoverage = std::clamp(CovSample[0] * CovAlpha, 0.f, 1.f);
                }
                if (LayerCoverage <= 1e-6f) continue;

                for (const auto &Spec : Plan.Channels)
                {
                    std::array<float, 3> Value{};
                    float SampleAlpha = 1.f;
                    if (!SampleChannel(Spec, U, V, Value, SampleAlpha)) continue;
                    const float Weight = std::clamp(LayerCoverage * SampleAlpha, 0.f, 1.f);
                    if (Weight <= 1e-6f) continue;

                    int TargetSlot = -1;
                    if (Spec.Channel == "colour") TargetSlot = 0;
                    else if (Spec.Channel == "normal" || Spec.Channel == "microNormal") TargetSlot = 1;
                    else if (Spec.Channel == "roughness") TargetSlot = 3;
                    else if (Spec.Channel == "metallic") TargetSlot = 4;

                    if (TargetSlot >= 0)
                    {
                        SlotTouched[size_t(TargetSlot)] = true;
                        float *Dst = &Premultiplied[size_t(TargetSlot)][Pixel * 3];
                        float &DstA = Alphas[size_t(TargetSlot)][Pixel];
                        for (size_t Comp = 0; Comp < 3; ++Comp)
                            Dst[Comp] = Value[Comp] * Weight + Dst[Comp] * (1.f - Weight);
                        DstA = Weight + DstA * (1.f - Weight);
                    }
                    else if (Plan.Model == "detail")
                    {
                        if (Spec.Channel == "ior") { DetailIorSum += Value[0] * Weight; DetailIorWeight += Weight; }
                        else if (Spec.Channel == "specular") { DetailSpecularSum += Value[0] * Weight; DetailSpecularWeight += Weight; }
                        else if (Spec.Channel == "sheen") { DetailSheenSum += Value[0] * Weight; DetailSheenWeight += Weight; }
                        else if (Spec.Channel == "anisotropy") { DetailAnisoSum += Value[0] * Weight; DetailAnisoWeight += Weight; }
                    }
                }
            }
        }
    }

    if (Illumination.Configuration[0] > 0)
    {
        auto &Base = Illumination.Strata[0];
        if (DetailIorWeight > 0.)
        {
            const float AvgWeight = float(std::clamp(DetailIorWeight / double(Texels), 0., 1.));
            Base.Optics[1] = std::lerp(Base.Optics[1], float(DetailIorSum / DetailIorWeight), AvgWeight);
        }
        if (DetailSpecularWeight > 0.)
        {
            const float AvgWeight = float(std::clamp(DetailSpecularWeight / double(Texels), 0., 1.));
            Compiled.Scattering.DirectionalOptics[0][3] = std::lerp(
                Compiled.Scattering.DirectionalOptics[0][3], float(DetailSpecularSum / DetailSpecularWeight), AvgWeight);
        }
        if (DetailSheenWeight > 0.)
        {
            const float AvgWeight = float(std::clamp(DetailSheenWeight / double(Texels), 0., 1.));
            Base.Sheen[3] = std::lerp(Base.Sheen[3], float(DetailSheenSum / DetailSheenWeight), AvgWeight);
        }
        if (DetailAnisoWeight > 0.)
        {
            const float AvgWeight = float(std::clamp(DetailAnisoWeight / double(Texels), 0., 1.));
            Compiled.Scattering.DirectionalOptics[0][0] = std::lerp(
                Compiled.Scattering.DirectionalOptics[0][0], float(DetailAnisoSum / DetailAnisoWeight), AvgWeight);
        }
    }

    constexpr std::array<MaterialTextureCategory, 6> Categories{
        MaterialTextureCategory::Albedo, MaterialTextureCategory::TangentNormal,
        MaterialTextureCategory::OcclusionRoughnessMetallic, MaterialTextureCategory::OcclusionRoughnessMetallic,
        MaterialTextureCategory::OcclusionRoughnessMetallic, MaterialTextureCategory::OcclusionRoughnessMetallic};
    for (size_t Slot = 0; Slot < 6; ++Slot)
    {
        if (!SlotTouched[Slot]) continue;
        std::vector<float> StraightRgba(Texels * 4, 0.f);
        for (size_t Pixel = 0; Pixel < Texels; ++Pixel)
        {
            const float Alpha = std::clamp(Alphas[Slot][Pixel], 0.f, 1.f);
            float *Out = &StraightRgba[Pixel * 4];
            if (Alpha > 1e-6f)
            {
                const float InvAlpha = 1.f / Alpha;
                if (Slot == 1)
                {
                    float Nx = Premultiplied[Slot][Pixel * 3 + 0] * InvAlpha;
                    float Ny = Premultiplied[Slot][Pixel * 3 + 1] * InvAlpha;
                    float Nz = Premultiplied[Slot][Pixel * 3 + 2] * InvAlpha;
                    const float Len = std::sqrt(Nx * Nx + Ny * Ny + Nz * Nz);
                    if (Len > 1e-6f) { Nx /= Len; Ny /= Len; Nz /= Len; }
                    else { Nx = 0.f; Ny = 0.f; Nz = 1.f; }
                    Out[0] = Nx * .5f + .5f;
                    Out[1] = Ny * .5f + .5f;
                    Out[2] = Nz * .5f + .5f;
                }
                else
                {
                    Out[0] = std::clamp(Premultiplied[Slot][Pixel * 3 + 0] * InvAlpha, 0.f, 1.f);
                    Out[1] = std::clamp(Premultiplied[Slot][Pixel * 3 + 1] * InvAlpha, 0.f, 1.f);
                    Out[2] = std::clamp(Premultiplied[Slot][Pixel * 3 + 2] * InvAlpha, 0.f, 1.f);
                }
                Out[3] = Alpha;
            }
            else if (Slot == 1) { Out[0] = .5f; Out[1] = .5f; Out[2] = 1.f; Out[3] = 0.f; }
            else { Out[0] = (Slot == 4 ? 0.f : 1.f); Out[1] = 0.f; Out[2] = 0.f; Out[3] = 0.f; }
        }
        Compiled.AuthoringMaps[Slot] = ConstructFloatMaterialPyramid(Extent, Extent, Categories[Slot], StraightRgba);
    }
}
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     STRICT AUTHORED TOML
//------------------------------------------------------------------------------------------------------------------------

MaterialDefinition DecodeSurfaceMaterial(std::string_view Text)
{
    if (Text.size() > 1048576) throw std::runtime_error("Material TOML exceeds 1 MiB.");
    auto Table = toml::parse(Text);
    ValidateKeys(Table,{"Format","Name","UvScaleOffset","Radiance","NormalStrength","Textures","Strata","AutomotivePaint","Authoring"});
    if (Table["Format"].value<int64_t>() != 1) throw std::runtime_error("Unsupported material format; expected Format = 1.");
    MaterialDefinition Material;
    const auto Name = Table["Name"].value<std::string>();
    if (!Name || Name->empty() || Name->size() > 128) throw std::runtime_error("Material Name must contain 1..128 bytes.");
    Material.Name = *Name;
    Material.Scattering.UvScaleOffset = DecodeSpectrum(Table["UvScaleOffset"],Material.Scattering.UvScaleOffset);
    const auto Radiance = DecodeSpectrum<3>(Table["Radiance"],{0,0,0});
    std::copy(Radiance.begin(),Radiance.end(),Material.Scattering.RadianceNormal.begin());
    Material.Scattering.RadianceNormal[3] = DecodeCoefficient(Table["NormalStrength"],1);
    if (Table.contains("Textures"))
    {
        const auto *Textures = Table["Textures"].as_table();
        if (!Textures) throw std::runtime_error("Textures must be a table.");
        ValidateKeys(*Textures,{"Albedo","Orm","Normal","Radiance"});
        const std::array<const char *,4> Names{"Albedo","Orm","Normal","Radiance"};
        for (size_t Index = 0; Index < Names.size(); ++Index)
            if (Textures->contains(Names[Index]))
            {
                const auto Source = (*Textures)[Names[Index]].value<std::string>();
                if (!Source || Source->empty() || Source->size() > 512) throw std::runtime_error("Invalid material texture path.");
                Material.TextureSources[Index] = *Source;
            }
    }
    if (Table.contains("AutomotivePaint"))
    {
        auto *PaintTable = Table["AutomotivePaint"].as_table();
        if (!PaintTable) throw std::runtime_error("AutomotivePaint must be a table.");
        ValidateKeys(*PaintTable, {"Enabled", "Layers", "ColourMode", "Distribution", "Finish", "Relief", "Palette"});
        auto &Paint = Material.Scattering.Paint;
        const auto Enabled = (*PaintTable)["Enabled"].value<bool>();
        const auto Layers = (*PaintTable)["Layers"].value<int64_t>();
        const auto Mode = (*PaintTable)["ColourMode"].value<std::string>();
        if (!Enabled || !Layers || *Layers < 1 || *Layers > 4 || !Mode ||
            (*Mode != "Palette" && *Mode != "Ramp" && *Mode != "Single"))
            throw std::runtime_error("Paint requires Enabled, Layers (1..4) and ColourMode (Palette, Ramp or Single).");
        Paint.Configuration[0] = *Enabled ? 1 : 0;
        Paint.Configuration[1] = uint32_t(*Layers);
        Paint.Configuration[2] = *Mode == "Single" ? 2 : *Mode == "Ramp" ? 1 : 0;
        Paint.Distribution = DecodeSpectrum<4>((*PaintTable)["Distribution"], Paint.Distribution);
        Paint.Finish = DecodeSpectrum<4>((*PaintTable)["Finish"], Paint.Finish);
        Paint.Relief = DecodeSpectrum<4>((*PaintTable)["Relief"], Paint.Relief);
        auto *Palette = (*PaintTable)["Palette"].as_array();
        if (!Palette || Palette->empty() || Palette->size() > 12) throw std::runtime_error("Paint requires 1..12 linear RGBA palette stops.");
        Paint.Configuration[3] = uint32_t(Palette->size());
        for (size_t Index = 0; Index < Palette->size(); ++Index)
            Paint.Palette[Index] = DecodeSpectrum<4>(toml::node_view<toml::node>(Palette->get(Index)), {});
    }
    auto *Strata = Table["Strata"].as_array();
    if (!Strata || Strata->empty() || Strata->size() > 8) throw std::runtime_error("Material requires 1..8 ordered Strata.");
    Material.Scattering.Illumination.Configuration[0] = uint32_t(Strata->size());
    for (size_t Index = 0; Index < Strata->size(); ++Index)
    {
        auto *Stratum = Strata->get(Index)->as_table();
        if (!Stratum) throw std::runtime_error("Stratum must be a table.");
        ValidateKeys(*Stratum,{"Category","Albedo","Roughness","Metallic","Ior","Coverage","Absorption","Thickness","TextureMask","Textures","Channels","UvScaleOffset","NormalStrength","Anisotropy","Rotation","Fresnel","Specular","HeightStrength","SheenColor","SheenIntensity","SheenRoughness"});
        const auto Category = (*Stratum)["Category"].value<std::string>();
        if (!Category || (*Category != "Opaque" && *Category != "Coating"))
            throw std::runtime_error("Unsupported scattering Category; supported: Opaque, Coating. Transmission/SSS are not implemented.");
        auto &Compiled = Material.Scattering.Illumination.Strata[Index];
        Compiled.Classification[0] = *Category == "Coating" ? 1 : 0;
        const auto Albedo = DecodeSpectrum<3>((*Stratum)["Albedo"],{.5f,.5f,.5f});
        std::copy(Albedo.begin(),Albedo.end(),Compiled.AlbedoRoughness.begin());
        Compiled.AlbedoRoughness[3] = DecodeCoefficient((*Stratum)["Roughness"],.4f);
        Compiled.Optics = {DecodeCoefficient((*Stratum)["Metallic"],0),DecodeCoefficient((*Stratum)["Ior"],1.5f),
                           DecodeCoefficient((*Stratum)["Coverage"],1),0};
        const auto Absorption = DecodeSpectrum<3>((*Stratum)["Absorption"],{0,0,0});
        std::copy(Absorption.begin(),Absorption.end(),Compiled.AbsorptionThickness.begin());
        Compiled.AbsorptionThickness[3] = DecodeCoefficient((*Stratum)["Thickness"],0);
        // 📝 Fabric sheen (fuzz rim): zero by default, so every non-fabric material stays bit-identical.
        const auto SheenColour = DecodeSpectrum<3>((*Stratum)["SheenColor"],{0,0,0});
        std::copy(SheenColour.begin(),SheenColour.end(),Compiled.Sheen.begin());
        Compiled.Sheen[3] = DecodeCoefficient((*Stratum)["SheenIntensity"],0);
        Compiled.SheenFinish[0] = DecodeCoefficient((*Stratum)["SheenRoughness"],0);
        const auto Mask = (*Stratum)["TextureMask"].value<int64_t>();
        if ((*Stratum).contains("TextureMask") && (!Mask || *Mask < 0 || *Mask > 15))
            throw std::runtime_error("TextureMask must be an integer within 0..15.");
        Material.Scattering.TextureUse[Index][0] = uint32_t(Mask.value_or(0));
        Material.Scattering.StratumUv[Index] = DecodeSpectrum<4>((*Stratum)["UvScaleOffset"],{1,1,0,0});
        Material.Scattering.StratumNormal[Index][0] = DecodeCoefficient((*Stratum)["NormalStrength"],1);
        Material.Scattering.StratumNormal[Index][1] = DecodeCoefficient((*Stratum)["HeightStrength"],1);
        auto &Directional = Material.Scattering.DirectionalOptics[Index];
        Directional[0] = DecodeCoefficient((*Stratum)["Anisotropy"],0);
        Directional[1] = DecodeCoefficient((*Stratum)["Rotation"],0);
        Directional[3] = DecodeCoefficient((*Stratum)["Specular"],.5f);
        if ((*Stratum).contains("Fresnel"))
        {
            const auto Fresnel = (*Stratum)["Fresnel"].value<std::string>();
            if (!Fresnel || (*Fresnel != "Schlick" && *Fresnel != "Dielectric"))
                throw std::runtime_error("Fresnel must be Schlick or Dielectric; exact conductor optical constants are not implemented.");
            Directional[2] = *Fresnel == "Dielectric" ? 1.f : 0.f;
        }
        if ((*Stratum).contains("Textures"))
        {
            const auto *Textures = (*Stratum)["Textures"].as_table();
            if (!Textures) throw std::runtime_error("Stratum Textures must be a table.");
            ValidateKeys(*Textures,{"Albedo","Orm","Normal","Radiance"});
            const std::array<const char *,4> Names{"Albedo","Orm","Normal","Radiance"};
            for (size_t Slot = 0; Slot < 4; ++Slot)
                if (Textures->contains(Names[Slot]))
                {
                    const auto Source = (*Textures)[Names[Slot]].value<std::string>();
                    if (!Source || Source->empty() || Source->size() > 512) throw std::runtime_error("Invalid stratum texture path.");
                    Material.StratumTextureSources[Index][Slot] = *Source;
                }
        }
        if ((*Stratum).contains("Channels"))
        {
            const auto *Channels = (*Stratum)["Channels"].as_array();
            if (!Channels || Channels->empty() || Channels->size() > 4) throw std::runtime_error("Stratum Channels must hold 1..4 entries.");
            for (const auto &ChannelTerm : *Channels)
            {
                const auto *Channel = ChannelTerm.as_table();
                if (!Channel) throw std::runtime_error("Stratum channel must be a table.");
                ValidateKeys(*Channel,{"Name","Op","Link","Params"});
                MaterialProceduralChannel Entry;
                const auto Name = (*Channel)["Name"].value<std::string>();
                if (!Name || (*Name != "Albedo" && *Name != "Orm" && *Name != "Normal" && *Name != "Radiance"))
                    throw std::runtime_error("Stratum channel Name must be Albedo, Orm, Normal or Radiance.");
                Entry.Name = *Name;
                for (const auto &Prior : Material.StratumChannels[Index])
                    if (Prior.Name == Entry.Name) throw std::runtime_error("Stratum channel Name is duplicated.");
                Entry.Op = (*Channel)["Op"].value_or(std::string{});
                if (Entry.Op.size() > 32) throw std::runtime_error("Stratum channel Op is unbounded.");
                Entry.Link = (*Channel)["Link"].value_or(std::string{});
                if (Entry.Link.empty()) Entry.Link = "shared";
                if (Entry.Link.size() > 32) throw std::runtime_error("Stratum channel Link is unbounded.");
                if (const auto *Params = (*Channel)["Params"].as_table())
                {
                    if (Params->size() > 64) throw std::runtime_error("Stratum channel Params are unbounded.");
                    for (const auto &[Key, Value] : *Params)
                    {
                        const std::string Param(Key.str());
                        if (Param.empty() || Param.size() > 64) throw std::runtime_error("Stratum channel param name is unbounded.");
                        if (Value.is_number() || Value.is_boolean())
                        {
                            const auto Scalar = Value.value<double>();
                            if (Scalar) Entry.Params[Param] = *Scalar;
                            else if (const auto Flag = Value.value<bool>()) Entry.Params[Param] = *Flag ? 1 : 0;
                            else throw std::runtime_error("Stratum channel param is not a scalar.");
                        }
                        else if (const auto Text = Value.value<std::string>()) Entry.Texts[Param] = *Text;
                        else throw std::runtime_error("Stratum channel param must be a scalar or text.");
                    }
                }
                else if ((*Channel).contains("Params")) throw std::runtime_error("Stratum channel Params must be a table.");
                Material.StratumChannels[Index].push_back(std::move(Entry));
            }
            for (const auto &Entry : Material.StratumChannels[Index])
            {
                if (Entry.Op.empty()) continue;
                const size_t Slot =
                    Entry.Name == "Albedo" ? 0 : Entry.Name == "Orm" ? 1 : Entry.Name == "Normal" ? 2 : 3;
                if (Material.StratumTextureSources[Index][Slot].empty())
                    Material.StratumTextureSources[Index][Slot] = EncodeProceduralSource(Index, Entry.Name);
                // 📝 Channel ops sample their baked maps: derive the mask bit so shipped procedurals
                // without an authored TextureMask still reach the shader (idempotent under round trip).
                Material.Scattering.TextureUse[Index][0] |= uint32_t(1u << Slot);
            }
        }
    }
    if (Table.contains("Authoring"))
    {
        const auto *Authoring = Table["Authoring"].as_table();
        if (!Authoring) throw std::runtime_error("Authoring must be a table.");
        Material.Authoring = DecodeMaterialAuthoring(*Authoring);
    }
    if (!ValidateMaterialScattering(Material.Scattering)) throw std::runtime_error("Material coefficients violate scattering limits.");
    return Material;
}

std::string EncodeProceduralSource(size_t Stratum, std::string_view Channel)
{
    std::string Key = "procedural/" + std::to_string(Stratum) + "/";
    for (char Byte : Channel) Key += char(std::tolower((unsigned char)Byte));
    return Key;
}

std::optional<MaterialPoolLocation> QueryProceduralPool(const MaterialDefinition &Material, std::string_view Key)
{
    const std::string Name(Key);
    for (size_t Index = 0; Index < Material.StratumChannels.size(); ++Index)
        for (const auto &Entry : Material.StratumChannels[Index])
        {
            if (Entry.Op.empty()) continue;
            const auto Value = Entry.Params.find(Name);
            if (Value != Entry.Params.end()) return MaterialPoolLocation{uint32_t(Index), Entry.Name, Value->second};
        }
    return std::nullopt;
}

std::string EncodeSurfaceMaterial(const MaterialDefinition &Material)
{
    if (!ValidateMaterialScattering(Material.Scattering) || Material.Name.empty() || Material.Name.size() > 128)
        throw std::runtime_error("Cannot encode invalid material.");
    toml::table Table{{"Format",1},{"Name",Material.Name},{"UvScaleOffset",EncodeSpectrum(Material.Scattering.UvScaleOffset)},
        {"Radiance",EncodeSpectrum<3>({Material.Scattering.RadianceNormal[0],Material.Scattering.RadianceNormal[1],Material.Scattering.RadianceNormal[2]})},
        {"NormalStrength",double(Material.Scattering.RadianceNormal[3])}};
    const auto &Paint = Material.Scattering.Paint;
    if (Paint.Configuration[3])
    {
        toml::array Palette;
        for (size_t Index = 0; Index < Paint.Configuration[3]; ++Index) Palette.push_back(EncodeSpectrum(Paint.Palette[Index]));
        Table.insert("AutomotivePaint", toml::table{{"Enabled", bool(Paint.Configuration[0])},
            {"Layers", int64_t(Paint.Configuration[1])}, {"ColourMode", Paint.Configuration[2] == 2 ? "Single" : Paint.Configuration[2] == 1 ? "Ramp" : "Palette"},
            {"Distribution", EncodeSpectrum(Paint.Distribution)}, {"Finish", EncodeSpectrum(Paint.Finish)},
            {"Relief", EncodeSpectrum(Paint.Relief)}, {"Palette", std::move(Palette)}});
    }
    toml::table Textures;
    const std::array<const char *,4> Names{"Albedo","Orm","Normal","Radiance"};
    for (size_t Index = 0; Index < 4; ++Index)
        if (!Material.TextureSources[Index].empty()) Textures.insert(Names[Index],Material.TextureSources[Index]);
    Table.insert("Textures",std::move(Textures));
    toml::array Strata;
    for (size_t Index = 0; Index < Material.Scattering.Illumination.Configuration[0]; ++Index)
    {
        const auto &Stratum = Material.Scattering.Illumination.Strata[Index];
        Strata.push_back(toml::table{{"Category",Stratum.Classification[0] ? "Coating" : "Opaque"},
            {"Albedo",EncodeSpectrum<3>({Stratum.AlbedoRoughness[0],Stratum.AlbedoRoughness[1],Stratum.AlbedoRoughness[2]})},
            {"Roughness",double(Stratum.AlbedoRoughness[3])},{"Metallic",double(Stratum.Optics[0])},
            {"Ior",double(Stratum.Optics[1])},{"Coverage",double(Stratum.Optics[2])},
            {"Absorption",EncodeSpectrum<3>({Stratum.AbsorptionThickness[0],Stratum.AbsorptionThickness[1],Stratum.AbsorptionThickness[2]})},
            {"Thickness",double(Stratum.AbsorptionThickness[3])},{"TextureMask",int64_t(Material.Scattering.TextureUse[Index][0])},
            {"SheenColor",EncodeSpectrum<3>({Stratum.Sheen[0],Stratum.Sheen[1],Stratum.Sheen[2]})},
            {"SheenIntensity",double(Stratum.Sheen[3])},{"SheenRoughness",double(Stratum.SheenFinish[0])}});
    }
    for (size_t Index = 0; Index < Material.Scattering.Illumination.Configuration[0]; ++Index)
    {
        auto &Stratum = *Strata.get(Index)->as_table();
        Stratum.insert("UvScaleOffset",EncodeSpectrum(Material.Scattering.StratumUv[Index]));
        Stratum.insert("NormalStrength",double(Material.Scattering.StratumNormal[Index][0]));
        Stratum.insert("Anisotropy",double(Material.Scattering.DirectionalOptics[Index][0]));
        Stratum.insert("Rotation",double(Material.Scattering.DirectionalOptics[Index][1]));
        Stratum.insert("Specular",double(Material.Scattering.DirectionalOptics[Index][3]));
        Stratum.insert("HeightStrength",double(Material.Scattering.StratumNormal[Index][1]));
        Stratum.insert("Fresnel",Material.Scattering.DirectionalOptics[Index][2] == 1 ? "Dielectric" : "Schlick");
        toml::table Textures;
        const std::array<const char *,4> Names{"Albedo","Orm","Normal","Radiance"};
        for (size_t Slot = 0; Slot < 4; ++Slot)
            if (!Material.StratumTextureSources[Index][Slot].empty() &&
                !Material.StratumTextureSources[Index][Slot].starts_with("procedural/"))
                Textures.insert(Names[Slot],Material.StratumTextureSources[Index][Slot]);
        if (!Textures.empty()) Stratum.insert("Textures",std::move(Textures));
        toml::array Channels;
        for (const auto &Entry : Material.StratumChannels[Index])
        {
            toml::table Params;
            for (const auto &[Key, Value] : Entry.Params) Params.insert(Key,Value);
            for (const auto &[Key, Value] : Entry.Texts) Params.insert(Key,Value);
            Channels.push_back(toml::table{{"Name",Entry.Name},{"Op",Entry.Op},{"Link",Entry.Link},{"Params",std::move(Params)}});
        }
        if (!Channels.empty()) Stratum.insert("Channels",std::move(Channels));
    }
    Table.insert("Strata",std::move(Strata));
    if (Material.Authoring) Table.insert("Authoring",EncodeMaterialAuthoring(*Material.Authoring));
    std::ostringstream Output;
    Output << toml::toml_formatter{Table};
    return Output.str();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     COLOUR-CORRECT TEXTURE PYRAMIDS
//------------------------------------------------------------------------------------------------------------------------

MaterialTexturePyramid ConstructFloatMaterialPyramid(uint32_t Width, uint32_t Height,
    MaterialTextureCategory Category, std::span<const float> FloatRgba)
{
    if (uint32_t(Category) > 3 || !Width || !Height || Width > 4096 || Height > 4096 ||
        FloatRgba.size() != size_t(Width) * Height * 4 ||
        !std::all_of(FloatRgba.begin(), FloatRgba.end(), [](float Sample) { return std::isfinite(Sample); }))
        throw std::runtime_error("Invalid Float32 material texture.");
    MaterialTexturePyramid Pyramid;
    Pyramid.Category = Category;
    Pyramid.Levels.push_back({Width, Height, 0});
    Pyramid.FloatRgba.assign(FloatRgba.begin(), FloatRgba.end());
    while (Width > 1 || Height > 1)
    {
        const auto Previous = Pyramid.Levels.back();
        const uint32_t Columns = std::max(1u, Width / 2), Rows = std::max(1u, Height / 2);
        const size_t Start = Pyramid.FloatRgba.size();
        Pyramid.Levels.push_back({Columns, Rows, Start * sizeof(float)});
        Pyramid.FloatRgba.resize(Start + size_t(Columns) * Rows * 4);
        if (Width == Columns * 2 && Height == Rows * 2)
        {
            // 📝 Exact-halving float box: unit area weights make this mathematically identical to the
            // generic integral, so Float32 authoring mip chains build an order of magnitude faster with
            // only float-rounding divergence (far inside golden tolerances).
            const float *Source = Pyramid.FloatRgba.data() + Previous.Offset / sizeof(float);
            float *Target = Pyramid.FloatRgba.data() + Start;
            const bool Renormalize = Category == MaterialTextureCategory::TangentNormal;
            for (uint32_t Row = 0; Row < Rows; ++Row)
                for (uint32_t Column = 0; Column < Columns; ++Column)
                {
                    const size_t Base = (size_t(Row) * 2 * Width + size_t(Column) * 2) * 4;
                    float Spectrum[4]{};
                    for (size_t Tap = 0; Tap < 4; ++Tap)
                    {
                        const size_t Pixel = Base + (Tap / 2) * size_t(Width) * 4 + (Tap % 2) * 4;
                        for (size_t Channel = 0; Channel < 4; ++Channel)
                        {
                            float Coefficient = Source[Pixel + Channel];
                            if (Renormalize && Channel < 3) Coefficient = Coefficient * 2 - 1;
                            Spectrum[Channel] += Coefficient;
                        }
                    }
                    for (size_t Channel = 0; Channel < 4; ++Channel) Spectrum[Channel] *= .25f;
                    if (Renormalize)
                    {
                        const float Length = std::sqrt(Spectrum[0] * Spectrum[0] + Spectrum[1] * Spectrum[1] +
                            Spectrum[2] * Spectrum[2]);
                        for (size_t Channel = 0; Channel < 3; ++Channel)
                            Spectrum[Channel] = (Length > 1e-8f ? Spectrum[Channel] / Length : float(Channel == 2)) * .5f + .5f;
                    }
                    float *Pixel = Target + (size_t(Row) * Columns + Column) * 4;
                    for (size_t Channel = 0; Channel < 4; ++Channel) Pixel[Channel] = Spectrum[Channel];
                }
            Width = Columns;
            Height = Rows;
            continue;
        }
        for (uint32_t Row = 0; Row < Rows; ++Row)
            for (uint32_t Column = 0; Column < Columns; ++Column)
            {
                std::array<double, 4> Spectrum{};
                double Total = 0;
                const double Left = double(Column) * Width / Columns, Right = double(Column + 1) * Width / Columns;
                const double Top = double(Row) * Height / Rows, Bottom = double(Row + 1) * Height / Rows;
                for (uint32_t Vertical = uint32_t(Top); Vertical < uint32_t(std::ceil(Bottom)); ++Vertical)
                    for (uint32_t Horizontal = uint32_t(Left); Horizontal < uint32_t(std::ceil(Right)); ++Horizontal)
                    {
                        const double Weight = (std::min(Right, double(Horizontal + 1)) - std::max(Left, double(Horizontal))) *
                                              (std::min(Bottom, double(Vertical + 1)) - std::max(Top, double(Vertical)));
                        const size_t Pixel = Previous.Offset / sizeof(float) + (size_t(Vertical) * Width + Horizontal) * 4;
                        for (size_t Channel = 0; Channel < 4; ++Channel)
                        {
                            double Coefficient = Pyramid.FloatRgba[Pixel + Channel];
                            if (Channel < 3 && Category == MaterialTextureCategory::TangentNormal) Coefficient = Coefficient * 2 - 1;
                            Spectrum[Channel] += Coefficient * Weight;
                        }
                        Total += Weight;
                    }
                for (auto &Channel : Spectrum) Channel /= Total;
                if (Category == MaterialTextureCategory::TangentNormal)
                {
                    const double Length = std::sqrt(Spectrum[0] * Spectrum[0] + Spectrum[1] * Spectrum[1] + Spectrum[2] * Spectrum[2]);
                    for (size_t Channel = 0; Channel < 3; ++Channel)
                        Spectrum[Channel] = (Length > 1e-8 ? Spectrum[Channel] / Length : double(Channel == 2)) * .5 + .5;
                }
                const size_t Pixel = Start + (size_t(Row) * Columns + Column) * 4;
                for (size_t Channel = 0; Channel < 4; ++Channel)
                    Pyramid.FloatRgba[Pixel + Channel] = float(Spectrum[Channel]);
            }
        Width = Columns;
        Height = Rows;
    }
    if (!ValidateMaterialPyramid(Pyramid)) throw std::runtime_error("Float32 material mip chain failed validation.");
    return Pyramid;
}

std::array<MaterialTexturePyramid,6> ConstructAuthoringMaterialMaps(uint32_t Width,uint32_t Height,
    const std::array<std::span<const float>,6> &FloatRgba)
{
    constexpr std::array<MaterialTextureCategory,6> Categories{
        MaterialTextureCategory::Albedo,MaterialTextureCategory::TangentNormal,
        MaterialTextureCategory::OcclusionRoughnessMetallic,MaterialTextureCategory::OcclusionRoughnessMetallic,
        MaterialTextureCategory::OcclusionRoughnessMetallic,MaterialTextureCategory::OcclusionRoughnessMetallic};
    std::array<MaterialTexturePyramid,6> Pyramids;
    for (size_t Index = 0; Index < Pyramids.size(); ++Index)
        Pyramids[Index] = ConstructFloatMaterialPyramid(Width,Height,Categories[Index],FloatRgba[Index]);
    return Pyramids;
}

MaterialTexturePyramid ConstructMaterialPyramid(const SurfaceTextureImage &Image,MaterialTextureCategory Category)
{
    if (!ValidateSurfaceTextureImage(Image) || uint32_t(Category) > 3) throw std::runtime_error("Invalid material texture image.");
    struct SrgbMipTables
    {
        std::array<float, 256> Decode{};
        std::array<uint8_t, 65536> Encode{};
        SrgbMipTables()
        {
            for (size_t Byte = 0; Byte < 256; ++Byte) Decode[Byte] = float(DecodeSrgb(double(Byte) / 255.0));
            for (size_t Step = 0; Step < 65536; ++Step)
                Encode[Step] = uint8_t(std::lround(std::clamp(EncodeSrgb(double(Step) / 65535.0), 0.0, 1.0) * 255.0));
        }
    };
    static const SrgbMipTables Tables;

    const bool IsSrgb = Category == MaterialTextureCategory::Albedo || Category == MaterialTextureCategory::Radiance;
    const bool IsNormal = Category == MaterialTextureCategory::TangentNormal;
    MaterialTexturePyramid Pyramid{Category,Image.Rgba,{{Image.Width,Image.Height,0}}};
    // Reserve full mip chain capacity upfront (~4/3 * base bytes) to avoid repeated reallocations.
    Pyramid.Rgba.reserve(Image.Rgba.size() + Image.Rgba.size() / 2 + 64);

    uint32_t Width = Image.Width, Height = Image.Height;
    while (Width > 1 || Height > 1)
    {
        const auto Previous = Pyramid.Levels.back();
        const uint32_t Columns = std::max(1u,Width/2), Rows = std::max(1u,Height/2);
        const size_t Start = Pyramid.Rgba.size();
        Pyramid.Levels.push_back({Columns,Rows,Start});
        Pyramid.Rgba.resize(Start+size_t(Columns)*Rows*4);
        const uint8_t *SrcBase = Pyramid.Rgba.data() + Previous.Offset;
        uint8_t *DstBase = Pyramid.Rgba.data() + Start;

        if (Width == Columns * 2 && Height == Rows * 2)
        {
            // Fast exact 2x2 downsample path for even mip dimensions (1024->512->...->1).
            for (uint32_t Row = 0; Row < Rows; ++Row)
            {
                const uint8_t *Row0 = SrcBase + size_t(Row * 2) * Width * 4;
                const uint8_t *Row1 = Row0 + size_t(Width) * 4;
                uint8_t *Out = DstBase + size_t(Row) * Columns * 4;
                if (IsSrgb)
                {
                    for (uint32_t Column = 0; Column < Columns; ++Column, Row0 += 8, Row1 += 8, Out += 4)
                    {
                        const float R = (Tables.Decode[Row0[0]] + Tables.Decode[Row0[4]] + Tables.Decode[Row1[0]] + Tables.Decode[Row1[4]]) * .25f;
                        const float G = (Tables.Decode[Row0[1]] + Tables.Decode[Row0[5]] + Tables.Decode[Row1[1]] + Tables.Decode[Row1[5]]) * .25f;
                        const float B = (Tables.Decode[Row0[2]] + Tables.Decode[Row0[6]] + Tables.Decode[Row1[2]] + Tables.Decode[Row1[6]]) * .25f;
                        Out[0] = Tables.Encode[size_t(std::clamp(R, 0.f, 1.f) * 65535.f + .5f)];
                        Out[1] = Tables.Encode[size_t(std::clamp(G, 0.f, 1.f) * 65535.f + .5f)];
                        Out[2] = Tables.Encode[size_t(std::clamp(B, 0.f, 1.f) * 65535.f + .5f)];
                        Out[3] = uint8_t((uint32_t(Row0[3]) + Row0[7] + Row1[3] + Row1[7] + 2u) >> 2);
                    }
                }
                else if (IsNormal)
                {
                    constexpr float Inv255 = 2.f / 255.f;
                    for (uint32_t Column = 0; Column < Columns; ++Column, Row0 += 8, Row1 += 8, Out += 4)
                    {
                        float Nx = (float(uint32_t(Row0[0]) + Row0[4] + Row1[0] + Row1[4]) * Inv255 - 4.f) * .25f;
                        float Ny = (float(uint32_t(Row0[1]) + Row0[5] + Row1[1] + Row1[5]) * Inv255 - 4.f) * .25f;
                        float Nz = (float(uint32_t(Row0[2]) + Row0[6] + Row1[2] + Row1[6]) * Inv255 - 4.f) * .25f;
                        const float LenSq = Nx * Nx + Ny * Ny + Nz * Nz;
                        if (LenSq > 1e-16f)
                        {
                            const float InvLen = 1.f / std::sqrt(LenSq);
                            Nx *= InvLen; Ny *= InvLen; Nz *= InvLen;
                        }
                        else { Nx = 0.f; Ny = 0.f; Nz = 1.f; }
                        Out[0] = uint8_t(std::clamp((Nx * .5f + .5f) * 255.f + .5f, 0.f, 255.f));
                        Out[1] = uint8_t(std::clamp((Ny * .5f + .5f) * 255.f + .5f, 0.f, 255.f));
                        Out[2] = uint8_t(std::clamp((Nz * .5f + .5f) * 255.f + .5f, 0.f, 255.f));
                        Out[3] = uint8_t((uint32_t(Row0[3]) + Row0[7] + Row1[3] + Row1[7] + 2u) >> 2);
                    }
                }
                else
                {
                    for (uint32_t Column = 0; Column < Columns; ++Column, Row0 += 8, Row1 += 8, Out += 4)
                    {
                        Out[0] = uint8_t((uint32_t(Row0[0]) + Row0[4] + Row1[0] + Row1[4] + 2u) >> 2);
                        Out[1] = uint8_t((uint32_t(Row0[1]) + Row0[5] + Row1[1] + Row1[5] + 2u) >> 2);
                        Out[2] = uint8_t((uint32_t(Row0[2]) + Row0[6] + Row1[2] + Row1[6] + 2u) >> 2);
                        Out[3] = uint8_t((uint32_t(Row0[3]) + Row0[7] + Row1[3] + Row1[7] + 2u) >> 2);
                    }
                }
            }
            Width = Columns;
            Height = Rows;
            continue;
        }

        for (uint32_t Row = 0; Row < Rows; ++Row)
            for (uint32_t Column = 0; Column < Columns; ++Column)
            {
                std::array<float,4> Spectrum{};
                float Total = 0;
                const float Left = float(Column)*Width/Columns, Right = float(Column+1)*Width/Columns;
                const float Top = float(Row)*Height/Rows, Bottom = float(Row+1)*Height/Rows;
                for (uint32_t Vertical = uint32_t(Top); Vertical < uint32_t(std::ceil(Bottom)); ++Vertical)
                    for (uint32_t Horizontal = uint32_t(Left); Horizontal < uint32_t(std::ceil(Right)); ++Horizontal)
                    {
                        const float Weight = (std::min(Right,float(Horizontal+1))-std::max(Left,float(Horizontal)))*
                                             (std::min(Bottom,float(Vertical+1))-std::max(Top,float(Vertical)));
                        const uint8_t *Px = SrcBase + (size_t(Vertical)*Width+Horizontal)*4;
                        for (size_t Channel = 0; Channel < 4; ++Channel)
                        {
                            float Coefficient = Channel < 3 && IsSrgb ? Tables.Decode[Px[Channel]] : Px[Channel] * (1.f / 255.f);
                            if (Channel < 3 && IsNormal) Coefficient = Coefficient*2.f-1.f;
                            Spectrum[Channel] += Coefficient*Weight;
                        }
                        Total += Weight;
                    }
                const float InvTotal = 1.f / Total;
                for (auto &Channel : Spectrum) Channel *= InvTotal;
                if (IsNormal)
                {
                    const float Length = std::sqrt(Spectrum[0]*Spectrum[0]+Spectrum[1]*Spectrum[1]+Spectrum[2]*Spectrum[2]);
                    for (size_t Channel = 0; Channel < 3; ++Channel)
                        Spectrum[Channel] = (Length > 1e-8f ? Spectrum[Channel]/Length : float(Channel == 2))*.5f+.5f;
                }
                uint8_t *Out = DstBase + (size_t(Row)*Columns+Column)*4;
                for (size_t Channel = 0; Channel < 4; ++Channel)
                {
                    const float Clamped = std::clamp(Spectrum[Channel], 0.f, 1.f);
                    Out[Channel] = (Channel < 3 && IsSrgb)
                        ? Tables.Encode[size_t(Clamped * 65535.f + .5f)]
                        : uint8_t(Clamped * 255.f + .5f);
                }
            }
        Width = Columns;
        Height = Rows;
    }
    return Pyramid;
}

void EnsureProceduralChannelBakes(MaterialDefinition &Material, uint32_t Extent)
{
    if (!Extent || Extent > 1024) return;
    const size_t Count = Material.Scattering.Illumination.Configuration[0];
    for (size_t Index = 0; Index < Count && Index < 8; ++Index)
        for (const auto &Entry : Material.StratumChannels[Index])
        {
            if (Entry.Op.empty()) continue;
            const size_t Slot =
                Entry.Name == "Albedo" ? 0 : Entry.Name == "Orm" ? 1 : Entry.Name == "Normal" ? 2 : 3;
            const std::string Derived = EncodeProceduralSource(Index, Entry.Name);
            const std::string &Live = Material.StratumTextureSources[Index][Slot].empty() ?
                Material.TextureSources[Slot] : Material.StratumTextureSources[Index][Slot];
            const bool LiveIsProcedural = Live.empty() || Live == Derived || Live.rfind("procedural/", 0) == 0;
            if (!LiveIsProcedural) continue; // File-backed: compile loads full resolution from disk; a proxy must not shadow it.
            const bool DerivedReady = Material.ProceduralMaps.find({Slot, Derived}) != Material.ProceduralMaps.end();
            const bool LiveReady = Live == Derived ||
                Material.ProceduralMaps.find({Slot, Live}) != Material.ProceduralMaps.end();
            if (DerivedReady && LiveReady) continue;
            try
            {
                auto Rgba = BakeProceduralChannelRgba(Entry.Op, Entry.Name, Entry.Params, Entry.Texts, Extent);
                SurfaceTextureImage BakedImage{Extent, Extent, std::move(Rgba)};
                Material.ProceduralMaps[{Slot, Derived}] = BakedImage;
                if (!Live.empty() && Live != Derived) Material.ProceduralMaps[{Slot, Live}] = std::move(BakedImage);
            }
            catch (const std::exception &Failure)
            {
                DiagnosticJournal::Append("ProceduralBake", "Channel bake skipped (" + Entry.Op + "/" +
                    Entry.Name + "): " + Failure.what());
            }
        }
}

CompiledSurfaceMaterial CompileSurfaceMaterial(const MaterialDefinition &Material,const std::filesystem::path &ContentDirectory)
{
    if (!ValidateMaterialScattering(Material.Scattering)) throw std::runtime_error("Material scattering cannot be compiled.");
    CompiledSurfaceMaterial Compiled{Material.Name,Material.Scattering,{},{}};
    auto &Paint = Compiled.Scattering.Paint;
    const std::array<float,4> NeutralColour{1,1,1,0}, NeutralNormal{.5f,.5f,1,0};
    const std::array<float,4> NeutralRoughness{1,0,0,0}, NeutralMetalness{0,0,0,0}, NeutralOcclusion{1,0,0,0};
    Compiled.AuthoringMaps = {
        ConstructFloatMaterialPyramid(1,1,MaterialTextureCategory::Albedo,NeutralColour),
        ConstructFloatMaterialPyramid(1,1,MaterialTextureCategory::TangentNormal,NeutralNormal),
        ConstructFloatMaterialPyramid(1,1,MaterialTextureCategory::OcclusionRoughnessMetallic,NeutralRoughness),
        ConstructFloatMaterialPyramid(1,1,MaterialTextureCategory::OcclusionRoughnessMetallic,NeutralRoughness),
        ConstructFloatMaterialPyramid(1,1,MaterialTextureCategory::OcclusionRoughnessMetallic,NeutralMetalness),
        ConstructFloatMaterialPyramid(1,1,MaterialTextureCategory::OcclusionRoughnessMetallic,NeutralOcclusion)};
    if (Material.Authoring) EvaluateAuthoringLayerStack(Material, *Material.Authoring, Compiled);
    Paint.AverageTint = {};
    if (Paint.Configuration[0])
        for (uint32_t Sample = 0; Sample < 48; ++Sample)
        {
            const float Position = (float(Sample) + .5f) / 48;
            auto Spectrum = Paint.Palette[0];
            if (Paint.Configuration[2] == 0) Spectrum = Paint.Palette[std::min(Paint.Configuration[3] - 1, uint32_t(Position * Paint.Configuration[3]))];
            else if (Paint.Configuration[2] == 1)
                for (size_t Index = 1; Index < Paint.Configuration[3]; ++Index)
                {
                    const float Blend = std::clamp((Position - Paint.Palette[Index - 1][3]) /
                        std::max(.0001f, Paint.Palette[Index][3] - Paint.Palette[Index - 1][3]), 0.f, 1.f);
                    for (size_t Channel = 0; Channel < 3; ++Channel) Spectrum[Channel] = std::lerp(Spectrum[Channel], Paint.Palette[Index][Channel], Blend);
                }
            for (size_t Channel = 0; Channel < 3; ++Channel) Paint.AverageTint[Channel] += Spectrum[Channel] / 48;
        }
    const std::array<std::array<uint8_t,4>,4> Defaults{{{255,255,255,255},{255,255,255,255},{128,128,255,255},{255,255,255,255}}};
    const auto Directory = std::filesystem::weakly_canonical(ContentDirectory);
    const auto DecodeTexture = [&](const std::string &Source,size_t Slot)
    {
        SurfaceTextureImage Image{1,1,{Defaults[Slot].begin(),Defaults[Slot].end()}};
        const auto Override = Material.ProceduralMaps.find({Slot,Source});
        if (Override != Material.ProceduralMaps.end())
        {
            const auto &Memory = Override->second;
            if (!Memory.Width || !Memory.Height || Memory.Width > 4096 || Memory.Height > 4096 ||
                Memory.Rgba.size() != size_t(Memory.Width) * Memory.Height * 4)
                throw std::runtime_error("Procedural rebake produced an invalid map: "+Source);
            Image = Memory;
        }
        else if (!Source.empty() && Source.starts_with("procedural/"))
        {
            // 📝 A procedural file source without a rebake override means the bake never ran (or failed);
            // keep the neutral map instead of refusing the whole material.
            DiagnosticJournal::Append("ProceduralBake", "Unbaked procedural source falls back to neutral: " + Source);
        }
        else if (!Source.empty())
        {
            const std::filesystem::path Relative(Source);
            if (Relative.is_absolute() || Source.find(':') != std::string::npos || Source.find('\\') != std::string::npos)
                throw std::runtime_error("Material texture paths must be portable ContentDirectory-relative paths.");
            const auto Path = std::filesystem::weakly_canonical(Directory/Relative);
            const auto Within = Path.lexically_relative(Directory);
            if (Within.empty() || *Within.begin() == "..") throw std::runtime_error("Material texture escapes ContentDirectory.");
            std::ifstream Input(Path,std::ios::binary | std::ios::ate);
            const auto Bytes = Input.tellg();
            if (!Input || Bytes <= 0 || Bytes > 67108864) throw std::runtime_error("Missing/oversized material texture: "+Source);
            std::vector<uint8_t> Encoded(static_cast<size_t>(Bytes));
            Input.seekg(0);
            Input.read(reinterpret_cast<char *>(Encoded.data()),Bytes);
            int Width = 0, Height = 0, Channels = 0;
            if (!Input || !stbi_info_from_memory(Encoded.data(),int(Bytes),&Width,&Height,&Channels) ||
                Width < 1 || Height < 1 || Width > 4096 || Height > 4096)
                throw std::runtime_error("Unsupported/oversized PNG or JPEG material texture: "+Source);
            std::unique_ptr<stbi_uc,decltype(&stbi_image_free)> Pixels(stbi_load_from_memory(Encoded.data(),int(Bytes),&Width,&Height,&Channels,4),stbi_image_free);
            if (!Pixels) throw std::runtime_error("Material texture decode failed: "+Source);
            Image = {uint32_t(Width),uint32_t(Height),std::vector<uint8_t>(Pixels.get(),Pixels.get()+size_t(Width)*Height*4)};
        }
        return ConstructMaterialPyramid(Image,MaterialTextureCategory(Slot));
    };
    size_t TotalBytes = 0;
    size_t ResidentBytes = 0;
    constexpr size_t MaximumBytes = 268435456;
    for (size_t Slot = 0; Slot < 4; ++Slot)
    {
        std::vector<std::string> Sources;
        std::vector<MaterialTexturePyramid> Slices;
        const size_t Count = Slot == 3 ? 1 : Material.Scattering.Illumination.Configuration[0];
        for (size_t Index = 0; Index < Count; ++Index)
        {
            const auto &Source = Slot < 4 && !Material.StratumTextureSources[Index][Slot].empty() ?
                Material.StratumTextureSources[Index][Slot] : Material.TextureSources[Slot];
            auto Position = std::find(Sources.begin(),Sources.end(),Source);
            size_t Slice = size_t(Position-Sources.begin());
            if (Position == Sources.end())
            {
                auto Texture = DecodeTexture(Source,Slot);
                if (Texture.Rgba.size() > MaximumBytes-TotalBytes) throw std::runtime_error("Material texture compilation exceeds 256 MiB.");
                TotalBytes += Texture.Rgba.size();
                Sources.push_back(Source);
                Slices.push_back(std::move(Texture));
            }
            if (Slot < 3) Compiled.Scattering.TextureUse[Index][Slot+1] = uint32_t(Slice);
        }
        uint32_t Width = 1, Height = 1;
        for (const auto &Slice : Slices)
        {
            Width = std::max(Width,Slice.Levels[0].Width);
            Height = std::max(Height,Slice.Levels[0].Height);
        }
        for (const auto &Slice : Slices)
            if ((Slice.Levels[0].Width != Width || Slice.Levels[0].Height != Height) &&
                (Slice.Levels[0].Width != 1 || Slice.Levels[0].Height != 1))
                throw std::runtime_error("Stratum textures in each slot must share dimensions; neutral 1x1 images may expand.");
        MaterialTexturePyramid Combined;
        Combined.Category = MaterialTextureCategory(Slot);
        Combined.Layers = uint32_t(Slices.size());
        size_t RequiredBytes = 0;
        for (uint32_t Columns = Width, Rows = Height;; Columns = std::max(1u,Columns/2), Rows = std::max(1u,Rows/2))
        {
            RequiredBytes += size_t(Columns)*Rows*4*Slices.size();
            if (Columns == 1 && Rows == 1) break;
        }
        if (RequiredBytes > MaximumBytes-ResidentBytes)
            throw std::runtime_error("Compiled material texture arrays exceed 256 MiB.");
        Combined.Rgba.reserve(RequiredBytes);
        size_t Level = 0;
        while (true)
        {
            Combined.Levels.push_back({Width,Height,Combined.Rgba.size()});
            const size_t Bytes = size_t(Width)*Height*4;
            if (Combined.Rgba.size()+Bytes*Slices.size() > MaximumBytes)
                throw std::runtime_error("Material array texture exceeds 256 MiB.");
            for (const auto &Slice : Slices)
            {
                if (Slice.Levels[0].Width == 1 && Slice.Levels[0].Height == 1)
                    for (size_t Pixel = 0; Pixel < Bytes/4; ++Pixel)
                        Combined.Rgba.insert(Combined.Rgba.end(),Slice.Rgba.begin(),Slice.Rgba.begin()+4);
                else
                {
                    const auto Start = Slice.Rgba.begin()+Slice.Levels[Level].Offset;
                    Combined.Rgba.insert(Combined.Rgba.end(),Start,Start+Bytes);
                }
            }
            if (Width == 1 && Height == 1) break;
            Width = std::max(1u,Width/2);
            Height = std::max(1u,Height/2);
            ++Level;
        }
        if (Combined.Rgba.size() > MaximumBytes-ResidentBytes)
            throw std::runtime_error("Compiled material texture arrays exceed 256 MiB.");
        ResidentBytes += Combined.Rgba.size();
        Compiled.Textures[Slot] = std::move(Combined);
    }
    return Compiled;
}
} // namespace Frontier
