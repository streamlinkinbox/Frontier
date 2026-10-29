/*==========================================================================================================================================
                                           REPLAYMATERIALSAMPLINGVALIDATION.H
==========================================================================================================================================*/
// 🧩 Validate authored replay scattering against independent double-precision hemispherical integration.
#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                            MATERIAL FIXTURE SPECIFICATIONS
//------------------------------------------------------------------------------------------------------------------------

struct ReplayMaterialSamplingSpecification
{
    const char* Label;
    uint32_t StrataCount;
    float NormalView;
    float Albedo;
    float Roughness;
    float Metallic;
    float Ior;
    float Coverage;
    float CoatRoughness;
    float CoatCoverage;
    float SheenCoverage;
    double ReportingScale;
};

LightComputeArchive ConstructReplayMaterialFixture(uint32_t Size, const ReplayMaterialSamplingSpecification& Specification)
{
    LightComputeArchive Scene{};
    Scene.Width = Size;
    Scene.Height = Size;
    Scene.Angles.resize(268u, 0.0f);
    Scene.Angles[0] = float(Specification.StrataCount);
    Scene.Angles[1] = 0;
    Scene.Angles[4] = Scene.Angles[5] = 1;
    float* Surface = Scene.Angles.data() + 8u;
    Surface[0] = Surface[1] = Surface[2] = Specification.Albedo;
    Surface[3] = Specification.Roughness;
    Surface[4] = Specification.Metallic;
    Surface[5] = Specification.Ior;
    Surface[6] = Specification.Coverage;
    Surface[16] = Surface[17] = Surface[18] = .8f;
    Surface[19] = Specification.SheenCoverage;
    Surface[20] = .6f;
    for (uint32_t Layer = 1u; Layer < Specification.StrataCount; ++Layer)
    {
        float* Coating = Surface + Layer * 24u;
        Coating[0] = Coating[1] = Coating[2] = 1;
        Coating[3] = Specification.CoatRoughness;
        Coating[5] = 1.52f;
        Coating[6] = Specification.CoatCoverage;
        Coating[8] = Coating[9] = Coating[10] = .3f;
        Coating[11] = .08f;
        Coating[12] = 1;
    }
    LightComputeReceiver Receiver{};
    Receiver.PositionHit = {0, 0, 0, 1};
    Receiver.NormalRoughness = {0, 0, 1, Specification.Roughness};
    Receiver.ViewMetallic = {std::sqrt(1.0f - Specification.NormalView * Specification.NormalView), 0,
                             Specification.NormalView, Specification.Metallic};
    Receiver.AlbedoIor = {Specification.Albedo, Specification.Albedo, Specification.Albedo, Specification.Ior};
    Receiver.ResidualSpecular = {0, 0, 0, .5f};
    Receiver.ExpectedDirect = {0, 0, 0, 1};
    Receiver.MotionPositionToken = {0, 0, 0, 1};
    Receiver.MotionNormal = {0, 0, 1, 0};
    Receiver.MotionPixel = {0, 0, .1f, 0};
    Scene.Receivers.assign(size_t(Size) * Size, Receiver);
    Scene.Camera[3][3] = .5f;
    Scene.Camera[11] = {1, 0, 1, 0};
    return Scene;
}

//------------------------------------------------------------------------------------------------------------------------
//                                       INDEPENDENT DOUBLE HEMISPHERE INTEGRAL
//------------------------------------------------------------------------------------------------------------------------

// 📝 This scalar reference is separate from the Slang implementation, sampler, RNG and visibility routines.
//     It integrates the same documented layered GGX/Charlie model, not an independent physical material model.
double EvaluateReplayMaterialReference(const LightComputeArchive& Scene, double NormalView, double NormalLight,
                                       double NormalHalf, double ViewHalf)
{
    constexpr double π = 3.14159265358979323846264338327950288;
    const auto& Receiver = Scene.Receivers[0];
    double Accumulated = 0;
    for (uint32_t Index = 0; Index < uint32_t(Scene.Angles[0]); ++Index)
    {
        const float* Surface = Scene.Angles.data() + 8u + Index * 24u;
        const double Albedo = Index == 0 ? double(Receiver.AlbedoIor[0]) : double(Surface[0]);
        const double Roughness = Index == 0 ? double(Receiver.NormalRoughness[3]) : double(Surface[3]);
        const double Metallic = Index == 0 ? double(Receiver.ViewMetallic[3]) : double(Surface[4]);
        const double Ior = Surface[5];
        const double Coverage = Surface[6];
        const double α = Roughness * Roughness, αSquared = α * α;
        const double Denominator = NormalHalf * NormalHalf * (αSquared - 1.0) + 1.0;
        const double Distribution = αSquared / (π * Denominator * Denominator);
        const double Visibility = .5 / std::max(1e-12,
            NormalLight * std::sqrt(NormalView * NormalView * (1.0 - αSquared) + αSquared) +
            NormalView * std::sqrt(NormalLight * NormalLight * (1.0 - αSquared) + αSquared));
        double Dielectric = (Ior - 1.0) / (Ior + 1.0);
        Dielectric *= Dielectric;
        const double FresnelOrigin = Dielectric * (1.0 - Metallic) + Albedo * Metallic;
        const double Fresnel = FresnelOrigin + (1.0 - FresnelOrigin) * std::pow(1.0 - ViewHalf, 5.0);
        double Scattering = Distribution * Visibility * Fresnel;
        if (Surface[12] == 0)
        {
            const double Entrance = FresnelOrigin + (1.0 - FresnelOrigin) * std::pow(1.0 - NormalLight, 5.0);
            const double Exit = FresnelOrigin + (1.0 - FresnelOrigin) * std::pow(1.0 - NormalView, 5.0);
            Scattering += (1.0 - Entrance) * (1.0 - Exit) * (1.0 - Metallic) * Albedo / π;
            if (Surface[19] > 0 && Surface[16] > 0)
            {
                const double SheenCoverage = double(Surface[16]) * double(Surface[19]);
                const double InverseAlpha = 1.0 / std::max(double(Surface[20]) * double(Surface[20]), 1e-3);
                const double Radius = std::sqrt(InverseAlpha / 8.0);
                const double Bound = (InverseAlpha + 2.0) * std::atan(Radius) / (4.0 * Radius);
                const double SheenDistribution = (2.0 + InverseAlpha) *
                    std::pow(std::max(1.0 - NormalHalf * NormalHalf, .0078125), InverseAlpha * .5) / (2.0 * π);
                const double SheenVisibility =
                    1.0 / std::max(1e-4, 4.0 * (NormalLight + NormalView - NormalLight * NormalView));
                const double Sheen = SheenDistribution * SheenVisibility / std::max(1.0, Bound);
                Scattering = Scattering * (1.0 - SheenCoverage) + Sheen * SheenCoverage;
            }
        }
        else
        {
            const double Entrance = Dielectric + (1.0 - Dielectric) * std::pow(1.0 - NormalLight, 5.0);
            const double Exit = Dielectric + (1.0 - Dielectric) * std::pow(1.0 - NormalView, 5.0);
            const double Absorption = std::exp(-double(Surface[8]) * double(Surface[11]) *
                (1.0 / std::max(.001, NormalView) + 1.0 / std::max(.001, NormalLight)));
            Scattering += Accumulated * (1.0 - Entrance) * (1.0 - Exit) * Absorption;
        }
        Accumulated += Coverage * (Scattering - Accumulated);
    }
    return Accumulated;
}

double IntegrateReplayMaterialReference(const LightComputeArchive& Scene, uint32_t Steps)
{
    constexpr double π = 3.14159265358979323846264338327950288;
    const double ViewX = Scene.Receivers[0].ViewMetallic[0];
    const double ViewZ = Scene.Receivers[0].ViewMetallic[2];
    const double ViewLength = std::sqrt(ViewX * ViewX + ViewZ * ViewZ);
    const double NormalView = ViewZ / ViewLength, TangentView = ViewX / ViewLength;
    double Integral = 0;
    for (uint32_t Row = 0; Row < Steps; ++Row)
    {
        const double μ = (double(Row) + .5) / double(Steps);
        const double Radius = std::sqrt(std::max(0.0, 1.0 - μ * μ));
        for (uint32_t Column = 0; Column < Steps * 2u; ++Column)
        {
            const double φ = 2.0 * π * (double(Column) + .5) / double(Steps * 2u);
            const double LightX = Radius * std::cos(φ), LightY = Radius * std::sin(φ);
            const double HalfX = TangentView + LightX, HalfY = LightY, HalfZ = NormalView + μ;
            const double HalfLength = std::sqrt(HalfX * HalfX + HalfY * HalfY + HalfZ * HalfZ);
            const double NormalHalf = HalfZ / HalfLength;
            const double ViewHalf = std::clamp((TangentView * HalfX + NormalView * HalfZ) / HalfLength, 0.0, 1.0);
            Integral += EvaluateReplayMaterialReference(Scene, NormalView, μ, NormalHalf, ViewHalf) * μ;
        }
    }
    return Integral * (π / (double(Steps) * double(Steps)));
}

//------------------------------------------------------------------------------------------------------------------------
//                                             VULKAN SAMPLE READBACK
//------------------------------------------------------------------------------------------------------------------------

struct ReplayMaterialBatchMoments
{
    double Sum = 0;
    double Squared = 0;
    uint32_t Count = 0;

    void Accumulate(double Sample)
    {
        Sum += Sample;
        Squared += Sample * Sample;
        ++Count;
    }
    double Mean() const { return Sum / double(Count); }
    double StandardError() const
    {
        const double Variance = (Squared - Sum * Sum / double(Count)) / double(Count - 1u);
        return std::sqrt(std::max(0.0, Variance) / double(Count));
    }
};

std::vector<std::array<float, 4>> EvaluateReplayMaterialSamplingGpu(LightComputeSubmission& Compute,
                                                                    uint32_t Size,
                                                                    uint32_t Seed)
{
    ComputeExtent Extent{};
    Extent.Dimensions[0] = Size * Size;
    Extent.Sampling[0] = Seed;
    Extent.Primary[3] = PathReplayRoughOpaque | PathReplayImportanceRough | PathReplayAuthoredMaterials;
    Confirm(Compute.ResetCommandPool(Compute.Device, Compute.Pool, 0));
    Confirm(Compute.ResetFences(Compute.Device, 1, &Compute.Completion));
    VkCommandBufferBeginInfo Begin{};
    Begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    Begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    Confirm(Compute.BeginCommandBuffer(Compute.Commands, &Begin));
    VkMemoryBarrier Barrier{};
    Barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    Barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    Compute.CmdPipelineBarrier(Compute.Commands, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               0, 1, &Barrier, 0, nullptr, 0, nullptr);
    Compute.CmdBindDescriptorSets(Compute.Commands, VK_PIPELINE_BIND_POINT_COMPUTE, Compute.PipelineLayout, 0, 1,
                                  &Compute.DescriptorSet, 0, nullptr);
    Compute.CmdPushConstants(Compute.Commands, Compute.PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                             0, sizeof(Extent), &Extent);
    Compute.CmdBindPipeline(Compute.Commands, VK_PIPELINE_BIND_POINT_COMPUTE, Compute.ReplayGeneratePipeline);
    Compute.CmdDispatch(Compute.Commands, (Extent.Dimensions[0] + 63u) / 64u, 1, 1);
    Barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    Barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    Compute.CmdPipelineBarrier(Compute.Commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                               0, 1, &Barrier, 0, nullptr, 0, nullptr);
    Confirm(Compute.EndCommandBuffer(Compute.Commands));
    VkSubmitInfo Submission{};
    Submission.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    Submission.commandBufferCount = 1;
    Submission.pCommandBuffers = &Compute.Commands;
    Confirm(Compute.QueueSubmit(Compute.Queue, 1, &Submission, Compute.Completion));
    Confirm(Compute.WaitForFences(Compute.Device, 1, &Compute.Completion, VK_TRUE, 600000000000ull));
    std::vector<std::array<float, 4>> Result(Extent.Dimensions[0]);
    std::memcpy(Result.data(), Compute.Buffers[6].Address, Result.size() * sizeof(Result[0]));
    return Result;
}

const std::array<ReplayMaterialSamplingSpecification, 11>& EnumerateReplayMaterialSpecifications()
{
    static const std::array<ReplayMaterialSamplingSpecification, 11> Specifications{{
        {"matte", 1, .95f, .62f, .65f, 0, 1.5f, 1, .3f, 0, 0, 1},
        {"metal", 1, .75f, .86f, .30f, 1, 1.5f, 1, .3f, 0, 0, 1},
        {"coat", 2, .80f, .30f, .46f, .25f, 1.5f, 1, .22f, .85f, 0, 1},
        {"sheen", 1, .45f, .20f, .80f, 0, 1.4f, 1, .3f, 0, .75f, 1},
        {"faint", 2, .75f, .60f, .40f, .30f, 1.5f, 1e-7f, .25f, 2e-7f, 0, 1e7},
        {"zero", 2, 1, .60f, .40f, .30f, 1.5f, 0, .25f, 0, 0, 1},
        {"coat_only", 2, .8f, .3f, .46f, .25f, 1.5f, 0, .22f, .85f, 0, 1},
        {"base_only", 2, .8f, .3f, .46f, .25f, 1.5f, 1, .22f, 0, 0, 1},
        {"faint_coat", 2, .8f, .3f, .46f, .25f, 1.5f, 0, .25f, 1e-7f, 0, 1e7},
        {"ultrafaint", 2, .75f, .60f, .40f, .30f, 1.5f, 1e-24f, .25f, 2e-24f, 0, 1e24},
        {"eight_coats", 8, .8f, .3f, .46f, .25f, 1.5f, 1, .3f, .2f, 0, 1},
    }};
    return Specifications;
}

void ValidateReplayMaterialSampling(const char* Driver, const std::string& Shaders)
{
    constexpr uint32_t Size = 64u, Batches = 32u;
    const auto& Specifications = EnumerateReplayMaterialSpecifications();
    const auto Capacity = ConstructReplayMaterialFixture(Size, Specifications[0]);
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Capacity, Shaders, false, false, false, false, true);
    bool WithinReference = true;
    for (const auto& Specification : Specifications)
    {
        const auto Scene = ConstructReplayMaterialFixture(Size, Specification);
        ValidatePathReplayDomain(ReplayDomainView(Scene), {false, true, false, true, false, false, false, true});
        const double Reference = IntegrateReplayMaterialReference(Scene, 1024u) * Specification.ReportingScale;
        const double Coarse = IntegrateReplayMaterialReference(Scene, 512u) * Specification.ReportingScale;
        Require(std::abs(Reference - Coarse) < .0002, "Material hemisphere quadrature did not converge");
        Require(Reference >= 0 && Reference <= Specification.ReportingScale,
                "Material hemisphere integral is outside the tested energy range");
        Compute.Upload(Scene, true);
        // 📝 ExpectedDirect.w is an authored-atlas address here; no native expected RGB enters the shader.
        std::memcpy(Compute.Buffers[1].Address, Scene.Receivers.data(),
                    Scene.Receivers.size() * sizeof(LightComputeReceiver));
        ReplayMaterialBatchMoments ImportanceMean, CosineMean, IntegrandMean, ImportanceMse, CosineMse;
        uint64_t Rejected = 0;
        double MaximumDensityError = 0;
        for (uint32_t Batch = 0; Batch < Batches; ++Batch)
        {
            const auto Picture = EvaluateReplayMaterialSamplingGpu(Compute, Size, 170003u + Batch * 7919u);
            double ImportanceSum = 0, CosineSum = 0, IntegrandSum = 0;
            double ImportanceSquared = 0, CosineSquared = 0;
            for (const auto& Pixel : Picture)
            {
                for (float Channel : Pixel) Require(std::isfinite(Channel), "Material sample is not finite");
                const double Importance = double(Pixel[0]) * Specification.ReportingScale;
                const double Cosine = double(Pixel[1]) * Specification.ReportingScale;
                Require(Importance >= 0 && Cosine >= 0 && Pixel[2] >= 0,
                        "Material sampler produced a negative estimate");
                ImportanceSum += Importance;
                CosineSum += Cosine;
                IntegrandSum += Pixel[2];
                ImportanceSquared += (Importance - Reference) * (Importance - Reference);
                CosineSquared += (Cosine - Reference) * (Cosine - Reference);
                if (Pixel[3] < 0) ++Rejected;
                else MaximumDensityError = std::max(MaximumDensityError, double(Pixel[3]));
            }
            const double InverseCount = 1.0 / double(Picture.size());
            ImportanceMean.Accumulate(ImportanceSum * InverseCount);
            CosineMean.Accumulate(CosineSum * InverseCount);
            IntegrandMean.Accumulate(IntegrandSum * InverseCount);
            ImportanceMse.Accumulate(ImportanceSquared * InverseCount);
            CosineMse.Accumulate(CosineSquared * InverseCount);
        }
        std::printf("REPLAY_MATERIAL case=%s reference=%.9g coarse_delta=%.3g importance=%.9g importance_se=%.3g "
                    "cosine=%.9g cosine_se=%.3g integrand=%.9g integrand_se=%.3g "
                    "importance_rmse=%.9g cosine_rmse=%.9g rejected=%llu pdf_delta=%.3g scale=%.3g "
                    "batches=%u samples_per_batch=%u\n",
                    Specification.Label, Reference, std::abs(Reference - Coarse), ImportanceMean.Mean(),
                    ImportanceMean.StandardError(), CosineMean.Mean(), CosineMean.StandardError(), IntegrandMean.Mean(),
                    IntegrandMean.StandardError(), std::sqrt(ImportanceMse.Mean()), std::sqrt(CosineMse.Mean()),
                    static_cast<unsigned long long>(Rejected), MaximumDensityError, Specification.ReportingScale,
                    Batches, Size * Size);
        std::fflush(stdout);
        WithinReference = WithinReference &&
            std::abs(ImportanceMean.Mean() - Reference) <= 5.0 * ImportanceMean.StandardError() + .001 &&
            std::abs(CosineMean.Mean() - Reference) <= 5.0 * CosineMean.StandardError() + .001 &&
            std::abs(IntegrandMean.Mean() - 1.0) <= 5.0 * IntegrandMean.StandardError() + .01 &&
            MaximumDensityError < 1e-5;
    }
    Require(WithinReference, "Authored material sampling differs from the independent hemisphere gates");
    std::printf("PASS ReplayMaterialSamplingValidation\n");
}
