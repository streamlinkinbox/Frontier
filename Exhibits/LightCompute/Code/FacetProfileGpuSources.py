# 🧩 Extend the unchanged image packet source with runtime source-profile indexing and rejection checks.
from pathlib import Path
import subprocess
import sys

Code = Path(__file__).resolve().parent
Build = Path(sys.argv[1])
subprocess.run([sys.executable, str(Code / "FacetImageGpuSources.py"), str(Build)], check=True)
Area = (Code / "AreaReconnectionValidation.h").read_text()
Begin = Area.index("double ContinuousFacetArea(")
Opening = Area.index("{", Begin)
Depth, End = 1, Opening + 1
while Depth:
    Depth += (Area[End] == "{") - (Area[End] == "}")
    End += 1
(Build / "FacetProfileGpuComparisons.h").write_text(
    '#include "FacetImageGpuComparisons.h"\nnamespace {\n' + Area[Begin:End] + "\n}\n"
)
Source = (Build / "FacetImageGpu.slang").read_text()
Header = """
uint FacetProfileCount() { return asuint(Receivers[0].PositionHit.x); }
uint FacetProfileCameras() { return asuint(Receivers[0].PositionHit.y); }
uint FacetProfileStart() { return asuint(Receivers[0].PositionHit.z); }

"""
Source = Source.replace("uint PacketSourceIndex(uint Ordinal)", Header + "uint PacketSourceIndex(uint Ordinal)")
Changes = {
    "Four additional receiver entries carry only the original source profile's index permutation.": "A header locates the runtime-sized source permutation after the camera inputs.",
    "Receivers[Extent.Dimensions.x - 4 + Ordinal / 4]": "Receivers[FacetProfileStart() + Ordinal / 4]",
    "Ordinal < 16": "Ordinal < FacetProfileCount()",
    "uint(Choice * 16), 15u": "uint(Choice * float(FacetProfileCount())), FacetProfileCount() - 1",
    "Chosen = 15;": "Chosen = FacetProfileCount() - 1;",
    "1.0 / 16 /": "1.0 / float(FacetProfileCount()) /",
    "GenerateFacetImage": "GenerateFacetProfile",
    "ResolveFacetImage": "ResolveFacetProfile",
    "if (Pixel >= Count - 4)": "if (Pixel >= Count || Pixel >= FacetProfileCameras())",
    "SurfacePoint Input = Receivers[Pixel];": """uint Rejection = ValidateFacetProfile();
    if (Rejection != 0)
    {
        OutputColour[79 * Count + Pixel] = float4(Rejection, 0, 0, 0);
        return;
    }
    SurfacePoint Input = Receivers[1 + Pixel];""",
}
for Before, After in Changes.items():
    assert Before in Source, Before
    Source = Source.replace(Before, After)
Validation = """
uint ValidateFacetProfile()
{
    uint Support = FacetProfileCount(), Cameras = FacetProfileCameras(), Start = FacetProfileStart();
    uint Capacity = asuint(Receivers[0].PositionHit.w), Count = Extent.Dimensions.x;
    if (Support == 0 || Support > 16777216u || Support > Extent.Sampling.z || Cameras >= Count ||
        Start != Cameras + 1 || Start > Count || Capacity != (Support + 3) / 4 || Capacity != Count - Start)
        return 1;
    float Total = 0;
    for (uint Ordinal = 0; Ordinal < Support; ++Ordinal)
    {
        uint Index = PacketSourceIndex(Ordinal);
        if (Index >= Extent.Sampling.z)
            return 2;
        for (uint Previous = 0; Previous < Ordinal; ++Previous)
            if (PacketSourceIndex(Previous) == Index)
                return 2;
        float Area = ReservoirFacetArea(Index);
        if (!isfinite(Area) || Area <= 0)
            return 3;
        Total += Area;
    }
    if (!isfinite(Total) || Total <= 0 || !isfinite(1.0 / Total) || 1.0 / Total <= 0)
        return 3;
    float Boundary = 0;
    for (uint Ordinal = 0; Ordinal < Support; ++Ordinal)
    {
        float Area = ReservoirFacetArea(PacketSourceIndex(Ordinal));
        float Next = Boundary + Area / Total;
        float Density = 1.0 / float(Support) / Area;
        if (!isfinite(Next) || Next <= Boundary || (Ordinal + 1 < Support && Next >= 1) ||
            !isfinite(Density) || Density <= 0)
            return 3;
        Boundary = Next;
    }
    return 0;
}

"""
Needle = '[shader("compute")][numthreads(64, 1, 1)] void GenerateFacetProfile'
assert Source.count(Needle) == 1
Source = Source.replace(Needle, Validation + Needle)
assert "Ordinal < 16" not in Source and "1.0 / 16 /" not in Source
(Build / "FacetProfileGpu.slang").write_text(Source)
print("PROFILE_GPU_SOURCES checked_extraction=1 dynamic_support=1 header=1")
