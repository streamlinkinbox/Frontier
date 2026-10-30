# 🧩 Compare composed API captures with retained native/Vulkan baselines and independent source-square quadrature.
from pathlib import Path
import ast
import gzip
import hashlib
import lzma
import math
import re
import struct
from PIL import Image, ImageDraw, ImageFont

Root = Path(__file__).resolve().parents[3]
Results = Root / "Exhibits/LightCompute/Results"
Images = Root / "Exhibits/LightCompute/Images"
Prefix = "FacetPacket"
Builds = [Root / f"_ClaudeScratch/build/facet-packet-{Mode}" for Mode in ("release", "sanitized")]
ManifestChecks = 0
for Old in ("FacetAreaTransport", "FacetAreaOcclusion", "FacetAreaOcclusionGpu"):
    for Line in (Results / f"{Old}-evidence-hashes.txt").read_text().splitlines():
        Digest, Name = Line.split("  ", 1)
        assert hashlib.sha256((Root / Name).read_bytes()).hexdigest() == Digest, Name
        ManifestChecks += 1
assert "acceptance=0" in (Results / "FacetAreaTransport-parity.txt").read_text()
Semantic = []
for Mode in ("release", "sanitized"):
    Text = (Results / f"{Prefix}-{Mode}.txt").read_text()
    assert "100% tests passed, 0 tests failed out of 6" in Text
    for Unit, Count in (("PROPOSAL", 193), ("PORT", 14831), ("SCORE", 1060), ("TRANSPORT", 1058)):
        assert f"FACET_AREA_{Unit}_UNIT checks={Count} pass=1" in Text
    assert "ANALYTIC_OCCLUSION_UNIT checks=132 paths=12 pass=1" in Text
    assert "FACET_PACKET_UNIT checks=31 allocation_calls=0 evaluations=6144 pass=1" in Text
    assert not re.search(r"ERROR: AddressSanitizer|runtime error:", Text)
    Rows = re.findall(r"^FACET_PACKET_(?:UNIT|CASE|TOTAL).*$", Text, re.M)
    assert len(Rows) == 14
    assert Rows[-1] == "FACET_PACKET_TOTAL evaluations=294912 candidates=1966080 paths=1572864 pass=1"
    Semantic.append(Rows)
    for Digest, Name in re.findall(r"^([a-f0-9]{64})  (.+)$", Text, re.M):
        assert hashlib.sha256((Root / Name).read_bytes()).hexdigest() == Digest, Name
assert Semantic[0] == Semantic[1]

# Execute only the mathematical reference function,never the historical collector or its file writes.
Tree = ast.parse((Root / "Exhibits/LightCompute/Code/FacetAreaOcclusionEvidence.py").read_text())
Definition = next(
    Entry for Entry in Tree.body if isinstance(Entry, ast.FunctionDef) and Entry.name == "IntegrateQuadrature"
)
Scope = {"math": math}
exec(compile(ast.Module(body=[Definition], type_ignores=[]), "analytic quadrature", "exec"), Scope)
Coarse, Fine = Scope["IntegrateQuadrature"](32), Scope["IntegrateQuadrature"](64)
Palettes = ((0.7, 0.4, 0.2), (0.2, 0.7, 0.3), (0.25, 0.35, 0.8))
References = [
    [
        [
            Coefficient * Albedo * Light * Receiver
            for Albedo, Light, Receiver in zip(Palette, (1, 0.7, 0.4), (0.6, 0.5, 0.4))
        ]
        for Coefficient in Fine
    ]
    for Palette in Palettes
]
Convergence = max(
    abs(First - Second) * Albedo * Light * Receiver
    for First, Second in zip(Coarse, Fine)
    for Palette in Palettes
    for Albedo, Light, Receiver in zip(Palette, (1, 0.7, 0.4), (0.6, 0.5, 0.4))
)
assert Convergence <= 2e-6
Count = 12288
Methods = ("RIS", "raw4", "raw8")
Variants = ("Clear", "Connection blocked", "Suffix blocked", "Both blocked")
Means = [["Layout", "Variant", "Method", "Estimate", "Channel", "Residual", "SE", "Gate"]]
Metrics = [["Layout", "Variant", "Method", "API RMSE", "Native RMSE", "Vulkan RMSE", "Max GPU delta"]]
Canvas = Image.new("RGB", (1370, 1040), "#121923")
Pen = ImageDraw.Draw(Canvas)
Font = ImageFont.load_default(size=13)
Small = ImageFont.load_default(size=12)
Pen.text(
    (16, 14),
    "Composed native API /actual fresh receiver RGB /layout0 RIS /linear exposure256,nearest enlarged",
    font=Font,
    fill="white",
)
Headings = ("Packet API", "Prior native caller", "Retained Vulkan", "Omit connection", "Omit suffix", "Omit both")
MeanChecks = 0
MaximumGpu = 0.0


def DecodePacket(Raw, Slab, Pixel):
    return struct.unpack_from("<4f", Raw, (Slab * Count + Pixel) * 16)


def Estimate(Raw, Method, Slab):
    return [
        [
            sum(DecodePacket(Raw, Slab, Method * 4096 + Seed * 1024 + Pixel)[Axis] for Seed in range(4)) / 4
            for Axis in range(3)
        ]
        for Pixel in range(1024)
    ]


def EncodeImage(Pixels, Name):
    Encoded = [tuple(round(255 * min(1, Channel * 256)) for Channel in Pixel) for Pixel in Pixels]
    Tile = Image.new("RGB", (32, 32))
    Tile.putdata(Encoded)
    Tile.save(Images / Name)
    assert Image.open(Images / Name).tobytes() == bytes(Channel for Pixel in Encoded for Channel in Pixel)
    return Tile


def EvaluateRmse(First, Second):
    return math.sqrt(sum((A[Axis] - B[Axis]) ** 2 for A, B in zip(First, Second) for Axis in range(3)) / 3072)


def ValidateMean(Pixels, Reference, Layout, Variant, Method, Label):
    global MeanChecks
    for Axis in range(3):
        Residuals = [A[Axis] - B[Axis] for A, B in zip(Pixels, Reference)]
        Mean = sum(Residuals) / 1024
        SE = math.sqrt(sum((Residual - Mean) ** 2 for Residual in Residuals) / (1023 * 1024))
        Accepted = abs(Mean) <= 6 * SE + 2e-5
        assert Accepted, (Layout, Variant, Method, Label, Axis)
        Means.append([str(Layout), str(Variant), Method, Label, str(Axis), f"{Mean:.8g}", f"{SE:.8g}", str(Accepted)])
        MeanChecks += 1


for Layout in range(3):
    for Variant in range(4):
        Name = f"FacetPacket{Layout}{Variant}.f32"
        Raw = (Builds[0] / "captures" / Name).read_bytes()
        assert len(Raw) == 5 * Count * 16
        assert Raw == (Builds[1] / "captures" / Name).read_bytes()
        Native = gzip.decompress((Results / f"FacetAreaOcclusion-{Layout}{Variant}.f32.gz").read_bytes())
        assert Raw == Native
        Gpu = lzma.decompress((Results / f"FacetAreaOcclusionGpu-{Layout}{Variant}.f32.xz").read_bytes())
        Archive = Results / f"{Prefix}-{Layout}{Variant}.f32.gz"
        Archive.write_bytes(gzip.compress(Raw, mtime=0))
        assert gzip.decompress(Archive.read_bytes()) == Raw
        assert all(math.isfinite(Channel) for Quartet in struct.iter_unpack("<4f", Raw) for Channel in Quartet)
        for Pixel in range(Count):
            Rgb = [DecodePacket(Raw, Slab, Pixel)[:3] for Slab in range(4)]
            assert all(Channel >= 0 for Triplet in Rgb for Channel in Triplet)
            assert all(DecodePacket(Raw, Slab, Pixel)[3] == 0 for Slab in range(5))
            if Variant == 0:
                assert all(Channel > 0 for Channel in Rgb[0])
            else:
                assert Rgb[0] == (0.0, 0.0, 0.0)
            assert all(Channel > 0 for Channel in Rgb[3])
            if Variant == 1:
                assert all(Channel > 0 for Channel in Rgb[1]) and Rgb[2] == (0.0, 0.0, 0.0)
            if Variant == 2:
                assert all(Channel > 0 for Channel in Rgb[2]) and Rgb[1] == (0.0, 0.0, 0.0)
            if Variant == 3:
                assert Rgb[1] == Rgb[2] == (0.0, 0.0, 0.0)
            for Slab in range(5):
                Delta = max(abs(A - B) for A, B in zip(DecodePacket(Raw, Slab, Pixel), DecodePacket(Gpu, Slab, Pixel)))
                assert Delta <= 2e-5
                if Slab < 4:
                    MaximumGpu = max(MaximumGpu, Delta)
        for Method in range(3):
            Estimates = [Estimate(Raw, Method, Slab) for Slab in range(4)]
            Reference = References[Layout] if Variant == 0 else [[0.0, 0.0, 0.0] for _ in range(1024)]
            ValidateMean(Estimates[0], Reference, Layout, Variant, Methods[Method], "correct")
            ValidateMean(Estimates[3], References[Layout], Layout, Variant, Methods[Method], "joint omitted")
            GpuEstimate = Estimate(Gpu, Method, 0)
            Delta = max(abs(A[Axis] - B[Axis]) for A, B in zip(Estimates[0], GpuEstimate) for Axis in range(3))
            Metrics.append(
                [
                    str(Layout),
                    str(Variant),
                    Methods[Method],
                    f"{EvaluateRmse(Estimates[0], Reference):.9g}",
                    f"{EvaluateRmse(Estimate(Native, Method, 0), Reference):.9g}",
                    f"{EvaluateRmse(GpuEstimate, Reference):.9g}",
                    f"{Delta:.8g}",
                ]
            )
            Tiles = [
                EncodeImage(Pixels, f"{Prefix}-{Layout}-{Variant}-{Method}-{Slab}.png")
                for Slab, Pixels in enumerate(Estimates)
            ]
            if Layout == 0 and Method == 0:
                NativeTile = Image.open(Images / f"FacetAreaOcclusion-{Layout}-{Variant}-{Method}-0.png")
                GpuTile = Image.open(Images / f"FacetAreaOcclusionGpu-{Layout}-{Variant}-{Method}-0.png")
                for Column, Tile in enumerate((Tiles[0], NativeTile, GpuTile, Tiles[1], Tiles[2], Tiles[3])):
                    X, Y = 170 + Column * 195, 95 + Variant * 230
                    Canvas.paste(Tile.resize((160, 160), Image.Resampling.NEAREST), (X, Y))
                    if Variant == 0:
                        Pen.text((X, Y - 27), Headings[Column], font=Small, fill="white")
                Pen.text((12, Y + 65), Variants[Variant], font=Font, fill="white")
assert MeanChecks == 216
Pen.text(
    (16, 1005),
    "API and prior native captures match bytewise. Vulkan baseline is retained software execution,not a fresh dispatch or physical GPU.",
    font=Small,
    fill="white",
)
Canvas.save(Images / f"{Prefix}Comparison.png")


def EncodeTable(Rows):
    Widths = [max(len(Row[Column]) for Row in Rows) for Column in range(len(Rows[0]))]
    Rows = [Rows[0], ["-" * Width for Width in Widths], *Rows[1:]]
    Lines = ["| " + " | ".join(Cell.ljust(Width) for Cell, Width in zip(Row, Widths)) + " |" for Row in Rows]
    assert max(map(len, Lines)) <= 120
    return "\n".join(Lines) + "\n"


for Name, Rows in (("means", Means), ("comparison", Metrics)):
    (Results / f"{Prefix}-{Name}.md").write_text(EncodeTable(Rows))
Summary = (
    "PASS opt-in composed native packet estimator:6 CTests/build,17305 focused units/build.\n"
    "New31 units include6144 ordinary-allocation-audited evaluations:0 allocation calls.\n"
    "294912 packet evaluations,1966080 proposals,1572864 paths/build;uniform and area agree on equal-area fixtures.\n"
    "All12 five-slab captures match prior native captures exactly and match Release/ASan+UBSan bytewise.\n"
    f"Retained software-Vulkan max packet/ablation RGB delta={MaximumGpu:.17g};216 independent quadrature means pass.\n"
    f"Quadrature convergence={Convergence:.17g};144 actual API image arrays verified.\n"
    f"All{ManifestChecks} old manifest entries verified unchanged;historical acceptance=0 retained.\n"
    "Cold preparation is transactional;miss/invalid/unrepresentable estimates reject with no partial RGB.\n"
    "Header-only opt-in API;no old arithmetic/CMake/default edits,fresh GPU dispatch,physical speedup or Windows binary.\n"
    "Production348.046875MiB/defaults/32B descriptors unchanged;no PR/Actions.\n"
)
(Results / f"{Prefix}-parity.txt").write_text(Summary)
print(Summary)
Files = sorted(
    [
        *Results.glob(f"{Prefix}*"),
        *Images.glob(f"{Prefix}*"),
        *Root.glob("Exhibits/LightCompute/Code/FacetPacket*"),
        Root / "Exhibits/LightCompute/FacetPacket.md",
        Root / "Exhibits/LightCompute/RunFacetPacket.sh",
        Root / "Exhibits/LightCompute/FacetPacket/CMakeLists.txt",
        Root / "Frontier/Engine/Rendering/FacetAreaPacket.h",
    ]
)
Manifest = Results / f"{Prefix}-evidence-hashes.txt"
Manifest.write_text(
    "".join(
        f"{hashlib.sha256(Pathname.read_bytes()).hexdigest()}  {Pathname.relative_to(Root)}\n"
        for Pathname in Files
        if Pathname != Manifest
    )
)
