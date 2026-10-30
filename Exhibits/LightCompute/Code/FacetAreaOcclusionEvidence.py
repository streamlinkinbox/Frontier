# 🧩 Audit predeclared positive-transport occlusion controls against analytic quadrature and retained regressions.
from pathlib import Path
import gzip
import hashlib
import math
import re
import struct
from PIL import Image, ImageDraw, ImageFont

Root = Path(__file__).resolve().parents[3]
Results = Root / "Exhibits/LightCompute/Results"
Images = Root / "Exhibits/LightCompute/Images"
Prefix = "FacetAreaOcclusion"
Builds = [Root / f"_ClaudeScratch/build/facet-area-occlusion-{M}" for M in ("release", "sanitized")]
# The entire previous transport evidence remains unchanged,including its failed acceptance gate.
for Line in (Results / "FacetAreaTransport-evidence-hashes.txt").read_text().splitlines():
    H, N = Line.split("  ", 1)
    assert hashlib.sha256((Root / N).read_bytes()).hexdigest() == H, N
assert "acceptance=0" in (Results / "FacetAreaTransport-parity.txt").read_text()
Semantics = []
for Mode in ("release", "sanitized"):
    Text = (Results / f"{Prefix}-{Mode}.txt").read_text()
    assert "100% tests passed, 0 tests failed out of 5" in Text
    for Unit, Count in (("TRANSPORT", 1058), ("SCORE", 1060), ("PROPOSAL", 193), ("PORT", 14831)):
        assert f"FACET_AREA_{Unit}_UNIT checks={Count} pass=1" in Text
    assert "ANALYTIC_OCCLUSION_UNIT checks=132 paths=12 pass=1" in Text
    assert not re.search(r"ERROR: AddressSanitizer|runtime error:", Text)
    Lines = [L for L in Text.splitlines() if L.startswith("ANALYTIC_OCCLUSION_")]
    assert (
        len(Lines) == 14
        and Lines[-1] == "ANALYTIC_OCCLUSION_TOTAL primary=147456 paths=786432 visibility=1572864 pass=1"
    )
    Semantics.append(Lines)
    for H, N in re.findall(r"^([a-f0-9]{64})  (.+)$", Text, re.M):
        assert hashlib.sha256((Root / N).read_bytes()).hexdigest() == H, N
assert Semantics[0] == Semantics[1]
Count = 12288
Palettes = ((0.7, 0.4, 0.2), (0.2, 0.7, 0.3), (0.25, 0.35, 0.8))
Scales = (1.0, 0.75, 1.5)
Offsets = ((0, 0, 0), (-1.5, 0.4, 0.3), (0.7, -0.6, -0.2))
Methods = ("RIS", "raw4", "raw8")
Variants = ("Clear", "Connection blocked", "Suffix blocked", "Both blocked")
Ablations = ("Correct visibility", "Connection omitted", "Suffix omitted", "Both omitted")
Font = ImageFont.load_default(size=14)
Small = ImageFont.load_default(size=12)
Canvas = Image.new("RGB", (1140, 1040), "#121923")
Pen = ImageDraw.Draw(Canvas)
Pen.text(
    (16, 12),
    "Actual fresh native ablations /layout0,RIS /32x32 nearest enlarged /linear exposure256",
    font=Font,
    fill="white",
)
PaletteSheet = Image.new("RGB", (900, 310), "#121923")
PalettePen = ImageDraw.Draw(PaletteSheet)
PalettePen.text(
    (16, 12),
    "Fresh native clear estimates /three declared layouts and palettes /same exposure256",
    font=Font,
    fill="white",
)


def IntegrateQuadrature(Steps):
    Samples = [-0.25 + (I + 0.5) * 0.5 / Steps for I in range(Steps)]
    Ref = []
    for P in range(1024):
        X = -0.2 + 0.4 * ((P % 32) + 0.5) / 32
        Y = -0.2 + 0.4 * ((P // 32) + 0.5) / 32
        Integral = (
            sum(4 / (((X - A) ** 2 + (Y - B) ** 2 + 4) ** 2) for A in Samples for B in Samples) * 0.25 / (Steps * Steps)
        )
        Ref.append(Integral * 0.6 / (math.pi * math.pi))
    return Ref


Coarse = IntegrateQuadrature(32)
Fine = IntegrateQuadrature(64)
References = [
    [[K * A * L * R for A, L, R in zip(Palette, (1, 0.7, 0.4), (0.6, 0.5, 0.4))] for K in Fine] for Palette in Palettes
]
Convergence = max(
    abs(A - B) * max(Palettes[L][C] * (1, 0.7, 0.4)[C] * (0.6, 0.5, 0.4)[C] for C in range(3))
    for L in range(3)
    for A, B in zip(Coarse, Fine)
)
assert Convergence <= 2e-6
Means = ["|Layout|Variant|Method|Estimate|Channel|Residual|SE|Gate|", "|---:|---:|---|---|---:|---:|---:|---|"]
Metrics = ["|Layout|Variant|Method|Correct RMSE|Joint-omission RMSE|", "|---:|---:|---|---:|---:|"]
Controls = [
    "|Layout|Variant|Correct positive|Omit connection|Omit suffix|Omit both|",
    "|---:|---|---:|---:|---:|---:|",
]
MeanChecks = 0
PrimaryChecks = 0


def DecodePacket(Raw, Slab, Pixel):
    return struct.unpack_from("<4f", Raw, (Slab * Count + Pixel) * 16)


def Estimate(Raw, Method, Slab):
    return [
        [sum(DecodePacket(Raw, Slab, Method * 4096 + Run * 1024 + P)[C] for Run in range(4)) / 4 for C in range(3)]
        for P in range(1024)
    ]


def EncodeImage(Pixels, Name):
    assert all(math.isfinite(C) and C >= 0 for P in Pixels for C in P)
    Encoded = [tuple(round(255 * min(1, C * 256)) for C in P) for P in Pixels]
    Tile = Image.new("RGB", (32, 32))
    Tile.putdata(Encoded)
    Tile.save(Images / Name)
    assert Image.open(Images / Name).tobytes() == bytes(C for P in Encoded for C in P)
    return Tile


def ValidateMeanGate(E, Ref, Layout, Variant, Method, Label):
    global MeanChecks
    for C in range(3):
        D = [A[C] - B[C] for A, B in zip(E, Ref)]
        Mean = sum(D) / 1024
        SE = math.sqrt(sum((X - Mean) ** 2 for X in D) / (1023 * 1024))
        Passed = abs(Mean) <= 6 * SE + 2e-5
        Means.append(f"|{Layout}|{Variant}|{Method}|{Label}|{C}|{Mean:.8g}|{SE:.8g}|{Passed}|")
        assert Passed, (Layout, Variant, Method, Label, C, Mean, SE)
        MeanChecks += 1


for Layout in range(3):
    for Variant in range(4):
        Name = f"AnalyticOcclusion{Layout}{Variant}.f32"
        Native = (Builds[0] / "captures" / Name).read_bytes()
        assert len(Native) == 5 * Count * 16 and Native == (Builds[1] / "captures" / Name).read_bytes()
        (Results / f"{Prefix}-{Layout}{Variant}.f32.gz").write_bytes(gzip.compress(Native, mtime=0))
        Positive = [0] * 4
        for P in range(Count):
            Primary = DecodePacket(Native, 4, P)
            Pixel = P % 1024
            Declared = (
                Offsets[Layout][0] + Scales[Layout] * (-0.2 + 0.4 * ((Pixel % 32) + 0.5) / 32),
                Offsets[Layout][1] + Scales[Layout] * (-0.2 + 0.4 * ((Pixel // 32) + 0.5) / 32),
                Offsets[Layout][2] + 2 * Scales[Layout],
            )
            assert max(abs(A - B) for A, B in zip(Primary[:3], Declared)) <= 2e-5
            PrimaryChecks += 1
            assert all(DecodePacket(Native, Slab, P)[3] == 0 for Slab in range(5))
            RGB = [DecodePacket(Native, S, P)[:3] for S in range(4)]
            for S, Triplet in enumerate(RGB):
                assert all(math.isfinite(C) and C >= 0 for C in Triplet)
                Positive[S] += all(C > 0 for C in Triplet)
            if Variant == 0:
                assert all(C > 0 for C in RGB[0])
            else:
                assert RGB[0] == (0.0, 0.0, 0.0)
            assert all(C > 0 for C in RGB[3])
            if Variant == 1:
                assert all(C > 0 for C in RGB[1]) and RGB[2] == (0.0, 0.0, 0.0)
            if Variant == 2:
                assert all(C > 0 for C in RGB[2]) and RGB[1] == (0.0, 0.0, 0.0)
            if Variant == 3:
                assert RGB[1] == RGB[2] == (0.0, 0.0, 0.0)
        Controls.append(f"|{Layout}|{Variants[Variant]}|" + "|".join(map(str, Positive)) + "|")
        for Method in range(3):
            Estimates = [Estimate(Native, Method, S) for S in range(4)]
            Ref = References[Layout] if Variant == 0 else [[0.0, 0.0, 0.0] for _ in range(1024)]
            ValidateMeanGate(Estimates[0], Ref, Layout, Variant, Methods[Method], "correct")
            ValidateMeanGate(Estimates[3], References[Layout], Layout, Variant, Methods[Method], "joint omitted")

            def EvaluateMse(A, B):
                return sum((X[C] - Y[C]) ** 2 for X, Y in zip(A, B) for C in range(3)) / 3072

            Metrics.append(
                f"|{Layout}|{Variant}|{Methods[Method]}|{math.sqrt(EvaluateMse(Estimates[0],Ref)):.9g}|"
                f"{math.sqrt(EvaluateMse(Estimates[3],References[Layout])):.9g}|"
            )
            for Slab, Pixels in enumerate(Estimates):
                Tile = EncodeImage(Pixels, f"{Prefix}-{Layout}-{Variant}-{Method}-{Slab}.png")
                if Layout == 0 and Method == 0:
                    X = 190 + Slab * 230
                    Y = 95 + Variant * 230
                    Canvas.paste(Tile.resize((160, 160), Image.Resampling.NEAREST), (X, Y))
                    if Variant == 0:
                        Pen.text((X, Y - 26), Ablations[Slab], font=Small, fill="white")
                    Pen.text(
                        (X, Y + 167),
                        "positive RGB" if max(max(P) for P in Pixels) > 0 else "exact zero RGB",
                        font=Small,
                        fill="white",
                    )
                    Pen.text((16, Y + 65), Variants[Variant], font=Font, fill="white")
                if Variant == 0 and Method == 0 and Slab == 0:
                    PaletteSheet.paste(Tile.resize((176, 176), Image.Resampling.NEAREST), (30 + Layout * 290, 65))
                    PalettePen.text(
                        (30 + Layout * 290, 250), f"Layout{Layout};scale{Scales[Layout]}", font=Font, fill="white"
                    )
assert MeanChecks == 216 and PrimaryChecks == 147456
Pen.text(
    (16, 1005),
    "All panels are actual native RGB,not a schematic. Fixed blockers isolate visibility;no new GPU or renderer promotion.",
    font=Small,
    fill="white",
)
Canvas.save(Images / f"{Prefix}Comparison.png")
PaletteSheet.save(Images / f"{Prefix}Layouts.png")


def EncodeMarkdownTable(Lines):
    Rows = [[Cell.strip() for Cell in Line.strip("|").split("|")] for Line in Lines]
    Widths = [max(len(Row[Column]) for Row in Rows) for Column in range(len(Rows[0]))]
    Rows[1] = ["-" * Width for Width in Widths]
    Encoded = ["| " + " | ".join(Cell.ljust(Width) for Cell, Width in zip(Row, Widths)) + " |" for Row in Rows]
    assert max(map(len, Encoded)) <= 120
    return "\n".join(Encoded) + "\n"


(Results / f"{Prefix}-means.md").write_text(EncodeMarkdownTable(Means))
(Results / f"{Prefix}-comparison.md").write_text(EncodeMarkdownTable(Metrics))
(Results / f"{Prefix}-controls.md").write_text(EncodeMarkdownTable(Controls))
Summary = (
    f"PASS independent native occlusion diagnostic:five CTests/build;132 new+17142 existing unit checks/build.\n"
    f"12 native variants,primary checks={PrimaryChecks};786432 paths and1572864 visibility checks/build match analytic/double BVH.\n"
    "Every isolated connection/suffix omission restores positive RGB in all12288 packets/layout;both-blocked single omissions stay zero.\n"
    f"Correct/joint-omission quadrature channel means={MeanChecks};32²/64² max convergence error={Convergence}.\n"
    "Twelve capture buffers and14 semantic rows identical across Release/ASan+UBSan;144 native PNG arrays verified.\n"
    "Complete prior transport manifest unchanged;its acceptance=0 remains retained,not reclassified.\n"
    "No engine modifications,new GPU comparison,general-scene quality promotion,Windows/editor/physical timing acceptance.\n"
    "Production348.046875MiB/defaults/32B descriptors unchanged.\n"
)
(Results / f"{Prefix}-parity.txt").write_text(Summary)
print(Summary)
Files = sorted(
    [
        *Results.glob(f"{Prefix}*"),
        *Images.glob(f"{Prefix}*"),
        *Root.glob("Exhibits/LightCompute/Code/FacetAreaOcclusion*"),
        Root / "Exhibits/LightCompute/FacetAreaOcclusion.md",
        Root / "Exhibits/LightCompute/RunFacetAreaOcclusion.sh",
        Root / "Exhibits/LightCompute/FacetAreaOcclusion/CMakeLists.txt",
    ]
)
Manifest = Results / f"{Prefix}-evidence-hashes.txt"
Manifest.write_text(
    "".join(f"{hashlib.sha256(P.read_bytes()).hexdigest()}  {P.relative_to(Root)}\n" for P in Files if P != Manifest)
)
