/*==========================================================================================================================================
                                                   REFERENCE RENDERER DEFINITION
==========================================================================================================================================*/

#include "ReferenceRenderer.h"

#include "DiscrepancySequence.h"
#include "ReservoirSampling.h"
#include "VarianceFiltering.h"
#include "Engine/Foundation/DeterministicRandom.h"
#include "Engine/Foundation/Math.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Frontier
{

namespace
{

constexpr double Pi = 3.14159265358979323846;

SpatialVector Add(SpatialVector A, SpatialVector B) noexcept { return {A.X + B.X, A.Y + B.Y, A.Z + B.Z}; }
SpatialVector Sub(SpatialVector A, SpatialVector B) noexcept { return {A.X - B.X, A.Y - B.Y, A.Z - B.Z}; }
SpatialVector Mul(SpatialVector A, double S) noexcept { return {A.X * S, A.Y * S, A.Z * S}; }
double        Dot(SpatialVector A, SpatialVector B) noexcept { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
double        Length(SpatialVector A) noexcept { return std::sqrt(Dot(A, A)); }
SpatialVector Cross(SpatialVector A, SpatialVector B) noexcept
{
    return {A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X};
}
SpatialVector Normalised(SpatialVector A) noexcept
{
    const double M = Length(A);
    return M > 0.0 ? Mul(A, 1.0 / M) : SpatialVector{};
}
double Luminance(const SpectrumRgb& C) noexcept { return 0.2126 * C.R + 0.7152 * C.G + 0.0722 * C.B; }
SpectrumRgb MulRgb(const SpectrumRgb& A, double S) noexcept { return {A.R * S, A.G * S, A.B * S}; }
SpectrumRgb AddRgb(const SpectrumRgb& A, const SpectrumRgb& B) noexcept { return {A.R + B.R, A.G + B.G, A.B + B.B}; }

struct PreparedTri
{
    SpatialVector V[3];
    SpatialVector N[3];
    uint32_t      Mesh = 0;
};

// Möller–Trumbore; returns ray parameter or -1.
double RayTriangle(SpatialVector Origin, SpatialVector Direction, const PreparedTri& Tri) noexcept
{
    const SpatialVector E1 = Sub(Tri.V[1], Tri.V[0]);
    const SpatialVector E2 = Sub(Tri.V[2], Tri.V[0]);
    const SpatialVector P  = Cross(Direction, E2);
    const double        Determinant = Dot(E1, P);
    if (std::abs(Determinant) < 1e-14)
    {
        return -1.0;
    }
    const double        Inv = 1.0 / Determinant;
    const SpatialVector T   = Sub(Origin, Tri.V[0]);
    const double        U   = Dot(T, P) * Inv;
    if (U < 0.0 || U > 1.0)
    {
        return -1.0;
    }
    const SpatialVector Q = Cross(T, E1);
    const double        V = Dot(Direction, Q) * Inv;
    if (V < 0.0 || U + V > 1.0)
    {
        return -1.0;
    }
    const double Distance = Dot(E2, Q) * Inv;
    return Distance > 0.0 ? Distance : -1.0;
}

bool Occluded(const std::vector<PreparedTri>& Triangles, SpatialVector Origin, SpatialVector Direction,
              double MaxDistance) noexcept
{
    for (const PreparedTri& Tri : Triangles)
    {
        const double T = RayTriangle(Origin, Direction, Tri);
        if (T > 0.0 && T < MaxDistance)
        {
            return true;
        }
    }
    return false;
}

// Unshadowed direct contribution of one light at a surface point (Lambert).
SpectrumRgb LightContribution(const ReferenceLight& Light, SpatialVector Point, SpatialVector Normal,
                              SpatialVector View, const ReferenceMaterial& Material,
                              SpatialVector& OutDirection, double& OutDistance) noexcept
{
    double Attenuation = 1.0;
    if (Light.Kind == ReferenceLight::Kind::Directional)
    {
        OutDirection = Light.Vector;
        OutDistance  = 1e30;
    }
    else
    {
        const SpatialVector Delta = Sub(Light.Vector, Point);
        OutDistance               = Length(Delta);
        OutDirection              = OutDistance > 0.0 ? Mul(Delta, 1.0 / OutDistance) : SpatialVector{0, 0, 1};
        Attenuation               = 1.0 / std::max(OutDistance * OutDistance, 1e-6);
    }
    const double NdotL = std::max(Dot(Normal, OutDirection), 0.0);
    const SpectrumRgb Brdf = EvaluateReferenceBrdf(Material, Normal, View, OutDirection);
    const double Scale = NdotL * Attenuation;
    return {Brdf.R * Light.Radiance.R * Scale, Brdf.G * Light.Radiance.G * Scale,
            Brdf.B * Light.Radiance.B * Scale};
}

} // namespace

SpectrumRgb EvaluateReferenceBrdf(const ReferenceMaterial& Material, SpatialVector Normal, SpatialVector View,
                                  SpatialVector Light) noexcept
{
    Normal = Normalised(Normal);
    View   = Normalised(View);
    Light  = Normalised(Light);
    const double NdotV = std::max(Dot(Normal, View), 0.0);
    const double NdotL = std::max(Dot(Normal, Light), 0.0);
    if (NdotV <= 0.0 || NdotL <= 0.0)
    {
        return {};
    }

    const SpatialVector Half = Normalised(Add(View, Light));
    const double NdotH = std::clamp(Dot(Normal, Half), 0.0, 1.0);
    const double VdotH = std::clamp(Dot(View, Half), 0.0, 1.0);
    const double Roughness = std::clamp(Material.Roughness, 0.04, 1.0);
    const double Alpha = Roughness * Roughness;
    const double AlphaSquared = Alpha * Alpha;
    const double Denominator = NdotH * NdotH * (AlphaSquared - 1.0) + 1.0;
    const double Distribution = AlphaSquared / (Pi * Denominator * Denominator);
    const double VisibilityDenominator =
        NdotL * std::sqrt(NdotV * NdotV * (1.0 - AlphaSquared) + AlphaSquared) +
        NdotV * std::sqrt(NdotL * NdotL * (1.0 - AlphaSquared) + AlphaSquared);
    const double Visibility = 0.5 / std::max(VisibilityDenominator, 1e-12);

    const double Ior = std::max(Material.Ior, 1.0);
    const double IorRatio = (Ior - 1.0) / (Ior + 1.0);
    const double DielectricF0 = IorRatio * IorRatio * std::clamp(Material.SpecularLevel, 0.0, 1.0) * 2.0;
    const double Metallic = std::clamp(Material.Metallic, 0.0, 1.0);
    const SpectrumRgb F0{DielectricF0 * (1.0 - Metallic) + Material.Albedo.R * Metallic,
                         DielectricF0 * (1.0 - Metallic) + Material.Albedo.G * Metallic,
                         DielectricF0 * (1.0 - Metallic) + Material.Albedo.B * Metallic};
    const double Schlick = std::pow(1.0 - VdotH, 5.0);
    const SpectrumRgb Fresnel{F0.R + (1.0 - F0.R) * Schlick,
                              F0.G + (1.0 - F0.G) * Schlick,
                              F0.B + (1.0 - F0.B) * Schlick};
    const double DiffuseWeight = (1.0 - Metallic) / Pi;
    return {Distribution * Visibility * Fresnel.R + (1.0 - Fresnel.R) * Material.Albedo.R * DiffuseWeight,
            Distribution * Visibility * Fresnel.G + (1.0 - Fresnel.G) * Material.Albedo.G * DiffuseWeight,
            Distribution * Visibility * Fresnel.B + (1.0 - Fresnel.B) * Material.Albedo.B * DiffuseWeight};
}

namespace
{

// MIS environment integral mirroring IntegrateMaterialEnvironment for a single isotropic stratum:
// one cosine-hemisphere proposal + one GGX-half-vector proposal, eight samples each by default.
SpectrumRgb IntegrateReferenceEnvironment(const ReferenceMaterial& Material, SpatialVector Normal,
                                          SpatialVector View, const ReferenceRenderSettings& Settings,
                                          const TransmittanceLut& Lut, SpatialVector PlanetObserver,
                                          DeterministicRandom& Random)
{
    if (!Settings.AtmosphereEnabled || Settings.EnvironmentSamples == 0)
    {
        return {};
    }
    Normal = Normalised(Normal);
    View = Normalised(View);
    const SpatialVector Helper = std::abs(Normal.Z) < 0.999 ? SpatialVector{0,0,1} : SpatialVector{0,1,0};
    const SpatialVector Tangent = Normalised(Cross(Helper, Normal));
    const SpatialVector Bitangent = Cross(Normal, Tangent);
    const double Components = 2.0;
    const uint32_t Samples = Settings.EnvironmentSamples;
    const double Roughness = std::clamp(Material.Roughness, 0.04, 1.0);
    const double Alpha = Roughness * Roughness;
    SpectrumRgb Accumulated{};

    for (uint32_t Component = 0; Component < 2; ++Component)
    {
        for (uint32_t Sample = 0; Sample < Samples; ++Sample)
        {
            const double First = (static_cast<double>(Sample) + 0.5) / Samples;
            double Second = std::fmod(Sample * 0.61803398875 + Component * 0.371 + Random.NextUnit(), 1.0);
            if (Second < 0.0) Second += 1.0;
            const double Phi = 2.0 * Pi * Second;
            SpatialVector Light{};
            SpatialVector Half{};

            if (Component == 0)
            {
                const SpatialVector Local{std::sqrt(First) * std::cos(Phi),
                                          std::sqrt(First) * std::sin(Phi),
                                          std::sqrt(1.0 - First)};
                Light = Normalised(Add(Add(Mul(Tangent, Local.X), Mul(Bitangent, Local.Y)), Mul(Normal, Local.Z)));
            }
            else
            {
                const double Radius = std::sqrt(First / std::max(1.0 - First, 1e-12));
                const SpatialVector LocalHalf = Normalised({Alpha * std::cos(Phi) * Radius,
                                                            Alpha * std::sin(Phi) * Radius, 1.0});
                Half = Normalised(Add(Add(Mul(Tangent, LocalHalf.X), Mul(Bitangent, LocalHalf.Y)),
                                      Mul(Normal, LocalHalf.Z)));
                if (Dot(View, Half) <= 0.0)
                {
                    continue;
                }
                Light = Normalised(Sub(Mul(Half, 2.0 * Dot(View, Half)), View));
            }

            const double NdotL = std::max(Dot(Normal, Light), 0.0);
            const SpatialVector H = Normalised(Add(View, Light));
            const double NdotH = std::max(Dot(Normal, H), 0.0);
            const double VdotH = std::max(Dot(View, H), 1e-6);
            const double Dden = NdotH * NdotH * (Alpha * Alpha - 1.0) + 1.0;
            const double D = (Alpha * Alpha) / (Pi * Dden * Dden);
            double Density = NdotL / Pi + D * NdotH / (4.0 * VdotH);
            Density /= Components;
            if (Density <= 1e-12)
            {
                continue;
            }

            SpectrumRgb Environment = SingleScattering(Settings.Atmosphere, PlanetObserver, Light,
                                                        Settings.SunDirection, Settings.SunIrradiance, &Lut, 12);
            const SpectrumRgb Brdf = EvaluateReferenceBrdf(Material, Normal, View, Light);
            const double Weight = NdotL / Density / (Components * Samples);
            Accumulated = AddRgb(Accumulated, {Brdf.R * Environment.R * Weight,
                                               Brdf.G * Environment.G * Weight,
                                               Brdf.B * Environment.B * Weight});
        }
    }
    return Accumulated;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                       SCENE PREP
//------------------------------------------------------------------------------------------------------------------------

void ReferenceRenderer::SetScene(ReferenceScene Scene)
{
    Scene_ = std::move(Scene);
    Prepared_.clear();
    for (const ReferenceMesh& Mesh : Scene_.Meshes)
    {
        PreparedMesh Prepared;
        Prepared.Material = Mesh.Material;
        Prepared.Emissive = Mesh.Emissive;
        Prepared.WorldGeometry.Indices = Mesh.Geometry.Indices;
        Prepared.WorldGeometry.Vertices.reserve(Mesh.Geometry.Vertices.size());
        for (const SpatialVector& V : Mesh.Geometry.Vertices)
        {
            Prepared.WorldGeometry.Vertices.push_back(Add(Mul(V, Mesh.Scale), Mesh.Translation));
        }
        // Area-weighted smooth normals.
        Prepared.Normals.assign(Prepared.WorldGeometry.Vertices.size(), SpatialVector{});
        const std::vector<uint32_t>& Indices = Prepared.WorldGeometry.Indices;
        for (size_t T = 0; T + 2 < Indices.size(); T += 3)
        {
            const SpatialVector A = Prepared.WorldGeometry.Vertices[Indices[T]];
            const SpatialVector B = Prepared.WorldGeometry.Vertices[Indices[T + 1]];
            const SpatialVector C = Prepared.WorldGeometry.Vertices[Indices[T + 2]];
            const SpatialVector Face = Cross(Sub(B, A), Sub(C, A));
            for (uint32_t K = 0; K < 3; ++K)
            {
                Prepared.Normals[Indices[T + K]] = Add(Prepared.Normals[Indices[T + K]], Face);
            }
        }
        for (SpatialVector& N : Prepared.Normals)
        {
            N = Normalised(N);
        }
        (void)Prepared.Nesting.Build(Prepared.WorldGeometry, 64);
        Prepared_.push_back(std::move(Prepared));
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          RENDER
//------------------------------------------------------------------------------------------------------------------------

ReferenceImage ReferenceRenderer::Render(const CullCamera& Camera, const ReferenceRenderSettings& Settings)
{
    ReferenceImage Image;
    Image.Width  = Settings.Width;
    Image.Height = Settings.Height;
    const uint32_t W = Settings.Width, H = Settings.Height;
    const size_t   PixelCount = static_cast<size_t>(W) * H;
    Image.Pixels.assign(PixelCount, SpectrumRgb{});
    if (W == 0 || H == 0)
    {
        return Image;
    }

    const double AltitudeKm = std::max(Camera.Position.Z, 0.0) / 1000.0;
    const SpatialVector PlanetObserver{0.0, 0.0, Settings.Atmosphere.GroundRadiusKm + AltitudeKm};
    if (Settings.AtmosphereEnabled && !AtmosphereLutBaked_)
    {
        (void)AtmosphereLut_.Bake(Settings.Atmosphere, 128, 32, 48);
        AtmosphereLutBaked_ = true;
    }

    const double TanY = std::tan(Camera.FovYRadians * 0.5);
    const double TanX = TanY * Camera.Aspect;
    const double Projection = ClusterNesting::ProjectionFactor(static_cast<double>(H), Camera.FovYRadians);

    CameraFrustum Frustum;
    (void)Frustum.Build(Camera);

    // --- Geometry stage: cut + cull per mesh, gather visible triangles --------------------------------
    std::vector<PreparedTri> Visible;
    for (uint32_t MeshIndex = 0; MeshIndex < Prepared_.size(); ++MeshIndex)
    {
        PreparedMesh& Mesh = Prepared_[MeshIndex];
        Image.Stats.TrianglesTotal += Mesh.Nesting.TriangleTotal();
        std::vector<uint32_t> Cut;
        Mesh.Nesting.SelectCut(Camera.Position, Projection, Settings.CutErrorPx, Cut);
        std::vector<uint32_t> Culled;
        CullCut(Mesh.Nesting, Cut, Frustum, Camera, nullptr, Culled);
        Image.Stats.CutNodes += static_cast<uint32_t>(Cut.size());
        for (uint32_t NodeIndex : Culled)
        {
            // Collect the node's triangles (contiguous range by construction of the builder).
            const ClusterNode& Node = Mesh.Nesting.Nodes()[NodeIndex];
            Image.Stats.TrianglesAfterCut += Node.TriangleCount;
            std::vector<uint32_t> Stack{NodeIndex};
            while (!Stack.empty())
            {
                const uint32_t Current = Stack.back();
                Stack.pop_back();
                const ClusterNode& N = Mesh.Nesting.Nodes()[Current];
                if (N.ChildCount > 0)
                {
                    for (uint32_t S = 0; S < N.ChildCount; ++S)
                    {
                        Stack.push_back(Mesh.Nesting.ChildIndices()[N.FirstChild + S]);
                    }
                    continue;
                }
                for (uint32_t Slot = 0; Slot < N.TriangleCount; ++Slot)
                {
                    const uint32_t TriIndex = Mesh.Nesting.TriangleOrder()[N.FirstTriangle + Slot];
                    PreparedTri Tri;
                    Tri.Mesh = MeshIndex;
                    for (uint32_t K = 0; K < 3; ++K)
                    {
                        const uint32_t Vertex = Mesh.WorldGeometry.Indices[TriIndex * 3 + K];
                        Tri.V[K] = Mesh.WorldGeometry.Vertices[Vertex];
                        Tri.N[K] = Mesh.Normals[Vertex];
                    }
                    Visible.push_back(Tri);
                }
            }
        }
    }
    Image.Stats.TrianglesVisible = static_cast<uint32_t>(Visible.size());

    // --- Raster stage: G-buffer ------------------------------------------------------------------------
    std::vector<double>        ViewDepth(PixelCount, std::numeric_limits<double>::infinity());
    std::vector<SpatialVector> WorldPos(PixelCount), WorldNormal(PixelCount);
    std::vector<ReferenceMaterial> MaterialBuffer(PixelCount);
    std::vector<SpectrumRgb>   EmissiveBuffer(PixelCount);
    std::vector<uint8_t>       HitMask(PixelCount, 0);

    const auto ToView = [&](SpatialVector World) {
        const SpatialVector Rel = Sub(World, Camera.Position);
        return SpatialVector{Dot(Rel, Camera.Right), Dot(Rel, Camera.Up), Dot(Rel, Camera.Forward)};
    };

    for (const PreparedTri& Tri : Visible)
    {
        // View-space polygon with attributes; clip against the near plane only
        // (screen bounds are handled by the rasteriser's bounding box clamp).
        struct ClipVertex { SpatialVector View, World, Normal; };
        std::vector<ClipVertex> Polygon;
        for (uint32_t K = 0; K < 3; ++K)
        {
            Polygon.push_back({ToView(Tri.V[K]), Tri.V[K], Tri.N[K]});
        }
        std::vector<ClipVertex> Clipped;
        for (size_t I = 0; I < Polygon.size(); ++I)
        {
            const ClipVertex& Current = Polygon[I];
            const ClipVertex& Previous = Polygon[(I + Polygon.size() - 1) % Polygon.size()];
            const double DC = Current.View.Z - Camera.NearPlane;
            const double DP = Previous.View.Z - Camera.NearPlane;
            if (DC >= 0.0)
            {
                if (DP < 0.0)
                {
                    const double F = DP / (DP - DC);
                    Clipped.push_back({Add(Previous.View, Mul(Sub(Current.View, Previous.View), F)),
                                       Add(Previous.World, Mul(Sub(Current.World, Previous.World), F)),
                                       Add(Previous.Normal, Mul(Sub(Current.Normal, Previous.Normal), F))});
                }
                Clipped.push_back(Current);
            }
            else if (DP >= 0.0)
            {
                const double F = DP / (DP - DC);
                Clipped.push_back({Add(Previous.View, Mul(Sub(Current.View, Previous.View), F)),
                                   Add(Previous.World, Mul(Sub(Current.World, Previous.World), F)),
                                   Add(Previous.Normal, Mul(Sub(Current.Normal, Previous.Normal), F))});
            }
        }
        if (Clipped.size() < 3)
        {
            continue;
        }
        // Project + fan-triangulate.
        struct ScreenVertex { double X, Y, InvZ, ViewZ; SpatialVector World, Normal; };
        std::vector<ScreenVertex> Screen;
        for (const ClipVertex& V : Clipped)
        {
            const double NdcX = V.View.X / (V.View.Z * TanX);
            const double NdcY = V.View.Y / (V.View.Z * TanY);
            Screen.push_back({(NdcX * 0.5 + 0.5) * W, (1.0 - (NdcY * 0.5 + 0.5)) * H, 1.0 / V.View.Z, V.View.Z,
                              V.World, V.Normal});
        }
        const ReferenceMaterial& MeshMaterial = Prepared_[Tri.Mesh].Material;
        const SpectrumRgb& MeshEmissive = Prepared_[Tri.Mesh].Emissive;
        for (size_t Fan = 1; Fan + 1 < Screen.size(); ++Fan)
        {
            const ScreenVertex* V[3] = {&Screen[0], &Screen[Fan], &Screen[Fan + 1]};
            const double Area = (V[1]->X - V[0]->X) * (V[2]->Y - V[0]->Y) - (V[1]->Y - V[0]->Y) * (V[2]->X - V[0]->X);
            if (std::abs(Area) < 1e-12)
            {
                continue;
            }
            const int32_t MinX = static_cast<int32_t>(std::max(0.0, std::floor(std::min({V[0]->X, V[1]->X, V[2]->X}))));
            const int32_t MaxX = static_cast<int32_t>(std::min(static_cast<double>(W - 1), std::ceil(std::max({V[0]->X, V[1]->X, V[2]->X}))));
            const int32_t MinY = static_cast<int32_t>(std::max(0.0, std::floor(std::min({V[0]->Y, V[1]->Y, V[2]->Y}))));
            const int32_t MaxY = static_cast<int32_t>(std::min(static_cast<double>(H - 1), std::ceil(std::max({V[0]->Y, V[1]->Y, V[2]->Y}))));
            for (int32_t Y = MinY; Y <= MaxY; ++Y)
            {
                for (int32_t X = MinX; X <= MaxX; ++X)
                {
                    const double PX = X + 0.5, PY = Y + 0.5;
                    const double W0 = ((V[1]->X - PX) * (V[2]->Y - PY) - (V[1]->Y - PY) * (V[2]->X - PX)) / Area;
                    const double W1 = ((V[2]->X - PX) * (V[0]->Y - PY) - (V[2]->Y - PY) * (V[0]->X - PX)) / Area;
                    const double W2 = 1.0 - W0 - W1;
                    if (W0 < 0.0 || W1 < 0.0 || W2 < 0.0)
                    {
                        continue;
                    }
                    const double InvZ = W0 * V[0]->InvZ + W1 * V[1]->InvZ + W2 * V[2]->InvZ;
                    if (InvZ <= 0.0)
                    {
                        continue;
                    }
                    const double ViewZ = 1.0 / InvZ;
                    const size_t Index = static_cast<size_t>(Y) * W + static_cast<size_t>(X);
                    if (ViewZ >= ViewDepth[Index])
                    {
                        continue;
                    }
                    const auto Interp = [&](const SpatialVector& A0, const SpatialVector& A1, const SpatialVector& A2) {
                        return Mul(Add(Add(Mul(A0, W0 * V[0]->InvZ), Mul(A1, W1 * V[1]->InvZ)),
                                       Mul(A2, W2 * V[2]->InvZ)), ViewZ);
                    };
                    ViewDepth[Index]     = ViewZ;
                    WorldPos[Index]      = Interp(V[0]->World, V[1]->World, V[2]->World);
                    WorldNormal[Index]   = Normalised(Interp(V[0]->Normal, V[1]->Normal, V[2]->Normal));
                    MaterialBuffer[Index] = MeshMaterial;
                    EmissiveBuffer[Index] = MeshEmissive;
                    HitMask[Index]       = 1;
                }
            }
        }
    }
    for (size_t I = 0; I < PixelCount; ++I)
    {
        Image.Stats.SurfacePixels += HitMask[I] ? 1u : 0u;
        // Double-sided shading: face normals toward the viewer.
        if (HitMask[I] && Dot(WorldNormal[I], Sub(WorldPos[I], Camera.Position)) > 0.0)
        {
            WorldNormal[I] = Mul(WorldNormal[I], -1.0);
        }
    }

    // Environment MIS is evaluated once per surface pixel and reused across direct-light frames.
    std::vector<SpectrumRgb> EnvironmentBuffer(PixelCount, SpectrumRgb{});
    if (Settings.AtmosphereEnabled && Settings.EnvironmentSamples > 0)
    {
        for (size_t P = 0; P < PixelCount; ++P)
        {
            if (!HitMask[P]) continue;
            DeterministicRandom Random(Settings.Seed ^ (0xD1B54A32D192ED03ull * (P + 1)), 0xE17B1B3ull);
            const SpatialVector View = Normalised(Sub(Camera.Position, WorldPos[P]));
            EnvironmentBuffer[P] = IntegrateReferenceEnvironment(MaterialBuffer[P], WorldNormal[P], View,
                Settings, AtmosphereLut_, PlanetObserver, Random);
            Image.Stats.MeanEnvironmentIbl += Luminance(EnvironmentBuffer[P]);
        }
        Image.Stats.MeanEnvironmentIbl /= std::max<uint32_t>(Image.Stats.SurfacePixels, 1u);
    }

    // Light proposal pdf (power proxy; correctness holds for any positive proposal).
    std::vector<double> LightPdf(Scene_.Lights.size(), 0.0);
    {
        double Total = 0.0;
        for (size_t I = 0; I < Scene_.Lights.size(); ++I)
        {
            LightPdf[I] = std::max(Luminance(Scene_.Lights[I].Radiance), 1e-6);
            Total += LightPdf[I];
        }
        if (Total > 0.0)
        {
            for (double& P : LightPdf) P /= Total;
        }
    }
    const auto PickLight = [&](double U) {
        double Acc = 0.0;
        for (uint32_t I = 0; I < LightPdf.size(); ++I)
        {
            Acc += LightPdf[I];
            if (U < Acc) return I;
        }
        return static_cast<uint32_t>(LightPdf.size()) - 1;
    };

    // --- Lighting accumulation frames (direct RIS + reuse, DF/card indirect) ----------------------------
    std::vector<SpectrumRgb> MeanColour(PixelCount, SpectrumRgb{});
    std::vector<double>      MeanLuminanceBuffer(PixelCount, 0.0);
    VarianceHistory          History;
    History.Resize(W, H);
    std::vector<Reservoir>   PreviousReservoirs(PixelCount);

    const uint32_t Frames = std::max(Settings.AccumulationFrames, 1u);
    std::vector<double> FrameLuminance(PixelCount, 0.0);
    for (uint32_t Frame = 0; Frame < Frames; ++Frame)
    {
        std::vector<Reservoir> Reservoirs(PixelCount);
        std::vector<SpectrumRgb> FrameColour(PixelCount, SpectrumRgb{});
        std::vector<SpectrumRgb> DirectColour(PixelCount, SpectrumRgb{});
        std::vector<SpectrumRgb> ResidualColour(PixelCount, SpectrumRgb{});   // indirect + emissive

        for (size_t P = 0; P < PixelCount; ++P)
        {
            if (!HitMask[P])
            {
                continue;
            }
            DeterministicRandom Random(Settings.Seed ^ (0x9E3779B9ull * (P + 1)), 0xF00Dull + Frame);
            const SpatialVector& Point  = WorldPos[P];
            const SpatialVector& Normal = WorldNormal[P];
            const ReferenceMaterial& Material = MaterialBuffer[P];
            const SpatialVector View = Normalised(Sub(Camera.Position, Point));

            // Initial candidates (RIS), temporal combine, finalise.
            Reservoir& R = Reservoirs[P];
            for (uint32_t C = 0; C < Settings.RisCandidates && !Scene_.Lights.empty(); ++C)
            {
                const uint32_t Light = PickLight(Random.NextUnit());
                SpatialVector Dir; double Dist;
                const SpectrumRgb Unshadowed = LightContribution(Scene_.Lights[Light], Point, Normal, View, Material, Dir, Dist);
                const double Target = Luminance(Unshadowed);
                R.Update({Light, 0.0, 0.0}, LightPdf[Light] > 0.0 ? Target / LightPdf[Light] : 0.0, Random.NextUnit());
            }
            if (Frame > 0 && !Scene_.Lights.empty())
            {
                Reservoir Previous = PreviousReservoirs[P];
                Previous.ClampSampleCount(20u * std::max(Settings.RisCandidates, 1u));
                SpatialVector Dir; double Dist;
                const double TargetHere = Luminance(LightContribution(
                    Scene_.Lights[Previous.Sample().Candidate], Point, Normal, View, Material, Dir, Dist));
                R.Combine(Previous, TargetHere, 1.0, Random.NextUnit());
            }
            if (!Scene_.Lights.empty())
            {
                SpatialVector Dir; double Dist;
                R.Finalize(Luminance(LightContribution(Scene_.Lights[R.Sample().Candidate], Point, Normal, View, Material, Dir, Dist)));
            }

            SpectrumRgb Direct{};
            if (!Scene_.Lights.empty() && R.HasSample())
            {
                const uint32_t Light = R.Sample().Candidate;
                SpatialVector Dir; double Dist;
                SpectrumRgb F = LightContribution(Scene_.Lights[Light], Point, Normal, View, Material, Dir, Dist);
                const SpatialVector Origin = Add(Point, Mul(Normal, 2e-3));
                if (Occluded(Visible, Origin, Dir, Dist - 4e-3))
                {
                    F = {};
                }
                Direct = {F.R * R.Weight(), F.G * R.Weight(), F.B * R.Weight()};
            }

            // Indirect: distance-field gather + optional card texel.
            SpectrumRgb Indirect = EnvironmentBuffer[P];
            if (Settings.IndirectSamples > 0 && !Scene_.Distance.Primitives().empty())
            {
                const double E = Scene_.Distance.IndirectIrradiance(Point, Normal, Settings.IndirectSamples, Random);
                Indirect = AddRgb(Indirect, {Material.Albedo.R * (1.0 - Material.Metallic) * E / Pi,
                                              Material.Albedo.G * (1.0 - Material.Metallic) * E / Pi,
                                              Material.Albedo.B * (1.0 - Material.Metallic) * E / Pi});
            }
            if (Scene_.Cards != nullptr && Scene_.Cards->TexelCount() > 0)
            {
                double BestD2 = 2.25;   // within 1.5 m
                double BestB = 0.0;
                for (uint32_t T = 0; T < Scene_.Cards->TexelCount(); ++T)
                {
                    const SpatialVector Delta = Sub(Scene_.Cards->TexelCentre(T), Point);
                    const double D2 = Dot(Delta, Delta);
                    if (D2 < BestD2) { BestD2 = D2; BestB = Scene_.Cards->Radiosity(T); }
                }
                Indirect = AddRgb(Indirect, {Material.Albedo.R * (1.0 - Material.Metallic) * BestB / Pi,
                                     Material.Albedo.G * (1.0 - Material.Metallic) * BestB / Pi,
                                     Material.Albedo.B * (1.0 - Material.Metallic) * BestB / Pi});
            }

            DirectColour[P]   = Direct;
            ResidualColour[P] = AddRgb(Indirect, EmissiveBuffer[P]);
            FrameColour[P]    = AddRgb(Direct, ResidualColour[P]);
        }

        // Spatial reuse pass (borrow right/below neighbours; target re-evaluated here).
        if (Settings.SpatialReuse && !Scene_.Lights.empty())
        {
            const std::vector<Reservoir> Source = Reservoirs;
            for (uint32_t Y = 0; Y < H; ++Y)
            {
                for (uint32_t X = 0; X < W; ++X)
                {
                    const size_t P = static_cast<size_t>(Y) * W + X;
                    if (!HitMask[P])
                    {
                        continue;
                    }
                    DeterministicRandom Random(Settings.Seed ^ (0x85EBCA77ull * (P + 1)), 0x57A7ull + Frame);
                    Reservoir& R = Reservoirs[P];
                    const size_t Neighbours[2] = {P + (X + 1 < W ? 1 : 0), P + (Y + 1 < H ? W : 0)};
                    for (size_t NP : Neighbours)
                    {
                        if (NP == P || !HitMask[NP] || !Source[NP].HasSample())
                        {
                            continue;
                        }
                        SpatialVector Dir; double Dist;
                        const double TargetHere = Luminance(LightContribution(
                            Scene_.Lights[Source[NP].Sample().Candidate], WorldPos[P], WorldNormal[P], Normalised(Sub(Camera.Position, WorldPos[P])),
                            MaterialBuffer[P], Dir, Dist));
                        R.Combine(Source[NP], TargetHere, 1.0, Random.NextUnit());
                    }
                    SpatialVector Dir; double Dist;
                    R.Finalize(Luminance(LightContribution(Scene_.Lights[R.Sample().Candidate], WorldPos[P],
                                                           WorldNormal[P], Normalised(Sub(Camera.Position, WorldPos[P])), MaterialBuffer[P], Dir, Dist)));
                    if (R.HasSample())
                    {
                        SpectrumRgb F = LightContribution(Scene_.Lights[R.Sample().Candidate], WorldPos[P],
                                                          WorldNormal[P], Normalised(Sub(Camera.Position, WorldPos[P])), MaterialBuffer[P], Dir, Dist);
                        const SpatialVector Origin = Add(WorldPos[P], Mul(WorldNormal[P], 2e-3));
                        if (Occluded(Visible, Origin, Dir, Dist - 4e-3))
                        {
                            F = {};
                        }
                        // Replace this pixel's direct term with the spatially-reused estimate;
                        // indirect + emissive stay as computed this frame.
                        DirectColour[P] = {F.R * R.Weight(), F.G * R.Weight(), F.B * R.Weight()};
                        FrameColour[P]  = AddRgb(DirectColour[P], ResidualColour[P]);
                    }
                }
            }
        }

        PreviousReservoirs = Reservoirs;

        for (size_t P = 0; P < PixelCount; ++P)
        {
            MeanColour[P] = AddRgb(MeanColour[P], MulRgb(FrameColour[P], 1.0 / Frames));
            FrameLuminance[P] = Luminance(FrameColour[P]);
        }
        if (Settings.Denoise)
        {
            (void)History.Integrate(FrameLuminance.data(), W, H, Frame == 0 ? 1.0 : 0.35);
        }
    }

    // --- Sky (computed once) ------------------------------------------------------------------------------
    std::vector<SpectrumRgb> Sky(PixelCount, SpectrumRgb{});
    if (Settings.AtmosphereEnabled)
    {
        const double CosSunDisc = std::cos(0.00465);
        for (uint32_t Y = 0; Y < H; ++Y)
        {
            for (uint32_t X = 0; X < W; ++X)
            {
                const size_t P = static_cast<size_t>(Y) * W + X;
                if (HitMask[P])
                {
                    continue;
                }
                const double NdcX = 2.0 * (X + 0.5) / W - 1.0;
                const double NdcY = 1.0 - 2.0 * (Y + 0.5) / H;
                const SpatialVector Dir = Normalised(Add(Add(Camera.Forward, Mul(Camera.Right, NdcX * TanX)),
                                                         Mul(Camera.Up, NdcY * TanY)));
                SpectrumRgb Value = SingleScattering(Settings.Atmosphere, PlanetObserver, Dir, Settings.SunDirection,
                                                     Settings.SunIrradiance, &AtmosphereLut_, 24);
                const double CosSun = Dot(Dir, Settings.SunDirection);
                if (CosSun > CosSunDisc)
                {
                    const double RadiusKm = Settings.Atmosphere.GroundRadiusKm + AltitudeKm;
                    const SpectrumRgb T = AtmosphereLut_.Sample(Settings.Atmosphere, RadiusKm,
                                                                std::clamp(Dir.Z, -1.0, 1.0));
                    const SpectrumRgb Disc{Settings.SunIrradiance.R * T.R * 40.0, Settings.SunIrradiance.G * T.G * 40.0,
                                           Settings.SunIrradiance.B * T.B * 40.0};
                    Value = AddRgb(Value, Disc);
                }
                if (Settings.CloudsEnabled && Dir.Z > -0.05)
                {
                    const CloudMarchResult CM = MarchClouds(Settings.Clouds, Camera.Position, Dir,
                                                            Settings.SunDirection,
                                                            MulRgb(Settings.SunIrradiance, Settings.CloudSunScale),
                                                            30000.0, 40, 4);
                    Value = {Value.R * CM.Transmittance + CM.Radiance.R, Value.G * CM.Transmittance + CM.Radiance.G,
                             Value.B * CM.Transmittance + CM.Radiance.B};
                }
                Sky[P] = Value;
            }
        }
    }

    // --- Denoise (SVGF luminance) + composite --------------------------------------------------------------
    std::vector<SpectrumRgb> FinalColour = MeanColour;
    if (Settings.Denoise && Frames > 0)
    {
        // Guides: view depth (miss = far), normals as float Vec3.
        std::vector<double> GuideDepth(PixelCount);
        std::vector<Vec3>   GuideNormals(PixelCount);
        for (size_t P = 0; P < PixelCount; ++P)
        {
            GuideDepth[P] = HitMask[P] ? ViewDepth[P] : Camera.FarPlane;
            const SpatialVector& N = WorldNormal[P];
            GuideNormals[P] = HitMask[P] ? Vec3{static_cast<float>(N.X), static_cast<float>(N.Y), static_cast<float>(N.Z)}
                                         : Vec3{0.0f, 0.0f, 1.0f};
            MeanLuminanceBuffer[P] = Luminance(MeanColour[P]);
        }
        std::vector<double> Ping = History.Mean(), Pong(PixelCount), VarPing = History.Variance(), VarPong(PixelCount);
        const VarianceFilterParameters Params{};
        for (uint32_t Level = 0; Level < 5; ++Level)
        {
            const bool Last = Level == 4;
            (void)VarianceWaveletStep(Ping.data(), VarPing.data(), GuideDepth.data(), GuideNormals.data(), W, H,
                                      1u << Level, Params, Pong.data(), Last ? nullptr : VarPong.data());
            Ping.swap(Pong);
            VarPing.swap(VarPong);
        }
        for (size_t P = 0; P < PixelCount; ++P)
        {
            if (!HitMask[P])
            {
                continue;
            }
            const double Ratio = std::clamp(Ping[P] / std::max(MeanLuminanceBuffer[P], 1e-6), 0.2, 5.0);
            FinalColour[P] = MulRgb(MeanColour[P], Ratio);
        }
    }

    double LuminanceSum = 0.0;
    for (size_t P = 0; P < PixelCount; ++P)
    {
        SpectrumRgb Value = HitMask[P] ? FinalColour[P] : Sky[P];
        if (HitMask[P] && Settings.FogEnabled)
        {
            const SpatialVector Delta = Sub(WorldPos[P], Camera.Position);
            const double Distance = Length(Delta);
            const double T = FogTransmittance(Settings.Fog, Camera.Position,
                                              Distance > 0 ? Mul(Delta, 1.0 / Distance) : Camera.Forward, Distance);
            Value = ApplyFog(Settings.Fog, Value, T);
        }
        Image.Pixels[P] = Value;
        LuminanceSum += Luminance(Value);
    }
    Image.Stats.MeanLuminance = PixelCount ? LuminanceSum / static_cast<double>(PixelCount) : 0.0;
    return Image;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       UTILITIES
//------------------------------------------------------------------------------------------------------------------------

uint64_t ReferenceImageHash(const ReferenceImage& Image) noexcept
{
    uint64_t Hash = 1469598103934665603ull;
    const auto Mix = [&Hash](uint64_t Value) {
        for (int Byte = 0; Byte < 8; ++Byte)
        {
            Hash ^= (Value >> (Byte * 8)) & 0xFFull;
            Hash *= 1099511628211ull;
        }
    };
    Mix(Image.Width);
    Mix(Image.Height);
    for (const SpectrumRgb& P : Image.Pixels)
    {
        Mix(static_cast<uint64_t>(std::llround(P.R * 1e6)));
        Mix(static_cast<uint64_t>(std::llround(P.G * 1e6)));
        Mix(static_cast<uint64_t>(std::llround(P.B * 1e6)));
    }
    return Hash;
}

void EncodeDisplayRgb8(const ReferenceImage& Image, double Exposure, std::vector<uint8_t>& OutRgb8)
{
    OutRgb8.assign(Image.Pixels.size() * 3, 0);
    const auto Encode = [Exposure](double V) {
        const double Tonemapped = 1.0 - std::exp(-std::max(V, 0.0) * Exposure);
        return static_cast<uint8_t>(std::clamp(std::pow(Tonemapped, 1.0 / 2.2), 0.0, 1.0) * 255.0 + 0.5);
    };
    for (size_t P = 0; P < Image.Pixels.size(); ++P)
    {
        OutRgb8[P * 3 + 0] = Encode(Image.Pixels[P].R);
        OutRgb8[P * 3 + 1] = Encode(Image.Pixels[P].G);
        OutRgb8[P * 3 + 2] = Encode(Image.Pixels[P].B);
    }
}

} // namespace Frontier
