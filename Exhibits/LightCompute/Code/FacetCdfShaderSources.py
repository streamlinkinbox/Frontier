# 🧩 Use owned CDF decoding/preparation/sampling while keeping fixture scores and transport in the exhibit.
from pathlib import Path
import subprocess
import sys

Code = Path(__file__).resolve().parent
Build = Path(sys.argv[1])
subprocess.run([sys.executable, str(Code / "FacetCdfSubmissionSources.py"), str(Build)], check=True)
Pathname = Build / "FacetCdfGpu.slang"
Source = Pathname.read_text()
Begin = Source.index("uint FacetProfileCount()")
End = Source.index('[shader("compute")][numthreads(64, 1, 1)] void GenerateFacetCdf')
Candidate = Source[
    Source.index("    SurfacePoint Source = ", Begin) : Source.index("float CountCdfArea", Begin)
].strip()
# Keep exactly the original fixture proxy and score,not a second generic sampler.
Wrapper = """AreaCandidate ProposePacketCandidate(
    uint Seed,
    uint Draw,
    uint Measure,
    float3 Receiver)
{
    FacetCdfSample Sample = ProposeFacetCdf(Seed, Draw, Measure);
    AreaCandidate Candidate = (AreaCandidate)0;
    Candidate.Index = Sample.Index;
    Candidate.U = Sample.U;
    Candidate.V = Sample.V;
    Candidate.Density = Sample.Density;
""" + "    " + Candidate + "\n\n"
Source = Source[:Begin] + '#include "FacetCdfProfile.slang"\n\n' + Wrapper + Source[End:]
Begin = Source.index('[shader("compute")][numthreads(1, 1, 1)] void PrepareFacetCdf')
Source = Source[:Begin]
Source = Source.replace("FacetProfileCameras()", "FacetCdfCameraCount()").replace(
    "FacetProfileCount()", "FacetCdfSourceCount()"
)
Pathname.write_text(Source)
Host = Build / "FacetCdfSubmissionComparisons.h"
Source = Host.read_text()
Before = """    Scene.Receivers[0].PositionHit = {EncodeCausalIdentity(Support), EncodeCausalIdentity(1024),
                                      EncodeCausalIdentity(Start), EncodeCausalIdentity(Capacity)};"""
assert Source.count(Before) == 1
Source = Source.replace(
    Before,
    """    const auto Header = ConstructFacetCdfHeader(Support, 1024);
    std::memcpy(Scene.Receivers[0].PositionHit.data(), &Header, sizeof(Header));""",
)
Host.write_text('#include "Frontier/Engine/Rendering/FacetCdfProfile.h"\n' + Source)
Validation = (Code / "FacetCdfSubmissionValidation.cpp").read_text()
assert Validation.count("int main(int Count, char **Arguments)") == 1
Validation = Validation.replace(
    "int main(int Count, char **Arguments)", "int ExecuteSubmissionCorpus(int Count, char **Arguments)"
)
(Build / "FacetCdfShaderComparisons.h").write_text(Validation)
print("CDF_SHADER_SOURCES owned_profile=1 owned_preparation=1 owned_sampling=1 fixture_score_retained=1 typed_header=1")
