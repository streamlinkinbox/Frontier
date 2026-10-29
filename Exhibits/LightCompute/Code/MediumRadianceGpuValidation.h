/*==========================================================================================================================================
                                             MEDIUMRADIANCEGPUVALIDATION.H
==========================================================================================================================================*/
// 🧩 Exercise Engine payloads on a small sky-only Vulkan scene with independent homogeneous-medium solutions.

void ValidateMediumRadianceGpu(const char* Driver, const std::string& Shaders)
{
    constexpr uint32_t CubeSize = 2;
    const std::array<float, 3> Sky{.5f, .25f, .125f};
    LightComputeArchive Scene;
    Scene.Width = Scene.Height = 1;
    Scene.Receivers.resize(1);
    for (uint32_t Texel = 0; Texel < 6 * CubeSize * CubeSize; ++Texel)
        Scene.Angles.insert(Scene.Angles.end(), Sky.begin(), Sky.end());
    Scene.Camera[0] = {0, 0, 0, .001f};
    Scene.Camera[1] = {0, 0, 1, 1000};
    Scene.Camera[2] = {1, 0, 0, 1};
    Scene.Camera[3] = {0, 1, 0, 1};
    for (uint32_t Index = 0; Index < 4; ++Index)
        Scene.Camera[Index + 4] = Scene.Camera[Index];
    Scene.Camera[10] = {0, float(CubeSize), 0, 1};

    CloudRadianceSpecification Cloud;
    Cloud.Medium.BaseHeightM = 10;
    Cloud.Medium.TopHeightM = 20;
    Cloud.Medium.UniformDensityOverride = .125;
    Cloud.Medium.ExtinctionPerM = .01;
    Cloud.Medium.AmbientRadiance = {.2, .1, .05};
    Cloud.MaximumDistanceM = 1000;
    Cloud.ViewSamples = 16;
    Cloud.SolarShadowSamples = 0;
    const auto CloudPayload = CompileCloudRadiancePayload(Cloud);
    const uint32_t CloudOffset = uint32_t(Scene.Angles.size());
    Scene.Angles.insert(Scene.Angles.end(), CloudPayload.begin(), CloudPayload.end());

    FogRadianceSpecification Fog;
    Fog.Medium.DensityAtBase = .02;
    Fog.Medium.BaseHeightM = 100;
    Fog.Medium.FalloffHeightM = 25;
    Fog.ScatteringAlbedo = .8;
    Fog.MaximumViewDistanceM = 30;
    Fog.ViewSamples = 16;
    Fog.AmbientRadiance = {.3, .15, .075};
    Fog.GeometryShadowsEnabled = false;
    Fog.CloudShadowsEnabled = false;
    Fog.AnalyticLightsEnabled = false;
    const auto FogPayload = CompileFogRadiancePayload(Fog);
    const uint32_t FogOffset = uint32_t(Scene.Angles.size());
    Scene.Angles.insert(Scene.Angles.end(), FogPayload.begin(), FogPayload.end());

    LightComputeSubmission Compute(Driver);
    std::cout << "MEDIUM_GPU_DEVICE name=" << Compute.Properties.deviceName
              << " type=" << Compute.Properties.deviceType << std::endl;
    Compute.Prepare(Scene, Shaders, false, false, false, false, false, true);
    auto Dispatch = [&](uint32_t CloudAddress, uint32_t FogAddress)
    {
        return Compute.Execute(Scene, 0, 16, 71, 64, 0, true, true, 0, true, false, false,
                               CloudAddress, FogAddress)[0];
    };
    const auto Clear = Dispatch(UINT32_MAX, UINT32_MAX);
    const auto CloudImage = Dispatch(CloudOffset, UINT32_MAX);
    const auto Cube = Compute.CloudCube(CubeSize);
    const auto FogImage = Dispatch(UINT32_MAX, FogOffset);
    const auto BothImage = Dispatch(CloudOffset, FogOffset);

    const double CloudVerticalT = std::exp(-double(CloudPayload[4]) * CloudPayload[7] * CloudPayload[3] *
                                           (double(CloudPayload[1]) - CloudPayload[0]));
    const double FogT = std::exp(-double(FogPayload[0]) * FogPayload[5]);
    double CloudError = 0, CubeError = 0, FogError = 0, CombinedError = 0, ClearError = 0;
    for (uint32_t Channel = 0; Channel < 3; ++Channel)
    {
        const double CloudValue = CloudPayload[24 + Channel] * (1 - CloudVerticalT) + CloudVerticalT * Sky[Channel];
        const double FogSource = double(FogPayload[3]) * FogPayload[8 + Channel] * (1 - FogT);
        ClearError = std::max(ClearError, std::abs(double(Clear[Channel]) - Sky[Channel]));
        CloudError = std::max(CloudError, std::abs(double(CloudImage[Channel]) - CloudValue));
        FogError = std::max(FogError, std::abs(double(FogImage[Channel]) - (Sky[Channel] * FogT + FogSource)));
        CombinedError = std::max(CombinedError, std::abs(double(BothImage[Channel]) - (CloudValue * FogT + FogSource)));
    }
    for (uint32_t Index = 0; Index < Cube.size(); ++Index)
    {
        const uint32_t Face = Index / (CubeSize * CubeSize), X = Index % CubeSize, Y = (Index / CubeSize) % CubeSize;
        const double U = 2 * (X + .5) / CubeSize - 1, V = 2 * (Y + .5) / CubeSize - 1;
        const double Z = (Face == 4 ? 1 : Face == 5 ? -1 : V) / std::sqrt(1 + U * U + V * V);
        const double Length = Z > 0 ? (double(CloudPayload[1]) - CloudPayload[0]) / Z : 0;
        const double T = std::exp(-double(CloudPayload[4]) * CloudPayload[7] * CloudPayload[3] * Length);
        CubeError = std::max(CubeError, std::abs(double(Cube[Index][3]) - T));
        for (uint32_t Channel = 0; Channel < 3; ++Channel)
        {
            const double Expected = CloudPayload[24 + Channel] * (1 - T) + Sky[Channel] * T;
            CubeError = std::max(CubeError, std::abs(double(Cube[Index][Channel]) - Expected));
        }
    }
    Require(ClearError < 1e-6 && CloudError < 1e-5 && CubeError < 1e-5 && FogError < 1e-5 && CombinedError < 1e-5,
            "Engine cloud/fog Vulkan payload disagrees with closed-form homogeneous medium");
    uint32_t Rejected = 0;
    for (uint32_t Case = 0; Case < 2; ++Case)
    {
        auto Invalid = Scene;
        if (Case == 0) Invalid.Angles[CloudOffset + 11] = 129;
        else Invalid.Angles[FogOffset + 6] = 0;
        try
        {
            (void)Compute.Execute(Invalid, 0, 16, 71, 64, 0, true, true, 0, true, false, false,
                                  CloudOffset, FogOffset);
        }
        catch (const std::runtime_error&)
        {
            ++Rejected;
        }
    }
    Require(Rejected == 2, "Malformed medium payload reached Vulkan dispatch");
    std::cout << "MEDIUM_GPU homogeneous_cube_texels=" << Cube.size() << " clear_error=" << ClearError
              << " cloud_camera_error=" << CloudError << " cloud_cube_error=" << CubeError
              << " fog_error=" << FogError << " combined_error=" << CombinedError
              << " malformed_payloads_rejected=" << Rejected << "\n";
}
