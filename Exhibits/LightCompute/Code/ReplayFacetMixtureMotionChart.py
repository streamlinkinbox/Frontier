#!/usr/bin/env python3
# 🧩 Plot paired moving-GI dispersion directly from the retained sample table and validation log.

import csv
import hashlib
import math
import pathlib
import statistics
import sys
from xml.sax.saxutils import escape


# 📝 The chart verifies the point estimates against the CSV; interval endpoints are reported by the C++ audit.
def CompileChart(LogPath, TablePath, OutputPath, RasterPath=None):
    Rows = list(csv.DictReader(TablePath.open(newline="", encoding="utf-8")))
    if len(Rows) < 8 or len({Row["seed"] for Row in Rows}) != len(Rows):
        raise ValueError("The sample table does not contain distinct repeated seeds")
    Captured = {}
    for Line in LogPath.read_text(encoding="utf-8").splitlines():
        if not Line.startswith("MOTION_REPEAT_DISPERSION "):
            continue
        Parts = dict(Entry.split("=", 1) for Entry in Line.split()[1:])
        Key = (Parts["comparison"], Parts["probe"], int(Parts["channel"]))
        Captured[Key] = Parts
    Descriptions = [
        ("mixture_over_uniform", "rigid", 0, "red", "Rigid red: mixture / uniform", "mixture_moved", "uniform_moved"),
        ("mixture_over_uniform", "rigid", 1, "green", "Rigid green: mixture / uniform", "mixture_moved", "uniform_moved"),
        ("mixture_over_uniform", "floor", 1, "green", "Floor green: mixture / uniform", "mixture_moved", "uniform_moved"),
        ("moved_reuse_over_fresh", "rigid", 1, "green", "Rigid green moved: reused / fresh", "reused_moved", "mixture_moved"),
        ("moved_reuse_over_fresh", "floor", 1, "green", "Floor green moved: reused / fresh", "reused_moved", "mixture_moved"),
        ("blocked_reuse_over_fresh", "rigid", 1, "green", "Rigid green blocked: reused / fresh", "reused_blocked", "mixture_blocked"),
        ("blocked_reuse_over_fresh", "floor", 1, "green", "Floor green blocked: reused / fresh", "reused_blocked", "mixture_blocked"),
        ("reopened_reuse_over_fresh", "rigid", 1, "green", "Rigid green reopened: reused / fresh", "reused_reopened", "mixture_reopened"),
        ("reopened_reuse_over_fresh", "floor", 1, "green", "Floor green reopened: reused / fresh", "reused_reopened", "mixture_reopened"),
    ]
    Comparisons = []
    for Name, Probe, Channel, ColourName, Label, Numerator, Denominator in Descriptions:
        Entry = Captured[(Name, Probe, Channel)]
        NumeratorSamples = [float(Row[f"{Numerator}_{Probe}_{ColourName}"]) for Row in Rows]
        DenominatorSamples = [float(Row[f"{Denominator}_{Probe}_{ColourName}"]) for Row in Rows]
        Ratio = statistics.pstdev(NumeratorSamples) / statistics.pstdev(DenominatorSamples)
        Recorded = float(Entry["sd_ratio"])
        if int(Entry["samples"]) != len(Rows) or abs(Ratio - Recorded) > max(1e-8, abs(Recorded) * 2e-8):
            raise ValueError(f"CSV and validation log disagree for {Name}, {Probe}")
        Lower, Upper = map(float, Entry["bootstrap_95"].split(","))
        if not (math.isfinite(Lower) and math.isfinite(Upper) and 0 <= Lower <= Upper):
            raise ValueError("Invalid nominal bootstrap endpoints")
        Comparisons.append((Label, Ratio, Lower, Upper))
    Maximum = max(1.5, math.ceil(max(Upper for _, _, _, Upper in Comparisons) * 2) / 2)
    Width, Height = 1100, 630
    Left, Right = 350, 810
    Bottom = 125 + (len(Comparisons) - 1) * 45 + 24
    Scale = (Right - Left) / Maximum
    Lines = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{Width}" height="{Height}" viewBox="0 0 {Width} {Height}">',
        '<rect width="100%" height="100%" fill="#f7f8fa"/>',
        '<text x="34" y="43" font-family="sans-serif" font-size="24" fill="#172131" font-weight="700">'
        'Moving GI: sample variability</text>',
        f'<text x="35" y="71" font-family="sans-serif" font-size="14" fill="#4b5969">'
        f'{len(Rows)} paired frame seeds · ratios of batch standard deviations · lower is less variable</text>',
    ]
    for Tick in range(int(Maximum * 2) + 1):
        Number = Tick / 2
        X = Left + Number * Scale
        Dashed = ' stroke-dasharray="5 5"' if Number == 1 else ""
        Colour = "#7e8995" if Number == 1 else "#e1e5ea"
        Lines.append(f'<line x1="{X:.2f}" y1="104" x2="{X:.2f}" y2="{Bottom}" '
                     f'stroke="{Colour}" stroke-width="1"{Dashed}/>')
        Lines.append(f'<text x="{X:.2f}" y="{Bottom + 23}" text-anchor="middle" font-family="sans-serif" '
                     f'font-size="13" fill="#4b5969">{Number:g}</text>')
    for Position, (Label, Ratio, Lower, Upper) in enumerate(Comparisons):
        Y = 125 + Position * 45
        Start, End, Point = Left + Lower * Scale, Left + Upper * Scale, Left + Ratio * Scale
        Colour = "#16806e" if Ratio < 1 else "#bd642b"
        Lines.append(f'<text x="{Left - 14}" y="{Y + 5}" text-anchor="end" font-family="sans-serif" '
                     f'font-size="14" fill="#263444">{escape(Label)}</text>')
        Lines.append(f'<line x1="{Start:.2f}" y1="{Y}" x2="{End:.2f}" y2="{Y}" '
                     'stroke="#5a6673" stroke-width="3"/>')
        for X in (Start, End):
            Lines.append(f'<line x1="{X:.2f}" y1="{Y - 6}" x2="{X:.2f}" y2="{Y + 6}" '
                         'stroke="#5a6673" stroke-width="2"/>')
        Lines.append(f'<circle cx="{Point:.2f}" cy="{Y}" r="6" fill="{Colour}"/>')
        Lines.append(f'<text x="{Right + 20}" y="{Y + 5}" font-family="sans-serif" font-size="13" '
                     f'fill="#263444">{Ratio:.2f}  ({Lower:.2f} to {Upper:.2f})</text>')
    Lines.extend([
        f'<text x="35" y="{Bottom + 60}" font-family="sans-serif" font-size="13" fill="#4b5969">'
        'Bars: nominal paired-bootstrap 95% intervals, conditional on the sampled frames; not an equal-cost comparison.</text>',
        f'<text x="35" y="{Bottom + 80}" font-family="sans-serif" font-size="12" fill="#697786">'
        'SwiftShader CPU Vulkan · two fixed probes · shader-stream independence and general unbiasedness unproven</text>',
        '</svg>',
    ])
    OutputPath.parent.mkdir(parents=True, exist_ok=True)
    OutputPath.write_text("\n".join(Lines) + "\n", encoding="utf-8")
    if RasterPath is not None:
        from PIL import Image, ImageDraw, ImageFont

        ImageContent = Image.new("RGB", (Width, Height), "#f7f8fa")
        Canvas = ImageDraw.Draw(ImageContent)
        Heading = ImageFont.load_default(size=24)
        Body = ImageFont.load_default(size=14)
        Small = ImageFont.load_default(size=13)
        Caption = ImageFont.load_default(size=12)
        Canvas.text((34, 20), "Moving GI: sample variability", font=Heading, fill="#172131")
        Canvas.text((35, 51), f"{len(Rows)} paired frame seeds · ratios of batch standard deviations · lower is less variable",
                    font=Body, fill="#4b5969")
        for Tick in range(int(Maximum * 2) + 1):
            Number = Tick / 2
            X = Left + Number * Scale
            if Number == 1:
                for Y in range(104, Bottom, 10):
                    Canvas.line((X, Y, X, min(Y + 5, Bottom)), fill="#7e8995", width=1)
            else:
                Canvas.line((X, 104, X, Bottom), fill="#e1e5ea", width=1)
            Canvas.text((X, Bottom + 10), f"{Number:g}", font=Small, fill="#4b5969", anchor="mt")
        for Position, (Label, Ratio, Lower, Upper) in enumerate(Comparisons):
            Y = 125 + Position * 45
            Start, End, Point = Left + Lower * Scale, Left + Upper * Scale, Left + Ratio * Scale
            Colour = "#16806e" if Ratio < 1 else "#bd642b"
            Canvas.text((Left - 14, Y), Label, font=Body, fill="#263444", anchor="rm")
            Canvas.line((Start, Y, End, Y), fill="#5a6673", width=3)
            for X in (Start, End):
                Canvas.line((X, Y - 6, X, Y + 6), fill="#5a6673", width=2)
            Canvas.ellipse((Point - 6, Y - 6, Point + 6, Y + 6), fill=Colour)
            Canvas.text((Right + 20, Y), f"{Ratio:.2f}  ({Lower:.2f} to {Upper:.2f})", font=Small,
                        fill="#263444", anchor="lm")
        Canvas.text((35, Bottom + 48), "Bars: nominal paired-bootstrap 95% intervals, conditional on sampled frames; not an equal-cost comparison.",
                    font=Small, fill="#4b5969")
        Canvas.text((35, Bottom + 70), "SwiftShader CPU Vulkan · two fixed probes · independent streams and general unbiasedness unproven",
                    font=Caption, fill="#697786")
        RasterPath.parent.mkdir(parents=True, exist_ok=True)
        ImageContent.save(RasterPath)
    Digest = hashlib.sha256(TablePath.read_bytes()).hexdigest()
    print(f"MOTION_REPEAT_CHART rows={len(Rows)} table_sha256={Digest} svg={OutputPath} png={RasterPath}")


if __name__ == "__main__":
    if len(sys.argv) not in (4, 5):
        raise SystemExit("Usage: python ReplayFacetMixtureMotionChart.py LOG CSV OUTPUT.svg [OUTPUT.png]")
    CompileChart(*(pathlib.Path(Argument) for Argument in sys.argv[1:]))
