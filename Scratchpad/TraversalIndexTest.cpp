#include "GeometricRaster/SceneCodec.h"
#include "GeometricRaster/SceneStructure.h"
#include "GeometricRaster/TraversalIndex.h"
#include <cstdio>
#include <cmath>
#include <random>
#include <chrono>
using namespace Frontier;
static bool Brute(const std::vector<TriangleIndex>& T, const float O[3], const float D[3], float& t, uint32_t& p){
    t=1e30f; bool hit=false;
    for(size_t i=0;i<T.size();++i){ const TriangleIndex& a=T[i];
        float v0[3]={a.VertexAlphaX,a.VertexAlphaY,a.VertexAlphaZ}, v1[3]={a.VertexBetaX,a.VertexBetaY,a.VertexBetaZ}, v2[3]={a.VertexGammaX,a.VertexGammaY,a.VertexGammaZ};
        float e1[3]={v1[0]-v0[0],v1[1]-v0[1],v1[2]-v0[2]}, e2[3]={v2[0]-v0[0],v2[1]-v0[1],v2[2]-v0[2]};
        float pv[3]={D[1]*e2[2]-D[2]*e2[1],D[2]*e2[0]-D[0]*e2[2],D[0]*e2[1]-D[1]*e2[0]};
        float det=e1[0]*pv[0]+e1[1]*pv[1]+e1[2]*pv[2]; if(std::fabs(det)<1e-8f) continue; float inv=1/det;
        float tv[3]={O[0]-v0[0],O[1]-v0[1],O[2]-v0[2]}; float u=(tv[0]*pv[0]+tv[1]*pv[1]+tv[2]*pv[2])*inv; if(u<0||u>1) continue;
        float qv[3]={tv[1]*e1[2]-tv[2]*e1[1],tv[2]*e1[0]-tv[0]*e1[2],tv[0]*e1[1]-tv[1]*e1[0]}; float v=(D[0]*qv[0]+D[1]*qv[1]+D[2]*qv[2])*inv; if(v<0||u+v>1) continue;
        float tt=(e2[0]*qv[0]+e2[1]*qv[1]+e2[2]*qv[2])*inv; if(tt>1e-4f&&tt<t){t=tt;p=(uint32_t)i;hit=true;} }
    return hit;
}
int main(int argc,char**argv){
    for(int a=1;a<argc;++a){
        SceneStructure S; std::string Err; SceneDecodeConfiguration C;
        if(!SceneCodec::Decode(argv[a],S,C,&Err)){printf("decode failed %s\n",Err.c_str());return 1;}
        const auto& T=S.QueryFlatTriangles();
        for(int hq=0;hq<2;++hq){
            TraversalIndex X; X.Build(T,hq==1); const auto& M=X.QueryMetrics();
            printf("%-12s %s tris=%u nodes=%u nodeBytes=%u leafBytes=%u SAH=%.2f build=%.1f ms  (%.1f B/tri)\n",S.QueryName().c_str(),hq?"HQ ":"SAH",M.TriangleCount,M.NodeCount,M.NodeByteCount,M.LeafByteCount,M.SahCost,M.BuildMilliseconds,double(M.NodeByteCount+M.LeafByteCount)/M.TriangleCount);
            // random rays from inside bounds; compare to brute force
            Vector3 lo=S.QueryBoundsMinimum(), hi=S.QueryBoundsMaximum(); std::mt19937 g(7); std::uniform_real_distribution<float> U(0,1);
            int N=T.size()>10000?400:4000, agree=0, hits=0; double maxdt=0;
            auto t0=std::chrono::steady_clock::now();
            for(int i=0;i<N;++i){ float O[3]={lo.x+U(g)*(hi.x-lo.x),lo.y+U(g)*(hi.y-lo.y),lo.z+U(g)*(hi.z-lo.z)};
                float z=1-2*U(g), ph=6.2831853f*U(g), r=std::sqrt(std::max(0.f,1-z*z)); float D[3]={r*std::cos(ph),r*std::sin(ph),z};
                float tb,tx; uint32_t pb,px; bool hb=Brute(T,O,D,tb,pb); bool hx=X.TraceClosest(O,D,tx,px);
                if(hb) hits++;
                if(hb==hx && (!hb || std::fabs(tb-tx)<1e-3f*std::max(1.f,tb))) agree++; else if(i<5) printf("   mismatch: brute %d %f #%u  bvh %d %f #%u\n",hb,tb,pb,hx,tx,px);
                maxdt=std::max(maxdt,(double)(hb&&hx?std::fabs(tb-tx):0)); }
            auto t1=std::chrono::steady_clock::now();
            printf("   trace check: %d/%d agree (%d hits), max |dt| = %.2e, %.1f us/ray (tinybvh CPU CWBVH)\n",agree,N,hits,maxdt,std::chrono::duration<double,std::micro>(t1-t0).count()/N);
        }
    }
}
