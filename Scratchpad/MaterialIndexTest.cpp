// R4a row-1 harness: Cornell identity, KHR round trip, slab fold at limit 1/2/4.
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include "ContentInterchange/MaterialDescriptor.h"
#include "ContentInterchange/MaterialIndex.h"
#include "ContentInterchange/MaterialCodec.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
using namespace Frontier;

static int Failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("  FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++Failures; } } while (0)

// The R3 decoder, verbatim (SceneCodec.cpp L34) — the reference the new path must reproduce.
struct OldRecord { float A[3], Rough, E[3], Metal; };
static OldRecord OldDecode(const cgltf_material* M)
{
    OldRecord R{ {0.8f,0.8f,0.8f}, 0.5f, {0,0,0}, 0.0f };
    if (!M) return R;
    if (M->has_pbr_metallic_roughness) { for (int i=0;i<3;++i) R.A[i]=M->pbr_metallic_roughness.base_color_factor[i]; R.Rough=M->pbr_metallic_roughness.roughness_factor; R.Metal=M->pbr_metallic_roughness.metallic_factor; }
    const float S = M->has_emissive_strength ? M->emissive_strength.emissive_strength : 1.0f;
    for (int i=0;i<3;++i) R.E[i] = M->emissive_factor[i]*S;
    return R;
}

static void PrintSlab(const MaterialSlabDescriptor& S, const char* Tag)
{
    std::printf("    %-6s base(%.3f %.3f %.3f) metal %.2f rough %.2f coat %.2f/ior %.2f fuzz %.2f film %.2f glint %.2f emis %.1f\n", Tag,
        S.BaseColor[0], S.BaseColor[1], S.BaseColor[2], S.BaseMetalness, S.SpecularRoughness, S.CoatWeight, S.CoatIor, S.FuzzWeight, S.ThinFilmWeight, S.SlateGlintDensity, S.EmissionLuminance);
}

static bool LoadGltf(const char* Path, cgltf_data** Out)
{
    cgltf_options O{}; return cgltf_parse_file(&O, Path, Out) == cgltf_result_success;
}

int main(int argc, char** argv)
{
    const char* Cornell = argc > 1 ? argv[1] : "Projects/Project-Zero/Content/Scenes/CornellBox.gltf";
    const char* Sponza  = argc > 2 ? argv[2] : nullptr;

    //── 1. Cornell identity ──────────────────────────────────────────────────────────────────────────────────────────
    std::printf("[1] Cornell identity: %s\n", Cornell);
    {
        cgltf_data* D = nullptr; CHECK(LoadGltf(Cornell, &D));
        MaterialIndex Index; MaterialDecodeConfiguration Cfg;
        for (cgltf_size i = 0; i < D->materials_count; ++i) Index.Register(MaterialCodec::DecodeGltf(&D->materials[i], Cfg, nullptr));
        Index.Register(MaterialCodec::DecodeGltf(nullptr, Cfg, nullptr));
        Index.Finalise(1);
        const auto& R = Index.QueryRecords();
        CHECK(R.size() == D->materials_count + 1u);
        int Identical = 0;
        for (size_t i = 0; i < R.size(); ++i)
        {
            OldRecord O = OldDecode(i < D->materials_count ? &D->materials[i] : nullptr);
            const bool Same = O.A[0]==R[i].AlbedoR && O.A[1]==R[i].AlbedoG && O.A[2]==R[i].AlbedoB && O.Rough==R[i].Roughness && O.Metal==R[i].Metalness
                           && O.E[0]==R[i].EmissiveR && O.E[1]==R[i].EmissiveG && O.E[2]==R[i].EmissiveB;
            Identical += Same;
            std::printf("  %zu %-10s albedo(%.3f %.3f %.3f) rough %.2f metal %.2f emissive(%.1f %.1f %.1f) slabs %u flags 0x%X %s\n", i, Index.QueryDescriptors()[i].Name.c_str(),
                R[i].AlbedoR, R[i].AlbedoG, R[i].AlbedoB, R[i].Roughness, R[i].Metalness, R[i].EmissiveR, R[i].EmissiveG, R[i].EmissiveB, R[i].SlabCount, R[i].Flags, Same ? "== R3" : "!= R3");
        }
        CHECK(Identical == (int)R.size());
        std::printf("  %d/%zu records bit-identical to the R3 RadianceStructure; %u slab records (%zu B each), %u B header\n",
            Identical, R.size(), Index.QueryMetrics().SlabCount, sizeof(MaterialSlabRecord), (unsigned)sizeof(MaterialRecord));

        // Encode back and compare with the material objects in the file text (Cornell writer emitted exactly these fields).
        std::vector<std::string> Ext;
        std::string J = MaterialCodec::EncodeGltf(Index.QueryDescriptors()[3], Ext, nullptr);
        std::printf("  encode material_3: %s\n  extensionsUsed: %s\n", J.c_str(), Ext.empty() ? "-" : Ext[0].c_str());
        CHECK(J.find("\"emissiveStrength\":32.0") != std::string::npos);
        cgltf_free(D);
    }

    //── 2. KHR coverage round trip ───────────────────────────────────────────────────────────────────────────────────
    std::printf("[2] KHR extension coverage round trip\n");
    {
        MaterialDescriptor Src; Src.Name = "everything"; Src.Slabs.emplace_back();
        MaterialSlabDescriptor& S = Src.Slabs[0];
        S.BaseColor[0]=0.2f; S.BaseColor[1]=0.3f; S.BaseColor[2]=0.4f; S.BaseMetalness=0.25f; S.SpecularRoughness=0.35f;
        S.SpecularIor=1.7f; S.SpecularWeight=0.8f; S.SpecularColor[0]=0.9f; S.SpecularRoughnessAnisotropy=0.5f;
        S.CoatWeight=0.6f; S.CoatRoughness=0.1f; S.CoatIor=1.5f;
        S.FuzzWeight=0.5f; S.FuzzColor[0]=1.0f; S.FuzzColor[1]=0.5f; S.FuzzColor[2]=0.25f; S.FuzzRoughness=0.7f;
        S.TransmissionWeight=0.9f; S.TransmissionDepth=0.02f; S.TransmissionColor[1]=0.5f; S.TransmissionDispersionScale=1.0f; S.TransmissionDispersionAbbeNumber=40.0f;
        S.ThinFilmWeight=1.0f; S.ThinFilmIor=1.3f; S.ThinFilmThickness=0.4f;
        S.EmissionLuminance=12.5f; S.EmissionColor[2]=0.5f;
        S.SlateHazinessWeight=0.3f; S.SlateGlintDensity=2.0f;
        Src.Flags = MaterialFlagDoubleSided | MaterialFlagAlphaMask; Src.AlphaCutoff = 0.33f;

        std::vector<std::string> Ext;
        std::string J = MaterialCodec::EncodeGltf(Src, Ext, nullptr);
        std::ostringstream File;
        File << "{\"asset\":{\"version\":\"2.0\"},\"extensionsUsed\":[";
        for (size_t i=0;i<Ext.size();++i) File << (i?",":"") << "\"" << Ext[i] << "\"";
        File << "],\"materials\":[" << J << "]}";
        std::string Text = File.str();
        std::printf("  %zu extensions: ", Ext.size()); for (auto& E : Ext) std::printf("%s ", E.c_str()+14); std::printf("\n");
        cgltf_options O{}; cgltf_data* D = nullptr;
        CHECK(cgltf_parse(&O, Text.data(), Text.size(), &D) == cgltf_result_success);
        MaterialDescriptor Back = MaterialCodec::DecodeGltf(&D->materials[0], MaterialDecodeConfiguration{}, nullptr);
        const MaterialSlabDescriptor& B = Back.Slabs[0];
        auto Near = [](float a, float b) { return std::fabs(a-b) < 1e-4f; };
        CHECK(Near(B.BaseColor[0],0.2f) && Near(B.BaseMetalness,0.25f) && Near(B.SpecularRoughness,0.35f));
        CHECK(Near(B.SpecularIor,1.7f) && Near(B.SpecularWeight,0.8f) && Near(B.SpecularColor[0],0.9f) && Near(B.SpecularRoughnessAnisotropy,0.5f));
        CHECK(Near(B.CoatWeight,0.6f) && Near(B.CoatRoughness,0.1f) && Near(B.CoatIor,1.5f));
        CHECK(Near(B.FuzzWeight,0.5f) && Near(B.FuzzColor[1],0.5f) && Near(B.FuzzRoughness,0.7f));
        CHECK(Near(B.TransmissionWeight,0.9f) && Near(B.TransmissionDepth,0.02f) && Near(B.TransmissionColor[1],0.5f) && Near(B.TransmissionDispersionAbbeNumber,40.0f));
        CHECK(Near(B.ThinFilmWeight,1.0f) && Near(B.ThinFilmIor,1.3f) && Near(B.ThinFilmThickness,0.4f));
        CHECK(Near(B.EmissionLuminance,12.5f) && Near(B.EmissionColor[2],0.5f));
        CHECK(Near(B.SlateHazinessWeight,0.3f) && Near(B.SlateGlintDensity,2.0f));
        CHECK(Back.Flags == Src.Flags && Near(Back.AlphaCutoff,0.33f));
        std::printf("  decoded: ior %.2f coat %.2f fuzz %.2f trans %.2f abbe %.0f film %.2f emis %.1f haze %.2f glint %.1f flags 0x%X cutoff %.2f\n",
            B.SpecularIor, B.CoatWeight, B.FuzzWeight, B.TransmissionWeight, B.TransmissionDispersionAbbeNumber, B.ThinFilmWeight, B.EmissionLuminance, B.SlateHazinessWeight, B.SlateGlintDensity, Back.Flags, Back.AlphaCutoff);
        cgltf_free(D);
    }

    //── 3. Car paint: 3 slabs, fold at limit 1 / 2 / 4, and the slate_slabs round trip ──────────────────────────────
    std::printf("[3] Car paint slab fold\n");
    {
        MaterialDescriptor Car; Car.Name = "car_paint";
        MaterialSlabDescriptor Coat;  Coat.BaseWeight = 0.0f; Coat.SpecularWeight = 0.0f; Coat.CoatWeight = 1.0f; Coat.CoatIor = 1.5f; Coat.CoatRoughness = 0.05f; Coat.ThinFilmWeight = 0.3f; Coat.ThinFilmThickness = 0.35f;
        MaterialSlabDescriptor Flake; Flake.BaseMetalness = 1.0f; Flake.BaseColor[0] = 0.9f; Flake.BaseColor[1] = 0.9f; Flake.BaseColor[2] = 0.95f; Flake.SpecularRoughness = 0.2f; Flake.SlateGlintDensity = 4.0f; Flake.BaseWeight = 0.3f;
        MaterialSlabDescriptor Body;  Body.BaseMetalness = 1.0f; Body.BaseColor[0] = 0.6f; Body.BaseColor[1] = 0.02f; Body.BaseColor[2] = 0.02f; Body.SpecularRoughness = 0.4f;
        Car.Slabs = { Coat, Flake, Body };                 // implicit vertical chain, top first
        Car.Operations = { { MaterialOperationCategory::VerticalLayer, 1u, 2u, 1.0f, {} },                         // flake over body → r0
                           { MaterialOperationCategory::VerticalLayer, 0u, kMaterialOperandResultBit | 0u, 1.0f, {} } }; // coat over r0
        for (uint32_t Limit : { 1u, 2u, 4u })
        {
            uint32_t Folded = 0; std::vector<std::string> Report;
            auto Slabs = MaterialIndex::Flatten(Car, Limit, &Folded, &Report);
            std::printf("  slab_limit %u → %zu slab(s), %u folded\n", Limit, Slabs.size(), Folded);
            for (size_t i = 0; i < Slabs.size(); ++i) PrintSlab(Slabs[i], i == 0 ? "top" : (i + 1 == Slabs.size() ? "bottom" : "mid"));
            for (auto& L : Report) std::printf("    report: %s\n", L.c_str());
            CHECK(Slabs.size() == std::min<size_t>(3u, Limit));
            if (Limit == 1u) { CHECK(Slabs[0].CoatWeight == 1.0f); CHECK(Slabs[0].SlateGlintDensity == 4.0f); CHECK(Slabs[0].BaseColor[0] < 0.6f); }
            if (Limit == 4u) { CHECK(Folded == 0u); CHECK(Slabs[0].CoatWeight == 1.0f && Slabs[2].BaseColor[0] == 0.6f); }
        }
        // Round trip through extras
        std::vector<std::string> Ext;
        std::string J = MaterialCodec::EncodeGltf(Car, Ext, nullptr);
        std::string Text = "{\"asset\":{\"version\":\"2.0\"},\"materials\":[" + J + "]}";
        cgltf_options O{}; cgltf_data* D = nullptr;
        CHECK(cgltf_parse(&O, Text.data(), Text.size(), &D) == cgltf_result_success);
        MaterialDescriptor Back = MaterialCodec::DecodeGltf(&D->materials[0], MaterialDecodeConfiguration{}, nullptr);
        CHECK(Back.Slabs.size() == 3u && Back.Operations.size() == 2u);
        CHECK(Back.Slabs == Car.Slabs);
        CHECK(Back.Operations == Car.Operations);
        std::printf("  extras round trip: %zu slabs, %zu operations, slabs %s, operations %s (%zu B of JSON)\n", Back.Slabs.size(), Back.Operations.size(),
            Back.Slabs == Car.Slabs ? "equal" : "DIFFER", Back.Operations == Car.Operations ? "equal" : "DIFFER", J.size());
        MaterialIndex Idx; Idx.Register(Car); Idx.Finalise(4);
        std::printf("  complexity class at limit 4: %u (2 = Complex), header albedo (%.3f %.3f %.3f) = body\n", Idx.QueryRecords()[0].Complexity, Idx.QueryRecords()[0].AlbedoR, Idx.QueryRecords()[0].AlbedoG, Idx.QueryRecords()[0].AlbedoB);
        CHECK(Idx.QueryRecords()[0].Complexity == MaterialComplexityComplex);
        cgltf_free(D);
    }

    //── 4. Sponza materials (textures resolved to fake slots by image index) ─────────────────────────────────────────
    if (Sponza)
    {
        std::printf("[4] Sponza materials: %s\n", Sponza);
        cgltf_data* D = nullptr; CHECK(LoadGltf(Sponza, &D));
        MaterialIndex Index;
        auto Resolve = [&](const cgltf_texture_view& V, bool) -> uint32_t { return V.texture && V.texture->image ? (uint32_t)(V.texture->image - D->images) : kMaterialTextureNone; };
        for (cgltf_size i = 0; i < D->materials_count; ++i) Index.Register(MaterialCodec::DecodeGltf(&D->materials[i], MaterialDecodeConfiguration{}, Resolve));
        Index.Finalise(1);
        for (size_t i = 0; i < Index.QueryRecords().size(); ++i)
        {
            const MaterialRecord& R = Index.QueryRecords()[i];
            const MaterialSlabRecord& S = Index.QuerySlabRecords()[R.SlabOffset];
            std::printf("  %2zu %-28s albedo(%.2f %.2f %.2f) rough %.2f metal %.2f baseTex %3d normalTex %3d mrTex %3d flags 0x%X\n", i, Index.QueryDescriptors()[i].Name.c_str(),
                R.AlbedoR, R.AlbedoG, R.AlbedoB, R.Roughness, R.Metalness, (int)R.BaseColourTexture, (int)R.NormalTexture, (int)(S.TextureSlots[1] & 0xFFFF) == 0xFFFF ? -1 : (int)(S.TextureSlots[1] & 0xFFFF), R.Flags);
        }
        std::printf("  %u materials, %zu images in file\n", Index.QueryCount(), (size_t)D->images_count);
        cgltf_free(D);
    }

    std::printf("%s (%d failure%s)\n", Failures ? "FAILED" : "ALL CHECKS PASSED", Failures, Failures == 1 ? "" : "s");
    return Failures ? 1 : 0;
}
