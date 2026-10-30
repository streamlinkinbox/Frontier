# 🧩 Retain full-frame readbacks and exact cross-build comparisons without reclassifying failed frozen gates.
from pathlib import Path
import gzip
import hashlib
import re
import numpy as np
from PIL import Image, ImageDraw, ImageFont

Root = Path(__file__).resolve().parents[3]
Exhibit = Root / "Exhibits/LightCompute"
Results, Images = Exhibit / "Results", Exhibit / "Images"
Prefix = "FacetImageGpu"
Builds = [Root / f"_ClaudeScratch/build/facet-image-gpu-{Mode}" for Mode in ("release", "sanitized")]
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
    "FacetPacketGpu",
):
    for Line in (Results / f"{Old}-evidence-hashes.txt").read_text().splitlines():
        Digest, Name = Line.split("  ", 1)
        assert hashlib.sha256((Root / Name).read_bytes()).hexdigest() == Digest, Name
        Prior += 1
assert Prior == 1568
assert "acceptance=0" in (Results / "FacetAreaTransport-parity.txt").read_text()
assert "promotion=FAIL" in (Results / "FacetNorm-parity.txt").read_text()
Semantic = []
for Mode in ("release", "sanitized"):
    Text = (Results / f"{Prefix}-{Mode}.txt").read_text()
    assert "100% tests passed, 0 tests failed out of 12" in Text
    assert "IMAGE_GPU_UNIT checks=6 pass=1" in Text
    assert "PACKET_GPU_UNIT checks=10 pass=1" in Text
    assert "SwiftShader Device (Subzero) type=4 driver_sanitized=0 leaks=0" in Text
    assert not re.search(r"ERROR: AddressSanitizer|runtime error:", Text)
    Rows = re.findall(r"^IMAGE_GPU_(?:FRAME|TOTAL|EXIT).*$", Text, re.M)
    assert len(Rows) == 38
    Semantic.append(Rows)
    for Digest, Name in re.findall(r"^([a-f0-9]{64})  (.+)$", Text, re.M):
        assert hashlib.sha256((Root / Name).read_bytes()).hexdigest() == Digest, Name
assert Semantic[0] == Semantic[1]
Arrays = []
for Name, Shape, Type in (
    ("FacetImageGpu.f32", (3, 6, 2, 1024, 80, 4), "<f4"),
    ("FacetImageNative.f32", (3, 6, 2, 1024, 80, 4), "<f4"),
    ("FacetImageValid.u8", (3, 6, 2, 1024), "u1"),
):
    Raw = (Builds[0] / "captures" / Name).read_bytes()
    assert Raw == (Builds[1] / "captures" / Name).read_bytes()
    Array = np.frombuffer(Raw, dtype=Type).reshape(Shape)
    assert np.isfinite(Array).all()
    Archive = Results / f"{Prefix}-{Name.removeprefix('FacetImage')}.gz"
    Archive.write_bytes(gzip.compress(Raw, mtime=0))
    assert gzip.decompress(Archive.read_bytes()) == Raw
    Arrays.append(Array)
Gpu, Native, Valid = Arrays
A, B = Gpu.reshape(-1, 80, 4), Native.reshape(-1, 80, 4)
DiagnosticBad = ~Valid.reshape(-1).astype(bool)


def Accumulate(Comparison):
    global DiagnosticBad
    DiagnosticBad |= ~Comparison.reshape(len(A), -1).all(axis=1)


Accumulate(A[:, 7, :1] == B[:, 7, :1])
Accumulate(A[:, 8:16, :3] == B[:, 8:16, :3])
Accumulate(A[:, 16:20, 3] == B[:, 16:20, 3])
Accumulate(A[:, 52:60, 3] == B[:, 52:60, 3])
Accumulate(A[:, 60] == B[:, 60])
Accumulate(A[:, 69:77, :2] == B[:, 69:77, :2])
for Observed, Expected in (
    (A[:, 8:16, 3], B[:, 8:16, 3]),
    (A[:, 16:20, :3], B[:, 16:20, :3]),
    (A[:, 69:77, 2], B[:, 69:77, 2]),
):
    Accumulate(np.abs(Observed.astype(np.float64) - Expected) <= 2e-5 * np.maximum(1, np.abs(Expected)))
for Observed, Expected in (
    (A[:, 4:7], B[:, 4:7]),
    (A[:, 7, 1:], B[:, 7, 1:]),
    (A[:, 44:52], B[:, 44:52]),
    (A[:, 52:60, :3], B[:, 52:60, :3]),
):
    Accumulate(np.abs(Observed.astype(np.float64) - Expected) <= 2e-5)
RgbSlabs = list(range(4)) + list(range(20, 44)) + list(range(61, 69))
Accumulate(
    np.abs(A[:, RgbSlabs, :3].astype(np.float64) - B[:, RgbSlabs, :3])
    <= 1e-10 + 2e-4 * np.max(np.abs(B[:, RgbSlabs, :3]), axis=-1, keepdims=True)
)
ZeroAlpha = list(range(7)) + list(range(20, 44)) + list(range(61, 80))
Accumulate(A[:, ZeroAlpha, 3] == 0)
Accumulate(A[:, 77:80] == 0)
for Slot in range(8):
    Unused = Slot >= B[:, 60, 1]
    for Start in (20, 28, 36, 44, 52, 61, 69):
        DiagnosticBad |= Unused & np.any(A[:, Start + Slot] != 0, axis=-1)
DiagnosticFailures = int(DiagnosticBad.sum())
First = (Builds[0] / "captures/FacetImageFirstFailure.txt").read_bytes()
assert First == (Builds[1] / "captures/FacetImageFirstFailure.txt").read_bytes()
(Results / f"{Prefix}-first-failure.txt").write_bytes(First)
Methods = ("RIS uniform", "RIS area", "raw4 uniform", "raw4 area", "raw8 uniform", "raw8 area")
Table = [["Fixture", "Method", "RGB RMSE", "RGB max delta", "RGB failures", "Historical failures", "Native failures"]]
Canvas = Image.new("RGB", (1210, 1550), "#121923")
Pen = ImageDraw.Draw(Canvas)
Font = ImageFont.load_default(size=12)
Pen.text(
    (12, 12),
    "Actual full-frame GPU-generated packets /32x32 /mean of two samples /SwiftShader CPU Vulkan",
    fill="white",
    font=Font,
)
Pen.text(
    (12, 32),
    "Native and GPU: linear exposure1024,no gamma. Error: maximum absolute channel difference x1e8 in red.",
    fill="white",
    font=Font,
)
for Method, Label in enumerate(Methods):
    Pen.text((110 + Method * 180, 62), Label, fill="white", font=Font)
Maximum, HistoricalMaximum, RgbFailures, HistoricalFailures = 0.0, 0.0, 0, 0
AllDifferences = []
for Fixture in range(3):
    Historical = np.frombuffer(
        gzip.decompress((Results / f"FacetCausal-baseline-{Fixture}.f32.gz").read_bytes()), dtype="<f4"
    ).reshape(3, 24576, 4)[0]
    for Method in range(6):
        G = Gpu[Fixture, Method, :, :, 0, :3].astype(np.float64)
        N = Native[Fixture, Method, :, :, 0, :3].astype(np.float64)
        H = Historical[Method * 4096 : Method * 4096 + 2048, :3].astype(np.float64).reshape(2, 1024, 3)
        V = Valid[Fixture, Method].astype(bool)
        Difference = np.abs(G - N)
        AllDifferences.append(Difference)
        Bad = (~V) | np.any(Difference > 1e-10 + 2e-4 * np.max(np.abs(N), axis=-1, keepdims=True), axis=-1)
        HistoryDifference = np.abs(N - H)
        BadHistory = (~V) | np.any(
            HistoryDifference > 1e-10 + 2e-4 * np.max(np.abs(H), axis=-1, keepdims=True), axis=-1
        )
        Maximum = max(Maximum, float(Difference.max()))
        HistoricalMaximum = max(HistoricalMaximum, float(HistoryDifference.max()))
        RgbFailures += int(Bad.sum())
        HistoricalFailures += int(BadHistory.sum())
        Table.append(
            [
                Fixture,
                Methods[Method],
                f"{np.sqrt(np.mean(Difference**2)):.9g}",
                f"{Difference.max():.9g}",
                int(Bad.sum()),
                int(BadHistory.sum()),
                int((~V).sum()),
            ]
        )
        for Row, Pixels in enumerate((N.mean(axis=0), G.mean(axis=0), Difference.max(axis=0))):
            if Row < 2:
                Encoded = np.rint(np.clip(Pixels * 1024, 0, 1) * 255).astype(np.uint8).reshape(32, 32, 3)
            else:
                Encoded = np.zeros((1024, 3), dtype=np.uint8)
                Encoded[:, 0] = np.rint(np.clip(Pixels.max(axis=-1) * 1e8, 0, 1) * 255).astype(np.uint8)
                Encoded = Encoded.reshape(32, 32, 3)
            Tile = Image.fromarray(Encoded)
            Label = ("Native", "Gpu", "Error")[Row]
            Tile.save(Images / f"{Prefix}-{Fixture}-{Method}-{Label}.png")
            Canvas.paste(
                Tile.resize((128, 128), Image.Resampling.NEAREST), (110 + Method * 180, 104 + Fixture * 466 + Row * 145)
            )
    for Row, Label in enumerate(("Native", "GPU", "Error x1e8")):
        Pen.text((10, 104 + Fixture * 466 + Row * 145), f"Fixture{Fixture}\n{Label}", fill="white", font=Font)
Pen.text(
    (12, 1518),
    "Full image parity only. Two samples are not converged quality evidence;no quality/speed/default promotion.",
    fill="white",
    font=Font,
)
Canvas.save(Images / f"{Prefix}Comparison.png")
Widths = [max(len(str(Row[Column])) for Row in Table) for Column in range(len(Table[0]))]


def Render(Row):
    return "| " + " | ".join(str(Cell).ljust(Width) for Cell, Width in zip(Row, Widths)) + " |"


(Results / f"{Prefix}-comparison.md").write_text(
    "\n".join([Render(Table[0]), Render(["-" * Width for Width in Widths])] + [Render(Row) for Row in Table[1:]]) + "\n"
)
Passed = "pass=1" in Semantic[0][-2] and RgbFailures == 0 and HistoricalFailures == 0 and DiagnosticFailures == 0
Summary = (
    f"Image packet parity={'PASS' if Passed else 'FAIL'};frozen gates unchanged.\n"
    f"36 images,36864 packets/build;12 CTests/build,20322 focused checks/build.\n"
    f"Packet RGB failed pixels={RgbFailures};historical native RGB failures={HistoricalFailures};native invalid={int((Valid == 0).sum())}.\n"
    f"Maximum packet RGB difference={Maximum:.17g};RGB RMSE={np.sqrt(np.mean(np.stack(AllDifferences)**2)):.17g}.\n"
    f"Maximum native-versus-historical RGB difference={HistoricalMaximum:.17g}.\n" + Semantic[0][-2] + "\n"
    f"Offline full-diagnostic failed packets={DiagnosticFailures};two diagnostic buffers,validity and38 rows exact across builds.\n"
    f"Nine prior manifests/{Prior} entries unchanged;transport HELD,norm promotion FAIL preserved.\n"
    "SwiftShader CPU Vulkan;host ASan+UBSan,driver uninstrumented,leaks0. No hardware/speed/quality acceptance.\n"
    "Actual32x32 images,common exposure1024,error x1e8;80-slab allocation is private exhibit diagnostics.\n"
    "Production348.046875MiB/defaults/32B unchanged. No PR/Actions.\n"
)
(Results / f"{Prefix}-parity.txt").write_text(Summary)
Paths = [
    Exhibit / "Code/FacetImageGpuSources.py",
    Exhibit / "Code/FacetImageGpuValidation.cpp",
    Exhibit / "Code/FacetImageGpuEvidence.py",
    Exhibit / "FacetImageGpu/CMakeLists.txt",
    Exhibit / "RunFacetImageGpu.sh",
    Exhibit / "FacetImageGpu.md",
]
Paths += sorted(Images.glob(f"{Prefix}*.png"))
Paths += sorted(P for P in Results.glob(f"{Prefix}-*") if P.name != f"{Prefix}-evidence-hashes.txt")
(Results / f"{Prefix}-evidence-hashes.txt").write_text(
    "".join(f"{hashlib.sha256(P.read_bytes()).hexdigest()}  {P.relative_to(Root).as_posix()}\n" for P in Paths)
)
print(Summary)
