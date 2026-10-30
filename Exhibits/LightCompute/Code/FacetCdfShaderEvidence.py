# 🧩 Compare actual maintained-API submissions against reconstructed prior captures without duplicating buffers.
from pathlib import Path
import hashlib
import re
import subprocess
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont

Root = Path(__file__).resolve().parents[3]
Exhibit = Root / "Exhibits/LightCompute"
Results, Images = Exhibit / "Results", Exhibit / "Images"
Prefix = "FacetCdfShader"
Prior = 0
for Name in (
    "FacetAreaTransport",
    "FacetAreaOcclusion",
    "FacetAreaOcclusionGpu",
    "FacetPacket",
    "FacetMixed",
    "FacetNorm",
    "FacetCausal",
    "FacetCausalGpu",
    "FacetPacketGpu",
    "FacetImageGpu",
    "FacetProfileGpu",
    "FacetCdfGpu",
    "FacetCdfSubmission",
):
    for Line in (Results / f"{Name}-evidence-hashes.txt").read_text().splitlines():
        Digest, Filename = Line.split("  ", 1)
        assert hashlib.sha256((Root / Filename).read_bytes()).hexdigest() == Digest, Filename
        Prior += 1
assert Prior == 1834
Reference = Root / "_ClaudeScratch/cdf-submission-reference"
subprocess.run(
    [sys.executable, str(Exhibit / "Code/FacetCdfGpuEvidence.py"), "--reconstruct", str(Reference)], check=True
)
Rows = []
SpirvRows = []
Builds = [Root / f"_ClaudeScratch/build/facet-cdf-shader-{Mode}" for Mode in ("release", "sanitized")]
Original = (Results / "FacetCdfGpu-release.txt").read_text()
ExpectedRows = re.findall(r"^CDF_GPU_(?:BATCH|TOTAL|EXIT).*$", Original, re.M)
assert len(ExpectedRows) == 124 and "pass=1" in ExpectedRows[-2]
for Mode, Build in zip(("release", "sanitized"), Builds):
    Text = (Results / f"{Prefix}-{Mode}.txt").read_text()
    assert "100% tests passed, 0 tests failed out of 16" in Text
    assert "CDF_SUBMISSION_UNIT checks=164 pass=1" in Text
    assert "CDF_SHADER_UNIT checks=29 pass=1" in Text
    assert "SwiftShader Device (Subzero) type=4 driver_sanitized=0 leaks=0" in Text
    assert not re.search(r"ERROR: AddressSanitizer|runtime error:", Text)
    Semantic = re.findall(r"^CDF_GPU_(?:BATCH|TOTAL|EXIT).*$", Text, re.M)
    assert Semantic == ExpectedRows
    Rows.append(Semantic)
    for Digest, Filename in re.findall(r"^([a-f0-9]{64})  (.+)$", Text, re.M):
        assert hashlib.sha256((Root / Filename).read_bytes()).hexdigest() == Digest, Filename
    Matches = 0
    for Digest, Filename in re.findall(r"^([a-f0-9]{64})  (.+)$", Original, re.M):
        if Filename.endswith(".spv"):
            Relative = Filename.split("/facet-cdf-gpu-release/", 1)[1]
            ActualDigest = hashlib.sha256((Build / Relative).read_bytes()).hexdigest()
            assert ActualDigest == hashlib.sha256((Builds[0] / Relative).read_bytes()).hexdigest()
            if Relative.startswith("uncached/"):
                assert ActualDigest == Digest
            if Mode == "release":
                SpirvRows.append(
                    f"{Relative} prior={Digest} actual={ActualDigest} unchanged={int(ActualDigest == Digest)}"
                )
            Matches += 1
    assert Matches == 6
assert Rows[0] == Rows[1]
(Results / f"{Prefix}-spirv-comparison.txt").write_text("\n".join(SpirvRows) + "\n")
ShaderRoot = Root / "Frontier/Engine/Rendering/Shaders"
Visited = set()


def InspectIncludes(Filename):
    Filename = Filename.resolve()
    assert any(Filename.is_relative_to(Directory.resolve()) for Directory in (ShaderRoot, Root / "Engine")), Filename
    if Filename in Visited:
        return
    Visited.add(Filename)
    for Include in re.findall(r'^#include "([^"]+)"', Filename.read_text(), re.M):
        InspectIncludes(Filename.parent / Include)


InspectIncludes(ShaderRoot / "FacetCdfPreparation.slang")
assert ShaderRoot.resolve() / "FacetCdfProfile.slang" in Visited
(Results / f"{Prefix}-ownership.txt").write_text(
    "Preparation compiled directly with maintained-only include paths;recursive source closure follows.\n"
    + "\n".join(str(File.relative_to(Root)) for File in sorted(Visited))
    + "\n"
)
Files = sorted(Reference.iterdir())
assert len(Files) == 7
for File in Files:
    Expected = hashlib.sha256(File.read_bytes()).hexdigest()
    for Build in Builds:
        assert hashlib.sha256((Build / "captures" / File.name).read_bytes()).hexdigest() == Expected, File.name
(Results / f"{Prefix}-capture-references.txt").write_text(
    "# Reconstruct with FacetCdfGpuEvidence.py --reconstruct;all seven NEW readbacks exactly match these retained bytes.\n"
    + "".join(f"{hashlib.sha256(File.read_bytes()).hexdigest()}  {File.name}\n" for File in Files)
)
Gpu = np.memmap(Builds[0] / "captures/FacetCdfGpu.f32", dtype="<f4", mode="r", shape=(116, 1024, 80, 4))
Native = np.memmap(Builds[0] / "captures/FacetCdfNative.f32", dtype="<f4", mode="r", shape=(116, 1024, 80, 4))
Work = np.fromfile(Builds[0] / "captures/FacetCdfWork.u32", dtype="<u4").reshape(122, 12)
Positive = Work[Work[:, 3] != 1]
Difference = np.abs(Gpu[:, :, 0, :3].astype(np.float64) - Native[:, :, 0, :3])
Canvas = Image.new("RGB", (1300, 930), "#121923")
Pen = ImageDraw.Draw(Canvas)
Font, Small = ImageFont.load_default(size=15), ImageFont.load_default(size=12)
Pen.text(
    (16, 12),
    "Maintained generic facet profile/preparation/sampling shaders /SwiftShader CPU Vulkan /not editor acceptance",
    fill="white",
    font=Font,
)
Pen.text(
    (16, 42),
    "16 CTests/20,535 focused checks per build /29 new profile-header assertions /244 actual GPU submissions",
    fill="white",
    font=Font,
)
Pen.text(
    (16, 72),
    "116 images,all80 slabs /7 captures exact prior and cross-build /30 preparations,466 measured area calls",
    fill="white",
    font=Font,
)
for Left, Label, Detail in (
    (16, "Typed16B header", "source count /camera count /offset /capacity"),
    (446, "Owned preparation", "validity /areas /CDF /uniform and area PDFs"),
    (876, "Owned cached sampling", "RNG /packed identity /continuous U,V /PDF"),
):
    Pen.rounded_rectangle((Left, 120, Left + 400, 200), radius=8, outline="#55d4a0", width=2)
    Pen.text((Left + 10, 130), Label, font=Font, fill="white")
    Pen.text((Left + 10, 165), Detail, font=Small, fill="white")
Pen.text(
    (16, 224),
    "Fixture lighting/proxy score and transport stay in the exhibit. Submission/fence interface is unchanged.",
    fill="white",
    font=Font,
)
Methods = ("RIS uniform", "RIS area", "raw4 uniform", "raw4 area", "raw8 uniform", "raw8 area")
DisplayMatches = 0
for Method, Label in enumerate(Methods):
    Index = int(
        np.flatnonzero(
            (Positive[:, 0] == 2) & (Positive[:, 1] == 65) & (Positive[:, 2] == Method) & (Positive[:, 3] == 0)
        )[0]
    )
    Left = 120 + Method * 190
    Pen.text((Left, 280), Label, font=Small, fill="white")
    Display = []
    for Row, Pixels in enumerate((Native[Index, :, 0, :3], Gpu[Index, :, 0, :3], Difference[Index])):
        if Row < 2:
            Encoded = np.rint(np.clip(Pixels.astype(np.float64) * 1024, 0, 1) * 255).astype(np.uint8).reshape(32, 32, 3)
            Display.append(Encoded.tobytes())
        else:
            Encoded = np.zeros((1024, 3), dtype=np.uint8)
            Encoded[:, 0] = np.rint(np.clip(Pixels.max(axis=-1) * 1e8, 0, 1) * 255).astype(np.uint8)
            Encoded = Encoded.reshape(32, 32, 3)
        Tile = Image.fromarray(Encoded)
        Tile.save(Images / f"{Prefix}-{Method}-{('Native', 'Gpu', 'Error')[Row]}.png")
        Canvas.paste(Tile.resize((160, 160), Image.Resampling.NEAREST), (Left, 325 + Row * 180))
    DisplayMatches += Display[0] == Display[1]
for Row, Label in enumerate(("Native", "Owned shaders", "Error x1e8")):
    Pen.text((12, 325 + Row * 180), Label, font=Small, fill="white")
Pen.text(
    (16, 875),
    "Actual layout2,N65,32x32 single-sample output /linear exposure1024,no gamma /red=RGB absolute error x1e8",
    fill="white",
    font=Small,
)
Pen.text(
    (16, 902),
    "No FPS/quality claim. Owned preparation has no exhibit includes. Production348.046875MiB/defaults/32B unchanged.",
    fill="white",
    font=Small,
)
Canvas.save(Images / f"{Prefix}Comparison.png")
assert DisplayMatches == 6
Summary = (
    "Maintained CDF shader ownership parity=PASS;frozen numeric gates unchanged.\n"
    "16 CTests/20535 focused checks per build,including29 new header and164 existing submission assertions.\n"
    "244 actual API submissions;116 positive images/118784 packets,6144 exact rejected pixels per build.\n"
    "Seven captures/124 semantic rows exact prior/cross-build;6 SPIR-V binaries exact cross-build,3 uncached binaries exact prior.\n"
    "30 cache preparations,466 cached area calls versus25656526 uncached;operation counts,not time/FPS.\n"
    f"Packet RGB maximum={Difference.max():.17g};RMSE={np.sqrt(np.mean(Difference**2)):.17g}.\n" + Rows[0][-2] + "\n"
    f"Actual displayed native/GPU pairs exact={DisplayMatches}/6. Thirteen prior manifests/{Prior} entries unchanged.\n"
    "Typed profile layout/extent/overflow tests PASS;owned preparation source closure has no exhibit dependency.\n"
    "Ownership remains pending on query error;caller must keep resources alive and acknowledge successful QueueSubmit.\n"
    "Driver uninstrumented,leaks0. SwiftShader CPU only,no physical-GPU/Windows/editor/history/general-material acceptance.\n"
    "Maintained profile/header/prepare/sample are active in tests;fixture score/transport remain exhibit-owned. Production348.046875MiB/32B/defaults unchanged.\n"
)
(Results / f"{Prefix}-parity.txt").write_text(Summary)
Paths = [
    Root / "Frontier/Engine/Rendering/FacetCdfProfile.h",
    Root / "Frontier/Engine/Rendering/Shaders/FacetCdfProfile.slang",
    Root / "Frontier/Engine/Rendering/Shaders/FacetCdfPreparation.slang",
    Exhibit / "Code/FacetCdfShaderSources.py",
    Exhibit / "Code/FacetCdfShaderValidation.cpp",
    Exhibit / "Code/FacetCdfShaderEvidence.py",
    Exhibit / "FacetCdfShader/CMakeLists.txt",
    Exhibit / "RunFacetCdfShader.sh",
    Exhibit / "FacetCdfShader.md",
]
Paths += sorted(Visited)
Paths += sorted(Images.glob(f"{Prefix}*.png"))
Paths += sorted(File for File in Results.glob(f"{Prefix}-*") if File.name != f"{Prefix}-evidence-hashes.txt")
Paths = list(dict.fromkeys(Paths))
(Results / f"{Prefix}-evidence-hashes.txt").write_text(
    "".join(f"{hashlib.sha256(File.read_bytes()).hexdigest()}  {File.relative_to(Root).as_posix()}\n" for File in Paths)
)
print(Summary)
