/*==========================================================================================================================================
                                                       POLESHADOWVALIDATION.H
==========================================================================================================================================*/
// 🧩 Compare complete pole shadows against an independent analytic box interval, across light types and receiver views.

//------------------------------------------------------------------------------------------------------------------------
//                                                     ANALYTIC SHADOW COVERAGE
//------------------------------------------------------------------------------------------------------------------------

void ValidatePoleShadows(const char* Driver, const std::string& Shaders, const std::string& Output)
{
    using namespace TransportReference;
    std::filesystem::create_directories(Output);
    LightComputeArchive Scene;
    auto Materials = AppendSceneMaterials(Scene, Shaders);
    // A six metre closed square pole; no primary-view culling or shadow-map coverage boundary.
    MaterialRectangle(Scene, {-.12, 0, 3}, {0, .12, 0}, {0, 0, 3}, 0, 700);
    MaterialRectangle(Scene, {.12, 0, 3}, {0, .12, 0}, {0, 0, 3}, 0, 700);
    MaterialRectangle(Scene, {0, -.12, 3}, {.12, 0, 0}, {0, 0, 3}, 0, 700);
    MaterialRectangle(Scene, {0, .12, 3}, {.12, 0, 0}, {0, 0, 3}, 0, 700);
    MaterialRectangle(Scene, {0, 0, 6}, {.12, 0, 0}, {0, .12, 0}, 0, 700);
    MaterialRectangle(Scene, {0, 0, 0}, {.12, 0, 0}, {0, .12, 0}, 0, 700);
    RebuildMaterialVisibility(Scene);
    Scene.Width = 384; Scene.Height = 128; Scene.Receivers.resize(Scene.Width * Scene.Height);
    for (uint32_t Y = 0; Y < Scene.Height; ++Y)
        for (uint32_t X = 0; X < Scene.Width; ++X)
        {
            auto& R = Scene.Receivers[Y * Scene.Width + X];
            R.PositionHit = {float(-15 + 19. * (X + .5) / Scene.Width), float(-4 + 8. * (Y + .5) / Scene.Height), 0, 1};
            R.NormalRoughness = {0, 0, 1, .6f}; R.ViewMetallic = {0, 0, 1, 0};
            R.AlbedoIor = {.8f, .8f, .8f, 1.5f}; R.ResidualSpecular[3] = .5f;
        }
    LightComputeShape Light;
    Light.PeakActive = {1, 1, 0, 0};
    Scene.Lights.push_back(Light);
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Scene, Shaders);
    std::cout << "POLE_DEVICE name=" << Compute.Properties.deviceName << " type=" << Compute.Properties.deviceType << std::endl;
    uint64_t Probes = 0, Shadowed = 0, FarHalf = 0;
    uint32_t Cases = 0;
    for (uint32_t Authored = 0; Authored < 2; ++Authored)
        for (uint32_t Category = 0; Category < 3; ++Category)
            for (uint32_t View = 0; View < 3; ++View)
                for (uint32_t Mode : {0u, 1u, 2u, 3u})
                {
                    for (auto& F : Scene.Surfaces) F.Emissive[3] = Authored ? float(Materials[3]) : 0;
                    auto& L = Scene.Lights[0];
                    auto Sun = Unit({1, .15, .5});
                    L.PositionReach = Category == 0 ? std::array<float, 4>{float(Sun.X), float(Sun.Y), float(Sun.Z), 0} : std::array<float, 4>{4, 1, 8, 100};
                    L.RadianceCategory = {Category == 0 ? 3.f : 500.f, Category == 0 ? 3.f : 500.f, Category == 0 ? 3.f : 500.f, float(Category)};
                    auto Forward = Unit({-4, -1, -8});
                    L.DirectionInner = {float(Forward.X), float(Forward.Y), float(Forward.Z), .99f}; L.UpOuter = {0, 1, 0, -1};
                    auto V = Unit({double(int(View) - 1), -.25, 1});
                    for (auto& R : Scene.Receivers) R.ViewMetallic = {float(V.X), float(V.Y), float(V.Z), 0};
                    auto Pixels = Compute.Execute(Scene, Mode, 16, 71, 64, 0, true);
                    uint32_t Blocked = 0, Distant = 0;
                    for (size_t I = 0; I < Pixels.size(); ++I)
                    {
                        const auto& R = Scene.Receivers[I];
                        SpatialVector O{R.PositionHit[0], R.PositionHit[1], .002};
                        auto Delta = Category == 0 ? Vector(L.PositionReach) : Sub(Vector(L.PositionReach), Vector(R.PositionHit));
                        double Length = std::sqrt(Dot(Delta, Delta));
                        auto D = Category == 0 ? Delta : Mul(Delta, 1 / Length);
                        double Near = 0, Far = Category == 0 ? 1e30 : Length - .004;
                        for (uint32_t Axis = 0; Axis < 3; ++Axis)
                        {
                            double P = Axis == 0 ? O.X : Axis == 1 ? O.Y : O.Z;
                            double Q = Axis == 0 ? D.X : Axis == 1 ? D.Y : D.Z;
                            double Low = Axis == 2 ? 0 : -.12, High = Axis == 2 ? 6 : .12;
                            if (std::abs(Q) < 1e-15) { if (P < Low || P > High) Far = -1; }
                            else { double A = (Low - P) / Q, B = (High - P) / Q; Near = std::max(Near, std::min(A, B)); Far = std::min(Far, std::max(A, B)); }
                        }
                        bool Expected = Far >= Near;
                        bool Actual = Pixels[I][0] == 0 && Pixels[I][1] == 0 && Pixels[I][2] == 0;
                        Require(Expected == Actual, "Pole shadow differs from analytic full-height box silhouette");
                        ++Probes; Shadowed += Expected; Blocked += Expected;
                        if (Expected && O.X < -6) { ++FarHalf; ++Distant; }
                    }
                    Require(Blocked > 0 && Distant > 0, "Pole far-half shadow not exercised");
                    if (View == 0 && Mode == 0)
                        WriteImage(Output + "/PoleShadow" + std::to_string(Authored) + std::to_string(Category) + ".ppm", Pixels, Scene.Width, Scene.Height);
                    ++Cases;
                    std::cout << "POLE_CASE authored=" << Authored << " light=" << Category << " view=" << View << " selection_mode=" << Mode << " shadow_pixels=" << Blocked << " far_half_pixels=" << Distant << " mismatches=0" << std::endl;
                }
    std::cout << "POLE_SUMMARY cases=" << Cases << " probes=" << Probes << " shadow_probes=" << Shadowed << " far_half_probes=" << FarHalf << " mismatches=0 user_report_reproduced=0" << std::endl;
}
