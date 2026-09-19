// Scratch probe: how tightly does the tessellated volume follow the Phase 32z closed form? (not part of the build)
#include "Kernel/BlendSolver.h"
#include <cstdio>
#include <cmath>
using namespace Frontier;
static Deliver<BrepBody> TaperedBoss(Vec3 Base, Vec3 Axis, double Ro, double Hs, double Rf, double Rt, double H)
{
    Axis = Axis.Normalised(); Workplane Axes = Workplane::FromNormal(Base, Axis); Vec3 C = Base + Axis * Hs;
    auto Outer = NurbsSurface::Cylinder(Base, Axis, Ro, Hs);
    auto Line = NurbsCurve::Line(C + Axes.AxisX * Ro, C + Axes.AxisX * Rf);
    auto Sh = NurbsSurface::Revolution(Line.Payload, Base, Axis, ScalarCriteria::TwoPi);
    auto Boss = NurbsSurface::Cone(C, Axis, Rf, Rt, H);
    return BrepBody::Sew({ Outer.Payload, Sh.Payload, Boss.Payload });
}
static double Wedge(double Rf, double a, double r)
{
    double s = std::sin(a), c = std::cos(a), zt = r * (1 - s), rc = Rf + r * (1 - s) / c, rt = Rf - zt * std::tan(a);
    double Q[4][2] = { { Rf, 0 }, { rc, 0 }, { rc, r }, { rt, zt } }; double m = 0;
    for (int i = 0; i < 4; ++i) { auto P = Q[i], N = Q[(i + 1) % 4]; m += (P[0] + N[0]) * (P[0] * N[1] - N[0] * P[1]); }
    m = std::fabs(m) / 6; double t0 = ScalarCriteria::Pi + a, t1 = 1.5 * ScalarCriteria::Pi;
    double sec = rc * r * r * (t1 - t0) / 2 + r * r * r * (std::sin(t1) - std::sin(t0)) / 3;
    return ScalarCriteria::TwoPi * (m - sec);
}
int main()
{
    struct F { double Ro, Hs, Rf, Rt, H, r; } Fx[] = { {10,8,5,3.5,7,2}, {12,5,4,5.5,6,1.5}, {8,6,3,2,5,1.25}, {9,5,4,2.5,6,1.5}, {10,4,6,1,5,1}, {10,8,5,5,7,2}, {10,8,5,3.5,7,0.75} };
    for (const F& f : Fx)
    {
        auto S = TaperedBoss({0,0,0},{0,0,1}, f.Ro, f.Hs, f.Rf, f.Rt, f.H);
        int root = -1; for (size_t e = 0; e < S.Payload.Edges.size(); ++e) { const auto& c = S.Payload.Edges[e].Curve; if (c.Closed() && std::fabs(c.Sample(c.DomainStart()).Distance({0,0,f.Hs}) - f.Rf) < 1e-9) root = (int)e; }
        auto R = BlendSolver::FilletEdge(S.Payload, root, f.r);
        double a = std::atan2(f.Rf - f.Rt, f.H), src = ScalarCriteria::Pi * f.Ro * f.Ro * f.Hs + ScalarCriteria::Pi * f.H * (f.Rf * f.Rf + f.Rf * f.Rt + f.Rt * f.Rt) / 3;
        double w = Wedge(f.Rf, a, f.r), vs = S.Payload.Validate().Volume, vr = R ? R.Payload.Validate().Volume : 0;
        std::printf("Ro=%4.1f Rf=%3.1f Rt=%3.1f H=%3.1f r=%4.2f | ok=%d | src rel %.2e | total rel %.2e | (vr-vs-w)/w %.2e | (vr-vs)/w=%.6f\n",
                    f.Ro, f.Rf, f.Rt, f.H, f.r, (bool)R, (vs - src) / src, (vr - src - w) / (src + w), (vr - vs - w) / w, (vr - vs) / w);
    }
}
