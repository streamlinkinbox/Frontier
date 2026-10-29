/*==========================================================================================================================================
                                                         LIGHTVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Native analytic-light checks; independent completeness probes and exact-image comparisons.
#include "Engine/Rendering/ReferenceRenderer.h"
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
using namespace Frontier;
namespace
{
void Require(bool Condition, const char* Message)
{
    if (!Condition)
        throw std::runtime_error(Message);
}
void Near(double A, double B)
{
    Require(std::abs(A - B) < 1e-10, "Numerical comparison failed");
}
template <class Action> void Reject(Action Test)
{
    bool Rejected = false;
    try
    {
        Test();
    }
    catch (const std::runtime_error&)
    {
        Rejected = true;
    }
    Require(Rejected, "Expected rejection");
}
std::string Ies(const std::string& Horizontal, const std::string& Values, uint32_t Count = 1)
{
    return "IESNA:LM-63-2002\n[TEST] Synthetic, not measured\nTILT=NONE\n1 -1 2 3 " + std::to_string(Count) +
           " 1 2 0 0 0\n1 1 10\n0 45 90\n" + Horizontal + "\n" + Values;
}
CandelaDistribution Profile(const std::string& Text)
{
    std::istringstream Stream(Text);
    return CandelaDistribution::Read(Stream);
}
ReferenceScene Scene()
{
    ReferenceScene S;
    ReferenceMesh Ground;
    Ground.Geometry.Vertices = {{-5, -5, 0}, {5, -5, 0}, {5, 5, 0}, {-5, 5, 0}};
    Ground.Geometry.Indices = {0, 1, 2, 0, 2, 3};
    S.Meshes.push_back(Ground);
    ReferenceLight Light;
    Light.Kind = ReferenceLight::Kind::Spot;
    Light.Vector = {0, 0, 3};
    Light.Radiance = {30, 20, 10};
    Light.Reach = 7;
    Light.InnerCosine = .9;
    Light.OuterCosine = .1;
    S.Lights.push_back(Light);
    Light.Kind = ReferenceLight::Kind::Point;
    Light.Vector = {2, 0, 3};
    Light.Radiance = {10, 25, 20};
    S.Lights.push_back(Light);
    Light.Vector = {-2, 0, 3};
    Light.Radiance = {10, 5, 25};
    Light.Photometry = std::make_shared<CandelaDistribution>(Profile(Ies("0", "100 50 0")));
    S.Lights.push_back(Light);
    for (int I = 0; I < 64; ++I)
    {
        Light.Photometry = nullptr;
        Light.Vector = {30. + I, 0, 2};
        Light.Reach = .5;
        S.Lights.push_back(Light);
    }
    return S;
}
} // namespace
int main()
{
    try
    {
        for (const char* Header : {"IESNA:LM-63-1995", "IESNA:LM-63-2002", "IES:LM-63-2019"})
        {
            auto Text = Ies("0", "100 50 0");
            Text.replace(0, Text.find('\n'), Header);
            Near(Profile(Text).Relative(45, 0), .5);
        }
        auto Csv = Ies("0", "100 50 0");
        std::replace(Csv.begin(), Csv.end(), ' ', ',');
        Near(Profile(Csv).Relative(45, 0), .5);
        auto CrLf = Ies("0", "100 50 0");
        for (size_t I = 0; I < CrLf.size(); ++I)
            if (CrLf[I] == '\n')
            {
                CrLf.insert(I, "\r");
                ++I;
            }
        Near(Profile(CrLf).Relative(45, 0), .5);
        Reject([&] { Profile("IESNA:LM-63-2002trailing" + Ies("0", "100 50 0")); });
        Reject([&] { Profile(std::string(8 * 1024 * 1024 + 1, 'A')); });
        auto P = Profile(Ies("0", "100 50 0"));
        Near(P.EvaluateCandela(0, 250), 200);
        Near(P.Relative(22.5, 70), .75);
        Near(P.Relative(95, 0), 0);
        Near(P.Relative(45, -100), .5);
        auto Q = Profile(Ies("0 90", "100 50 0 20 10 0", 2));
        Near(Q.Relative(0, 45), .6);
        Near(Q.Relative(0, 135), .6);
        Near(Q.Relative(0, 225), .6);
        Near(Q.Relative(0, 315), .6);
        auto H = Profile(Ies("0 180", "100 50 0 20 10 0", 2));
        Near(H.Relative(0, 270), .6);
        auto Full = Profile(Ies("0 180 360", "100 50 0 20 10 0 100 50 0", 3));
        Near(Full.Relative(0, 270), .6);
        Reject([&] { Profile(Ies("0 180 360", "100 50 0 20 10 0 90 50 0", 3)); });
        auto Tilt = Ies("0", "100 50 0");
        Tilt.replace(Tilt.find("TILT=NONE"), 9, "TILT=INCLUDE");
        Reject([&] { Profile(Tilt); });
        Reject([&] { Profile(Ies("0", "100 50")); });
        Reject([&] { Profile(Ies("0", "100 -50 0")); });
        Reject([&] { Profile(Ies("0", "0 0 0")); });
        Reject([&] { Profile(Ies("0", "100 50 0 EXTRA")); });
        Reject([&] { Profile(Ies("0 45", "100 50 0 20 10 0", 2)); });
        auto Wrong = Ies("0", "100 50 0");
        Wrong.replace(Wrong.find("1 2 0 0 0"), 9, "2 2 0 0 0");
        Reject([&] { Profile(Wrong); });
        auto Angles = Ies("0", "100 50 0");
        Angles.replace(Angles.find("0 45 90"), 7, "0 0 90");
        Reject([&] { Profile(Angles); });
        ReferenceLight Spot;
        Spot.Kind = ReferenceLight::Kind::Spot;
        Spot.Vector = {0, 0, 2};
        Spot.Reach = 4;
        SpatialVector D;
        double Distance;
        auto Center = ReferenceLightArrival(Spot, {0, 0, 0}, D, Distance);
        Near(Center.R, std::pow(1 - std::pow(.5, 4), 2) / 4);
        Near(Distance, 2);
        Near(D.Z, 1);
        Near(ReferenceLightArrival(Spot, {3, 0, 2}, D, Distance).R, 0);
        Near(ReferenceLightArrival(Spot, {0, 0, -2}, D, Distance).R, 0);
        Spot.Active = false;
        Near(ReferenceLightArrival(Spot, {0, 0, 0}, D, Distance).R, 0);
        Spot.Active = true;
        Spot.Photometry = std::make_shared<CandelaDistribution>(P);
        Near(ReferenceLightArrival(Spot, {0, 0, 0}, D, Distance).R, Center.R);
        ReferenceLight Oriented;
        Oriented.Vector = {0, 0, 2};
        Oriented.Photometry = std::make_shared<CandelaDistribution>(
            Profile(Ies("0 90 180 270 360", "100 80 0 100 40 0 100 20 0 100 10 0 100 80 0", 5)));
        Near(ReferenceLightArrival(Oriented, {0, 1, 1}, D, Distance).R, .4);
        Near(ReferenceLightArrival(Oriented, {-1, 0, 1}, D, Distance).R, .2);
        Oriented.Up = {1, 0, 0};
        Near(ReferenceLightArrival(Oriented, {1, 0, 1}, D, Distance).R, .4);
        ReferenceRenderer Bad;
        auto Invalid = Scene();
        Invalid.Lights[0].InnerCosine = Invalid.Lights[0].OuterCosine;
        Reject([&] { Bad.SetScene(Invalid); });
        Invalid = Scene();
        Invalid.Lights[2].Up = {0, 0, 1};
        Reject([&] { Bad.SetScene(Invalid); });
        std::mt19937_64 Random(99);
        std::uniform_real_distribution<double> Position(-16, 16), Radius(.1, 4);
        std::vector<LightInfluence> Lights;
        for (uint32_t I = 0; I < 96; ++I)
            Lights.push_back({{Position(Random), Position(Random), Position(Random)}, Radius(Random)});
        Lights.push_back({{}, 0});
        LightInfluenceSpace Grid;
        Grid.Build(Lights, {-10, -10, -10}, {10, 10, 10}, {8, 8, 8}, 128);
        uint64_t Checks = 0;
        for (uint32_t I = 0; I < 10000; ++I)
        {
            SpatialVector Point{Position(Random), Position(Random), Position(Random)};
            auto& List = Grid.Candidates(Point);
            for (uint32_t J = 0; J < Lights.size(); ++J)
            {
                const auto& L = Lights[J];
                double Distance =
                    std::hypot(Point.X - L.Position.X, Point.Y - L.Position.Y, Point.Z - L.Position.Z);
                if (L.Radius == 0 || Distance < L.Radius)
                {
                    Require(std::find(List.begin(), List.end(), J) != List.end(), "Affecting light lost");
                    ++Checks;
                }
            }
        }
        bool Fallback = false;
        Grid.Candidates({10, 0, 0}, &Fallback);
        Require(Fallback, "Upper bound must fall back");
        Grid.Build(Lights, {-10, -10, -10}, {10, 10, 10}, {8, 8, 8}, 1);
        bool SawOverflow = false;
        for (const auto& L : Lights)
        {
            auto& List = Grid.Candidates(L.Position, &Fallback);
            if (Fallback)
            {
                SawOverflow = true;
                Require(List.size() == 97, "Overflow lost lights");
            }
        }
        Require(SawOverflow, "Overflow fixture failed");
        Reject([&] { Grid.Build(Lights, {0, 0, 0}, {0, 1, 1}, {1, 1, 1}, 1); });
        Reject([&] { Grid.Build(Lights, {0, 0, 0}, {1, 1, 1}, {0, 1, 1}, 1); });
        Require(Grid.Candidates({20, 0, 0}).size() == 97, "Rejected rebuild changed grid");
        std::vector<LightInfluence> Dense(97, {{0, 0, 0}, 100});
        Grid.Build(Dense, {-10, -10, -10}, {10, 10, 10}, {8, 8, 8}, 16);
        const auto& DenseList = Grid.Candidates({0, 0, 0}, &Fallback);
        Require(Fallback && DenseList.size() == 97, "Dense 97-light cell was truncated");
        ReferenceRenderer Renderer;
        Renderer.SetScene(Scene());
        CullCamera Camera;
        Camera.Position = {0, -5, 3};
        Camera.Forward = {0, 5, -3};
        Camera.Up = {0, 0, 1};
        ReferenceRenderSettings Settings;
        Settings.Width = 24;
        Settings.Height = 16;
        Settings.AtmosphereEnabled = false;
        Settings.EnvironmentSamples = 0;
        Settings.IndirectSamples = 0;
        Settings.AccumulationFrames = 1;
        Settings.Denoise = false;
        Settings.ExhaustiveLights = true;
        Settings.SpatialReuse = false;
        auto All = Renderer.Render(Camera, Settings);
        Settings.ClusteredLights = true;
        auto Cluster = Renderer.Render(Camera, Settings);
        Require(All.Stats.SurfacePixels > 0, "No rasterized receiver");
        Require(Cluster.Stats.LightListVisits < All.Stats.LightListVisits, "Grid did not cull remote lights");
        for (size_t I = 0; I < All.Pixels.size(); ++I)
        {
            Near(All.Pixels[I].R, Cluster.Pixels[I].R);
            Near(All.Pixels[I].G, Cluster.Pixels[I].G);
            Near(All.Pixels[I].B, Cluster.Pixels[I].B);
        }
        auto SamplesSettings = Settings;
        SamplesSettings.StoreSurfaceSamples = true;
        auto Samples = Renderer.Render(Camera, SamplesSettings);
        Require(ReferenceImageHash(Samples) == ReferenceImageHash(All), "Surface export changed pixels");
        Require(Samples.SurfaceSamples.size() == Samples.Pixels.size(), "Surface export extent mismatch");
        uint32_t SampleHits = 0;
        for (const auto& S : Samples.SurfaceSamples)
        {
            SampleHits += S.Hit;
            Require(S.Hit ? S.SurfaceToken != 0 : S.SurfaceToken == 0, "Surface export identity mismatch");
        }
        Require(SampleHits == Samples.Stats.SurfacePixels, "Surface export lost hits");
        SamplesSettings.Denoise = true;
        Reject([&] { (void)Renderer.Render(Camera, SamplesSettings); });
        SamplesSettings.Denoise = false;
        SamplesSettings.AccumulationFrames = 2;
        Reject([&] { (void)Renderer.Render(Camera, SamplesSettings); });
        Settings.LightCellCapacity = 1;
        auto OverflowImage = Renderer.Render(Camera, Settings);
        Require(OverflowImage.Stats.LightFallbackQueries > 0, "No rendered overflow fallback");
        Require(ReferenceImageHash(All) == ReferenceImageHash(OverflowImage), "Overflow changed rendering");
        ReferenceRenderer EmptyCell;
        auto Remote = Scene();
        Remote.Lights.resize(1);
        Remote.Lights[0].Kind = ReferenceLight::Kind::Point;
        Remote.Lights[0].Vector = {50, 50, 50};
        Remote.Lights[0].Reach = .1;
        EmptyCell.SetScene(Remote);
        auto Fresh = Settings;
        Fresh.ExhaustiveLights = false;
        auto EmptyImage = EmptyCell.Render(Camera, Fresh);
        for (auto C : EmptyImage.Pixels)
            Near(C.R + C.G + C.B, 0);
        ReferenceRenderer Emission;
        auto Glowing = Scene();
        for (auto& Light : Glowing.Lights)
            Light.Active = false;
        Glowing.Meshes[0].Emissive = {.1, .2, .3};
        Emission.SetScene(Glowing);
        auto GlowImage = Emission.Render(Camera, Settings);
        double GlowEnergy = 0;
        for (auto C : GlowImage.Pixels)
            GlowEnergy += C.R + C.G + C.B;
        Near(GlowEnergy, .6 * GlowImage.Stats.SurfacePixels);
        Settings.LightCellCapacity = 16;
        Settings.ExhaustiveLights = false;
        Settings.RisCandidates = 8;
        double Mean = 0, Variance = 0;
        const uint32_t Trials = 1024;
        for (uint32_t I = 0; I < Trials; ++I)
        {
            Settings.Seed = I + 1;
            auto Image = Renderer.Render(Camera, Settings);
            double Energy = 0;
            for (auto C : Image.Pixels)
                Energy += C.R + C.G + C.B;
            Mean += Energy;
            Variance += Energy * Energy;
        }
        Mean /= Trials;
        Variance = std::max(0., Variance / Trials - Mean * Mean);
        double Exact = 0;
        for (auto C : All.Pixels)
            Exact += C.R + C.G + C.B;
        Require(std::abs(Mean - Exact) < 6 * std::sqrt(Variance / Trials) + 1e-9,
                "Fresh clustered RIS energy differs from exhaustive sum");
        Settings.SpatialReuse = true;
        Settings.AccumulationFrames = 4;
        auto Reused = Renderer.Render(Camera, Settings);
        for (auto C : Reused.Pixels)
            Require(std::isfinite(C.R) && std::isfinite(C.G) && std::isfinite(C.B), "Nonfinite reuse output");
        std::cout << "PASS Type-C IES interpolation/symmetry/rejections; spot/range/orientation checks\n"
                  << "PASS 10000 independent query probes, " << Checks
                  << " affecting-light checks, 97-light overflow/outside fallback\n"
                  << "PASS native exact images: global visits=" << All.Stats.LightListVisits
                  << " local=" << Cluster.Stats.LightListVisits << "\n"
                  << "PASS 1024 fresh clustered RIS trials: mean=" << Mean << " exhaustive=" << Exact
                  << " standard_error=" << std::sqrt(Variance / Trials) << "\n"
                  << "PASS emissive radiance survives inactive analytic proxies\n"
                  << "PASS four-frame temporal/spatial reuse finite; NOT a motion correctness proof\n";
    }
    catch (const std::exception& Error)
    {
        std::cerr << Error.what() << '\n';
        return 1;
    }
}
