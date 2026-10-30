#!/usr/bin/env python3
"""Validate same-seed direct-visibility transitions and temporal weights per blocker ablation."""
from pathlib import Path
import math
import re
import sys

SCENARIOS = ("base_only", "added_only")
MODES = ("temporal1", "temporal4")
GRID = 32
PIXEL_IDS = tuple(p for p in range(GRID * GRID) if (p % GRID + p // GRID) % 2 == 1)
STATES = ("No ray", "Blocked", "Visible")


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
    headers, pixels, results, passes = {}, {}, {}, {}
    for line in Path(path).read_text(errors="replace").splitlines():
        if line.startswith("REPLAY_TEMPORAL_VISIBILITY_DIRECT_ABLATION pipelines_ready="):
            row = fields(line)
            scenario = row["visibility"]
            if scenario in headers:
                raise ValueError(f"duplicate visibility-ablation header {scenario}")
            headers[scenario] = row
        elif line.startswith("REPLAY_TEMPORAL_VISIBILITY_DIRECT_ABLATION_PIXEL "):
            row = fields(line)
            key = (row["visibility"], row["mode"], count(row, "cohort"), count(row, "pixel"))
            if key in pixels:
                raise ValueError(f"duplicate visibility-ablation sample {key}")
            pixels[key] = row
        elif line.startswith("REPLAY_TEMPORAL_VISIBILITY_DIRECT_ABLATION_RESULT "):
            row = fields(line)
            key = (row["visibility"], row["mode"])
            if key in results:
                raise ValueError(f"duplicate visibility-ablation result {key}")
            results[key] = row
        elif line.startswith("PASS ReplayTemporalDirectVisibilityAblationValidation "):
            row = fields(line)
            scenario = row["visibility"]
            if scenario in passes:
                raise ValueError(f"duplicate visibility-ablation pass marker {scenario}")
            passes[scenario] = row

    if set(headers) != set(SCENARIOS) or set(passes) != set(SCENARIOS):
        raise ValueError("visibility-ablation scenarios or PASS coverage incomplete")
    if set(results) != {(scenario, mode) for scenario in SCENARIOS for mode in MODES}:
        raise ValueError("visibility-ablation scenario/mode result coverage incomplete")

    parsed = {}
    for scenario in SCENARIOS:
        header = headers[scenario]
        cohorts = count(header, "cohorts")
        pixels_per_cohort = count(header, "pixels_per_cohort")
        if cohorts < 2 or pixels_per_cohort != len(PIXEL_IDS):
            raise ValueError(f"{scenario}: invalid visibility-ablation dimensions")
        expected = {(scenario, mode, cohort, pixel) for mode in MODES
                    for cohort in range(cohorts) for pixel in PIXEL_IDS}
        actual = {key for key in pixels if key[0] == scenario}
        if actual != expected:
            raise ValueError(f"{scenario}: per-pixel coverage {len(actual)}/{len(expected)}")
        pass_row = passes[scenario]
        expected_rows = cohorts * len(MODES) * pixels_per_cohort
        if (count(pass_row, "rows") != expected_rows or
                pass_row.get("same_seed_prior_current_pairs") != "verified" or
                pass_row.get("matched_frame_seed_sequences") != "verified" or
                pass_row.get("estimator_math_changed") != "0"):
            raise ValueError(f"{scenario}: visibility-ablation PASS marker invalid")
        parsed[scenario] = {"header": header, "cohorts": cohorts, "pixels": pixels,
                            "results": results, "pass": pass_row}

    # Verify that every pixel/mode in a cohort used the same host-generated four-frame
    # sequence, and that each sequence is identical across blocker arms.
    cohort_sequences = {}
    for scenario in SCENARIOS:
        for mode in MODES:
            for cohort in range(parsed[scenario]["cohorts"]):
                for pixel in PIXEL_IDS:
                    row = pixels[(scenario, mode, cohort, pixel)]
                    sequence = row.get("seed_sequence", "").split(":")
                    if (len(sequence) != 4 or any(re.fullmatch(r"[0-9a-fA-F]{8}", seed) is None
                                                  for seed in sequence) or
                            row.get("seed", "").lower() != sequence[-1].lower()):
                        raise ValueError(f"invalid four-frame host seed sequence for {scenario}/{mode}/cohort {cohort}")
                    key = (scenario, cohort)
                    if key in cohort_sequences and cohort_sequences[key] != sequence:
                        raise ValueError(f"inconsistent host seed sequence in {scenario}/cohort {cohort}")
                    cohort_sequences[key] = sequence
    for cohort in range(parsed["base_only"]["cohorts"]):
        if cohort_sequences[("base_only", cohort)] != cohort_sequences[("added_only", cohort)]:
            raise ValueError(f"matched host seed sequence mismatch for cohort {cohort}")
    return parsed


def summarize(parsed):
    summaries = {}
    for scenario in SCENARIOS:
        data = parsed[scenario]
        cohorts, pixels_per_cohort = data["cohorts"], len(PIXEL_IDS)
        summaries[scenario] = {}
        for mode in MODES:
            transitions = [0] * 9
            history_sums = [0.0] * 9
            nee_sums = [0.0] * 9
            outcomes = [0] * 3
            direct_rays = direct_visible = 0
            for cohort in range(cohorts):
                for pixel in PIXEL_IDS:
                    row = data["pixels"][(scenario, mode, cohort, pixel)]
                    previous, current = count(row, "prior_direct_code"), count(row, "current_direct_code")
                    rays, visible = count(row, "direct_rays"), count(row, "direct_visible")
                    if previous > 2 or current > 2 or rays > 1 or visible > rays:
                        raise ValueError(f"{scenario}/{mode}: invalid direct-state flags")
                    if current != (0 if rays == 0 else (2 if visible else 1)):
                        raise ValueError(f"{scenario}/{mode}: current state disagrees with traced visibility")
                    target = number(row, "target")
                    source_weight = number(row, "source_weight")
                    source_count = number(row, "source_count")
                    retained = number(row, "retained")
                    history_weight = number(row, "history_weight")
                    fresh_weight = number(row, "fresh_weight")
                    probability = number(row, "selection_probability")
                    nee = number(row, "nee_luma")
                    if (target <= 0 or source_weight <= 0 or retained != 1 or source_count < retained or
                            history_weight <= 0 or fresh_weight <= 0 or not 0 < probability < 1 or nee < 0):
                        raise ValueError(f"{scenario}/{mode}: invalid reservoir or NEE value")
                    if current != 2 and nee != 0:
                        raise ValueError(f"{scenario}/{mode}: blocked/no-ray direct segment has nonzero NEE")
                    if not math.isclose(history_weight, target * source_weight * retained,
                                        rel_tol=5e-6, abs_tol=5e-7):
                        raise ValueError(f"{scenario}/{mode}: history-weight identity failed")
                    if not math.isclose(probability, history_weight / (history_weight + fresh_weight),
                                        rel_tol=5e-6, abs_tol=5e-7):
                        raise ValueError(f"{scenario}/{mode}: selection-probability identity failed")
                    transition = previous * 3 + current
                    transitions[transition] += 1
                    history_sums[transition] += history_weight
                    nee_sums[transition] += nee
                    outcomes[current] += 1
                    direct_rays += rays
                    direct_visible += visible

            result = data["results"][(scenario, mode)]
            if (count(result, "rows") != cohorts * pixels_per_cohort or
                    count(result, "direct_rays") != direct_rays or
                    count(result, "direct_visible") != direct_visible or
                    count(result, "blocked") != outcomes[1] or count(result, "visible") != outcomes[2] or
                    count(result, "no_ray") != outcomes[0]):
                raise ValueError(f"{scenario}/{mode}: outcome aggregate disagrees with per-pixel records")
            for transition in range(9):
                previous, current = divmod(transition, 3)
                expected_count = transitions[transition]
                expected_history_mean = history_sums[transition] / expected_count if expected_count else 0.0
                expected_nee_mean = nee_sums[transition] / expected_count if expected_count else 0.0
                if count(result, f"transition_{previous}{current}") != expected_count:
                    raise ValueError(f"{scenario}/{mode}: transition aggregate disagrees with records")
                if not math.isclose(number(result, f"history_weight_mean_{previous}{current}"),
                                    expected_history_mean, rel_tol=5e-7, abs_tol=5e-8):
                    raise ValueError(f"{scenario}/{mode}: transition history-weight mean disagrees")
                if not math.isclose(number(result, f"nee_mean_{previous}{current}"),
                                    expected_nee_mean, rel_tol=5e-7, abs_tol=5e-8):
                    raise ValueError(f"{scenario}/{mode}: transition NEE mean disagrees")

            if scenario == "base_only" and (outcomes[1] != 0 or transitions[5] or transitions[7]):
                raise ValueError("base-only control unexpectedly showed a blocked/visible transition")
            if scenario == "added_only" and (outcomes[1] == 0 or transitions[5] + transitions[7] == 0):
                raise ValueError("added-only blocker did not produce blocked samples and visibility transitions")
            summaries[scenario][mode] = {
                "transitions": transitions,
                "history_weight_means": [history_sums[i] / transitions[i] if transitions[i] else 0.0
                                          for i in range(9)],
                "nee_means": [nee_sums[i] / transitions[i] if transitions[i] else 0.0 for i in range(9)],
                "outcomes": outcomes,
                "direct_rays": direct_rays,
                "direct_visible": direct_visible,
            }
            state_transitions = ",".join(str(x) for x in transitions)
            history_means = ",".join(f"{x:.9g}" for x in summaries[scenario][mode]["history_weight_means"])
            print(f"VISIBILITY_ABLATION scenario={scenario} mode={mode} outcomes={outcomes} "
                  f"transitions={state_transitions} history_weight_means={history_means}")
    return summaries


def chart(path, summaries):
    width, height = 1240, 760
    cell_w, cell_h = 138, 57
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
             '<rect width="100%" height="100%" fill="#f8fafc"/>',
             '<rect x="16" y="16" width="1208" height="728" rx="18" fill="white" stroke="#dbe3ee"/>',
             '<text x="46" y="58" font-family="Inter,Segoe UI,Arial,sans-serif" font-size="25" font-weight="700" fill="#0f172a">Visibility transitions and retained-history weights</text>',
             '<text x="46" y="86" font-family="Arial" font-size="13" fill="#475569">Cells show count and mean history weight W̄; rows are prior-frame state, columns are current direct visibility.</text>']
    labels = STATES
    scenario_x = {"base_only": 56, "added_only": 650}
    mode_y = {"temporal1": 128, "temporal4": 425}
    for scenario in SCENARIOS:
        x0 = scenario_x[scenario]
        for mode in MODES:
            y0 = mode_y[mode]
            data = summaries[scenario][mode]
            parts.append(f'<text x="{x0}" y="{y0}" font-family="Arial" font-size="18" font-weight="700" fill="#0f172a">{scenario} · {mode}</text>')
            parts.append(f'<text x="{x0}" y="{y0+19}" font-family="Arial" font-size="11" fill="#64748b">direct rays {data["direct_rays"]}; visible {data["direct_visible"]}</text>')
            grid_x, grid_y = x0 + 105, y0 + 35
            for col, label in enumerate(labels):
                parts.append(f'<text x="{grid_x+col*cell_w+cell_w/2}" y="{grid_y-5}" text-anchor="middle" font-family="Arial" font-size="11" fill="#334155">{label}</text>')
            for row, label in enumerate(labels):
                parts.append(f'<text x="{x0+98}" y="{grid_y+row*cell_h+cell_h/2+4}" text-anchor="end" font-family="Arial" font-size="11" fill="#334155">{label}</text>')
                for col in range(3):
                    transition = row * 3 + col
                    n = data["transitions"][transition]
                    weight = data["history_weight_means"][transition]
                    nee = data["nee_means"][transition]
                    fill = "#ecfdf5" if col == 2 else ("#fff7ed" if col == 1 else "#f1f5f9")
                    cx, cy = grid_x + col * cell_w, grid_y + row * cell_h
                    parts.append(f'<rect x="{cx}" y="{cy}" width="{cell_w-4}" height="{cell_h-4}" rx="5" fill="{fill}" stroke="#cbd5e1"/>')
                    parts.append(f'<text x="{cx+8}" y="{cy+21}" font-family="Arial" font-size="12" fill="#0f172a">n = {n}</text>')
                    parts.append(f'<text x="{cx+8}" y="{cy+39}" font-family="Arial" font-size="10" fill="#475569">W̄ {weight:.4g} · NEE {nee:.4g}</text>')
    parts.extend(['<text x="46" y="716" font-family="Arial" font-size="12" fill="#475569">Matched host seed sequences and pixel identities; emitter sample endpoints are not explicitly paired. Diagnostic only, not causal proof.</text>',
                  '</svg>'])
    Path(path).write_text("\n".join(parts) + "\n", encoding="utf-8")


def run(log, svg):
    parsed = load(log)
    summaries = summarize(parsed)
    chart(svg, summaries)
    print(f"VISUAL {svg}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(f"Usage: {Path(sys.argv[0]).name} RUN_LOG CHART.svg")
    try:
        run(*sys.argv[1:])
    except (KeyError, ValueError, ZeroDivisionError) as error:
        print(f"FAIL ReplayTemporalDirectVisibilityAblationSummary: {error}", file=sys.stderr)
        raise SystemExit(2)
