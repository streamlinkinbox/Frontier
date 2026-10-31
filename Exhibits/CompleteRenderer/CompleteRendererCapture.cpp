#include "Engine/Rendering/ReferenceRenderer.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace Frontier;
static TriangleMesh Box(double hx, double hy, double hz)
{
    TriangleMesh M;
    const double c[8][3] = {{-hx,-hy,-hz},{hx,-hy,-hz},{hx,hy,-hz},{-hx,hy,-hz},{-hx,-hy,hz},{hx,-hy,hz},{hx,hy,hz},{-hx,hy,hz}};
    for (auto& v : c) M.Vertices.push_back({v[0],v[1],v[2]});
    const uint32_t Q[6][4] = {{0,1,2,3},{4,7,6,5},{0,4,5,1},{1,5,6,2},{2,6,7,3},{3,7,4,0}};
    for (auto& q : Q) { M.Indices.insert(M.Indices.end(), {q[0],q[1],q[2], q[0],q[2],q[3]}); }
    return M;
}
static TriangleMesh Plane(double h, uint32_t n)
{
    TriangleMesh M;
    for (uint32_t y=0;y<=n;++y) for (uint32_t x=0;x<=n;++x)
        M.Vertices.push_back({-h + 2*h*x/n, -h + 2*h*y/n, 0.0});
    for (uint32_t y=0;y<n;++y) for (uint32_t x=0;x<n;++x)
    { const uint32_t a=y*(n+1)+x,b=a+1,c=a+n+2,d=a+n+1; M.Indices.insert(M.Indices.end(),{a,b,c,a,c,d}); }
    return M;
}
static TriangleMesh Sphere(double r, uint32_t rings, uint32_t segs)
{
    TriangleMesh M;
    for (uint32_t i=0;i<=rings;++i){ const double th=3.14159265*i/rings;
        for (uint32_t j=0;j<=segs;++j){ const double ph=2*3.14159265*j/segs;
            M.Vertices.push_back({r*std::sin(th)*std::cos(ph), r*std::sin(th)*std::sin(ph), r*std::cos(th)}); } }
    for (uint32_t i=0;i<rings;++i) for (uint32_t j=0;j<segs;++j){
        const uint32_t a=i*(segs+1)+j,b=a+1,c=a+segs+2,d=a+segs+1; M.Indices.insert(M.Indices.end(),{a,c,b,a,d,c}); }
    return M;
}
int main(int ArgumentCount, char** Arguments)
{
    const char* OutputPath = ArgumentCount > 1 ? Arguments[1] : "Exhibits/CompleteRenderer/Capture/reference-frame.ppm";
    const bool EnableIndirect = ArgumentCount <= 2 || std::strcmp(Arguments[2], "--direct-only") != 0;
    ReferenceScene Scene;
    ReferenceMesh Ground; Ground.Geometry = Plane(180.0, 32); Ground.Material.Albedo = {0.45,0.5,0.42}; Ground.Material.Roughness = 0.82;
    ReferenceMesh Cube; Cube.Geometry = Box(1.0,1.0,1.0); Cube.Material.Albedo = {0.75,0.35,0.25}; Cube.Material.Roughness = 0.42; Cube.Material.Metallic = 0.12; Cube.Translation = {0.0, 4.0, 1.0};
    ReferenceMesh Ball; Ball.Geometry = Sphere(1.2, 24, 32); Ball.Material.Albedo = {0.3,0.45,0.85}; Ball.Material.Roughness = 0.24; Ball.Material.Metallic = 0.64; Ball.Translation = {3.2, 5.0, 1.2};
    ReferenceMesh LampPost; LampPost.Geometry = Box(0.12,0.12,1.6); LampPost.Material.Albedo = {0.2,0.2,0.22}; LampPost.Material.Roughness = 0.64; LampPost.Translation = {-3.0, 4.0, 1.6};
    ReferenceMesh LampBox; LampBox.Geometry = Box(0.3,0.3,0.3); LampBox.Emissive = {6.0,4.5,2.5}; LampBox.Translation = {-3.0, 4.0, 3.4};
    Scene.Meshes = {Ground, Cube, Ball, LampPost, LampBox};

    const SpatialVector SunTo{ -0.45, -0.25, 0.86 };
    const double sl = std::sqrt(SunTo.X*SunTo.X + SunTo.Y*SunTo.Y + SunTo.Z*SunTo.Z);
    Scene.Lights.push_back({ReferenceLight::Kind::Directional, {SunTo.X/sl, SunTo.Y/sl, SunTo.Z/sl}, {3.2,3.05,2.8}});
    Scene.Lights.push_back({ReferenceLight::Kind::Point, {-3.0, 4.0, 3.4}, {60.0, 45.0, 25.0}});

    Scene.Distance.AddPrimitive({DistancePrimitiveKind::HalfSpace, {0,0,0}, {0,0,1}, 0.0});
    Scene.Distance.AddPrimitive({DistancePrimitiveKind::Box, {0,4,1}, {1,1,1}, 0.0});
    Scene.Distance.AddPrimitive({DistancePrimitiveKind::Sphere, {3.2,5,1.2}, {1.2,1.2,1.2}, 0.0});
    Scene.Distance.AddPrimitive({DistancePrimitiveKind::Sphere, {-3.0,4.0,3.4}, {0.35,0.35,0.35}, 2.5});

    ReferenceRenderer Renderer; Renderer.SetScene(Scene);
    CullCamera Cam;
    Cam.Position = {0.0, -7.5, 3.2};
    const SpatialVector Target{0.0, 5.0, 0.6};
    SpatialVector F{Target.X-Cam.Position.X, Target.Y-Cam.Position.Y, Target.Z-Cam.Position.Z};
    const double fl = std::sqrt(F.X*F.X+F.Y*F.Y+F.Z*F.Z); F = {F.X/fl,F.Y/fl,F.Z/fl};
    SpatialVector R{F.Y*1 - F.Z*0, F.Z*0 - F.X*1, F.X*0 - F.Y*0}; // F x Z... use cross(F, up0)
    R = {F.Y*1.0 - F.Z*0.0, F.Z*0.0 - F.X*1.0, 0.0}; // cross(F,(0,0,1)) = (Fy, -Fx, 0)
    const double rl = std::sqrt(R.X*R.X+R.Y*R.Y+R.Z*R.Z); R = {R.X/rl,R.Y/rl,R.Z/rl};
    SpatialVector U{R.Y*F.Z - R.Z*F.Y, R.Z*F.X - R.X*F.Z, R.X*F.Y - R.Y*F.X}; // R x F
    Cam.Forward = F; Cam.Right = R; Cam.Up = U;
    Cam.FovYRadians = 0.9; Cam.Aspect = 16.0/10.0; Cam.NearPlane = 0.1; Cam.FarPlane = 500.0;

    ReferenceRenderSettings S; S.Width = 320; S.Height = 200; S.AccumulationFrames = 24; S.RisCandidates = 8; S.IndirectSamples = EnableIndirect ? 8u : 0u; S.EnvironmentSamples = 32;
    S.SunDirection = Scene.Lights[0].Vector; S.SunIrradiance = {20,19,17};
    const ReferenceImage Img = Renderer.Render(Cam, S);
    std::printf("tris total=%u cut=%u visible=%u surface px=%u meanL=%.4f\n",
        Img.Stats.TrianglesTotal, Img.Stats.TrianglesAfterCut, Img.Stats.TrianglesVisible, Img.Stats.SurfacePixels, Img.Stats.MeanLuminance);
    std::vector<uint8_t> Rgb; EncodeDisplayRgb8(Img, 0.35, Rgb);
    FILE* f = std::fopen(OutputPath,"wb");
    if (!f) { std::perror(OutputPath); return 1; }
    std::fprintf(f,"P6\n%u %u\n255\n", Img.Width, Img.Height);
    std::fwrite(Rgb.data(),1,Rgb.size(),f); std::fclose(f);
    std::printf("wrote %s (%s indirect)\n", OutputPath, EnableIndirect ? "with" : "without");
    return 0;
}
