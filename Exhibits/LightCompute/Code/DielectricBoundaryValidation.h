/*==========================================================================================================================================
                                                   DIELECTRICBOUNDARYVALIDATION.H
==========================================================================================================================================*/
// 🧩 Compare the production float boundary evaluator with independent double arithmetic and reciprocal Snell identities.
void ValidateDielectricBoundaries(const char* Driver, const std::string& Shaders)
{
    LightComputeArchive Scene;
    uint32_t Identities = 0;
    auto Append = [&](std::array<double, 3> I, std::array<double, 3> N, double A, double B) {
        auto R = EvaluateDielectricRefraction(I, N, A, B);
        if (R.Valid && !R.TotalInternalReflection)
        {
            std::array<double, 3> Reverse{}, BackNormal{};
            double Length = 0;
            for (uint32_t C = 0; C < 3; ++C)
            {
                Reverse[C] = -R.Direction[C]; BackNormal[C] = -N[C];
                Length += R.Direction[C] * R.Direction[C];
            }
            auto Back = EvaluateDielectricRefraction(Reverse, BackNormal, B, A);
            Require(Back.Valid && !Back.TotalInternalReflection, "Reciprocal dielectric boundary failed");
            Require(std::abs(Length - 1) < 1e-12, "Snell direction is not unit length");
            for (uint32_t C = 0; C < 3; ++C)
                Require(std::abs(Back.Direction[C] + I[C]) < 1e-10, "Snell reciprocity failed");
            Require(std::abs(Back.Reflectance - R.Reflectance) < 1e-10 &&
                    std::abs(Back.RadianceScale * R.RadianceScale - 1) < 1e-12, "Fresnel reciprocity failed");
            ++Identities;
        }
        Scene.Angles.insert(Scene.Angles.end(), {float(I[0]),float(I[1]),float(I[2]),float(A),
                                               float(N[0]),float(N[1]),float(N[2]),float(B)});
    };
    for (double Ior : {1.,1.1,1.33,1.5,2.4,3.})
        for (double Angle : {0.,.1,.4,.7,.9,1.2,1.4,1.55})
            for (uint32_t Reverse = 0; Reverse < 2; ++Reverse)
                for (uint32_t Axis = 0; Axis < 2; ++Axis)
                {
                    double S = std::sin(Angle), C = std::cos(Angle);
                    Append(Axis ? std::array<double,3>{-C,S,0} : std::array<double,3>{S,0,-C},
                           Axis ? std::array<double,3>{1,0,0} : std::array<double,3>{0,0,1}, Reverse ? Ior : 1, Reverse ? 1 : Ior);
                }
    for (double Delta : {-1e-4,1e-4})
    {
        double Angle = std::asin(1 / 1.5) + Delta;
        Append({std::sin(Angle),0,-std::cos(Angle)}, {0,0,1}, 1.5, 1);
    }
    Append({0,0,-1},{0,0,1},0,1.5);
    Append({0,0,-1},{0,0,1},1,-1);
    Append({0,0,-1},{0,0,0},1,1.5);
    Append({0,0,1},{0,0,1},1,1.5);
    Append({0,0,-2},{0,0,1},1,1.5);
    Scene.Width = uint32_t(Scene.Angles.size()/8); Scene.Height = 1;
    Scene.Receivers.resize(Scene.Width);
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Scene, Shaders);
    auto Actual = Compute.Execute(Scene,0);
    auto Metadata = Compute.TransportSlab(16);
    double Error = 0;
    uint32_t Tir = 0, Invalid = 0;
    for (uint32_t I = 0; I < Scene.Width; ++I)
    {
        size_t P = size_t(I)*8;
        auto R = EvaluateDielectricRefraction({Scene.Angles[P],Scene.Angles[P+1],Scene.Angles[P+2]},
                                             {Scene.Angles[P+4],Scene.Angles[P+5],Scene.Angles[P+6]},
                                             Scene.Angles[P+3],Scene.Angles[P+7]);
        Require(Metadata[I][1] == float(R.Valid) && Metadata[I][2] == float(R.TotalInternalReflection), "GPU dielectric flags differ");
        for (uint32_t C = 0; C < 4; ++C)
        {
            Require(std::isfinite(Actual[I][C]), "Nonfinite GPU dielectric output");
            Error = std::max(Error,std::abs(Actual[I][C] - (C < 3 ? R.Direction[C] : R.Reflectance)));
        }
        Error = std::max(Error,std::abs(Metadata[I][0] - R.RadianceScale));
        Tir += R.TotalInternalReflection; Invalid += !R.Valid;
    }
    std::cout << "DIELECTRIC_BOUNDARY device=" << Compute.Properties.deviceName << " type=" << Compute.Properties.deviceType
              << " cases=" << Scene.Width << " reciprocal_identities=" << Identities << " tir=" << Tir
              << " invalid=" << Invalid << " max_error=" << Error << std::endl;
    Require(Tir > 0 && Invalid == 5 && Error < .0002, "Dielectric GPU boundary parity failed");
}
