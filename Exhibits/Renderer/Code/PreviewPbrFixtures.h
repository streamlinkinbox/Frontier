/*==========================================================================================================================================
                                                        PREVIEWPBRFIXTURES.H
==========================================================================================================================================*/
// 🧩 Shared resident test geometry and deterministic material specimens for Vulkan evidence and the earlier HTML browser.

#pragma once
#include "VulkanReadback.h"
#include "MatCapFixtures.h"

namespace
{
struct PreviewPbrFixtures
{
    RasterValidationDevice Device;
    Frontier::SurfaceRasterPrograms Programs;
    Frontier::SurfaceGeometryResidence PlaneGeometry, BracketGeometry, SphereGeometry;
    Frontier::SurfaceRasterization Plane, Bracket, Sphere;
    Frontier::SurfaceTextureResidence White, Copper;

    PreviewPbrFixtures(const char *Driver,const std::filesystem::path &Shaders,bool MaterialEnabled = false)
        : Device(Driver,VK_API_VERSION_1_2)
    {
        using namespace Frontier;
        const std::array<const char *,10> Names{"CadSurfaceVertex","CadSurfaceFragment","CadEdgeVertex","CadEdgeFragment",
            "MatCapSurfaceVertex","MatCapSurfaceFragment","PreviewPbrSurfaceVertex","PreviewPbrSurfaceFragment","MaterialSurfaceVertex","MaterialSurfaceFragment"};
        std::array<std::vector<uint32_t>,10> Words;
        for (size_t Index = 0; Index < (MaterialEnabled ? Names.size() : 8); ++Index)
            Words[Index] = DecodeShaderArchive(Shaders/(std::string(Names[Index])+".spv"));
        Programs.Initialize(Device.QueryAccess(),{{Words[0],"main"},{Words[1],"main"},{Words[2],"main"},{Words[3],"main"},
            {Words[4],"main"},{Words[5],"main"},{Words[6],"main"},{Words[7],"main"},{Words[8],"main"},{Words[9],"main"}});
        auto AuthoredPlane = ConstructMatCapPlane();
        for (auto &Corner : AuthoredPlane.Corners) Corner.Coordinates = {(Corner.Position[0]+.8f)/1.6f,(Corner.Position[1]+.8f)/1.6f};
        AuthoredPlane.MaterialCoordinatesEnabled = true;
        PlaneGeometry.Initialize(Device.QueryAccess(),AuthoredPlane);
        auto AuthoredBracket = ConstructBracketFixture();
        for (size_t First = 0; First < AuthoredBracket.Corners.size(); First += 4)
        {
            auto &Origin = AuthoredBracket.Corners[First];
            std::array<float,3> Tangent{}, Bitangent{};
            float Length = 0;
            for (size_t Axis = 0; Axis < 3; ++Axis)
            {
                Tangent[Axis] = AuthoredBracket.Corners[First+1].Position[Axis]-Origin.Position[Axis];
                Bitangent[Axis] = AuthoredBracket.Corners[First+3].Position[Axis]-Origin.Position[Axis];
                Length += Tangent[Axis]*Tangent[Axis];
            }
            for (auto &Axis : Tangent) Axis /= std::sqrt(Length);
            const std::array<float,3> Cross{Origin.Normal[1]*Tangent[2]-Origin.Normal[2]*Tangent[1],
                Origin.Normal[2]*Tangent[0]-Origin.Normal[0]*Tangent[2],Origin.Normal[0]*Tangent[1]-Origin.Normal[1]*Tangent[0]};
            float Correlation = 0;
            for (size_t Axis = 0; Axis < 3; ++Axis) Correlation += Cross[Axis]*Bitangent[Axis];
            for (size_t Index = 0; Index < 4; ++Index)
            {
                auto &Corner = AuthoredBracket.Corners[First+Index];
                Corner.Coordinates = {Index == 1 || Index == 2 ? 1.f : 0.f,Index >= 2 ? 1.f : 0.f};
                Corner.Tangent = {Tangent[0],Tangent[1],Tangent[2],Correlation < 0 ? -1.f : 1.f};
            }
        }
        AuthoredBracket.MaterialCoordinatesEnabled = true;
        BracketGeometry.Initialize(Device.QueryAccess(),AuthoredBracket);
        auto AuthoredSphere = ConstructNormalCalibrationSphere();
        for (size_t Index = 0; Index < AuthoredSphere.Corners.size(); ++Index)
        {
            auto &Corner = AuthoredSphere.Corners[Index];
            const float Longitude = float(Index%97)/96;
            const float Latitude = float(Index/97)/48;
            const double φ = Longitude*2*std::numbers::pi;
            Corner.Coordinates = {Longitude,Latitude};
            Corner.Tangent = {float(-std::sin(φ)),float(std::cos(φ)),0,-1};
        }
        AuthoredSphere.MaterialCoordinatesEnabled = true;
        SphereGeometry.Initialize(Device.QueryAccess(),AuthoredSphere);
        Plane.Initialize(Programs,PlaneGeometry,128,128);
        Bracket.Initialize(Programs,BracketGeometry,960,720);
        Sphere.Initialize(Programs,SphereGeometry,720,720);
        White.Initialize(Programs,{1,1,{255,255,255,0}});
        Copper.Initialize(Programs,ConstructCopperMatCap());
    }

    static Frontier::PreviewScattering ConstructSpecimen(float Roughness,float Metallic,float Coating,float Exposure)
    {
        Frontier::PreviewScattering Scattering;
        Scattering.Strata[0].AlbedoRoughness = {.65f,.22f,.075f,Roughness};
        Scattering.Strata[0].Optics[0] = Metallic;
        Scattering.LightColourExposure[3] = Exposure;
        if (Coating > 0)
        {
            Scattering.Configuration[0] = 2;
            auto &Stratum = Scattering.Strata[1];
            Stratum.Classification[0] = 1;
            Stratum.AlbedoRoughness[3] = .15f;
            Stratum.Optics[2] = Coating;
            Stratum.AbsorptionThickness = {1,3,8,.035f};
        }
        return Scattering;
    }

    std::vector<unsigned char> CaptureMaterial(uint32_t Geometry,Frontier::MaterialScattering Scattering,
                                               const Frontier::SurfaceMaterialResidence &Textures)
    {
        using namespace Frontier;
        auto Camera = ProjectBracketFixture(960,720);
        auto View = DeriveBracketNormalView(960,720);
        if (Geometry != 0)
        {
            Camera.ClipFromSurface = {1,0,0,0,0,-1,0,0,0,0,-.4f,0,0,0,.5f,1};
            View = {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        }
        for (size_t Index = 0; Index < View.size(); ++Index) Scattering.Illumination.ViewFromSurface[Index] = float(View[Index]);
        Scattering.Illumination.ViewFromSurface[14] = -3;
        SurfaceScatteringResidence Parameters;
        Parameters.Initialize(Programs,Scattering);
        return Device.ExtractCapture(Geometry == 0 ? Bracket : Sphere,DeriveSurfaceNormalProjection(Camera,View),Textures,Parameters);
    }

    std::vector<unsigned char> Capture(uint32_t Geometry,uint32_t Mode,Frontier::PreviewScattering Scattering)
    {
        using namespace Frontier;
        auto &Raster = Geometry == 0 ? Bracket : Sphere;
        auto Camera = ProjectBracketFixture(960,720);
        auto View = DeriveBracketNormalView(960,720);
        if (Geometry != 0)
        {
            Camera.ClipFromSurface = {1,0,0,0,0,-1,0,0,0,0,-.4f,0,0,0,.5f,1};
            View = {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        }
        if (Mode == 0) return Device.ExtractCapture(Raster,Camera);
        const auto Projection = DeriveSurfaceNormalProjection(Camera,View);
        if (Mode == 1) return Device.ExtractCapture(Raster,Projection,Copper);
        for (size_t Index = 0; Index < View.size(); ++Index) Scattering.ViewFromSurface[Index] = float(View[Index]);
        Scattering.ViewFromSurface[14] = -3;
        SurfaceScatteringResidence Parameters;
        Parameters.Initialize(Programs,Scattering);
        return Device.ExtractCapture(Raster,Projection,White,Parameters);
    }
};
} // namespace
