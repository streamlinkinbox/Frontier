#!/usr/bin/env python3
"""Validate paired same-seed direct-NEE segment visibility from the controlled-emitter holdout."""
from pathlib import Path
import math
import re
import sys

MODES = ("temporal1", "temporal4")
COHORTS = 128
GRID = 32
PIXEL_IDS = tuple(p for p in range(GRID * GRID) if (p % GRID + p // GRID) % 2 == 1)


def fields(line):
    return dict(re.findall(r"([A-Za-z_][A-Za-z_0-9]*)=([^\s]+)", line))


def number(row, key):
    value = float(row[key])
    if not math.isfinite(value):
        raise ValueError(f"non-finite {key}={row[key]}")
    return value


def count(row, key):
    value = int(row[key])
    if value < 0:
        raise ValueError(f"negative {key}={value}")
    return value


def load(path):
    pixels, results, passes, headers = {}, {}, [], []
    for line in Path(path).read_text(errors="replace").splitlines():
        if line.startswith("REPLAY_TEMPORAL_VISIBILITY_DIRECT pipelines_ready="):
            headers.append(fields(line))
        if line.startswith("REPLAY_TEMPORAL_VISIBILITY_DIRECT_PIXEL "):
            row = fields(line)
            key = (row["mode"], int(row["cohort"]), int(row["pixel"]))
            if key in pixels:
                raise ValueError(f"duplicate direct-visibility sample {key}")
            pixels[key] = row
        elif line.startswith("REPLAY_TEMPORAL_VISIBILITY_DIRECT_RESULT "):
            row = fields(line)
            if row["mode"] in results:
                raise ValueError(f"duplicate direct-visibility result {row['mode']}")
            results[row["mode"]] = row
        elif line.startswith("PASS ReplayTemporalDirectVisibilityValidation "):
            passes.append(fields(line))
    if len(headers) != 1:
        raise ValueError("direct pipeline coverage header missing or duplicated")
    cohorts = count(headers[0], "cohorts")
    if cohorts <= 0 or count(headers[0], "pixels_per_cohort") != len(PIXEL_IDS):
        raise ValueError("direct pipeline coverage header has invalid dimensions")
    expected = {(mode, cohort, pixel) for mode in MODES for cohort in range(cohorts) for pixel in PIXEL_IDS}
    if set(pixels) != expected:
        raise ValueError(f"direct per-pixel coverage {len(pixels)}/{len(expected)}")
    if set(results) != set(MODES) or len(passes) != 1:
        raise ValueError("direct result or PASS coverage incomplete")
    if count(passes[0], "rows") != len(expected) or passes[0].get("same_seed_prior_current_pairs") != "verified":
        raise ValueError("direct same-seed validation did not pass for every row")
    return pixels, results


def summarize(pixels, results):
    summaries = {}
    for mode in MODES:
        mode_cohorts = sorted({cohort for row_mode, cohort, _ in pixels if row_mode == mode})
        rows = [pixels[(mode, cohort, pixel)] for cohort in mode_cohorts for pixel in PIXEL_IDS]
        transitions = [0] * 9
        outcomes = [0] * 3
        sums = {key: [0.0] * 3 for key in ("nee_luma", "target", "history_weight")}
        direct_rays = direct_visible = 0
        for row in rows:
            previous = count(row, "prior_direct_code")
            current = count(row, "current_direct_code")
            rays, visible = count(row, "direct_rays"), count(row, "direct_visible")
            if previous > 2 or current > 2 or rays > 1 or visible > rays:
                raise ValueError(f"{mode}: invalid prior/current direct-segment outcome")
            expected_code = 0 if rays == 0 else (2 if visible else 1)
            if current != expected_code:
                raise ValueError(f"{mode}: direct state does not encode current traced/visible flags")
            if number(row, "previous_path_luma") < 0:
                raise ValueError(f"{mode}: prior path seed did not match current temporal candidate")
            target = number(row, "target")
            history_weight = number(row, "history_weight")
            source_weight = number(row, "source_weight")
            source_count = number(row, "source_count")
            retained = number(row, "retained")
            nee = number(row, "nee_luma")
            probability = number(row, "selection_probability")
            if target <= 0 or source_weight <= 0 or retained < 1 or source_count < retained:
                raise ValueError(f"{mode}: invalid direct-case reservoir source")
            if not math.isclose(history_weight, target * source_weight * retained, rel_tol=5e-6, abs_tol=5e-7):
                raise ValueError(f"{mode}: direct-case history weight identity failed")
            if not math.isclose(probability, history_weight / (history_weight + number(row, "fresh_weight")),
                                rel_tol=5e-6, abs_tol=5e-7):
                raise ValueError(f"{mode}: direct-case selection probability identity failed")
            transitions[previous * 3 + current] += 1
            outcomes[current] += 1
            sums["nee_luma"][current] += nee
            sums["target"][current] += target
            sums["history_weight"][current] += history_weight
            direct_rays += rays
            direct_visible += visible
        means = {key: [sums[key][idx] / outcomes[idx] if outcomes[idx] else 0.0 for idx in range(3)]
                 for key in sums}
        result = results[mode]
        expected_result = {
            "pixels": len(rows), "direct_rays": direct_rays, "direct_visible": direct_visible,
            "no_ray": outcomes[0], "blocked": outcomes[1], "visible": outcomes[2],
            **{f"transition_{idx // 3}{idx % 3}": transitions[idx] for idx in range(9)},
        }
        for key, expected_value in expected_result.items():
            if count(result, key) != expected_value:
                raise ValueError(f"{mode}: aggregate {key} disagrees with per-pixel direct events")
        for outcome, label in enumerate(("no_ray", "blocked", "visible")):
            for metric in sums:
                key = f"{label}_mean_{metric}"
                if metric == "nee_luma" and key not in result:
                    key = f"{label}_mean_nee"
                if not math.isclose(number(result, key), means[metric][outcome], rel_tol=5e-7, abs_tol=5e-8):
                    raise ValueError(f"{mode}: aggregate {key} disagrees with per-pixel samples")
        summaries[mode] = {"pixels": len(rows), "direct_rays": direct_rays,
                           "direct_visible": direct_visible, "transitions": transitions,
                           "outcomes": outcomes, "means": means}
        print(f"DIRECT_VISIBILITY mode={mode} pixels={len(rows)} direct_rays={direct_rays} "
              f"direct_visible={direct_visible} no_ray={outcomes[0]} blocked={outcomes[1]} visible={outcomes[2]} "
              f"prior_to_current={','.join(map(str, transitions))} "
              f"nee_means={','.join(f'{x:.9g}' for x in means['nee_luma'])} "
              f"history_weight_means={','.join(f'{x:.9g}' for x in means['history_weight'])}")
    return summaries


def chart(path, summaries):
    width, height = 1320, 610
    colors = ("#94a3b8", "#dc2626", "#15803d")
    labels = ("No ray", "Blocked", "Visible")
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
             '<rect width="100%" height="100%" fill="#f8fafc"/>',
             '<rect x="20" y="20" width="1280" height="570" rx="18" fill="white" stroke="#dbe3ee"/>',
             '<text x="55" y="72" font-family="Inter,Segoe UI,Arial,sans-serif" font-size="27" font-weight="700" fill="#0f172a">Same-seed direct-light visibility transitions</text>',
             '<text x="55" y="105" font-family="Inter,Segoe UI,Arial,sans-serif" font-size="15" fill="#475569">Controlled overhead emitter · moving receiver · opaque quad occluder · one-bounce NEE · SwiftShader Vulkan</text>']
    for index, mode in enumerate(MODES):
        y = 185 + index * 160
        data = summaries[mode]
        total = data["pixels"]
        parts.append(f'<text x="58" y="{y}" font-family="Inter,Segoe UI,Arial,sans-serif" font-size="17" font-weight="700" fill="#0f172a">{mode}</text>')
        x = 205
        for outcome, label, color in zip(data["outcomes"], labels, colors):
            bar = 470 * outcome / total
            parts.append(f'<rect x="{x:.2f}" y="{y-24}" width="{bar:.2f}" height="34" fill="{color}"/>')
            if bar > 50:
                parts.append(f'<text x="{x+bar/2:.2f}" y="{y-2}" text-anchor="middle" font-family="Arial" font-size="12" fill="white">{label}: {outcome}</text>')
            x += bar
        means = data["means"]
        parts.append(f'<text x="710" y="{y-5}" font-family="Arial" font-size="13" fill="#334155">Traced {data["direct_rays"]}; visible {data["direct_visible"]} · '
                      f'blocked/visible mean NEE={means["nee_luma"][1]:.5g}/{means["nee_luma"][2]:.5g}</text>')
        t = data["transitions"]
        parts.append(f'<text x="205" y="{y+39}" font-family="Arial" font-size="12" fill="#475569">Prior→current [no-ray, blocked, visible]:  {t[0]}/{t[1]}/{t[2]} · {t[3]}/{t[4]}/{t[5]} · {t[6]}/{t[7]}/{t[8]}</text>')
        parts.append(f'<text x="205" y="{y+64}" font-family="Arial" font-size="12" fill="#475569">Mean history weight by current state: {means["history_weight"][0]:.5g} / {means["history_weight"][1]:.5g} / {means["history_weight"][2]:.5g}</text>')
    parts.extend(['<text x="58" y="525" font-family="Arial" font-size="13" fill="#334155">Each prior state is the selected previous-frame path’s direct-segment result, persisted with its seed.</text>',
                  '<text x="58" y="550" font-family="Arial" font-size="12" fill="#64748b">Current outcome is directly traced for the same seed in the moving scene. This is a fixture-level attribution, not a production-render or estimator-bias proof.</text>',
                  '</svg>'])
    Path(path).write_text("\n".join(parts) + "\n", encoding="utf-8")


def run(log, svg):
    pixels, results = load(log)
    summaries = summarize(pixels, results)
    chart(svg, summaries)
    print(f"VISUAL {svg}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(f"Usage: {Path(sys.argv[0]).name} RUN_LOG CHART.svg")
    try:
        run(*sys.argv[1:])
    except (KeyError, ValueError, ZeroDivisionError) as error:
        print(f"FAIL ReplayTemporalDirectVisibilitySummary: {error}", file=sys.stderr)
        raise SystemExit(2)
