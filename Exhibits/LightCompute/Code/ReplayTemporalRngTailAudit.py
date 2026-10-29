#!/usr/bin/env python3
"""Re-audit the paired 128-cohort RNG studies for tail leverage and cross-study reproducibility."""
from pathlib import Path
import math
import re
import sys

MODES = ("fresh1", "fresh2", "fresh4", "temporal1", "temporal4")
FLOORS = ("default", "lower")
ARMS = ("baseline", "decorrelated")
COHORTS = 128
NUM = re.compile(r"^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$")

def fields(line):
    return dict(re.findall(r"([A-Za-z_]+)=([^\s]+)", line))

def number(row, key):
    text = row[key]
    if not NUM.fullmatch(text):
        raise ValueError(f"non-numeric {key}={text}")
    value = float(text)
    if not math.isfinite(value):
        raise ValueError(f"non-finite {key}={text}")
    return value

def moments(values):
    if not values:
        raise ValueError("empty cohort sequence")
    mean = sum(values) / len(values)
    if len(values) < 2:
        return mean, 0.0
    variance = sum((x - mean) ** 2 for x in values) / (len(values) - 1)
    return mean, math.sqrt(max(0.0, variance) / len(values))

def quantile(values, p):
    ordered = sorted(values)
    x = p * (len(ordered) - 1)
    lo = int(math.floor(x)); hi = int(math.ceil(x))
    return ordered[lo] + (ordered[hi] - ordered[lo]) * (x - lo)

def load(path, prefix):
    batches, results = {}, {}
    for line in Path(path).read_text(errors="replace").splitlines():
        if line.startswith(prefix + "_BATCH"):
            row = fields(line)
            key = (row["arm"], row["mode"], row["floor"], int(row["batch"]))
            if key in batches:
                raise ValueError(f"duplicate cohort row in {path}: {key}")
            batches[key] = row
        elif line.startswith(prefix + "_RESULT"):
            row = fields(line)
            key = (row["arm"], row["mode"], row["floor"])
            if key in results:
                raise ValueError(f"duplicate result row in {path}: {key}")
            results[key] = row
    expected_batches = {(arm, mode, floor, cohort) for arm in ARMS for mode in MODES
                        for floor in FLOORS for cohort in range(COHORTS)}
    expected_results = {(arm, mode, floor) for arm in ARMS for mode in MODES for floor in FLOORS}
    if set(batches) != expected_batches:
        raise ValueError(f"{path}: cohort coverage {len(batches)}/{len(expected_batches)}")
    if set(results) != expected_results:
        raise ValueError(f"{path}: result coverage {len(results)}/{len(expected_results)}")
    return batches, results

def summarize(rows, reference):
    means = [number(r, "mean") for r in rows]
    mses = [number(r, "mse") for r in rows]
    max_pixels = [number(r, "max_pixel") for r in rows]
    mean, mean_se = moments(means)
    mse, mse_se = moments(mses)
    gate = abs(mean - reference) <= 5 * mean_se + .001
    top = max(1, math.ceil(.05 * len(mses)))
    largest = max(mses)
    return {
        "mean": mean, "mean_se": mean_se, "mean_p05": quantile(means, .05),
        "mean_p50": quantile(means, .50), "mean_p95": quantile(means, .95),
        "half_a": moments(means[:64])[0], "half_b": moments(means[64:])[0],
        "mse": mse, "mse_se": mse_se, "rmse": math.sqrt(mse),
        "mse_p50": quantile(mses, .50), "mse_p95": quantile(mses, .95),
        "mse_p99": quantile(mses, .99), "mse_max": largest,
        "top5_share": sum(sorted(mses, reverse=True)[:top]) / sum(mses),
        "largest_share": largest / sum(mses), "max_pixel_p99": quantile(max_pixels, .99),
        "max_pixel_max": max(max_pixels), "gate": gate,
        "gate_allowance": 5 * mean_se + .001,
    }

def verify_summary(rows, result, stats, key):
    for field in ("mean", "mean_se", "mse", "mse_se", "rmse"):
        if not math.isclose(number(result, field), stats[field], rel_tol=2e-9, abs_tol=2e-10):
            raise ValueError(f"{key}: logged {field} disagrees with recomputed 128 cohorts")
    if int(result["mean_gate"]) != int(stats["gate"]):
        raise ValueError(f"{key}: logged mean gate disagrees with recomputation")

def write_chart(output_path, summary, reference):
    """Write a deterministic SVG of the two lower-floor temporal cells."""
    width, height = 1440, 940
    palette = {"baseline": "#b7791f", "path_hash": "#2563eb", "selection_hash": "#0f8b6d"}
    labels = {"baseline": "Baseline", "path_hash": "Path RNG", "selection_hash": "Selection RNG"}
    x0, x1, y0, y1 = 92, 840, 190, 565
    ymin, ymax = .78, .87
    def yval(value):
        return y0 + (ymax - value) / (ymax - ymin) * (y1 - y0)
    def text(x, y, value, size=15, color="#334155", weight="400", anchor="start"):
        return f'<text x="{x}" y="{y}" font-family="Inter,Segoe UI,Arial,sans-serif" font-size="{size}" fill="{color}" font-weight="{weight}" text-anchor="{anchor}">{value}</text>'
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
             '<rect width="100%" height="100%" fill="#f8fafc"/>',
             '<rect x="24" y="22" width="1392" height="894" rx="22" fill="white" stroke="#dbe3ee"/>',
             text(58, 76, "Temporal RNG lower-floor tail audit", 30, "#0f172a", "700"),
             text(58, 108, "128 paired cohorts · mean-reference behavior and concentration of cohort MSE", 16, "#475569"),
             text(58, 146, "A  ·  Mean estimate ± 1 cohort SE", 18, "#0f172a", "700"),
             text(952, 146, "B  ·  Share of total MSE in worst 5% of cohorts", 18, "#0f172a", "700")]
    # Mean distribution: plot grid and fixed reference.
    for tick in (.78, .80, .82, .84, .86):
        yy = yval(tick)
        parts.append(f'<line x1="{x0}" y1="{yy:.1f}" x2="{x1}" y2="{yy:.1f}" stroke="#e2e8f0"/>')
        parts.append(text(x0 - 12, yy + 5, f"{tick:.2f}", 13, "#64748b", anchor="end"))
    ref_y = yval(reference)
    parts.append(f'<line x1="{x0}" y1="{ref_y:.1f}" x2="{x1}" y2="{ref_y:.1f}" stroke="#111827" stroke-width="2" stroke-dasharray="7 6"/>')
    parts.append(text(x1 - 2, ref_y - 8, f"reference {reference:.6f}", 12, "#111827", "600", "end"))
    parts.append(f'<line x1="{x0}" y1="{y0}" x2="{x0}" y2="{y1}" stroke="#94a3b8"/><line x1="{x0}" y1="{y1}" x2="{x1}" y2="{y1}" stroke="#94a3b8"/>')
    centers = {"temporal1": 280, "temporal4": 650}
    for mode, caption in (("temporal1", "Temporal 1"), ("temporal4", "Temporal 4")):
        center = centers[mode]
        parts.append(text(center, y1 + 30, caption, 15, "#334155", "600", "middle"))
        for offset, variant in zip((-70, 0, 70), ("baseline", "path_hash", "selection_hash")):
            stat = summary[(variant, mode, "lower")]
            x = center + offset
            yy = yval(stat["mean"])
            up, down = yval(stat["mean"] + stat["mean_se"]), yval(stat["mean"] - stat["mean_se"])
            color = palette[variant]
            parts.append(f'<line x1="{x}" y1="{up:.1f}" x2="{x}" y2="{down:.1f}" stroke="{color}" stroke-width="2.5"/>')
            parts.append(f'<line x1="{x-7}" y1="{up:.1f}" x2="{x+7}" y2="{up:.1f}" stroke="{color}" stroke-width="2"/><line x1="{x-7}" y1="{down:.1f}" x2="{x+7}" y2="{down:.1f}" stroke="{color}" stroke-width="2"/>')
            parts.append(f'<circle cx="{x}" cy="{yy:.1f}" r="7" fill="{color}" stroke="white" stroke-width="2"/>')
            if not stat["gate"]:
                parts.append(f'<circle cx="{x}" cy="{yy:.1f}" r="12" fill="none" stroke="#dc2626" stroke-width="2"/>')
    # Right panel: six horizontal bars, one per variant/cell.
    bar_x, bar_width = 1110, 250
    rows = [(mode, variant) for mode in ("temporal1", "temporal4") for variant in ("baseline", "path_hash", "selection_hash")]
    for i, (mode, variant) in enumerate(rows):
        yy = 206 + i * 51
        stat = summary[(variant, mode, "lower")]
        share = stat["top5_share"]
        mode_short = "T1" if mode == "temporal1" else "T4"
        parts.append(text(952, yy + 18, f"{mode_short}  {labels[variant]}", 13, "#334155"))
        parts.append(f'<rect x="{bar_x}" y="{yy}" width="{bar_width}" height="22" rx="6" fill="#e2e8f0"/>')
        parts.append(f'<rect x="{bar_x}" y="{yy}" width="{bar_width*min(1,share):.1f}" height="22" rx="6" fill="{palette[variant]}"/>')
        parts.append(text(bar_x + bar_width + 12, yy + 17, f"{share:.1%}", 13, "#334155", "600"))
    parts.append(text(952, 535, "Rare-cohort tail concentration", 13, "#64748b"))
    # Focus box with the stable candidate failures and interpretation limits.
    parts.extend(['<rect x="58" y="630" width="1324" height="240" rx="16" fill="#f1f5f9" stroke="#cbd5e1"/>',
                  text(84, 670, "What the tails say", 19, "#0f172a", "700")])
    path = summary[("path_hash", "temporal1", "lower")]
    sel = summary[("selection_hash", "temporal4", "lower")]
    parts.append(text(84, 710, f"Path RNG · Temporal 1: mean {path['mean']:.6f} ± {path['mean_se']:.6f}; worst-5% MSE share {path['top5_share']:.1%}; half means {path['half_a']:.6f} / {path['half_b']:.6f}.", 15, "#1e3a8a"))
    parts.append(text(84, 744, f"Selection RNG · Temporal 4: mean {sel['mean']:.6f} ± {sel['mean_se']:.6f}; worst-5% MSE share {sel['top5_share']:.1%}; half means {sel['half_a']:.6f} / {sel['half_b']:.6f}.", 15, "#065f46"))
    parts.append(text(84, 788, "Both failed cells have narrow, consistently low cohort means—not a handful of high-MSE outliers. The baseline's lower-floor temporal cells", 14, "#334155"))
    parts.append(text(84, 814, "and Path RNG / Temporal 4 pass partly under very large rare-cohort tails. This is distribution evidence, not pixel-history provenance or proof of asymptotic bias.", 14, "#334155"))
    parts.append(text(84, 848, "Whiskers: ±1 cohort SE. Bars: fraction of summed cohort MSE in the largest ceil(5% × 128) cohorts. Red rings mark failed fixed mean gates.", 12, "#64748b"))
    parts.append('</svg>')
    Path(output_path).write_text("\n".join(parts), encoding="utf-8")

def run(selection_path, path_path):
    sel_batches, sel_results = load(selection_path, "REPLAY_SELECTION_STREAM")
    path_batches, path_results = load(path_path, "REPLAY_PATH_STREAM")
    reference = number(sel_results[("baseline", "fresh1", "default")], "reference")
    if not math.isclose(reference, number(path_results[("baseline", "fresh1", "default")], "reference"), abs_tol=1e-12):
        raise ValueError("the two studies use different references")

    baseline_image_matches = selection_stream_matches = path_stream_changes = prior_selection_changes = 0
    total = len(MODES) * len(FLOORS) * COHORTS
    for mode in MODES:
        for floor in FLOORS:
            for cohort in range(COHORTS):
                sbase = sel_batches[("baseline", mode, floor, cohort)]
                pbase = path_batches[("baseline", mode, floor, cohort)]
                ppath = path_batches[("decorrelated", mode, floor, cohort)]
                sselect = sel_batches[("decorrelated", mode, floor, cohort)]
                baseline_image_matches += sbase["hash"] == pbase["hash"]
                selection_stream_matches += pbase["selection_hash"] == ppath["selection_hash"]
                path_stream_changes += pbase["path_hash"] != ppath["path_hash"]
                prior_selection_changes += sbase["selection_hash"] != sselect["selection_hash"]
                if sbase["hash"] != pbase["hash"]:
                    raise ValueError(f"baseline output changed between studies: {mode}/{floor}/{cohort}")
    if baseline_image_matches != total or selection_stream_matches != total or path_stream_changes != total or prior_selection_changes != total:
        raise ValueError(f"stream-control mismatch: baseline={baseline_image_matches}, selection_fixed={selection_stream_matches}, path_changed={path_stream_changes}, prior_selection_changed={prior_selection_changes}, expected={total}")

    summary = {}
    failures = []
    for study, batches, results in (("selection", sel_batches, sel_results),
                                    ("path", path_batches, path_results)):
        for arm in ARMS:
            variant = "baseline" if arm == "baseline" else ("selection_hash" if study == "selection" else "path_hash")
            for mode in MODES:
                for floor in FLOORS:
                    key = (arm, mode, floor)
                    rows = [batches[(arm, mode, floor, n)] for n in range(COHORTS)]
                    ref = number(results[key], "reference")
                    stats = summarize(rows, ref)
                    verify_summary(rows, results[key], stats, (variant, mode, floor))
                    summary[(variant, mode, floor)] = stats
                    if not stats["gate"]:
                        failures.append((variant, mode, floor))
                    if variant == "baseline" and study == "path":
                        continue
                    print(f"DIST variant={variant:14s} mode={mode:9s} floor={floor:7s} "
                          f"mean={stats['mean']:.9f}±{stats['mean_se']:.9f} "
                          f"mean_p05/p50/p95={stats['mean_p05']:.6f}/{stats['mean_p50']:.6f}/{stats['mean_p95']:.6f} "
                          f"RMSE={stats['rmse']:.6f} MSE_p50/p95/p99/max="
                          f"{stats['mse_p50']:.5g}/{stats['mse_p95']:.5g}/{stats['mse_p99']:.5g}/{stats['mse_max']:.5g} "
                          f"top5_share={stats['top5_share']:.3f} max_pixel_p99/max={stats['max_pixel_p99']:.4g}/{stats['max_pixel_max']:.4g} "
                          f"half_means={stats['half_a']:.6f}/{stats['half_b']:.6f} "
                          f"gate={'PASS' if stats['gate'] else 'FAIL'} allowance={stats['gate_allowance']:.6f}")

    for variant in ("path_hash", "selection_hash"):
        source_batches = path_batches if variant == "path_hash" else sel_batches
        source_arm = "decorrelated"
        for mode in MODES:
            for floor in FLOORS:
                differences_mean, differences_mse = [], []
                for n in range(COHORTS):
                    candidate = source_batches[(source_arm, mode, floor, n)]
                    baseline = path_batches[("baseline", mode, floor, n)]
                    differences_mean.append(number(candidate, "mean") - number(baseline, "mean"))
                    differences_mse.append(number(candidate, "mse") - number(baseline, "mse"))
                dm, dse = moments(differences_mean)
                dq, dqse = moments(differences_mse)
                print(f"PAIRED variant={variant:14s} mode={mode:9s} floor={floor:7s} "
                      f"delta_mean={dm:+.8f}±{dse:.8f} delta_mse={dq:+.8f}±{dqse:.8f} "
                      f"95pct_mean=[{dm-1.96*dse:+.8f},{dm+1.96*dse:+.8f}] "
                      f"delta_rmse={summary[(variant,mode,floor)]['rmse']-summary[('baseline',mode,floor)]['rmse']:+.6f}")

    print(f"CONTROL same_baseline_image_hashes={baseline_image_matches}/{total} "
          f"path_arm_selection_hashes_equal={selection_stream_matches}/{total} "
          f"path_hashes_changed={path_stream_changes}/{total} prior_selection_hashes_changed={prior_selection_changes}/{total}")
    print("GATE_FAILURES " + (", ".join("/".join(x) for x in failures) if failures else "none"))
    print("NOTICE This audits 128 cohort aggregates only; it cannot attribute individual pixel histories or establish asymptotic bias.")
    return summary, reference

if __name__ == "__main__":
    if len(sys.argv) not in (3, 4):
        raise SystemExit(f"Usage: {Path(sys.argv[0]).name} SELECTION_LOG PATH_LOG [CHART.svg]")
    try:
        summary, reference = run(sys.argv[1], sys.argv[2])
        if len(sys.argv) == 4:
            write_chart(sys.argv[3], summary, reference)
            print(f"VISUAL {sys.argv[3]}")
    except (KeyError, ValueError, ZeroDivisionError) as error:
        print(f"FAIL ReplayTemporalRngTailAudit: {error}", file=sys.stderr)
        raise SystemExit(2)
