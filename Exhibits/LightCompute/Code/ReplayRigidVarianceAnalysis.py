#!/usr/bin/env python3
# 🧩 Independently summarize paired rigid-primary samples and graph red-channel reference estimates.
import csv
import hashlib
import math
import random
import re
import statistics
import sys
from pathlib import Path
from xml.sax.saxutils import escape


Modes = ("uniform_single", "uniform_spatial", "mixture_single", "mixture_spatial")
Channels = ("red", "green", "blue")


def ReadReference(Log, Label, Channel=0):
    Pattern = (rf"^TEXTURE_MOTION_REFERENCE {Label} channel={Channel} cpu=(\S+) cpu_se=(\S+) "
               rf"vulkan=(\S+) batch_se=(\S+) cpu_paths=(\d+) gpu_batches=(\d+)$")
    Match = re.search(Pattern, Log, re.MULTILINE)
    if not Match:
        raise ValueError(f"Missing red reference: {Label}")
    Cpu, CpuSe, Vulkan, VulkanSe = map(float, Match.groups()[:4])
    return Cpu, CpuSe, Vulkan, VulkanSe, int(Match[5]), int(Match[6])


def Summarize(Values):
    return statistics.mean(Values), statistics.stdev(Values) / math.sqrt(len(Values))


def BootstrapRatios(Series, Names, Repetitions=2000):
    # Paired resampling assumes the recorded batches adequately represent the distribution.
    Number = len(Series[0])
    Random = random.Random(0x68cc45f2)
    Results = [[] for _ in Names]
    for _ in range(Repetitions):
        Indices = Random.choices(range(Number), k=Number)
        Deviations = []
        for Values in Series:
            Sum = sum(Values[Index] for Index in Indices)
            Squares = sum(Values[Index] ** 2 for Index in Indices)
            Deviations.append(math.sqrt(max(0.0, Squares / Number - (Sum / Number) ** 2)))
        for Index, (Numerator, Denominator) in enumerate(Names):
            Results[Index].append(Deviations[Numerator] / Deviations[Denominator])
    Intervals = []
    for Values in Results:
        Values.sort()
        def Quantile(Fraction):
            Position = Fraction * (len(Values) - 1)
            Lower = math.floor(Position)
            return Values[Lower] + (Values[math.ceil(Position)] - Values[Lower]) * (Position - Lower)
        Intervals.append((Quantile(.025), Quantile(.975)))
    return Intervals


def AxisTicks(Minimum, Maximum):
    Raw = (Maximum - Minimum) / 7
    Magnitude = 10 ** math.floor(math.log10(Raw))
    Fraction = Raw / Magnitude
    Nice = 1 if Fraction < 1.5 else 2 if Fraction < 2.25 else 2.5 if Fraction < 3.75 else 5 if Fraction < 7.5 else 10
    Step = Nice * Magnitude
    Digits = max(0, -math.floor(math.log10(Step)))
    while not math.isclose(Step * 10 ** Digits, round(Step * 10 ** Digits), abs_tol=1e-8):
        Digits += 1
    Ticks = []
    Value = math.ceil(Minimum / Step) * Step
    while Value <= Maximum:
        Ticks.append((Value, f"{Value:.{Digits}f}"))
        Value += Step
    return Ticks


def Chart(Rows, Count, CpuPaths, SvgPath, PngPath=None):
    Width, Height = 1180, 610
    Left, Right = 355, 850
    Low = min(Mean - 2 * Se for _, Mean, Se, _ in Rows)
    High = max(Mean + 2 * Se for _, Mean, Se, _ in Rows)
    Padding = (High - Low) * .09
    Minimum, Maximum = Low - Padding, High + Padding
    X = lambda Value: Left + (Value - Minimum) / (Maximum - Minimum) * (Right - Left)
    FirstY, Step = 140, 46
    Bottom = FirstY + Step * (len(Rows) - 1) + 25
    Ref, RefSe = Rows[0][1:3]
    Svg = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{Width}" height="{Height}" viewBox="0 0 {Width} {Height}">',
        '<rect width="100%" height="100%" fill="#f6f7f9"/>',
        '<text x="35" y="43" font-family="sans-serif" font-size="24" fill="#172131">Rigid red-channel follow-up</text>',
        f'<text x="35" y="67" font-family="sans-serif" font-size="14" fill="#536174">'
        f'{Count:,} new paired seeds; {CpuPaths:,} new CPU paths</text>',
        f'<rect x="{X(Ref - 2 * RefSe):.2f}" y="110" width="{X(Ref + 2 * RefSe) - X(Ref - 2 * RefSe):.2f}" '
        f'height="{Bottom - 110}" fill="#d9e9e3"/>',
    ]
    for Tick, Label in AxisTicks(Minimum, Maximum):
        Svg.append(f'<line x1="{X(Tick):.2f}" y1="110" x2="{X(Tick):.2f}" y2="{Bottom}" stroke="#e1e5ea"/>')
        Svg.append(f'<text x="{X(Tick):.2f}" y="{Bottom + 22}" text-anchor="middle" font-family="sans-serif" '
                   f'font-size="12" fill="#536174">{Label}</text>')
    for Index, (Label, Mean, Se, Colour) in enumerate(Rows):
        Y = FirstY + Index * Step
        A, B, Centre = X(Mean - 2 * Se), X(Mean + 2 * Se), X(Mean)
        Svg.extend([
            f'<text x="{Left - 14}" y="{Y + 5}" text-anchor="end" font-family="sans-serif" '
            f'font-size="14" fill="#172131">{escape(Label)}</text>',
            f'<line x1="{A:.2f}" y1="{Y}" x2="{B:.2f}" y2="{Y}" stroke="#596777" stroke-width="2"/>',
            f'<line x1="{A:.2f}" y1="{Y - 6}" x2="{A:.2f}" y2="{Y + 6}" stroke="#596777" stroke-width="2"/>',
            f'<line x1="{B:.2f}" y1="{Y - 6}" x2="{B:.2f}" y2="{Y + 6}" stroke="#596777" stroke-width="2"/>',
            f'<circle cx="{Centre:.2f}" cy="{Y}" r="6" fill="{Colour}"/>',
            f'<text x="{Right + 20}" y="{Y + 5}" font-family="sans-serif" font-size="13" fill="#25354a">'
            f'{Mean:.7f} +/- {Se:.7f}</text>',
        ])
    Svg.extend([
        f'<text x="35" y="{Bottom + 57}" font-family="sans-serif" font-size="13" fill="#4b5969">'
        'Bars: +/- 2 nominal sample SE; numbers: mean +/- 1 SE. Green band: new CPU mean +/- 2 SE.</text>',
        f'<text x="35" y="{Bottom + 78}" font-family="sans-serif" font-size="12" fill="#697786">'
        'SwiftShader CPU Vulkan; streams and normal-interval coverage are not proven; spatial modes cost more.</text>',
        '</svg>',
    ])
    Path(SvgPath).parent.mkdir(parents=True, exist_ok=True)
    Path(SvgPath).write_text('\n'.join(Svg) + '\n', encoding='utf-8')
    if not PngPath:
        return
    from PIL import Image, ImageDraw, ImageFont
    ImageData = Image.new("RGB", (Width, Height), "#f6f7f9")
    Canvas = ImageDraw.Draw(ImageData)
    FontPath = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
    Font = lambda Size: ImageFont.truetype(FontPath, Size)
    Heading, Normal, Small, Tiny = Font(24), Font(14), Font(13), Font(12)
    Canvas.text((35, 20), "Rigid red-channel follow-up", font=Heading, fill="#172131")
    Canvas.text((35, 54), f"{Count:,} new paired seeds; {CpuPaths:,} new CPU paths", font=Normal, fill="#536174")
    Canvas.rectangle((X(Ref - 2 * RefSe), 110, X(Ref + 2 * RefSe), Bottom), fill="#d9e9e3")
    for Tick, Label in AxisTicks(Minimum, Maximum):
        Canvas.line((X(Tick), 110, X(Tick), Bottom), fill="#e1e5ea")
        Canvas.text((X(Tick), Bottom + 10), Label, font=Tiny, fill="#536174", anchor="mt")
    for Index, (Label, Mean, Se, Colour) in enumerate(Rows):
        Y = FirstY + Index * Step
        A, B, Centre = X(Mean - 2 * Se), X(Mean + 2 * Se), X(Mean)
        Canvas.text((Left - 14, Y), Label, font=Normal, fill="#172131", anchor="rm")
        Canvas.line((A, Y, B, Y), fill="#596777", width=2)
        Canvas.line((A, Y - 6, A, Y + 6), fill="#596777", width=2)
        Canvas.line((B, Y - 6, B, Y + 6), fill="#596777", width=2)
        Canvas.ellipse((Centre - 6, Y - 6, Centre + 6, Y + 6), fill=Colour)
        Canvas.text((Right + 20, Y), f"{Mean:.7f} +/- {Se:.7f}", font=Small, fill="#25354a", anchor="lm")
    Canvas.text((35, Bottom + 45), "Bars: +/- 2 nominal sample SE; numbers: mean +/- 1 SE. Green band: new CPU mean +/- 2 SE.",
                font=Small, fill="#4b5969")
    Canvas.text((35, Bottom + 67), "SwiftShader CPU Vulkan; streams and normal-interval coverage are not proven; spatial modes cost more.",
                font=Tiny, fill="#697786")
    ImageData.save(PngPath)


def Main():
    if len(sys.argv) not in (6, 7):
        raise SystemExit("Usage: ReplayRigidVarianceAnalysis.py NEW_LOG PRIOR_LOG NEW_CSV PRIOR_CSV SVG [PNG]")
    _, NewPath, PriorPath, TablePath, PriorTablePath, SvgPath, *Optional = sys.argv
    NewLog = Path(NewPath).read_text(encoding='utf-8')
    PriorLog = Path(PriorPath).read_text(encoding='utf-8')
    PriorUniform = ReadReference(PriorLog, "repeat_uniform_moved_rigid")
    PriorMixture = ReadReference(PriorLog, "repeat_mixture_moved_rigid")
    New = [ReadReference(NewLog, f"rigid_repeat_{Mode}") for Mode in Modes]
    with open(TablePath, newline='', encoding='utf-8') as File:
        Rows = list(csv.DictReader(File))
    with open(PriorTablePath, newline='', encoding='utf-8') as File:
        PriorRows = list(csv.DictReader(File))
    if len(Rows) != New[0][5] or len(set(Row["seed"] for Row in Rows)) != len(Rows):
        raise ValueError("CSV batch count or seed uniqueness does not match the log")
    if len(PriorRows) != PriorUniform[5] or len(set(Row["seed"] for Row in PriorRows)) != len(PriorRows):
        raise ValueError("Prior CSV batch count or seed uniqueness does not match the log")
    Columns = {(Mode, Channel): [float(Row[f"{Mode}_{Channel}"]) for Row in Rows]
               for Mode in Modes for Channel in Channels}
    for Mode in Modes:
        for Index, Channel in enumerate(Channels):
            Reference = ReadReference(NewLog, f"rigid_repeat_{Mode}", Index)
            Mean, Se = Summarize(Columns[(Mode, Channel)])
            if not math.isclose(Mean, Reference[2], rel_tol=2e-7) or not math.isclose(Se, Reference[3], rel_tol=2e-7):
                raise ValueError(f"CSV moments differ from log: {Mode} {Channel}")
    print(f"RIGID_ANALYSIS rows={len(Rows)} csv_sha256={hashlib.sha256(Path(TablePath).read_bytes()).hexdigest()} "
          f"prior_rows={len(PriorRows)} prior_csv_sha256={hashlib.sha256(Path(PriorTablePath).read_bytes()).hexdigest()}")
    for Mode, Reference in (("uniform", PriorUniform), ("mixture", PriorMixture)):
        Values = [float(Row[f"{Mode}_moved_rigid_red"]) for Row in PriorRows]
        Mean, Se = Summarize(Values)
        if not math.isclose(Mean, Reference[2], rel_tol=2e-7) or not math.isclose(Se, Reference[3], rel_tol=2e-7):
            raise ValueError(f"Prior CSV red moments differ from log: {Mode}")
        TailCount = max(1, math.ceil(len(Values) * .01))
        TailShare = sum(sorted(Values)[-TailCount:]) / sum(Values)
        print(f"RIGID_ANALYSIS_PRIOR_SAMPLES mode={Mode} max={max(Values):.9g} top1pct_share={TailShare:.6f}")
    Cpu, CpuSe = New[0][:2]
    print(f"RIGID_ANALYSIS_NEW_CPU red={Cpu:.9g} se={CpuSe:.9g} paths={New[0][4]}")
    for Mode in Modes:
        for Channel in Channels:
            Values = Columns[(Mode, Channel)]
            Mean, Se = Summarize(Values)
            TailCount = max(1, math.ceil(len(Values) * .01))
            TailShare = sum(sorted(Values)[-TailCount:]) / sum(Values)
            Reference = ReadReference(NewLog, f"rigid_repeat_{Mode}", Channels.index(Channel))
            Z = (Mean - Reference[0]) / math.hypot(Se, Reference[1])
            print(f"RIGID_ANALYSIS mode={Mode} channel={Channel} mean={Mean:.9g} se={Se:.9g} "
                  f"sd={statistics.stdev(Values):.9g} max={max(Values):.9g} top1pct_share={TailShare:.6f} "
                  f"nominal_cpu_z={Z:.6g}")
    for First, Second, Label in ((1, 0, "uniform_spatial_minus_single"), (3, 2, "mixture_spatial_minus_single"),
                                 (2, 0, "mixture_minus_uniform_single"), (3, 1, "mixture_minus_uniform_spatial")):
        for Channel in Channels:
            Difference = [A - B for A, B in zip(Columns[(Modes[First], Channel)], Columns[(Modes[Second], Channel)])]
            Mean, Se = Summarize(Difference)
            print(f"RIGID_ANALYSIS_PAIRED name={Label} channel={Channel} mean={Mean:.9g} se={Se:.9g}")
    RatioPairs = ((3, 1), (1, 0), (3, 2))
    Red = [Columns[(Mode, "red")] for Mode in Modes]
    for (Numerator, Denominator), (Low, High) in zip(RatioPairs, BootstrapRatios(Red, RatioPairs)):
        Ratio = statistics.stdev(Red[Numerator]) / statistics.stdev(Red[Denominator])
        print(f"RIGID_ANALYSIS_RATIO numerator={Modes[Numerator]} denominator={Modes[Denominator]} "
              f"channel=red sd_ratio={Ratio:.6g} bootstrap95={Low:.6g},{High:.6g}")
    Ranked = sorted(Rows, key=lambda Row: float(Row["uniform_spatial_red"]), reverse=True)
    for Removed in (1, 3):
        Remaining = Ranked[Removed:]
        Uniform = [float(Row["uniform_spatial_red"]) for Row in Remaining]
        Mixture = [float(Row["mixture_spatial_red"]) for Row in Remaining]
        print(f"RIGID_ANALYSIS_INFLUENCE removed_largest_uniform_pairs={Removed} "
              f"remaining={len(Remaining)} sd_ratio={statistics.stdev(Mixture) / statistics.stdev(Uniform):.6g}")
    for Group in sorted(set(int(Row["group"]) for Row in Rows)):
        Members = [Row for Row in Rows if int(Row["group"]) == Group]
        for Mode in Modes:
            Mean, Se = Summarize([float(Row[f"{Mode}_red"]) for Row in Members])
            print(f"RIGID_ANALYSIS_GROUP group={Group} mode={Mode} mean={Mean:.9g} se={Se:.9g} n={len(Members)}")
        Uniform = [float(Row["uniform_spatial_red"]) for Row in Members]
        Mixture = [float(Row["mixture_spatial_red"]) for Row in Members]
        print(f"RIGID_ANALYSIS_GROUP_RATIO group={Group} mixture_over_uniform_red="
              f"{statistics.stdev(Mixture) / statistics.stdev(Uniform):.6g} "
              f"uniform_max={max(Uniform):.9g} mixture_max={max(Mixture):.9g}")
    PriorZ = (PriorUniform[2] - Cpu) / math.hypot(PriorUniform[3], CpuSe)
    PriorCpuZ = (PriorUniform[0] - Cpu) / math.hypot(PriorUniform[1], CpuSe)
    SequenceZ = (New[1][2] - PriorUniform[2]) / math.hypot(New[1][3], PriorUniform[3])
    print(f"RIGID_ANALYSIS_PRIOR uniform_vs_new_cpu_z={PriorZ:.6g} old_cpu_vs_new_cpu_z={PriorCpuZ:.6g} "
          f"new_vs_old_uniform_z={SequenceZ:.6g}")
    ChartRows = [
        ("New CPU reference", Cpu, CpuSe, "#128171"),
        ("Prior CPU reference", PriorUniform[0], PriorUniform[1], "#4e7298"),
        ("Prior uniform spatial (256)", PriorUniform[2], PriorUniform[3], "#c36a28"),
        ("Prior mixture spatial (256)", PriorMixture[2], PriorMixture[3], "#c36a28"),
    ]
    ChartRows += [(f"New {Mode.replace('_', ' ')} ({len(Rows):,})", Values[2], Values[3], "#128171")
                  for Mode, Values in zip(Modes, New)]
    Chart(ChartRows, len(Rows), New[0][4], SvgPath, Optional[0] if Optional else None)
    print(f"RIGID_ANALYSIS_CHART svg={SvgPath} png={Optional[0] if Optional else 'none'}")


if __name__ == "__main__":
    Main()
