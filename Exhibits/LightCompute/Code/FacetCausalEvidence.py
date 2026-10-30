# 🧩 Verify historical replay bytes,attribute zero-energy occlusion and independently strike new causal triangles.
from pathlib import Path
import csv
import gzip
import hashlib
import io
import lzma
import math
import re
import numpy as np
from PIL import Image, ImageDraw, ImageFont

Root = Path(__file__).resolve().parents[3]
Results = Root / "Exhibits/LightCompute/Results"
Images = Root / "Exhibits/LightCompute/Images"
Prefix = "FacetCausal"
Builds = [Root / f"_ClaudeScratch/build/facet-causal-{Mode}" for Mode in ("release", "sanitized")]
ManifestChecks = 0
for Old in (
    "FacetAreaTransport",
    "FacetAreaOcclusion",
    "FacetAreaOcclusionGpu",
    "FacetPacket",
    "FacetMixed",
    "FacetNorm",
):
    for Line in (Results / f"{Old}-evidence-hashes.txt").read_text().splitlines():
        Digest, Name = Line.split("  ", 1)
        assert hashlib.sha256((Root / Name).read_bytes()).hexdigest() == Digest, Name
        ManifestChecks += 1
assert "acceptance=0" in (Results / "FacetAreaTransport-parity.txt").read_text()
assert "promotion=FAIL" in (Results / "FacetNorm-parity.txt").read_text()
Semantic = []
for Mode in ("release", "sanitized"):
    Text = (Results / f"{Prefix}-{Mode}.txt").read_text()
    assert "100% tests passed, 0 tests failed out of 9" in Text
    for Unit, Count in (("PROPOSAL", 193), ("PORT", 14831), ("SCORE", 1060), ("TRANSPORT", 1058)):
        assert f"FACET_AREA_{Unit}_UNIT checks={Count} pass=1" in Text
    for Row in (
        "ANALYTIC_OCCLUSION_UNIT checks=132 paths=12 pass=1",
        "FACET_PACKET_UNIT checks=31 allocation_calls=0 evaluations=6144 pass=1",
        "FACET_MIXED_UNIT checks=2796 pass=1",
        "FACET_NORM_UNIT checks=193 pass=1",
        "FACET_CAUSAL_UNIT checks=8 pass=1",
    ):
        assert Row in Text
    assert not re.search(r"ERROR: AddressSanitizer|runtime error:", Text)
    Rows = re.findall(r"^(?:FRESH_NATIVE_|FACET_CAUSAL_(?:UNIT|MUTATION|TOTAL)).*$", Text, re.M)
    assert len(Rows) == 42
    assert (
        "FRESH_NATIVE_TOTAL primary=73728 candidates=491520 paths=393216 visibility=786432 double_primary_ties=0 pass=1"
        in Rows
    )
    assert Rows[-1] == "FACET_CAUSAL_TOTAL mutations=36 fault_rejections=36 pass=1"
    Semantic.append(Rows)
    for Digest, Name in re.findall(r"^([a-f0-9]{64})  (.+)$", Text, re.M):
        assert hashlib.sha256((Root / Name).read_bytes()).hexdigest() == Digest, Name
assert Semantic[0] == Semantic[1]
Methods = ("RIS uniform", "RIS area", "raw4 uniform", "raw4 area", "raw8 uniform", "raw8 area")
Operations = ("Connection", "Suffix")
Font = ImageFont.load_default(size=13)
Small = ImageFont.load_default(size=12)
Canvas = Image.new("RGB", (1140, 1060), "#121923")
Pen = ImageDraw.Draw(Canvas)
Pen.text(
    (14, 10), "Fresh original deformed renders /exact historical replay /exposure1024,no gamma", font=Font, fill="white"
)
for Method, Label in enumerate(Methods):
    Pen.text((110 + Method * 170, 38), Label, font=Small, fill="white")
ReplayTable = [["Fixture", "Method", "Conn changed packets", "Suffix changed packets", "Historical max RGB delta"]]
Changed = np.zeros((3, 6, 2), dtype=np.int64)
for Fixture in range(3):
    Name = f"NativeAreaTransport2{Fixture}.f32"
    Raw = (Builds[0] / "captures" / Name).read_bytes()
    assert Raw == (Builds[1] / "captures" / Name).read_bytes()
    Historical = lzma.decompress((Results / f"FacetAreaTransport-2{Fixture}.f32.xz").read_bytes())
    assert Raw == Historical  # Stronger than the frozen three-slab RGB check:all41 historical slabs are identical.
    Values = np.frombuffer(Raw, dtype="<f4").reshape(41, 24576, 4)
    Captured = Values[[0, 39, 40]].copy()
    assert np.isfinite(Captured).all() and (Captured >= 0).all()
    (Results / f"{Prefix}-baseline-{Fixture}.f32.gz").write_bytes(gzip.compress(Captured.tobytes(), mtime=0))
    for Method in range(6):
        Span = slice(Method * 4096, (Method + 1) * 4096)
        Correct = Captured[0, Span, :3]
        for Operation in range(2):
            Changed[Fixture, Method, Operation] = np.count_nonzero(
                np.any(np.abs(Captured[Operation + 1, Span, :3] - Correct) > 1e-12, axis=1)
            )
        ReplayTable.append([str(Fixture), Methods[Method], *map(str, Changed[Fixture, Method]), "0"])
        for Slab in range(3):
            Rgb = Captured[Slab, Span, :3].astype(np.float64).reshape(4, 1024, 3).mean(axis=0)
            Encoded = np.rint(np.clip(Rgb * 1024, 0, 1) * 255).astype(np.uint8).reshape(32, 32, 3)
            Tile = Image.fromarray(Encoded)
            Name = Images / f"{Prefix}-{Fixture}-{Method}-{Slab}.png"
            Tile.save(Name)
            assert Image.open(Name).tobytes() == Encoded.tobytes()
            if Slab == 0:
                Canvas.paste(
                    Tile.resize((128, 128), Image.Resampling.NEAREST), (110 + Method * 170, 65 + Fixture * 160)
                )
        Pen.text((12, 110 + Fixture * 160), f"Fixture{Fixture}", font=Font, fill="white")
assert np.array_equal(Changed.sum(axis=1), ((0, 0), (0, 7247), (4158, 3841)))
AttributionBytes = (Builds[0] / "captures/FacetCausalAttribution.csv").read_bytes()
assert AttributionBytes == (Builds[1] / "captures/FacetCausalAttribution.csv").read_bytes()
(Results / f"{Prefix}-attribution.csv").write_bytes(AttributionBytes)
Attribution = list(csv.DictReader(io.StringIO(AttributionBytes.decode())))
assert len(Attribution) == 36
Totals = np.zeros((3, 2, 8), dtype=np.uint64)
for Row in Attribution:
    Fixture, Method, Operation = [int(Row[Key]) for Key in ("Fixture", "Method", "Operation")]
    Counts = np.array([int(Row[Key]) for Key in list(Row)[3:]], dtype=np.uint64)
    assert Counts[0] == Counts[1:4].sum() and Counts[1] == Counts[4:].sum()
    Totals[Fixture, Operation] += Counts
AttributionTable = [["Fixture", "Operation", "Blocked paths", "Zero proxy", "Other blocked", "Restorable paths"]]
ProjectionTable = [["Fixture", "Operation", "Zero proxy", "Neither zero", "Source only", "Receiver only", "Both zero"]]
for Fixture in range(3):
    for Operation in range(2):
        Counts = Totals[Fixture, Operation]
        AttributionTable.append([str(Fixture), Operations[Operation], *map(str, Counts[:4])])
        ProjectionTable.append([str(Fixture), Operations[Operation], str(Counts[1]), *map(str, Counts[4:])])
        if Changed[Fixture, :, Operation].sum() == 0:
            assert Counts[0] == Counts[1] and Counts[2] == 0 and Counts[3] == 0


def StrikeTriangle(Vertices, Origin, Direction, Maximum):
    First, Second = Vertices[1] - Vertices[0], Vertices[2] - Vertices[0]
    Cross = np.cross(Direction, Second)
    Determinant = np.dot(First, Cross)
    if abs(Determinant) < 1e-12:
        return False
    Delta = Origin - Vertices[0]
    U = np.dot(Delta, Cross) / Determinant
    Other = np.cross(Delta, First)
    V = np.dot(Direction, Other) / Determinant
    Travel = np.dot(Second, Other) / Determinant
    return bool(U >= 0 and V >= 0 and U + V <= 1 and 0.00001 < Travel < Maximum)


MutationBytes = (Builds[0] / "captures/FacetCausalMutations.f32").read_bytes()
assert MutationBytes == (Builds[1] / "captures/FacetCausalMutations.f32").read_bytes()
assert len(MutationBytes) == 36 * 18 * 16
(Results / f"{Prefix}-mutations.f32.gz").write_bytes(gzip.compress(MutationBytes, mtime=0))
Mutations = np.frombuffer(MutationBytes, dtype="<f4").reshape(36, 18, 4)
assert np.isfinite(Mutations).all()
MutationTable = [
    ["Fixture", "Method", "Blocked operation", "Packet/lane", "Radius", "Original max RGB", "Restored delta"]
]
Pen.text(
    (14, 562),
    "Real added-triangle tests /weighted selected-path RGB,NOT full mutated-scene images /exposure4096",
    font=Font,
    fill="white",
)
Labels = ("Connection:original", "Blocked", "Omit connection", "Suffix:original", "Blocked", "Omit suffix")
for Column, Label in enumerate(Labels):
    Pen.text((110 + 170 * Column, 595), Label, font=Small, fill="white")
AllContributions = Image.new("RGB", (1140, 1280), "#121923")
AllPen = ImageDraw.Draw(AllContributions)
AllPen.text(
    (10, 10),
    "Actual weighted selected-path RGB /all36 real added-triangle tests /exposure4096",
    font=Font,
    fill="white",
)
for Column, Label in enumerate(Labels):
    AllPen.text((110 + 170 * Column, 35), Label, font=Small, fill="white")
for Row in Mutations:
    Fixture, Method, Operation, Packet = Row[0].astype(int)
    Lane = int(Row[1, 0])
    assert 0 <= Fixture < 3 and 0 <= Method < 6 and Operation in (0, 1) and 0 <= Packet < 4096 and 0 <= Lane < 4
    assert Row[2, 1] > 0 and Row[2, 2] > 1e-6
    Vertices = Row[7:10, :3].astype(np.float64)
    Origin, Direction = Row[3, :3].astype(np.float64), Row[4, :3].astype(np.float64)
    Light = np.array((0.8, 0, 0.6), dtype=np.float32).astype(np.float64)
    Connection = StrikeTriangle(Vertices, Origin, Direction, float(Row[3, 3]))
    Suffix = StrikeTriangle(Vertices, Origin, Light, 100.0)
    Camera = StrikeTriangle(Vertices, Row[5, :3].astype(np.float64), Row[6, :3].astype(np.float64), 100.0)
    assert Connection == (Operation == 0) and Suffix == (Operation == 1) and not Camera
    assert np.array_equal(Row[14], (Operation == 1, Operation == 0, Operation == 1, Operation == 0))
    Original, Blocked, Restored, WrongOmission = Row[10:14, :3]
    assert (
        Original.max() > 0
        and (Blocked == 0).all()
        and np.array_equal(Original, Restored)
        and (WrongOmission == 0).all()
    )
    MutationTable.append(
        [
            str(Fixture),
            Methods[Method],
            Operations[Operation],
            f"{Packet}/{Lane}",
            f"{Row[2,2]:.7g}",
            f"{Original.max():.9g}",
            "0",
        ]
    )
    for Column, Rgb in enumerate((Original, Blocked, Restored)):
        Colour = tuple(np.rint(np.clip(Rgb * 4096, 0, 1) * 255).astype(np.uint8).tolist())
        Patch = Image.new("RGB", (128, 45), Colour)
        AllContributions.paste(Patch, (110 + 170 * (3 * Operation + Column), 60 + (6 * Fixture + Method) * 65))
        if Method == 0:
            Canvas.paste(Patch.resize((128, 75)), (110 + 170 * (3 * Operation + Column), 625 + Fixture * 125))
    AllPen.text((8, 75 + (6 * Fixture + Method) * 65), f"F{Fixture} M{Method}", font=Small, fill="white")
    if Method == 0:
        Pen.text((10, 647 + Fixture * 125), f"Fixture{Fixture}\nRIS uniform", font=Small, fill="white")
Pen.text(
    (14, 1020),
    "Historical gate stays HELD. Geometry mutations are targeted causal regression,not quality or production acceptance.",
    font=Small,
    fill="white",
)
Canvas.save(Images / f"{Prefix}Comparison.png")
AllContributions.save(Images / f"{Prefix}Contributions.png")


def EncodeTable(Rows):
    Widths = [max(len(Row[Column]) for Row in Rows) for Column in range(len(Rows[0]))]
    Lines = [
        "| " + " | ".join(Cell.ljust(Width) for Cell, Width in zip(Row, Widths)) + " |"
        for Row in [Rows[0], ["-" * Width for Width in Widths], *Rows[1:]]
    ]
    assert max(map(len, Lines)) <= 120
    return "\n".join(Lines) + "\n"


for Name, Rows in (
    ("replay", ReplayTable),
    ("attribution", AttributionTable),
    ("projection", ProjectionTable),
    ("mutations", MutationTable),
):
    (Results / f"{Prefix}-{Name}.md").write_text(EncodeTable(Rows))
Summary = (
    "PASS deformed path-energy attribution and targeted causal visibility regression;historical acceptance remains HELD.\n"
    "9 CTests/build,20302 focused checks/build;Release and host ASan+UBSan with leaks enabled.\n"
    "73728 original deformed packets,491520 candidates,393216 paths and786432 independent visibility decisions/build.\n"
    "All41 historical slabs of each of3 fresh native captures exactly match retained historical buffers.\n"
    "Original omission sensitivity conn/suffix packets:fixture0=0/0,fixture1=0/7247,fixture2=4158/3841 unchanged.\n"
    "Every blocked path in the3 insensitive operation/fixture pairs has exactly zero unoccluded proxy RGB.\n"
    "36 real added-triangle mutations independently strike only the intended ray,not the opposite or primary ray.\n"
    "36/36 mutations preserve primary/source/rays,block RGB,and restore it exactly only by omitting the targeted gate.\n"
    "36/36 test-only visibility-multiplication bypass faults rejected by the same causal predicate.\n"
    "3 historical buffers,attribution CSV,mutation buffer and42 semantic rows identical across builds.\n"
    f"All{ManifestChecks} prior manifest entries unchanged;original acceptance=0 and RGB-norm promotion=FAIL retained.\n"
    "54 actual original-scene RGB image arrays retained,plus weighted-path RGB comparisons,not invented scene renders.\n"
    "No engine/API/shader/default change,no new GPU/history/physical-time/Windows/production acceptance.\n"
    "Production348.046875MiB output/defaults/32B descriptors unchanged;no PR/Actions.\n"
)
(Results / f"{Prefix}-parity.txt").write_text(Summary)
print(Summary)
Files = sorted(
    [
        *Results.glob(f"{Prefix}*"),
        *Images.glob(f"{Prefix}*"),
        *Root.glob("Exhibits/LightCompute/Code/FacetCausal*"),
        Root / "Exhibits/LightCompute/FacetCausal.md",
        Root / "Exhibits/LightCompute/RunFacetCausal.sh",
        Root / "Exhibits/LightCompute/FacetCausal/CMakeLists.txt",
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
