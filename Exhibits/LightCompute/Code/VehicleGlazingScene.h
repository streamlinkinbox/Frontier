/*==========================================================================================================================================
                                                       VEHICLEGLAZINGSCENE.H
==========================================================================================================================================*/
// 🧩 Shared saved-spectrum loading and retained vehicle pane construction for static and moving Vulkan acceptance.

using GlazingSpectra = std::array<std::array<float,4>,4>;
GlazingSpectra ReadGlazingSpectra(const std::string& Saved)
{
    GlazingSpectra Coefficients;
    std::ifstream Input(Saved,std::ios::binary | std::ios::ate);
    Require(Input && Input.tellg() == std::streamoff(sizeof(Coefficients)),"Missing native glazing coefficients");
    Input.seekg(0); Input.read(reinterpret_cast<char*>(Coefficients.data()),sizeof(Coefficients));
    Require(bool(Input),"Truncated native glazing coefficients");
    for (const auto &Volume : Coefficients)
    {
        Require(std::isfinite(Volume[0]) && Volume[0] >= 1 && Volume[0] <= 3,"Invalid saved glazing IOR");
        for (uint32_t C = 1; C < 4; ++C)
            Require(std::isfinite(Volume[C]) && Volume[C] >= 0 && Volume[C] <= 100,"Invalid glazing absorption");
    }
    return Coefficients;
}

struct VehicleGlazingView
{
    SpatialVector Eye{6,-7,3.5}, Focus{0,0,1};
    uint32_t Width = 320, Height = 200, GlassToken = 700, MirrorToken = 301;
    double Angle = 0;
};
struct VehicleGlazingFrame
{
    LightComputeArchive Thin, Closed;
    uint32_t Address = 0;
    double Volume = 0;
};
VehicleGlazingFrame AssembleVehicleGlazing(LightComputeArchive Scene,
                                          const std::string& Shaders,
                                          const VehicleGlazingView& View = {})
{
    using namespace TransportReference;
    for (auto& L : Scene.Lights)
        if (L.RadianceCategory[3] == 0) L.RadianceCategory = {5.5f,5.2f,4.7f,0};
    Prepare(Scene,16,24,true);
    const auto Materials = AppendSceneMaterials(Scene,Shaders);
    AssignVehicleSurfaces(Scene,Materials);
    SurfaceGeometry Windows;
    for (const auto &F : Scene.Surfaces)
        if (F.Emissive[3] == float(Materials[2]))
            for (const auto &P : {F.A,F.B,F.C})
            {
                SurfaceCorner Corner;
                Corner.Position = {P[0],P[1],P[2]}; Corner.Normal = {0,0,1};
                Windows.TriangleCorners.push_back(uint32_t(Windows.Corners.size()));
                Windows.Corners.push_back(Corner);
            }
    Require(Windows.TriangleCorners.size()/3 == 74,"Original glazing selection changed");
    const auto Glazing = ThickenPane(Windows,.006);
    Require(Glazing.PaneCount == 3 && Glazing.BoundaryEdges == 68 &&
            Glazing.Surface.TriangleCorners.size()/3 == 284,"Vehicle pane closure changed");
    std::cout << "GLAZING_GEOMETRY source_triangles=74 panes=" << Glazing.PaneCount
              << " closed_triangles=284 source_boundary=68 output_boundary=0 thickness_m=0.006 volume_m3="
              << Glazing.SignedVolume << std::endl;
    const auto Mirror = DuplicateSceneMaterial(Scene,Materials[3],1,{.94f,.94f,.94f,.04f},1);
    const auto Orange = DuplicateSceneMaterial(Scene,Materials[3],3,{.85f,.105f,.009f,.22f},0);
    const auto Band = DuplicateSceneMaterial(Scene,Materials[3],3,{.82f,.85f,.88f,.12f},.65f);
    MaterialCone(Scene,2.8,-2.9,Orange,Band); MaterialCone(Scene,-.4,-3.5,Orange,Band);
    auto PosePoint = [&](SpatialVector P)
    {
        const double C = std::cos(View.Angle), S = std::sin(View.Angle);
        return SpatialVector{C*P.X-S*P.Y+.8*View.Angle,S*P.X+C*P.Y,P.Z};
    };
    auto PoseVector = [&](SpatialVector P)
    {
        const double C = std::cos(View.Angle), S = std::sin(View.Angle);
        return SpatialVector{C*P.X-S*P.Y,S*P.X+C*P.Y,P.Z};
    };
    for (double Side : {-1.,1.})
        MaterialRectangle(Scene,PosePoint({.85,Side*1.24,1.30}),PoseVector({.035,Side*.17,0}),
                          {0,0,.09},Mirror,View.MirrorToken);
    MaterialCamera(Scene,View.Eye,View.Focus,View.Width,View.Height);
    RebuildMaterialVisibility(Scene);
    const auto Thin = Scene;
    Scene.Surfaces.erase(std::remove_if(Scene.Surfaces.begin(),Scene.Surfaces.end(),[&](const auto &F)
        { return F.Emissive[3] == float(Materials[2]); }),Scene.Surfaces.end());
    const auto Address = DuplicateSceneMaterial(Scene,Materials[2],4,{1,1,1,0},0);
    for (size_t I = 0; I < Glazing.Surface.TriangleCorners.size(); I += 3)
    {
        auto P = [&](uint32_t C)
        {
            const auto &V = Glazing.Surface.Corners[Glazing.Surface.TriangleCorners[I+C]].Position;
            return SpatialVector{V[0],V[1],V[2]};
        };
        MaterialTriangle(Scene,P(0),P(1),P(2),Address,View.GlassToken);
    }
    RebuildMaterialVisibility(Scene);
    return {Thin,std::move(Scene),Address,Glazing.SignedVolume};
}
void BindGlazingSpectrum(LightComputeArchive& Scene,uint32_t Address,const std::array<float,4>& Spectrum)
{
    const auto Offset = Address-1;
    Scene.Angles[Offset+13] = Spectrum[0];
    for (uint32_t C = 0; C < 3; ++C) Scene.Angles[Offset+16+C] = Spectrum[C+1];
}
