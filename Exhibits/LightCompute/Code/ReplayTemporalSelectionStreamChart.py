#!/usr/bin/env python3
"""Render an SVG comparison plot from the audited paired selection-stream log."""
from pathlib import Path
import html
import math
import re
import sys

MODES = ("fresh1", "fresh2", "fresh4", "temporal1", "temporal4")
FLOORS = ("default", "lower")
ARMS = ("baseline", "decorrelated")

def parse_line(line):
    return dict(re.findall(r"([A-Za-z_]+)=([^\s]+)", line))

def esc(value):
    return html.escape(str(value), quote=True)

def main(log_path, svg_path):
    results = {}
    for line in Path(log_path).read_text(errors="replace").splitlines():
        if line.startswith("REPLAY_SELECTION_STREAM_RESULT"):
            row = parse_line(line)
            results[(row["arm"], row["mode"], row["floor"])] = row
    expected = {(arm, mode, floor) for arm in ARMS for mode in MODES for floor in FLOORS}
    if set(results) != expected:
        raise SystemExit(f"Expected {len(expected)} audited result rows, found {len(results)}")

    width, height = 1500, 1020
    left, right = 140, 1430
    panel_top, panel_bottom = 175, 515
    rmse_top, rmse_bottom = 655, 910
    step = (right - left) / len(MODES) / len(FLOORS)
    x_centers = [left + step * (i + .5) for i in range(len(MODES) * len(FLOORS))]
    baseline_color, candidate_color = "#5dc8ff", "#ffb35c"
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">']
    out.append('''<defs><style>
      text{font-family:DejaVu Sans,Arial,sans-serif;fill:#e7edf7}
      .sub{fill:#9eacc2}.grid{stroke:#273349;stroke-width:1}.axis{stroke:#69788f;stroke-width:1.4}
      .title{font-size:31px;font-weight:700}.panel{font-size:21px;font-weight:700}.tick{font-size:14px}.label{font-size:14px}
      .note{font-size:13px;fill:#a9b7ca}.legend{font-size:16px;font-weight:600}
    </style></defs>''')
    out.append(f'<rect width="{width}" height="{height}" fill="#0b1220"/>')
    out.append('<text x="70" y="58" class="title">Moving-visibility holdout · selection-stream isolation</text>')
    out.append('<text x="70" y="91" class="sub" font-size="16">128 paired cohorts · 512 far-group pixels/cohort · path seeds and temporal weights fixed</text>')
    out.append(f'<circle cx="1020" cy="53" r="7" fill="{baseline_color}"/><text x="1038" y="59" class="legend">Original selection RNG</text>')
    out.append(f'<circle cx="1240" cy="53" r="7" fill="{candidate_color}"/><text x="1258" y="59" class="legend">Decorrelated hash</text>')

    # Mean error relative to the independent integral. Error bars are one standard error;
    # the explicit gate is |error| <= 5 SE + 0.001, so the plotted intervals are descriptive.
    out.append('<text x="70" y="139" class="panel">Mean error vs reference (one cohort-level SE)</text>')
    ymin, ymax = -.012, .006
    ymap = lambda value: panel_bottom - (value - ymin) * (panel_bottom - panel_top) / (ymax - ymin)
    for tick in (-.01, -.005, 0, .005):
        y = ymap(tick)
        out.append(f'<line x1="{left}" y1="{y:.2f}" x2="{right}" y2="{y:.2f}" class="grid"/>')
        out.append(f'<text x="{left-16}" y="{y+5:.2f}" text-anchor="end" class="tick sub">{tick*1000:+.0f}e-3</text>')
    out.append(f'<line x1="{left}" y1="{ymap(0):.2f}" x2="{right}" y2="{ymap(0):.2f}" stroke="#eaf2ff" stroke-width="2"/>')
    out.append(f'<line x1="{left}" y1="{panel_top}" x2="{left}" y2="{panel_bottom}" class="axis"/>')
    out.append(f'<line x1="{left}" y1="{panel_bottom}" x2="{right}" y2="{panel_bottom}" class="axis"/>')
    for i, mode in enumerate(MODES):
        for j, floor in enumerate(FLOORS):
            index = i * 2 + j
            center = x_centers[index]
            out.append(f'<line x1="{center:.2f}" y1="{panel_top}" x2="{center:.2f}" y2="{panel_bottom}" stroke="#182337"/>')
            out.append(f'<text x="{center:.2f}" y="{panel_bottom+24}" text-anchor="middle" class="label">{mode}</text>')
            out.append(f'<text x="{center:.2f}" y="{panel_bottom+43}" text-anchor="middle" class="tick sub">{floor}</text>')
            for arm, offset, color in (("baseline", -7, baseline_color), ("decorrelated", 7, candidate_color)):
                row = results[(arm, mode, floor)]
                delta = float(row["mean"]) - float(row["reference"])
                se = float(row["mean_se"])
                x = center + offset
                raw_y1, raw_y2 = ymap(delta-se), ymap(delta+se)
                y1, y2 = max(panel_top, min(panel_bottom, raw_y1)), max(panel_top, min(panel_bottom, raw_y2))
                out.append(f'<line x1="{x}" y1="{y1:.2f}" x2="{x}" y2="{y2:.2f}" stroke="{color}" stroke-width="2.5"/>')
                out.append(f'<line x1="{x-4}" y1="{y1:.2f}" x2="{x+4}" y2="{y1:.2f}" stroke="{color}" stroke-width="2"/>')
                out.append(f'<line x1="{x-4}" y1="{y2:.2f}" x2="{x+4}" y2="{y2:.2f}" stroke="{color}" stroke-width="2"/>')
                if max(raw_y1, raw_y2) > panel_bottom:
                    out.append(f'<path d="M{x-5},{panel_bottom-2} L{x+5},{panel_bottom-2} L{x},{panel_bottom+5} Z" fill="{color}"/>')
                if min(raw_y1, raw_y2) < panel_top:
                    out.append(f'<path d="M{x-5},{panel_top+2} L{x+5},{panel_top+2} L{x},{panel_top-5} Z" fill="{color}"/>')
                out.append(f'<circle cx="{x}" cy="{max(panel_top,min(panel_bottom,ymap(delta))):.2f}" r="5.5" fill="{color}"/>')
    out.append('<text x="70" y="589" class="panel">RMSE by method and floor · log scale</text>')
    out.append('<text x="70" y="611" class="note">RMSE = √(mean squared pixel error); rare-tail uncertainty is substantial for temporal1.</text>')
    log_min, log_max = math.log10(.08), math.log10(10)
    rmap = lambda value: rmse_bottom - (math.log10(max(.08, min(10, value))) - log_min) * (rmse_bottom-rmse_top)/(log_max-log_min)
    for tick in (.1, .2, .5, 1, 2, 5, 10):
        y = rmap(tick)
        out.append(f'<line x1="{left}" y1="{y:.2f}" x2="{right}" y2="{y:.2f}" class="grid"/>')
        out.append(f'<text x="{left-16}" y="{y+5:.2f}" text-anchor="end" class="tick sub">{tick:g}</text>')
    out.append(f'<line x1="{left}" y1="{rmse_top}" x2="{left}" y2="{rmse_bottom}" class="axis"/>')
    out.append(f'<line x1="{left}" y1="{rmse_bottom}" x2="{right}" y2="{rmse_bottom}" class="axis"/>')
    for i, mode in enumerate(MODES):
        for j, floor in enumerate(FLOORS):
            center = x_centers[i*2+j]
            for arm, offset, color in (("baseline", -7, baseline_color), ("decorrelated", 7, candidate_color)):
                row = results[(arm, mode, floor)]
                rmse = float(row["rmse"])
                x = center + offset
                y = rmap(rmse)
                shape = 'rect' if arm == 'baseline' else 'circle'
                if shape == 'rect':
                    out.append(f'<rect x="{x-4}" y="{y-4}" width="8" height="8" fill="{color}"/>')
                else:
                    out.append(f'<circle cx="{x}" cy="{y}" r="4.5" fill="{color}"/>')
                if mode == "temporal4" and floor == "lower" and arm == "decorrelated":
                    out.append(f'<circle cx="{x}" cy="{y}" r="10" fill="none" stroke="#ff626f" stroke-width="2.5"/>')
    out.append('<text x="70" y="973" class="note">Red ring: decorrelated temporal4/lower fails the predeclared mean gate. This finite-sample result is diagnostic, not a production recommendation.</text>')
    out.append('</svg>')
    target = Path(svg_path)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text('\n'.join(out))
    print(f'WROTE {target}')

if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(f"Usage: {Path(sys.argv[0]).name} RESULT_LOG OUTPUT.svg")
    main(sys.argv[1], sys.argv[2])
