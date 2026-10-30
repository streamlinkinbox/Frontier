#!/usr/bin/env python3
"""Validate exact stored-emitter-endpoint visibility re-traces and cohort associations."""
from pathlib import Path
import importlib.util
import math
import re
import sys

HERE = Path(__file__).parent
SPEC = importlib.util.spec_from_file_location(
    "ReplayTemporalDirectVisibilityAblationSummary",
    HERE / "ReplayTemporalDirectVisibilityAblationSummary.py")
VISIBILITY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VISIBILITY)
SCENARIOS = VISIBILITY.SCENARIOS
MODES = VISIBILITY.MODES
PIXEL_IDS = VISIBILITY.PIXEL_IDS
STATES = VISIBILITY.STATES


def fields(line):
    return VISIBILITY.fields(line)


def finite(row, name):
    result = float(row[name])
    if not math.isfinite(result):
        raise ValueError(f"non-finite {name}={row[name]}")
    return result


def nonnegative_int(row, name):
    value = int(row[name])
    if value < 0:
        raise ValueError(f"negative {name}={value}")
    return value


def endpoint(row, name):
    pieces = row[name].split(",")
    if len(pieces) != 4:
        raise ValueError(f"{name} must contain xyz and facet id")
    xyz = tuple(float(x) for x in pieces[:3])
    facet = int(pieces[3])
    if not all(math.isfinite(x) for x in xyz) or not 0 <= facet <= 0xFFFFFFFF:
        raise ValueError(f"invalid endpoint record {name}={row[name]}")
    return xyz, facet


def same_endpoint(a, b):
    p, pf = a
    q, qf = b
    return pf != 0xFFFFFFFF and pf == qf and sum((x - y) ** 2 for x, y in zip(p, q)) <= 1e-10


def pearson(xs, ys):
    if len(xs) != len(ys) or len(xs) < 3:
        return None
    mx, my = sum(xs) / len(xs), sum(ys) / len(ys)
    dx, dy = [x - mx for x in xs], [y - my for y in ys]
    denom = math.sqrt(sum(x * x for x in dx) * sum(y * y for y in dy))
    return sum(x * y for x, y in zip(dx, dy)) / denom if denom > 0 else None


def parse(log_path):
    visibility = VISIBILITY.load(log_path)
    cohort_rows, pass_rows = {}, {}
    for line in Path(log_path).read_text(errors="replace").splitlines():
        if line.startswith("REPLAY_TEMPORAL_DIRECT_ENDPOINT_COHORT "):
            row = fields(line)
            key = (row["visibility"], row["mode"], nonnegative_int(row, "cohort"))
            if key in cohort_rows:
                raise ValueError(f"duplicate endpoint-pair cohort record {key}")
            cohort_rows[key] = row
        elif line.startswith("PASS ReplayTemporalDirectVisibilityAblationValidation "):
            row = fields(line)
            pass_rows[row["visibility"]] = row

    summaries = {}
    for scenario in SCENARIOS:
        if pass_rows.get(scenario, {}).get("matched_endpoint_retraces") != "verified":
            raise ValueError(f"{scenario}: paired endpoint runtime marker missing")
        base = visibility[scenario]
        cohorts = base["cohorts"]
        header = base["header"]
        reference = finite(header, "reference_luma")
        coarse = finite(header, "reference_coarse_luma")
        tolerance = .005 * max(1.0, abs(reference))
        if reference <= 0 or coarse <= 0 or abs(reference - coarse) > tolerance:
            raise ValueError(f"{scenario}: endpoint-pair CPU reference did not converge")
        summaries[scenario] = {}
        for mode in MODES:
            transitions = [0] * 9
            history_sums = [0.0] * 9
            endpoint_pairs = endpoint_matches = selected_endpoint_matches = 0
            estimate_values, cohort_means = [], []
            by_cohort = {}
            for cohort in range(cohorts):
                counts = [0] * 9
                weights = [0.0] * 9
                estimate_sum = 0.0
                matches = selected_matches = rays = 0
                for pixel in PIXEL_IDS:
                    row = base["pixels"][(scenario, mode, cohort, pixel)]
                    previous = nonnegative_int(row, "prior_direct_code")
                    paired_previous = int(row["paired_prior_code"])
                    paired_current = int(row["paired_current_code"])
                    pair_ray = nonnegative_int(row, "paired_ray")
                    transmittance = finite(row, "paired_transmittance")
                    history_weight = finite(row, "history_weight")
                    direct_estimate = finite(row, "direct_estimate_luma")
                    if (previous > 2 or paired_previous != previous or paired_current > 2 or
                            pair_ray > 1 or transmittance < 0 or history_weight <= 0 or direct_estimate < 0):
                        raise ValueError(f"{scenario}/{mode}: invalid paired endpoint state or sample")
                    if (pair_ray == 0 and paired_current != 0) or (pair_ray == 1 and paired_current not in (1, 2)):
                        raise ValueError(f"{scenario}/{mode}: paired-ray flag disagrees with visibility state")
                    if paired_current == 1 and transmittance != 0:
                        raise ValueError(f"{scenario}/{mode}: blocked endpoint has nonzero transmittance")
                    if paired_current == 2 and transmittance <= 0:
                        raise ValueError(f"{scenario}/{mode}: visible endpoint has zero transmittance")

                    prior_endpoint = endpoint(row, "prior_endpoint")
                    replay_endpoint = endpoint(row, "history_endpoint")
                    selected_endpoint = endpoint(row, "selected_endpoint")
                    computed_match = same_endpoint(prior_endpoint, replay_endpoint)
                    logged_match = nonnegative_int(row, "endpoint_same_replay")
                    if logged_match not in (0, 1) or bool(logged_match) != computed_match:
                        raise ValueError(f"{scenario}/{mode}: endpoint equality flag disagrees with coordinates")
                    selected_match = same_endpoint(prior_endpoint, selected_endpoint)
                    logged_selected_match = nonnegative_int(row, "endpoint_same_selected")
                    if logged_selected_match not in (0, 1) or bool(logged_selected_match) != selected_match:
                        raise ValueError(f"{scenario}/{mode}: selected endpoint equality flag disagrees with coordinates")
                    if pair_ray and prior_endpoint[1] == 0xFFFFFFFF:
                        raise ValueError(f"{scenario}/{mode}: retraced endpoint has no stored facet")
                    current_direct = nonnegative_int(row, "current_direct_code")
                    if computed_match and current_direct and current_direct != paired_current:
                        raise ValueError(f"{scenario}/{mode}: same-endpoint visibility disagrees with replay")
                    # Parse selected endpoint even if not equal: this confirms the actual
                    # current reservoir's endpoint was captured independently.
                    if selected_endpoint[1] != 0xFFFFFFFF and not all(map(math.isfinite, selected_endpoint[0])):
                        raise ValueError(f"{scenario}/{mode}: invalid selected endpoint")

                    cell = paired_previous * 3 + paired_current
                    counts[cell] += 1
                    weights[cell] += history_weight
                    transitions[cell] += 1
                    history_sums[cell] += history_weight
                    estimate_sum += direct_estimate
                    rays += pair_ray
                    matches += int(computed_match)
                    selected_matches += int(selected_match)
                    estimate_values.append(direct_estimate)
                mean = estimate_sum / len(PIXEL_IDS)
                cohort_means.append(mean)
                key = (scenario, mode, cohort)
                row = cohort_rows.get(key)
                if row is None:
                    raise ValueError(f"missing endpoint-pair cohort record {key}")
                if (nonnegative_int(row, "samples") != len(PIXEL_IDS) or
                        nonnegative_int(row, "endpoint_pairs") != rays or
                        nonnegative_int(row, "endpoint_matches") != matches or
                        nonnegative_int(row, "selected_endpoint_matches") != selected_matches or
                        not math.isclose(finite(row, "estimate_mean_luma"), mean, rel_tol=5e-7, abs_tol=5e-8)):
                    raise ValueError(f"{scenario}/{mode}/cohort {cohort}: cohort aggregate disagrees with pixel records")
                for cell in range(9):
                    p, c = divmod(cell, 3)
                    if nonnegative_int(row, f"pair_transition_{p}{c}") != counts[cell]:
                        raise ValueError(f"{scenario}/{mode}/cohort {cohort}: paired transition count disagrees")
                    expected_weight = weights[cell] / counts[cell] if counts[cell] else 0.0
                    if not math.isclose(finite(row, f"pair_history_mean_{p}{c}"), expected_weight,
                                        rel_tol=5e-7, abs_tol=5e-8):
                        raise ValueError(f"{scenario}/{mode}/cohort {cohort}: paired weight mean disagrees")
                by_cohort[cohort] = row
                endpoint_pairs += rays
                endpoint_matches += matches
                selected_endpoint_matches += selected_matches

            result = base["results"][(scenario, mode)]
            if (nonnegative_int(result, "paired_endpoint_pairs") != endpoint_pairs or
                    nonnegative_int(result, "endpoint_matches") != endpoint_matches or
                    nonnegative_int(result, "selected_endpoint_matches") != selected_endpoint_matches):
                raise ValueError(f"{scenario}/{mode}: paired endpoint result totals disagree")
            for cell in range(9):
                p, c = divmod(cell, 3)
                if nonnegative_int(result, f"paired_transition_{p}{c}") != transitions[cell]:
                    raise ValueError(f"{scenario}/{mode}: paired result transition count disagrees")
                expected_weight = history_sums[cell] / transitions[cell] if transitions[cell] else 0.0
                if not math.isclose(finite(result, f"paired_history_weight_mean_{p}{c}"), expected_weight,
                                    rel_tol=5e-7, abs_tol=5e-8):
                    raise ValueError(f"{scenario}/{mode}: paired result history mean disagrees")

            mean = sum(cohort_means) / len(cohort_means)
            batch_se = math.sqrt(sum((x - mean) ** 2 for x in cohort_means) /
                                  (len(cohort_means) - 1) / len(cohort_means))
            rmse = math.sqrt(sum((x - reference) ** 2 for x in estimate_values) / len(estimate_values))
            mean_gate = abs(mean - reference) <= 6.0 * batch_se + tolerance
            transition_features = {
                "visible_to_blocked_count": [nonnegative_int(by_cohort[c], "pair_transition_21")
                                             for c in range(cohorts)],
                "blocked_to_visible_count": [nonnegative_int(by_cohort[c], "pair_transition_12")
                                             for c in range(cohorts)],
                "visible_to_blocked_weight": [finite(by_cohort[c], "pair_history_mean_21")
                                               for c in range(cohorts)],
                "blocked_to_visible_weight": [finite(by_cohort[c], "pair_history_mean_12")
                                               for c in range(cohorts)],
                "selected_endpoint_match_count": [nonnegative_int(by_cohort[c], "selected_endpoint_matches")
                                                   for c in range(cohorts)],
            }
            correlations = {key: pearson(cohort_means, values) for key, values in transition_features.items()}
            summaries[scenario][mode] = {
                "transitions": transitions,
                "history_weight_means": [history_sums[i] / transitions[i] if transitions[i] else 0.0
                                          for i in range(9)],
                "endpoint_pairs": endpoint_pairs,
                "endpoint_matches": endpoint_matches,
                "endpoint_match_rate": endpoint_matches / endpoint_pairs if endpoint_pairs else 0.0,
                "selected_endpoint_matches": selected_endpoint_matches,
                "selected_endpoint_match_rate": selected_endpoint_matches / endpoint_pairs if endpoint_pairs else 0.0,
                "reference": reference,
                "coarse_reference": coarse,
                "mean": mean,
                "batch_se": batch_se,
                "rmse": rmse,
                "mean_gate": mean_gate,
                "cohort_means": cohort_means,
                "correlations": correlations,
            }
            trans = ",".join(map(str, transitions))
            corr = ",".join(f"{k}:{v:.5g}" if v is not None else f"{k}:undefined"
                             for k, v in correlations.items())
            print(f"ENDPOINT_PAIR scenario={scenario} mode={mode} transitions={trans} "
                  f"retrace_matches={endpoint_matches}/{endpoint_pairs} selected_endpoint_matches={selected_endpoint_matches}/{endpoint_pairs} "
                  f"estimate_mean={mean:.9g} "
                  f"reference={reference:.9g} batch_se={batch_se:.6g} rmse={rmse:.9g} "
                  f"mean_gate={'PASS' if mean_gate else 'FAIL'} cohort_r={corr}")

    expected_cohorts = {(s, m, c) for s in SCENARIOS for m in MODES
                        for c in range(visibility[s]["cohorts"])}
    if set(cohort_rows) != expected_cohorts:
        raise ValueError(f"endpoint-pair cohort coverage {len(cohort_rows)}/{len(expected_cohorts)}")
    return summaries


def chart(path, summaries):
    width, height = 1240, 760
    cell_w, cell_h = 138, 57
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
             '<rect width="100%" height="100%" fill="#f8fafc"/>',
             '<rect x="16" y="16" width="1208" height="728" rx="18" fill="white" stroke="#dbe3ee"/>',
             '<text x="46" y="58" font-family="Inter,Segoe UI,Arial,sans-serif" font-size="25" font-weight="700" fill="#0f172a">Same-endpoint visibility re-trace and temporal weights</text>',
             '<text x="46" y="86" font-family="Arial" font-size="13" fill="#475569">Prior endpoint is traced again from the current receiver pose; cells show count and mean retained history weight.</text>']
    x_positions = {"base_only": 56, "added_only": 650}
    y_positions = {"temporal1": 128, "temporal4": 425}
    for scenario in SCENARIOS:
        x0 = x_positions[scenario]
        for mode in MODES:
            data = summaries[scenario][mode]
            y0 = y_positions[mode]
            gate = "PASS" if data["mean_gate"] else "FAIL"
            r = data["correlations"]["visible_to_blocked_count"]
            r_text = "undefined" if r is None else f"{r:+.2f}"
            parts.append(f'<text x="{x0}" y="{y0}" font-family="Arial" font-size="17" font-weight="700" fill="#0f172a">{scenario} · {mode} · mean gate {gate}</text>')
            parts.append(f'<text x="{x0}" y="{y0+18}" font-family="Arial" font-size="10" fill="#64748b">retrace match {data["endpoint_match_rate"]:.1%}; selected match {data["selected_endpoint_match_rate"]:.1%}; mean {data["mean"]:.4g}/{data["reference"]:.4g}; r(V→B,mean) {r_text}</text>')
            grid_x, grid_y = x0 + 105, y0 + 35
            for col, label in enumerate(STATES):
                parts.append(f'<text x="{grid_x+col*cell_w+cell_w/2}" y="{grid_y-5}" text-anchor="middle" font-family="Arial" font-size="11" fill="#334155">{label}</text>')
            for row, label in enumerate(STATES):
                parts.append(f'<text x="{x0+98}" y="{grid_y+row*cell_h+cell_h/2+4}" text-anchor="end" font-family="Arial" font-size="11" fill="#334155">{label}</text>')
                for col in range(3):
                    cell = row * 3 + col
                    n = data["transitions"][cell]
                    weight = data["history_weight_means"][cell]
                    color = "#ecfdf5" if col == 2 else ("#fff7ed" if col == 1 else "#f1f5f9")
                    cx, cy = grid_x + col * cell_w, grid_y + row * cell_h
                    parts.append(f'<rect x="{cx}" y="{cy}" width="{cell_w-4}" height="{cell_h-4}" rx="5" fill="{color}" stroke="#cbd5e1"/>')
                    parts.append(f'<text x="{cx+8}" y="{cy+21}" font-family="Arial" font-size="12" fill="#0f172a">n = {n}</text>')
                    parts.append(f'<text x="{cx+8}" y="{cy+39}" font-family="Arial" font-size="10" fill="#475569">W̄ {weight:.4g}</text>')
    parts.extend(['<text x="46" y="716" font-family="Arial" font-size="12" fill="#475569">Host cohort seeds matched; prior world-space endpoint explicitly re-traced. Cohort correlations are descriptive, not causal.</text>',
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
        print(f"FAIL ReplayTemporalDirectEndpointPairSummary: {error}", file=sys.stderr)
        raise SystemExit(2)
