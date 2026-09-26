#include "ConstructWorld.h"
#include <cmath>
#include <algorithm>
#include <cstdio>

namespace Frontier {
namespace {
constexpr float Pi = 3.14159265358979323846f;
struct Point { Vector3 P, N; };
void Triangle(GeometryStructure& Mesh, Point A, Point B, Point C) {
    auto Cross = OrientationClassifier::CrossProduct(B.P-A.P,C.P-A.P);
    if (Cross.LengthSquared() < 1e-14f) return; // latitude poles, not degenerate facets
    if (OrientationClassifier::DotProduct(Cross,A.N+B.N+C.N)<0) std::swap(B,C);
    VertexRecord V[3]{}; unsigned J=0;
    for (auto P : {A,B,C}) {
        auto N=P.N.Normalized();
        auto T=OrientationClassifier::CrossProduct(std::abs(N.z)<.9f?Vector3{0,0,1}:Vector3{0,1,0},N).Normalized();
        V[J].SpatialLocation=P.P; V[J].NormalDirection=N;
        V[J].TangentDirection=Vector4{T.x,T.y,T.z,1};
        V[J].TextureCoordinateU=J==1?1.f:0.f; V[J].TextureCoordinateV=J==2?1.f:0.f; ++J;
    }
    const auto Start=static_cast<uint32_t>(Mesh.QueryVertices().size());
    uint32_t Indices[]={Start,Start+1,Start+2}; Mesh.AppendVertices(V,3); Mesh.AppendIndices(Indices,3);
}
void Quad(GeometryStructure& M, Point A, Point B, Point C, Point D) {
    Triangle(M,A,B,C); Triangle(M,A,C,D);
}
void Build(GeometryStructure& M, ConstructKind K) {
    if (K==ConstructKind::Cube) {
        for(int Axis=0;Axis<3;++Axis) for(float Sign:{-1.f,1.f}) {
            Point V[4]; const int U=(Axis+1)%3,W=(Axis+2)%3;
            for(int I=0;I<4;++I) {
                float P[3]={},N[3]={}; P[Axis]=Sign*.5f; N[Axis]=Sign;
                P[U]=(I==1||I==2)?.5f:-.5f; P[W]=(I>=2)?.5f:-.5f;
                V[I]={{P[0],P[1],P[2]},{N[0],N[1],N[2]}};
            }
            Quad(M,V[0],V[1],V[2],V[3]);
        }
    } else if(K==ConstructKind::Plane||K==ConstructKind::Area) {
        Vector3 N{0,0,K==ConstructKind::Area?-1.f:1.f};
        Quad(M,{{-.5f,-.5f,0},N},{{.5f,-.5f,0},N},{{.5f,.5f,0},N},{{-.5f,.5f,0},N});
    } else if(K==ConstructKind::Sphere||K==ConstructKind::Torus) {
        const int Rings=K==ConstructKind::Sphere?16:12;
        auto Sample=[&](int I,int J)->Point {
            float U=2*Pi*float(I)/32,V=(K==ConstructKind::Sphere?Pi:2*Pi)*float(J)/Rings;
            if(K==ConstructKind::Sphere) { Vector3 N{std::sin(V)*std::cos(U),std::sin(V)*std::sin(U),std::cos(V)}; return {N*.5f,N}; }
            Vector3 N{std::cos(V)*std::cos(U),std::cos(V)*std::sin(U),std::sin(V)};
            return {{(.35f+.15f*std::cos(V))*std::cos(U),(.35f+.15f*std::cos(V))*std::sin(U),.15f*std::sin(V)},N};
        };
        // Indexed parametric grid: share interior edges while retaining the UV seam.
        // Triangle soup with per-triangle UVs locks every edge against safe patch reduction.
        std::vector<VertexRecord> Vertices;
        const uint32_t Offset=static_cast<uint32_t>(M.QueryVertices().size());
        for(int I=0;I<=32;++I) for(int J=0;J<=Rings;++J) {
            auto P=Sample(I==32?0:I,K==ConstructKind::Torus&&J==Rings?0:J);
            if(K==ConstructKind::Sphere&&(J==0||J==Rings)) P.N={0,0,J==0?1.f:-1.f},P.P=P.N*.5f;
            float U=2*Pi*float(I==32?0:I)/32;
            VertexRecord V{}; V.SpatialLocation=P.P;V.NormalDirection=P.N.Normalized();
            V.TangentDirection={-std::sin(U),std::cos(U),0,1};
            V.TextureCoordinateU=float(I)/32;V.TextureCoordinateV=float(J)/Rings;
            Vertices.push_back(V);
        }
        M.AppendVertices(Vertices.data(),Vertices.size());
        auto Emit=[&](uint32_t A,uint32_t B,uint32_t C) {
            auto Cross=OrientationClassifier::CrossProduct(Vertices[B].SpatialLocation-Vertices[A].SpatialLocation,Vertices[C].SpatialLocation-Vertices[A].SpatialLocation);
            if(Cross.LengthSquared()<1e-14f)return;
            if(OrientationClassifier::DotProduct(Cross,Vertices[A].NormalDirection+Vertices[B].NormalDirection+Vertices[C].NormalDirection)<0)std::swap(B,C);
            uint32_t Ix[]={Offset+A,Offset+B,Offset+C};M.AppendIndices(Ix,3);
        };
        for(uint32_t I=0;I<32;++I)for(uint32_t J=0;J<uint32_t(Rings);++J) {
            uint32_t A=I*(Rings+1)+J,B=(I+1)*(Rings+1)+J,C=B+1,D=A+1;
            Emit(A,B,C);Emit(A,C,D);
        }
    } else {
        for(int I=0;I<32;++I) {
            float A=2*Pi*I/32,B=2*Pi*(I+1)/32;
            Vector3 PA{.5f*std::cos(A),.5f*std::sin(A),-.5f},PB{.5f*std::cos(B),.5f*std::sin(B),-.5f};
            float Slope=K==ConstructKind::Cone?.5f:0.f;
            Vector3 NA{std::cos(A),std::sin(A),Slope},NB{std::cos(B),std::sin(B),Slope};
            Triangle(M,{{0,0,-.5f},{0,0,-1}},{PA,{0,0,-1}},{PB,{0,0,-1}});
            if(K==ConstructKind::Cone) Triangle(M,{PA,NA},{PB,NB},{{0,0,.5f},(NA+NB).Normalized()});
            else {
                Vector3 TA{PA.x,PA.y,.5f},TB{PB.x,PB.y,.5f};
                Quad(M,{PA,NA},{PB,NB},{TB,NB},{TA,NA});
                Triangle(M,{{0,0,.5f},{0,0,1}},{TA,{0,0,1}},{TB,{0,0,1}});
            }
        }
    }
}
}
const char* ConstructName(ConstructKind K) {
    static const char* Names[]={"Cube","Sphere","Cylinder","Cone","Plane","Torus","Area emitter","Camera","Empty entity"};
    return unsigned(K)<unsigned(ConstructKind::Count)?Names[unsigned(K)]:"Invalid";
}
ConstructResult ConstructEntity(SceneStructure& World,const ConstructRequest& R,uint32_t SlabLimit) {
    if(unsigned(R.Kind)>=unsigned(ConstructKind::Count)) return {kPlacementNone,"Unsupported entity"};
    if(!std::isfinite(R.Size)||R.Size<.001f||R.Size>10000) return {kPlacementNone,"Size must be between 0.001 and 10000 metres"};
    for(float P:R.Position) if(!std::isfinite(P)||std::abs(P)>100000) return {kPlacementNone,"Position outside authoring range"};
    if(World.QueryPlacements().size()>=4096) return {kPlacementNone,"Authoring entity limit reached"};
    if(R.Name.size()>64||std::any_of(R.Name.begin(),R.Name.end(),[](unsigned char C){return C<32||C==127;})) return {kPlacementNone,"Invalid name"};
    // Validate before changing any scene table. Placement indices are stable append-only IDs.
    std::string Base=R.Name.empty()?ConstructName(R.Kind):R.Name,Name=Base;
    auto Exists=[&](const std::string& N){for(const auto& P:World.QueryPlacements()) if(P.Name==N)return true;return false;};
    for(unsigned I=2;Exists(Name);++I) Name=Base+" "+std::to_string(I);
    Matrix4x4 T; for(unsigned I=0;I<3;++I) {T.Columns[I][I]=R.Size;T.Columns[3][I]=R.Position[I];}
    GeometryStructure Mesh;
    const bool IsMesh=R.Kind<ConstructKind::Camera;
    if(IsMesh) Build(Mesh,R.Kind);
    uint32_t P=World.RegisterPlacement(Name,kPlacementNone,T,T);
    if(IsMesh) {
        MaterialDescriptor Material; Material.Name=Name+" surface"; Material.Slabs.emplace_back();
        const bool Emissive=R.Kind==ConstructKind::Area;
        if(Emissive) Material.Slabs[0].EmissionLuminance=1000;
        auto Slot=World.RegisterMaterial(Material);
        auto First=World.RegisterInstance(Mesh,T,Slot,Emissive?InstanceFlagEmissive:0u);
        World.AttachInstances(P,First,static_cast<uint32_t>(World.QueryInstances().size())-First);
    } else if(R.Kind==ConstructKind::Camera) {CameraRecord Camera;Camera.Name=Name;World.RegisterCamera(Camera,P);}
    World.Finalise(SlabLimit);
    return {P,{}};
}
}
