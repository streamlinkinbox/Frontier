/*==========================================================================================================================================
                                                           BAKERASTERIZATION.H
==========================================================================================================================================*/
// 🧩 Borrowed-device Vulkan compute bake: staged device-local BVH residence, banded BakeFieldCompute dispatches
//    and harvested per-texel fields, under the same submission-serial discipline as VectorCoverageRasterization.

#pragma once
#include "Engine/ContentInterchange/BakeField.h"
#include "SurfaceRasterization.h"
#include <filesystem>
#include <memory>
#include <string>

namespace Frontier
{
// 📝 One bake at a time flows through three host-visible steps across ordinary frame submissions:
//    ConfigureRequest retains the flat request; ConstructCommands uploads (once) and dispatches one texel band
//    per submission, harvesting the previous band once its serial completes; QueryOutcome reports progress and
//    finally hands over every target's evaluated fields for CPU map encoding. No queue waits, no fences of its
//    own — the presentation owner serializes the single queue exactly as for the coverage units.
// ⚠️ Any Vulkan compute queue serves (the traversal runs on ordinary shader cores, GTX included); capability is
//    verified at Initialize and per request, and every refusal surfaces through BakeGpuOutcome::Failure so the
//    caller can fall back to its CPU route instead of guessing.
class BakeRasterization
{
public:
    BakeRasterization();
    ~BakeRasterization();
    BakeRasterization(const BakeRasterization &) = delete;
    BakeRasterization &operator=(const BakeRasterization &) = delete;

    // [px] - Texels per dispatch: bounds one submission's traversal work well inside the Windows GPU watchdog
    // (the same concern VectorCoverageRasterization::SelectBakeExtent budgets for), whatever the bake resolution.
    static constexpr uint32_t BandTexels = 262144;

    void Initialize(const VulkanRasterAccess &Access, RasterShader Shader);
    void Initialize(const VulkanRasterAccess &Access, const std::filesystem::path &ShaderArchive);
    // 📝 Retains the request for banded dispatch. Returns false only when another bake still owns the unit or
    // the unit is faulted/uninitialized; device-capability refusals are accepted and reported as a failed
    // outcome so the caller learns the reason through the same poll it already performs.
    bool ConfigureRequest(std::shared_ptr<const BakeFieldRequest> Request);
    bool ConstructCommands(VkCommandBuffer Commands, uint64_t CompletedSerial, uint64_t SubmissionSerial);
    void ConfirmSubmission() noexcept;
    // 📝 Records a host-side dispatch failure (the frame callback's catch site) as the active bake's outcome;
    // the unit stays faulted for later ConfigureRequest calls until Finalize, like the coverage units' latches.
    void ConfigureFaulted(std::string Failure) noexcept;
    [[nodiscard]] BakeGpuOutcome QueryOutcome();
    [[nodiscard]] bool QueryReady() const noexcept;
    [[nodiscard]] const std::string &QueryDeviceName() const noexcept;
    [[nodiscard]] uint64_t QueryDispatchCount() const noexcept;
    // ⚠️ Borrowed-device ownership: retire every GPU reference before Finalize or destruction; neither inserts queue waits.
    void Finalize() noexcept;

private:
    struct BakeAllocation;
    std::unique_ptr<BakeAllocation> Allocation;
    std::string DeviceName;
};
} // namespace Frontier
