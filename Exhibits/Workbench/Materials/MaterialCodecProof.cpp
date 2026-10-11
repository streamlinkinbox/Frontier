//============================================================================================================================================
//                                                      MATERIALCODECPROOF.CPP
//============================================================================================================================================
// 🧩 M6 codec gap-fill proof: per-extension glTF / FBX / OBJ round-trip fixtures for MaterialCodec. Also the INTERIM
//    mapping table (MaterialSystemResearch-2026.md §5 is pending in-tree; this header is authoritative until it lands).
//
// glTF → descriptor
//    core ...... baseColorFactor → BaseColor + GeometryOpacity · metallicFactor → BaseMetalness · roughnessFactor →
//                SpecularRoughness · baseColorTexture → BaseColor(Rgb) + GeometryOpacity(A) · metallicRoughnessTexture →
//                SpecularRoughness(G) + Metalness(B) · normalTexture → GeometryNormal · occlusionTexture(R, strength →
//                Scalar) · emissiveFactor × emissive_strength × config → EmissionLuminance + EmissionColor (peak fold) ·
//                alphaMode/cutoff + doubleSided + unlit → flags.
//    ior ....... SpecularIor. specular: factor + color + colorTexture; the factor TEXTURE (.A) is dropped (no carrier).
//    anisotropy  strength + rotation [rad, verbatim incl. Sultan-range negatives] + texture.
//    clearcoat . factor + roughness + IOR 1.5 (spec-fixed) + texture(R) + normal; the roughness TEXTURE is dropped.
//    sheen ..... color×weight fold (weight = peak) + roughness + colorTexture; the roughness TEXTURE is dropped.
//    transmission factor + texture(R). volume: attenuation → depth + color · thickness_factor → VolumeThickness
//                (descriptor-level fidelity carry, re-encoded verbatim) · thickness ≤ 0 → thin-walled; thickness_texture
//                (G/B) is dropped. dispersion: 20 / Abbe both ways. iridescence: factor + ior + thickness_max nm → µm
//                (min collapses) + texture(R); the thickness TEXTURE is dropped. diffuse_transmission: factor + color +
//                texture(A) → Subsurface + thin-walled; the color TEXTURE is dropped.
//    transform . KHR_texture_transform → (UvSet incl. texCoord override, offset, rotation, scale).
//    extras .... flat slate_* scalars (single slab, incl. slate_direct_f0_weight) + geometry_thin_walled bool;
//                slate_slabs replaces the slab list (full graph + textures + slate_operations). Extras override core.
// descriptor → glTF: single slab = plain glTF + extensions + extras for the rest; multi-slab/ops = slate_slabs graph.
//    Encode reads Slabs.back() as the core material; the bottom graph slab inherits core-resolved texture slots.
//    Authored slabs (VolumeThickness 0) encode thicknessFactor = attenuation depth (documented lossy scale).
// FBX (Standard Surface via ufbx): base, metalness, diffuse_roughness, specular_* (rotation TURNS × 2π → radians),
//    roughness | 1 − glossiness | RoughnessFromShininess(specular_exponent), transmission_* (dispersion = Abbe direct),
//    subsurface_* (radius → RadiusScale, scale → Radius [m]), sheen_*, coat_* (+anisotropy), thin_film_* (nm → µm),
//    emission_factor × color × config (peak fold), opacity, normal_map, ambient_occlusion, thin_walled/double_sided.
//    Dropped (no carrier): transmission_extra_roughness, subsurface_tint_color/type, matte_*, indirect_*, coat_rotation.
// OBJ (.mtl): Kd → BaseColor · Ks → metal heuristic (illum 3, or Ks > 0.5 with Kd < 0.1) else SpecularWeight + tint ·
//    Ns → RoughnessFromShininess (Ns ≤ 0 → 0.5) · Ni → Ior · d → transmission_weight (1 − d, every illum; opacity stays
//    1 — M6 correction, .mtl has no cutout) · Ke × config → emission (peak fold) · Tf → TransmissionColor (glass illum
//    4–7) · MapKd/Ks/Ke/Bump → slots · MapD → opacity slot as BLEND coverage · MapNs DROPPED (polarity inverted).
//    Tr is folded into d by fast_obj (proved in T36).
//
// Assertion tiers. TIER-1 (value-exact): descriptor operator== after decode→encode→decode, plus BYTE-identical second
//    encode (encode∘decode is a fixed point — the normalising mappings converge in one pass). TIER-2 (renormalising:
//    sheen / emission peak folds, iridescence nm↔µm, dispersion 20/Abbe): relative 1e-6 across TWO round-trips (bounded,
//    no drift). Every drop-inventory entry has a lock assert (dropped on decode AND absent on encode).

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#define FAST_OBJ_IMPLEMENTATION
#include "fast_obj.h"
#include "ufbx.h"
#include "MaterialCodec.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <functional>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>

namespace {

using namespace Frontier;

int Checks = 0, Failures = 0;
void Check(bool Cond, const char* Name, const std::string& Detail = {})
{
    ++Checks;
    if (Cond) std::printf("ok %d: %s\n", Checks, Name);
    else { ++Failures; std::printf("FAIL %d: %s%s%s\n", Checks, Name, Detail.empty() ? "" : " — ", Detail.c_str()); }
}
std::string F(float V) { char B[32]; std::snprintf(B, sizeof B, "%.9g", V); return B; }

//------------------------------------------------------------------------------------------------------------------------
//                                                                  GLTF SCAFFOLD
//------------------------------------------------------------------------------------------------------------------------

cgltf_data* ParseDoc(const std::string& Json)
{
    cgltf_options Opt{}; cgltf_data* Data = nullptr;
    if (cgltf_parse(&Opt, Json.c_str(), Json.size(), &Data) != cgltf_result_success) return nullptr;
    return Data;
}
std::string Doc(const char* Material, const char* Extra = "")
{
    std::string J = "{\"asset\":{\"version\":\"2.0\"},\"materials\":[";
    J += Material; J += "]";
    if (Extra && *Extra) { J += ","; J += Extra; }
    return J + "}";
}
const char* kTwoTextures = "\"textures\":[{\"source\":0},{\"source\":1}],\"images\":[{\"uri\":\"a.png\"},{\"uri\":\"b.png\"}]";
const char* kThreeTextures = "\"textures\":[{\"source\":0},{\"source\":1},{\"source\":2}],\"images\":[{\"uri\":\"a.png\"},{\"uri\":\"b.png\"},{\"uri\":\"c.png\"}]";

// Highest "index":N referenced by an encode (core + slate textures) → wrapper texture/image count.
int WrapperTextureCount(const std::string& J)
{
    int Max = -1; size_t At = 0;
    for (;;) { At = J.find("\"index\":", At); if (At == std::string::npos) break; Max = std::max(Max, std::atoi(J.c_str() + At + 8)); At += 8; }
    return Max + 1;
}
std::string WrapMaterial(const std::string& MaterialJson)
{
    std::string Extra;
    const int N = WrapperTextureCount(MaterialJson);
    if (N > 0)
    {
        Extra = "\"textures\":[";
        for (int I = 0; I < N; ++I) { if (I) Extra += ","; Extra += "{\"source\":" + std::to_string(I) + "}"; }
        Extra += "],\"images\":[";
        for (int I = 0; I < N; ++I) { if (I) Extra += ","; Extra += "{\"uri\":\"t" + std::to_string(I) + ".png\"}"; }
        Extra += "]";
    }
    return Doc(MaterialJson.c_str(), Extra.c_str());
}
// Identity harness: texture pointer → index within the parse = resident slot; encode maps the slot back.
GltfTextureResolver IdentityResolver(const cgltf_data* Data)
{
    return [Data](const cgltf_texture_view& View, bool) -> uint32_t
    {
        if (!View.texture) return kMaterialTextureNone;
        return static_cast<uint32_t>(View.texture - Data->textures);
    };
}
bool OnlyBound(const MaterialSlabDescriptor& S, std::initializer_list<MaterialTextureChannel> Allowed)
{
    for (uint32_t C = 0; C < kMaterialTextureChannelCount; ++C)
    {
        bool Want = false;
        for (MaterialTextureChannel A : Allowed) if (static_cast<uint32_t>(A) == C) Want = true;
        if (S.Textures[C].IsBound() != Want) return false;
    }
    return true;
}
MaterialDescriptor DecodeFirst(const std::string& Json, const MaterialDecodeConfiguration& Cfg = {})
{
    MaterialDescriptor D;
    cgltf_data* Data = ParseDoc(Json);
    if (Data && Data->materials_count > 0) D = MaterialCodec::DecodeGltf(&Data->materials[0], Cfg, IdentityResolver(Data));
    cgltf_free(Data);
    return D;
}
std::string EncodeIdentity(const MaterialDescriptor& D, std::vector<std::string>* Ext = nullptr)
{
    std::vector<std::string> Local; std::vector<std::string>& Out = Ext ? *Ext : Local;
    return MaterialCodec::EncodeGltf(D, Out, [](uint32_t Slot) { return static_cast<int>(Slot); });
}
double ExtractNumber(const std::string& J, const char* Key)
{
    const size_t At = J.find(std::string("\"") + Key + "\":");
    if (At == std::string::npos) return std::numeric_limits<double>::quiet_NaN();
    const char* P = J.c_str() + At + std::strlen(Key) + 3;
    if (*P == '[') ++P;   // array-valued keys (baseColorFactor): read the first component
    return std::strtod(P, nullptr);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                                TIER-2 COMPARE
//------------------------------------------------------------------------------------------------------------------------

// The 9 renormalising floats (peak folds, nm↔µm, 20/Abbe): relative-eps, everything else bitwise.
void ZeroRenorm(MaterialSlabDescriptor& S)
{
    S.FuzzWeight = 0.0f; S.FuzzColor[0] = S.FuzzColor[1] = S.FuzzColor[2] = 0.0f;
    S.EmissionLuminance = 0.0f; S.EmissionColor[0] = S.EmissionColor[1] = S.EmissionColor[2] = 0.0f;
    S.ThinFilmThickness = 0.0f; S.TransmissionDispersionAbbeNumber = 0.0f;
}
bool NearRel(float A, float B, float Eps = 1e-6f)
{
    if (A == B) return true;
    return std::fabs(A - B) <= Eps * std::max(1.0f, std::max(std::fabs(A), std::fabs(B)));
}
bool Tier2EqualSlab(const MaterialSlabDescriptor& A, const MaterialSlabDescriptor& B, std::string& Field)
{
    MaterialSlabDescriptor X = A, Y = B;
    ZeroRenorm(X); ZeroRenorm(Y);
    if (std::memcmp(&X, &Y, offsetof(MaterialSlabDescriptor, GeometryThinWalled)) != 0) { Field = "float-prefix"; return false; }
    if (X.GeometryThinWalled != Y.GeometryThinWalled) { Field = "thin-walled"; return false; }
    if (X.SlateAnisotropyRotation != Y.SlateAnisotropyRotation) { Field = "anisotropy-rotation"; return false; }
    if (X.SlateDirectF0Weight != Y.SlateDirectF0Weight) { Field = "direct-f0"; return false; }
    for (uint32_t C = 0u; C < kMaterialTextureChannelCount; ++C)   // memberwise: TextureReference has padding bytes
    {
        const TextureReference& P = X.Textures[C]; const TextureReference& Q = Y.Textures[C];
        if (P.Texture != Q.Texture || P.UvSet != Q.UvSet || P.Channel != Q.Channel || P.OffsetU != Q.OffsetU || P.OffsetV != Q.OffsetV
            || P.ScaleU != Q.ScaleV || P.ScaleV != Q.ScaleV || P.Rotation != Q.Rotation || P.Scalar != Q.Scalar) { Field = "textures"; return false; }
    }
    const char* Names[9] = { "fuzz-weight", "fuzz-c0", "fuzz-c1", "fuzz-c2", "emission-lum", "emission-c0", "emission-c1", "emission-c2", "film-or-abbe" };
    const float* AF[9] = { &A.FuzzWeight, A.FuzzColor, A.FuzzColor + 1, A.FuzzColor + 2, &A.EmissionLuminance, A.EmissionColor, A.EmissionColor + 1, A.EmissionColor + 2, &A.ThinFilmThickness };
    const float* BF[9] = { &B.FuzzWeight, B.FuzzColor, B.FuzzColor + 1, B.FuzzColor + 2, &B.EmissionLuminance, B.EmissionColor, B.EmissionColor + 1, B.EmissionColor + 2, &B.ThinFilmThickness };
    for (int I = 0; I < 9; ++I) if (!NearRel(*AF[I], *BF[I])) { Field = Names[I]; return false; }
    if (!NearRel(A.TransmissionDispersionAbbeNumber, B.TransmissionDispersionAbbeNumber)) { Field = "abbe"; return false; }
    return true;
}
bool Tier2Equal(const MaterialDescriptor& A, const MaterialDescriptor& B, std::string& Field)
{
    if (A.Name != B.Name) { Field = "name"; return false; }
    if (A.Flags != B.Flags) { Field = "flags"; return false; }
    if (A.AlphaCutoff != B.AlphaCutoff) { Field = "cutoff"; return false; }
    if (A.VolumeThickness != B.VolumeThickness) { Field = "volume-thickness"; return false; }
    if (A.Slabs.size() != B.Slabs.size()) { Field = "slab-count"; return false; }
    if (A.Operations != B.Operations) { Field = "operations"; return false; }
    for (size_t I = 0; I < A.Slabs.size(); ++I) if (!Tier2EqualSlab(A.Slabs[I], B.Slabs[I], Field)) { Field = "slab" + std::to_string(I) + "." + Field; return false; }
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                              GLTF CORE (T01–T05)
//------------------------------------------------------------------------------------------------------------------------

void T01_EmptyMaterial()
{
    cgltf_data* Data = ParseDoc(Doc("{}"));
    Check(Data && Data->materials_count == 1, "T01 parse");
    const bool HasPbr = Data->materials[0].has_pbr_metallic_roughness;
    MaterialDescriptor D = MaterialCodec::DecodeGltf(&Data->materials[0], {}, IdentityResolver(Data));
    cgltf_free(Data);
    Check(!HasPbr, "T01 cgltf leaves has_pbr false for {}");
    const MaterialSlabDescriptor& S = D.Slabs[0];
    Check(S.BaseColor[0] == 0.8f && S.BaseMetalness == 0.0f && S.SpecularRoughness == 0.5f && S.GeometryOpacity == 1.0f,
        "T01 empty material = descriptor defaults + R2 roughness");
    const std::string J1 = EncodeIdentity(D);
    Check(DecodeFirst(WrapMaterial(J1)) == D, "T01 round-trip ==");
    Check(EncodeIdentity(DecodeFirst(WrapMaterial(J1))) == J1, "T01 byte-idempotent");
}
void T02_NullMaterial()
{
    MaterialDescriptor D = MaterialCodec::DecodeGltf(nullptr, {}, nullptr);
    Check(D.Name == "fallback" && D.Slabs.size() == 1, "T02 null name/fallback");
    const MaterialSlabDescriptor& S = D.Slabs[0];
    Check(S.BaseColor[0] == 0.8f && S.SpecularRoughness == 0.5f && S.BaseMetalness == 0.0f, "T02 R2 fallback values");
}
void T03_CoreValues()
{
    MaterialDescriptor D = DecodeFirst(Doc("{\"name\":\"core\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.1,0.2,0.3,0.5],\"metallicFactor\":0.7,\"roughnessFactor\":0.4}}"));
    const MaterialSlabDescriptor& S = D.Slabs[0];
    Check(D.Name == "core", "T03 name");
    Check(S.BaseColor[0] == 0.1f && S.BaseColor[1] == 0.2f && S.BaseColor[2] == 0.3f, "T03 base colour");
    Check(S.GeometryOpacity == 0.5f && S.BaseMetalness == 0.7f && S.SpecularRoughness == 0.4f, "T03 opacity/metal/rough");
    const std::string J1 = EncodeIdentity(D);
    Check(static_cast<float>(ExtractNumber(J1, "baseColorFactor")) == 0.1f
        && static_cast<float>(ExtractNumber(J1, "metallicFactor")) == 0.7f
        && static_cast<float>(ExtractNumber(J1, "roughnessFactor")) == 0.4f, "T03 encode carries factors");
    Check(DecodeFirst(WrapMaterial(J1)) == D, "T03 round-trip ==");
    Check(EncodeIdentity(DecodeFirst(WrapMaterial(J1))) == J1, "T03 byte-idempotent");
}
void T04_AlphaMaskDoubleSided()
{
    MaterialDescriptor D = DecodeFirst(Doc("{\"alphaMode\":\"MASK\",\"alphaCutoff\":0.4,\"doubleSided\":true}"));
    Check((D.Flags & MaterialFlagAlphaMask) && !(D.Flags & MaterialFlagAlphaTranslucent), "T04 mask flag");
    Check(D.AlphaCutoff == 0.4f && (D.Flags & MaterialFlagDoubleSided), "T04 cutoff + double-sided");
    const std::string J1 = EncodeIdentity(D);
    Check(J1.find("\"alphaMode\":\"MASK\"") != std::string::npos && J1.find("\"doubleSided\":true") != std::string::npos, "T04 encode keys");
    Check(DecodeFirst(WrapMaterial(J1)) == D, "T04 round-trip ==");
    Check(EncodeIdentity(DecodeFirst(WrapMaterial(J1))) == J1, "T04 byte-idempotent");
}
void T05_AlphaBlend()
{
    MaterialDescriptor D = DecodeFirst(Doc("{\"alphaMode\":\"BLEND\"}"));
    Check((D.Flags & MaterialFlagAlphaTranslucent) && !(D.Flags & MaterialFlagAlphaMask), "T05 blend flag");
    const std::string J1 = EncodeIdentity(D);
    Check(DecodeFirst(WrapMaterial(J1)) == D && EncodeIdentity(DecodeFirst(WrapMaterial(J1))) == J1, "T05 round-trip + idempotent");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                           EXTENSIONS (T06–T18)
//------------------------------------------------------------------------------------------------------------------------

void T06_Ior()
{
    MaterialDescriptor D = DecodeFirst(Doc("{\"extensions\":{\"KHR_materials_ior\":{\"ior\":1.33000004}}}"));
    Check(D.Slabs[0].SpecularIor == 1.33000004f, "T06 ior value");
    const std::string J1 = EncodeIdentity(D);
    Check(DecodeFirst(WrapMaterial(J1)) == D && EncodeIdentity(DecodeFirst(WrapMaterial(J1))) == J1, "T06 round-trip + idempotent");
}
void T07_Specular()
{
    MaterialDescriptor D = DecodeFirst(Doc(
        "{\"extensions\":{\"KHR_materials_specular\":{\"specularFactor\":0.800000012,\"specularColorFactor\":[0.899999976,0.800000012,0.699999988],"
        "\"specularColorTexture\":{\"index\":0},\"specularTexture\":{\"index\":1}}}}", kTwoTextures));
    const MaterialSlabDescriptor& S = D.Slabs[0];
    Check(S.SpecularWeight == 0.800000012f, "T07 factor");
    Check(S.SpecularColor[0] == 0.899999976f && S.SpecularColor[2] == 0.699999988f, "T07 colour");
    Check(S.Texture(MaterialTextureChannel::SpecularColor).Texture == 0u, "T07 colour-texture slot");
    Check(OnlyBound(S, { MaterialTextureChannel::SpecularColor }), "T07 factor-texture dropped (lock)");
    const std::string J1 = EncodeIdentity(D);
    Check(J1.find("specularTexture") == std::string::npos || J1.find("specularColorTexture") != std::string::npos, "T07 encode keeps colour only");
    Check(J1.find("\"specularTexture\"") == std::string::npos, "T07 encode omits factor-texture (lock)");
    Check(DecodeFirst(WrapMaterial(J1)) == D, "T07 round-trip ==");
    Check(EncodeIdentity(DecodeFirst(WrapMaterial(J1))) == J1, "T07 byte-idempotent");
}
void T08_Anisotropy()
{
    MaterialDescriptor D = DecodeFirst(Doc(
        "{\"extensions\":{\"KHR_materials_anisotropy\":{\"anisotropyStrength\":0.600000024,\"anisotropyRotation\":1.20000005,\"anisotropyTexture\":{\"index\":0}}}}", kTwoTextures));
    const MaterialSlabDescriptor& S = D.Slabs[0];
    Check(S.SpecularRoughnessAnisotropy == 0.600000024f, "T08 strength");
    Check(S.SlateAnisotropyRotation == 1.20000005f, "T08 rotation radians verbatim (M2 home)");
    Check(S.Texture(MaterialTextureChannel::Anisotropy).Texture == 0u, "T08 texture slot");
    const std::string J1 = EncodeIdentity(D);
    Check(J1.find("slate_anisotropy_rotation") == std::string::npos, "T08 rotation not duplicated into extras (M2)");
    Check(DecodeFirst(WrapMaterial(J1)) == D && EncodeIdentity(DecodeFirst(WrapMaterial(J1))) == J1, "T08 round-trip + idempotent");
    MaterialDescriptor N = DecodeFirst(Doc("{\"extensions\":{\"KHR_materials_anisotropy\":{\"anisotropyStrength\":-0.5}}}"));
    Check(N.Slabs[0].SpecularRoughnessAnisotropy == -0.5f, "T08 Sultan-range negative decodes");
    const std::string NJ = EncodeIdentity(N);
    Check(NJ.find("anisotropyStrength") != std::string::npos, "T08 negative encodes (M2 != 0)");
    Check(DecodeFirst(WrapMaterial(NJ)) == N, "T08 negative round-trip ==");
}
void T09_Clearcoat()
{
    MaterialDescriptor D = DecodeFirst(Doc(
        "{\"extensions\":{\"KHR_materials_clearcoat\":{\"clearcoatFactor\":0.899999976,\"clearcoatRoughnessFactor\":0.100000001,"
        "\"clearcoatTexture\":{\"index\":0},\"clearcoatRoughnessTexture\":{\"index\":1},\"clearcoatNormalTexture\":{\"index\":2}}}}", kThreeTextures));
    const MaterialSlabDescriptor& S = D.Slabs[0];
    Check(S.CoatWeight == 0.899999976f && S.CoatRoughness == 0.100000001f && S.CoatIor == 1.5f, "T09 factor/roughness/spec-ior");
    Check(S.Texture(MaterialTextureChannel::Coat).Texture == 0u && S.Texture(MaterialTextureChannel::Coat).Channel == TextureChannelSelection::R, "T09 coat texture R");
    Check(S.Texture(MaterialTextureChannel::GeometryCoatNormal).Texture == 2u, "T09 coat normal slot");
    Check(OnlyBound(S, { MaterialTextureChannel::Coat, MaterialTextureChannel::GeometryCoatNormal }), "T09 nothing else bound");
    const std::string J1 = EncodeIdentity(D);
    Check(J1.find("clearcoatRoughnessTexture") == std::string::npos, "T09 roughness-texture dropped (lock)");
    Check(DecodeFirst(WrapMaterial(J1)) == D, "T09 round-trip ==");
    Check(EncodeIdentity(DecodeFirst(WrapMaterial(J1))) == J1, "T09 byte-idempotent");
}
void T10_Sheen()
{
    MaterialDescriptor A = DecodeFirst(Doc("{\"extensions\":{\"KHR_materials_sheen\":{\"sheenColorFactor\":[0.5,0.25,0.125],\"sheenRoughnessFactor\":0.300000012,\"sheenColorTexture\":{\"index\":0}}}}", kTwoTextures));
    const MaterialSlabDescriptor& SA = A.Slabs[0];
    Check(SA.FuzzWeight == 0.5f && SA.FuzzColor[0] == 1.0f && SA.FuzzColor[1] == 0.5f && SA.FuzzColor[2] == 0.25f, "T10a peak fold (binary-exact)");
    Check(SA.FuzzRoughness == 0.300000012f && SA.Texture(MaterialTextureChannel::Fuzz).Texture == 0u, "T10a roughness + slot");
    const std::string JA = EncodeIdentity(A);
    Check(DecodeFirst(WrapMaterial(JA)) == A && EncodeIdentity(DecodeFirst(WrapMaterial(JA))) == JA, "T10a tier-1 round-trip + idempotent");
    MaterialDescriptor B = DecodeFirst(Doc("{\"extensions\":{\"KHR_materials_sheen\":{\"sheenColorFactor\":[0.600000024,0.300000012,0.100000001],\"sheenRoughnessTexture\":{\"index\":1}}}}", kTwoTextures));
    Check(B.Slabs[0].FuzzWeight == 0.600000024f, "T10b peak fold (renorm case)");
    Check(OnlyBound(B.Slabs[0], {}), "T10b roughness-texture dropped (lock)");
    const std::string JB = EncodeIdentity(B);
    Check(JB.find("sheenRoughnessTexture") == std::string::npos, "T10b roughness-texture dropped (lock)");
    std::string F;
    MaterialDescriptor B1 = DecodeFirst(WrapMaterial(JB));
    Check(Tier2Equal(B, B1, F), "T10b tier-2 round-trip", F);
    MaterialDescriptor B2 = DecodeFirst(WrapMaterial(EncodeIdentity(B1)));
    Check(Tier2Equal(B, B2, F), "T10b bounded, no drift", F);
}
void T11_Transmission()
{
    MaterialDescriptor D = DecodeFirst(Doc(
        "{\"extensions\":{\"KHR_materials_transmission\":{\"transmissionFactor\":0.75,\"transmissionTexture\":{\"index\":0}}}}", kTwoTextures));
    Check(D.Slabs[0].TransmissionWeight == 0.75f, "T11 factor");
    Check(D.Slabs[0].Texture(MaterialTextureChannel::Transmission).Texture == 0u, "T11 texture slot");
    const std::string J1 = EncodeIdentity(D);
    Check(DecodeFirst(WrapMaterial(J1)) == D && EncodeIdentity(DecodeFirst(WrapMaterial(J1))) == J1, "T11 round-trip + idempotent");
}
void T12_Volume()
{
    std::vector<std::string> Ext;
    MaterialDescriptor D = DecodeFirst(Doc(
        "{\"extensions\":{\"KHR_materials_transmission\":{\"transmissionFactor\":1.0},\"KHR_materials_volume\":{\"thicknessFactor\":0.050000001,"
        "\"attenuationColor\":[0.800000012,0.200000003,0.100000001],\"attenuationDistance\":2.5,\"thicknessTexture\":{\"index\":0}}}}", kTwoTextures));
    const MaterialSlabDescriptor& S = D.Slabs[0];
    Check(S.TransmissionDepth == 2.5f, "T12 attenuation distance");
    Check(S.TransmissionColor[0] == 0.800000012f && S.TransmissionColor[2] == 0.100000001f, "T12 attenuation colour");
    Check(D.VolumeThickness == 0.050000001f, "T12 thickness fidelity carry (M6)");
    Check(OnlyBound(S, {}), "T12 thickness-texture dropped: zero slots (lock)");
    Check(!(D.Flags & MaterialFlagThinWalled) && !S.GeometryThinWalled, "T12 solid, not thin");
    const std::string J1 = EncodeIdentity(D, &Ext);
    Check(static_cast<float>(ExtractNumber(J1, "thicknessFactor")) == 0.050000001f, "T12 thicknessFactor re-encoded verbatim (M6)");
    Check(J1.find("thicknessTexture") == std::string::npos, "T12 thickness-texture dropped (lock)");
    Check(std::find(Ext.begin(), Ext.end(), "KHR_materials_transmission") != Ext.end()
        && std::find(Ext.begin(), Ext.end(), "KHR_materials_volume") != Ext.end(), "T12 extensionsUsed");
    Check(DecodeFirst(WrapMaterial(J1)) == D, "T12 round-trip ==");
    Check(EncodeIdentity(DecodeFirst(WrapMaterial(J1))) == J1, "T12 byte-idempotent");
}
void T13_VolumeThin()
{
    MaterialDescriptor D = DecodeFirst(Doc(
        "{\"extensions\":{\"KHR_materials_transmission\":{\"transmissionFactor\":1.0},\"KHR_materials_volume\":{\"thicknessFactor\":0.0,\"attenuationDistance\":1.5,\"attenuationColor\":[1.0,1.0,1.0]}}}"));
    Check((D.Flags & MaterialFlagThinWalled) && D.Slabs[0].GeometryThinWalled, "T13 zero thickness → thin");
    Check(D.Slabs[0].TransmissionDepth == 1.5f && D.VolumeThickness == 0.0f, "T13 depth kept, thickness 0");
    const std::string J1 = EncodeIdentity(D);
    Check(ExtractNumber(J1, "thicknessFactor") == 0.0, "T13 thin encodes thickness 0");
    Check(J1.find("\"geometry_thin_walled\":true") != std::string::npos, "T13 extras thin bool (M6)");
    Check(DecodeFirst(WrapMaterial(J1)) == D, "T13 round-trip ==");
    Check(EncodeIdentity(DecodeFirst(WrapMaterial(J1))) == J1, "T13 byte-idempotent");
}
void T14_AuthoredDepthFallback()
{
    MaterialDescriptor D; D.Slabs.emplace_back();
    D.Slabs[0].TransmissionWeight = 1.0f; D.Slabs[0].TransmissionDepth = 1.5f;
    const std::string J1 = EncodeIdentity(D);
    Check(ExtractNumber(J1, "thicknessFactor") == 1.5, "T14 authored slab: depth as the lossy scale (M6)");
}
void T15_FlagOnlyThin()
{
    MaterialDescriptor D; D.Slabs.emplace_back();
    D.Flags |= MaterialFlagThinWalled;   // slab bool deliberately false — inconsistent hand-built input
    D.Slabs[0].TransmissionWeight = 1.0f; D.Slabs[0].TransmissionDepth = 1.5f;
    const std::string J1 = EncodeIdentity(D);
    Check(ExtractNumber(J1, "thicknessFactor") == 0.0, "T15 flag-only still encodes thin (M6)");
    Check(J1.find("\"geometry_thin_walled\":true") != std::string::npos, "T15 extras thin bool");
}
void T16_Dispersion()
{
    MaterialDescriptor D = DecodeFirst(Doc(
        "{\"extensions\":{\"KHR_materials_transmission\":{\"transmissionFactor\":1.0},\"KHR_materials_dispersion\":{\"dispersion\":0.02}}}"));
    Check(D.Slabs[0].TransmissionDispersionScale == 1.0f, "T16 scale enabled");
    Check(NearRel(D.Slabs[0].TransmissionDispersionAbbeNumber, 1000.0f), "T16 Abbe = 20 / dispersion", F(D.Slabs[0].TransmissionDispersionAbbeNumber));
    const std::string J1 = EncodeIdentity(D);
    Check(NearRel(static_cast<float>(ExtractNumber(J1, "dispersion")), 0.02f), "T16 dispersion re-encoded");
    std::string F;
    MaterialDescriptor D1 = DecodeFirst(WrapMaterial(J1));
    Check(Tier2Equal(D, D1, F), "T16 tier-2 round-trip", F);
    MaterialDescriptor D2 = DecodeFirst(WrapMaterial(EncodeIdentity(D1)));
    Check(Tier2Equal(D, D2, F), "T16 bounded, no drift", F);
    MaterialDescriptor Z = DecodeFirst(Doc("{\"extensions\":{\"KHR_materials_dispersion\":{\"dispersion\":0.0}}}"));
    Check(Z.Slabs[0].TransmissionDispersionScale == 0.0f, "T16 zero dispersion stays off");
}
void T17_Iridescence()
{
    std::vector<std::string> Ext;
    MaterialDescriptor D = DecodeFirst(Doc(
        "{\"extensions\":{\"KHR_materials_iridescence\":{\"iridescenceFactor\":0.600000024,\"iridescenceIor\":1.79999995,"
        "\"iridescenceThicknessMinimum\":100.0,\"iridescenceThicknessMaximum\":400.0,\"iridescenceTexture\":{\"index\":0},\"iridescenceThicknessTexture\":{\"index\":1}}}}", kTwoTextures));
    const MaterialSlabDescriptor& S = D.Slabs[0];
    Check(S.ThinFilmWeight == 0.600000024f && S.ThinFilmIor == 1.79999995f, "T17 factor + ior");
    Check(NearRel(S.ThinFilmThickness, 0.4f), "T17 max × 0.001 nm→µm (min collapses)", F(S.ThinFilmThickness));
    Check(S.Texture(MaterialTextureChannel::ThinFilm).Texture == 0u, "T17 texture slot");
    Check(OnlyBound(S, { MaterialTextureChannel::ThinFilm }), "T17 thickness-texture dropped (lock)");
    const std::string J1 = EncodeIdentity(D, &Ext);
    Check(J1.find("iridescenceThicknessTexture") == std::string::npos, "T17 thickness-texture dropped (lock)");
    Check(ExtractNumber(J1, "iridescenceThicknessMinimum") == ExtractNumber(J1, "iridescenceThicknessMaximum"), "T17 min == max on encode");
    Check(std::find(Ext.begin(), Ext.end(), "KHR_materials_iridescence") != Ext.end(), "T17 extensionsUsed");
    std::string F;
    MaterialDescriptor D1 = DecodeFirst(WrapMaterial(J1));
    Check(Tier2Equal(D, D1, F), "T17 tier-2 round-trip", F);
    MaterialDescriptor D2 = DecodeFirst(WrapMaterial(EncodeIdentity(D1)));
    Check(Tier2Equal(D, D2, F), "T17 bounded, no drift", F);
}
void T18_DiffuseTransmission()
{
    MaterialDescriptor D = DecodeFirst(Doc(
        "{\"extensions\":{\"KHR_materials_diffuse_transmission\":{\"diffuseTransmissionFactor\":0.5,\"diffuseTransmissionColorFactor\":[0.899999976,0.699999988,0.5],"
        "\"diffuseTransmissionTexture\":{\"index\":0},\"diffuseTransmissionColorTexture\":{\"index\":1}}}}", kTwoTextures));
    const MaterialSlabDescriptor& S = D.Slabs[0];
    Check(S.SubsurfaceWeight == 0.5f, "T18 factor");
    Check(S.SubsurfaceColor[0] == 0.899999976f && S.SubsurfaceColor[2] == 0.5f, "T18 colour");
    Check(S.Texture(MaterialTextureChannel::Subsurface).Texture == 0u && S.Texture(MaterialTextureChannel::Subsurface).Channel == TextureChannelSelection::A, "T18 texture .A");
    Check(OnlyBound(S, { MaterialTextureChannel::Subsurface }), "T18 colour-texture dropped (lock)");
    Check((D.Flags & MaterialFlagThinWalled) && S.GeometryThinWalled, "T18 thin-walled");
    const std::string J1 = EncodeIdentity(D);
    Check(J1.find("\"diffuseTransmissionTexture\"") != std::string::npos, "T18 texture re-encoded (M6)");
    Check(J1.find("diffuseTransmissionColorTexture") == std::string::npos, "T18 colour-texture dropped (lock)");
    Check(DecodeFirst(WrapMaterial(J1)) == D, "T18 round-trip ==");
    Check(EncodeIdentity(DecodeFirst(WrapMaterial(J1))) == J1, "T18 byte-idempotent");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     EMISSION / ALPHA / MAPS (T19–T28)
//------------------------------------------------------------------------------------------------------------------------

void T19_EmissiveNoStrength()
{
    MaterialDescriptor D = DecodeFirst(Doc("{\"emissiveFactor\":[1.0,0.5,0.25],\"extensions\":{\"KHR_materials_unlit\":{}}}"));
    Check((D.Flags & MaterialFlagUnlit), "T19 unlit flag");
    Check(D.Slabs[0].EmissionLuminance == 1.0f, "T19 luminance = peak");
    Check(D.Slabs[0].EmissionColor[1] == 0.5f && D.Slabs[0].EmissionColor[2] == 0.25f, "T19 colour passthrough");
    const std::string J1 = EncodeIdentity(D);
    Check(ExtractNumber(J1, "emissiveStrength") == 1.0, "T19 strength always written (= 1)");
    Check(DecodeFirst(WrapMaterial(J1)) == D && EncodeIdentity(DecodeFirst(WrapMaterial(J1))) == J1, "T19 tier-1 round-trip + idempotent");
}
void T20_EmissiveStrength()
{
    MaterialDescriptor A = DecodeFirst(Doc("{\"emissiveFactor\":[1.0,1.0,1.0],\"extensions\":{\"KHR_materials_emissive_strength\":{\"emissiveStrength\":2.5}}}"));
    Check(A.Slabs[0].EmissionLuminance == 2.5f, "T20a uniform fold");
    const std::string JA = EncodeIdentity(A);
    Check(DecodeFirst(WrapMaterial(JA)) == A && EncodeIdentity(DecodeFirst(WrapMaterial(JA))) == JA, "T20a tier-1 round-trip + idempotent");
    MaterialDescriptor B = DecodeFirst(Doc("{\"emissiveFactor\":[1.0,0.300000012,0.100000001],\"extensions\":{\"KHR_materials_emissive_strength\":{\"emissiveStrength\":2.5}}}"));
    Check(B.Slabs[0].EmissionLuminance == 2.5f, "T20b peak fold (renorm case)");
    const std::string JB = EncodeIdentity(B);
    std::string F;
    MaterialDescriptor B1 = DecodeFirst(WrapMaterial(JB));
    Check(Tier2Equal(B, B1, F), "T20b tier-2 round-trip", F);
    MaterialDescriptor B2 = DecodeFirst(WrapMaterial(EncodeIdentity(B1)));
    Check(Tier2Equal(B, B2, F), "T20b bounded, no drift", F);
    MaterialDecodeConfiguration Fold; Fold.EmissiveRadiance = 2.0f;
    MaterialDescriptor C = DecodeFirst(Doc("{\"emissiveFactor\":[1.0,1.0,1.0],\"extensions\":{\"KHR_materials_emissive_strength\":{\"emissiveStrength\":2.5}}}"), Fold);
    Check(C.Slabs[0].EmissionLuminance == 5.0f, "T20c config fold ×2 (documented lossy)");
}
void T21_TextureTransform()
{
    MaterialDescriptor D = DecodeFirst(Doc(
        "{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0,\"texCoord\":1,\"extensions\":{\"KHR_texture_transform\":{"
        "\"offset\":[0.100000001,0.200000003],\"rotation\":0.5,\"scale\":[2.0,3.0],\"texCoord\":2}}}}}", kTwoTextures));
    const TextureReference& T = D.Slabs[0].Texture(MaterialTextureChannel::BaseColor);
    Check(T.Texture == 0u && T.UvSet == 2u, "T21 texCoord override wins");
    Check(T.OffsetU == 0.100000001f && T.OffsetV == 0.200000003f && T.Rotation == 0.5f && T.ScaleU == 2.0f && T.ScaleV == 3.0f, "T21 affine verbatim");
    const std::string J1 = EncodeIdentity(D);
    Check(J1.find("KHR_texture_transform") != std::string::npos, "T21 transform re-encoded");
    const std::string J2 = EncodeIdentity(DecodeFirst(WrapMaterial(J1)));
    Check(J1 == J2, "T21 normalisation converges (byte-idempotent)");
    Check(DecodeFirst(WrapMaterial(J1)) == D, "T21 round-trip ==");
}
void T22_NormalScaleOcclusionStrength()
{
    MaterialDescriptor D = DecodeFirst(Doc("{\"normalTexture\":{\"index\":0,\"scale\":2.0},\"occlusionTexture\":{\"index\":1,\"strength\":0.5}}", kTwoTextures));
    Check(D.Slabs[0].Texture(MaterialTextureChannel::GeometryNormal).Scalar == 2.0f, "T22 normal scale → Scalar");
    Check(D.Slabs[0].Texture(MaterialTextureChannel::Occlusion).Scalar == 0.5f, "T22 occlusion strength → Scalar");
    Check(D.Slabs[0].Texture(MaterialTextureChannel::Occlusion).Channel == TextureChannelSelection::R, "T22 occlusion .R");
    const std::string J1 = EncodeIdentity(D);
    Check(DecodeFirst(WrapMaterial(J1)) == D && EncodeIdentity(DecodeFirst(WrapMaterial(J1))) == J1, "T22 round-trip + idempotent");
}
void T23_MetalRoughFallback()
{
    MaterialDescriptor D; D.Slabs.emplace_back();
    D.Slabs[0].Texture(MaterialTextureChannel::Metalness).Texture = 7u;   // metalness-only bind, no resolver needed
    const std::string J1 = EncodeIdentity(D);
    Check(J1.find("\"metallicRoughnessTexture\":{\"index\":7}") != std::string::npos, "T23 metalness-only survives (M6)");
    MaterialDescriptor R; R.Slabs.emplace_back();
    R.Slabs[0].Texture(MaterialTextureChannel::SpecularRoughness).Texture = 3u;
    Check(EncodeIdentity(R).find("\"metallicRoughnessTexture\":{\"index\":3}") != std::string::npos, "T23 roughness-only uses its own");
    MaterialDescriptor D1 = DecodeFirst(WrapMaterial(J1));
    Check(D1.Slabs[0].Texture(MaterialTextureChannel::Metalness).Texture == 7u
        && D1.Slabs[0].Texture(MaterialTextureChannel::SpecularRoughness).Texture == 7u, "T23 decode binds both (converges)");
    Check(EncodeIdentity(D1) == J1, "T23 byte-idempotent");
}
void T24_ExtrasFlat()
{
    MaterialDescriptor D; D.Slabs.emplace_back(); D.Name = "flat";
    MaterialSlabDescriptor& S = D.Slabs[0];
    S.SlateDirectF0Weight = 0.699999988f; S.BaseDiffuseRoughness = 0.300000012f; S.CoatIor = 2.0f; S.SlateGlintDensity = 5.0f;
    const std::string J1 = EncodeIdentity(D);
    Check(J1.find("\"slate_direct_f0_weight\"") != std::string::npos, "T24 D-f0 in extras (plan item)");
    Check(J1.find("\"base_color\"") == std::string::npos && J1.find("\"metalness\"") == std::string::npos, "T24 covered params omitted");
    MaterialDescriptor Back = DecodeFirst(WrapMaterial(J1));
    Check(Back == D, "T24 round-trip ==");
    Check(EncodeIdentity(Back) == J1, "T24 byte-idempotent");
    MaterialDescriptor U; U.Slabs.emplace_back();
    Check(MaterialCodec::DecodeSlateExtras("{\"slate_direct_f0_weight\":0.25}", U), "T24 extras unit: Found");
    Check(U.Slabs[0].SlateDirectF0Weight == 0.25f, "T24 extras unit: value");
    MaterialDescriptor V; V.Slabs.emplace_back();
    Check(!MaterialCodec::DecodeSlateExtras("{\"unrelated\":1}", V), "T24 extras unit: no slate block");
}
void T25_ExtrasGraph()
{
    MaterialDescriptor D; D.Name = "graph";
    D.Slabs.resize(2);
    D.Slabs[0].BaseColor[0] = 0.25f; D.Slabs[0].GeometryThinWalled = true;   // top: thin red-ish
    D.Slabs[0].Texture(MaterialTextureChannel::BaseColor) = TextureReference{};
    D.Slabs[0].Texture(MaterialTextureChannel::BaseColor).Texture = 0u;
    D.Slabs[1].CoatWeight = 0.5f;   // bottom = core
    D.Slabs[1].Texture(MaterialTextureChannel::Coat).Texture = 1u;
    D.Slabs[1].Texture(MaterialTextureChannel::Coat).UvSet = 2u;
    D.Slabs[1].Texture(MaterialTextureChannel::Coat).Channel = TextureChannelSelection::G;
    D.Slabs[1].Texture(MaterialTextureChannel::Coat).OffsetU = 0.5f;
    D.Slabs[1].Texture(MaterialTextureChannel::Coat).ScaleU = 2.0f;
    D.Slabs[1].Texture(MaterialTextureChannel::Coat).Rotation = 1.0f;
    D.Slabs[1].Texture(MaterialTextureChannel::Coat).Scalar = 0.75f;
    MaterialOperation Op; Op.Category = MaterialOperationCategory::HorizontalMix; Op.Left = 0u; Op.Right = 1u; Op.Weight = 0.300000012f;
    Op.Mask.Texture = 2u; Op.Mask.Channel = TextureChannelSelection::R;
    D.Operations.push_back(Op);
    const std::string J1 = EncodeIdentity(D);
    Check(J1.find("slate_slabs") != std::string::npos && J1.find("slate_operations") != std::string::npos, "T25 graph keys");
    MaterialDescriptor Back = DecodeFirst(WrapMaterial(J1));
    Check(Back == D, "T25 graph round-trip == (bitwise, incl. ops + full TextureReferences)");
    Check(EncodeIdentity(Back) == J1, "T25 byte-idempotent");
    // Bottom-slab core inheritance (E11 semantic): core texture + graph extras WITHOUT textures.
    MaterialDescriptor Inh = DecodeFirst(Doc(
        "{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0}},"
        "\"extras\":{\"slate_slabs\":[{\"base_weight\":0.5},{\"base_weight\":1.0}]}}", kTwoTextures));
    Check(Inh.Slabs.size() == 2, "T25b graph replaces slabs");
    Check(Inh.Slabs[1].Texture(MaterialTextureChannel::BaseColor).Texture == 0u, "T25b bottom inherits the core slot");
    Check(!Inh.Slabs[0].Texture(MaterialTextureChannel::BaseColor).IsBound(), "T25b top inherits nothing");
}
void T26_ExtrasOverride()
{
    MaterialDescriptor D = DecodeFirst(Doc(
        "{\"pbrMetallicRoughness\":{\"baseColorFactor\":[1.0,0.0,0.0,1.0]},\"extras\":{\"base_color\":[0.0,1.0,0.0]}}"));
    Check(D.Slabs[0].BaseColor[1] == 1.0f && D.Slabs[0].BaseColor[0] == 0.0f, "T26 extras override core");
    const std::string J1 = EncodeIdentity(D);
    Check(J1.find("\"extras\"") == std::string::npos, "T26 encode normalises covered params into core");
    Check(DecodeFirst(WrapMaterial(J1)) == D, "T26 converges");
}
void T27_SpecGloss()
{
    MaterialDescriptor D = DecodeFirst(Doc(
        "{\"extensions\":{\"KHR_materials_pbrSpecularGlossiness\":{\"diffuseFactor\":[0.200000003,0.300000012,0.400000006,1.0],"
        "\"specularFactor\":[0.899999976,0.899999976,0.899999976],\"glossinessFactor\":0.800000012}}}"));
    const MaterialSlabDescriptor& S = D.Slabs[0];
    Check(S.BaseColor[1] == 0.300000012f && S.BaseMetalness == 0.0f, "T27 diffuse → base dielectric");
    Check(NearRel(S.SpecularRoughness, 0.2f), "T27 1 − glossiness", F(S.SpecularRoughness));
    Check(S.SpecularColor[0] == 0.899999976f, "T27 specular → tint");
}
void T28_OpacityRidesBaseAlpha()
{
    MaterialDescriptor D = DecodeFirst(Doc("{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0}}}", kTwoTextures));
    Check(D.Slabs[0].Texture(MaterialTextureChannel::BaseColor).Texture == 0u, "T28 base slot");
    Check(D.Slabs[0].Texture(MaterialTextureChannel::GeometryOpacity).Texture == 0u
        && D.Slabs[0].Texture(MaterialTextureChannel::GeometryOpacity).Channel == TextureChannelSelection::A, "T28 opacity .A same image");
    const std::string J1 = EncodeIdentity(D);
    Check(J1.find("baseColorTexture") != std::string::npos, "T28 single texture re-encoded");
    Check(DecodeFirst(WrapMaterial(J1)) == D && EncodeIdentity(DecodeFirst(WrapMaterial(J1))) == J1, "T28 round-trip + idempotent");
}
void T29_OpaqueThinExtrasOnly()
{
    MaterialDescriptor D = DecodeFirst(Doc("{\"extras\":{\"geometry_thin_walled\":true}}"));
    Check((D.Flags & MaterialFlagThinWalled) && D.Slabs[0].GeometryThinWalled, "T29 flat thin bool sets both (M6)");
    const std::string J1 = EncodeIdentity(D);
    Check(DecodeFirst(WrapMaterial(J1)) == D && EncodeIdentity(DecodeFirst(WrapMaterial(J1))) == J1, "T29 round-trip + idempotent");
    MaterialDescriptor C = DecodeFirst(Doc(
        "{\"extensions\":{\"KHR_materials_transmission\":{\"transmissionFactor\":1.0},\"KHR_materials_volume\":{\"thicknessFactor\":0.0,\"attenuationDistance\":1.0}},"
        "\"extras\":{\"geometry_thin_walled\":false}}"));
    Check(!(C.Flags & MaterialFlagThinWalled) && !C.Slabs[0].GeometryThinWalled, "T29 explicit false clears core thin (override)");
}
void T30_ShininessLocks()
{
    Check(MaterialCodec::RoughnessFromShininess(0.0f) == 1.0f, "T30 n=0 → 1");
    Check(NearRel(MaterialCodec::RoughnessFromShininess(2.0f), 0.840896f), "T30 n=2", F(MaterialCodec::RoughnessFromShininess(2.0f)));
    Check(NearRel(MaterialCodec::RoughnessFromShininess(60.0f), 0.4237986f), "T30 n=60", F(MaterialCodec::RoughnessFromShininess(60.0f)));
    Check(MaterialCodec::RoughnessFromShininess(1000.0f) < 0.22f, "T30 n=1000 small");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                                     OBJ (T31–T36)
//------------------------------------------------------------------------------------------------------------------------

MaterialCodec::ObjMaterialSource ObjSrc(const char* Name)
{
    MaterialCodec::ObjMaterialSource S; S.Name = Name; return S;
}
void T31_ObjGlass()
{
    MaterialCodec::ObjMaterialSource S = ObjSrc("glass");
    S.Illum = 4; S.d = 0.5f; S.Ni = 1.52f;
    S.Tf[0] = 0.9f; S.Tf[1] = 0.95f; S.Tf[2] = 1.0f;
    MaterialDescriptor D = MaterialCodec::DecodeObj(S, {}, nullptr);
    Check(D.Slabs[0].TransmissionWeight == 0.5f, "T31 transmission = 1 − d");
    Check(D.Slabs[0].GeometryOpacity == 1.0f, "T31 opacity stays 1");
    Check(D.Slabs[0].SpecularIor == 1.52f, "T31 ior");
    Check(D.Slabs[0].TransmissionColor[2] == 1.0f, "T31 Tf colour");
    Check(!(D.Flags & MaterialFlagAlphaMask), "T31 no cutout flag (M6)");
    Check((D.Flags & MaterialFlagDoubleSided), "T31 OBJ renders double-sided");
}
void T32_ObjTransparentPlastic()
{
    MaterialCodec::ObjMaterialSource S = ObjSrc("plastic");
    S.Illum = 2; S.d = 0.4f; S.Ns = 32.0f;
    S.Kd[0] = 0.8f; S.Kd[1] = 0.1f; S.Kd[2] = 0.1f;
    MaterialDescriptor D = MaterialCodec::DecodeObj(S, {}, nullptr);
    Check(NearRel(D.Slabs[0].TransmissionWeight, 0.6f), "T32 non-glass transparency → transmission (M6 fix)");
    Check(D.Slabs[0].GeometryOpacity == 1.0f, "T32 opacity stays 1");
    Check(!(D.Flags & MaterialFlagAlphaMask) && !(D.Flags & MaterialFlagAlphaTranslucent), "T32 no alpha flags without MapD");
    Check(D.Slabs[0].SpecularRoughness == MaterialCodec::RoughnessFromShininess(32.0f), "T32 Ns inversion shared");
}
void T33_ObjMetalHeuristic()
{
    MaterialCodec::ObjMaterialSource M = ObjSrc("metal");
    M.Illum = 3; M.Ks[0] = 0.9f; M.Ks[1] = 0.7f; M.Ks[2] = 0.5f;
    MaterialDescriptor D = MaterialCodec::DecodeObj(M, {}, nullptr);
    Check(D.Slabs[0].BaseMetalness == 1.0f && D.Slabs[0].BaseColor[0] == 0.9f, "T33a illum-3 metal");
    MaterialCodec::ObjMaterialSource T = ObjSrc("tint");
    T.Ks[0] = 0.5f; T.Ks[1] = 0.4f; T.Ks[2] = 0.3f;
    T.Kd[0] = 0.8f; T.Kd[1] = 0.8f; T.Kd[2] = 0.8f;
    MaterialDescriptor E = MaterialCodec::DecodeObj(T, {}, nullptr);
    Check(E.Slabs[0].BaseMetalness == 0.0f && E.Slabs[0].SpecularWeight == 0.5f, "T33b dielectric tint weight = peak");
    Check(E.Slabs[0].SpecularColor[0] == 1.0f && NearRel(E.Slabs[0].SpecularColor[2], 0.6f), "T33b tint normalised");
    MaterialCodec::ObjMaterialSource Z = ObjSrc("matte");
    MaterialDescriptor F = MaterialCodec::DecodeObj(Z, {}, nullptr);
    Check(F.Slabs[0].SpecularWeight == 0.0f, "T33c Ks=0 kills specular");
}
void T34_ObjEmission()
{
    MaterialCodec::ObjMaterialSource S = ObjSrc("lamp");
    S.Ke[1] = 2.0f;
    MaterialDescriptor D = MaterialCodec::DecodeObj(S, {}, nullptr);
    Check(D.Slabs[0].EmissionLuminance == 2.0f && D.Slabs[0].EmissionColor[1] == 1.0f, "T34 Ke peak fold");
}
void T35_ObjMaps()
{
    std::map<std::string, uint32_t> Slots;
    PathTextureResolver Resolve = [&](const std::string& Path, bool) -> uint32_t
    {
        auto F = Slots.find(Path);
        if (F != Slots.end()) return F->second;
        const uint32_t Slot = static_cast<uint32_t>(Slots.size());
        return Slots.emplace(Path, Slot).first->second;
    };
    MaterialCodec::ObjMaterialSource S = ObjSrc("mapped");
    S.MapKd = "kd.png"; S.MapKs = "ks.png"; S.MapKe = "ke.png"; S.MapD = "d.png"; S.MapBump = "b.png"; S.MapNs = "ns.png";
    MaterialDescriptor D = MaterialCodec::DecodeObj(S, {}, Resolve);
    Check(D.Slabs[0].Texture(MaterialTextureChannel::BaseColor).IsBound(), "T35a MapKd bound");
    Check(D.Slabs[0].Texture(MaterialTextureChannel::SpecularColor).IsBound(), "T35b MapKs bound");
    Check(D.Slabs[0].Texture(MaterialTextureChannel::Emission).IsBound(), "T35c MapKe bound");
    Check(D.Slabs[0].Texture(MaterialTextureChannel::GeometryNormal).IsBound(), "T35d MapBump bound");
    Check(D.Slabs[0].Texture(MaterialTextureChannel::GeometryOpacity).IsBound()
        && D.Slabs[0].Texture(MaterialTextureChannel::GeometryOpacity).Channel == TextureChannelSelection::R, "T35e MapD → opacity .R");
    Check((D.Flags & MaterialFlagAlphaTranslucent) && !(D.Flags & MaterialFlagAlphaMask), "T35f MapD = BLEND coverage (M6)");
    Check(!D.Slabs[0].Texture(MaterialTextureChannel::SpecularRoughness).IsBound(), "T35g MapNs unbound (lock)");
    MaterialDescriptor N = MaterialCodec::DecodeObj(S, {}, nullptr);
    Check(!(N.Flags & MaterialFlagAlphaTranslucent), "T35h no resolver → no blend flag");
}
void T36_FastObjTrFold()
{
    const char* ObjPath = "/tmp/M6TrFold.obj", *MtlPath = "/tmp/M6TrFold.mtl";
    std::FILE* O = std::fopen(ObjPath, "w");
    std::FILE* Mt = std::fopen(MtlPath, "w");
    Check(O && Mt, "T36 temp files");
    if (!O || !Mt) { if (O) std::fclose(O); if (Mt) std::fclose(Mt); return; }
    std::fputs("mtllib M6TrFold.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl t\nf 1 2 3\nusemtl d\nf 1 2 3\n", O);
    std::fputs("newmtl t\nTr 0.3\nnewmtl d\nd 0.8\nTr 0.3\n", Mt);
    std::fclose(O); std::fclose(Mt);
    fastObjMesh* Mesh = fast_obj_read(ObjPath);
    Check(Mesh && Mesh->material_count == 2u, "T36 fast_obj parses (named slots only)");
    float TrD = -1.0f, DD = -1.0f;
    if (Mesh) for (unsigned I = 0; I < Mesh->material_count; ++I)
    {
        if (Mesh->materials[I].name && std::strcmp(Mesh->materials[I].name, "t") == 0) TrD = Mesh->materials[I].d;
        if (Mesh->materials[I].name && std::strcmp(Mesh->materials[I].name, "d") == 0) DD = Mesh->materials[I].d;
    }
    Check(NearRel(TrD, 0.7f), "T36 Tr folds to 1 − Tr", F(TrD));
    Check(NearRel(DD, 0.8f), "T36 explicit d wins over Tr", F(DD));
    fast_obj_destroy(Mesh);
    std::remove(ObjPath); std::remove(MtlPath);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                                     FBX (T37–T41)
//------------------------------------------------------------------------------------------------------------------------

void SetReal(ufbx_material_map& Map, double V) { Map.has_value = true; Map.value_components = 1; Map.value_real = V; }
void SetVec3(ufbx_material_map& Map, double X, double Y, double Z)
{
    Map.has_value = true; Map.value_components = 3; Map.value_vec3.x = X; Map.value_vec3.y = Y; Map.value_vec3.z = Z;
}
void T37_FbxFullHouse()
{
    static ufbx_material M; std::memset(&M, 0, sizeof M);
    static ufbx_texture Tex[4]; std::memset(Tex, 0, sizeof Tex);
    M.name.data = "full"; M.name.length = 4;
    ufbx_material_pbr_maps& P = M.pbr;
    SetReal(P.base_factor, 0.9); SetVec3(P.base_color, 0.2, 0.3, 0.4);
    P.base_color.texture = &Tex[0]; P.base_color.texture_enabled = true;
    SetReal(P.metalness, 0.8);
    P.metalness.texture = &Tex[1]; P.metalness.texture_enabled = true;
    SetReal(P.diffuse_roughness, 0.2);
    SetReal(P.specular_factor, 0.7); SetVec3(P.specular_color, 1.0, 0.9, 0.8);
    SetReal(P.specular_ior, 1.6); SetReal(P.specular_anisotropy, 0.5); SetReal(P.specular_rotation, 0.25);
    SetReal(P.roughness, 0.35);
    SetReal(P.transmission_factor, 0.6); SetVec3(P.transmission_color, 0.9, 0.9, 1.0);
    SetReal(P.transmission_depth, 3.0); SetVec3(P.transmission_scatter, 0.1, 0.2, 0.3);
    SetReal(P.transmission_scatter_anisotropy, 0.4); SetReal(P.transmission_dispersion, 30.0);
    SetReal(P.subsurface_factor, 0.5); SetVec3(P.subsurface_color, 0.8, 0.4, 0.2);
    SetVec3(P.subsurface_radius, 2.0, 1.0, 0.5); SetReal(P.subsurface_scale, 0.5); SetReal(P.subsurface_anisotropy, 0.1);
    SetReal(P.sheen_factor, 0.3); SetVec3(P.sheen_color, 1.0, 0.5, 0.25); SetReal(P.sheen_roughness, 0.6);
    SetReal(P.coat_factor, 0.8); SetVec3(P.coat_color, 0.9, 0.9, 0.9);
    SetReal(P.coat_roughness, 0.15); SetReal(P.coat_ior, 1.7); SetReal(P.coat_anisotropy, 0.4);
    SetReal(P.thin_film_factor, 0.9); SetReal(P.thin_film_thickness, 500.0); SetReal(P.thin_film_ior, 1.45);
    SetReal(P.emission_factor, 2.0); SetVec3(P.emission_color, 1.0, 0.5, 0.25);
    SetReal(P.opacity, 0.9);
    P.opacity.texture = &Tex[2]; P.opacity.texture_enabled = true;
    P.normal_map.texture = &Tex[3]; P.normal_map.texture_enabled = true;
    M.features.thin_walled.enabled = true; M.features.double_sided.enabled = true;
    std::map<const void*, uint32_t> Slots;
    FbxTextureResolver Resolve = [&](const void* T, bool) -> uint32_t
    {
        auto F = Slots.find(T);
        if (F != Slots.end()) return F->second;
        const uint32_t Slot = static_cast<uint32_t>(Slots.size());
        return Slots.emplace(T, Slot).first->second;
    };
    MaterialDescriptor D = MaterialCodec::DecodeFbx(&M, {}, Resolve);
    const MaterialSlabDescriptor& S = D.Slabs[0];
    Check(D.Name == "full", "T37 name");
    Check(S.BaseWeight == 0.9f && S.BaseColor[1] == 0.3f, "T37 base");
    Check(S.Texture(MaterialTextureChannel::BaseColor).Texture == 0u, "T37 base texture first slot");
    Check(S.BaseMetalness == 0.8f && S.Texture(MaterialTextureChannel::Metalness).Channel == TextureChannelSelection::R, "T37 metalness + .R");
    Check(S.BaseDiffuseRoughness == 0.2f, "T37 diffuse roughness");
    Check(S.SpecularWeight == 0.7f && S.SpecularColor[2] == 0.8f && S.SpecularIor == 1.6f, "T37 specular triple");
    Check(S.SpecularRoughnessAnisotropy == 0.5f, "T37 specular anisotropy");
    Check(NearRel(S.SlateAnisotropyRotation, 1.57079633f), "T37 rotation turns → radians (M6)", F(S.SlateAnisotropyRotation));
    Check(S.SpecularRoughness == 0.35f, "T37 roughness direct");
    Check(S.TransmissionWeight == 0.6f && S.TransmissionColor[2] == 1.0f && S.TransmissionDepth == 3.0f, "T37 transmission triple");
    Check(S.TransmissionScatter[1] == 0.2f && S.TransmissionScatterAnisotropy == 0.4f, "T37 scatter pair");
    Check(S.TransmissionDispersionScale == 1.0f && S.TransmissionDispersionAbbeNumber == 30.0f, "T37 dispersion = Abbe direct");
    Check(S.SubsurfaceWeight == 0.5f && S.SubsurfaceColor[0] == 0.8f, "T37 subsurface pair");
    Check(S.SubsurfaceRadiusScale[0] == 2.0f && S.SubsurfaceRadiusScale[2] == 0.5f && S.SubsurfaceRadius == 0.5f, "T37 radius → scale, scale → radius [m]");
    Check(S.SubsurfaceScatterAnisotropy == 0.1f, "T37 subsurface aniso");
    Check(S.FuzzWeight == 0.3f && S.FuzzColor[2] == 0.25f && S.FuzzRoughness == 0.6f, "T37 sheen triple");
    Check(S.CoatWeight == 0.8f && S.CoatColor[0] == 0.9f && S.CoatRoughness == 0.15f && S.CoatIor == 1.7f, "T37 coat quad");
    Check(S.CoatRoughnessAnisotropy == 0.4f, "T37 coat anisotropy");
    Check(S.ThinFilmWeight == 0.9f && NearRel(S.ThinFilmThickness, 0.5f) && S.ThinFilmIor == 1.45f, "T37 thin-film nm → µm");
    Check(S.EmissionLuminance == 2.0f && S.EmissionColor[2] == 0.25f, "T37 emission fold");
    Check(S.GeometryOpacity == 0.9f && S.Texture(MaterialTextureChannel::GeometryOpacity).IsBound(), "T37 opacity + texture");
    Check(S.Texture(MaterialTextureChannel::GeometryNormal).IsBound(), "T37 normal texture");
    Check((D.Flags & MaterialFlagThinWalled) && (D.Flags & MaterialFlagDoubleSided), "T37 features → flags");
    Check((D.Flags & MaterialFlagAlphaTranslucent), "T37 opacity < 1 → blend");
}
void T38_FbxRoughnessLadder()
{
    static ufbx_material M; std::memset(&M, 0, sizeof M);
    Check(MaterialCodec::DecodeFbx(&M, {}, nullptr).Slabs[0].SpecularRoughness == 0.5f, "T38a R2 fallback when sourceless");
    SetReal(M.pbr.glossiness, 0.7);
    Check(MaterialCodec::DecodeFbx(&M, {}, nullptr).Slabs[0].SpecularRoughness == 0.3f, "T38b 1 − glossiness");
    std::memset(&M, 0, sizeof M);
    SetReal(M.fbx.specular_exponent, 64.0);
    Check(MaterialCodec::DecodeFbx(&M, {}, nullptr).Slabs[0].SpecularRoughness == MaterialCodec::RoughnessFromShininess(64.0f), "T38c exponent ladder");
    SetReal(M.pbr.roughness, 0.42f);
    Check(MaterialCodec::DecodeFbx(&M, {}, nullptr).Slabs[0].SpecularRoughness == 0.42f, "T38d roughness wins");
}
void T39_FbxVec3Mean()
{
    static ufbx_material M; std::memset(&M, 0, sizeof M);
    SetVec3(M.pbr.specular_factor, 0.6, 0.9, 1.2);
    Check(MaterialCodec::DecodeFbx(&M, {}, nullptr).Slabs[0].SpecularWeight == 0.9f, "T39 vec3 → scalar xyz mean (documented lossy)");
}
void T40_FbxDispersionEdge()
{
    static ufbx_material M; std::memset(&M, 0, sizeof M);
    SetReal(M.pbr.transmission_dispersion, 0.0);
    MaterialDescriptor D = MaterialCodec::DecodeFbx(&M, {}, nullptr);
    Check(D.Slabs[0].TransmissionDispersionScale == 0.0f && D.Slabs[0].TransmissionDispersionAbbeNumber == 20.0f, "T40 non-positive dispersion resets");
}
void T41_FbxNull()
{
    MaterialDescriptor D = MaterialCodec::DecodeFbx(nullptr, {}, nullptr);
    Check(D.Name == "fallback" && D.Slabs[0].SpecularRoughness == 0.5f, "T41 null mirrors glTF");
}

} // namespace

int main()
{
    T01_EmptyMaterial(); T02_NullMaterial(); T03_CoreValues(); T04_AlphaMaskDoubleSided(); T05_AlphaBlend();
    T06_Ior(); T07_Specular(); T08_Anisotropy(); T09_Clearcoat(); T10_Sheen();
    T11_Transmission(); T12_Volume(); T13_VolumeThin(); T14_AuthoredDepthFallback(); T15_FlagOnlyThin();
    T16_Dispersion(); T17_Iridescence(); T18_DiffuseTransmission();
    T19_EmissiveNoStrength(); T20_EmissiveStrength(); T21_TextureTransform(); T22_NormalScaleOcclusionStrength();
    T23_MetalRoughFallback(); T24_ExtrasFlat(); T25_ExtrasGraph(); T26_ExtrasOverride(); T27_SpecGloss();
    T28_OpacityRidesBaseAlpha(); T29_OpaqueThinExtrasOnly(); T30_ShininessLocks();
    T31_ObjGlass(); T32_ObjTransparentPlastic(); T33_ObjMetalHeuristic(); T34_ObjEmission(); T35_ObjMaps(); T36_FastObjTrFold();
    T37_FbxFullHouse(); T38_FbxRoughnessLadder(); T39_FbxVec3Mean(); T40_FbxDispersionEdge(); T41_FbxNull();
    if (Failures) std::printf("MATERIAL CODEC: FAIL (%d checks, %d failures)\n", Checks, Failures);
    else std::printf("MATERIAL CODEC: PASS (%d checks)\n", Checks);
    return Failures ? 1 : 0;
}
