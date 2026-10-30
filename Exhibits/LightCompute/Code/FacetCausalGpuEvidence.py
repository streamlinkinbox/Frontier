# 🧩 Audit fixed-proposal deformed Vulkan readbacks and display actual native/GPU contribution parity.
from pathlib import Path
import gzip
import hashlib
import re
import numpy as np
from PIL import Image, ImageDraw, ImageFont

Root = Path(__file__).resolve().parents[3]
Results = Root / "Exhibits/LightCompute/Results"
Images = Root / "Exhibits/LightCompute/Images"
Prefix = "FacetCausalGpu"
Builds = [Root / f"_ClaudeScratch/build/facet-causal-gpu-{Mode}" for Mode in ("release", "sanitized")]
Prior = 0
for Old in (
    "FacetAreaTransport",
    "FacetAreaOcclusion",
    "FacetAreaOcclusionGpu",
    "FacetPacket",
    "FacetMixed",
    "FacetNorm",
    "FacetCausal",
):
    for Line in (Results / f"{Old}-evidence-hashes.txt").read_text().splitlines():
        Digest, Name = Line.split("  ", 1)
        assert hashlib.sha256((Root / Name).read_bytes()).hexdigest() == Digest, Name
        Prior += 1
assert "acceptance=0" in (Results / "FacetAreaTransport-parity.txt").read_text()
assert "promotion=FAIL" in (Results / "FacetNorm-parity.txt").read_text()
Semantic = []
for Mode in ("release", "sanitized"):
    Text = (Results / f"{Prefix}-{Mode}.txt").read_text()
    assert "100% tests passed, 0 tests failed out of 10" in Text
    assert "CAUSAL_GPU_UNIT checks=4 pass=1" in Text
    assert "FACET_CAUSAL_TOTAL mutations=36 fault_rejections=36 pass=1" in Text
    assert "type=4 driver_sanitized=0 leaks=0" in Text and "SwiftShader" in Text
    assert not re.search(r"ERROR: AddressSanitizer|runtime error:", Text)
    Rows = re.findall(r"^CAUSAL_GPU_(?:UNIT|CASE|TOTAL).*$", Text, re.M)
    assert len(Rows) == 38 and "executions=108 visibility=216 faults_rejected=36" in Rows[-1]
    Semantic.append(Rows)
    for Digest, Name in re.findall(r"^([a-f0-9]{64})  (.+)$", Text, re.M):
        assert hashlib.sha256((Root / Name).read_bytes()).hexdigest() == Digest, Name
assert Semantic[0] == Semantic[1]
Raw = (Builds[0] / "gpu-captures/FacetCausalGpu.f32").read_bytes()
assert Raw == (Builds[1] / "gpu-captures/FacetCausalGpu.f32").read_bytes()
assert len(Raw) == 36 * 3 * 16 * 16
Readback = np.frombuffer(Raw, dtype="<f4").reshape(36, 3, 16, 4)
assert np.isfinite(Readback).all()
NativeBytes = gzip.decompress((Results / "FacetCausal-mutations.f32.gz").read_bytes())
Native = np.frombuffer(NativeBytes, dtype="<f4").reshape(36, 18, 4)
for Build in Builds:
    assert (Build / "captures/FacetCausalMutations.f32").read_bytes() == NativeBytes
    assert (Build / "captures/FacetCausalAttribution.csv").read_bytes() == (
        Results / "FacetCausal-attribution.csv"
    ).read_bytes()
Archive = Results / f"{Prefix}-readback.f32.gz"
Archive.write_bytes(gzip.compress(Raw, mtime=0))
assert gzip.decompress(Archive.read_bytes()) == Raw
Methods = ("RIS U", "RIS A", "raw4 U", "raw4 A", "raw8 U", "raw8 A")
Rows = [["Fixture", "Method", "Operation", "Max RGB error", "Restored error", "Bypass rejected"]]
Canvas = Image.new("RGB", (1160, 1450), "#121923")
Pen = ImageDraw.Draw(Canvas)
Font = ImageFont.load_default(size=12)
Pen.text(
    (12, 12),
    "Actual weighted selected-path RGB /native versus Slang SwiftShader /NOT full-scene renders /exposure4096",
    font=Font,
    fill="white",
)
Labels = ("Native original", "GPU original", "GPU blocked", "GPU restored", "GPU bypass fault")
for Column, Label in enumerate(Labels):
    Pen.text((190 + 185 * Column, 42), Label, font=Font, fill="white")
Maximum = 0.0
for Mutation, (Captured, Expected) in enumerate(zip(Readback, Native)):
    Fixture, Method, Operation = Expected[0, :3].astype(int)
    Correct = Expected[10, :3]
    RestoreSlab = 1 if Operation == 0 else 2
    WrongSlab = 2 if Operation == 0 else 1
    Tolerance = 1e-10 + 2e-4 * np.max(np.abs(Correct))
    assert (Captured[0, 0, :3] > 0).any()
    assert (Captured[1, 0, :3] == 0).all() and (Captured[1, WrongSlab, :3] == 0).all()
    assert (Captured[2, 0, :3] > 0).any()  # Actual shader bypass changes RGB despite the blocked visibility bit.
    Error = float(np.max(np.abs(Captured[0, 0, :3] - Correct)))
    RestoreError = float(np.max(np.abs(Captured[1, RestoreSlab, :3] - Correct)))
    assert max(Error, RestoreError) <= Tolerance
    assert np.array_equal(Captured[2, 12], Captured[1, 12])
    assert np.max(np.abs(Captured[2, 0, :3] - Captured[1, RestoreSlab, :3])) <= Tolerance
    assert np.array_equal(Captured[0, 12], (1, 1, 1, 1))
    assert np.array_equal(Captured[1, 12], (Operation == 1, Operation == 0, 1, 1))
    Maximum = max(Maximum, Error, RestoreError)
    Rows.append(
        [
            str(Fixture),
            Methods[Method],
            "Connection" if Operation == 0 else "Suffix",
            f"{Error:.9g}",
            f"{RestoreError:.9g}",
            "YES",
        ]
    )
    for Column, Rgb in enumerate(
        (Correct, Captured[0, 0, :3], Captured[1, 0, :3], Captured[1, RestoreSlab, :3], Captured[2, 0, :3])
    ):
        Encoded = np.rint(np.clip(Rgb * 4096, 0, 1) * 255).astype(np.uint8)
        Tile = Image.new("RGB", (130, 23), tuple(Encoded.tolist()))
        Canvas.paste(Tile, (190 + 185 * Column, 66 + Mutation * 37))
    Pen.text(
        (10, 70 + Mutation * 37),
        f"F{Fixture} {Methods[Method]} {'conn' if Operation == 0 else 'suffix'}",
        font=Font,
        fill="white",
    )
Pen.text(
    (12, 1410),
    "Fixed native-selected proposals,not GPU reservoir sampling. CPU Vulkan device;no hardware speed or production acceptance.",
    font=Font,
    fill="white",
)
Canvas.save(Images / f"{Prefix}Comparison.png")
Widths = [max(len(Row[Column]) for Row in Rows) for Column in range(len(Rows[0]))]
Lines = [
    "| " + " | ".join(Cell.ljust(Width) for Cell, Width in zip(Row, Widths)) + " |"
    for Row in [Rows[0], ["-" * Width for Width in Widths], *Rows[1:]]
]
assert max(map(len, Lines)) <= 120
(Results / f"{Prefix}-comparison.md").write_text("\n".join(Lines) + "\n")
Summary = (
    "PASS fixed-selected-proposal deformed Slang/Vulkan causal parity;NOT fresh GPU sampler/selector acceptance.\n"
    "SwiftShader CPU Vulkan device type4;no physical GPU or speed claim. Host sanitizer,driver uninstrumented,leaks disabled.\n"
    "10 CTests/build,20306 focused checks/build;Release/host ASan+UBSan. Native causal inputs regenerated unchanged.\n"
    "108 GPU primary/source/path executions/build;216 visibility decisions exactly match native and double BVH.\n"
    "36/36 real mutations block correct RGB;target-only omission restores RGB;wrong-only omission stays zero.\n"
    "36/36 actual GPU shader-bypass faults rejected by the same causal predicate.\n"
    f"Max original/restored weighted native-GPU RGB error={Maximum:.17g}.\n"
    "Readback bytes and38 GPU semantic rows identical across Release/sanitized builds.\n"
    f"All{Prior} prior manifest entries unchanged;historical transport HELD and norm promotion FAIL preserved.\n"
    "Actual readback contribution comparison retained;not full mutated-scene rendering.\n"
    "No engine/API/shader-default change,renderer/history/general materials/Windows acceptance.\n"
    "Production348.046875MiB/defaults/32B unchanged;no PR/Actions.\n"
)
(Results / f"{Prefix}-parity.txt").write_text(Summary)
print(Summary)
Files = sorted(
    [
        *Results.glob(f"{Prefix}*"),
        *Images.glob(f"{Prefix}*"),
        *Root.glob("Exhibits/LightCompute/Code/FacetCausalGpu*"),
        Root / "Exhibits/LightCompute/FacetCausalGpu.md",
        Root / "Exhibits/LightCompute/RunFacetCausalGpu.sh",
        Root / "Exhibits/LightCompute/FacetCausalGpu/CMakeLists.txt",
    ]
)
Manifest = Results / f"{Prefix}-evidence-hashes.txt"
Manifest.write_text(
    "".join(
        f"{hashlib.sha256(Path.read_bytes()).hexdigest()}  {Path.relative_to(Root)}\n"
        for Path in Files
        if Path != Manifest
    )
)
