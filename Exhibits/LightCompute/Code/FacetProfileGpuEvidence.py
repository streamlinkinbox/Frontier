# 🧩 Retain runtime-sized profile readbacks,negative submissions and actual image comparisons under frozen gates.
from pathlib import Path
import gzip
import hashlib
import re
import numpy as np
from PIL import Image, ImageDraw, ImageFont

Root = Path(__file__).resolve().parents[3]
Exhibit = Root / "Exhibits/LightCompute"
Results, Images = Exhibit / "Results", Exhibit / "Images"
Prefix = "FacetProfileGpu"
Sizes = (1, 3, 7, 16, 31, 65)
Methods = ("RIS uniform", "RIS area", "raw4 uniform", "raw4 area", "raw8 uniform", "raw8 area")
Builds = [Root / f"_ClaudeScratch/build/facet-profile-gpu-{Mode}" for Mode in ("release", "sanitized")]
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
    "FacetImageGpu",
):
    for Line in (Results / f"{Old}-evidence-hashes.txt").read_text().splitlines():
        Digest, Name = Line.split("  ", 1)
        assert hashlib.sha256((Root / Name).read_bytes()).hexdigest() == Digest, Name
        Prior += 1
assert Prior == 1637
assert "acceptance=0" in (Results / "FacetAreaTransport-parity.txt").read_text()
assert "promotion=FAIL" in (Results / "FacetNorm-parity.txt").read_text()
Semantic = []
for Mode in ("release", "sanitized"):
    Text = (Results / f"{Prefix}-{Mode}.txt").read_text()
    assert "100% tests passed, 0 tests failed out of 13" in Text
    assert "PROFILE_GPU_UNIT checks=12 pass=1" in Text
    assert "SwiftShader Device (Subzero) type=4 driver_sanitized=0 leaks=0" in Text
    assert not re.search(r"ERROR: AddressSanitizer|runtime error:", Text)
    Rows = re.findall(r"^PROFILE_GPU_(?:IMAGE|REJECTION|TOTAL|EXIT).*$", Text, re.M)
    assert len(Rows) == 116
    assert "images=108 packets=110592" in Rows[-2]
    Semantic.append(Rows)
    for Digest, Name in re.findall(r"^([a-f0-9]{64})  (.+)$", Text, re.M):
        assert hashlib.sha256((Root / Name).read_bytes()).hexdigest() == Digest, Name
assert Semantic[0] == Semantic[1]
Arrays = []
for Name, Shape, Type in (
    ("FacetProfileGpu.f32", (3, 6, 6, 1024, 80, 4), "<f4"),
    ("FacetProfileNative.f32", (3, 6, 6, 1024, 80, 4), "<f4"),
    ("FacetProfileValid.u8", (3, 6, 6, 1024), "u1"),
    ("FacetProfileRejections.f32", (6, 1024, 80, 4), "<f4"),
):
    Raw = (Builds[0] / "captures" / Name).read_bytes()
    assert Raw == (Builds[1] / "captures" / Name).read_bytes()
    Array = np.frombuffer(Raw, dtype=Type).reshape(Shape)
    assert np.isfinite(Array).all()
    Archive = Results / f"{Prefix}-{Name.removeprefix('FacetProfile')}.gz"
    Archive.write_bytes(gzip.compress(Raw, mtime=0))
    assert gzip.decompress(Archive.read_bytes()) == Raw
    Arrays.append(Array)
Gpu, Native, Valid, Rejections = Arrays
# Execute only the exact retained diagnostic-check block,never the previous collector's I/O.
Previous = (Exhibit / "Code/FacetImageGpuEvidence.py").read_text()
Begin, End = Previous.index("A, B = Gpu.reshape"), Previous.index("First =")
Scope = {"Gpu": Gpu, "Native": Native, "Valid": Valid, "np": np}
exec(compile(Previous[Begin:End], "unchanged image diagnostic predicates", "exec"), Scope)
DiagnosticFailures = Scope["DiagnosticFailures"]
DiagnosticMask = Scope["DiagnosticBad"].reshape(3, 6, 6, 1024)
ExpectedRejections = np.zeros_like(Rejections)
ExpectedRejections[:, :, 79, 0] = np.array([1, 1, 1, 2, 2, 3])[:, None]
RejectionFailures = int(np.any(Rejections != ExpectedRejections, axis=(-1, -2)).sum())
First = (Builds[0] / "captures/FacetProfileFirstFailure.txt").read_bytes()
assert First == (Builds[1] / "captures/FacetProfileFirstFailure.txt").read_bytes()
(Results / f"{Prefix}-first-failure.txt").write_bytes(First)
OldGpu = np.frombuffer(gzip.decompress((Results / "FacetImageGpu-Gpu.f32.gz").read_bytes()), dtype="<f4").reshape(
    3, 6, 2, 1024, 80, 4
)
ControlDifference = float(np.abs(Gpu[:, 3, :, :, 0, :3].astype(np.float64) - OldGpu[:, :, 0, :, 0, :3]).max())
ControlExact = Gpu[:, 3].tobytes() == OldGpu[:, :, 0].tobytes()
Table = [["Fixture", "Support", "Method", "RGB RMSE", "RGB max delta", "Failed packets", "Native invalid"]]
HistoricalFailures, HistoricalMaximum = 0, 0.0
AllDifferences = []
Canvas = Image.new("RGB", (1120, 2110), "#121923")
Pen = ImageDraw.Draw(Canvas)
Font = ImageFont.load_default(size=12)
Pen.text(
    (12, 12),
    "Actual runtime source profiles /32x32 /sample0 /SwiftShader CPU Vulkan /linear exposure1024,no gamma",
    fill="white",
    font=Font,
)
Pen.text(
    (12, 34),
    "Profile16 is the historical control;65 uses subdivided geometry and reversed proposal order. Error x1e8 red.",
    fill="white",
    font=Font,
)
for Method, Label in enumerate(Methods):
    Pen.text((112 + 164 * Method, 64), Label, fill="white", font=Font)
DisplayMatches = 0
for Fixture in range(3):
    Historical = np.frombuffer(
        gzip.decompress((Results / f"FacetCausal-baseline-{Fixture}.f32.gz").read_bytes()), dtype="<f4"
    ).reshape(3, 24576, 4)[0]
    for SizeIndex, Support in enumerate(Sizes):
        for Method in range(6):
            G = Gpu[Fixture, SizeIndex, Method, :, 0, :3].astype(np.float64)
            N = Native[Fixture, SizeIndex, Method, :, 0, :3].astype(np.float64)
            Difference = np.abs(G - N)
            AllDifferences.append(Difference)
            V = Valid[Fixture, SizeIndex, Method].astype(bool)
            Bad = (~V) | np.any(Difference > 1e-10 + 2e-4 * np.max(np.abs(N), axis=-1, keepdims=True), axis=-1)
            Table.append(
                [
                    Fixture,
                    Support,
                    Methods[Method],
                    f"{np.sqrt(np.mean(Difference**2)):.9g}",
                    f"{Difference.max():.9g}",
                    int(DiagnosticMask[Fixture, SizeIndex, Method].sum()),
                    int((~V).sum()),
                ]
            )
            if Support == 16:
                H = Historical[Method * 4096 : Method * 4096 + 1024, :3].astype(np.float64)
                HistoricalMaximum = max(HistoricalMaximum, float(np.abs(N - H).max()))
                HistoricalFailures += int(
                    (
                        (~V) | np.any(np.abs(N - H) > 1e-10 + 2e-4 * np.max(np.abs(H), axis=-1, keepdims=True), axis=-1)
                    ).sum()
                )
            if Support not in (16, 65):
                continue
            Group = Fixture * 2 + (Support == 65)
            DisplayBytes = []
            for Row, Pixels in enumerate((N, G, Difference)):
                if Row < 2:
                    Encoded = np.rint(np.clip(Pixels * 1024, 0, 1) * 255).astype(np.uint8).reshape(32, 32, 3)
                    DisplayBytes.append(Encoded.tobytes())
                else:
                    Encoded = np.zeros((1024, 3), dtype=np.uint8)
                    Encoded[:, 0] = np.rint(np.clip(Pixels.max(axis=-1) * 1e8, 0, 1) * 255).astype(np.uint8)
                    Encoded = Encoded.reshape(32, 32, 3)
                Tile = Image.fromarray(Encoded)
                Label = ("Native", "Gpu", "Error")[Row]
                Tile.save(Images / f"{Prefix}-{Fixture}-{Support}-{Method}-{Label}.png")
                Canvas.paste(
                    Tile.resize((96, 96), Image.Resampling.NEAREST), (112 + Method * 164, 104 + Group * 330 + Row * 105)
                )
            DisplayMatches += DisplayBytes[0] == DisplayBytes[1]
        if Support in (16, 65):
            Group = Fixture * 2 + (Support == 65)
            for Row, Label in enumerate(("Native", "GPU", "Error")):
                Pen.text(
                    (10, 104 + Group * 330 + Row * 105),
                    f"Fixture{Fixture}\nN={Support}\n{Label}",
                    fill="white",
                    font=Font,
                )
Pen.text(
    (12, 2080),
    "Actual one-sample transport,not converged quality. Six support sizes1/3/7/16/31/65,not arbitrary-scene/hardware acceptance.",
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
Differences = np.stack(AllDifferences)
Passed = "pass=1" in Semantic[0][-2] and DiagnosticFailures == 0 and HistoricalFailures == 0 and RejectionFailures == 0
Summary = (
    f"Runtime-sized profile parity={'PASS' if Passed else 'FAIL'};frozen gates unchanged.\n"
    "Three layouts,six runtime sizes1/3/7/16/31/65,six methods,108 images/110592 packets per build.\n"
    "13 CTests/20334 focused checks per build;host ASan+UBSan,driver uninstrumented,leaks0.\n"
    f"Offline diagnostic failed packets={DiagnosticFailures};native invalid={int((Valid == 0).sum())};negative failed pixels={RejectionFailures}.\n"
    f"Maximum packet RGB difference={Differences.max():.17g};RGB RMSE={np.sqrt(np.mean(Differences**2)):.17g}.\n"
    f"Profile16 historical failures={HistoricalFailures};max native historical delta={HistoricalMaximum:.17g};prior GPU RGB max delta={ControlDifference:.17g};full80slabs exact={int(ControlExact)}.\n"
    + Semantic[0][-2]
    + "\n"
    f"All four buffers,first failure and116 semantic rows identical across builds.\n"
    f"Displayed byte-identical native/GPU image pairs={DisplayMatches}/36. Ten prior manifests/{Prior} entries unchanged.\n"
    "Original transport HELD,norm promotion FAIL preserved. SwiftShader CPU Vulkan,not physical GPU.\n"
    "Actual32x32 one-sample images,not converged quality. Production348.046875MiB/defaults/32B unchanged. No PR/Actions.\n"
)
(Results / f"{Prefix}-parity.txt").write_text(Summary)
Paths = [
    Exhibit / "Code/FacetProfileGpuSources.py",
    Exhibit / "Code/FacetProfileGpuValidation.cpp",
    Exhibit / "Code/FacetProfileGpuEvidence.py",
    Exhibit / "FacetProfileGpu/CMakeLists.txt",
    Exhibit / "RunFacetProfileGpu.sh",
    Exhibit / "FacetProfileGpu.md",
]
Paths += sorted(Images.glob(f"{Prefix}*.png"))
Paths += sorted(P for P in Results.glob(f"{Prefix}-*") if P.name != f"{Prefix}-evidence-hashes.txt")
(Results / f"{Prefix}-evidence-hashes.txt").write_text(
    "".join(f"{hashlib.sha256(P.read_bytes()).hexdigest()}  {P.relative_to(Root).as_posix()}\n" for P in Paths)
)
print(Summary)
