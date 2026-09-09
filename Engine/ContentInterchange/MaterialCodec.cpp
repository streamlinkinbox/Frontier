//============================================================================================================================================
//                                                       MATERIALCODEC.CPP
//============================================================================================================================================
// 🧩 glTF / FBX / OBJ material → MaterialDescriptor, descriptor → glTF JSON, and the `extras.slate_*` round trip.

#include "MaterialCodec.h"

#include <cgltf.h>
#include <ufbx.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace Frontier {

namespace {

//------------------------------------------------------------------------------------------------------------------------
//                                          OPENPBR PARAMETER TABLE (drives extras encode / decode)
//------------------------------------------------------------------------------------------------------------------------

struct ParameterEntry { const char* Identifier; size_t Offset; uint8_t Components; };
#define SLATE_PARAM(Identifier, Field, N) { Identifier, offsetof(MaterialSlabDescriptor, Field), N }
constexpr ParameterEntry kParameters[] = {
    SLATE_PARAM("base_weight", BaseWeight, 1), SLATE_PARAM("base_color", BaseColor, 3), SLATE_PARAM("base_metalness", BaseMetalness, 1),
    SLATE_PARAM("base_diffuse_roughness", BaseDiffuseRoughness, 1),
    SLATE_PARAM("specular_weight", SpecularWeight, 1), SLATE_PARAM("specular_color", SpecularColor, 3), SLATE_PARAM("specular_roughness", SpecularRoughness, 1),
    SLATE_PARAM("specular_roughness_anisotropy", SpecularRoughnessAnisotropy, 1), SLATE_PARAM("specular_ior", SpecularIor, 1),
    SLATE_PARAM("transmission_weight", TransmissionWeight, 1), SLATE_PARAM("transmission_color", TransmissionColor, 3), SLATE_PARAM("transmission_depth", TransmissionDepth, 1),
    SLATE_PARAM("transmission_scatter", TransmissionScatter, 3), SLATE_PARAM("transmission_scatter_anisotropy", TransmissionScatterAnisotropy, 1),
    SLATE_PARAM("transmission_dispersion_scale", TransmissionDispersionScale, 1), SLATE_PARAM("transmission_dispersion_abbe_number", TransmissionDispersionAbbeNumber, 1),
    SLATE_PARAM("subsurface_weight", SubsurfaceWeight, 1), SLATE_PARAM("subsurface_color", SubsurfaceColor, 3), SLATE_PARAM("subsurface_radius", SubsurfaceRadius, 1),
    SLATE_PARAM("subsurface_radius_scale", SubsurfaceRadiusScale, 3), SLATE_PARAM("subsurface_scatter_anisotropy", SubsurfaceScatterAnisotropy, 1),
    SLATE_PARAM("coat_weight", CoatWeight, 1), SLATE_PARAM("coat_color", CoatColor, 3), SLATE_PARAM("coat_roughness", CoatRoughness, 1),
    SLATE_PARAM("coat_roughness_anisotropy", CoatRoughnessAnisotropy, 1), SLATE_PARAM("coat_ior", CoatIor, 1), SLATE_PARAM("coat_darkening", CoatDarkening, 1),
    SLATE_PARAM("fuzz_weight", FuzzWeight, 1), SLATE_PARAM("fuzz_color", FuzzColor, 3), SLATE_PARAM("fuzz_roughness", FuzzRoughness, 1),
    SLATE_PARAM("emission_luminance", EmissionLuminance, 1), SLATE_PARAM("emission_color", EmissionColor, 3),
    SLATE_PARAM("thin_film_weight", ThinFilmWeight, 1), SLATE_PARAM("thin_film_thickness", ThinFilmThickness, 1), SLATE_PARAM("thin_film_ior", ThinFilmIor, 1),
    SLATE_PARAM("geometry_opacity", GeometryOpacity, 1),
    SLATE_PARAM("slate_haziness_weight", SlateHazinessWeight, 1), SLATE_PARAM("slate_haziness_roughness", SlateHazinessRoughness, 1),
    SLATE_PARAM("slate_glint_density", SlateGlintDensity, 1), SLATE_PARAM("slate_glint_uv_scale", SlateGlintUvScale, 1),
};
#undef SLATE_PARAM

// Parameters core glTF + the KHR extensions can carry (EncodeGltf writes them natively). Everything else in kParameters
//    that differs from the OpenPBR default rides in extras as a flat "identifier": value member (R4b: previously only
//    slate_* did, which silently dropped base_diffuse_roughness / coat_ior / coat_darkening / coat_color / …).
constexpr const char* kGltfCoveredParameters[] = {
    "base_color", "base_metalness", "specular_weight", "specular_color", "specular_roughness", "specular_roughness_anisotropy", "specular_ior",
    "transmission_weight", "transmission_color", "transmission_depth", "transmission_dispersion_scale", "transmission_dispersion_abbe_number",
    "coat_weight", "coat_roughness", "fuzz_weight", "fuzz_color", "fuzz_roughness", "emission_luminance", "emission_color",
    "thin_film_weight", "thin_film_thickness", "thin_film_ior", "geometry_opacity",
};
bool IsGltfCovered(const char* Identifier)
{
    for (const char* C : kGltfCoveredParameters) if (std::strcmp(C, Identifier) == 0) return true;
    return false;
}

constexpr const char* kTextureChannelNames[kMaterialTextureChannelCount] = {
    "base_color", "metalness", "specular_roughness", "specular_color", "geometry_normal", "geometry_coat_normal", "emission", "geometry_opacity",
    "transmission", "subsurface", "coat", "fuzz", "thin_film", "anisotropy", "occlusion", "mask" };
constexpr const char* kOperationNames[] = { "VerticalLayer", "HorizontalMix", "Weight", "Coverage" };
constexpr const char* kChannelSelectionNames[] = { "rgb", "r", "g", "b", "a" };

float*       Param(MaterialSlabDescriptor& S, const ParameterEntry& E)       { return reinterpret_cast<float*>(reinterpret_cast<char*>(&S) + E.Offset); }
const float* Param(const MaterialSlabDescriptor& S, const ParameterEntry& E) { return reinterpret_cast<const float*>(reinterpret_cast<const char*>(&S) + E.Offset); }

std::string Number(float F)
{
    char Buffer[32];
    std::snprintf(Buffer, sizeof(Buffer), "%.9g", static_cast<double>(F));
    std::string S = Buffer;
    if (S.find_first_of(".einf") == std::string::npos) S += ".0";
    return S;
}

std::string Escape(const std::string& In)
{
    std::string Out; Out.reserve(In.size() + 2u);
    for (char C : In) { if (C == '"' || C == '\\') Out.push_back('\\'); if (static_cast<unsigned char>(C) < 0x20) { Out += ' '; continue; } Out.push_back(C); }
    return Out;
}

//------------------------------------------------------------------------------------------------------------------------
//                                    MINIMAL JSON READER (for extras only — cgltf keeps extras as raw text)
//------------------------------------------------------------------------------------------------------------------------

struct JsonValue
{
    enum Category : uint8_t { Null, Bool, Number, String, Array, Object } Kind = Null;
    double                                          Numeric = 0.0;
    bool                                            Truth   = false;
    std::string                                     Text;
    std::vector<JsonValue>                          Items;
    std::vector<std::pair<std::string, JsonValue>>  Members;

    [[nodiscard]] const JsonValue* Find(const char* Key) const noexcept
    {
        for (const auto& M : Members) if (M.first == Key) return &M.second;
        return nullptr;
    }
    [[nodiscard]] float Scalar(float Fallback) const noexcept { return Kind == Number ? static_cast<float>(Numeric) : Fallback; }
};

struct JsonReader
{
    const char* P; const char* End;
    void Skip() { while (P < End && (*P == ' ' || *P == '\t' || *P == '\n' || *P == '\r')) ++P; }
    bool Parse(JsonValue& Out, int Depth = 0)
    {
        if (Depth > 32) return false;
        Skip(); if (P >= End) return false;
        if (*P == '{')
        {
            Out.Kind = JsonValue::Object; ++P; Skip();
            if (P < End && *P == '}') { ++P; return true; }
            for (;;)
            {
                JsonValue Key; if (!Parse(Key, Depth + 1) || Key.Kind != JsonValue::String) return false;
                Skip(); if (P >= End || *P != ':') return false; ++P;
                JsonValue Value; if (!Parse(Value, Depth + 1)) return false;
                Out.Members.emplace_back(std::move(Key.Text), std::move(Value));
                Skip(); if (P >= End) return false;
                if (*P == ',') { ++P; continue; }
                if (*P == '}') { ++P; return true; }
                return false;
            }
        }
        if (*P == '[')
        {
            Out.Kind = JsonValue::Array; ++P; Skip();
            if (P < End && *P == ']') { ++P; return true; }
            for (;;)
            {
                JsonValue Value; if (!Parse(Value, Depth + 1)) return false;
                Out.Items.push_back(std::move(Value));
                Skip(); if (P >= End) return false;
                if (*P == ',') { ++P; continue; }
                if (*P == ']') { ++P; return true; }
                return false;
            }
        }
        if (*P == '"')
        {
            Out.Kind = JsonValue::String; ++P;
            while (P < End && *P != '"') { if (*P == '\\' && P + 1 < End) ++P; Out.Text.push_back(*P++); }
            if (P >= End) return false;
            ++P; return true;
        }
        if (std::strncmp(P, "true", 4) == 0)  { Out.Kind = JsonValue::Bool; Out.Truth = true;  P += 4; return true; }
        if (std::strncmp(P, "false", 5) == 0) { Out.Kind = JsonValue::Bool; Out.Truth = false; P += 5; return true; }
        if (std::strncmp(P, "null", 4) == 0)  { Out.Kind = JsonValue::Null; P += 4; return true; }
        char* Stop = nullptr;
        Out.Numeric = std::strtod(P, &Stop);
        if (Stop == P) return false;
        Out.Kind = JsonValue::Number; P = Stop; return true;
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  TEXTURE REFERENCE HELPERS
//------------------------------------------------------------------------------------------------------------------------

TextureReference FromGltfView(const cgltf_texture_view& View, bool Linear, TextureChannelSelection Channel, const GltfTextureResolver& Resolve)
{
    TextureReference R;
    if (!View.texture || !Resolve) return R;
    R.Texture = Resolve(View, Linear);
    R.UvSet   = static_cast<uint8_t>(std::clamp(View.texcoord, 0, 3));
    R.Channel = Channel;
    R.Scalar  = View.scale;
    if (View.has_transform)
    {
        R.OffsetU = View.transform.offset[0]; R.OffsetV = View.transform.offset[1];
        R.ScaleU  = View.transform.scale[0];  R.ScaleV  = View.transform.scale[1];
        R.Rotation = View.transform.rotation;
        if (View.transform.has_texcoord) R.UvSet = static_cast<uint8_t>(std::clamp(View.transform.texcoord, 0, 3));
    }
    return R;
}

std::string EncodeTextureReference(const TextureReference& T, const std::function<int(uint32_t)>& TextureIndexOf, const char* ExtraKey = nullptr, float Extra = 0.0f)
{
    const int Index = T.IsBound() && TextureIndexOf ? TextureIndexOf(T.Texture) : -1;
    if (Index < 0) return {};
    std::ostringstream S;
    S << "{\"index\":" << Index;
    if (T.UvSet) S << ",\"texCoord\":" << int(T.UvSet);
    if (ExtraKey) S << ",\"" << ExtraKey << "\":" << Number(Extra);
    const bool Transformed = T.OffsetU != 0.0f || T.OffsetV != 0.0f || T.ScaleU != 1.0f || T.ScaleV != 1.0f || T.Rotation != 0.0f;
    if (Transformed)
        S << ",\"extensions\":{\"KHR_texture_transform\":{\"offset\":[" << Number(T.OffsetU) << "," << Number(T.OffsetV) << "],\"rotation\":" << Number(T.Rotation)
          << ",\"scale\":[" << Number(T.ScaleU) << "," << Number(T.ScaleV) << "]}}";
    S << "}";
    return S.str();
}

std::string EncodeSlateTexture(const TextureReference& T, const std::function<int(uint32_t)>& TextureIndexOf)
{
    const int Index = T.IsBound() && TextureIndexOf ? TextureIndexOf(T.Texture) : -1;
    if (Index < 0) return {};
    std::ostringstream S;
    S << "{\"index\":" << Index << ",\"texCoord\":" << int(T.UvSet) << ",\"channel\":\"" << kChannelSelectionNames[static_cast<int>(T.Channel)] << "\""
      << ",\"offset\":[" << Number(T.OffsetU) << "," << Number(T.OffsetV) << "],\"scale\":[" << Number(T.ScaleU) << "," << Number(T.ScaleV) << "]"
      << ",\"rotation\":" << Number(T.Rotation) << ",\"scalar\":" << Number(T.Scalar) << "}";
    return S.str();
}

TextureReference DecodeSlateTexture(const JsonValue& V, const std::function<uint32_t(int)>* SlotOfGltfIndex)
{
    TextureReference T;
    const JsonValue* Index = V.Find("index");
    if (!Index || Index->Kind != JsonValue::Number) return T;
    T.Texture = SlotOfGltfIndex && *SlotOfGltfIndex ? (*SlotOfGltfIndex)(static_cast<int>(Index->Numeric)) : static_cast<uint32_t>(Index->Numeric);
    if (const JsonValue* U = V.Find("texCoord")) T.UvSet = static_cast<uint8_t>(std::clamp(U->Scalar(0.0f), 0.0f, 3.0f));
    if (const JsonValue* C = V.Find("channel"); C && C->Kind == JsonValue::String)
        for (int I = 0; I < 5; ++I) if (C->Text == kChannelSelectionNames[I]) T.Channel = static_cast<TextureChannelSelection>(I);
    if (const JsonValue* O = V.Find("offset"); O && O->Items.size() == 2u) { T.OffsetU = O->Items[0].Scalar(0.0f); T.OffsetV = O->Items[1].Scalar(0.0f); }
    if (const JsonValue* S = V.Find("scale");  S && S->Items.size() == 2u) { T.ScaleU  = S->Items[0].Scalar(1.0f); T.ScaleV  = S->Items[1].Scalar(1.0f); }
    if (const JsonValue* R = V.Find("rotation")) T.Rotation = R->Scalar(0.0f);
    if (const JsonValue* S = V.Find("scalar"))   T.Scalar   = S->Scalar(1.0f);
    return T;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                       SHARED HELPERS
//------------------------------------------------------------------------------------------------------------------------

float MaterialCodec::RoughnessFromShininess(float Shininess) noexcept
{
    // Blinn–Phong exponent n ↔ Beckmann/GGX α: α = sqrt(2 / (n + 2)); roughness = sqrt(α) (perceptual, OpenPBR uses r²).
    const float Alpha = std::sqrt(2.0f / (std::max(Shininess, 0.0f) + 2.0f));
    return std::clamp(std::sqrt(Alpha), 0.0f, 1.0f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        GLTF DECODE
//------------------------------------------------------------------------------------------------------------------------

MaterialDescriptor MaterialCodec::DecodeGltf(const cgltf_material* M, const MaterialDecodeConfiguration& Config, const GltfTextureResolver& Resolve) noexcept
{
    MaterialDescriptor D;
    D.Slabs.emplace_back();
    MaterialSlabDescriptor& S = D.Slabs.back();
    // R2 fallback: base 0.8 grey, roughness 0.5 (kept so the fallback slot is unchanged).
    S.SpecularRoughness = 0.5f;
    if (!M) { D.Name = "fallback"; return D; }
    D.Name = M->name ? M->name : "";

    if (M->has_pbr_metallic_roughness)
    {
        const cgltf_pbr_metallic_roughness& P = M->pbr_metallic_roughness;
        S.BaseColor[0] = P.base_color_factor[0]; S.BaseColor[1] = P.base_color_factor[1]; S.BaseColor[2] = P.base_color_factor[2];
        S.GeometryOpacity   = P.base_color_factor[3];
        S.SpecularRoughness = P.roughness_factor;
        S.BaseMetalness     = P.metallic_factor;
        S.Texture(MaterialTextureChannel::BaseColor)         = FromGltfView(P.base_color_texture, false, TextureChannelSelection::Rgb, Resolve);
        S.Texture(MaterialTextureChannel::GeometryOpacity)   = FromGltfView(P.base_color_texture, false, TextureChannelSelection::A, Resolve);
        S.Texture(MaterialTextureChannel::SpecularRoughness) = FromGltfView(P.metallic_roughness_texture, true, TextureChannelSelection::G, Resolve);
        S.Texture(MaterialTextureChannel::Metalness)         = FromGltfView(P.metallic_roughness_texture, true, TextureChannelSelection::B, Resolve);
    }
    else if (M->has_pbr_specular_glossiness)
    {
        // Archived KHR_materials_pbrSpecularGlossiness: diffuse → base, glossiness → 1 − roughness, specular → specular_color (dielectric).
        const cgltf_pbr_specular_glossiness& P = M->pbr_specular_glossiness;
        S.BaseColor[0] = P.diffuse_factor[0]; S.BaseColor[1] = P.diffuse_factor[1]; S.BaseColor[2] = P.diffuse_factor[2];
        S.GeometryOpacity   = P.diffuse_factor[3];
        S.SpecularRoughness = 1.0f - P.glossiness_factor;
        S.SpecularColor[0] = P.specular_factor[0]; S.SpecularColor[1] = P.specular_factor[1]; S.SpecularColor[2] = P.specular_factor[2];
        S.Texture(MaterialTextureChannel::BaseColor)         = FromGltfView(P.diffuse_texture, false, TextureChannelSelection::Rgb, Resolve);
        S.Texture(MaterialTextureChannel::SpecularColor)     = FromGltfView(P.specular_glossiness_texture, false, TextureChannelSelection::Rgb, Resolve);
    }

    // Emission: emissiveFactor × KHR_materials_emissive_strength × config. Stored as luminance × colour (peak-normalised).
    {
        const float Strength = (M->has_emissive_strength ? M->emissive_strength.emissive_strength : 1.0f) * Config.EmissiveRadiance;
        const float E[3] = { M->emissive_factor[0] * Strength, M->emissive_factor[1] * Strength, M->emissive_factor[2] * Strength };
        const float Peak = std::max({ E[0], E[1], E[2], 0.0f });
        if (Peak > 0.0f)
        {
            S.EmissionLuminance = Peak;
            S.EmissionColor[0] = E[0] / Peak; S.EmissionColor[1] = E[1] / Peak; S.EmissionColor[2] = E[2] / Peak;
        }
        S.Texture(MaterialTextureChannel::Emission) = FromGltfView(M->emissive_texture, false, TextureChannelSelection::Rgb, Resolve);
    }
    S.Texture(MaterialTextureChannel::GeometryNormal) = FromGltfView(M->normal_texture, true, TextureChannelSelection::Rgb, Resolve);
    S.Texture(MaterialTextureChannel::Occlusion)      = FromGltfView(M->occlusion_texture, true, TextureChannelSelection::R, Resolve);

    if (M->has_ior) S.SpecularIor = M->ior.ior;
    if (M->has_specular)
    {
        S.SpecularWeight = M->specular.specular_factor;
        std::memcpy(S.SpecularColor, M->specular.specular_color_factor, sizeof(S.SpecularColor));
        S.Texture(MaterialTextureChannel::SpecularColor) = FromGltfView(M->specular.specular_color_texture, false, TextureChannelSelection::Rgb, Resolve);
    }
    if (M->has_anisotropy)
    {
        S.SpecularRoughnessAnisotropy = M->anisotropy.anisotropy_strength;
        S.Texture(MaterialTextureChannel::Anisotropy) = FromGltfView(M->anisotropy.anisotropy_texture, true, TextureChannelSelection::Rgb, Resolve);
        S.Texture(MaterialTextureChannel::Anisotropy).Scalar = M->anisotropy.anisotropy_rotation;   // rotation rides in Scalar (radians)
    }
    if (M->has_clearcoat)
    {
        S.CoatWeight    = M->clearcoat.clearcoat_factor;
        S.CoatRoughness = M->clearcoat.clearcoat_roughness_factor;
        S.CoatIor       = 1.5f;   // KHR_materials_clearcoat fixes the coat IOR at 1.5
        S.Texture(MaterialTextureChannel::Coat)               = FromGltfView(M->clearcoat.clearcoat_texture, true, TextureChannelSelection::R, Resolve);
        S.Texture(MaterialTextureChannel::GeometryCoatNormal) = FromGltfView(M->clearcoat.clearcoat_normal_texture, true, TextureChannelSelection::Rgb, Resolve);
    }
    if (M->has_sheen)
    {
        // OpenPBR fuzz: weight = peak of sheen colour, colour normalised.
        const float* C = M->sheen.sheen_color_factor;
        const float Peak = std::max({ C[0], C[1], C[2], 0.0f });
        S.FuzzWeight = Peak;
        if (Peak > 0.0f) { S.FuzzColor[0] = C[0] / Peak; S.FuzzColor[1] = C[1] / Peak; S.FuzzColor[2] = C[2] / Peak; }
        S.FuzzRoughness = M->sheen.sheen_roughness_factor;
        S.Texture(MaterialTextureChannel::Fuzz) = FromGltfView(M->sheen.sheen_color_texture, false, TextureChannelSelection::Rgb, Resolve);
    }
    if (M->has_transmission)
    {
        S.TransmissionWeight = M->transmission.transmission_factor;
        S.Texture(MaterialTextureChannel::Transmission) = FromGltfView(M->transmission.transmission_texture, true, TextureChannelSelection::R, Resolve);
    }
    if (M->has_volume)
    {
        // attenuation colour over attenuation distance → transmission_color at transmission_depth
        S.TransmissionDepth = M->volume.attenuation_distance;
        if (std::isfinite(S.TransmissionDepth) && S.TransmissionDepth > 0.0f)
            std::memcpy(S.TransmissionColor, M->volume.attenuation_color, sizeof(S.TransmissionColor));
        else S.TransmissionDepth = 0.0f;
        if (M->volume.thickness_factor <= 0.0f) D.Flags |= MaterialFlagThinWalled;
    }
    if (M->has_dispersion && M->dispersion.dispersion > 0.0f)
    {
        S.TransmissionDispersionScale      = 1.0f;
        S.TransmissionDispersionAbbeNumber = 20.0f / M->dispersion.dispersion;   // KHR: dispersion = 20 / Abbe
    }
    if (M->has_iridescence)
    {
        S.ThinFilmWeight    = M->iridescence.iridescence_factor;
        S.ThinFilmIor       = M->iridescence.iridescence_ior;
        S.ThinFilmThickness = M->iridescence.iridescence_thickness_max * 0.001f;   // nm → µm
        S.Texture(MaterialTextureChannel::ThinFilm) = FromGltfView(M->iridescence.iridescence_texture, true, TextureChannelSelection::R, Resolve);
    }
    if (M->has_diffuse_transmission)
    {
        // Thin-walled diffuse transmission ≈ OpenPBR subsurface in thin-walled mode.
        S.SubsurfaceWeight = M->diffuse_transmission.diffuse_transmission_factor;
        std::memcpy(S.SubsurfaceColor, M->diffuse_transmission.diffuse_transmission_color_factor, sizeof(S.SubsurfaceColor));
        S.Texture(MaterialTextureChannel::Subsurface) = FromGltfView(M->diffuse_transmission.diffuse_transmission_texture, true, TextureChannelSelection::A, Resolve);
        D.Flags |= MaterialFlagThinWalled;
    }

    if (M->double_sided) D.Flags |= MaterialFlagDoubleSided;
    if (M->unlit)        D.Flags |= MaterialFlagUnlit;
    if (M->alpha_mode == cgltf_alpha_mode_mask)  { D.Flags |= MaterialFlagAlphaMask; D.AlphaCutoff = M->alpha_cutoff; }
    if (M->alpha_mode == cgltf_alpha_mode_blend) D.Flags |= MaterialFlagAlphaTranslucent;
    if (D.Flags & MaterialFlagThinWalled) S.GeometryThinWalled = true;

    // Slate extras: scalars first (single slab), then a full slab graph if present (replaces the slab list).
    if (M->extras.data) (void)DecodeSlateExtras(M->extras.data, D);
    return D;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SLATE EXTRAS DECODE
//------------------------------------------------------------------------------------------------------------------------

bool MaterialCodec::DecodeSlateExtras(const char* ExtrasJson, MaterialDescriptor& D) noexcept
{
    if (!ExtrasJson) return false;
    JsonValue Root;
    JsonReader Reader{ ExtrasJson, ExtrasJson + std::strlen(ExtrasJson) };
    if (!Reader.Parse(Root) || Root.Kind != JsonValue::Object) return false;
    if (D.Slabs.empty()) D.Slabs.emplace_back();

    bool Found = false;
    // Flat parameters apply to slab 0 (the plain-glTF single-slab case): slate_* and any OpenPBR parameter glTF cannot say.
    for (const ParameterEntry& E : kParameters)
    {
        const JsonValue* V = Root.Find(E.Identifier);
        if (!V) continue;
        float* F = Param(D.Slabs[0], E);
        if (E.Components == 1) { if (V->Kind == JsonValue::Number) { F[0] = V->Scalar(F[0]); Found = true; } }
        else if (V->Kind == JsonValue::Array && V->Items.size() >= E.Components) { for (uint8_t C = 0; C < E.Components; ++C) F[C] = V->Items[C].Scalar(F[C]); Found = true; }
    }

    const JsonValue* Slabs = Root.Find("slate_slabs");
    if (Slabs && Slabs->Kind == JsonValue::Array && !Slabs->Items.empty())
    {
        Found = true;
        std::vector<MaterialSlabDescriptor> Decoded;
        for (const JsonValue& SlabJson : Slabs->Items)
        {
            MaterialSlabDescriptor S;
            for (const ParameterEntry& E : kParameters)
            {
                const JsonValue* V = SlabJson.Find(E.Identifier);
                if (!V) continue;
                float* F = Param(S, E);
                if (E.Components == 1) { if (V->Kind == JsonValue::Number) F[0] = V->Scalar(F[0]); }
                else if (V->Kind == JsonValue::Array && V->Items.size() >= E.Components)
                    for (uint8_t C = 0; C < E.Components; ++C) F[C] = V->Items[C].Scalar(F[C]);
            }
            if (const JsonValue* T = SlabJson.Find("geometry_thin_walled"); T && T->Kind == JsonValue::Bool) S.GeometryThinWalled = T->Truth;
            if (const JsonValue* Textures = SlabJson.Find("textures"); Textures && Textures->Kind == JsonValue::Object)
                for (uint32_t C = 0u; C < kMaterialTextureChannelCount; ++C)
                    if (const JsonValue* T = Textures->Find(kTextureChannelNames[C])) S.Textures[C] = DecodeSlateTexture(*T, nullptr);
            Decoded.push_back(S);
        }
        // Slab 0 of the graph inherits the texture slots the core glTF resolved (extras hold raw glTF texture indices
        //    only when a codec wrote them; the resident slots come from the core block).
        if (!D.Slabs.empty())
            for (uint32_t C = 0u; C < kMaterialTextureChannelCount; ++C)
                if (!Decoded.back().Textures[C].IsBound() && D.Slabs[0].Textures[C].IsBound()) Decoded.back().Textures[C] = D.Slabs[0].Textures[C];
        D.Slabs = std::move(Decoded);
    }

    const JsonValue* Ops = Root.Find("slate_operations");
    if (Ops && Ops->Kind == JsonValue::Array)
    {
        Found = true;
        D.Operations.clear();
        for (const JsonValue& OpJson : Ops->Items)
        {
            MaterialOperation Op;
            if (const JsonValue* K = OpJson.Find("op"); K && K->Kind == JsonValue::String)
                for (int I = 0; I < 4; ++I) if (K->Text == kOperationNames[I]) Op.Category = static_cast<MaterialOperationCategory>(I);
            if (const JsonValue* L = OpJson.Find("left"))   Op.Left   = static_cast<uint32_t>(L->Scalar(0.0f));
            if (const JsonValue* R = OpJson.Find("right"))  Op.Right  = static_cast<uint32_t>(R->Scalar(0.0f));
            if (const JsonValue* W = OpJson.Find("weight")) Op.Weight = W->Scalar(1.0f);
            if (const JsonValue* M = OpJson.Find("mask"))   Op.Mask   = DecodeSlateTexture(*M, nullptr);
            D.Operations.push_back(Op);
        }
    }
    return Found;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        GLTF ENCODE
//------------------------------------------------------------------------------------------------------------------------

std::string MaterialCodec::EncodeGltf(const MaterialDescriptor& D, std::vector<std::string>& ExtensionsUsed, const std::function<int(uint32_t)>& TextureIndexOf) noexcept
{
    const auto Use = [&](const char* Name) { if (std::find(ExtensionsUsed.begin(), ExtensionsUsed.end(), Name) == ExtensionsUsed.end()) ExtensionsUsed.emplace_back(Name); };
    const MaterialSlabDescriptor Default{};
    const MaterialSlabDescriptor& S = D.Slabs.empty() ? Default : D.Slabs.back();   // bottom slab = core glTF material

    std::ostringstream J;
    J << "{\"name\":\"" << Escape(D.Name) << "\",\"pbrMetallicRoughness\":{\"baseColorFactor\":["
      << Number(S.BaseColor[0]) << "," << Number(S.BaseColor[1]) << "," << Number(S.BaseColor[2]) << "," << Number(S.GeometryOpacity) << "],"
      << "\"metallicFactor\":" << Number(S.BaseMetalness) << ",\"roughnessFactor\":" << Number(S.SpecularRoughness);
    if (std::string T = EncodeTextureReference(S.Texture(MaterialTextureChannel::BaseColor), TextureIndexOf); !T.empty()) J << ",\"baseColorTexture\":" << T;
    if (std::string T = EncodeTextureReference(S.Texture(MaterialTextureChannel::SpecularRoughness), TextureIndexOf); !T.empty()) J << ",\"metallicRoughnessTexture\":" << T;
    J << "}";
    if (std::string T = EncodeTextureReference(S.Texture(MaterialTextureChannel::GeometryNormal), TextureIndexOf, "scale", S.Texture(MaterialTextureChannel::GeometryNormal).Scalar); !T.empty()) J << ",\"normalTexture\":" << T;
    if (std::string T = EncodeTextureReference(S.Texture(MaterialTextureChannel::Occlusion), TextureIndexOf, "strength", S.Texture(MaterialTextureChannel::Occlusion).Scalar); !T.empty()) J << ",\"occlusionTexture\":" << T;
    if (std::string T = EncodeTextureReference(S.Texture(MaterialTextureChannel::Emission), TextureIndexOf); !T.empty()) J << ",\"emissiveTexture\":" << T;

    std::ostringstream X;   // extensions object body
    const auto Extension = [&](const char* Name, const std::string& Body) { Use(Name); if (X.tellp() > 0) X << ","; X << "\"" << Name << "\":{" << Body << "}"; };

    if (S.EmissionLuminance > 0.0f)
    {
        // emissiveFactor is clamped to [0,1] by the spec; the magnitude rides in KHR_materials_emissive_strength.
        J << ",\"emissiveFactor\":[" << Number(S.EmissionColor[0]) << "," << Number(S.EmissionColor[1]) << "," << Number(S.EmissionColor[2]) << "]";
        Extension("KHR_materials_emissive_strength", "\"emissiveStrength\":" + Number(S.EmissionLuminance));
    }
    if (S.SpecularIor != 1.5f) Extension("KHR_materials_ior", "\"ior\":" + Number(S.SpecularIor));
    if (S.SpecularWeight != 1.0f || S.SpecularColor[0] != 1.0f || S.SpecularColor[1] != 1.0f || S.SpecularColor[2] != 1.0f)
    {
        std::string Body = "\"specularFactor\":" + Number(S.SpecularWeight) + ",\"specularColorFactor\":[" + Number(S.SpecularColor[0]) + "," + Number(S.SpecularColor[1]) + "," + Number(S.SpecularColor[2]) + "]";
        if (std::string T = EncodeTextureReference(S.Texture(MaterialTextureChannel::SpecularColor), TextureIndexOf); !T.empty()) Body += ",\"specularColorTexture\":" + T;
        Extension("KHR_materials_specular", Body);
    }
    if (S.SpecularRoughnessAnisotropy > 0.0f)
    {
        std::string Body = "\"anisotropyStrength\":" + Number(S.SpecularRoughnessAnisotropy) + ",\"anisotropyRotation\":" + Number(S.Texture(MaterialTextureChannel::Anisotropy).Scalar);
        if (std::string T = EncodeTextureReference(S.Texture(MaterialTextureChannel::Anisotropy), TextureIndexOf); !T.empty()) Body += ",\"anisotropyTexture\":" + T;
        Extension("KHR_materials_anisotropy", Body);
    }
    if (S.CoatWeight > 0.0f)
    {
        std::string Body = "\"clearcoatFactor\":" + Number(S.CoatWeight) + ",\"clearcoatRoughnessFactor\":" + Number(S.CoatRoughness);
        if (std::string T = EncodeTextureReference(S.Texture(MaterialTextureChannel::Coat), TextureIndexOf); !T.empty()) Body += ",\"clearcoatTexture\":" + T;
        if (std::string T = EncodeTextureReference(S.Texture(MaterialTextureChannel::GeometryCoatNormal), TextureIndexOf); !T.empty()) Body += ",\"clearcoatNormalTexture\":" + T;
        Extension("KHR_materials_clearcoat", Body);
    }
    if (S.FuzzWeight > 0.0f)
    {
        std::string Body = "\"sheenColorFactor\":[" + Number(S.FuzzColor[0] * S.FuzzWeight) + "," + Number(S.FuzzColor[1] * S.FuzzWeight) + "," + Number(S.FuzzColor[2] * S.FuzzWeight) + "],\"sheenRoughnessFactor\":" + Number(S.FuzzRoughness);
        if (std::string T = EncodeTextureReference(S.Texture(MaterialTextureChannel::Fuzz), TextureIndexOf); !T.empty()) Body += ",\"sheenColorTexture\":" + T;
        Extension("KHR_materials_sheen", Body);
    }
    if (S.TransmissionWeight > 0.0f)
    {
        std::string Body = "\"transmissionFactor\":" + Number(S.TransmissionWeight);
        if (std::string T = EncodeTextureReference(S.Texture(MaterialTextureChannel::Transmission), TextureIndexOf); !T.empty()) Body += ",\"transmissionTexture\":" + T;
        Extension("KHR_materials_transmission", Body);
        if (S.TransmissionDepth > 0.0f)
            Extension("KHR_materials_volume", "\"thicknessFactor\":" + Number(S.GeometryThinWalled ? 0.0f : S.TransmissionDepth) + ",\"attenuationDistance\":" + Number(S.TransmissionDepth)
                      + ",\"attenuationColor\":[" + Number(S.TransmissionColor[0]) + "," + Number(S.TransmissionColor[1]) + "," + Number(S.TransmissionColor[2]) + "]");
        if (S.TransmissionDispersionScale > 0.0f && S.TransmissionDispersionAbbeNumber > 0.0f)
            Extension("KHR_materials_dispersion", "\"dispersion\":" + Number(20.0f * S.TransmissionDispersionScale / S.TransmissionDispersionAbbeNumber));
    }
    if (S.ThinFilmWeight > 0.0f)
    {
        std::string Body = "\"iridescenceFactor\":" + Number(S.ThinFilmWeight) + ",\"iridescenceIor\":" + Number(S.ThinFilmIor)
                         + ",\"iridescenceThicknessMinimum\":" + Number(S.ThinFilmThickness * 1000.0f) + ",\"iridescenceThicknessMaximum\":" + Number(S.ThinFilmThickness * 1000.0f);
        if (std::string T = EncodeTextureReference(S.Texture(MaterialTextureChannel::ThinFilm), TextureIndexOf); !T.empty()) Body += ",\"iridescenceTexture\":" + T;
        Extension("KHR_materials_iridescence", Body);
    }
    if (S.SubsurfaceWeight > 0.0f && S.GeometryThinWalled)
        Extension("KHR_materials_diffuse_transmission", "\"diffuseTransmissionFactor\":" + Number(S.SubsurfaceWeight) + ",\"diffuseTransmissionColorFactor\":["
                  + Number(S.SubsurfaceColor[0]) + "," + Number(S.SubsurfaceColor[1]) + "," + Number(S.SubsurfaceColor[2]) + "]");
    if (D.Flags & MaterialFlagUnlit) Extension("KHR_materials_unlit", "");
    if (X.tellp() > 0) J << ",\"extensions\":{" << X.str() << "}";

    if (D.Flags & MaterialFlagAlphaMask)        J << ",\"alphaMode\":\"MASK\",\"alphaCutoff\":" << Number(D.AlphaCutoff);
    if (D.Flags & MaterialFlagAlphaTranslucent) J << ",\"alphaMode\":\"BLEND\"";
    if (D.Flags & MaterialFlagDoubleSided)      J << ",\"doubleSided\":true";

    // Extras: everything glTF cannot say — slate_* scalars for a single slab, or the full graph.
    std::ostringstream Extras;
    const auto Member = [&](const std::string& Text) { if (Extras.tellp() > 0) Extras << ","; Extras << Text; };
    const bool Graph = D.Slabs.size() > 1u || !D.Operations.empty();
    if (!Graph)
    {
        for (const ParameterEntry& E : kParameters)
        {
            if (IsGltfCovered(E.Identifier)) continue;
            const float* F = Param(S, E); const float* G = Param(Default, E);
            if (std::memcmp(F, G, E.Components * sizeof(float)) == 0) continue;
            std::string Text = std::string("\"") + E.Identifier + "\":";
            if (E.Components == 1) Text += Number(F[0]);
            else { Text += "["; for (uint8_t C = 0; C < E.Components; ++C) { if (C) Text += ","; Text += Number(F[C]); } Text += "]"; }
            Member(Text);
        }
    }
    else
    {
        std::ostringstream Slabs;
        for (size_t I = 0; I < D.Slabs.size(); ++I)
        {
            const MaterialSlabDescriptor& Slab = D.Slabs[I];
            if (I) Slabs << ",";
            Slabs << "{";
            bool First = true;
            const auto Field = [&](const std::string& Text) { if (!First) Slabs << ","; First = false; Slabs << Text; };
            for (const ParameterEntry& E : kParameters)
            {
                const float* F = Param(Slab, E); const float* G = Param(Default, E);
                if (std::memcmp(F, G, E.Components * sizeof(float)) == 0) continue;
                std::string Text = std::string("\"") + E.Identifier + "\":";
                if (E.Components == 1) Text += Number(F[0]);
                else { Text += "["; for (uint8_t C = 0; C < E.Components; ++C) { if (C) Text += ","; Text += Number(F[C]); } Text += "]"; }
                Field(Text);
            }
            if (Slab.GeometryThinWalled) Field("\"geometry_thin_walled\":true");
            std::ostringstream Textures; bool AnyTexture = false;
            for (uint32_t C = 0u; C < kMaterialTextureChannelCount; ++C)
            {
                const std::string T = EncodeSlateTexture(Slab.Textures[C], TextureIndexOf);
                if (T.empty()) continue;
                if (AnyTexture) Textures << ",";
                AnyTexture = true;
                Textures << "\"" << kTextureChannelNames[C] << "\":" << T;
            }
            if (AnyTexture) Field("\"textures\":{" + Textures.str() + "}");
            Slabs << "}";
        }
        Member("\"slate_slabs\":[" + Slabs.str() + "]");
        if (!D.Operations.empty())
        {
            std::ostringstream Ops;
            for (size_t I = 0; I < D.Operations.size(); ++I)
            {
                const MaterialOperation& Op = D.Operations[I];
                if (I) Ops << ",";
                Ops << "{\"op\":\"" << kOperationNames[static_cast<int>(Op.Category)] << "\",\"left\":" << Op.Left << ",\"right\":" << Op.Right << ",\"weight\":" << Number(Op.Weight);
                if (std::string M = EncodeSlateTexture(Op.Mask, TextureIndexOf); !M.empty()) Ops << ",\"mask\":" << M;
                Ops << "}";
            }
            Member("\"slate_operations\":[" + Ops.str() + "]");
        }
    }
    if (Extras.tellp() > 0) J << ",\"extras\":{" << Extras.str() << "}";
    J << "}";
    return J.str();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         FBX DECODE
//------------------------------------------------------------------------------------------------------------------------

namespace {

void FbxMap(const ufbx_material_map& Map, float* Out, int Components, const FbxTextureResolver& Resolve, TextureReference* Slot, bool Linear, TextureChannelSelection Channel = TextureChannelSelection::Rgb)
{
    if (Map.has_value)
    {
        if (Components == 1) Out[0] = static_cast<float>(Map.value_components >= 3 ? (Map.value_vec3.x + Map.value_vec3.y + Map.value_vec3.z) / 3.0 : Map.value_real);
        else if (Map.value_components >= 3) { Out[0] = static_cast<float>(Map.value_vec3.x); Out[1] = static_cast<float>(Map.value_vec3.y); Out[2] = static_cast<float>(Map.value_vec3.z); }
        else Out[0] = Out[1] = Out[2] = static_cast<float>(Map.value_real);
    }
    if (Slot && Map.texture && Map.texture_enabled && Resolve)
    {
        Slot->Texture = Resolve(Map.texture, Linear);
        Slot->Channel = Channel;
        if (Map.texture->has_uv_transform)
        {
            Slot->OffsetU = static_cast<float>(Map.texture->uv_transform.translation.x); Slot->OffsetV = static_cast<float>(Map.texture->uv_transform.translation.y);
            Slot->ScaleU  = static_cast<float>(Map.texture->uv_transform.scale.x);       Slot->ScaleV  = static_cast<float>(Map.texture->uv_transform.scale.y);
        }
    }
}

} // namespace

MaterialDescriptor MaterialCodec::DecodeFbx(const ufbx_material* M, const MaterialDecodeConfiguration& Config, const FbxTextureResolver& Resolve) noexcept
{
    MaterialDescriptor D;
    D.Slabs.emplace_back();
    MaterialSlabDescriptor& S = D.Slabs.back();
    S.SpecularRoughness = 0.5f;
    if (!M) { D.Name = "fallback"; return D; }
    D.Name = std::string(M->name.data, M->name.length);

    const ufbx_material_pbr_maps& P = M->pbr;
    const ufbx_material_fbx_maps& F = M->fbx;
    float Scratch[3];

    // ufbx normalises every shader model (Phong, Lambert, Arnold, OSL Standard Surface, ...) into `pbr`; those maps
    //    are OpenPBR's ancestors (Autodesk Standard Surface), so the mapping is direct.
    FbxMap(P.base_factor,   &S.BaseWeight,        1, Resolve, nullptr, true);
    FbxMap(P.base_color,    S.BaseColor,          3, Resolve, &S.Texture(MaterialTextureChannel::BaseColor), false);
    FbxMap(P.metalness,     &S.BaseMetalness,     1, Resolve, &S.Texture(MaterialTextureChannel::Metalness), true, TextureChannelSelection::R);
    FbxMap(P.diffuse_roughness, &S.BaseDiffuseRoughness, 1, Resolve, nullptr, true);
    FbxMap(P.specular_factor, &S.SpecularWeight,  1, Resolve, nullptr, true);
    FbxMap(P.specular_color,  S.SpecularColor,    3, Resolve, &S.Texture(MaterialTextureChannel::SpecularColor), false);
    FbxMap(P.specular_ior,    &S.SpecularIor,     1, Resolve, nullptr, true);
    FbxMap(P.specular_anisotropy, &S.SpecularRoughnessAnisotropy, 1, Resolve, &S.Texture(MaterialTextureChannel::Anisotropy), true);
    if (P.roughness.has_value || P.roughness.texture)
        FbxMap(P.roughness, &S.SpecularRoughness, 1, Resolve, &S.Texture(MaterialTextureChannel::SpecularRoughness), true, TextureChannelSelection::R);
    else if (P.glossiness.has_value) { FbxMap(P.glossiness, Scratch, 1, Resolve, nullptr, true); S.SpecularRoughness = 1.0f - Scratch[0]; }
    else if (F.specular_exponent.has_value) { FbxMap(F.specular_exponent, Scratch, 1, Resolve, nullptr, true); S.SpecularRoughness = RoughnessFromShininess(Scratch[0]); }
    FbxMap(P.transmission_factor, &S.TransmissionWeight, 1, Resolve, &S.Texture(MaterialTextureChannel::Transmission), true, TextureChannelSelection::R);
    FbxMap(P.transmission_color,  S.TransmissionColor,   3, Resolve, nullptr, false);
    FbxMap(P.transmission_depth,  &S.TransmissionDepth,  1, Resolve, nullptr, true);
    FbxMap(P.transmission_scatter, S.TransmissionScatter, 3, Resolve, nullptr, false);
    FbxMap(P.transmission_scatter_anisotropy, &S.TransmissionScatterAnisotropy, 1, Resolve, nullptr, true);
    FbxMap(P.transmission_dispersion, &S.TransmissionDispersionAbbeNumber, 1, Resolve, nullptr, true);
    if (P.transmission_dispersion.has_value && S.TransmissionDispersionAbbeNumber > 0.0f) S.TransmissionDispersionScale = 1.0f; else S.TransmissionDispersionAbbeNumber = 20.0f;
    FbxMap(P.subsurface_factor, &S.SubsurfaceWeight, 1, Resolve, &S.Texture(MaterialTextureChannel::Subsurface), true, TextureChannelSelection::R);
    FbxMap(P.subsurface_color,  S.SubsurfaceColor,   3, Resolve, nullptr, false);
    FbxMap(P.subsurface_radius, S.SubsurfaceRadiusScale, 3, Resolve, nullptr, true);
    FbxMap(P.subsurface_scale,  &S.SubsurfaceRadius, 1, Resolve, nullptr, true);
    FbxMap(P.subsurface_anisotropy, &S.SubsurfaceScatterAnisotropy, 1, Resolve, nullptr, true);
    FbxMap(P.sheen_factor,   &S.FuzzWeight,   1, Resolve, &S.Texture(MaterialTextureChannel::Fuzz), true, TextureChannelSelection::R);
    FbxMap(P.sheen_color,    S.FuzzColor,     3, Resolve, nullptr, false);
    FbxMap(P.sheen_roughness, &S.FuzzRoughness, 1, Resolve, nullptr, true);
    FbxMap(P.coat_factor,    &S.CoatWeight,   1, Resolve, &S.Texture(MaterialTextureChannel::Coat), true, TextureChannelSelection::R);
    FbxMap(P.coat_color,     S.CoatColor,     3, Resolve, nullptr, false);
    FbxMap(P.coat_roughness, &S.CoatRoughness, 1, Resolve, nullptr, true);
    FbxMap(P.coat_ior,       &S.CoatIor,      1, Resolve, nullptr, true);
    FbxMap(P.coat_anisotropy, &S.CoatRoughnessAnisotropy, 1, Resolve, nullptr, true);
    FbxMap(P.coat_normal,    Scratch,         3, Resolve, &S.Texture(MaterialTextureChannel::GeometryCoatNormal), true);
    FbxMap(P.thin_film_factor,    &S.ThinFilmWeight,    1, Resolve, &S.Texture(MaterialTextureChannel::ThinFilm), true, TextureChannelSelection::R);
    FbxMap(P.thin_film_thickness, &S.ThinFilmThickness, 1, Resolve, nullptr, true);
    if (P.thin_film_thickness.has_value) S.ThinFilmThickness *= 0.001f;   // Standard Surface: nm → µm
    FbxMap(P.thin_film_ior,       &S.ThinFilmIor,       1, Resolve, nullptr, true);
    {
        float Factor = 0.0f, Colour[3] = { 1.0f, 1.0f, 1.0f };
        FbxMap(P.emission_factor, &Factor, 1, Resolve, nullptr, true);
        FbxMap(P.emission_color,  Colour,  3, Resolve, &S.Texture(MaterialTextureChannel::Emission), false);
        const float E[3] = { Factor * Colour[0] * Config.EmissiveRadiance, Factor * Colour[1] * Config.EmissiveRadiance, Factor * Colour[2] * Config.EmissiveRadiance };
        const float Peak = std::max({ E[0], E[1], E[2], 0.0f });
        if (Peak > 0.0f) { S.EmissionLuminance = Peak; S.EmissionColor[0] = E[0] / Peak; S.EmissionColor[1] = E[1] / Peak; S.EmissionColor[2] = E[2] / Peak; }
    }
    FbxMap(P.opacity,    &S.GeometryOpacity, 1, Resolve, &S.Texture(MaterialTextureChannel::GeometryOpacity), true, TextureChannelSelection::R);
    FbxMap(P.normal_map, Scratch, 3, Resolve, &S.Texture(MaterialTextureChannel::GeometryNormal), true);
    FbxMap(P.ambient_occlusion, Scratch, 1, Resolve, &S.Texture(MaterialTextureChannel::Occlusion), true, TextureChannelSelection::R);
    if (M->features.thin_walled.enabled) { S.GeometryThinWalled = true; D.Flags |= MaterialFlagThinWalled; }
    if (M->features.double_sided.enabled) D.Flags |= MaterialFlagDoubleSided;
    if (S.GeometryOpacity < 1.0f || S.Texture(MaterialTextureChannel::GeometryOpacity).IsBound()) D.Flags |= MaterialFlagAlphaTranslucent;
    return D;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         OBJ DECODE
//------------------------------------------------------------------------------------------------------------------------

MaterialDescriptor MaterialCodec::DecodeObj(const ObjMaterialSource& Src, const MaterialDecodeConfiguration& Config, const PathTextureResolver& Resolve) noexcept
{
    MaterialDescriptor D;
    D.Name = Src.Name;
    D.Slabs.emplace_back();
    MaterialSlabDescriptor& S = D.Slabs.back();
    std::memcpy(S.BaseColor, Src.Kd, sizeof(S.BaseColor));
    S.SpecularRoughness = Src.Ns > 0.0f ? RoughnessFromShininess(Src.Ns) : 0.5f;
    S.SpecularIor       = Src.Ni > 1.0f ? Src.Ni : 1.5f;
    // Ks: the spec colour of a dielectric; a strongly coloured Ks with a dark Kd is how .mtl files fake metals.
    const float KsPeak = std::max({ Src.Ks[0], Src.Ks[1], Src.Ks[2], 0.0f });
    const float KdPeak = std::max({ Src.Kd[0], Src.Kd[1], Src.Kd[2], 0.0f });
    if (Src.Illum == 3 || (KsPeak > 0.5f && KdPeak < 0.1f))
    {
        S.BaseMetalness = 1.0f;
        std::memcpy(S.BaseColor, Src.Ks, sizeof(S.BaseColor));
    }
    else if (KsPeak > 0.0f)
    {
        S.SpecularWeight = std::min(KsPeak, 1.0f);
        S.SpecularColor[0] = Src.Ks[0] / KsPeak; S.SpecularColor[1] = Src.Ks[1] / KsPeak; S.SpecularColor[2] = Src.Ks[2] / KsPeak;
    }
    else S.SpecularWeight = 0.0f;
    S.GeometryOpacity = std::clamp(Src.d, 0.0f, 1.0f);
    if (Src.Illum >= 4 && Src.Illum <= 7)   // glass models
    {
        S.TransmissionWeight = 1.0f - S.GeometryOpacity; S.GeometryOpacity = 1.0f;
        std::memcpy(S.TransmissionColor, Src.Tf, sizeof(S.TransmissionColor));
    }
    const float E[3] = { Src.Ke[0] * Config.EmissiveRadiance, Src.Ke[1] * Config.EmissiveRadiance, Src.Ke[2] * Config.EmissiveRadiance };
    const float Peak = std::max({ E[0], E[1], E[2], 0.0f });
    if (Peak > 0.0f) { S.EmissionLuminance = Peak; S.EmissionColor[0] = E[0] / Peak; S.EmissionColor[1] = E[1] / Peak; S.EmissionColor[2] = E[2] / Peak; }

    const auto Bind = [&](const std::string& Path, MaterialTextureChannel C, bool Linear, TextureChannelSelection Channel)
    {
        if (Path.empty() || !Resolve) return;
        TextureReference& T = S.Texture(C);
        T.Texture = Resolve(Path, Linear); T.Channel = Channel;
    };
    Bind(Src.MapKd,   MaterialTextureChannel::BaseColor,       false, TextureChannelSelection::Rgb);
    Bind(Src.MapKs,   MaterialTextureChannel::SpecularColor,   false, TextureChannelSelection::Rgb);
    Bind(Src.MapKe,   MaterialTextureChannel::Emission,        false, TextureChannelSelection::Rgb);
    Bind(Src.MapD,    MaterialTextureChannel::GeometryOpacity, true,  TextureChannelSelection::R);
    Bind(Src.MapBump, MaterialTextureChannel::GeometryNormal,  true,  TextureChannelSelection::Rgb);
    Bind(Src.MapNs,   MaterialTextureChannel::SpecularRoughness, true, TextureChannelSelection::R);
    if (S.GeometryOpacity < 1.0f || !Src.MapD.empty()) D.Flags |= MaterialFlagAlphaMask;
    D.Flags |= MaterialFlagDoubleSided;   // OBJ has no winding guarantee; every OBJ importer renders two-sided
    return D;
}

} // namespace Frontier
