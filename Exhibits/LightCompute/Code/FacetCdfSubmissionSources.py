# 🧩 Replace manual exhibit dispatches with maintained caller-owned CDF submission and fence publication.
from pathlib import Path
import subprocess
import sys

Code = Path(__file__).resolve().parent
Build = Path(sys.argv[1])
subprocess.run([sys.executable, str(Code / "FacetCdfGpuSources.py"), str(Build)], check=True)
Pathname = Build / "FacetPacketGpuSubmission.h"
Source = Pathname.read_text()
Source = '#include "Frontier/Engine/Rendering/FacetCdfSubmission.h"\n' + Source
Before = "    bool CdfPrepared = false;"
assert Source.count(Before) == 1
Source = Source.replace(Before, Before + "\n    FacetCdfSubmission CdfSubmission;\n    FacetCdfRevision CdfRevision;")
Before = "        const bool PrepareCdf = !CdfPrepared || Changed || !CdfInputsEqual(Inputs, CdfProfileRows);"
assert Source.count(Before) == 1
Source = Source.replace(
    Before,
    """        if (Changed) ++CdfRevision.Geometry;
        if (!CdfInputsEqual(Inputs, CdfProfileRows)) ++CdfRevision.Profile;""",
)
Begin = Source.index("    std::vector<std::array<float, 4>> ExecuteDiffuse(")
End = Source.index("\n    std::vector<std::array<float, 4>>", Begin + 10)
Method = Source[Begin:End]
First = Method.index("        CmdBindDescriptorSets(Commands,")
Last = Method.index("        Barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;", First)
Method = Method[:First] + """        FacetCdfVulkanAccess Access;
        Access.Device = Device;
        Access.Completion = Completion;
        Access.Layout = PipelineLayout;
        Access.Descriptors = DescriptorSet;
        Access.Preparation = CdfPreparationPipeline;
        Access.Generation = DiffuseGeneratePipeline;
        Access.Resolution = DiffuseResolvePipeline;
        Access.BindPipeline = CmdBindPipeline;
        Access.BindDescriptors = CmdBindDescriptorSets;
        Access.PushConstants = CmdPushConstants;
        Access.Dispatch = CmdDispatch;
        Access.Dependency = CmdPipelineBarrier;
        Access.QueryFence = Lookup<PFN_vkGetFenceStatus>("vkGetFenceStatus");
        Access.MaxGroupCount = Properties.limits.maxComputeWorkGroupCount[0];
        Access.MaxPushBytes = Properties.limits.maxPushConstantsSize;
        FacetCdfDescriptorRanges Ranges;
        Ranges.Receivers = Buffers[1].Size / sizeof(LightComputeReceiver);
        Ranges.Enclosures = Buffers[2].Size / sizeof(TriangleVisibilitySpace::FloatEnclosure);
        Ranges.Facets = Buffers[3].Size / sizeof(LightComputeFacet);
        Ranges.CacheWords = Buffers[5].Size / 4;
        Ranges.OutputQuartets = Buffers[6].Size / 16;
        Ranges.CounterWords = Buffers[7].Size / 4;
        CdfSubmission.Encode(Commands, Access, Ranges, Extent, CdfRevision);
        Barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
""" + Method[Last:]
Before = """        Confirm(EndCommandBuffer(Commands));
        VkSubmitInfo Submission{};
        Submission.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        Submission.commandBufferCount = 1;
        Submission.pCommandBuffers = &Commands;
        Confirm(QueueSubmit(Queue, 1, &Submission, Completion));
        Confirm(WaitForFences(Device, 1, &Completion, VK_TRUE, 600000000000ull));"""
assert Method.count(Before) == 1
Method = Method.replace(
    Before,
    """        try
        {
            Confirm(EndCommandBuffer(Commands));
            VkSubmitInfo Submission{};
            Submission.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            Submission.commandBufferCount = 1;
            Submission.pCommandBuffers = &Commands;
            Confirm(QueueSubmit(Queue, 1, &Submission, Completion));
        }
        catch (...)
        {
            CdfSubmission.Discard();
            throw;
        }
        CdfSubmission.Acknowledge();
        Confirm(WaitForFences(Device, 1, &Completion, VK_TRUE, 600000000000ull));
        Require(CdfSubmission.Resolve() && CdfSubmission.Prepared() && !CdfSubmission.Pending(),
                "CDF residency was not fence-published");""",
)
Pathname.write_text(Source[:Begin] + Method + Source[End:])
Host = (Code / "FacetCdfGpuValidation.cpp").read_text()
assert Host.count("int main(int Count, char **Arguments)") == 1
Host = Host.replace("int main(int Count, char **Arguments)", "int ExecuteCdfCorpus(int Count, char **Arguments)")
(Build / "FacetCdfSubmissionComparisons.h").write_text(Host)
print("CDF_SUBMISSION_SOURCES maintained_encoder=1 fence_publication=1 shader_changes=0 corpus_changes=0")
