/*==========================================================================================================================================
                                                         VISIBILITYRESOLVE.CPP
==========================================================================================================================================*/
// 🧩 VisibilityResolve.js statement for statement: identity packing, the two-pass visibility raster and the deferred resolve.
#include "VisibilityResolve.h"
#include <algorithm>
namespace Frontier::VirtualizedGeometry
{
namespace
{
struct WorldCorner
{
    Point3 Local{}, World{};
    Point2 Uv{};
};
[[nodiscard]] std::array<WorldCorner, 3> TriangleWorldCorners(const RefreshCluster& Item, uint32_t Triangle)
{
    const NestingTier& Tier = Item.Nesting->Tiers[Item.Node->Tier];
    std::array<WorldCorner, 3> Corners;
    for (uint32_t C = 0; C < 3; ++C)
    {
        Corners[C].Local = Tier.ReadPosition(Tier.Triangles[Triangle * 3 + C]);
        Corners[C].World = TransformWorldPoint(Item.LocalToWorld, Corners[C].Local);
        Corners[C].Uv    = {Tier.CornerUvs[static_cast<size_t>(Triangle) * 6 + C * 2], Tier.CornerUvs[static_cast<size_t>(Triangle) * 6 + C * 2 + 1]};
    }
    return Corners;
}
[[nodiscard]] Point3 FaceNormal(const std::array<WorldCorner, 3>& Corners)
{
    const Point3 AB = Subtract(Corners[1].World, Corners[0].World);
    const Point3 AC = Subtract(Corners[2].World, Corners[0].World);
    const Point3 N  = Cross(AB, AC);
    double L = JsHypot(N[0], N[1], N[2]);
    if (L == 0 || std::isnan(L)) L = 1;
    return {N[0] / L, N[1] / L, N[2] / L};
}

constexpr std::array<std::array<double, 3>, 8> FamilyPalette = {{{0.62, 0.65, 0.70}, {0.55, 0.48, 0.38}, {0.72, 0.74, 0.78}, {0.30, 0.34, 0.40},
                                                                 {0.66, 0.30, 0.24}, {0.24, 0.44, 0.62}, {0.28, 0.56, 0.36}, {0.70, 0.58, 0.26}}};
constexpr std::array<double, 3> SkyColour = {0.36, 0.48, 0.66};

[[nodiscard]] Point3 LightDirection() noexcept
{
    const double L = JsHypot(0.45, 0.75, 0.6);
    return {0.45 / L, 0.75 / L, 0.6 / L};
}

struct SurfacePoint
{
    const RefreshCluster* Item = nullptr;
    uint32_t              Triangle = 0;
    Point3                World{};
    Point2                Uv{};
    Point3                Normal{};
    uint32_t              Family = 0, ClassMask = 0;
};
[[nodiscard]] std::optional<SurfacePoint> ResolveSurfacePoint(const VisibilityRaster& Raster, const std::vector<uint32_t>& Vis,
                                                              const std::vector<float>& Bary, size_t Pixel)
{
    const std::optional<VisibilityIdentity> Decoded = UnpackVisibilityIdentity(Vis[Pixel]);
    if (!Decoded || Decoded->RecordSlot >= Raster.Items.size()) return std::nullopt;
    const RefreshCluster& Item = Raster.Items[Decoded->RecordSlot];
    if (Decoded->LocalTriangle >= Item.Node->Triangles.size()) return std::nullopt;
    const uint32_t Triangle = Item.Node->Triangles[Decoded->LocalTriangle];
    const std::array<WorldCorner, 3> Corners = TriangleWorldCorners(Item, Triangle);
    const double W1 = Bary[Pixel * 2], W2 = Bary[Pixel * 2 + 1], W0 = 1 - W1 - W2;
    SurfacePoint Surface;
    Surface.Item     = &Item;
    Surface.Triangle = Triangle;
    for (int A = 0; A < 3; ++A) Surface.World[A] = W0 * Corners[0].World[A] + W1 * Corners[1].World[A] + W2 * Corners[2].World[A];
    for (int A = 0; A < 2; ++A) Surface.Uv[A] = W0 * Corners[0].Uv[A] + W1 * Corners[1].Uv[A] + W2 * Corners[2].Uv[A];
    Surface.Normal    = FaceNormal(Corners);
    Surface.Family    = Item.Nesting->Tiers[Item.Node->Tier].Families[Triangle];
    Surface.ClassMask = Item.Node->ClassMask;
    return Surface;
}

[[nodiscard]] std::array<double, 3> ShadeOpaque(const SurfacePoint& Surface)
{
    const std::array<double, 3>& Base = FamilyBaseColour(Surface.Family);
    double R = Base[0], G = Base[1], B = Base[2];
    // UV checker modulation where the surface carries UVs: the deferred resolve is where UVs become material.
    if (Surface.Item->Nesting->HasUvs)
    {
        const double Checker = std::fmod(std::floor(Surface.Uv[0] * 24) + std::floor(Surface.Uv[1] * 24), 2.0) == 0 ? 1.0 : 0.45;
        R *= Checker;
        G *= Checker;
        B *= Checker;
    }
    const Point3 Light   = LightDirection();
    const double Diffuse = JsMax(0.0, Surface.Normal[0] * Light[0] + Surface.Normal[1] * Light[1] + Surface.Normal[2] * Light[2]);
    const double Ambient = 0.32;
    return {R * (Ambient + Diffuse), G * (Ambient + Diffuse), B * (Ambient + Diffuse)};
}

[[nodiscard]] std::optional<Point3> RefractDirection(const Point3& ViewDir, const Point3& Normal, double EtaRatio)
{
    const double CosI = -(ViewDir[0] * Normal[0] + ViewDir[1] * Normal[1] + ViewDir[2] * Normal[2]);
    const double K    = 1 - EtaRatio * EtaRatio * (1 - CosI * CosI);
    if (K < 0) return std::nullopt;   // total internal reflection
    const double F = EtaRatio * CosI - std::sqrt(K);
    return Point3{EtaRatio * ViewDir[0] + F * Normal[0], EtaRatio * ViewDir[1] + F * Normal[1], EtaRatio * ViewDir[2] + F * Normal[2]};
}
} // namespace

std::optional<VisibilityIdentity> UnpackVisibilityIdentity(uint32_t Identity) noexcept
{
    if (!Identity) return std::nullopt;
    const uint32_t Zero = Identity - 1;
    return VisibilityIdentity{Zero / 256, Zero % 256};
}

const std::array<double, 3>& FamilyBaseColour(uint32_t Family) noexcept { return FamilyPalette[Family % FamilyPalette.size()]; }

VisibilityRaster RasterizeVisibility(const std::vector<RefreshCluster>& Items, uint32_t Width, uint32_t Height, const ColumnMatrix& ViewProjection)
{
    const size_t Pixels = static_cast<size_t>(Width) * Height;
    VisibilityRaster Raster;
    Raster.Width  = Width;
    Raster.Height = Height;
    Raster.OpaqueVis.assign(Pixels, 0);
    Raster.OpaqueDepth.assign(Pixels, 0.0f);
    Raster.OpaqueBary.assign(Pixels * 2, 0.0f);
    for (int Layer = 0; Layer < 2; ++Layer)
    {
        Raster.GlassVis[Layer].assign(Pixels, 0);
        Raster.GlassDepth[Layer].assign(Pixels, 0.0f);
        Raster.GlassBary[Layer].assign(Pixels * 2, 0.0f);
    }
    Raster.Items = Items;
    // 📝 Two internal passes: opaque first, then glass depth-tested against the opaque buffer — glass behind opaque geometry is
    //    occluded by it, while glass in front refracts what is behind.
    std::vector<uint32_t> OrderedSlots(Items.size());
    for (uint32_t Slot = 0; Slot < OrderedSlots.size(); ++Slot) OrderedSlots[Slot] = Slot;
    std::stable_sort(OrderedSlots.begin(), OrderedSlots.end(), [&](uint32_t A, uint32_t B) {
        return ((Items[A].Node->ClassMask & 2u) != 0 ? 1 : 0) < ((Items[B].Node->ClassMask & 2u) != 0 ? 1 : 0);
    });
    for (uint32_t Slot : OrderedSlots)
    {
        const RefreshCluster& Item = Items[Slot];
        const ColumnMatrix M = Item.LocalToWorld ? MultiplyMatrices(ViewProjection, *Item.LocalToWorld) : ViewProjection;
        const bool IsGlass = (Item.Node->ClassMask & 2u) != 0;
        for (uint32_t LocalTriangle = 0; LocalTriangle < Item.Node->Triangles.size(); ++LocalTriangle)
        {
            const std::array<WorldCorner, 3> WorldCorners = TriangleWorldCorners(Item, Item.Node->Triangles[LocalTriangle]);
            std::array<std::array<double, 3>, 3> S{};
            bool Valid = true;
            for (uint32_t C = 0; C < 3; ++C)
            {
                const std::array<double, 4> Q = TransformClipPoint(M, WorldCorners[C].Local);
                if (Q[3] <= 1e-4)
                {
                    Valid = false;
                    break;
                }
                S[C] = {(Q[0] / Q[3] * 0.5 + 0.5) * Width, (0.5 - Q[1] / Q[3] * 0.5) * Height, Q[2] / Q[3]};
            }
            if (!Valid) continue;
            const double Area = (S[1][0] - S[0][0]) * (S[2][1] - S[0][1]) - (S[1][1] - S[0][1]) * (S[2][0] - S[0][0]);
            if (std::fabs(Area) < 1e-5) continue;
            const double MinX = JsMax(0.0, std::floor(JsMin(JsMin(S[0][0], S[1][0]), S[2][0])));
            const double MaxX = JsMin(Width - 1.0, std::ceil(JsMax(JsMax(S[0][0], S[1][0]), S[2][0])));
            const double MinY = JsMax(0.0, std::floor(JsMin(JsMin(S[0][1], S[1][1]), S[2][1])));
            const double MaxY = JsMin(Height - 1.0, std::ceil(JsMax(JsMax(S[0][1], S[1][1]), S[2][1])));
            const uint32_t Identity = PackVisibilityIdentity(Slot, LocalTriangle);
            for (double Y = MinY; Y <= MaxY; ++Y)
                for (double X = MinX; X <= MaxX; ++X)
                {
                    const double PX = X + 0.5, PY = Y + 0.5;
                    const double W0 = ((S[1][0] - PX) * (S[2][1] - PY) - (S[1][1] - PY) * (S[2][0] - PX)) / Area;
                    const double W1 = ((S[2][0] - PX) * (S[0][1] - PY) - (S[2][1] - PY) * (S[0][0] - PX)) / Area;
                    const double W2 = 1 - W0 - W1;
                    if (W0 < 0 || W1 < 0 || W2 < 0) continue;
                    const double Z = W0 * S[0][2] + W1 * S[1][2] + W2 * S[2][2];
                    if (Z < 0 || Z > 1) continue;
                    const size_t Pixel = static_cast<size_t>(Y) * Width + static_cast<size_t>(X);
                    if (IsGlass && Z <= Raster.OpaqueDepth[Pixel]) continue;   // occluded by a nearer opaque surface
                    if (!IsGlass)
                    {
                        if (Z <= Raster.OpaqueDepth[Pixel]) continue;
                        Raster.OpaqueDepth[Pixel]        = static_cast<float>(Z);
                        Raster.OpaqueVis[Pixel]          = Identity;
                        Raster.OpaqueBary[Pixel * 2]     = static_cast<float>(W1);
                        Raster.OpaqueBary[Pixel * 2 + 1] = static_cast<float>(W2);
                    }
                    else if (Z > Raster.GlassDepth[0][Pixel])
                    {
                        // New nearest glass surface: demote the old front layer to the back layer.
                        Raster.GlassVis[1][Pixel]          = Raster.GlassVis[0][Pixel];
                        Raster.GlassDepth[1][Pixel]        = Raster.GlassDepth[0][Pixel];
                        Raster.GlassBary[1][Pixel * 2]     = Raster.GlassBary[0][Pixel * 2];
                        Raster.GlassBary[1][Pixel * 2 + 1] = Raster.GlassBary[0][Pixel * 2 + 1];
                        Raster.GlassVis[0][Pixel]          = Identity;
                        Raster.GlassDepth[0][Pixel]        = static_cast<float>(Z);
                        Raster.GlassBary[0][Pixel * 2]     = static_cast<float>(W1);
                        Raster.GlassBary[0][Pixel * 2 + 1] = static_cast<float>(W2);
                    }
                    else if (Z > Raster.GlassDepth[1][Pixel])
                    {
                        Raster.GlassVis[1][Pixel]          = Identity;
                        Raster.GlassDepth[1][Pixel]        = static_cast<float>(Z);
                        Raster.GlassBary[1][Pixel * 2]     = static_cast<float>(W1);
                        Raster.GlassBary[1][Pixel * 2 + 1] = static_cast<float>(W2);
                    }
                }
        }
    }
    return Raster;
}

VisibilityResolution ResolveVisibility(const VisibilityRaster& Raster, const NestingCamera& Camera, std::optional<double> IndexOfRefraction,
                                       const std::optional<ColumnMatrix>& ViewProjectionOverride)
{
    const uint32_t Width = Raster.Width, Height = Raster.Height;
    const size_t   Pixels = static_cast<size_t>(Width) * Height;
    const double   Eta = IndexOfRefraction.value_or(GlassIndexOfRefraction);
    VisibilityResolution Resolution;
    Resolution.Width  = Width;
    Resolution.Height = Height;
    Resolution.OpaqueColor.assign(Pixels * 3, 0.0f);
    Resolution.FinalColor.assign(Pixels * 3, 0.0f);
    const Point3& Eye = Camera.Eye;
    auto Store = [](std::vector<float>& Target, size_t Pixel, const std::array<double, 3>& Colour) {
        for (int C = 0; C < 3; ++C) Target[Pixel * 3 + C] = static_cast<float>(Colour[C]);
    };
    for (size_t Pixel = 0; Pixel < Pixels; ++Pixel)
    {
        const std::optional<SurfacePoint> Surface = ResolveSurfacePoint(Raster, Raster.OpaqueVis, Raster.OpaqueBary, Pixel);
        const std::array<double, 3> Colour = Surface ? ShadeOpaque(*Surface) : SkyColour;
        Store(Resolution.OpaqueColor, Pixel, Colour);
        Store(Resolution.FinalColor, Pixel, Colour);
    }
    // Screen projection for the refraction gather (same convention as the raster).
    const std::optional<ColumnMatrix> ViewProjection = Camera.ViewProjection ? Camera.ViewProjection : ViewProjectionOverride;
    auto ProjectToPixel = [&](const Point3& World) -> std::optional<size_t> {
        if (!ViewProjection) return std::nullopt;
        const std::array<double, 4> Q = TransformClipPoint(*ViewProjection, World);
        if (Q[3] <= 1e-4) return std::nullopt;
        const double X = std::floor((Q[0] / Q[3] * 0.5 + 0.5) * Width);
        const double Y = std::floor((0.5 - Q[1] / Q[3] * 0.5) * Height);
        if (X < 0 || X >= Width || Y < 0 || Y >= Height) return std::nullopt;
        return static_cast<size_t>(Y) * Width + static_cast<size_t>(X);
    };
    for (size_t Pixel = 0; Pixel < Pixels; ++Pixel)
    {
        const std::optional<SurfacePoint> Front = ResolveSurfacePoint(Raster, Raster.GlassVis[0], Raster.GlassBary[0], Pixel);
        if (!Front) continue;
        ++Resolution.GlassPixels;
        const std::optional<SurfacePoint> Back = ResolveSurfacePoint(Raster, Raster.GlassVis[1], Raster.GlassBary[1], Pixel);
        if (Back) ++Resolution.TwoLayerPixels;
        Point3 ViewDir = Subtract(Front->World, Eye);
        double ViewLength = JsHypot(ViewDir[0], ViewDir[1], ViewDir[2]);
        if (ViewLength == 0 || std::isnan(ViewLength)) ViewLength = 1;
        for (double& V : ViewDir) V /= ViewLength;
        Point3 Normal = Front->Normal;
        if (Normal[0] * ViewDir[0] + Normal[1] * ViewDir[1] + Normal[2] * ViewDir[2] > 0) Normal = {-Normal[0], -Normal[1], -Normal[2]};
        const std::optional<Point3> Inside = RefractDirection(ViewDir, Normal, 1 / Eta);
        std::array<double, 3> Gathered = SkyColour;
        double Thickness = Front->Item->Node->Sphere.Radius * 0.5;
        if (Inside)
        {
            Point3 ExitPoint{};
            std::optional<Point3> ExitNormal;
            if (Back)
            {
                Thickness  = Length(Subtract(Back->World, Front->World));
                ExitPoint  = Back->World;
                ExitNormal = Back->Normal;
                if ((*ExitNormal)[0] * (*Inside)[0] + (*ExitNormal)[1] * (*Inside)[1] + (*ExitNormal)[2] * (*Inside)[2] < 0)
                    ExitNormal = Point3{-(*ExitNormal)[0], -(*ExitNormal)[1], -(*ExitNormal)[2]};
            }
            else
                for (int A = 0; A < 3; ++A) ExitPoint[A] = Front->World[A] + (*Inside)[A] * Thickness;
            const std::optional<Point3> Outside =
                ExitNormal ? RefractDirection(*Inside, {-(*ExitNormal)[0], -(*ExitNormal)[1], -(*ExitNormal)[2]}, Eta) : Inside;
            Point3 SamplePoint = ExitPoint;
            if (Outside)
                for (int A = 0; A < 3; ++A) SamplePoint[A] = ExitPoint[A] + (*Outside)[A] * 2.0;
            const std::optional<size_t> SamplePixel = ProjectToPixel(SamplePoint);
            if (SamplePixel)
                Gathered = {Resolution.OpaqueColor[*SamplePixel * 3], Resolution.OpaqueColor[*SamplePixel * 3 + 1], Resolution.OpaqueColor[*SamplePixel * 3 + 2]};
            else if (Outside)
                Gathered = SkyColour;
        }
        // Beer absorption over the thickness (pale green glass tint) and a Schlick Fresnel sky reflection.
        const std::array<double, 3> Absorb = {std::exp(-0.22 * Thickness), std::exp(-0.10 * Thickness), std::exp(-0.16 * Thickness)};
        const double CosView = JsMin(1.0, std::fabs(Normal[0] * ViewDir[0] + Normal[1] * ViewDir[1] + Normal[2] * ViewDir[2]));
        const double F0      = std::pow((Eta - 1) / (Eta + 1), 2);
        const double Fresnel = F0 + (1 - F0) * std::pow(1 - CosView, 5);
        std::array<double, 3> Colour;
        for (int C = 0; C < 3; ++C) Colour[C] = Gathered[C] * Absorb[C] * (1 - Fresnel) + SkyColour[C] * Fresnel;
        Store(Resolution.FinalColor, Pixel, Colour);
    }
    return Resolution;
}
} // namespace Frontier::VirtualizedGeometry
