# 🧩 Reuse exact profile sampling and Vulkan submission with GPU-resident area/CDF preparation.
from pathlib import Path
import subprocess
import sys

Code = Path(__file__).resolve().parent
Build = Path(sys.argv[1])
subprocess.run([sys.executable, str(Code / "FacetProfileGpuSources.py"), str(Build)], check=True)
PrefixPath = Build / "FacetPacketGpuSubmission.h"
Prefix = PrefixPath.read_text()
Helpers = """
std::vector<LightComputeReceiver> CaptureCdfInputs(const LightComputeArchive& Scene)
{
    std::vector<LightComputeReceiver> Rows;
    if (Scene.Receivers.empty()) return Rows;
    Rows.push_back(Scene.Receivers[0]);
    uint32_t Start;
    std::memcpy(&Start, &Scene.Receivers[0].PositionHit[2], 4);
    if (Start <= Scene.Receivers.size())
        Rows.insert(Rows.end(), Scene.Receivers.begin() + Start, Scene.Receivers.end());
    return Rows;
}
bool CdfInputsEqual(const std::vector<LightComputeReceiver>& First,
                    const std::vector<LightComputeReceiver>& Second)
{
    return First.size() == Second.size() && (First.empty() ||
        std::memcmp(First.data(), Second.data(), First.size() * sizeof(LightComputeReceiver)) == 0);
}

"""
Needle = "struct LightComputeSubmission"
assert Prefix.count(Needle) == 1
Prefix = Prefix.replace(Needle, Helpers + Needle)
Changes = {
    "VkPipeline DiffuseGeneratePipeline = VK_NULL_HANDLE, DiffuseResolvePipeline = VK_NULL_HANDLE;": "VkPipeline DiffuseGeneratePipeline = VK_NULL_HANDLE, DiffuseResolvePipeline = VK_NULL_HANDLE;\n    VkPipeline CdfPreparationPipeline = VK_NULL_HANDLE;\n    bool CdfPrepared = false;\n    std::vector<LightComputeReceiver> CdfProfileRows;",
    "if (DiffuseResolvePipeline) DestroyPipeline(Device, DiffuseResolvePipeline, nullptr);": "if (CdfPreparationPipeline) DestroyPipeline(Device, CdfPreparationPipeline, nullptr);\n            if (DiffuseResolvePipeline) DestroyPipeline(Device, DiffuseResolvePipeline, nullptr);",
    "ConstructBuffer(5, nullptr, 256 * 17 * 4);": """ConstructBuffer(5, nullptr, DiffuseOnly ? (Scene.Surfaces.size() + 5) * 16 : 256 * 17 * 4);
        if (DiffuseOnly)
        {
            auto* Cache = static_cast<uint32_t*>(Buffers[5].Address);
            std::fill(Cache, Cache + (Scene.Surfaces.size() + 5) * 4, 0x5a5a5a5au);
            Cache[2] = Cache[9] = Cache[10] = 0;
        }""",
    'DiffuseResolvePipeline = ConstructPipeline(Shaders + "/DiffuseResolve.spv");': 'DiffuseResolvePipeline = ConstructPipeline(Shaders + "/DiffuseResolve.spv");\n            CdfPreparationPipeline = ConstructPipeline(Shaders + "/CdfPrepare.spv");',
}
for Before, After in Changes.items():
    assert Prefix.count(Before) == 1, Before
    Prefix = Prefix.replace(Before, After)
Begin = Prefix.index("    std::vector<std::array<float, 4>> ExecuteDiffuse(")
End = Prefix.index("\n    std::vector<std::array<float, 4>>", Begin + 10)
Method = Prefix[Begin:End]
Before = "        Upload(Scene);\n        LastDiffuseReset"
After = """        const auto Inputs = CaptureCdfInputs(Scene);
        const bool PrepareCdf = !CdfPrepared || Changed || !CdfInputsEqual(Inputs, CdfProfileRows);
        CdfProfileRows = Inputs;
        Upload(Scene);
        LastDiffuseReset"""
assert Method.count(Before) == 1
Method = Method.replace(Before, After)
Before = "        CmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, DiffuseGeneratePipeline);"
After = """        if (PrepareCdf)
        {
            CmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, CdfPreparationPipeline);
            CmdDispatch(Commands, 1, 1, 1);
            Barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            CmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               0, 1, &Barrier, 0, nullptr, 0, nullptr);
        }
""" + Before
assert Method.count(Before) == 1
Method = Method.replace(Before, After)
Before = "        HistoryDirty = false;"
assert Method.count(Before) == 1
Method = Method.replace(Before, Before + "\n        CdfPrepared = true;")
PrefixPath.write_text(Prefix[:Begin] + Method + Prefix[End:])
Fixture = (Code / "FacetProfileGpuValidation.cpp").read_text()
Fixture = Fixture[: Fixture.index("void ValidateProfileUnits()")]
(Build / "FacetCdfGpuComparisons.h").write_text(Fixture + "\n}\n")
Original = (Build / "FacetProfileGpu.slang").read_text()
Source = Original
Before = """    float Total = 0;
    for (uint Ordinal = 0; Ordinal < FacetProfileCount(); ++Ordinal)
        Total += ReservoirFacetArea(PacketSourceIndex(Ordinal));
"""
assert Source.count(Before) == 1
Source = Source.replace(Before, "")
Before = """        float Cumulative = 0;
        for (uint Ordinal = 0; Ordinal < FacetProfileCount(); ++Ordinal)
        {
            Cumulative += ReservoirFacetArea(PacketSourceIndex(Ordinal)) / Total;
            if (Choice < Cumulative)"""
After = """        for (uint Ordinal = 0; Ordinal < FacetProfileCount(); ++Ordinal)
        {
            if (Choice < asfloat(Cells[16 + 4 * Ordinal + 1]))"""
assert Source.count(Before) == 1
Source = Source.replace(Before, After)
Before = "Candidate.Index = PacketSourceIndex(Chosen);"
assert Source.count(Before) == 1
Source = Source.replace(Before, "Candidate.Index = Cells[16 + 4 * Chosen + 3];")
Before = "Candidate.Density = Measure == 0 ? 1.0 / float(FacetProfileCount()) / ReservoirFacetArea(Candidate.Index) : 1.0 / Total;"
assert Source.count(Before) == 1
Source = Source.replace(
    Before, "Candidate.Density = Measure == 0 ? asfloat(Cells[16 + 4 * Chosen + 2]) : asfloat(Cells[5]);"
)
Before = "float Area = ReservoirFacetArea(Index);"
assert Source.count(Before) == 1
Source = Source.replace(
    Before,
    """float Area = CountCdfArea(Index);
        Cells[16 + 4 * Ordinal] = asuint(Area);
        Cells[16 + 4 * Ordinal + 3] = Index;""",
)
Before = "float Area = ReservoirFacetArea(PacketSourceIndex(Ordinal));"
assert Source.count(Before) == 1
Source = Source.replace(Before, "float Area = asfloat(Cells[16 + 4 * Ordinal]);")
Before = "        Boundary = Next;"
assert Source.count(Before) == 1
Source = Source.replace(
    Before,
    """        Cells[16 + 4 * Ordinal + 1] = asuint(Next);
        Cells[16 + 4 * Ordinal + 2] = asuint(Density);
        Boundary = Next;""",
)
Before = "    float Boundary = 0;"
assert Source.count(Before) == 1
Source = Source.replace(Before, "    Cells[4] = asuint(Total);\n    Cells[5] = asuint(1.0 / Total);\n" + Before)
Before = "    uint Rejection = ValidateFacetProfile();"
assert Source.count(Before) == 1
Source = Source.replace(
    Before,
    """    if (Pixel == 0) Cells[9] += 1;
    uint Rejection = Cells[0];
    if (Cells[1] != FacetProfileCount()) Rejection = 1;""",
)
Counter = """
float CountCdfArea(uint Index)
{
    InterlockedAdd(Counters[20], 1);
    Cells[3] += 1;
    Cells[10] += 1;
    return ReservoirFacetArea(Index);
}

"""
Needle = "uint ValidateFacetProfile()"
assert Source.count(Needle) == 1
Source = Source.replace(Needle, Counter + Needle)
Source += """
[shader("compute")][numthreads(1, 1, 1)] void PrepareFacetCdf(uint3 Dispatch : SV_DispatchThreadID)
{
    if (Dispatch.x != 0) return;
    uint Runs = Cells[2], Samples = Cells[9], Areas = Cells[10];
    for (uint Index = 0; Index < (Extent.Sampling.z + 4) * 4; ++Index) Cells[Index] = 0;
    Cells[1] = FacetProfileCount();
    Cells[2] = Runs + 1;
    Cells[6] = Extent.Sampling.z;
    Cells[8] = 1;
    Cells[9] = Samples;
    Cells[10] = Areas;
    Cells[0] = ValidateFacetProfile();
}
"""
Source = Source.replace("GenerateFacetProfile", "GenerateFacetCdf").replace("ResolveFacetProfile", "ResolveFacetCdf")
(Build / "FacetCdfGpu.slang").write_text(Source)
Baseline = Original.replace("ReservoirFacetArea(", "CountUncachedArea(")
Needle = '#include "GeometricAreaReservoirGpu.slang"'
Baseline = Baseline.replace(
    Needle,
    Needle + """
float CountUncachedArea(uint Index)
{
    InterlockedAdd(Counters[20], 1);
    return ReservoirFacetArea(Index);
}
""",
)
Baseline += (
    '\n[shader("compute")][numthreads(1, 1, 1)] void PrepareUncachedCdf(uint3 Dispatch : SV_DispatchThreadID) {}\n'
)
(Build / "FacetCdfUncached.slang").write_text(Baseline)
assert (
    "ReservoirFacetArea("
    not in Source[Source.index("AreaCandidate ProposePacketCandidate") : Source.index("float CountCdfArea")]
)
print("CDF_GPU_SOURCES cache_binding=5 dynamic_quartets=geometry+5 preparation_barrier=1 baseline_counter=20")
