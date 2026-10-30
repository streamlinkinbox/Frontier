# 🧩 Independently integrate fresh RGB-norm rigs and retain failed frozen promotion gates without retuning.
from pathlib import Path
import gzip
import hashlib
import math
import re
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont

Root = Path(__file__).resolve().parents[3]
Results = Root / "Exhibits/LightCompute/Results"
Images = Root / "Exhibits/LightCompute/Images"
Prefix = "FacetNorm"
Builds = [Root / f"_ClaudeScratch/build/facet-norm-{Mode}" for Mode in ("release", "sanitized")]
ManifestChecks = 0
for Old in ("FacetAreaTransport", "FacetAreaOcclusion", "FacetAreaOcclusionGpu", "FacetPacket", "FacetMixed"):
    for Line in (Results / f"{Old}-evidence-hashes.txt").read_text().splitlines():
        Digest, Name = Line.split("  ", 1)
        assert hashlib.sha256((Root / Name).read_bytes()).hexdigest() == Digest, Name
        ManifestChecks += 1
assert "acceptance=0" in (Results / "FacetAreaTransport-parity.txt").read_text()
Semantic = []
for Mode in ("release", "sanitized"):
    Text = (Results / f"{Prefix}-{Mode}.txt").read_text()
    assert "100% tests passed, 0 tests failed out of 8" in Text
    for Unit, Count in (("PROPOSAL", 193), ("PORT", 14831), ("SCORE", 1060), ("TRANSPORT", 1058)):
        assert f"FACET_AREA_{Unit}_UNIT checks={Count} pass=1" in Text
    assert "ANALYTIC_OCCLUSION_UNIT checks=132 paths=12 pass=1" in Text
    assert "FACET_PACKET_UNIT checks=31 allocation_calls=0 evaluations=6144 pass=1" in Text
    assert "FACET_MIXED_UNIT checks=2796 pass=1" in Text
    assert not re.search(r"ERROR: AddressSanitizer|runtime error:", Text)
    Rows = re.findall(r"^FACET_NORM_(?:UNIT|CASE|TOTAL).*$", Text, re.M)
    assert len(Rows) == 14
    assert Rows[-1] == (
        "FACET_NORM_TOTAL evaluations=3145728 candidates=22020096 paths=15728640 "
        "visibility=31457280 api_baselines=2359296 disagreements=0"
    )
    Semantic.append(Rows)
    for Digest, Name in re.findall(r"^([a-f0-9]{64})  (.+)$", Text, re.M):
        assert hashlib.sha256((Root / Name).read_bytes()).hexdigest() == Digest, Name
assert Semantic[0] == Semantic[1]
UnitChecks = 20101 + int(re.search(r"checks=(\d+)", Semantic[0][0])[1])
# Independent declaration,not decoded from native captures or imported from a side-effecting collector.
Rigs = (
    (
        1.0,
        ((-0.9, -0.5, -0.25, 0.25, (0.9, 0.035, 0.02)), (0.05, 0.75, -0.2, 0.2, (0.2, 0.45, 0.8))),
        ((0.55, 0.5, 0.45), (0.25, 0.65, 0.45)),
        (-0.20, 0.11, -0.12, 0.12),
        (1.65, 2.25, -0.6, 0.6),
    ),
    (
        0.8,
        ((-0.75, -0.3, -0.3, 0.3, (0.025, 0.85, 0.05)), (0.15, 0.85, -0.25, 0.25, (0.5, 0.45, 0.5))),
        ((0.65, 0.3, 0.6), (0.3, 0.7, 0.2)),
        (-0.18, 0.14, -0.15, 0.08),
        (0.68, 1.0, -0.6, 0.6),
    ),
    (
        1.25,
        ((-0.85, -0.45, -0.15, 0.15, (0.75, 0.65, 0.015)), (0.12, 0.82, -0.32, 0.32, (0.015, 0.4, 0.85))),
        ((0.5, 0.7, 0.25), (0.75, 0.25, 0.6)),
        (-0.25, 0.08, -0.06, 0.17),
        (1.55, 2.12, -0.6, 0.6),
    ),
)
Methods = ("mean U", "norm U", "raw4 U", "raw8 U", "mean A", "norm A", "raw4 A", "raw8 A")
Variants = ("Clear", "Connection", "Suffix", "Both")
Pixels = np.arange(1024)
ReceiverX = -0.195 + 0.4 * ((Pixels % 32) + 0.5) / 32
ReceiverY = -0.2 + 0.4 * ((Pixels // 32) + 0.5) / 32
SeedInputs = [
    (
        (np.arange(4096, dtype=np.uint32) + 0x6DB41895)
        ^ (Rig * 0x6C8E9CF5 & 0xFFFFFFFF)
        ^ (Cohort * 0x9E3779B9 & 0xFFFFFFFF)
    )
    for Rig in range(3)
    for Cohort in range(8)
]
assert np.unique(np.concatenate(SeedInputs)).size == 3 * 8 * 4096


def IntegrateClippedSources(Rig, Steps):
    Scale, Sources, Colours, Connection, Suffix = Rig
    ReceiverRgb = np.where((ReceiverX > ReceiverY)[:, None], Colours[0], Colours[1])
    Reference = np.zeros((4, 4, 1024, 3), dtype=np.float64)
    Samples = (np.arange(Steps, dtype=np.float64) + 0.5) / Steps
    SuffixLow = Suffix[0] - (1 - 0.002 / Scale) * (0.8 / 0.6)
    SuffixHigh = Suffix[1] - (1 - 0.002 / Scale) * (0.8 / 0.6)
    # The directional suffix also has a y interval;split it rather than assuming the declared width covers all sources.
    for Pixel, (ReceiverFirst, ReceiverSecond) in enumerate(zip(ReceiverX, ReceiverY)):
        ConnectionLow, ConnectionHigh = 2 * Connection[0] - ReceiverFirst, 2 * Connection[1] - ReceiverFirst
        VerticalLow, VerticalHigh = 2 * Connection[2] - ReceiverSecond, 2 * Connection[3] - ReceiverSecond
        for Left, Right, Bottom, Top, Colour in Sources:
            X = sorted(
                set(
                    (
                        Left,
                        Right,
                        *[Cut for Cut in (ConnectionLow, ConnectionHigh, SuffixLow, SuffixHigh) if Left < Cut < Right],
                    )
                )
            )
            Y = sorted(
                set(
                    (
                        Bottom,
                        Top,
                        *[Cut for Cut in (VerticalLow, VerticalHigh, Suffix[2], Suffix[3]) if Bottom < Cut < Top],
                    )
                )
            )
            Factor = np.array(Colour) * ReceiverRgb[Pixel] * np.array((1.0, 0.7, 0.4)) * 0.6 / math.pi**2
            for First, Last in zip(X, X[1:]):
                SourceX = First + (Last - First) * Samples
                for Low, High in zip(Y, Y[1:]):
                    SourceY = Low + (High - Low) * Samples
                    Squared = (SourceX[:, None] - ReceiverFirst) ** 2 + (SourceY[None, :] - ReceiverSecond) ** 2 + 4
                    Integral = np.mean(4 / Squared**2) * (Last - First) * (High - Low)
                    XMid, YMid = (First + Last) / 2, (Low + High) / 2
                    ConnectionBlocked = ConnectionLow <= XMid <= ConnectionHigh and VerticalLow <= YMid <= VerticalHigh
                    SuffixBlocked = SuffixLow <= XMid <= SuffixHigh and Suffix[2] <= YMid <= Suffix[3]
                    for Variant in range(4):
                        SuffixVisible = not (Variant & 2 and SuffixBlocked)
                        ConnectionVisible = not (Variant & 1 and ConnectionBlocked)
                        for Slab, Visible in enumerate(
                            (SuffixVisible and ConnectionVisible, SuffixVisible, ConnectionVisible, True)
                        ):
                            if Visible:
                                Reference[Variant, Slab, Pixel] += Factor * Integral
    return Reference


def EncodeTable(Rows):
    Widths = [max(len(Row[Column]) for Row in Rows) for Column in range(len(Rows[0]))]
    Lines = [
        "| " + " | ".join(Cell.ljust(Width) for Cell, Width in zip(Row, Widths)) + " |"
        for Row in [Rows[0], ["-" * Width for Width in Widths], *Rows[1:]]
    ]
    assert max(map(len, Lines)) <= 120
    return "\n".join(Lines) + "\n"


def EncodeImage(Colour, Name):
    Encoded = np.rint(np.clip(Colour * 256, 0, 1) * 255).astype(np.uint8).reshape(32, 32, 3)
    Tile = Image.fromarray(Encoded)
    Tile.save(Images / Name)
    assert Image.open(Images / Name).tobytes() == Encoded.tobytes()
    return Tile


Means = [["Rig", "Variant", "Arm", "Slab", "RGB", "Residual", "SE", "Gate"]]
Metrics = [["Rig", "Variant", "Arm", "MSE", "Cohort MSE SE", "RMSE"]]
Contrasts = [["Rig", "Variant", "Baseline", "Norm/base MSE", "Paired MSE delta", "Delta SE", "Case gate"]]
Controls = [["Rig", "Arm", "Conn changes", "Suffix changes", "Both conn", "Both suffix"]]
AllMse = np.zeros((3, 4, 8, 8))  # rig,variant,arm,independent cohort
AllCaptures = []
MeanFailures, Convergence = 0, 0.0
Font = ImageFont.load_default(size=13)
Canvas = Image.new("RGB", (1050, 1970), "#121923")
Ablations = Image.new("RGB", (1050, 720), "#121923")
Pen, AblationPen = ImageDraw.Draw(Canvas), ImageDraw.Draw(Ablations)
Pen.text(
    (12, 10),
    "Actual four-packet cohort0 /fresh RGB norm holdouts /common exposure256,no gamma",
    font=Font,
    fill="white",
)
AblationPen.text(
    (12, 10), "Actual uniform-norm ablations /rig0,cohort0 /common exposure256,no gamma", font=Font, fill="white"
)
for Column, Label in enumerate(
    ("raw4 uniform", "mean RIS uniform", "norm RIS uniform", "raw8 uniform", "Clipped reference")
):
    Pen.text((140 + 180 * Column, 35), Label, font=Font, fill="white")
for Column, Label in enumerate(("Correct", "Omit connection", "Omit suffix", "Omit both", "Correct reference")):
    AblationPen.text((140 + 180 * Column, 35), Label, font=Font, fill="white")
for RigIndex, Rig in enumerate(Rigs):
    Coarse, Fine = [IntegrateClippedSources(Rig, Steps) for Steps in (32, 64)]
    Convergence = max(Convergence, float(np.max(np.abs(Coarse - Fine))))
    ReferenceBytes = np.stack((Coarse, Fine)).astype("<f8").tobytes()
    (Results / f"{Prefix}-reference-{RigIndex}.f64.gz").write_bytes(gzip.compress(ReferenceBytes, mtime=0))
    Captures = []
    for Variant in range(4):
        Name = f"FacetNorm{RigIndex}{Variant}.f64"
        Raw = (Builds[0] / "captures" / Name).read_bytes()
        assert len(Raw) == 8 * 4 * 8 * 1024 * 3 * 8 and Raw == (Builds[1] / "captures" / Name).read_bytes()
        Archive = Results / f"{Prefix}-{RigIndex}{Variant}.f64.gz"
        Archive.write_bytes(gzip.compress(Raw, mtime=0))
        assert gzip.decompress(Archive.read_bytes()) == Raw
        Values = np.frombuffer(Raw, dtype="<f8").reshape(8, 4, 8, 1024, 3)
        assert np.isfinite(Values).all() and (Values >= 0).all()
        Captures.append(Values)
        for Arm in range(8):
            CohortMse = np.mean((Values[:, 0, Arm] - Fine[Variant, 0]) ** 2, axis=(1, 2))
            AllMse[RigIndex, Variant, Arm] = CohortMse
            Metrics.append(
                [
                    str(RigIndex),
                    Variants[Variant],
                    Methods[Arm],
                    f"{CohortMse.mean():.12g}",
                    f"{CohortMse.std(ddof=1)/math.sqrt(8):.9g}",
                    f"{math.sqrt(CohortMse.mean()):.12g}",
                ]
            )
            for Slab in range(4):
                CohortMeans = Values[:, Slab, Arm].mean(axis=1)
                Residual = CohortMeans.mean(axis=0) - Fine[Variant, Slab].mean(axis=0)
                Se = CohortMeans.std(axis=0, ddof=1) / math.sqrt(8)
                for Axis in range(3):
                    Gate = abs(Residual[Axis]) <= 6 * Se[Axis] + 2e-5
                    MeanFailures += not Gate
                    Means.append(
                        [
                            str(RigIndex),
                            str(Variant),
                            Methods[Arm],
                            str(Slab),
                            str(Axis),
                            f"{Residual[Axis]:.9g}",
                            f"{Se[Axis]:.9g}",
                            "PASS" if Gate else "FAIL",
                        ]
                    )
                EncodeImage(Values[0, Slab, Arm], f"{Prefix}-{RigIndex}-{Variant}-{Arm}-{Slab}.png")
        for Baseline in (0, 2):
            Delta = AllMse[RigIndex, Variant, 1] - AllMse[RigIndex, Variant, Baseline]
            Ratio = AllMse[RigIndex, Variant, 1].mean() / AllMse[RigIndex, Variant, Baseline].mean()
            Contrasts.append(
                [
                    str(RigIndex),
                    Variants[Variant],
                    Methods[Baseline],
                    f"{Ratio:.9g}",
                    f"{Delta.mean():.12g}",
                    f"{Delta.std(ddof=1)/math.sqrt(8):.9g}",
                    "PASS" if Ratio <= 1 else "FAIL",
                ]
            )
        for Slab in range(4):
            EncodeImage(Fine[Variant, Slab], f"{Prefix}-reference-{RigIndex}-{Variant}-{Slab}.png")
        for Column, Arm in enumerate((2, 0, 1, 3, -1)):
            Tile = Image.open(
                Images
                / (
                    f"{Prefix}-reference-{RigIndex}-{Variant}-0.png"
                    if Arm == -1
                    else f"{Prefix}-{RigIndex}-{Variant}-{Arm}-0.png"
                )
            )
            X, Y = 140 + 180 * Column, 62 + (RigIndex * 4 + Variant) * 155
            Canvas.paste(Tile.resize((128, 128), Image.Resampling.NEAREST), (X, Y))
            Pen.text((10, Y + 55), f"Rig{RigIndex} {Variants[Variant]}", font=Font, fill="white")
        if RigIndex == 0:
            for Column in range(5):
                Tile = Image.open(
                    Images
                    / (
                        f"{Prefix}-reference-0-{Variant}-0.png"
                        if Column == 4
                        else f"{Prefix}-0-{Variant}-1-{Column}.png"
                    )
                )
                X, Y = 140 + 180 * Column, 62 + Variant * 155
                Ablations.paste(Tile.resize((128, 128), Image.Resampling.NEAREST), (X, Y))
                AblationPen.text((10, Y + 55), Variants[Variant], font=Font, fill="white")
    for Arm in range(8):
        Clear, Connection, Suffix, Both = Captures
        ConnChanges = int(np.count_nonzero(np.any(Connection[:, 1, Arm] != Connection[:, 0, Arm], axis=-1)))
        SuffixChanges = int(np.count_nonzero(np.any(Suffix[:, 2, Arm] != Suffix[:, 0, Arm], axis=-1)))
        BothConn = int(np.count_nonzero(np.any(Both[:, 1, Arm] != Both[:, 0, Arm], axis=-1)))
        BothSuffix = int(np.count_nonzero(np.any(Both[:, 2, Arm] != Both[:, 0, Arm], axis=-1)))
        for Slab in (1, 2):
            assert np.array_equal(Clear[:, 0, Arm], Clear[:, Slab, Arm])
        assert np.array_equal(Connection[:, 0, Arm], Connection[:, 2, Arm])
        assert np.array_equal(Suffix[:, 0, Arm], Suffix[:, 1, Arm])
        assert min(ConnChanges, SuffixChanges, BothConn, BothSuffix) > 0
        Controls.append([str(RigIndex), Methods[Arm], *map(str, (ConnChanges, SuffixChanges, BothConn, BothSuffix))])
    AllCaptures.append(Captures)
assert len(Means) - 1 == 1152
Canvas.save(Images / f"{Prefix}Comparison.png")
Ablations.save(Images / f"{Prefix}Ablations.png")
CaseMse = AllMse.mean(axis=-1)
AggregateMse = CaseMse.mean(axis=(0, 1))
Ratios = {Baseline: CaseMse[:, :, 1] / CaseMse[:, :, Baseline] for Baseline in (0, 2)}
Quality = all((Ratios[Baseline] <= 1).all() and AggregateMse[1] <= 0.98 * AggregateMse[Baseline] for Baseline in (0, 2))
Correctness = MeanFailures == 0 and Convergence <= 2e-6
Aggregate = [["Arm", "Paths", "Scores", "Equal-case MSE", "Relative to raw4 U"]]
for Arm, Method in enumerate(Methods):
    Aggregate.append(
        [
            Method,
            "8" if Arm % 4 == 3 else "4",
            "4" if Arm % 4 == 2 else "8",
            f"{AggregateMse[Arm]:.12g}",
            f"{AggregateMse[Arm]/AggregateMse[2]:.9g}",
        ]
    )
for Name, Rows in (
    ("means", Means),
    ("comparison", Metrics),
    ("contrasts", Contrasts),
    ("controls", Controls),
    ("aggregate", Aggregate),
):
    (Results / f"{Prefix}-{Name}.md").write_text(EncodeTable(Rows))
Summary = (
    f"Correctness={'PASS' if Correctness else 'FAIL'};frozen uniform-norm promotion={'PASS' if Quality and Correctness else 'FAIL'}.\n"
    f"8 CTests/build,{UnitChecks} focused checks/build;Release and host ASan+UBSan with leaks enabled.\n"
    "3145728 prototype packets/build;2359296 non-norm packets match unchanged API exactly.\n"
    "22020096 candidate draws,15728640 selected paths,31457280 independently checked visibility decisions/build.\n"
    "No analytic/double BVH disagreement;12 buffers and14 semantic rows byte-identical across builds.\n"
    f"1152 RGB mean gates;failures={MeanFailures};max32/64 clipped reference difference={Convergence:.17g}.\n"
    "8 independent four-packet cohorts/case;quality MSE averages cohort image MSE,not32-packet-image MSE.\n"
    f"Uniform norm/mean MSE ratios min/max={Ratios[0].min():.9g}/{Ratios[0].max():.9g};case wins={np.count_nonzero(Ratios[0]<=1)}/12;"
    f"aggregate ratio={AggregateMse[1]/AggregateMse[0]:.9g}.\n"
    f"Uniform norm/raw4 MSE ratios min/max={Ratios[2].min():.9g}/{Ratios[2].max():.9g};case wins={np.count_nonzero(Ratios[2]<=1)}/12;"
    f"aggregate ratio={AggregateMse[1]/AggregateMse[2]:.9g}.\n"
    f"384 actual cohort0 image arrays,48 analytic references;12 lossless cohort buffers and3 references retained.\n"
    f"All{ManifestChecks} prior manifest entries unchanged;historical transport acceptance=0 remains HELD.\n"
    "No engine/API/shader/default change,no new GPU/history/physical-time/Windows/production acceptance.\n"
    "Production348.046875MiB output/defaults/32B descriptors unchanged;no PR/Actions.\n"
    "Collector writes complete evidence before nonzero exit when a frozen gate fails;no threshold/seed/rig retuning.\n"
)
(Results / f"{Prefix}-parity.txt").write_text(Summary)
print(Summary)
Files = sorted(
    [
        *Results.glob(f"{Prefix}*"),
        *Images.glob(f"{Prefix}*"),
        *Root.glob("Exhibits/LightCompute/Code/FacetNorm*"),
        Root / "Exhibits/LightCompute/FacetNorm.md",
        Root / "Exhibits/LightCompute/RunFacetNorm.sh",
        Root / "Exhibits/LightCompute/FacetNorm/CMakeLists.txt",
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
sys.exit(0 if Quality and Correctness else 1)
