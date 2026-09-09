// R4b row 2 proof harness: the ResolveMaterial section of ReSTIRViewport.slang compiled as C++ (GlslShim.h) against synthetic
// geometry. Checks: interpolated normal + tangent frame orthonormal & handed, normal-map perturbation, UV-derived tangents,
// ray-cone LOD against the analytic texel-footprint level, slab → ShadingRecord field mapping, header fallback.
// Build (from repo root): see MaterialEvaluationTest.cpp for the .inc generation, then
//   g++ -std=c++20 -O2 -I Scratchpad -I Engine Scratchpad/MaterialResolveTest.cpp Engine/DisplayPresentation/ShadingTableCodec.cpp -o /tmp/mrt && /tmp/mrt
#include "GlslShim.h"
#include "DisplayPresentation/ShadingTableCodec.h"
#include "ContentInterchange/MaterialIndex.h"
#include <cstdio>
#define FRONTIER_CPU_PORT
static Frontier::ShadingTableSet gTables;
vec3 FetchEnergy(float mu, float alpha) { float o[3]; Frontier::ShadingTableCodec::SampleEnergy(gTables, mu, alpha, o); return vec3(o[0], o[1], o[2]); }
vec3 FetchSheen(float mu, float alpha)  { float o[3]; Frontier::ShadingTableCodec::SampleSheen(gTables, mu, alpha, o); return vec3(o[0], o[1], o[2]); }
static uint ViewportHeight = 1080u; static float FieldOfViewTanHalf = 0.57735f;   // 60° vertical
float PixelSpreadAngle() { return 2.0f * FieldOfViewTanHalf / float(ViewportHeight); }
#include "/tmp/ResolveMaterial.port.inc"

static int gFail = 0;
static void Check(const char* n, double got, double want, double tol) { bool ok = std::fabs(got - want) <= tol; gFail += !ok; std::printf("  %-64s %10.5f (want %.4f +-%.4f) %s\n", n, got, want, tol, ok ? "PASS" : "FAIL"); }

int main() {
    gTables = Frontier::ShadingTableCodec::Bake(512);
    // One instance, one 1 m × 1 m right triangle in the XY plane at z = 0, UVs (0,0) (1,0) (0,1); world = identity.
    GpuInstance inst{}; for (int i = 0; i < 4; ++i) { inst.World[i] = vec4(0); } inst.World[0].x = inst.World[1].y = inst.World[2].z = inst.World[3].w = 1;
    inst.VertexOffset = 0; inst.FirstIndex = 0; inst.TriangleCount = 1; inst.MaterialIndex = 0; inst.FlatTriangleOffset = 0;
    Vertices.resize(3); Indices = { 0, 1, 2 };
    vec3 P[3] = { vec3(0, 0, 0), vec3(1, 0, 0), vec3(0, 1, 0) }; vec2 UV[3] = { {0, 0}, {1, 0}, {0, 1} };
    for (int i = 0; i < 3; ++i) { Vertices[i].Position = vec4(P[i], 1); Vertices[i].Normal = vec4(0, 0, 1, 0); Vertices[i].Tangent = vec4(1, 0, 0, 1); Vertices[i].Texcoord = vec4_from(UV[i], vec2(0)); }
    GpuTriangle tri; tri.v0 = vec4(P[0], uintBitsToFloat(0)); tri.v1 = vec4(P[1], UV[2].x); tri.v2 = vec4(P[2], UV[2].y); tri.uv = vec4_from(UV[0], UV[1]); Triangles = { tri };
    GpuMaterial mat{}; mat.Albedo = vec4(0.2f, 0.4f, 0.6f, 0.35f); mat.Emissive = vec4(0, 0, 0, 0.7f); mat.Slabs = { 0, 0, 0, 0 }; mat.Textures = { kMaterialTextureNone, kMaterialTextureNone };

    std::printf("[1] header fallback (no slab): base colour / roughness / metalness from the 64 B record\n");
    { SurfaceFrame F; ShadingRecord m = ResolveMaterial(mat, inst, 0, vec3(0.25f, 0.25f, 0), vec3(0, 0, 1), 0.001f, F);
      Check("BaseColor.g", m.BaseColor.y, 0.4, 1e-6); Check("SpecularRoughness", m.SpecularRoughness, 0.35, 1e-6); Check("Metalness", m.Metalness, 0.7, 1e-6);
      Check("frame N.z", F.Normal.z, 1, 1e-6); Check("frame T.x", F.Tangent.x, 1, 1e-6); Check("frame B.y (right-handed)", F.Bitangent.y, 1, 1e-6); }

    std::printf("[2] viewer below the face: geometric + shading normal flip, frame stays orthonormal\n");
    { SurfaceFrame F; ResolveMaterial(mat, inst, 0, vec3(0.25f, 0.25f, 0), vec3(0, 0, -1), 0.001f, F);
      Check("Geometric.z", F.Geometric.z, -1, 1e-6); Check("Normal.z", F.Normal.z, -1, 1e-6);
      Check("|T x B . N| = 1", std::fabs(dot(cross(F.Tangent, F.Bitangent), F.Normal)), 1, 1e-5); }

    std::printf("[3] interpolated vertex normals (tilted 30 deg about X at vertex 1 only) -> barycentric blend, renormalised\n");
    { Vertices[1].Normal = vec4(0, -std::sin(0.5236f), std::cos(0.5236f), 0); SurfaceFrame F;
      ResolveMaterial(mat, inst, 0, vec3(0.5f, 0.0f, 0), vec3(0, 0, 1), 0.001f, F);   // midpoint of edge 0-1: 50/50 blend
      vec3 expect = normalize(vec3(0, -std::sin(0.5236f), 1 + std::cos(0.5236f)));
      Check("N.y", F.Normal.y, expect.y, 1e-4); Check("N.z", F.Normal.z, expect.z, 1e-4); Check("T . N = 0 (Gram-Schmidt)", dot(F.Tangent, F.Normal), 0, 1e-5);
      Vertices[1].Normal = vec4(0, 0, 1, 0); }

    std::printf("[4] no authored tangents -> derived from UV parametrisation (dP/du = +X here); tangent.w < 0 flips B\n");
    { for (auto& v : Vertices) v.Tangent = vec4(0); SurfaceFrame F; ResolveMaterial(mat, inst, 0, vec3(0.25f, 0.25f, 0), vec3(0, 0, 1), 0.001f, F);
      Check("derived T.x", F.Tangent.x, 1, 1e-5); Check("derived B.y", F.Bitangent.y, 1, 1e-5);
      for (auto& v : Vertices) v.Tangent = vec4(1, 0, 0, -1); ResolveMaterial(mat, inst, 0, vec3(0.25f, 0.25f, 0), vec3(0, 0, 1), 0.001f, F);
      Check("tangent.w = -1 -> B.y", F.Bitangent.y, -1, 1e-5); for (auto& v : Vertices) v.Tangent = vec4(1, 0, 0, 1); }

    std::printf("[5] slab path: OpenPBR fields, textures (metalness B / roughness G / opacity A), normal map, occlusion\n");
    MaterialSlabs.resize(1); { Frontier::MaterialSlabRecord R{}; R.BaseWeight = 1; R.BaseColorR = 0.8f; R.BaseColorG = 0.5f; R.BaseColorB = 0.2f; R.BaseMetalness = 1.0f; R.BaseDiffuseRoughness = 0.3f;
      R.SpecularWeight = 0.9f; R.SpecularColorR = R.SpecularColorG = R.SpecularColorB = 1; R.SpecularRoughness = 1.0f; R.SpecularRoughnessAnisotropy = 0.25f; R.SpecularIor = 1.7f;
      R.CoatWeight = 0.6f; R.CoatColorR = 0.9f; R.CoatColorG = 0.8f; R.CoatColorB = 0.7f; R.CoatRoughness = 0.1f; R.CoatIor = 1.55f; R.CoatDarkening = 0.5f;
      R.FuzzWeight = 0.4f; R.FuzzColorR = 1; R.FuzzColorG = 0.5f; R.FuzzColorB = 0.25f; R.FuzzRoughness = 0.8f;
      R.EmissionLuminance = 100; R.EmissionColorR = 1; R.EmissionColorG = 0.5f; R.EmissionColorB = 0;
      R.ThinFilmWeight = 1; R.ThinFilmThickness = 0.35f; R.ThinFilmIor = 1.3f; R.GeometryOpacity = 0.75f; R.SlateHazinessWeight = 0.2f; R.SlateHazinessRoughness = 0.9f;
      for (uint32_t& s : R.TextureSlots) s = 0xFFFFFFFFu;
      auto SetSlot = [&](int c, uint16_t slot) { R.TextureSlots[c / 2] = (R.TextureSlots[c / 2] & ~(0xFFFFu << ((c & 1) * 16))) | (uint32_t(slot) << ((c & 1) * 16)); };
      SetSlot(1, 0); SetSlot(2, 0); SetSlot(0, 1); SetSlot(7, 1); SetSlot(4, 2); SetSlot(14, 3);
      R.NormalScale = 1.0f; R.OcclusionStrength = 0.5f; R.MixWeight = 1;
      static_assert(sizeof(GpuMaterialSlab) == sizeof(Frontier::MaterialSlabRecord)); std::memcpy(&MaterialSlabs[0], &R, sizeof R); }
    Textures.resize(4); Textures[0].Constant = vec4(0.1f, 0.6f, 0.4f, 1); Textures[0].Width = Textures[0].Height = 1024;   // metalRough: G = 0.6 rough, B = 0.4 metal
    Textures[1].Constant = vec4(0.5f, 0.5f, 0.5f, 0.8f); Textures[1].Width = Textures[1].Height = 2048;                 // base colour, A = 0.8
    Textures[2].Constant = vec4(0.5f + 0.5f * 0.6f, 0.5f, 0.5f + 0.5f * 0.8f, 1); Textures[2].Width = Textures[2].Height = 512;   // normal map (0.6, 0, 0.8)
    Textures[3].Constant = vec4(0.4f, 0, 0, 1); Textures[3].Width = Textures[3].Height = 256;                            // occlusion R = 0.4
    mat.Slabs = { 0, 1, 0, 1 };
    { SurfaceFrame F; ShadingRecord m = ResolveMaterial(mat, inst, 0, vec3(0.25f, 0.25f, 0), vec3(0, 0, 1), 0.001f, F);
      Check("BaseColor.r = 0.8 * 0.5", m.BaseColor.x, 0.4, 1e-6); Check("Metalness = 1.0 * B(0.4)", m.Metalness, 0.4, 1e-6);
      Check("SpecularRoughness = 1.0 * G(0.6)", m.SpecularRoughness, 0.6, 1e-6); Check("SpecularAnisotropy", m.SpecularAnisotropy, 0.25, 1e-6);
      Check("SpecularWeight", m.SpecularWeight, 0.9, 1e-6); Check("SpecularIor", m.SpecularIor, 1.7, 1e-6); Check("DiffuseRoughness", m.DiffuseRoughness, 0.3, 1e-6);
      Check("CoatWeight", m.CoatWeight, 0.6, 1e-6); Check("CoatColor.b", m.CoatColor.z, 0.7, 1e-6); Check("CoatRoughness", m.CoatRoughness, 0.1, 1e-6); Check("CoatIor", m.CoatIor, 1.55, 1e-6); Check("CoatDarkening", m.CoatDarkening, 0.5, 1e-6);
      Check("FuzzWeight", m.FuzzWeight, 0.4, 1e-6); Check("FuzzColor.b", m.FuzzColor.z, 0.25, 1e-6); Check("FuzzRoughness", m.FuzzRoughness, 0.8, 1e-6);
      Check("Emission.g = 100 * 0.5", m.Emission.y, 50, 1e-4); Check("ThinFilmWeight", m.ThinFilmWeight, 1, 1e-6); Check("ThinFilmThickness", m.ThinFilmThickness, 0.35, 1e-6); Check("ThinFilmIor", m.ThinFilmIor, 1.3, 1e-6);
      Check("HazinessWeight", m.HazinessWeight, 0.2, 1e-6); Check("HazinessRoughness", m.HazinessRoughness, 0.9, 1e-6);
      Check("Opacity = 0.75 * A(0.8)", F.Opacity, 0.6, 1e-6); Check("Occlusion = lerp(1, 0.4, 0.5)", F.Occlusion, 0.7, 1e-6);
      Check("normal map N.x (0.6 along T)", F.Normal.x, 0.6, 1e-4); Check("normal map N.z", F.Normal.z, 0.8, 1e-4);
      Check("T . N after re-orthogonalisation", dot(F.Tangent, F.Normal), 0, 1e-5); Check("|B| = 1", length(F.Bitangent), 1, 1e-5); }

    std::printf("[6] ray-cone LOD: 1 m face, uvArea = worldArea -> lambda = log2(w / |n.d|) + 0.5 log2(texW*texH); must land within 0.5 of the analytic texel level\n");
    { float t = 4.0f; float w = t * PixelSpreadAngle();   // camera 4 m away at normal incidence: footprint = 4 * 2*0.577/1080 = 4.27 mm
      SurfaceFrame F; ResolveMaterial(mat, inst, 0, vec3(0.25f, 0.25f, 0), vec3(0, 0, 1), w, F);
      float analytic2048 = std::log2(w * 2048.0f);   // texels per footprint on the 2048² base colour map (1 m = 2048 texels)
      Check("LOD base colour 2048^2 at 4 m normal incidence", Textures[1].LastLod, analytic2048, 0.5); std::printf("      (footprint %.2f mm = %.1f texels -> level %.2f)\n", w * 1000, w * 2048, analytic2048);
      Check("LOD occlusion 256^2 = base - 3", Textures[3].LastLod, analytic2048 - 3, 0.5);
      ResolveMaterial(mat, inst, 0, vec3(0.25f, 0.25f, 0), normalize(vec3(0, 0.866f, 0.5f)), w, F);   // 60 deg grazing: footprint stretches 1/cos = 2x -> +1 level
      Check("LOD at 60 deg = normal + 1", Textures[1].LastLod, analytic2048 + 1, 0.5);
      ResolveMaterial(mat, inst, 0, vec3(0.25f, 0.25f, 0), vec3(0, 0, 1), 2 * w, F);   // bounce widened the cone 2x -> +1 level
      Check("LOD at double cone width = +1", Textures[1].LastLod, analytic2048 + 1, 0.5); }

    std::printf("[7] the resolved record drives the R4b-1 BSDF without NaN: albedo of the full slab at mu = 0.6\n");
    { SurfaceFrame F; ShadingRecord m = ResolveMaterial(mat, inst, 0, vec3(0.25f, 0.25f, 0), vec3(0, 0, 1), 0.001f, F); vec3 wo = normalize(vec3(0.8f, 0, 0.6f));
      ResolvedLayers L = ResolveLayers(m, wo); vec3 sum(0); int n = 128; for (int i = 0; i < n; ++i) for (int j = 0; j < n; ++j) { float ct = (i + .5f) / n, ph = 2 * kPi * (j + .5f) / n; float st = std::sqrt(1 - ct * ct); vec3 wi(st * std::cos(ph), st * std::sin(ph), ct); sum += EvaluateBsdf(m, L, wo, wi) * ct; }
      sum = sum * (2 * kPi / (n * n)); std::printf("      albedo = (%.4f %.4f %.4f)\n", sum.x, sum.y, sum.z); Check("finite & <= 1", (sum.x == sum.x && sum.x <= 1.0f) ? 1 : 0, 1, 0); }

    std::printf("\n%s (%d failures)\n", gFail ? "FAILED" : "ALL PASS", gFail); return gFail ? 1 : 0;
}
