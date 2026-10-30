# 🧩 Extract unchanged packet arithmetic and comparisons with checked image indexing substitutions.
from pathlib import Path
import subprocess
import sys

Code = Path(__file__).resolve().parent
Build = Path(sys.argv[1])
subprocess.run(
    [
        sys.executable,
        str(Code / "FacetPacketGpuSubmission.py"),
        str(Build / "FacetPacketGpuSubmission.h"),
        str(Build / "FacetPacketGpuComparisons.h"),
    ],
    check=True,
)
Source = (Code / "FacetPacketGpuValidation.cpp").read_text()
Source = Source[: Source.index("void ValidatePacketGpuUnits()")]
(Build / "FacetImageGpuComparisons.h").write_text(Source + "\n}\n")
Source = (Code / "FacetPacketGpu.slang").read_text()
Changes = {
    "Receivers[1 + Ordinal / 4]": "Receivers[Extent.Dimensions.x - 4 + Ordinal / 4]",
    "GeneratePacketPayload": "GenerateFacetImage",
    "ResolvePacketPayload": "ResolveFacetImage",
    "if (Dispatch.x != 0)\n        return;\n    uint Count = Extent.Dimensions.x;": "uint Pixel = Dispatch.x, Count = Extent.Dimensions.x;\n    if (Pixel >= Count - 4)\n        return;\n    SuffixCalls = 0;\n    ReconnectCalls = 0;",
    "SurfacePoint Input = Receivers[0];": "SurfacePoint Input = Receivers[Pixel];",
    "* Count]": "* Count + Pixel]",
    "OutputColour[0]": "OutputColour[Pixel]",
    "OutputColour[Count]": "OutputColour[Count + Pixel]",
}
for Before, After in Changes.items():
    assert Before in Source, Before
    Source = Source.replace(Before, After)
(Build / "FacetImageGpu.slang").write_text(Source)
print("IMAGE_GPU_SOURCES checked_extraction=1 slabs=80 tail=4")
