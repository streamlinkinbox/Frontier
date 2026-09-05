//============================================================================================================================================
// 📦 ParametricSketcher/Presentation/ScenePresentation.cpp — Kernel geometry → vertex streams
//============================================================================================================================================

#include "ScenePresentation.h"

namespace Frontier
{

SegmentStream ScenePresentation::CurveSegments(const NurbsCurve& Curve, double ChordTolerance) noexcept
{
    std::vector<Vec3> Points;
    Curve.Tessellate(Points, nullptr, ChordTolerance);
    SegmentStream S;
    S.AppendPolyline(Points, false);
    return S;
}

SurfaceStream ScenePresentation::SurfaceTriangles(const NurbsSurface& Surface, double ChordTolerance) noexcept
{
    NurbsSurface::Tessellation T = Surface.Tessellate(ChordTolerance);
    SurfaceStream S;
    S.Positions.reserve(T.Positions.size() * 3);
    S.Normals.reserve(T.Normals.size() * 3);
    S.Parameters.reserve(T.Parameters.size() * 2);
    for (size_t I = 0; I < T.Positions.size(); ++I)
    {
        const Vec3& P = T.Positions[I]; const Vec3& N = T.Normals[I]; const Vec2& Q = T.Parameters[I];
        S.Positions.push_back(float(P.X)); S.Positions.push_back(float(P.Y)); S.Positions.push_back(float(P.Z));
        S.Normals.push_back(float(N.X));   S.Normals.push_back(float(N.Y));   S.Normals.push_back(float(N.Z));
        S.Parameters.push_back(float(Q.X)); S.Parameters.push_back(float(Q.Y));
    }
    S.Triangles = std::move(T.Triangles);
    return S;
}

SegmentStream ScenePresentation::SurfaceIsoCurves(const NurbsSurface& Surface, int CountU, int CountV, double ChordTolerance) noexcept
{
    SegmentStream S;
    std::vector<Vec3> Points;
    for (int I = 0; I <= CountU; ++I)
    {
        double U = ScalarCriteria::Lerp(Surface.DomainStartU(), Surface.DomainEndU(), static_cast<double>(I) / CountU);
        Surface.IsoCurveU(U).Tessellate(Points, nullptr, ChordTolerance);
        S.AppendPolyline(Points);
    }
    for (int J = 0; J <= CountV; ++J)
    {
        double V = ScalarCriteria::Lerp(Surface.DomainStartV(), Surface.DomainEndV(), static_cast<double>(J) / CountV);
        Surface.IsoCurveV(V).Tessellate(Points, nullptr, ChordTolerance);
        S.AppendPolyline(Points);
    }
    return S;
}

SegmentStream ScenePresentation::ControlPolygon(const NurbsCurve& Curve) noexcept
{
    SegmentStream S;
    for (int I = 0; I + 1 < Curve.PoleCount(); ++I) S.Append(Curve.Poles[I].Divide(), Curve.Poles[I + 1].Divide());
    return S;
}

PointStream ScenePresentation::ControlPoints(const NurbsCurve& Curve, PointGlyph Glyph) noexcept
{
    PointStream P;
    for (const Vec4& Pole : Curve.Poles) P.Append(Pole.Divide(), Glyph);
    return P;
}

SegmentStream ScenePresentation::ControlNet(const NurbsSurface& Surface) noexcept
{
    SegmentStream S;
    for (int I = 0; I < Surface.CountU; ++I)
        for (int J = 0; J < Surface.CountV; ++J)
        {
            if (I + 1 < Surface.CountU) S.Append(Surface.Pole(I, J).Divide(), Surface.Pole(I + 1, J).Divide());
            if (J + 1 < Surface.CountV) S.Append(Surface.Pole(I, J).Divide(), Surface.Pole(I, J + 1).Divide());
        }
    return S;
}

PointStream ScenePresentation::ControlPoints(const NurbsSurface& Surface, PointGlyph Glyph) noexcept
{
    PointStream P;
    for (const Vec4& Pole : Surface.Poles) P.Append(Pole.Divide(), Glyph);
    return P;
}

void ScenePresentation::DrawTriad(RasterExchange& Raster, double Length) noexcept
{
    const float Colours[3][3] = { { 0.90f, 0.25f, 0.25f }, { 0.30f, 0.80f, 0.30f }, { 0.30f, 0.50f, 0.95f } };
    const Vec3 Axes[3] = { Vec3::UnitX(), Vec3::UnitY(), Vec3::UnitZ() };
    for (int A = 0; A < 3; ++A)
    {
        SegmentStream S; S.Append({}, Axes[A] * Length);
        DrawRecord D = Tinted(Colours[A][0], Colours[A][1], Colours[A][2]);
        D.LineWidth = 2.5f;
        Raster.DrawSegments(S, D);
        PointStream P; P.Append(Axes[A] * Length, PointGlyph::Disc);
        D.PointSize = 8.0f;
        Raster.DrawPoints(P, D);
    }
}

} // namespace Frontier
