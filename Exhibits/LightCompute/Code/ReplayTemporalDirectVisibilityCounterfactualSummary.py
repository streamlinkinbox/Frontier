#!/usr/bin/env python3
"""Summarize fixed-origin, fixed-endpoint CPU traces through mask 1 and mask 3."""
from pathlib import Path
import importlib.util
import math
import sys

HERE = Path(__file__).parent
PAIR_SPEC = importlib.util.spec_from_file_location(
    "ReplayTemporalDirectEndpointPairSummary",
    HERE / "ReplayTemporalDirectEndpointPairSummary.py")
PAIR = importlib.util.module_from_spec(PAIR_SPEC)
PAIR_SPEC.loader.exec_module(PAIR)
VISIBILITY = PAIR.VISIBILITY
SCENARIOS, MODES = PAIR.SCENARIOS, PAIR.MODES
PIXEL_IDS = PAIR.PIXEL_IDS
TRANSITIONS = ("blocked_to_blocked", "blocked_to_visible",
               "visible_to_blocked", "visible_to_visible")
COLORS = ("#cbd5e1", "#93c5fd", "#fb923c", "#86efac")


def parse(path):
    # Reuse the stricter endpoint-pair validator so this audit cannot silently
    # detach its counterfactual traces from the matched GPU endpoint evidence.
    PAIR.parse(path)
    parsed = VISIBILITY.load(path)
    summaries = {}
    for scenario in SCENARIOS:
        rows = parsed[scenario]["pixels"]
        marker = parsed[scenario]["pass"]
        if marker.get("fixed_endpoint_mask1_vs_mask3") != "verified":
            raise ValueError(f"{scenario}: fixed-endpoint mask-1/mask-3 marker missing")
        expected_gpu_match = "verified" if scenario == "base_only" else "crosscheck_only"
        if marker.get("mask1_gpu_cpu_match") != expected_gpu_match:
            raise ValueError(f"{scenario}: unexpected mask-1 CPU/GPU cross-check marker")
        summaries[scenario] = {}
        for mode in MODES:
            counts = [0] * 4
            cohort_changes, cohort_estimates = [], []
            pairs = gpu_matches = 0
            for cohort in range(parsed[scenario]["cohorts"]):
                changed = 0
                estimate_sum = 0.0
                for pixel in PIXEL_IDS:
                    row = rows[(scenario, mode, cohort, pixel)]
                    base_code = int(row["fixed_endpoint_mask1_code"])
                    combined_code = int(row["fixed_endpoint_mask3_code"])
                    ray = PAIR.nonnegative_int(row, "paired_ray")
                    if ray != 1 or base_code not in (1, 2) or combined_code not in (1, 2):
                        raise ValueError(f"{scenario}/{mode}: fixed-endpoint trace is missing or invalid")
                    if base_code == 1:
                        cell = 0 if combined_code == 1 else 1
                    else:
                        cell = 2 if combined_code == 1 else 3
                    counts[cell] += 1
                    pairs += 1
                    changed += int(base_code != combined_code)
                    estimate_sum += PAIR.finite(row, "direct_estimate_luma")
                    if scenario == "base_only":
                        gpu_code = int(row["paired_current_code"])
                        if base_code != gpu_code:
                            raise ValueError("base-only CPU mask-1 trace disagrees with paired GPU visibility")
                        gpu_matches += 1
                cohort_changes.append(changed)
                cohort_estimates.append(estimate_sum / len(PIXEL_IDS))
            expected_pairs = parsed[scenario]["cohorts"] * len(PIXEL_IDS)
            if pairs != expected_pairs or sum(counts) != expected_pairs:
                raise ValueError(f"{scenario}/{mode}: fixed-endpoint coverage {pairs}/{expected_pairs}")
            if scenario == "base_only" and gpu_matches != expected_pairs:
                raise ValueError(f"{scenario}/{mode}: mask-1 CPU/GPU match coverage {gpu_matches}/{expected_pairs}")
            correlation = PAIR.pearson(cohort_changes, cohort_estimates)
            summaries[scenario][mode] = {
                "counts": counts,
                "pairs": pairs,
                "changed": counts[1] + counts[2],
                "added_blocker_visibility_losses": counts[2],
                "gpu_matches": gpu_matches,
                "cohort_changes": cohort_changes,
                "cohort_estimates": cohort_estimates,
                "change_estimate_correlation": correlation,
            }
            corr_text = "undefined" if correlation is None else f"{correlation:+.5g}"
            print(f"FIXED_ENDPOINT_COUNTERFACTUAL scenario={scenario} mode={mode} "
                  f"mask1_to_mask3={','.join(map(str, counts))} "
                  f"visibility_changes={counts[1] + counts[2]}/{pairs} "
                  f"added_blocker_visibility_losses={counts[2]}/{pairs} "
                  f"mask1_gpu_matches={gpu_matches}/{pairs if scenario == 'base_only' else 0} "
                  f"cohort_change_vs_estimate_r={corr_text}")
    return summaries


def chart(path, summaries):
    width, height = 1240, 560
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
             '<rect width="100%" height="100%" fill="#f8fafc"/>',
             '<rect x="16" y="16" width="1208" height="528" rx="18" fill="white" stroke="#dbe3ee"/>',
             '<text x="44" y="54" font-family="Inter,Segoe UI,Arial,sans-serif" font-size="24" font-weight="700" fill="#0f172a">Fixed-origin, fixed-endpoint visibility counterfactual</text>',
             '<text x="44" y="80" font-family="Arial" font-size="13" fill="#475569">CPU triangle-space traces of GPU-read-back endpoints: mask 1 = base geometry; mask 3 = same base plus added blocker.</text>']
    y_positions = [138, 230, 322, 414]
    index = 0
    for scenario in SCENARIOS:
        for mode in MODES:
            data = summaries[scenario][mode]
            y = y_positions[index]
            index += 1
            bb, bv, vb, vv = data["counts"]
            label = f"{scenario} · {mode}"
            parts.append(f'<text x="48" y="{y+20}" font-family="Arial" font-size="15" font-weight="700" fill="#0f172a">{label}</text>')
            bar_x, bar_y, bar_w, bar_h = 250, y, 700, 30
            cursor = bar_x
            for count, color in zip(data["counts"], COLORS):
                segment = bar_w * count / data["pairs"]
                if segment > 0:
                    parts.append(f'<rect x="{cursor:.2f}" y="{bar_y}" width="{segment:.2f}" height="{bar_h}" fill="{color}"/>')
                    if segment > 55:
                        parts.append(f'<text x="{cursor+segment/2:.2f}" y="{bar_y+20}" text-anchor="middle" font-family="Arial" font-size="11" fill="#0f172a">{count}</text>')
                cursor += segment
            rate = data["added_blocker_visibility_losses"] / data["pairs"]
            gpu = f"mask 1 CPU/GPU exact {data['gpu_matches']}/{data['pairs']}" if scenario == "base_only" else "GPU cross-check: separate added-only arm"
            parts.append(f'<text x="970" y="{y+14}" font-family="Arial" font-size="12" font-weight="700" fill="#9a3412">added-blocker visibility loss {data["added_blocker_visibility_losses"]} ({rate:.2%})</text>')
            parts.append(f'<text x="970" y="{y+32}" font-family="Arial" font-size="10" fill="#64748b">{gpu}; changed total {data["changed"]}/{data["pairs"]}</text>')
    legend_y = 492
    labels = ("blocked → blocked", "blocked → visible", "visible → blocked", "visible → visible")
    x = 48
    for label, color in zip(labels, COLORS):
        parts.append(f'<rect x="{x}" y="{legend_y-11}" width="14" height="14" rx="2" fill="{color}" stroke="#94a3b8"/>')
        parts.append(f'<text x="{x+21}" y="{legend_y}" font-family="Arial" font-size="11" fill="#334155">{label}</text>')
        x += 220
    parts.extend(['<text x="44" y="520" font-family="Arial" font-size="11" fill="#64748b">Fixture-only CPU counterfactual; receiver origin is held fixed and each sampled endpoint is identical across masks. No estimator or production shader changes.</text>',
                  '</svg>'])
    Path(path).write_text("\n".join(parts) + "\n", encoding="utf-8")


def run(log, svg):
    summaries = parse(log)
    chart(svg, summaries)
    print(f"VISUAL {svg}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(f"Usage: {Path(sys.argv[0]).name} RUN_LOG CHART.svg")
    try:
        run(*sys.argv[1:])
    except (KeyError, ValueError, ZeroDivisionError) as error:
        print(f"FAIL ReplayTemporalDirectVisibilityCounterfactualSummary: {error}", file=sys.stderr)
        raise SystemExit(2)
