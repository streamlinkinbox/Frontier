#include "Engine/Rendering/CompleteFrameUnits.h"
#include "Engine/Rendering/BarrierDerivation.h"
#include "Engine/Rendering/ReferenceRenderer.h"
#include "Engine/Rendering/SubmissionOrder.h"
#include "Engine/Rendering/TransientTargetAtlas.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
int Failures = 0;
void Check(bool Condition, const char* Name)
{
    if (!Condition) { std::printf("FAIL %s\n", Name); ++Failures; }
    else { std::printf("PASS %s\n", Name); }
}

Frontier::TriangleMesh MakeGround()
{
    using namespace Frontier;
    TriangleMesh M;
    M.Vertices = {{-20,-20,0},{20,-20,0},{20,20,0},{-20,20,0}};
    M.Indices = {0,1,2,0,2,3};
    return M;
}
Frontier::TriangleMesh MakeCube()
{
    using namespace Frontier;
    TriangleMesh M;
    M.Vertices = {{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0},{-1,-1,2},{1,-1,2},{1,1,2},{-1,1,2}};
    const uint32_t Q[6][4] = {{0,1,2,3},{4,7,6,5},{0,4,5,1},{1,5,6,2},{2,6,7,3},{3,7,4,0}};
    for (const auto& q : Q) M.Indices.insert(M.Indices.end(), {q[0],q[1],q[2],q[0],q[2],q[3]});
    return M;
}
}

int main()
{
    using namespace Frontier;
    ReferenceMaterial SmoothDielectric; SmoothDielectric.Albedo={0.7,0.7,0.7}; SmoothDielectric.Roughness=0.08;
    ReferenceMaterial RoughDielectric=SmoothDielectric; RoughDielectric.Roughness=0.8;
    const SpectrumRgb SmoothResponse = EvaluateReferenceBrdf(SmoothDielectric,{0,0,1},{0,0,1},{0,0,1});
    const SpectrumRgb RoughResponse = EvaluateReferenceBrdf(RoughDielectric,{0,0,1},{0,0,1},{0,0,1});
    Check(SmoothResponse.R > RoughResponse.R * 100.0, "ported GGX response narrows as roughness decreases");
    ReferenceMaterial RedMetal; RedMetal.Albedo={0.9,0.05,0.02}; RedMetal.Metallic=1.0; RedMetal.Roughness=0.3;
    const SpectrumRgb MetalResponse = EvaluateReferenceBrdf(RedMetal,{0,0,1},{0,0,1},{0,0,1});
    Check(MetalResponse.R > MetalResponse.G * 10.0 && MetalResponse.G > MetalResponse.B,
          "metallic Fresnel follows conductor albedo tint");
    Check(EvaluateReferenceBrdf(SmoothDielectric,{0,0,1},{0,0,1},{0,0,-1}).R == 0.0,
          "BRDF rejects a below-surface light");
    ReferenceMaterial Oracle; Oracle.Albedo={0.7,0.6,0.5}; Oracle.Roughness=0.5;
    const SpectrumRgb OracleValue=EvaluateReferenceBrdf(Oracle,{0,0,1},{0,0,1},{0,0,1});
    const double F0=0.04, ExpectedRed=F0/(4.0*3.14159265358979323846*0.5*0.5*0.5*0.5)+
        (1.0-F0)*0.7/3.14159265358979323846;
    Check(std::abs(OracleValue.R-ExpectedRed)<1e-12, "GGX + Smith + Schlick matches independent closed form");

    const CompleteFrameDeclaration Frame = DeclareCompleteFrame(160, 100, {true});
    ResolvedSubmissionOrder Order;
    std::string Diagnostic;
    Check(ResolveSubmissionOrder(Frame.Units, Order, Diagnostic), "whole frame resolves without ordering cycle");
    Check(!Order.Order.empty() && Frame.Units[Order.Order.front()].Name == "SurfaceRaster", "surface raster begins frame");
    bool HasPresent = false;
    for (uint32_t I : Order.Order) HasPresent |= Frame.Units[I].Name == "DisplayPresent";
    Check(HasPresent, "resolved frame contains display present");
    Check(Frame.Units.size() == 38, "frame combines all declared stages in one 38-unit schedule");
    bool HasIllumination=false, HasEnvironment=false;
    for (const auto& Unit : Frame.Units)
    {
        HasIllumination |= Unit.Name == "DistanceRecordsSubmission";
        HasEnvironment  |= Unit.Name == "NightStarsRasterization";
    }
    Check(HasIllumination && HasEnvironment, "same schedule includes indirect-light and environment families");
    DerivedBarriers Barriers;
    Check(DeriveBarriers(Frame.Units, Order, Frame.Targets, Barriers, Diagnostic), "whole-frame barriers derive");
    Check(Barriers.Barriers.size() > 40, "whole frame has nontrivial inter-stage barriers");
    ResolvedTransientAtlas Atlas;
    Check(ResolveTransientTargetAtlas(Frame.Units, Order, Frame.Targets, Atlas, Diagnostic), "whole-frame atlas resolves");
    Check(Atlas.TotalBytes > 0 && Atlas.TotalBytes < Atlas.UnaliasedBytes, "whole-frame transient targets alias safely");

    ReferenceScene Scene;
    ReferenceMesh Ground; Ground.Geometry = MakeGround(); Ground.Material.Albedo = {0.45,0.48,0.4}; Ground.Material.Roughness = 0.8;
    ReferenceMesh Cube; Cube.Geometry = MakeCube(); Cube.Material.Albedo = {0.8,0.25,0.1}; Cube.Material.Roughness = 0.36; Cube.Material.Metallic = 0.15; Cube.Translation = {0,4,0};
    Scene.Meshes = {Ground, Cube};
    Scene.Lights.push_back({ReferenceLight::Kind::Directional, {0.0,-0.4,0.916515}, {2.5,2.4,2.3}});
    Scene.Distance.AddPrimitive({DistancePrimitiveKind::HalfSpace,{0,0,0},{0,0,1},0.0});
    Scene.Distance.AddPrimitive({DistancePrimitiveKind::Box,{0,4,1},{1,1,1},1.5});

    CullCamera Camera;
    Camera.Position = {0,-7,3}; Camera.Forward = {0,0.97,-0.243}; Camera.Right = {1,0,0};
    Camera.Up = {0,0.243,0.97}; Camera.FovYRadians = 0.9; Camera.Aspect = 1.6;
    Camera.NearPlane = 0.1; Camera.FarPlane = 100.0;
    ReferenceRenderSettings Settings;
    Settings.Width=64; Settings.Height=40; Settings.AccumulationFrames=2; Settings.RisCandidates=4;
    Settings.IndirectSamples=4; Settings.Denoise=true;

    ReferenceRenderer A; A.SetScene(Scene);
    ReferenceRenderer B; B.SetScene(Scene);
    const ReferenceImage ImageA = A.Render(Camera, Settings);
    const ReferenceImage ImageB = B.Render(Camera, Settings);
    ReferenceRenderSettings NoEnvironment = Settings;
    NoEnvironment.EnvironmentSamples = 0;
    ReferenceRenderer WithoutEnvironment; WithoutEnvironment.SetScene(Scene);
    const ReferenceImage ImageWithoutEnvironment = WithoutEnvironment.Render(Camera, NoEnvironment);
    Check(ImageA.Stats.MeanEnvironmentIbl > 1e-8 && ImageWithoutEnvironment.Stats.MeanEnvironmentIbl == 0.0 &&
          ReferenceImageHash(ImageA) != ReferenceImageHash(ImageWithoutEnvironment),
          "atmospheric environment MIS contributes to the final surface image");
    Check(ImageA.Stats.SurfacePixels > 500, "renderer rasterises visible scene surfaces");
    Check(ImageA.Stats.TrianglesVisible > 0 && ImageA.Stats.TrianglesVisible <= ImageA.Stats.TrianglesTotal,
          "cluster cut + frustum cull feed rasterizer");
    Check(ReferenceImageHash(ImageA) == ReferenceImageHash(ImageB), "full frame is bit-deterministic for fixed seed");
    std::vector<uint8_t> Display;
    EncodeDisplayRgb8(ImageA, 0.35, Display);
    bool HasSignal = false;
    for (uint8_t C : Display) HasSignal |= C > 12;
    Check(Display.size() == ImageA.Pixels.size()*3 && HasSignal, "linear HDR frame encodes to visible display pixels");
    std::printf("reference frame: %ux%u, triangles %u/%u, surface pixels %u, mean luminance %.4f, mean IBL %.6f\n",
                ImageA.Width, ImageA.Height, ImageA.Stats.TrianglesVisible, ImageA.Stats.TrianglesTotal,
                ImageA.Stats.SurfacePixels, ImageA.Stats.MeanLuminance, ImageA.Stats.MeanEnvironmentIbl);
    std::printf("complete renderer validation: %d passed, %d failed\n", 18-Failures, Failures);
    return Failures == 0 ? 0 : 1;
}
