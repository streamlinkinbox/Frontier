# 🧩 Independently audit fresh Slang readbacks,retained native images,analytic means,and all frozen visibility controls.
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
Prefix = "FacetAreaOcclusionGpu"
Builds = [Root / f"_ClaudeScratch/build/facet-area-occlusion-gpu-{Mode}" for Mode in ("release", "sanitized")]
for Old in ("FacetAreaTransport", "FacetAreaOcclusion"):
    for Line in (Results / f"{Old}-evidence-hashes.txt").read_text().splitlines():
        Digest, Name = Line.split("  ", 1)
        assert hashlib.sha256((Root / Name).read_bytes()).hexdigest() == Digest, Name
assert "acceptance=0" in (Results / "FacetAreaTransport-parity.txt").read_text()
Semantic = []
for Mode in ("release", "sanitized"):
    Text = (Results / f"{Prefix}-{Mode}.txt").read_text()
    assert "100% tests passed, 0 tests failed out of 6" in Text
    for Unit, Count in (("PROPOSAL", 193), ("PORT", 14831), ("SCORE", 1060), ("TRANSPORT", 1058)):
        assert f"FACET_AREA_{Unit}_UNIT checks={Count} pass=1" in Text
    assert "OCCLUSION_GPU_UNIT checks=8 pass=1" in Text
    assert "ANALYTIC_OCCLUSION_UNIT checks=132 paths=12 pass=1" in Text
    assert "type=4 driver_sanitized=0" in Text
    assert not re.search(r"ERROR: AddressSanitizer|runtime error:", Text)
    Rows = re.findall(r"^OCCLUSION_GPU_(?:DEVICE|CASE|TOTAL).*$", Text, re.M)
    assert len(Rows) == 14
    assert "primary=147456 candidates=983040 paths=786432 visibility=1572864" in Rows[-1]
    Semantic.append(Rows)
    for Digest, Name in re.findall(r"^([a-f0-9]{64})  (.+)$", Text, re.M):
        assert hashlib.sha256((Root / Name).read_bytes()).hexdigest() == Digest, Name
assert Semantic[0] == Semantic[1]
for Name in ("DiffuseGenerate.spv", "DiffuseResolve.spv", "FacetAreaOcclusionGpuSubmission.h"):
    assert (Builds[0] / Name).read_bytes() == (Builds[1] / Name).read_bytes()

# Reuse only the unchanged analytic integration function,not the old executable collector or native/GPU RGB.
Tree = ast.parse((Root / "Exhibits/LightCompute/Code/FacetAreaOcclusionEvidence.py").read_text())
Definition = next(
    Entry for Entry in Tree.body if isinstance(Entry, ast.FunctionDef) and Entry.name == "IntegrateQuadrature"
)
Scope = {"math": math}
exec(compile(ast.Module(body=[Definition], type_ignores=[]), "analytic quadrature", "exec"), Scope)
Coarse = Scope["IntegrateQuadrature"](32)
Fine = Scope["IntegrateQuadrature"](64)
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
Metrics = [["Layout", "Variant", "Method", "Native RMSE", "GPU RMSE", "Joint GPU RMSE", "Max RGB delta"]]
Controls = [["Layout", "Variant", "Correct positive", "Omit connection", "Omit suffix", "Omit both"]]
Font = ImageFont.load_default(size=13)
Small = ImageFont.load_default(size=12)
Canvas = Image.new("RGB", (1460, 1040), "#121923")
Pen = ImageDraw.Draw(Canvas)
Pen.text(
    (16, 14),
    "Fresh Slang/Vulkan sensitive controls /SwiftShader software device /layout0 RIS /actual readbacks",
    font=Font,
    fill="white",
)
Headings = (
    "GPU correct (x256)",
    "Native correct (x256)",
    "Abs delta (x1e9)",
    "Omit connection (x256)",
    "Omit suffix (x256)",
    "Omit both (x256)",
)
MeanChecks = 0
MaximumPacket = MaximumImage = MaximumPrimary = 0.0


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


def EncodeImage(Pixels, Name, Exposure):
    assert all(math.isfinite(Channel) and Channel >= 0 for Pixel in Pixels for Channel in Pixel)
    Encoded = [tuple(round(255 * min(1, Channel * Exposure)) for Channel in Pixel) for Pixel in Pixels]
    Tile = Image.new("RGB", (32, 32))
    Tile.putdata(Encoded)
    Tile.save(Images / Name)
    assert Image.open(Images / Name).tobytes() == bytes(Channel for Pixel in Encoded for Channel in Pixel)
    return Tile


def EvaluateRmse(First, Second):
    return math.sqrt(sum((A[Axis] - B[Axis]) ** 2 for A, B in zip(First, Second) for Axis in range(3)) / 3072)


def ValidateMean(EstimateRgb, ReferenceRgb, Layout, Variant, Method, Label):
    global MeanChecks
    for Axis in range(3):
        Residuals = [First[Axis] - Second[Axis] for First, Second in zip(EstimateRgb, ReferenceRgb)]
        Mean = sum(Residuals) / 1024
        SE = math.sqrt(sum((Residual - Mean) ** 2 for Residual in Residuals) / (1023 * 1024))
        Accepted = abs(Mean) <= 6 * SE + 2e-5
        Means.append([str(Layout), str(Variant), Method, Label, str(Axis), f"{Mean:.8g}", f"{SE:.8g}", str(Accepted)])
        assert Accepted, (Layout, Variant, Method, Label, Axis, Mean, SE)
        MeanChecks += 1


for Layout in range(3):
    for Variant in range(4):
        Name = f"OcclusionGpu{Layout}{Variant}.f32"
        Gpu = (Builds[0] / "captures" / Name).read_bytes()
        assert len(Gpu) == 40 * Count * 16
        assert Gpu == (Builds[1] / "captures" / Name).read_bytes()
        Archive = Results / f"{Prefix}-{Layout}{Variant}.f32.xz"
        Archive.write_bytes(lzma.compress(Gpu, preset=6))
        assert lzma.decompress(Archive.read_bytes()) == Gpu
        Native = gzip.decompress((Results / f"FacetAreaOcclusion-{Layout}{Variant}.f32.gz").read_bytes())
        assert all(math.isfinite(Channel) for Quartet in struct.iter_unpack("<4f", Gpu) for Channel in Quartet)
        Positive = [0] * 4
        for Pixel in range(Count):
            for Slab in range(5):
                First, Second = DecodePacket(Gpu, Slab, Pixel), DecodePacket(Native, Slab, Pixel)
                Delta = max(abs(A - B) for A, B in zip(First, Second))
                assert Delta <= 2e-5, (Layout, Variant, Slab, Pixel, Delta)
                if Slab < 4:
                    MaximumPacket = max(MaximumPacket, Delta)
                    assert First[3] == 0
                else:
                    MaximumPrimary = max(MaximumPrimary, Delta)
            Rgb = [DecodePacket(Gpu, Slab, Pixel)[:3] for Slab in range(4)]
            for Slab, Triplet in enumerate(Rgb):
                Positive[Slab] += all(Channel > 0 for Channel in Triplet)
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
        Controls.append([str(Layout), Variants[Variant], *map(str, Positive)])
        for Method in range(3):
            Estimates = [Estimate(Gpu, Method, Slab) for Slab in range(4)]
            NativeEstimates = [Estimate(Native, Method, Slab) for Slab in range(4)]
            Reference = References[Layout] if Variant == 0 else [[0.0, 0.0, 0.0] for _ in range(1024)]
            ValidateMean(Estimates[0], Reference, Layout, Variant, Methods[Method], "correct")
            ValidateMean(Estimates[3], References[Layout], Layout, Variant, Methods[Method], "joint omitted")
            Delta = max(
                abs(A[Axis] - B[Axis])
                for First, Second in zip(Estimates, NativeEstimates)
                for A, B in zip(First, Second)
                for Axis in range(3)
            )
            assert Delta <= 2e-5
            MaximumImage = max(MaximumImage, Delta)
            Metrics.append(
                [
                    str(Layout),
                    str(Variant),
                    Methods[Method],
                    f"{EvaluateRmse(NativeEstimates[0], Reference):.9g}",
                    f"{EvaluateRmse(Estimates[0], Reference):.9g}",
                    f"{EvaluateRmse(Estimates[3], References[Layout]):.9g}",
                    f"{Delta:.8g}",
                ]
            )
            Tiles = [
                EncodeImage(Pixels, f"{Prefix}-{Layout}-{Variant}-{Method}-{Slab}.png", 256)
                for Slab, Pixels in enumerate(Estimates)
            ]
            Difference = [
                [abs(A[Axis] - B[Axis]) for Axis in range(3)] for A, B in zip(Estimates[0], NativeEstimates[0])
            ]
            DeltaTile = EncodeImage(Difference, f"{Prefix}-delta-{Layout}-{Variant}-{Method}.png", 1e9)
            if Layout == 0 and Method == 0:
                NativeTile = Image.open(Images / f"FacetAreaOcclusion-{Layout}-{Variant}-{Method}-0.png")
                for Column, Tile in enumerate((Tiles[0], NativeTile, DeltaTile, Tiles[1], Tiles[2], Tiles[3])):
                    X, Y = 175 + Column * 210, 95 + Variant * 230
                    Canvas.paste(Tile.resize((160, 160), Image.Resampling.NEAREST), (X, Y))
                    if Variant == 0:
                        Pen.text((X, Y - 27), Headings[Column], font=Small, fill="white")
                Pen.text((12, Y + 65), Variants[Variant], font=Font, fill="white")
assert MeanChecks == 216
Pen.text(
    (16, 1005),
    "Actual RGB,nearest enlarged,no gamma. Diagnostic receiver-only view;not a production scene or physical GPU benchmark.",
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


for Name, Rows in (("means", Means), ("comparison", Metrics), ("controls", Controls)):
    (Results / f"{Prefix}-{Name}.md").write_text(EncodeTable(Rows))
Summary = (
    "PASS sensitive Slang/Vulkan diagnostic;SwiftShader software device,not a physical GPU.\n"
    "Six CTests and17282 focused units/build;Release and host ASan/UBSan. External driver unsanitized,LSan disabled.\n"
    "147456 primary observations,983040 proposals,786432 paths,1572864 visibility decisions/build pass native/analytic audits.\n"
    f"Native/GPU max packet/ablation RGB delta={MaximumPacket:.17g};max image RGB delta={MaximumImage:.17g}.\n"
    f"Maximum primary position delta={MaximumPrimary:.17g};216 quadrature mean gates pass;convergence={Convergence:.17g}.\n"
    "Every isolated affected omission restores positive RGB in all12288 packets/layout;all unaffected controls stay zero.\n"
    "Twelve buffers,14 semantic rows,two SPIR-V binaries and submission prefix identical across builds.\n"
    "180 actual GPU/difference PNG arrays verified,lossless40-slab archives retained.\n"
    "Previous two evidence manifests unchanged;historical transport acceptance=0 remains.\n"
    "No engine/default/production promotion,physical timing,Windows/editor/history/material acceptance.\n"
    "Production348.046875MiB output and32B descriptors unchanged;no PR/Actions.\n"
)
(Results / f"{Prefix}-parity.txt").write_text(Summary)
print(Summary)
Files = sorted(
    [
        *Results.glob(f"{Prefix}*"),
        *Images.glob(f"{Prefix}*"),
        *Root.glob("Exhibits/LightCompute/Code/FacetAreaOcclusionGpu*"),
        Root / "Exhibits/LightCompute/FacetAreaOcclusionGpu.md",
        Root / "Exhibits/LightCompute/RunFacetAreaOcclusionGpu.sh",
        Root / "Exhibits/LightCompute/FacetAreaOcclusionGpu/CMakeLists.txt",
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
