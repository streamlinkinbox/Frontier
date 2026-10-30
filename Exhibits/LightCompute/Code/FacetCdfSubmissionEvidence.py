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
Prefix = "FacetCdfSubmission"
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
):
    for Line in (Results / f"{Name}-evidence-hashes.txt").read_text().splitlines():
        Digest, Filename = Line.split("  ", 1)
        assert hashlib.sha256((Root / Filename).read_bytes()).hexdigest() == Digest, Filename
        Prior += 1
assert Prior == 1803
Reference = Root / "_ClaudeScratch/cdf-submission-reference"
subprocess.run(
    [sys.executable, str(Exhibit / "Code/FacetCdfGpuEvidence.py"), "--reconstruct", str(Reference)], check=True
)
Rows = []
Builds = [Root / f"_ClaudeScratch/build/facet-cdf-submission-{Mode}" for Mode in ("release", "sanitized")]
Original = (Results / "FacetCdfGpu-release.txt").read_text()
ExpectedRows = re.findall(r"^CDF_GPU_(?:BATCH|TOTAL|EXIT).*$", Original, re.M)
assert len(ExpectedRows) == 124 and "pass=1" in ExpectedRows[-2]
for Mode, Build in zip(("release", "sanitized"), Builds):
    Text = (Results / f"{Prefix}-{Mode}.txt").read_text()
    assert "100% tests passed, 0 tests failed out of 15" in Text
    assert "CDF_SUBMISSION_UNIT checks=164 pass=1" in Text
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
            assert hashlib.sha256((Build / Relative).read_bytes()).hexdigest() == Digest
            Matches += 1
    assert Matches == 6
assert Rows[0] == Rows[1]
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
    "Maintained caller-owned CDF command/lifetime integration /SwiftShader CPU Vulkan /not editor acceptance",
    fill="white",
    font=Font,
)
Pen.text(
    (16, 42),
    "15 CTests/20,506 focused checks per build /164 new command/fence assertions /244 actual GPU submissions",
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
    (16, "Encoded", "not reusable;discard before submit only"),
    (446, "Submitted", "pending;NOT_READY cannot publish"),
    (876, "Fence SUCCESS", "publish cache;next warm encode may reuse"),
):
    Pen.rounded_rectangle((Left, 120, Left + 400, 200), radius=8, outline="#55d4a0", width=2)
    Pen.text((Left + 10, 130), Label, font=Font, fill="white")
    Pen.text((Left + 10, 165), Detail, font=Small, fill="white")
Pen.text(
    (16, 224),
    "Lifecycle diagram is the tested contract,not a measured timeline. Query errors retain pending ownership.",
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
for Row, Label in enumerate(("Native", "Maintained API", "Error x1e8")):
    Pen.text((12, 325 + Row * 180), Label, font=Small, fill="white")
Pen.text(
    (16, 875),
    "Actual layout2,N65,32x32 single-sample output /linear exposure1024,no gamma /red=RGB absolute error x1e8",
    fill="white",
    font=Small,
)
Pen.text(
    (16, 902),
    "No FPS/quality claim. Shaders still use retained checked source extraction. Production348.046875MiB/defaults/32B unchanged.",
    fill="white",
    font=Small,
)
Canvas.save(Images / f"{Prefix}Comparison.png")
assert DisplayMatches == 6
Summary = (
    "Renderer-facing CDF command/lifetime parity=PASS;frozen numeric gates unchanged.\n"
    "15 CTests/20506 focused checks per build,including164 new submission assertions.\n"
    "244 actual API submissions;116 positive images/118784 packets,6144 exact rejected pixels per build.\n"
    "Seven actual captures,124 semantic rows and6 SPIR-V binaries identical across builds and prior cache phase.\n"
    "30 cache preparations,466 cached area calls versus25656526 uncached;operation counts,not time/FPS.\n"
    f"Packet RGB maximum={Difference.max():.17g};RMSE={np.sqrt(np.mean(Difference**2)):.17g}.\n" + Rows[0][-2] + "\n"
    f"Actual displayed native/GPU pairs exact={DisplayMatches}/6. Twelve prior manifests/{Prior} entries unchanged.\n"
    "Range/callback/overflow/overlap/discard/revision/order/barrier/fence tests PASS;failure controls are host spies.\n"
    "Ownership remains pending on query error;caller must keep resources alive and acknowledge successful QueueSubmit.\n"
    "Driver uninstrumented,leaks0. SwiftShader CPU only,no physical-GPU/Windows/editor/history/general-material acceptance.\n"
    "Maintained header is directly included;no linked editor or shader promotion. Production348.046875MiB/32B/defaults unchanged.\n"
)
(Results / f"{Prefix}-parity.txt").write_text(Summary)
Paths = [
    Root / "Frontier/Engine/Rendering/FacetCdfSubmission.h",
    Exhibit / "Code/FacetCdfSubmissionSources.py",
    Exhibit / "Code/FacetCdfSubmissionValidation.cpp",
    Exhibit / "Code/FacetCdfSubmissionEvidence.py",
    Exhibit / "FacetCdfSubmission/CMakeLists.txt",
    Exhibit / "RunFacetCdfSubmission.sh",
    Exhibit / "FacetCdfSubmission.md",
]
Paths += sorted(Images.glob(f"{Prefix}*.png"))
Paths += sorted(File for File in Results.glob(f"{Prefix}-*") if File.name != f"{Prefix}-evidence-hashes.txt")
(Results / f"{Prefix}-evidence-hashes.txt").write_text(
    "".join(f"{hashlib.sha256(File.read_bytes()).hexdigest()}  {File.relative_to(Root).as_posix()}\n" for File in Paths)
)
print(Summary)
