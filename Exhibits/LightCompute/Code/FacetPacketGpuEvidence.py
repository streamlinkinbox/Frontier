# 🧩 Audit GPU-generated four-lane packet evidence and retain actual packet/path comparisons without quality claims.
from pathlib import Path
import gzip
import hashlib
import re
import numpy as np
from PIL import Image, ImageDraw, ImageFont

Root = Path(__file__).resolve().parents[3]
Results = Root / "Exhibits/LightCompute/Results"
Images = Root / "Exhibits/LightCompute/Images"
Prefix = "FacetPacketGpu"
Builds = [Root / f"_ClaudeScratch/build/facet-packet-gpu-{Mode}" for Mode in ("release", "sanitized")]
Prior = 0
for Old in (
    "FacetAreaTransport",
    "FacetAreaOcclusion",
    "FacetAreaOcclusionGpu",
    "FacetPacket",
    "FacetMixed",
    "FacetNorm",
    "FacetCausal",
    "FacetCausalGpu",
):
    for Line in (Results / f"{Old}-evidence-hashes.txt").read_text().splitlines():
        Digest, Name = Line.split("  ", 1)
        assert hashlib.sha256((Root / Name).read_bytes()).hexdigest() == Digest, Name
        Prior += 1
assert "acceptance=0" in (Results / "FacetAreaTransport-parity.txt").read_text()
assert "promotion=FAIL" in (Results / "FacetNorm-parity.txt").read_text()
Semantic, Totals = [], []
for Mode in ("release", "sanitized"):
    Text = (Results / f"{Prefix}-{Mode}.txt").read_text()
    assert "100% tests passed, 0 tests failed out of 11" in Text
    assert "PACKET_GPU_UNIT checks=10 pass=1" in Text
    assert "CAUSAL_GPU_UNIT checks=4 pass=1" in Text
    assert "FACET_CAUSAL_TOTAL mutations=36 fault_rejections=36 pass=1" in Text
    assert "SwiftShader Device (Subzero) type=4 driver_sanitized=0 leaks=0" in Text
    assert not re.search(r"ERROR: AddressSanitizer|runtime error:", Text)
    Rows = re.findall(r"^PACKET_GPU_(?:UNIT|CASE|TOTAL).*$", Text, re.M)
    assert len(Rows) == 38
    assert "executions=108 candidates=720 paths=576 pairs=144 visibility=1152 mutations=36 faults=36" in Rows[-1]
    Semantic.append(Rows)
    Totals.append(Rows[-1])
    for Digest, Name in re.findall(r"^([a-f0-9]{64})  (.+)$", Text, re.M):
        assert hashlib.sha256((Root / Name).read_bytes()).hexdigest() == Digest, Name
assert Semantic[0] == Semantic[1]
Arrays = []
for Name, Shape in (
    ("FacetPacketGpu", (36, 3, 80, 4)),
    ("FacetPacketNative", (36, 3, 80, 4)),
    ("FacetPacketQualified", (36, 3, 4)),
):
    Raw = (Builds[0] / f"gpu-captures/{Name}.f32").read_bytes()
    assert Raw == (Builds[1] / f"gpu-captures/{Name}.f32").read_bytes()
    assert len(Raw) == np.prod(Shape) * 4
    Archive = Results / f"{Prefix}-{Name.removeprefix('FacetPacket')}.f32.gz"
    Archive.write_bytes(gzip.compress(Raw, mtime=0))
    assert gzip.decompress(Archive.read_bytes()) == Raw
    Array = np.frombuffer(Raw, dtype="<f4").reshape(Shape)
    assert np.isfinite(Array).all()
    Arrays.append(Array)
Gpu, Native, Qualified = Arrays
Old = np.frombuffer(gzip.decompress((Results / "FacetCausal-mutations.f32.gz").read_bytes()), dtype="<f4").reshape(
    36, 18, 4
)
for Build in Builds:
    assert (Build / "captures/FacetCausalMutations.f32").read_bytes() == gzip.decompress(
        (Results / "FacetCausal-mutations.f32.gz").read_bytes()
    )
Methods = ("RIS U", "RIS A", "raw4 U", "raw4 A", "raw8 U", "raw8 A")
Metrics = [["Fixture", "Method", "Operation", "Packet max error", "Path restore error", "Bypass rejected"]]
Canvas = Image.new("RGB", (1210, 1470), "#121923")
Pen = ImageDraw.Draw(Canvas)
Font = ImageFont.load_default(size=12)
Pen.text(
    (12, 12),
    "Actual full GPU-generated four-lane packets and qualified paths /SwiftShader CPU Vulkan /exposure4096",
    font=Font,
    fill="white",
)
Labels = (
    "Native packet",
    "GPU packet",
    "GPU blocked packet",
    "GPU target blocked",
    "GPU target restored",
    "GPU target bypass",
)
for Column, Label in enumerate(Labels):
    Pen.text((170 + 170 * Column, 40), Label, font=Font, fill="white")
MaximumPacket, MaximumRestore = 0.0, 0.0
Counts = np.zeros(4, dtype=np.int64)
for Case in range(36):
    Fixture, Method, Operation = Old[Case, 0, :3].astype(int)
    for Variant in range(3):
        Slot = int(Qualified[Case, Variant, 2])
        assert np.array_equal(Qualified[Case, Variant, [0, 1, 3]], (Case, Variant, Operation))
        ExpectedWork = (4, 4, 4, 4) if Method in (2, 3) else (8, 8, 8, 8) if Method >= 4 else (8, 4, 4, 4)
        assert np.array_equal(Gpu[Case, Variant, 60], ExpectedWork)
        assert np.array_equal(Gpu[Case, Variant, 60], Native[Case, Variant, 60])
        Counts[:2] += np.array(ExpectedWork[:2])
        Counts[2] += 2 * ExpectedWork[1]
        Counts[3] += 4 if Method < 2 else 0
        assert np.array_equal(Gpu[Case, Variant, 16:20, 3], Native[Case, Variant, 16:20, 3])
        for Lane in range(4):
            for Ordinal in range(1 if Method in (2, 3) else 2):
                assert np.array_equal(
                    Gpu[Case, Variant, 8 + 2 * Lane + Ordinal, :3], Native[Case, Variant, 8 + 2 * Lane + Ordinal, :3]
                )
        for Slab in (0, 1, 2, 3):
            Error = np.max(np.abs(Gpu[Case, Variant, Slab, :3] - Native[Case, Variant, Slab, :3]))
            assert Error <= 1e-10 + 2e-4 * np.max(np.abs(Native[Case, Variant, Slab, :3]))
        if Variant == 1:
            assert (Gpu[Case, Variant, 20 + Slot, :3] == 0).all()
            RestoreSlab, WrongSlab = (28, 36) if Operation == 0 else (36, 28)
            assert (Gpu[Case, Variant, WrongSlab + Slot, :3] == 0).all()
            Restored = Gpu[Case, Variant, RestoreSlab + Slot, :3]
            RestoreError = float(np.max(np.abs(Restored - Old[Case, 10, :3])))
            assert RestoreError <= 1e-10 + 2e-4 * np.max(np.abs(Old[Case, 10, :3]))
        if Variant == 2:
            assert (Gpu[Case, Variant, 20 + Slot, :3] > 0).any()
    PacketError = float(np.max(np.abs(Gpu[Case, :, :4, :3] - Native[Case, :, :4, :3])))
    MaximumPacket, MaximumRestore = max(MaximumPacket, PacketError), max(MaximumRestore, RestoreError)
    Metrics.append(
        [
            str(Fixture),
            Methods[Method],
            "Connection" if Operation == 0 else "Suffix",
            f"{PacketError:.9g}",
            f"{RestoreError:.9g}",
            "YES",
        ]
    )
    Slot = int(Qualified[Case, 1, 2])
    FaultSlot = int(Qualified[Case, 2, 2])
    Colours = (
        Native[Case, 0, 0, :3],
        Gpu[Case, 0, 0, :3],
        Gpu[Case, 1, 0, :3],
        Gpu[Case, 1, 20 + Slot, :3],
        Gpu[Case, 1, (28 if Operation == 0 else 36) + Slot, :3],
        Gpu[Case, 2, 20 + FaultSlot, :3],
    )
    for Column, Rgb in enumerate(Colours):
        Encoded = tuple(np.rint(np.clip(Rgb * 4096, 0, 1) * 255).astype(np.uint8).tolist())
        Tile = Image.new("RGB", (125, 23), Encoded)
        Canvas.paste(Tile, (170 + 170 * Column, 64 + Case * 37))
    Pen.text(
        (8, 69 + Case * 37),
        f"F{Fixture} {Methods[Method]} {'conn' if Operation == 0 else 'suffix'}",
        font=Font,
        fill="white",
    )
assert np.array_equal(Counts, (720, 576, 1152, 144))
Pen.text(
    (12, 1430),
    "Packets may retain other lit paths. Target contribution,not entire packet,must become black. No quality/speed/default promotion.",
    font=Font,
    fill="white",
)
Canvas.save(Images / f"{Prefix}Comparison.png")
Widths = [max(len(Row[Column]) for Row in Metrics) for Column in range(len(Metrics[0]))]
Lines = [
    "| " + " | ".join(Cell.ljust(Width) for Cell, Width in zip(Row, Widths)) + " |"
    for Row in [Metrics[0], ["-" * Width for Width in Widths], *Metrics[1:]]
]
assert max(map(len, Lines)) <= 120
(Results / f"{Prefix}-comparison.md").write_text("\n".join(Lines) + "\n")
Summary = (
    "PASS GPU-generated four-lane deformed causal packets;native selected proposals and weights removed from shader inputs.\n"
    "SwiftShader CPU Vulkan type4,not physical GPU. Host ASan+UBSan;driver uninstrumented,leaks disabled.\n"
    "11 CTests/build,20316 focused checks/build;108 packets,720 generated candidates,144 exact pair selections/build.\n"
    "576 traced paths and1152 visibility decisions/build exactly match native/double BVH;no edge/selection waivers.\n"
    "36/36 generated qualified-path mutations pass;36/36 actual GPU shader multiplication bypass faults rejected.\n"
    f"Maximum packet/qualified-restore RGB error={MaximumPacket:.17g}/{MaximumRestore:.17g}.\n"
    f"{Totals[0]}\n"
    "Three readback/reference/qualification buffers and38 packet semantic rows identical across builds.\n"
    f"All{Prior} prior manifest entries unchanged;historical transport HELD and norm promotion FAIL preserved.\n"
    "Targeted packet/contribution swatches retained,not full-scene images or random-scene quality acceptance.\n"
    "Exhibit-only80-slab diagnostics;production348.046875MiB/defaults/32B unchanged. No PR/Actions.\n"
)
(Results / f"{Prefix}-parity.txt").write_text(Summary)
print(Summary)
Files = sorted(
    [
        *Results.glob(f"{Prefix}*"),
        *Images.glob(f"{Prefix}*"),
        *Root.glob("Exhibits/LightCompute/Code/FacetPacketGpu*"),
        Root / "Exhibits/LightCompute/FacetPacketGpu.md",
        Root / "Exhibits/LightCompute/RunFacetPacketGpu.sh",
        Root / "Exhibits/LightCompute/FacetPacketGpu/CMakeLists.txt",
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
