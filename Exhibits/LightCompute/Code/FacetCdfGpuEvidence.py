# 🧩 Audit GPU-prepared cache reuse and retain lossless diagnostic deltas,measured work and actual images.
from pathlib import Path
import gzip
import hashlib
import re
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont

Root = Path(__file__).resolve().parents[3]
Exhibit = Root / "Exhibits/LightCompute"
Results, Images = Exhibit / "Results", Exhibit / "Images"
Prefix = "FacetCdfGpu"
Sizes = (1, 3, 7, 16, 31, 65)
Builds = [Root / f"_ClaudeScratch/build/facet-cdf-gpu-{Mode}" for Mode in ("release", "sanitized")]
# 📝 Archived captures can be reconstructed without a GPU or the original scratch readbacks.
if len(sys.argv) > 1 and sys.argv[1] == "--reconstruct":
    Destination = Path(sys.argv[2]) if len(sys.argv) > 2 else Root / "_ClaudeScratch/cdf-reconstructed"
    Destination.mkdir(parents=True, exist_ok=True)
    ArchivedWork = np.frombuffer(
        gzip.decompress((Results / f"{Prefix}-Work.u32.gz").read_bytes()), dtype="<u4"
    ).reshape(122, 12)
    PositiveRows = ArchivedWork[ArchivedWork[:, 3] != 1]
    for Category, Filename in (("Gpu", "FacetCdfGpu.f32"), ("Native", "FacetCdfNative.f32")):
        Restored = (
            np.frombuffer(gzip.decompress((Results / f"{Prefix}-{Category}.xor.gz").read_bytes()), dtype="<u4")
            .reshape(116, 1024, 80, 4)
            .copy()
        )
        Reference = np.frombuffer(
            gzip.decompress((Results / f"FacetProfileGpu-{Category}.f32.gz").read_bytes()), dtype="<u4"
        ).reshape(3, 6, 6, 1024, 80, 4)
        for Index, Row in enumerate(PositiveRows):
            Fixture, Support, Method, Classification = map(int, Row[:4])
            if Classification == 0:
                Restored[Index] ^= Reference[Fixture, Sizes.index(Support), Method]
        (Destination / Filename).write_bytes(Restored.tobytes())
    RejectReference = np.frombuffer(
        gzip.decompress((Results / "FacetProfileGpu-Rejections.f32.gz").read_bytes()), dtype="<u4"
    )
    RejectDelta = np.frombuffer(gzip.decompress((Results / f"{Prefix}-Rejections.xor.gz").read_bytes()), dtype="<u4")
    (Destination / "FacetCdfRejections.f32").write_bytes((RejectReference ^ RejectDelta).tobytes())
    for Category, Filename in (
        ("Cache.u32", "FacetCdfCache.u32"),
        ("Work.u32", "FacetCdfWork.u32"),
        ("Valid.u8", "FacetCdfValid.u8"),
    ):
        (Destination / Filename).write_bytes(gzip.decompress((Results / f"{Prefix}-{Category}.gz").read_bytes()))
    (Destination / "FacetCdfFirstFailure.txt").write_bytes((Results / f"{Prefix}-first-failure.txt").read_bytes())
    Matches = 0
    for Digest, Filename in re.findall(
        r"^([a-f0-9]{64})  (.+)$", (Results / f"{Prefix}-release.txt").read_text(), re.M
    ):
        if "/captures/" in Filename:
            Actual = Destination / Path(Filename).name
            assert hashlib.sha256(Actual.read_bytes()).hexdigest() == Digest, Actual
            Matches += 1
    assert Matches == 7
    print(f"CDF_GPU_RECONSTRUCT captures=7 hashes={Matches} pass=1")
    sys.exit(0)

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
    "FacetProfileGpu",
):
    for Line in (Results / f"{Old}-evidence-hashes.txt").read_text().splitlines():
        Digest, Name = Line.split("  ", 1)
        assert hashlib.sha256((Root / Name).read_bytes()).hexdigest() == Digest, Name
        Prior += 1
assert Prior == 1761
assert "acceptance=0" in (Results / "FacetAreaTransport-parity.txt").read_text()
assert "promotion=FAIL" in (Results / "FacetNorm-parity.txt").read_text()
Semantic = []
for Mode in ("release", "sanitized"):
    Text = (Results / f"{Prefix}-{Mode}.txt").read_text()
    assert "100% tests passed, 0 tests failed out of 14" in Text
    assert "CDF_GPU_UNIT checks=8 pass=1" in Text
    assert "SwiftShader Device (Subzero) type=4 driver_sanitized=0 leaks=0" in Text
    assert not re.search(r"ERROR: AddressSanitizer|runtime error:", Text)
    Rows = re.findall(r"^CDF_GPU_(?:BATCH|TOTAL|EXIT).*$", Text, re.M)
    assert len(Rows) == 124
    Semantic.append(Rows)
    for Digest, Name in re.findall(r"^([a-f0-9]{64})  (.+)$", Text, re.M):
        assert hashlib.sha256((Root / Name).read_bytes()).hexdigest() == Digest, Name
assert Semantic[0] == Semantic[1]


def Capture(Name, Type, Shape):
    Raw = (Builds[0] / "captures" / Name).read_bytes()
    assert Raw == (Builds[1] / "captures" / Name).read_bytes(), Name
    return np.frombuffer(Raw, dtype=Type).reshape(Shape)


Cache = Capture("FacetCdfCache.u32", "<u4", (122, 72, 4))
Work = Capture("FacetCdfWork.u32", "<u4", (122, 12))
Valid = Capture("FacetCdfValid.u8", "u1", (116, 1024))
assert np.array_equal(Cache[:, 0], Work[:, :4])
assert np.array_equal(Cache[:, 1, 0], Work[:, 4])
for Name, Array in (("Cache.u32", Cache), ("Work.u32", Work), ("Valid.u8", Valid)):
    (Results / f"{Prefix}-{Name}.gz").write_bytes(gzip.compress(Array.tobytes(), mtime=0))
Positive = np.flatnonzero(Work[:, 3] != 1)
Extra = np.flatnonzero(Work[:, 3] == 2)
assert len(Positive) == 116 and len(Extra) == 8
Gpu = Capture("FacetCdfGpu.f32", "<f4", (116, 1024, 80, 4))
Native = Capture("FacetCdfNative.f32", "<f4", (116, 1024, 80, 4))
Rejections = Capture("FacetCdfRejections.f32", "<f4", (6, 1024, 80, 4))
assert np.isfinite(Gpu).all() and np.isfinite(Native).all()
PriorExact = []
for Name, Actual in (("Gpu", Gpu), ("Native", Native)):
    Reference = np.frombuffer(
        gzip.decompress((Results / f"FacetProfileGpu-{Name}.f32.gz").read_bytes()), dtype="<u4"
    ).reshape(3, 6, 6, 1024, 80, 4)
    Delta = Actual.view("<u4").copy()
    BaseExact = True
    for Index, Batch in enumerate(Positive):
        Fixture, Support, Method, Category = Work[Batch, :4].astype(int)
        if Category == 0:
            Ref = Reference[Fixture, Sizes.index(Support), Method]
            BaseExact &= np.array_equal(Delta[Index], Ref)
            Delta[Index] ^= Ref
    Archive = Results / f"{Prefix}-{Name}.xor.gz"
    Archive.write_bytes(gzip.compress(Delta.tobytes(), mtime=0))
    Restored = np.frombuffer(gzip.decompress(Archive.read_bytes()), dtype="<u4").reshape(Delta.shape).copy()
    for Index, Batch in enumerate(Positive):
        Fixture, Support, Method, Category = Work[Batch, :4].astype(int)
        if Category == 0:
            Restored[Index] ^= Reference[Fixture, Sizes.index(Support), Method]
    assert Restored.tobytes() == Actual.tobytes()
    PriorExact.append(bool(BaseExact))
OldReject = np.frombuffer(
    gzip.decompress((Results / "FacetProfileGpu-Rejections.f32.gz").read_bytes()), dtype="<u4"
).reshape(Rejections.shape)
Delta = Rejections.view("<u4") ^ OldReject
RejectArchive = Results / f"{Prefix}-Rejections.xor.gz"
RejectArchive.write_bytes(gzip.compress(Delta.tobytes(), mtime=0))
assert (
    np.frombuffer(gzip.decompress(RejectArchive.read_bytes()), dtype="<u4").reshape(Delta.shape) ^ OldReject
).tobytes() == Rejections.tobytes()
Expected = np.zeros_like(Rejections)
Expected[:, :, 79, 0] = np.array([1, 1, 1, 2, 2, 3])[:, None]
RejectFailures = int(np.any(Rejections != Expected, axis=(-1, -2)).sum())
Previous = (Exhibit / "Code/FacetImageGpuEvidence.py").read_text()
Begin, End = Previous.index("A, B = Gpu.reshape"), Previous.index("First =")
Scope = {"Gpu": Gpu, "Native": Native, "Valid": Valid, "np": np}
exec(compile(Previous[Begin:End], "unchanged diagnostic predicates", "exec"), Scope)
DiagnosticFailures = Scope["DiagnosticFailures"]
First = (Builds[0] / "captures/FacetCdfFirstFailure.txt").read_bytes()
assert First == (Builds[1] / "captures/FacetCdfFirstFailure.txt").read_bytes()
(Results / f"{Prefix}-first-failure.txt").write_bytes(First)
CachedAreas, BaselineAreas, Preparations = [int(Work[:, Column].astype(np.uint64).sum()) for Column in (6, 7, 8)]
Reduced = 100 * (1 - CachedAreas / BaselineAreas)
assert CachedAreas == 466 and Preparations == 30
Lifetime = {}
for Batch, Row in enumerate(Work):
    Fixture, Support, Method, Category, Revision, Runs, Areas, OtherAreas, NewRuns, Samples, TotalAreas, Rejection = (
        map(int, Row)
    )
    Context = (Fixture, 16 if Category else Support)
    PreviousRuns, PreviousSamples, PreviousAreas = Lifetime.get(Context, (0, 0, 0))
    assert Runs == PreviousRuns + NewRuns and Samples == PreviousSamples + 1 and TotalAreas == PreviousAreas + Areas
    assert NewRuns in (0, 1)
    Header = Cache[Batch, 2:6].reshape(-1)
    assert tuple(map(int, Header[[0, 1, 2, 6, 8, 9, 10]])) == (
        Rejection,
        Support if Category != 1 else (0, 17, 16, 16, 16, 16)[Revision],
        Runs,
        int(Cache[Batch, 1, 1]),
        1,
        Samples,
        TotalAreas,
    )
    Geometry = int(Cache[Batch, 1, 1])
    assert np.array_equal(Cache[Batch, 2 + Geometry + 4], np.full(4, 0x5A5A5A5A, dtype=np.uint32))
    if not Rejection:
        Entries = Cache[Batch, 6 : 6 + Support]
        AreasFloat = Entries[:, 0].view("<f4")
        Cdf = Entries[:, 1].view("<f4")
        Density = Entries[:, 2].view("<f4")
        Total = Header[4:5].view("<f4")[0]
        assert (AreasFloat > 0).all() and (Density > 0).all() and (np.diff(Cdf) > 0).all()
        assert np.array_equal(np.cumsum(AreasFloat / Total, dtype=np.float32), Cdf)
        assert np.array_equal((np.float32(1) / np.float32(Support)) / AreasFloat, Density)
        assert len(set(Entries[:, 3].tolist())) == Support
    Lifetime[Context] = (Runs, Samples, TotalAreas)
ExtraRuns = Work[Extra, 5].tolist()
assert ExtraRuns == [8, 8, 9, 10, 11, 12, 12, 13]
Difference = np.abs(Gpu[:, :, 0, :3].astype(np.float64) - Native[:, :, 0, :3])
Maximum, Rmse = float(Difference.max()), float(np.sqrt(np.mean(Difference**2)))
Table = [["Fixture", "Support", "Method", "Category", "Revision", "Prepares", "GPU area calls", "Uncached calls"]]
for Row in Work:
    Table.append(
        [int(Row[0]), int(Row[1]), int(Row[2]), int(Row[3]), int(Row[4]), int(Row[5]), int(Row[6]), int(Row[7])]
    )
Widths = [max(len(str(Row[Column])) for Row in Table) for Column in range(len(Table[0]))]


def RenderComparisonRow(Row):
    return "| " + " | ".join(str(Cell).ljust(Width) for Cell, Width in zip(Row, Widths)) + " |"


(Results / f"{Prefix}-comparison.md").write_text(
    "\n".join(
        [RenderComparisonRow(Table[0]), RenderComparisonRow(["-" * Width for Width in Widths])]
        + [RenderComparisonRow(Row) for Row in Table[1:]]
    )
    + "\n"
)
Canvas = Image.new("RGB", (1440, 1050), "#121923")
Pen = ImageDraw.Draw(Canvas)
Font = ImageFont.load_default(size=14)
Small = ImageFont.load_default(size=12)
Pen.text(
    (16, 12),
    "Measured GPU facet-area evaluations /paired identical diagnostics /SwiftShader CPU Vulkan /not a timing benchmark",
    font=Font,
    fill="white",
)
Pen.text(
    (16, 38),
    f"All122 batches: uncached {BaselineAreas:,} calls -> GPU-resident {CachedAreas:,} calls; {Reduced:.6f}% fewer area evaluations",
    font=Font,
    fill="white",
)
Totals = []
for Support in Sizes:
    Rows = Work[(Work[:, 1] == Support) & (Work[:, 3] == 0)]
    Totals.append((int(Rows[:, 6].sum()), int(Rows[:, 7].sum())))
Limit = np.log10(max(Other for _, Other in Totals) + 1)
for Index, (Support, (Current, Other)) in enumerate(zip(Sizes, Totals)):
    Y = 80 + Index * 49
    Pen.text((16, Y), f"N={Support}", font=Font, fill="white")
    for Offset, Count, Colour in ((0, Other, "#e39b56"), (19, Current, "#55d4a0")):
        End = 120 + int(np.log10(Count + 1) / Limit * 860)
        Pen.rectangle((120, Y + Offset, End, Y + Offset + 12), fill=Colour)
        Pen.text((End + 8, Y + Offset - 2), f"{Count:,}", font=Small, fill="white")
Pen.text(
    (16, 382),
    "Bars: log10(count+1),base108 images only. Orange=uncached;green=GPU-resident. Cold checks remain conservative.",
    font=Font,
    fill="white",
)
Labels = (
    "Restore",
    "Warm camera/seed",
    "Reorder",
    "Material",
    "Geometry",
    "Restore original",
    "Warm method",
    "Support16->15",
)
DisplayMatches = 0
for Column, Batch in enumerate(Extra):
    PositiveIndex = int(np.flatnonzero(Positive == Batch)[0])
    Pen.text((112 + Column * 164, 430), Labels[Column], font=Small, fill="white")
    Display = []
    for Row, Pixels in enumerate(
        (Native[PositiveIndex, :, 0, :3], Gpu[PositiveIndex, :, 0, :3], Difference[PositiveIndex])
    ):
        if Row < 2:
            Encoded = np.rint(np.clip(Pixels.astype(np.float64) * 1024, 0, 1) * 255).astype(np.uint8).reshape(32, 32, 3)
            Display.append(Encoded.tobytes())
        else:
            Encoded = np.zeros((1024, 3), dtype=np.uint8)
            Encoded[:, 0] = np.rint(np.clip(Pixels.max(axis=-1) * 1e8, 0, 1) * 255).astype(np.uint8)
            Encoded = Encoded.reshape(32, 32, 3)
        Tile = Image.fromarray(Encoded)
        Tile.save(Images / f"{Prefix}-{Column}-{('Native', 'Gpu', 'Error')[Row]}.png")
        Canvas.paste(Tile.resize((128, 128), Image.Resampling.NEAREST), (112 + Column * 164, 470 + Row * 155))
    DisplayMatches += Display[0] == Display[1]
for Row, Label in enumerate(("Native", "GPU-resident", "Error x1e8")):
    Pen.text((10, 470 + Row * 155), Label, font=Small, fill="white")
Pen.text(
    (16, 960),
    "Actual32x32 single-sample images /linear exposure1024,no gamma /red=max absolute RGB error x1e8",
    font=Font,
    fill="white",
)
Pen.text(
    (16, 990),
    "Operation counts are not FPS or physical-GPU speed. Production348.046875MiB/defaults/32B unchanged.",
    font=Font,
    fill="white",
)
Canvas.save(Images / f"{Prefix}Comparison.png")
Passed = "pass=1" in Semantic[0][-2] and DiagnosticFailures == 0 and RejectFailures == 0 and all(PriorExact)
Summary = (
    f"GPU-resident area/CDF parity={'PASS' if Passed else 'FAIL'};frozen gates unchanged.\n"
    "116 positive images/118784 packets plus6 malformed submissions;paired uncached GPU on every batch.\n"
    "14 CTests/20342 focused checks per build;host ASan+UBSan,driver uninstrumented,leaks0.\n"
    f"GPU area calls {BaselineAreas}->{CachedAreas};reduction={Reduced:.9f} percent,NOT measured time/FPS. Preparations={Preparations}.\n"
    f"Offline diagnostic failed packets={DiagnosticFailures};native invalid={int((Valid == 0).sum())};malformed failed pixels={RejectFailures}.\n"
    f"Maximum packet RGB difference={Maximum:.17g};RGB RMSE={Rmse:.17g}.\n"
    f"Original108 image GPU/native captures exact prior={int(PriorExact[0])}/{int(PriorExact[1])};extra preparations={ExtraRuns}.\n"
    + Semantic[0][-2]
    + "\n"
    f"All seven capture files and124 semantic rows identical across builds;displayed native/GPU image pairs exact={DisplayMatches}/8.\n"
    f"Eleven prior manifests/{Prior} entries unchanged;transport HELD,norm promotion FAIL preserved.\n"
    "Dynamic cache is private binding5 scratch,not a production allocation change. No physical-GPU/Windows/renderer/quality acceptance.\n"
)
(Results / f"{Prefix}-parity.txt").write_text(Summary)
Paths = [
    Exhibit / "Code/FacetCdfGpuSources.py",
    Exhibit / "Code/FacetCdfGpuValidation.cpp",
    Exhibit / "Code/FacetCdfGpuEvidence.py",
    Exhibit / "FacetCdfGpu/CMakeLists.txt",
    Exhibit / "RunFacetCdfGpu.sh",
    Exhibit / "FacetCdfGpu.md",
]
Paths += sorted(Images.glob(f"{Prefix}*.png"))
Paths += sorted(P for P in Results.glob(f"{Prefix}-*") if P.name != f"{Prefix}-evidence-hashes.txt")
(Results / f"{Prefix}-evidence-hashes.txt").write_text(
    "".join(f"{hashlib.sha256(P.read_bytes()).hexdigest()}  {P.relative_to(Root).as_posix()}\n" for P in Paths)
)
print(Summary)
