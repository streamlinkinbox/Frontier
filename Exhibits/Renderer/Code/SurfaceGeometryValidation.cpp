/*==========================================================================================================================================
                                                       SURFACEGEOMETRYVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Validate geometry bounds, finite coordinates and normals, explicit topology, and dormant Vulkan lifecycle refusal without a GPU.

#include "Engine/Render/SurfaceRasterization.h"
#include "BracketFixture.h"
#include <iostream>
#include <limits>
#include <stdexcept>

//----------------------------------------------------------------------------------------------------------------------
//                                               CPU CONTRACT VALIDATION
//----------------------------------------------------------------------------------------------------------------------

int main()
{
    using namespace Frontier;
    try
    {
        std::string Diagnostic;
        uint32_t Confirmed = 0;
        const auto AssertCondition = [&](bool Condition)
        {
            if (!Condition) throw std::runtime_error("Surface geometry assertion " + std::to_string(Confirmed+1) + " failed");
            ++Confirmed;
        };
        const auto ConfirmRefusal = [&](auto Construct)
        {
            bool Refused = false;
            try { Construct(); }
            catch (const std::exception &)
            {
                Refused = true;
            }
            AssertCondition(Refused);
        };
        auto Geometry = ConstructBracketFixture();
        AssertCondition(ValidateSurfaceGeometry(Geometry,Diagnostic));
        AssertCondition(Diagnostic.empty());
        AssertCondition(Geometry.Corners.size() == 48 && Geometry.TriangleCorners.size() == 72 && Geometry.Edges.size() == 24);
        Geometry.Edges.clear();
        AssertCondition(ValidateSurfaceGeometry(Geometry,Diagnostic));
        Geometry.Corners.clear();
        AssertCondition(!ValidateSurfaceGeometry(Geometry,Diagnostic) && !Diagnostic.empty());
        Geometry = ConstructBracketFixture();
        Geometry.TriangleCorners.pop_back();
        AssertCondition(!ValidateSurfaceGeometry(Geometry,Diagnostic));
        Geometry = ConstructBracketFixture();
        Geometry.TriangleCorners[0] = UINT32_MAX;
        AssertCondition(!ValidateSurfaceGeometry(Geometry,Diagnostic));
        Geometry = ConstructBracketFixture();
        Geometry.Corners[0].Position[1] = std::numeric_limits<float>::infinity();
        AssertCondition(!ValidateSurfaceGeometry(Geometry,Diagnostic));
        Geometry = ConstructBracketFixture();
        Geometry.Corners[0].Normal = {0,0,0};
        AssertCondition(!ValidateSurfaceGeometry(Geometry,Diagnostic));
        Geometry.Corners[0].Normal[1] = std::numeric_limits<float>::quiet_NaN();
        AssertCondition(!ValidateSurfaceGeometry(Geometry,Diagnostic));
        Geometry = ConstructBracketFixture();
        Geometry.Edges[0].End = Geometry.Edges[0].Start;
        AssertCondition(!ValidateSurfaceGeometry(Geometry,Diagnostic));
        Geometry = ConstructBracketFixture();
        Geometry.Edges[0].Start[0] = std::numeric_limits<float>::quiet_NaN();
        AssertCondition(!ValidateSurfaceGeometry(Geometry,Diagnostic));
        SurfaceRasterPrograms Programs;
        SurfaceGeometryResidence Residence;
        SurfaceRasterization Target;
        AssertCondition(!Target.QueryImage() && !Target.QueryImageView() && !Target.QueryExtent().width);
        Target.ConfirmSubmission();
        AssertCondition(Target.QueryGeometryUploadCount() == 0);
        ConfirmRefusal([&]
        {
            Programs.Initialize({},{});
        });
        ConfirmRefusal([&]
        {
            Residence.Initialize({},ConstructBracketFixture());
        });
        ConfirmRefusal([&]
        {
            Target.Initialize(Programs,Residence,0,0);
        });
        auto Projection = ProjectBracketFixture(640,480);
        ConfirmRefusal([&]
        {
            Target.ConstructCommands(VK_NULL_HANDLE,Projection);
        });
        Projection.DisplayStroke[2] = -1;
        ConfirmRefusal([&]
        {
            Target.ConstructCommands(VK_NULL_HANDLE,Projection);
        });
        Projection.DisplayStroke[2] = 1;
        Projection.ClipFromSurface[0] = std::numeric_limits<float>::quiet_NaN();
        ConfirmRefusal([&]
        {
            Target.ConstructCommands(VK_NULL_HANDLE,Projection);
        });
        MatCapImage Lookup{1,1,{128,64,32,0}};
        AssertCondition(ValidateMatCapImage(Lookup));
        Lookup.Width = 0;
        AssertCondition(!ValidateMatCapImage(Lookup));
        Lookup.Width = 4097;
        AssertCondition(!ValidateMatCapImage(Lookup));
        Lookup.Width = 1;
        Lookup.Rgba.pop_back();
        AssertCondition(!ValidateMatCapImage(Lookup));
        const std::array<double,16> IdentityView{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        const auto FiniteProjection = ProjectBracketFixture(640,480);
        auto MatCap = DeriveMatCapProjection(FiniteProjection,IdentityView);
        AssertCondition(ValidateMatCapProjection(MatCap));
        auto NonUniform = IdentityView;
        NonUniform[0] = 4;
        NonUniform[10] = .5;
        MatCap = DeriveMatCapProjection(FiniteProjection,NonUniform);
        AssertCondition(MatCap.ViewNormalRow0[0] == .125f && MatCap.ViewNormalRow1[1] == .5f && MatCap.ViewNormalRow2[2] == 1);
        auto Shear = IdentityView;
        Shear[4] = .5;
        MatCap = DeriveMatCapProjection(FiniteProjection,Shear);
        AssertCondition(MatCap.ViewNormalRow1[0] == -.5f && MatCap.ViewNormalRow0[1] == 0);
        auto Reflection = IdentityView;
        Reflection[0] = -1;
        MatCap = DeriveMatCapProjection(FiniteProjection,Reflection);
        AssertCondition(MatCap.ViewNormalRow0[0] == -1 && MatCap.ViewNormalRow1[1] == 1);
        for (const auto Magnitude : {1e250,1e-250})
        {
            auto Uniform = IdentityView;
            Uniform[0] = Uniform[5] = Uniform[10] = Magnitude;
            const auto UniformProjection = DeriveMatCapProjection(FiniteProjection,Uniform);
            AssertCondition(UniformProjection.ViewNormalRow0[0] == 1 && UniformProjection.ViewNormalRow2[2] == 1);
        }
        auto Singular = IdentityView;
        Singular[10] = 0;
        ConfirmRefusal([&]
        {
            (void)DeriveMatCapProjection(FiniteProjection,Singular);
        });
        Singular = IdentityView;
        Singular[10] = 1e-20;
        ConfirmRefusal([&]
        {
            (void)DeriveMatCapProjection(FiniteProjection,Singular);
        });
        auto Projective = IdentityView;
        Projective[11] = 1;
        ConfirmRefusal([&]
        {
            (void)DeriveMatCapProjection(FiniteProjection,Projective);
        });
        auto Nonfinite = IdentityView;
        Nonfinite[12] = std::numeric_limits<double>::infinity();
        ConfirmRefusal([&]
        {
            (void)DeriveMatCapProjection(FiniteProjection,Nonfinite);
        });
        MatCap = DeriveMatCapProjection(FiniteProjection,IdentityView);
        MatCap.ViewNormalRow0[3] = 1;
        AssertCondition(!ValidateMatCapProjection(MatCap));
        MatCap.ViewNormalRow0[3] = 0;
        MatCap.ViewNormalRow1 = MatCap.ViewNormalRow0;
        AssertCondition(!ValidateMatCapProjection(MatCap));
        MatCap = DeriveMatCapProjection(FiniteProjection,IdentityView);
        MatCap.SurfaceTint[3] = .5f;
        AssertCondition(!ValidateMatCapProjection(MatCap));
        SurfaceMatCap DormantLookup;
        AssertCondition(DormantLookup.QueryUploadCount() == 0);
        ConfirmRefusal([&]
        {
            DormantLookup.Initialize(Programs,{});
        });
        MatCap = DeriveMatCapProjection(FiniteProjection,IdentityView);
        ConfirmRefusal([&]
        {
            Target.ConstructCommands(VK_NULL_HANDLE,MatCap,DormantLookup);
        });
        DormantLookup.Finalize();
        DormantLookup.Finalize();
        Target.Finalize();
        Target.Finalize();
        Residence.Finalize();
        Programs.Finalize();
        std::cout << "PASS: " << Confirmed << " geometry and dormant lifecycle assertions\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << Failure.what() << '\n';
        return 1;
    }
}
