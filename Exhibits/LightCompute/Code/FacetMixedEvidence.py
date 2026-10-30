# 🧩 Integrate clipped analytic source domains independently of native transport and compare all mixed holdout arms.
from pathlib import Path
import gzip
import hashlib
import math
import re
import numpy as np
from PIL import Image, ImageDraw, ImageFont

Root = Path(__file__).resolve().parents[3]
Results = Root / "Exhibits/LightCompute/Results"
Images = Root / "Exhibits/LightCompute/Images"
Prefix = "FacetMixed"
Builds = [Root / f"_ClaudeScratch/build/facet-mixed-{Mode}" for Mode in ("release", "sanitized")]
ManifestChecks = 0
for Old in ("FacetAreaTransport", "FacetAreaOcclusion", "FacetAreaOcclusionGpu", "FacetPacket"):
    for Line in (Results / f"{Old}-evidence-hashes.txt").read_text().splitlines():
        Digest, Name = Line.split("  ", 1)
        assert hashlib.sha256((Root / Name).read_bytes()).hexdigest() == Digest, Name
        ManifestChecks += 1
assert "acceptance=0" in (Results / "FacetAreaTransport-parity.txt").read_text()
Semantic = []
for Mode in ("release", "sanitized"):
    Text = (Results / f"{Prefix}-{Mode}.txt").read_text()
    assert "100% tests passed, 0 tests failed out of 7" in Text
    for Unit, Count in (("PROPOSAL", 193), ("PORT", 14831), ("SCORE", 1060), ("TRANSPORT", 1058)):
        assert f"FACET_AREA_{Unit}_UNIT checks={Count} pass=1" in Text
    assert "ANALYTIC_OCCLUSION_UNIT checks=132 paths=12 pass=1" in Text
    assert "FACET_PACKET_UNIT checks=31 allocation_calls=0 evaluations=6144 pass=1" in Text
    assert "FACET_MIXED_UNIT checks=2796 pass=1" in Text
    assert not re.search(r"ERROR: AddressSanitizer|runtime error:", Text)
    Rows = re.findall(r"^FACET_MIXED_(?:UNIT|CASE|TOTAL).*$", Text, re.M)
    assert len(Rows) == 14
    assert Rows[-1] == "FACET_MIXED_TOTAL primary=294912 candidates=1966080 paths=1572864 visibility=3145728 pass=1"
    Semantic.append(Rows)
    for Digest, Name in re.findall(r"^([a-f0-9]{64})  (.+)$", Text, re.M):
        assert hashlib.sha256((Root / Name).read_bytes()).hexdigest() == Digest, Name
assert Semantic[0] == Semantic[1]

Methods = ("RIS uniform", "RIS area", "raw4 uniform", "raw4 area", "raw8 uniform", "raw8 area")
Variants = ("Clear", "Connection partial", "Suffix partial", "Both partial")
Scales = (1.0, 0.65, 1.4)
Offsets = ((0.0, 0.0, 0.0), (-1.2, 0.3, 0.2), (0.6, -0.5, -0.1))
Sources = ((-0.8, -0.4, -0.2, 0.2, (0.8, 0.1, 0.05)), (0.1, 0.7, -0.3, 0.3, (0.05, 0.3, 0.9)))
Pixels = np.arange(1024)
ReceiverX = -0.195 + 0.4 * ((Pixels % 32) + 0.5) / 32
ReceiverY = -0.2 + 0.4 * ((Pixels // 32) + 0.5) / 32
ReceiverRgb = np.where((ReceiverX > ReceiverY)[:, None], (0.6, 0.5, 0.4), (0.2, 0.7, 0.3))
LightRgb = np.array((1.0, 0.7, 0.4))


def IntegrateClippedSources(Scale, Steps):
    # Discontinuities are split analytically;each midpoint rectangle has constant visibility/material.
    Reference = np.zeros((4, 4, 1024, 3), dtype=np.float64)
    Samples = (np.arange(Steps, dtype=np.float64) + 0.5) / Steps
    SuffixLow = 1.5 - (1 - 0.002 / Scale) * (0.8 / 0.6)
    SuffixHigh = 2.2 - (1 - 0.002 / Scale) * (0.8 / 0.6)
    for Pixel, (ReceiverFirst, ReceiverSecond) in enumerate(zip(ReceiverX, ReceiverY)):
        ConnectionLow, ConnectionHigh = -0.44 - ReceiverFirst, 0.14 - ReceiverFirst
        VerticalLow, VerticalHigh = -0.2 - ReceiverSecond, 0.3 - ReceiverSecond
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
            Y = sorted(set((Bottom, Top, *[Cut for Cut in (VerticalLow, VerticalHigh) if Bottom < Cut < Top])))
            Factor = np.array(Colour) * ReceiverRgb[Pixel] * LightRgb * 0.6 / math.pi**2
            for First, Last in zip(X, X[1:]):
                SourceX = First + (Last - First) * Samples
                for Low, High in zip(Y, Y[1:]):
                    SourceY = Low + (High - Low) * Samples
                    Squared = (SourceX[:, None] - ReceiverFirst) ** 2 + (SourceY[None, :] - ReceiverSecond) ** 2 + 4
                    Integral = np.mean(4 / Squared**2) * (Last - First) * (High - Low)
                    XMid, YMid = (First + Last) / 2, (Low + High) / 2
                    ConnectionBlocked = ConnectionLow <= XMid <= ConnectionHigh and VerticalLow <= YMid <= VerticalHigh
                    SuffixBlocked = SuffixLow <= XMid <= SuffixHigh
                    for Variant in range(4):
                        SuffixVisible = not (Variant & 2 and SuffixBlocked)
                        ConnectionVisible = not (Variant & 1 and ConnectionBlocked)
                        Gates = (SuffixVisible and ConnectionVisible, SuffixVisible, ConnectionVisible, True)
                        for Slab, Visible in enumerate(Gates):
                            if Visible:
                                Reference[Variant, Slab, Pixel] += Factor * Integral
    return Reference


Means = [["Layout", "Variant", "Method", "Slab", "Channel", "Residual", "SE", "Gate"]]
Metrics = [["Layout", "Variant", "Method", "Correct RMSE", "Omit conn RMSE", "Omit suffix RMSE", "Omit both RMSE"]]
Controls = [
    ["Layout", "Method", "Conn isolated changes", "Suffix isolated changes", "Both conn changes", "Both suffix changes"]
]
Font = ImageFont.load_default(size=13)
Small = ImageFont.load_default(size=12)
Canvas = Image.new("RGB", (1230, 1040), "#121923")
Ablations = Image.new("RGB", (1040, 1040), "#121923")
Pen, AblationPen = ImageDraw.Draw(Canvas), ImageDraw.Draw(Ablations)
Pen.text(
    (16, 14),
    "Fresh mixed-source API holdout /unequal areas and RGB,receiver materials,partial visibility /exposure256",
    font=Font,
    fill="white",
)
AblationPen.text(
    (16, 14), "Actual mixed-source API ablations /layout0,area RIS /exposure256,no gamma", font=Font, fill="white"
)
MeanChecks = 0
Convergence = 0.0
AllRmse = np.zeros((3, 4, 6, 4))
AllEstimates = []


def EncodeImage(Colour, Name):
    assert np.isfinite(Colour).all() and (Colour >= 0).all()
    Encoded = np.rint(np.clip(Colour * 256, 0, 1) * 255).astype(np.uint8).reshape(32, 32, 3)
    Tile = Image.fromarray(Encoded)
    Tile.save(Images / Name)
    assert Image.open(Images / Name).tobytes() == Encoded.tobytes()
    return Tile


for Layout, Scale in enumerate(Scales):
    Coarse = IntegrateClippedSources(Scale, 32)
    Fine = IntegrateClippedSources(Scale, 64)
    Difference = float(np.max(np.abs(Coarse - Fine)))
    Convergence = max(Convergence, Difference)
    assert Difference <= 2e-6, (Layout, Difference)
    References = np.stack((Coarse, Fine)).astype("<f8")
    (Results / f"{Prefix}-reference-{Layout}.f64.gz").write_bytes(gzip.compress(References.tobytes(), mtime=0))
    LayoutEstimates = []
    for Variant in range(4):
        Name = f"FacetMixed{Layout}{Variant}.f32"
        Raw = (Builds[0] / "captures" / Name).read_bytes()
        assert len(Raw) == 6 * 24576 * 16 and Raw == (Builds[1] / "captures" / Name).read_bytes()
        Archive = Results / f"{Prefix}-{Layout}{Variant}.f32.gz"
        Archive.write_bytes(gzip.compress(Raw, mtime=0))
        assert gzip.decompress(Archive.read_bytes()) == Raw
        Values = np.frombuffer(Raw, dtype="<f4").reshape(6, 6, 4, 1024, 4)
        assert np.isfinite(Values).all() and (Values[:4] >= 0).all() and (Values[:5, :, :, :, 3] == 0).all()
        Primary = Values[4, :, :, :, :3]
        Declared = np.stack(
            (
                ReceiverX * Scale + Offsets[Layout][0],
                ReceiverY * Scale + Offsets[Layout][1],
                np.full(1024, 2 * Scale + Offsets[Layout][2]),
            ),
            axis=1,
        )
        assert np.max(np.abs(Primary - Declared)) <= 2e-5
        for Method in range(6):
            Work = (8, 4, 8, 0) if Method < 2 else (4, 4, 8, 0) if Method < 4 else (8, 8, 16, 0)
            assert (Values[5, Method] == Work).all()
        PacketRgb = Values[:4, :, :, :, :3].astype(np.float64)
        Estimates = PacketRgb.mean(axis=2)
        LayoutEstimates.append(PacketRgb)
        for Method in range(6):
            for Slab in range(4):
                Residuals = Estimates[Slab, Method] - Fine[Variant, Slab]
                Rmse = float(np.sqrt(np.mean(Residuals**2)))
                AllRmse[Layout, Variant, Method, Slab] = Rmse
                for Axis in range(3):
                    Residual = Residuals[:, Axis]
                    Mean = float(Residual.mean())
                    SE = float(Residual.std(ddof=1) / math.sqrt(1024))
                    Accepted = abs(Mean) <= 6 * SE + 2e-5
                    Means.append(
                        [
                            str(Layout),
                            str(Variant),
                            Methods[Method],
                            str(Slab),
                            str(Axis),
                            f"{Mean:.8g}",
                            f"{SE:.8g}",
                            str(Accepted),
                        ]
                    )
                    assert Accepted, (Layout, Variant, Method, Slab, Axis, Mean, SE)
                    MeanChecks += 1
                Tile = EncodeImage(Estimates[Slab, Method], f"{Prefix}-{Layout}-{Variant}-{Method}-{Slab}.png")
                if Layout == 0 and Method == 1:
                    X, Y = 175 + Slab * 210, 95 + Variant * 230
                    Ablations.paste(Tile.resize((160, 160), Image.Resampling.NEAREST), (X, Y))
                    if Variant == 0:
                        AblationPen.text(
                            (X, Y - 27),
                            ("Correct", "Omit connection", "Omit suffix", "Omit both")[Slab],
                            font=Small,
                            fill="white",
                        )
                    AblationPen.text((12, Y + 65), Variants[Variant], font=Font, fill="white")
            Metrics.append(
                [
                    str(Layout),
                    str(Variant),
                    Methods[Method],
                    *[f"{Number:.9g}" for Number in AllRmse[Layout, Variant, Method]],
                ]
            )
            assert (Estimates[0, Method].mean(axis=0) > 0).all()
        for Slab in range(4):
            ReferenceTile = EncodeImage(Fine[Variant, Slab], f"{Prefix}-reference-{Layout}-{Variant}-{Slab}.png")
            if Layout == 0 and Slab == 0:
                for Column, (Method, Label) in enumerate(
                    (
                        (0, "RIS uniform"),
                        (1, "RIS area"),
                        (2, "raw4 uniform"),
                        (4, "raw8 uniform"),
                        (-1, "Analytic reference"),
                    )
                ):
                    Tile = (
                        ReferenceTile
                        if Method == -1
                        else Image.open(Images / f"{Prefix}-{Layout}-{Variant}-{Method}-0.png")
                    )
                    X, Y = 175 + Column * 210, 95 + Variant * 230
                    Canvas.paste(Tile.resize((160, 160), Image.Resampling.NEAREST), (X, Y))
                    if Variant == 0:
                        Pen.text((X, Y - 27), Label, font=Small, fill="white")
                    Pen.text((12, Y + 65), Variants[Variant], font=Font, fill="white")
    for Method in range(6):
        Clear, Connection, Suffix, Both = [Estimate[:, Method].reshape(4, 4096, 3) for Estimate in LayoutEstimates]
        assert np.max(np.abs(Clear - Clear[0])) <= 2e-7
        ConnChanges = int(np.count_nonzero(np.any(Connection[1] != Connection[0], axis=1)))
        SuffixChanges = int(np.count_nonzero(np.any(Suffix[2] != Suffix[0], axis=1)))
        BothConn = int(np.count_nonzero(np.any(Both[1] != Both[0], axis=1)))
        BothSuffix = int(np.count_nonzero(np.any(Both[2] != Both[0], axis=1)))
        assert ConnChanges >= 64 and SuffixChanges >= 64 and BothConn > 0 and BothSuffix > 0
        assert np.array_equal(Connection[2], Connection[0]) and np.array_equal(Suffix[1], Suffix[0])
        Controls.append(
            [str(Layout), Methods[Method], str(ConnChanges), str(SuffixChanges), str(BothConn), str(BothSuffix)]
        )
    AllEstimates.append(LayoutEstimates)
assert MeanChecks == 864
Pen.text(
    (16, 1005),
    "Actual API output versus independent clipped quadrature. No new GPU,temporal or production acceptance.",
    font=Small,
    fill="white",
)
AblationPen.text(
    (16, 1005),
    "Only visibility operations are omitted;geometry and seeds are unchanged. Every prior frozen report remains preserved.",
    font=Small,
    fill="white",
)
Canvas.save(Images / f"{Prefix}Comparison.png")
Ablations.save(Images / f"{Prefix}Ablations.png")


def EncodeTable(Rows):
    Widths = [max(len(Row[Column]) for Row in Rows) for Column in range(len(Rows[0]))]
    Rows = [Rows[0], ["-" * Width for Width in Widths], *Rows[1:]]
    Lines = ["| " + " | ".join(Cell.ljust(Width) for Cell, Width in zip(Row, Widths)) + " |" for Row in Rows]
    assert max(map(len, Lines)) <= 120
    return "\n".join(Lines) + "\n"


Aggregate = [["Method", "Paths", "Candidate scores", "Mean correct RMSE"]]
for Method in range(6):
    Aggregate.append(
        [
            Methods[Method],
            "4" if Method < 4 else "8",
            "4" if 2 <= Method < 4 else "8",
            f"{AllRmse[:,:,Method,0].mean():.12g}",
        ]
    )
for Name, Rows in (("means", Means), ("comparison", Metrics), ("controls", Controls), ("aggregate", Aggregate)):
    (Results / f"{Prefix}-{Name}.md").write_text(EncodeTable(Rows))
Ratios = AllRmse[:, :, 0, 0] / AllRmse[:, :, 2, 0]
AreaRatios = AllRmse[:, :, 1, 0] / AllRmse[:, :, 3, 0]
UniformVsArea = AllRmse[:, :, 1, 0] / AllRmse[:, :, 0, 0]
Summary = (
    "PASS frozen mixed-source packet holdout;7 CTests/build,20101 focused checks/build.\n"
    "294912 API packets/build match the separately assembled native baseline exactly.\n"
    "1966080 candidate draws,1572864 selected paths,3145728 visibility decisions in the independent baseline/build.\n"
    f"All864 RGB mean gates pass clipped independent quadrature;max32/64 channel reference difference={Convergence:.17g}.\n"
    "Every isolated affected omission changes >=64 packets/layout/method;unaffected omission is exactly unchanged.\n"
    "Both blockers independently affect some RGB;every correct variant has positive mean.\n"
    "Twelve buffers and14 semantic rows byte-identical across Release/host ASan+UBSan;leaks enabled.\n"
    "288 actual API and48 analytic-reference image arrays verified;lossless captures/references retained.\n"
    f"Uniform RIS/raw4 correct-RMSE ratios min/max={Ratios.min():.9g}/{Ratios.max():.9g};wins={np.count_nonzero(Ratios<1)}/12.\n"
    f"Area RIS/raw4 correct-RMSE ratios min/max={AreaRatios.min():.9g}/{AreaRatios.max():.9g};wins={np.count_nonzero(AreaRatios<1)}/12.\n"
    f"Area/uniform RIS correct-RMSE ratios min/max={UniformVsArea.min():.9g}/{UniformVsArea.max():.9g};area wins={np.count_nonzero(UniformVsArea<1)}/12.\n"
    f"All{ManifestChecks} prior manifest entries unchanged;historical transport acceptance=0 retained.\n"
    "No engine/API/shader/default changes or new GPU execution;no universal quality,physical speed,history or Windows acceptance.\n"
    "Production348.046875MiB/defaults/32B descriptors unchanged;no PR/Actions.\n"
)
(Results / f"{Prefix}-parity.txt").write_text(Summary)
print(Summary)
Files = sorted(
    [
        *Results.glob(f"{Prefix}*"),
        *Images.glob(f"{Prefix}*"),
        *Root.glob("Exhibits/LightCompute/Code/FacetMixed*"),
        Root / "Exhibits/LightCompute/FacetMixed.md",
        Root / "Exhibits/LightCompute/RunFacetMixed.sh",
        Root / "Exhibits/LightCompute/FacetMixed/CMakeLists.txt",
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
