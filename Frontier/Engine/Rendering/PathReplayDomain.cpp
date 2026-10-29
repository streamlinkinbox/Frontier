/*==========================================================================================================================================
                                                    PATHREPLAYDOMAIN.CPP
==========================================================================================================================================*/
// 🧩 Guard replay-compatible BVH, material and receiver inputs before shader submission.

#include "PathReplayDomain.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <stdexcept>

namespace Frontier
{
namespace
{
void Require(bool Condition, const char* Message)
{
    if (!Condition) throw std::runtime_error(Message);
}
// 📝 Validate every referenced mip before the shader can dereference the untrusted float atlas.
void ValidateReplayDiffuseAtlas(const PathReplaySceneView& Scene, uint32_t MaterialOffset, bool RoughOpaque)
{
    const auto& Atlas = Scene.MaterialAtlas;
    Require(Atlas.size() <= (1u << 24) && MaterialOffset <= Atlas.size() && Atlas.size() - MaterialOffset >= 268,
            "Replay diffuse material record is out of bounds");
    for (size_t I = MaterialOffset; I < size_t(MaterialOffset) + 268; ++I)
        Require(std::isfinite(Atlas[I]), "Replay diffuse material record is not finite");
    const float* Material = Atlas.data() + MaterialOffset;
    Require(Material[0] == 1 && Material[1] == 0 && Material[3] == 0 && Material[200] == 0,
            "Replay diffuse atlas forbids layered, non-opaque, normal-mapped and procedural-paint materials");
    for (uint32_t I = 4; I < 8; ++I)
        Require(std::abs(Material[I]) <= 10000, "Replay diffuse UV mapping exceeds the bounded domain");
    for (uint32_t I = 8; I < 11; ++I)
        Require(Material[I] >= 0 && Material[I] <= 1, "Replay diffuse base colour is outside [0,1]");
    const bool ValidScattering = RoughOpaque
        ? Material[11] >= .25f && Material[11] <= 1 && Material[12] >= 0 && Material[12] <= 1
        : Material[11] == 1 && Material[12] == 0;
    Require(ValidScattering && Material[13] >= 1 && Material[13] <= 3 && Material[14] == 1 &&
                Material[15] == 0 && Material[27] == 0,
            "Replay atlas requires guarded scattering, full coverage, reserved-zero optics and no sheen");
    for (uint32_t I = 16; I < 24; ++I)
        Require(Material[I] == 0, "Replay diffuse atlas forbids absorption and coating classification");
    const float TextureOffset = Material[2];
    Require(TextureOffset >= 1 && TextureOffset < float(Atlas.size()) && std::floor(TextureOffset) == TextureOffset,
            "Replay diffuse albedo texture address is invalid");
    const size_t Map = size_t(TextureOffset);
    Require(Atlas.size() - Map >= 4, "Replay diffuse texture header is out of bounds");
    const float Dimension = Atlas[Map];
    Require(std::isfinite(Dimension) && Dimension >= 1 && Dimension <= 1024 && std::floor(Dimension) == Dimension,
            "Replay diffuse texture dimension is invalid");
    for (size_t I = Map + 1; I < Map + 4; ++I)
        Require(std::isfinite(Atlas[I]), "Replay diffuse texture header is not finite");
    uint32_t Size = uint32_t(Dimension);
    Require((Size & (Size - 1)) == 0, "Replay diffuse texture must have power-of-two mip dimensions");
    size_t Cursor = Map + 4;
    for (;;)
    {
        const size_t Values = size_t(Size) * Size * 3;
        Require(Cursor <= Atlas.size() && Values <= Atlas.size() - Cursor, "Replay diffuse mip is out of bounds");
        for (size_t I = Cursor; I < Cursor + Values; ++I)
            Require(std::isfinite(Atlas[I]) && Atlas[I] >= 0 && Atlas[I] <= 1,
                    "Replay diffuse texel is outside finite [0,1]");
        Cursor += Values;
        if (Size == 1) break;
        Size /= 2;
    }
}

void ValidateReplayPyramid(const std::span<const float>& Atlas, float TextureOffset, bool NormalMap)
{
    Require(TextureOffset >= 1 && TextureOffset < float(Atlas.size()) && std::floor(TextureOffset) == TextureOffset,
            "Replay authored texture address is invalid");
    const size_t Map = size_t(TextureOffset);
    Require(Atlas.size() - Map >= 4, "Replay authored texture header is out of bounds");
    const float Dimension = Atlas[Map];
    Require(std::isfinite(Dimension) && Dimension >= 1 && Dimension <= 1024 && std::floor(Dimension) == Dimension,
            "Replay authored texture dimension is invalid");
    for (size_t I = Map + 1; I < Map + 4; ++I)
        Require(std::isfinite(Atlas[I]), "Replay authored texture header is not finite");
    uint32_t Size = uint32_t(Dimension);
    Require((Size & (Size - 1)) == 0, "Replay authored texture must have power-of-two mip dimensions");
    size_t Cursor = Map + 4;
    for (;;)
    {
        const size_t Texels = size_t(Size) * Size;
        const size_t Values = Texels * 3;
        Require(Cursor <= Atlas.size() && Values <= Atlas.size() - Cursor, "Replay authored mip is out of bounds");
        if (NormalMap)
        {
            for (size_t T = 0; T < Texels; ++T)
            {
                const float Nx = Atlas[Cursor + T * 3 + 0];
                const float Ny = Atlas[Cursor + T * 3 + 1];
                const float Nz = Atlas[Cursor + T * 3 + 2];
                Require(std::isfinite(Nx) && std::isfinite(Ny) && std::isfinite(Nz) &&
                            std::abs(Nx) <= 1.01f && std::abs(Ny) <= 1.01f && std::abs(Nz) <= 1.01f &&
                            Nx * Nx + Ny * Ny + Nz * Nz > 1e-6f,
                        "Replay authored normal texel is invalid or degenerate");
            }
        }
        else
        {
            for (size_t I = Cursor; I < Cursor + Values; ++I)
                Require(std::isfinite(Atlas[I]) && Atlas[I] >= 0 && Atlas[I] <= 1,
                        "Replay authored albedo texel is outside finite [0,1]");
        }
        Cursor += Values;
        if (Size == 1) break;
        Size /= 2;
    }
}

void ValidateReplayAuthoredAtlas(const PathReplaySceneView& Scene, uint32_t MaterialOffset)
{
    const auto& Atlas = Scene.MaterialAtlas;
    Require(Atlas.size() <= (1u << 24) && MaterialOffset <= Atlas.size() && Atlas.size() - MaterialOffset >= 268,
            "Replay authored material record is out of bounds");
    for (size_t I = MaterialOffset; I < size_t(MaterialOffset) + 268; ++I)
        Require(std::isfinite(Atlas[I]), "Replay authored material record is not finite");
    const float* Material = Atlas.data() + MaterialOffset;
    const float StrataFloat = Material[0], CategoryFloat = Material[1];
    Require(StrataFloat >= 1 && StrataFloat <= 8 && std::floor(StrataFloat) == StrataFloat,
            "Replay authored material requires one to eight scattering strata");
    Require((CategoryFloat == 0 || CategoryFloat == 2 || CategoryFloat == 3) &&
                std::floor(CategoryFloat) == CategoryFloat,
            "Replay authored material category must be opaque (0/3) or thin-sheet glazing (2)");
    const uint32_t StrataCount = uint32_t(StrataFloat);
    const uint32_t Category = uint32_t(CategoryFloat);
    for (uint32_t I = 4; I < 8; ++I)
        Require(std::abs(Material[I]) <= 10000, "Replay authored UV mapping exceeds the bounded domain");
    if (Material[2] != 0) ValidateReplayPyramid(Atlas, Material[2], false);
    if (Material[3] != 0)
    {
        Require(Category != 2, "Replay thin-sheet glazing forbids tangent-space normal pyramids");
        ValidateReplayPyramid(Atlas, Material[3], true);
    }
    for (uint32_t Stratum = 0; Stratum < StrataCount; ++Stratum)
    {
        const float* S = Material + 8 + Stratum * 24;
        for (uint32_t C = 0; C < 3; ++C)
            Require(S[C] >= 0 && S[C] <= 1, "Replay authored stratum albedo is outside [0,1]");
        Require(S[3] >= .04f && S[3] <= 1.0f, "Replay authored stratum roughness lies outside [0.04,1]");
        Require(S[4] >= 0 && S[4] <= 1 && S[5] >= 1.0f && S[5] <= 3.0f && S[6] >= 0 && S[6] <= 1 && S[7] == 0,
                "Replay authored stratum optics are outside [0,1] metallic/coverage or [1,3] IOR");
        for (uint32_t C = 0; C < 3; ++C)
            Require(S[8 + C] >= 0 && S[8 + C] <= 1000.0f, "Replay authored stratum absorption is invalid");
        Require(S[11] >= 0 && S[11] <= 100.0f, "Replay authored stratum thickness is invalid");
        Require((S[12] == 0 || S[12] == 1) && S[13] == 0 && S[14] == 0 && S[15] == 0,
                "Replay authored stratum classification is invalid");
        if (S[12] == 1)
            Require(S[4] == 0, "Replay coating stratum must be dielectric (metallic == 0)");
        for (uint32_t C = 0; C < 3; ++C)
            Require(S[16 + C] >= 0 && S[16 + C] <= 1, "Replay authored sheen colour is outside [0,1]");
        Require(S[19] >= 0 && S[19] <= 1 && S[20] >= 0 && S[20] <= 1,
                "Replay authored sheen intensity or roughness is outside [0,1]");
        if (S[19] > 0)
            Require(S[12] == 0 && S[20] >= .04f, "Replay Charlie sheen requires an opaque base stratum and roughness >= 0.04");
    }
    Require(Material[200] == 0 || Material[200] == 1, "Replay automotive paint enable flag must be 0 or 1");
    if (Material[200] == 1)
    {
        Require(Category != 2, "Replay thin-sheet glazing forbids procedural automotive paint flakes");
        const float Layers = Material[201], Mode = Material[202], PaletteCount = Material[203];
        Require(Layers >= 1 && Layers <= 8 && std::floor(Layers) == Layers &&
                    (Mode == 0 || Mode == 1 || Mode == 2) &&
                    PaletteCount >= 1 && PaletteCount <= 12 && std::floor(PaletteCount) == PaletteCount,
                "Replay automotive paint configuration is invalid");
        Require(Material[204] >= 0 && Material[204] <= 1 && Material[205] > 0 && Material[205] <= 100000.0f &&
                    Material[206] >= 0 && Material[206] <= 2 && Material[207] >= 0 && Material[207] <= 2,
                "Replay automotive paint flake distribution is invalid");
        Require(Material[208] >= .04f && Material[208] <= 1 && Material[209] >= 0 && Material[209] <= 1 &&
                    Material[210] >= 0 && Material[210] <= 1 && Material[211] >= 0 && Material[211] <= 2,
                "Replay automotive paint finish is invalid");
        Require(Material[212] >= 0 && Material[212] <= 2, "Replay automotive paint relief strength is invalid");
        for (uint32_t Entry = 0; Entry < uint32_t(PaletteCount); ++Entry)
        {
            const float* Stop = Material + 216 + Entry * 4;
            for (uint32_t C = 0; C < 3; ++C)
                Require(Stop[C] >= 0 && Stop[C] <= 1, "Replay automotive paint palette colour is outside [0,1]");
            Require(Stop[3] >= 0 && Stop[3] <= 1, "Replay automotive paint palette position is outside [0,1]");
        }
        for (uint32_t C = 0; C < 3; ++C)
            Require(Material[264 + C] >= 0 && Material[264 + C] <= 1,
                    "Replay automotive paint average tint is outside [0,1]");
    }
}
} // namespace

void ValidatePathReplayDomain(const PathReplaySceneView& Scene, const PathReplayDomainSpecification& Specification)
{
    const bool PrimaryVisibility = Specification.PrimaryVisibility;
    const bool RoughOpaque = Specification.RoughOpaque;
    const bool TexturedDiffuse = Specification.TexturedDiffuse;
    const bool AuthoredMaterials = Specification.AuthoredMaterials;
    const bool UsesAtlas = TexturedDiffuse || AuthoredMaterials;
    const bool DiffuseCamera = Specification.DiffuseCameraReprojection;
    const bool VarianceCamera = Specification.VarianceFilter && PrimaryVisibility;
    const bool HardwareRayQuery = Specification.HardwareRayQuery;
    const float MinimumRoughness = AuthoredMaterials ? .04f : .25f;
    Require(!Specification.ImportanceRough || RoughOpaque, "GGX replay sampling requires the rough-opaque domain");
    Require(!AuthoredMaterials || (RoughOpaque && Specification.ImportanceRough && !DiffuseCamera),
            "Authored PBR replay requires rough-opaque multi-lobe importance sampling");
    Require(!DiffuseCamera || (PrimaryVisibility && !RoughOpaque && !TexturedDiffuse),
            "Camera reprojection requires untextured diffuse primary rays");
    Require(Scene.Camera.size() == 12, "Replay camera requires twelve float4 entries");
    if (UsesAtlas)
        Require(Scene.MaterialAtlas.size() <= (1u << 24) && Scene.Camera[10][2] == 0 &&
                    std::isfinite(Scene.Camera[3][3]) && Scene.Camera[3][3] >= 0 && Scene.Camera[3][3] <= 10000,
                "Invalid replay diffuse atlas or camera footprint");
    if (UsesAtlas || DiffuseCamera || VarianceCamera)
    {
        for (const auto& Projection : Scene.Camera)
            for (float Coefficient : Projection)
                Require(std::isfinite(Coefficient) && std::abs(Coefficient) <= 1000000,
                        "Invalid replay diffuse camera projection");
        if (PrimaryVisibility)
        {
            Require(Scene.Camera[0][3] >= 0 && Scene.Camera[1][3] > Scene.Camera[0][3] &&
                        Scene.Camera[2][3] > 0 && Scene.Camera[3][3] > 0 && Scene.Camera[4][3] >= 0 &&
                        Scene.Camera[6][3] > 0 && Scene.Camera[7][3] > 0,
                    "Invalid replay diffuse primary camera frustum");
            if (DiffuseCamera || VarianceCamera || AuthoredMaterials)
            {
                Require(Scene.Camera[0][3] >= 1e-4f && Scene.Camera[4][3] >= 1e-4f &&
                            Scene.Camera[5][3] > Scene.Camera[4][3], "Invalid camera-reprojection clipping interval");
                for (uint32_t Row : {2u, 3u, 6u, 7u})
                    Require(Scene.Camera[Row][3] >= 1e-4f && Scene.Camera[Row][3] <= 10000,
                            "Invalid camera-reprojection field of view");
            }
            for (uint32_t CameraOffset : {0u, 4u})
                for (uint32_t A = 1; A <= 3; ++A)
                    for (uint32_t B = A; B <= 3; ++B)
                    {
                        double Dot = 0;
                        for (uint32_t C = 0; C < 3; ++C) Dot += double(Scene.Camera[CameraOffset+A][C]) * Scene.Camera[CameraOffset+B][C];
                        Require(std::abs(Dot - (A == B ? 1.0 : 0.0)) <= .005,
                                "Replay diffuse camera axes must be orthonormal");
                    }
        }
    }
    std::map<uint32_t, bool> ValidatedMaterials;
    const bool ValidCorners = HardwareRayQuery
        ? (Scene.Corners.empty() ? !Scene.Surfaces.empty() || Scene.Enclosures.empty()
                                 : Scene.Surfaces.size() == Scene.Corners.size())
        : (Scene.Surfaces.size() == Scene.Corners.size());
    const bool ValidEnclosures = HardwareRayQuery
        ? (!Scene.Surfaces.empty() || Scene.Enclosures.empty())
        : (Scene.Surfaces.empty() == Scene.Enclosures.empty());
    Require(Scene.AnalyticLightCount == 0 && (!RoughOpaque || UsesAtlas || Scene.MaterialAtlas.empty()) &&
                ValidCorners && ValidEnclosures,
            "Replay requires matching triangle/BVH extents and no analytic lights");
    for (size_t I = 0; I < Scene.Surfaces.size(); ++I)
    {
        const auto& F = Scene.Surfaces[I];
        Require(std::isfinite(F.Emissive[3]) && F.Emissive[3] >= 0 &&
                    std::isfinite(F.OpticalToken[3]) && F.OpticalToken[3] > 0,
                "Replay requires an identified opaque facet with a valid material address");
        const bool HasMaterial = F.Emissive[3] != 0;
        Require(!HasMaterial || UsesAtlas, "Replay textured diffuse materials require explicit opt-in");
        if (HasMaterial)
        {
            Require(F.Emissive[3] <= float(Scene.MaterialAtlas.size()) && std::floor(F.Emissive[3]) == F.Emissive[3],
                    "Replay diffuse material address is invalid");
            const uint32_t MaterialOffset = uint32_t(F.Emissive[3]) - 1;
            if (ValidatedMaterials.emplace(MaterialOffset, true).second)
            {
                if (AuthoredMaterials) ValidateReplayAuthoredAtlas(Scene, MaterialOffset);
                else ValidateReplayDiffuseAtlas(Scene, MaterialOffset, RoughOpaque);
            }
            for (const auto* Coordinate : {&F.A, &F.B, &F.C, &F.NormalA, &F.NormalB, &F.NormalC})
                Require(std::isfinite((*Coordinate)[3]) && std::abs((*Coordinate)[3]) <= 10000,
                        "Replay diffuse UV coordinate is invalid");
            if (!Scene.Corners.empty())
            {
                const auto& T = Scene.Corners[I];
                for (uint32_t C = 0; C < 3; ++C)
                    Require(F.A[C] == T.A[C] && F.B[C] == T.B[C] && F.C[C] == T.C[C],
                            "Replay material facet does not match the BVH triangle");
            }
        }
        else if (!Scene.Corners.empty())
        {
            const auto& T = Scene.Corners[I];
            Require(F.A == T.A && F.B == T.B && F.C == T.C, "Replay facet does not match the BVH triangle");
        }
        if (RoughOpaque)
            Require(std::isfinite(F.OpticalToken[0]) && F.OpticalToken[0] >= 0 && F.OpticalToken[0] <= 1 &&
                        std::isfinite(F.OpticalToken[1]) && F.OpticalToken[1] >= 1 && F.OpticalToken[1] <= 3 &&
                        std::isfinite(F.OpticalToken[2]) && F.OpticalToken[2] >= 0 && F.OpticalToken[2] <= 1 &&
                        std::isfinite(F.AlbedoRoughness[3]) && F.AlbedoRoughness[3] >= MinimumRoughness &&
                        F.AlbedoRoughness[3] <= 1, "Rough replay facet lies outside the guarded GGX domain");
        else Require(F.OpticalToken[0] == 0, "Diffuse replay rejects metallic facets");
        if (DiffuseCamera)
            Require(std::isfinite(F.OpticalToken[1]) && F.OpticalToken[1] >= 1 && F.OpticalToken[1] <= 3 &&
                        std::isfinite(F.OpticalToken[2]) && F.OpticalToken[2] >= 0 && F.OpticalToken[2] <= 1 &&
                        std::isfinite(F.AlbedoRoughness[3]) && F.AlbedoRoughness[3] >= 0 && F.AlbedoRoughness[3] <= 1,
                    "Camera-reprojection facet has invalid optical guides");
        for (uint32_t C = 0; C < 3; ++C)
            Require(std::isfinite(F.AlbedoRoughness[C]) && F.AlbedoRoughness[C] >= 0 && F.AlbedoRoughness[C] <= 1 &&
                        std::isfinite(F.Emissive[C]) && F.Emissive[C] >= 0 && F.Emissive[C] <= 256 &&
                        std::isfinite(F.A[C]) && std::isfinite(F.B[C]) && std::isfinite(F.C[C]) &&
                        std::isfinite(F.NormalA[C]) && std::abs(F.NormalA[C]) <= 1e6f &&
                        std::isfinite(F.NormalB[C]) && std::abs(F.NormalB[C]) <= 1e6f &&
                        std::isfinite(F.NormalC[C]) && std::abs(F.NormalC[C]) <= 1e6f,
                    "Invalid replay facet coefficients");
        if (UsesAtlas || DiffuseCamera)
            for (const auto* P : {&F.A, &F.B, &F.C})
                for (uint32_t C = 0; C < 3; ++C)
                    Require(std::abs((*P)[C]) <= 1000000, "Replay diffuse geometry exceeds the bounded domain");
        for (const auto* N : {&F.NormalA, &F.NormalB, &F.NormalC})
            Require((*N)[0]*(*N)[0] + (*N)[1]*(*N)[1] + (*N)[2]*(*N)[2] > 1e-12f,
                    "Replay facet normal is degenerate");
    }
    if (!Scene.Enclosures.empty())
    {
        std::vector<uint8_t> Seen(Scene.Enclosures.size()), Covered(Scene.Surfaces.size());
        std::vector<uint32_t> Pending{0};
        while (!Pending.empty())
        {
            const uint32_t Index = Pending.back(); Pending.pop_back();
            Require(Index < Seen.size() && Seen[Index] == 0, "Replay BVH has an invalid or repeated enclosure");
            Seen[Index] = 1;
            const auto& Box = Scene.Enclosures[Index];
            for (uint32_t C = 0; C < 3; ++C)
                Require(std::isfinite(Box.Minimum[C]) && std::isfinite(Box.Maximum[C]) &&
                            Box.Minimum[C] <= Box.Maximum[C], "Invalid replay BVH bounds");
            Require(Box.Links[2] <= 1, "Invalid replay BVH enclosure category");
            if (Box.Links[2] != 0)
            {
                const uint64_t End = uint64_t(Box.Links[0]) + Box.Links[1];
                Require(Box.Links[1] > 0 && End <= Scene.Surfaces.size(), "Replay BVH leaf exceeds facets");
                for (uint32_t F = Box.Links[0]; F < End; ++F)
                {
                    Require(Covered[F]++ == 0, "Replay BVH repeats a facet");
                    const auto& Facet = Scene.Surfaces[F];
                    for (const auto* P : {&Facet.A, &Facet.B, &Facet.C})
                        for (uint32_t C = 0; C < 3; ++C)
                            Require((*P)[C] >= Box.Minimum[C] && (*P)[C] <= Box.Maximum[C],
                                    "Replay BVH leaf does not enclose its facets");
                }
            }
            else
            {
                for (uint32_t Nested : {Box.Links[0], Box.Links[1]})
                {
                    Require(Nested < Scene.Enclosures.size(), "Replay BVH nested enclosure exceeds extent");
                    const auto& Inner = Scene.Enclosures[Nested];
                    for (uint32_t C = 0; C < 3; ++C)
                        Require(Inner.Minimum[C] >= Box.Minimum[C] && Inner.Maximum[C] <= Box.Maximum[C],
                                "Replay BVH enclosure does not contain its nested bounds");
                    Pending.push_back(Nested);
                }
            }
        }
        for (uint8_t Mark : Seen) Require(Mark == 1, "Replay BVH has an unreachable enclosure");
        for (uint8_t Mark : Covered) Require(Mark == 1, "Replay BVH omits a facet");
    }
    if (!PrimaryVisibility)
        for (const auto& R : Scene.Receivers)
        {
            Require(R.PositionHit[3] == 0 || R.PositionHit[3] == 1, "Invalid replay receiver hit indicator");
            if (R.PositionHit[3] == 0) continue;
            Require(std::isfinite(R.MotionPositionToken[3]) && R.MotionPositionToken[3] > 0,
                    "Replay receiver must have a positive surface token");
            if (RoughOpaque)
                Require(std::isfinite(R.ViewMetallic[3]) && R.ViewMetallic[3] >= 0 && R.ViewMetallic[3] <= 1 &&
                            std::isfinite(R.AlbedoIor[3]) && R.AlbedoIor[3] >= 1 && R.AlbedoIor[3] <= 3 &&
                            std::isfinite(R.ResidualSpecular[3]) && R.ResidualSpecular[3] >= 0 && R.ResidualSpecular[3] <= 1 &&
                            std::isfinite(R.NormalRoughness[3]) && R.NormalRoughness[3] >= MinimumRoughness &&
                            R.NormalRoughness[3] <= 1, "Rough replay receiver lies outside the guarded GGX domain");
            else Require(R.ViewMetallic[3] == 0, "Diffuse replay rejects metallic receivers");
            if (AuthoredMaterials && std::isfinite(R.ExpectedDirect[3]) && R.ExpectedDirect[3] != 0)
            {
                Require(R.ExpectedDirect[3] >= 1 && R.ExpectedDirect[3] <= float(Scene.MaterialAtlas.size()) &&
                            std::floor(R.ExpectedDirect[3]) == R.ExpectedDirect[3],
                        "Replay receiver authored material address is invalid");
                const uint32_t MaterialOffset = uint32_t(R.ExpectedDirect[3]) - 1;
                if (ValidatedMaterials.emplace(MaterialOffset, true).second)
                    ValidateReplayAuthoredAtlas(Scene, MaterialOffset);
            }
            double NormalSquared = 0, ViewSquared = 0, NormalView = 0;
            for (uint32_t C = 0; C < 3; ++C)
            {
                Require(std::isfinite(R.PositionHit[C]) && (!UsesAtlas || std::abs(R.PositionHit[C]) <= 1000000) &&
                            std::isfinite(R.NormalRoughness[C]) && std::abs(R.NormalRoughness[C]) <= 1e6f &&
                            std::isfinite(R.AlbedoIor[C]) && R.AlbedoIor[C] >= 0 && R.AlbedoIor[C] <= 1 &&
                            std::isfinite(R.MotionPositionToken[C]) && std::isfinite(R.MotionNormal[C]) &&
                            std::abs(R.MotionNormal[C]) <= 1e6f &&
                            std::isfinite(R.ResidualSpecular[C]) && R.ResidualSpecular[C] >= 0,
                        "Invalid replay receiver");
                NormalSquared += double(R.NormalRoughness[C]) * R.NormalRoughness[C];
                if (RoughOpaque)
                {
                    Require(std::isfinite(R.ViewMetallic[C]), "Invalid rough replay view");
                    ViewSquared += double(R.ViewMetallic[C]) * R.ViewMetallic[C];
                    NormalView += double(R.NormalRoughness[C]) * R.ViewMetallic[C];
                }
            }
            Require(NormalSquared > 1e-12 && (!RoughOpaque || (ViewSquared > 1e-12 && NormalView > 1e-8)) &&
                        std::isfinite(R.MotionPixel[0]) && std::isfinite(R.MotionPixel[1]) &&
                        std::isfinite(R.MotionPixel[2]) && R.MotionPixel[2] >= 0 &&
                        std::isfinite(R.MotionPixel[3]), "Invalid replay receiver normal, view or motion");
        }
}

std::vector<float> CompilePathReplayFacetSources(std::span<const PathReplayFacet> Surfaces)
{
    Require(Surfaces.size() <= (1u << 24), "Replay facet indices exceed exact float storage");
    std::vector<float> Indices;
    for (size_t Index = 0; Index < Surfaces.size(); ++Index)
    {
        const auto& F = Surfaces[Index];
        if (!(F.Emissive[0] > 0 || F.Emissive[1] > 0 || F.Emissive[2] > 0)) continue;
        const double Ax = double(F.B[0]) - F.A[0], Ay = double(F.B[1]) - F.A[1], Az = double(F.B[2]) - F.A[2];
        const double Bx = double(F.C[0]) - F.A[0], By = double(F.C[1]) - F.A[1], Bz = double(F.C[2]) - F.A[2];
        const double Px = Ay*Bz, Qx = Az*By, Py = Az*Bx, Qy = Ax*Bz, Pz = Ax*By, Qz = Ay*Bx;
        const double CrossSquared = (Px-Qx)*(Px-Qx) + (Py-Qy)*(Py-Qy) + (Pz-Qz)*(Pz-Qz);
        Require(std::isfinite(CrossSquared) && CrossSquared >= 1e-16 && CrossSquared <= 1e32 &&
                    std::max({std::abs(Ax),std::abs(Ay),std::abs(Az),std::abs(Bx),std::abs(By),std::abs(Bz)}) <= 1e16 &&
                    std::max({std::abs(Px),std::abs(Qx),std::abs(Py),std::abs(Qy),std::abs(Pz),std::abs(Qz)}) <= 1e16,
                "Replay source facet is degenerate or outside bounded area arithmetic");
        Indices.push_back(float(Index));
    }
    return Indices;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  REPLAY HISTORY INTAKE
//------------------------------------------------------------------------------------------------------------------------

PathReplayHistoryDecision PathReplayHistoryDomain::Resolve(const PathReplaySceneView& Scene,
                                                           const PathReplayHistorySpecification& Specification,
                                                           bool FullHistoryForAudit)
{
    Require(Scene.Camera.size() == PriorCamera.size(), "Replay camera requires twelve float4 entries");
    Require(Specification.InitialCandidates >= 1 &&
                Specification.InitialCandidates <= PathReplayMaximumInitialCandidates,
            "Replay history requires one to four initial candidates");
    const bool SpecificationChanged = !PriorAvailable || PriorSpecification != Specification;
    const bool CameraChanged = !PriorAvailable ||
                               std::memcmp(Scene.Camera.data(), PriorCamera.data(),
                                           PriorCamera.size() * sizeof(PriorCamera[0])) != 0;
    bool SceneChanged = !PriorAvailable || PriorSurfaces.size() != Scene.Surfaces.size() ||
                        PriorReceivers.size() != Scene.Receivers.size() ||
                        PriorEnclosures.size() != Scene.Enclosures.size() || PriorCorners.size() != Scene.Corners.size() ||
                        PriorMaterialAtlas.size() != Scene.MaterialAtlas.size();
    if (!SceneChanged)
    {
        SceneChanged = (!Scene.Surfaces.empty() && std::memcmp(PriorSurfaces.data(), Scene.Surfaces.data(),
                                                              Scene.Surfaces.size_bytes()) != 0) ||
                       (!Scene.Receivers.empty() && std::memcmp(PriorReceivers.data(), Scene.Receivers.data(),
                                                               Scene.Receivers.size_bytes()) != 0) ||
                       (!Scene.Enclosures.empty() && std::memcmp(PriorEnclosures.data(), Scene.Enclosures.data(),
                                                                Scene.Enclosures.size_bytes()) != 0) ||
                       (!Scene.Corners.empty() && std::memcmp(PriorCorners.data(), Scene.Corners.data(),
                                                             Scene.Corners.size_bytes()) != 0) ||
                       (!Scene.MaterialAtlas.empty() && std::memcmp(PriorMaterialAtlas.data(), Scene.MaterialAtlas.data(),
                                                                   Scene.MaterialAtlas.size_bytes()) != 0);
    }
    const bool Changed = SpecificationChanged || CameraChanged || SceneChanged;
    // 📝 Previous projection must name the actual preceding camera, and non-camera metadata must not change.
    const bool CameraOnly = PriorAvailable && CameraChanged && !SpecificationChanged && !SceneChanged &&
                            std::memcmp(Scene.Camera.data() + 4, PriorCamera.data(), 4 * sizeof(PriorCamera[0])) == 0 &&
                            std::memcmp(Scene.Camera.data() + 8, PriorCamera.data() + 8, 4 * sizeof(PriorCamera[0])) == 0;
    // 📝 Construct all replacement snapshots before changing the retained domain, so allocation failures are atomic.
    std::vector<PathReplayFacet> CurrentSurfaces(Scene.Surfaces.size());
    std::vector<PathReplayReceiver> CurrentReceivers(Scene.Receivers.size());
    std::vector<TriangleVisibilitySpace::FloatEnclosure> CurrentEnclosures(Scene.Enclosures.size());
    std::vector<TriangleVisibilitySpace::FloatFacet> CurrentCorners(Scene.Corners.size());
    std::vector<float> CurrentMaterialAtlas(Scene.MaterialAtlas.size());
    if (!Scene.Surfaces.empty()) std::memcpy(CurrentSurfaces.data(), Scene.Surfaces.data(), Scene.Surfaces.size_bytes());
    if (!Scene.Receivers.empty()) std::memcpy(CurrentReceivers.data(), Scene.Receivers.data(), Scene.Receivers.size_bytes());
    if (!Scene.Enclosures.empty()) std::memcpy(CurrentEnclosures.data(), Scene.Enclosures.data(), Scene.Enclosures.size_bytes());
    if (!Scene.Corners.empty()) std::memcpy(CurrentCorners.data(), Scene.Corners.data(), Scene.Corners.size_bytes());
    if (!Scene.MaterialAtlas.empty())
        std::memcpy(CurrentMaterialAtlas.data(), Scene.MaterialAtlas.data(), Scene.MaterialAtlas.size_bytes());
    std::copy(Scene.Camera.begin(), Scene.Camera.end(), PriorCamera.begin());
    PriorSpecification = Specification;
    PriorSurfaces.swap(CurrentSurfaces);
    PriorReceivers.swap(CurrentReceivers);
    PriorEnclosures.swap(CurrentEnclosures);
    PriorCorners.swap(CurrentCorners);
    PriorMaterialAtlas.swap(CurrentMaterialAtlas);
    PriorAvailable = true;
    return {Changed, Changed && !FullHistoryForAudit ? 1u : 64u, CameraOnly};
}

void PathReplayHistoryDomain::Invalidate() noexcept
{
    PriorAvailable = false;
    PriorSurfaces.clear();
    PriorReceivers.clear();
    PriorEnclosures.clear();
    PriorCorners.clear();
    PriorMaterialAtlas.clear();
}
} // namespace Frontier
