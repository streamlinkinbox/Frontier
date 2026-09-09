// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
//  PrimitiveGeometryTest.cpp — the tessellated primitives are closed, correctly wound, and free of degenerate faces
// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
//  Winding is the whole point of this file. AppendTriangle derives the geometric normal from the edge cross
//  product, so a reversed winding gives a surface lit from inside and shadowed from outside — which reads as a
//  shading or lighting bug and sends you looking in entirely the wrong place. Writing this test caught exactly
//  that three times: the sphere was uniformly inverted, and so was the ceiling plate.
//
//  A torus is NOT star-shaped about its centre, so "does the normal point away from the centre" is a meaningless
//  question for it — its inner wall legitimately faces inward. The reference is the nearest point on the tube's
//  centre circle instead. Using the centre reported 576 of 1296 faces as wrong on geometry that was correct.
//
//  Build: see Scratchpad/CheckPrimitiveGeometry.sh
// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════

#include "RayTracingSolver.h"
#include "GeometricRaster/CelestialSolver.h"
#include <cstdio>
#include <cmath>
#include <map>
using namespace Frontier;
using namespace Frontier::ProjectZero;

static int Failures = 0;

// A closed surface has every edge shared by exactly two triangles with opposite direction.
static void EdgeAudit(const std::vector<TriangleGeometry>& T, size_t From, size_t To, const char* Name){
    std::map<std::pair<long long,long long>,int> Edges;
    auto Key=[](const Vector3&v)->long long{
        auto q=[](float f){ return (long long)llround(f*100000.0); };
        return q(v.x)*73856093LL ^ q(v.y)*19349663LL ^ q(v.z)*83492791LL; };
    for(size_t i=From;i<To;++i){
        const Vector3 V[3]={T[i].VertexAlpha,T[i].VertexBeta,T[i].VertexGamma};
        for(int e=0;e<3;++e){ long long a=Key(V[e]),b=Key(V[(e+1)%3]);
            Edges[{a,b}]++; }
    }
    int Unmatched=0;
    for(auto&[E,N]:Edges){ auto it=Edges.find({E.second,E.first});
        if(it==Edges.end()||it->second!=N) ++Unmatched; }
    printf("  %-22s %5zu tris   %s\n",Name,To-From, Unmatched==0?"closed (every edge paired opposite)":"OPEN/NON-MANIFOLD");
    if(Unmatched!=0) ++Failures;
}
static void NormalAudit(const std::vector<TriangleGeometry>& T,size_t From,size_t To,const Vector3& C,const char* Name){
    int Inward=0; double MinDot=1e9;
    for(size_t i=From;i<To;++i){
        Vector3 Mid{ (T[i].VertexAlpha.x+T[i].VertexBeta.x+T[i].VertexGamma.x)/3.0f,
                     (T[i].VertexAlpha.y+T[i].VertexBeta.y+T[i].VertexGamma.y)/3.0f,
                     (T[i].VertexAlpha.z+T[i].VertexBeta.z+T[i].VertexGamma.z)/3.0f };
        Vector3 Out{ Mid.x-C.x, Mid.y-C.y, Mid.z-C.z };
        double L=std::sqrt(Out.x*Out.x+Out.y*Out.y+Out.z*Out.z); if(L<1e-6) continue;
        double D=(T[i].SurfaceNormal.x*Out.x+T[i].SurfaceNormal.y*Out.y+T[i].SurfaceNormal.z*Out.z)/L;
        if(D<0) ++Inward;
        MinDot=std::min(MinDot,D);
    }
    printf("  %-22s %d inward-facing of %zu   min dot %+.3f  %s\n",Name,Inward,To-From,MinDot,
           Inward==0?"all outward":"*** WINDING BUG ***");
    if(Inward!=0) ++Failures;
}
static void Expect(bool C,const char* W){ std::printf("  %-62s %s\n",W,C?"PASS":"FAIL"); if(!C) ++Failures; }

int main(){
    RayTracingSolver S;
    const auto& T=S.QueryTriangles();
    printf("total triangles: %zu   materials: %zu\n\n",T.size(),S.QueryMaterials().size());
    // Count per material to locate each primitive's range
    std::map<uint32_t,std::pair<size_t,size_t>> Range;
    for(size_t i=0;i<T.size();++i){ auto m=T[i].MaterialIndex;
        if(!Range.count(m)) Range[m]={i,i+1}; else Range[m].second=i+1; }
    for(auto&[M,R]:Range) printf("  material %u: tris [%zu,%zu)  count %zu\n",M,R.first,R.second,R.second-R.first);
    printf("\nclosed-surface audit:\n");
    EdgeAudit(T,Range[6].first,Range[6].second,"sphere");
    EdgeAudit(T,Range[7].first,Range[7].second,"cone");
    EdgeAudit(T,Range[8].first,Range[8].second,"torus");
    printf("\noutward-normal audit:\n");
    NormalAudit(T,Range[6].first,Range[6].second,Vector3{-1.15f,1.05f,0.45f},"sphere");
    // A torus is not star-shaped about its centre: the inner wall legitimately faces inward. The correct
    // outward reference is the nearest point on the tube's centre circle.
    {
        const Vector3 C{0.0f,1.55f,0.32f}; const double Major=0.42;
        int Inward=0; double MinDot=1e9;
        for(size_t i=Range[8].first;i<Range[8].second;++i){
            Vector3 Mid{ (T[i].VertexAlpha.x+T[i].VertexBeta.x+T[i].VertexGamma.x)/3.0f,
                         (T[i].VertexAlpha.y+T[i].VertexBeta.y+T[i].VertexGamma.y)/3.0f,
                         (T[i].VertexAlpha.z+T[i].VertexBeta.z+T[i].VertexGamma.z)/3.0f };
            double dx=Mid.x-C.x, dy=Mid.y-C.y; double R=std::sqrt(dx*dx+dy*dy);
            if(R<1e-9) continue;
            Vector3 Ring{ (float)(C.x+dx/R*Major), (float)(C.y+dy/R*Major), C.z };
            Vector3 Out{ Mid.x-Ring.x, Mid.y-Ring.y, Mid.z-Ring.z };
            double L=std::sqrt(Out.x*Out.x+Out.y*Out.y+Out.z*Out.z); if(L<1e-9) continue;
            double D=(T[i].SurfaceNormal.x*Out.x+T[i].SurfaceNormal.y*Out.y+T[i].SurfaceNormal.z*Out.z)/L;
            if(D<0) ++Inward;
        MinDot=std::min(MinDot,D);
        }
        printf("  %-22s %d inward-facing of %zu   min dot %+.3f  %s\n","torus (tube axis)",Inward,
               Range[8].second-Range[8].first,MinDot,Inward==0?"all outward":"*** WINDING BUG ***");
        if(Inward!=0) ++Failures;
    }
    // degenerate triangles
    int Degenerate=0; for(const auto&t:T){
        Vector3 e1{t.VertexBeta.x-t.VertexAlpha.x,t.VertexBeta.y-t.VertexAlpha.y,t.VertexBeta.z-t.VertexAlpha.z};
        Vector3 e2{t.VertexGamma.x-t.VertexAlpha.x,t.VertexGamma.y-t.VertexAlpha.y,t.VertexGamma.z-t.VertexAlpha.z};
        Vector3 c{e1.y*e2.z-e1.z*e2.y,e1.z*e2.x-e1.x*e2.z,e1.x*e2.y-e1.y*e2.x};
        if(std::sqrt(c.x*c.x+c.y*c.y+c.z*c.z)*0.5f < 1e-9f) ++Degenerate; }
    printf("\ndegenerate (zero-area) triangles: %d\n",Degenerate);

    // ── Ceiling oculus ───────────────────────────────────────────────────────────────────────────────────────
    // The plate is not a closed solid, so the edge audit does not apply; what matters is that it faces the room
    // (−Z), that the hole is genuinely open, and that the rest of the ceiling is still solid.
    printf("\nceiling oculus\n");
    {
        int Ceiling = 0, WrongWay = 0;
        double MinRim = 1e9, MaxRim = -1e9;
        for (const auto& t : T)
        {
            if (t.MaterialIndex != 0u) continue;
            if (std::fabs(t.VertexAlpha.z - 3.0f) > 1e-4f) continue;
            ++Ceiling;
            if (t.SurfaceNormal.z > -0.99f) ++WrongWay;
            for (const Vector3* v : { &t.VertexAlpha, &t.VertexBeta, &t.VertexGamma })
            {
                const double R = std::sqrt((v->x) * (v->x) + (v->y - 2.10) * (v->y - 2.10));
                if (R < 0.9) { MinRim = std::min(MinRim, R); MaxRim = std::max(MaxRim, R); }
            }
        }

        const auto Covers = [&](float px, float py)
        {
            int N = 0;
            for (const auto& t : T)
            {
                if (t.MaterialIndex != 0u) continue;
                if (std::fabs(t.VertexAlpha.z - 3.0f) > 1e-4f) continue;
                const auto Side = [&](float ax, float ay, float bx, float by)
                                  { return (px - bx) * (ay - by) - (ax - bx) * (py - by); };
                const float D1 = Side(t.VertexAlpha.x, t.VertexAlpha.y, t.VertexBeta.x,  t.VertexBeta.y);
                const float D2 = Side(t.VertexBeta.x,  t.VertexBeta.y,  t.VertexGamma.x, t.VertexGamma.y);
                const float D3 = Side(t.VertexGamma.x, t.VertexGamma.y, t.VertexAlpha.x, t.VertexAlpha.y);
                const bool Neg = (D1 < 0) || (D2 < 0) || (D3 < 0);
                const bool Pos = (D1 > 0) || (D2 > 0) || (D3 > 0);
                if (!(Neg && Pos)) ++N;
            }
            return N;
        };

        printf("  %d ceiling tris, %d facing the wrong way, rim %.4f .. %.4f\n", Ceiling, WrongWay, MinRim, MaxRim);
        Expect(Ceiling > 0,        "the ceiling exists");
        Expect(WrongWay == 0,      "every ceiling triangle faces the room (-Z), not the sky");
        Expect(Covers(0.0f, 2.10f) == 0,  "the oculus is genuinely open — no triangle covers its centre");
        Expect(Covers(0.0f, 3.90f) >  0,  "the ceiling just outside the rim is solid");
        Expect(Covers(-1.8f, 0.3f) >  0,  "and so is the far corner, so the ring reaches the walls");
        Expect(std::fabs(MinRim - 0.75) < 1e-3 && std::fabs(MaxRim - 0.75) < 1e-3,
               "every rim vertex sits exactly on the 0.75 m radius");

        // ⚠️ The luminaire hangs 5 mm below the ceiling plane, so any part of it inside the opening would be
        //    seen through the hole as a bright slab in front of the sky — the lamp blocking the very thing the
        //    aperture exists to show, and to be compared against.
        double LampNearest = 1e9;
        for (const auto& t : T)
        {
            if (t.MaterialIndex != 3u) continue;
            for (const Vector3* v : { &t.VertexAlpha, &t.VertexBeta, &t.VertexGamma })
                LampNearest = std::min(LampNearest,
                                       std::sqrt((v->x) * (v->x) + (v->y - 2.10) * (v->y - 2.10)));
        }
        printf("  the luminaire's nearest corner is %.2f m from the aperture centre\n", LampNearest);
        Expect(LampNearest > 0.75, "the luminaire stays out of the opening, so the sky is what shows through it");
    }

    // ── Does sunlight actually come through? ─────────────────────────────────────────────────────────────────
    // 🔴 The point of the aperture, and the thing that was wrong about it. A roof opening only reads as sunlight
    // if a shaft LANDS somewhere the camera can see. At any northern latitude the midday sun stands to the
    // south, so the shaft runs north, AWAY from the camera — which means the hole belongs south of where the
    // light should fall. It was at Y = 3.05, only 0.95 m from the back wall, and the light landed at Y ≈ 4.3:
    // behind the wall, every hour of every day.
    printf("\nsunlight through the aperture\n");
    {
        constexpr double TopZ = 3.0;
        constexpr double MinX = -2.0, MaxX = 2.0, MinY = 0.0, MaxY = 4.0;

        // ⚠️ MEASURED from the ceiling that was built, not copied from the solver's constant. A second copy of
        //    the aperture's position would keep agreeing with itself after the real one moved, and this test
        //    would then be asserting where the light falls through a hole that is somewhere else.
        //
        //    The rim vertices are the ceiling vertices that are not on the room's boundary rectangle; their
        //    centroid is the centre and their mean distance from it is the radius.
        double SumX = 0.0, SumY = 0.0; int RimCount = 0;
        for (const auto& t : T)
        {
            if (t.MaterialIndex != 0u) continue;
            if (std::fabs(t.VertexAlpha.z - 3.0f) > 1e-4f) continue;
            for (const Vector3* v : { &t.VertexAlpha, &t.VertexBeta, &t.VertexGamma })
            {
                const bool OnBoundary = std::fabs(v->x - (-2.0f)) < 1e-3f || std::fabs(v->x - 2.0f) < 1e-3f
                                     || std::fabs(v->y -   0.0f) < 1e-3f || std::fabs(v->y - 4.0f) < 1e-3f;
                if (OnBoundary) continue;
                SumX += v->x; SumY += v->y; ++RimCount;
            }
        }
        const double HoleX = RimCount ? SumX / RimCount : 0.0;
        const double HoleY = RimCount ? SumY / RimCount : 0.0;
        double SumR = 0.0;
        for (const auto& t : T)
        {
            if (t.MaterialIndex != 0u) continue;
            if (std::fabs(t.VertexAlpha.z - 3.0f) > 1e-4f) continue;
            for (const Vector3* v : { &t.VertexAlpha, &t.VertexBeta, &t.VertexGamma })
            {
                const bool OnBoundary = std::fabs(v->x - (-2.0f)) < 1e-3f || std::fabs(v->x - 2.0f) < 1e-3f
                                     || std::fabs(v->y -   0.0f) < 1e-3f || std::fabs(v->y - 4.0f) < 1e-3f;
                if (OnBoundary) continue;
                SumR += std::sqrt((v->x - HoleX) * (v->x - HoleX) + (v->y - HoleY) * (v->y - HoleY));
            }
        }
        const double HoleR = RimCount ? SumR / RimCount : 0.0;
        printf("  measured from the built ceiling: centre (%.2f, %.2f), radius %.2f m, %d rim vertices\n",
               HoleX, HoleY, HoleR, RimCount);
        Expect(RimCount > 0, "the ceiling actually has a rim — there is a hole in it to measure");

        // Where the disc of light falls: the aperture translated by the sun's slope over the room's height.
        const auto Shaft = [&](const CelestialSolver& Sky, double Hour, double& Cx, double& Cy)
        {
            const HorizonDirection S = Sky.QuerySunDirection(Hour * 3600.0);
            if (S.Zenith <= 0.05) return false;
            const double T2 = TopZ / S.Zenith;
            Cx = HoleX - T2 * S.East;
            Cy = HoleY - T2 * S.North;
            return true;
        };

        for (double LatitudeDegrees : { 45.0, 0.0 })
        {
            CelestialSolver Sky;
            CelestialConfiguration Cfg{};
            Cfg.LatitudeRadians = LatitudeDegrees * 3.14159265358979 / 180.0;
            Sky.AssignConfiguration(Cfg);

            // Noon first, because that is the setting the panel opens on: if a user sets the clock to 12 and
            //    looks up, there has to be a shaft on the floor.
            double Cx = 0.0, Cy = 0.0;
            const bool Daylight = Shaft(Sky, 12.0, Cx, Cy);
            printf("  latitude %2.0f: the noon shaft lands at (%.2f, %.2f)\n", LatitudeDegrees, Cx, Cy);
            Expect(Daylight, "the sun is up at 12:00 — the clock's noon is the sun's noon");
            Expect(Cx > MinX + HoleR && Cx < MaxX - HoleR, "and the noon shaft is between the side walls");
            Expect(Cy > MinY && Cy < MaxY,                 "and lands on the floor, not beyond the back wall");

            // And it stays there for a usable part of the day rather than for one instant.
            int Lit = 0;
            for (int Quarter = 0; Quarter < 96; ++Quarter)
            {
                const double Hour = Quarter / 4.0;
                if (!Shaft(Sky, Hour, Cx, Cy)) continue;
                if (Cx > MinX && Cx < MaxX && Cy > MinY && Cy < MaxY) ++Lit;
            }
            printf("  latitude %2.0f: on the floor for %.1f hours of the day\n", LatitudeDegrees, Lit / 4.0);
            Expect(Lit >= 12, "the shaft is on the floor for at least three hours, not a passing instant");
        }

        // The old placement, so the regression is recognisable. Same sun, same room, hole at 3.05.
        CelestialSolver Sky;
        CelestialConfiguration Cfg{};
        Cfg.LatitudeRadians = 45.0 * 3.14159265358979 / 180.0;
        Sky.AssignConfiguration(Cfg);
        const HorizonDirection S = Sky.QuerySunDirection(12.0 * 3600.0);
        const double OldY = 3.05 - (TopZ / S.Zenith) * S.North;
        printf("  from the old 3.05 the noon shaft would land at Y = %.2f, past the %.1f m back wall\n",
               OldY, MaxY);
        Expect(OldY > MaxY, "the old placement really did throw the light behind the wall — this is the fix");
    }

    // ── Outdoor scene ────────────────────────────────────────────────────────────────────────────────────────
    // A1-A7 built a sun, sky, sunset, moon, stars, skylight and adaptive exposure whose only window was a 13°
    // oculus at 12.9° elevation. This scene exists to make that work visible, so what it must prove is exactly
    // that: most of the frame is sky, and the sun is the only light.
    printf("\noutdoor scene\n");
    {
        RayTracingSolver Open;
        Open.ConstructOutdoorScene();

        int Emissive = 0;
        for (const auto& M : Open.QueryMaterials())
            if (M.EmissiveRadiance.x > 0.0f || M.EmissiveRadiance.y > 0.0f || M.EmissiveRadiance.z > 0.0f) ++Emissive;

        // Cast the default camera's frustum and count how much of it misses geometry.
        const Vector3 Eye{ 0.0f, -6.0f, 1.70f };
        const float Pitch = 8.0f * 3.14159265f / 180.0f;
        const float TanHalf = std::tan(55.0f * 3.14159265f / 180.0f * 0.5f);
        int Miss = 0, Total = 0, GroundHits = 0;
        for (int Y = 0; Y < 90; ++Y)
            for (int X = 0; X < 160; ++X)
            {
                const float Nx = ((X + 0.5f) / 160.0f * 2.0f - 1.0f) * TanHalf * (16.0f / 9.0f);
                const float Ny = (1.0f - (Y + 0.5f) / 90.0f * 2.0f) * TanHalf;
                Vector3 D{ Nx, std::cos(Pitch) - Ny * std::sin(Pitch), std::sin(Pitch) + Ny * std::cos(Pitch) };
                const float L = std::sqrt(D.x * D.x + D.y * D.y + D.z * D.z);
                D.x /= L; D.y /= L; D.z /= L;

                RayStructure R{};
                R.SpatialOrigin = Eye; R.RayDirection = D;
                R.MinimumDistance = 0.001f; R.MaximumDistance = 1e30f;
                const HitIntersection Hit = Open.EvaluateIntersection(R);
                ++Total;
                if (!Hit.ValidCondition) ++Miss;
                else if (Hit.MaterialIndex == 0u) ++GroundHits;
            }

        const double SkyFraction = 100.0 * Miss / Total;
        printf("  %zu triangles, %d emissive materials, %.1f%% of the frame is sky, %.1f%% is ground\n",
               Open.QueryTriangles().size(), Emissive, SkyFraction, 100.0 * GroundHits / Total);

        Expect(Emissive == 0,
               "no emissive triangle — the sun is the only light, which is a live test of the A4 path");
        Expect(SkyFraction > 40.0,
               "most of the frame is sky, so a sunset can actually be judged");
        Expect(GroundHits > 0,
               "and the ground is still in shot, so shadows have somewhere to land");
    }

    printf("\nassertions\n");
    Expect(Degenerate == 0, "no zero-area triangles reach the BVH");
    Expect(T.size() > 2000 && T.size() < 4000, "triangle budget is in the intended range for a 1650 SUPER");
    Expect(S.QueryMaterials().size() == 9u, "nine materials: room, walls, luminaire, two boxes, three primitives");
    printf("\n>>> %s (%d failure%s)\n", Failures==0?"ALL PASS":"FAILURES", Failures, Failures==1?"":"s");
    return Failures==0?0:1;
}
