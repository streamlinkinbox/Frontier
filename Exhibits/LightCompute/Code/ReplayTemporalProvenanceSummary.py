#!/usr/bin/env python3
"""Audit matched temporal-reservoir provenance readbacks against both RNG holdouts."""
from pathlib import Path
import math
import re
import sys

MODES = ("temporal1", "temporal4")
ARMS = ("baseline", "path_hash", "selection_hash")
COHORTS = 128
PIXELS = 512

def fields(line):
    return dict(re.findall(r"([A-Za-z_]+)=([^\s]+)", line))

def val(row, key):
    x = float(row[key])
    if not math.isfinite(x):
        raise ValueError(f"non-finite {key}={row[key]}")
    return x

def integer(row, key):
    x = int(row[key])
    if x < 0:
        raise ValueError(f"negative {key}={x}")
    return x

def moments(xs):
    if not xs:
        raise ValueError("empty sample")
    mean = sum(xs) / len(xs)
    if len(xs) < 2:
        return mean, 0.0
    variance = sum((x - mean) ** 2 for x in xs) / (len(xs) - 1)
    return mean, math.sqrt(max(0.0, variance) / len(xs))

def load_provenance(path):
    rows, results, exits = {}, {}, {}
    for line in Path(path).read_text(errors="replace").splitlines():
        if line.startswith("REPLAY_TEMPORAL_PROVENANCE_BATCH "):
            row = fields(line)
            key = (row["arm"], row["mode"], row["floor"], int(row["batch"]))
            if key in rows:
                raise ValueError(f"duplicate provenance row {key}")
            rows[key] = row
        elif line.startswith("REPLAY_TEMPORAL_PROVENANCE_RESULT "):
            row = fields(line)
            key = (row["arm"], row["mode"], row["floor"])
            if key in results:
                raise ValueError(f"duplicate provenance result {key}")
            results[key] = row
        elif line.startswith("REPLAY_TEMPORAL_PROVENANCE_ARM_EXIT "):
            row = fields(line)
            exits[row["arm"]] = int(row["status"])
    expected = {(arm, mode, "lower", n) for arm in ARMS for mode in MODES for n in range(COHORTS)}
    expected_results = {(arm, mode, "lower") for arm in ARMS for mode in MODES}
    if set(rows) != expected:
        raise ValueError(f"provenance coverage {len(rows)}/{len(expected)} rows")
    if set(results) != expected_results:
        raise ValueError(f"provenance summary coverage {len(results)}/{len(expected_results)}")
    if exits != {arm: 0 for arm in ARMS}:
        raise ValueError(f"arm exit status is not all-zero: {exits}")
    return rows, results

def load_prior_batches(path, prefix):
    out = {}
    for line in Path(path).read_text(errors="replace").splitlines():
        if line.startswith(prefix + "_BATCH "):
            row = fields(line)
            if row.get("arm") in ("baseline", "decorrelated") and row.get("floor") == "lower" and row.get("mode") in MODES:
                key = (row["arm"], row["mode"], int(row["batch"]))
                if key in out:
                    raise ValueError(f"duplicate prior row {prefix} {key}")
                out[key] = row
    expected = {(arm, mode, n) for arm in ("baseline", "decorrelated")
                for mode in MODES for n in range(COHORTS)}
    if set(out) != expected:
        raise ValueError(f"prior {prefix} coverage {len(out)}/{len(expected)}")
    return out

def arm_summary(rows, results, arm, mode):
    key = (arm, mode, "lower")
    result = results[key]
    r = [rows[(arm, mode, "lower", n)] for n in range(COHORTS)]
    accepted = sum(integer(x, "accepted") for x in r)
    selected = sum(integer(x, "selected") for x in r)
    expected_selected = sum(val(x, "expected_selected") for x in r)
    fresh = sum(val(x, "fresh_weight") for x in r)
    history = sum(val(x, "history_weight") for x in r)
    target = sum(val(x, "history_target") for x in r)
    retained = sum(val(x, "retained") for x in r)
    pixels = sum(integer(x, "pixels") for x in r)
    if pixels != COHORTS * PIXELS:
        raise ValueError(f"{arm}/{mode}: pixel coverage {pixels}")
    if selected > accepted or accepted > pixels:
        raise ValueError(f"{arm}/{mode}: impossible accepted/selected counts")
    if integer(result, "accepted") != accepted or integer(result, "selected") != selected:
        raise ValueError(f"{arm}/{mode}: result counters disagree with cohort sums")
    if not math.isclose(val(result, "expected_selected_given_accepted"),
                        expected_selected / accepted if accepted else 0.0, rel_tol=2e-7, abs_tol=1e-9):
        raise ValueError(f"{arm}/{mode}: expected selection probability summary mismatch")
    for field, total in (("history_weight", history), ("fresh_weight", fresh),
                         ("history_target", target), ("retained", retained)):
        if not math.isclose(val(result, field), total, rel_tol=2e-7, abs_tol=1e-8):
            raise ValueError(f"{arm}/{mode}: {field} result disagrees with cohort sums")
    means = [val(x, "mean") for x in r]
    mse = [val(x, "mse") for x in r]
    mean, se = moments(means)
    mean_mse = sum(mse) / len(mse)
    for field, recomputed in (("mean", mean), ("mean_se", se), ("mse", mean_mse),
                              ("rmse", math.sqrt(mean_mse))):
        if not math.isclose(val(result, field), recomputed, rel_tol=2e-7, abs_tol=1e-9):
            raise ValueError(f"{arm}/{mode}: result {field} disagrees with cohort recomputation")
    by_cohort = []
    for x in r:
        a = integer(x, "accepted")
        by_cohort.append({
            "accepted_rate": a / PIXELS,
            "selected_rate": integer(x, "selected") / a if a else 0.0,
            "expected_rate": val(x, "expected_selected") / a if a else 0.0,
            "fresh_per_pixel": val(x, "fresh_weight") / PIXELS,
            "history_per_pixel": val(x, "history_weight") / PIXELS,
            "weight_share": val(x, "weight_share"),
            "retained_per_pixel": val(x, "retained") / PIXELS,
        })
    return {
        "mean": mean, "mean_se": se, "rmse": math.sqrt(mean_mse), "mse": mean_mse,
        "accepted_rate": accepted / pixels, "selected_given_accepted": selected / accepted if accepted else 0,
        "expected_given_accepted": expected_selected / accepted if accepted else 0,
        "selection_z_logged": val(result, "selection_z_approx"),
        "fresh_per_pixel": fresh / pixels, "history_per_pixel": history / pixels,
        "history_weight_share": history / (fresh + history) if fresh + history else 0,
        "mean_history_target_per_pixel": target / pixels,
        "retained_per_pixel": retained / pixels,
        "cohorts": by_cohort,
    }

def paired_summary(base, candidate, field):
    deltas = [b[field] - a[field] for a, b in zip(base["cohorts"], candidate["cohorts"])]
    mean, se = moments(deltas)
    return mean, se

def run(raw_path, selection_path, path_path):
    rows, results = load_provenance(raw_path)
    prior_selection = load_prior_batches(selection_path, "REPLAY_SELECTION_STREAM")
    prior_path = load_prior_batches(path_path, "REPLAY_PATH_STREAM")
    count = len(MODES) * COHORTS
    path_changed = selection_fixed = selection_changed = path_fixed = baseline_images_match_path = baseline_images_match_selection = 0
    baseline_image_hash_matches_by_mode = {mode: 0 for mode in MODES}
    prior_numeric_matches = {"baseline_path": 0, "baseline_selection": 0, "path_candidate": 0, "selection_candidate": 0}
    prior_max_mean_delta = {key: 0.0 for key in prior_numeric_matches}
    prior_max_mse_delta = {key: 0.0 for key in prior_numeric_matches}
    for mode in MODES:
        for cohort in range(COHORTS):
            base = rows[("baseline", mode, "lower", cohort)]
            path = rows[("path_hash", mode, "lower", cohort)]
            selection = rows[("selection_hash", mode, "lower", cohort)]
            path_changed += base["path_hash"] != path["path_hash"]
            selection_fixed += base["selection_hash"] == path["selection_hash"]
            selection_changed += base["selection_hash"] != selection["selection_hash"]
            path_fixed += base["path_hash"] == selection["path_hash"]
            prior_path_base = prior_path[("baseline", mode, cohort)]
            prior_selection_base = prior_selection[("baseline", mode, cohort)]
            prior_path_candidate = prior_path[("decorrelated", mode, cohort)]
            prior_selection_candidate = prior_selection[("decorrelated", mode, cohort)]
            match_path_hash = base["hash"] == prior_path_base["hash"]
            match_selection_hash = base["hash"] == prior_selection_base["hash"]
            baseline_images_match_path += match_path_hash
            baseline_images_match_selection += match_selection_hash
            baseline_image_hash_matches_by_mode[mode] += match_path_hash and match_selection_hash
            pairs = (("baseline_path", base, prior_path_base),
                     ("baseline_selection", base, prior_selection_base),
                     ("path_candidate", path, prior_path_candidate),
                     ("selection_candidate", selection, prior_selection_candidate))
            for name, current, old in pairs:
                dm = abs(val(current, "mean") - val(old, "mean"))
                dq = abs(val(current, "mse") - val(old, "mse"))
                prior_max_mean_delta[name] = max(prior_max_mean_delta[name], dm)
                prior_max_mse_delta[name] = max(prior_max_mse_delta[name], dq)
                same = (math.isclose(val(current, "mean"), val(old, "mean"), rel_tol=1e-8, abs_tol=1e-9) and
                        math.isclose(val(current, "mse"), val(old, "mse"), rel_tol=1e-8, abs_tol=1e-9))
                if same:
                    prior_numeric_matches[name] += 1
    if (path_changed, selection_fixed, selection_changed, path_fixed) != (count,) * 4:
        raise ValueError("RNG stream isolation hashes did not match expected per-arm controls")
    print(f"CONTROL provenance_pairs={count} path_arm_path_hash_changed={path_changed}/{count} "
          f"path_arm_selection_hash_fixed={selection_fixed}/{count} "
          f"selection_arm_selection_hash_changed={selection_changed}/{count} "
          f"selection_arm_path_hash_fixed={path_fixed}/{count}")
    print(f"CONTROL prior_baseline_image_hash_matches path_study={baseline_images_match_path}/{count} "
          f"selection_study={baseline_images_match_selection}/{count} "
          f"Slang 2026.14.1 was used for both run generations; cause of any mismatch remains unknown")
    for mode in MODES:
        print(f"CONTROL prior_baseline_image_hash_mode mode={mode} "
              f"matches_both_prior_studies={baseline_image_hash_matches_by_mode[mode]}/{COHORTS} "
              f"mismatches={COHORTS-baseline_image_hash_matches_by_mode[mode]}/{COHORTS}")
    for name in prior_numeric_matches:
        print(f"CONTROL prior_numeric_match {name}={prior_numeric_matches[name]}/{count} "
              f"max_abs_mean_delta={prior_max_mean_delta[name]:.9g} "
              f"max_abs_mse_delta={prior_max_mse_delta[name]:.9g}")
    summaries = {}
    for mode in MODES:
        for arm in ARMS:
            s = arm_summary(rows, results, arm, mode)
            summaries[(arm, mode)] = s
            print(f"CELL arm={arm:14s} mode={mode:9s} floor=lower mean={s['mean']:.9f}±{s['mean_se']:.9f} "
                  f"RMSE={s['rmse']:.6f} "
                  f"history_accept_rate={s['accepted_rate']:.6f} "
                  f"history_selected|accepted={s['selected_given_accepted']:.6f} "
                  f"expected_selection|accepted={s['expected_given_accepted']:.6f} "
                  f"selection_z_approx={s['selection_z_logged']:.4f} "
                  f"fresh_weight/pixel={s['fresh_per_pixel']:.8g} history_weight/pixel={s['history_per_pixel']:.8g} "
                  f"history_weight_share={s['history_weight_share']:.6f} "
                  f"retained/pixel={s['retained_per_pixel']:.6f} target/pixel={s['mean_history_target_per_pixel']:.8g}")
        base = summaries[("baseline", mode)]
        for arm in ("path_hash", "selection_hash"):
            candidate = summaries[(arm, mode)]
            values = ["accepted_rate", "selected_rate", "expected_rate", "fresh_per_pixel",
                      "history_per_pixel", "weight_share", "retained_per_pixel"]
            parts = []
            for field in values:
                delta, se = paired_summary(base, candidate, field)
                parts.append(f"delta_{field}={delta:+.7g}±{se:.4g}")
            print(f"PAIRED arm={arm:14s} mode={mode:9s} floor=lower " + " ".join(parts))
    print("NOTICE Seed/choice slabs are test-only diagnostics. The selection_z field uses an iid Bernoulli approximation over pixels; it is descriptive and does not prove independence or an estimator defect.")
    print("NOTICE Cohort aggregates localize reservoir-selection behavior; they do not identify individual ray visibility contributions, prove support completeness, or establish asymptotic bias.")
    return summaries

def write_chart(output_path, summaries):
    width, height = 1400, 950
    colors = {"baseline": "#b7791f", "path_hash": "#2563eb", "selection_hash": "#0f8b6d"}
    labels = {"baseline": "Baseline", "path_hash": "Path RNG", "selection_hash": "Selection RNG"}
    def text(x, y, value, size=15, color="#334155", weight="400", anchor="start"):
        return f'<text x="{x}" y="{y}" font-family="Inter,Segoe UI,Arial,sans-serif" font-size="{size}" fill="{color}" font-weight="{weight}" text-anchor="{anchor}">{value}</text>'
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
             '<rect width="100%" height="100%" fill="#f8fafc"/>',
             '<rect x="22" y="20" width="1356" height="820" rx="20" fill="white" stroke="#dbe3ee"/>',
             text(58, 72, "Temporal reservoir provenance · moving visibility", 28, "#0f172a", "700"),
             text(58, 105, "128 paired cohorts · lower firefly floor · SwiftShader software Vulkan", 16, "#475569"),
             text(68, 152, "A  ·  History share of the fresh + temporal reservoir weight", 17, "#0f172a", "700"),
             text(750, 152, "B  ·  Observed vs expected temporal-selection residual", 17, "#0f172a", "700")]
    # Panel A: total temporal history weight share per cell and arm.
    x0, x1, y0, y1 = 86, 665, 205, 510
    ymax = .6
    for tick in (0, .15, .30, .45, .60):
        yy = y1 - tick / ymax * (y1 - y0)
        parts.append(f'<line x1="{x0}" y1="{yy:.1f}" x2="{x1}" y2="{yy:.1f}" stroke="#e2e8f0"/>')
        parts.append(text(x0 - 10, yy + 5, f"{tick:.0%}", 12, "#64748b", anchor="end"))
    parts.append(f'<line x1="{x0}" y1="{y0}" x2="{x0}" y2="{y1}" stroke="#94a3b8"/><line x1="{x0}" y1="{y1}" x2="{x1}" y2="{y1}" stroke="#94a3b8"/>')
    for mode_idx, mode in enumerate(MODES):
        center = 250 + mode_idx * 300
        for offset, arm in zip((-48, 0, 48), ARMS):
            share = summaries[(arm, mode)]["history_weight_share"]
            height_px = share / ymax * (y1 - y0)
            x = center + offset
            parts.append(f'<rect x="{x-17}" y="{y1-height_px:.1f}" width="34" height="{height_px:.1f}" rx="5" fill="{colors[arm]}"/>')
            parts.append(text(x, y1-height_px-8, f"{share:.1%}", 11, colors[arm], "600", "middle"))
        parts.append(text(center, y1+28, "Temporal 1" if mode == "temporal1" else "Temporal 4", 14, "#334155", "600", "middle"))
    # Panel B: z-like residual of observed roulette decisions vs the stored per-pixel probabilities.
    bx0, bx1, by0, by1 = 785, 1320, 205, 510
    zmin, zmax = -2.5, 2.5
    for tick in (-2, -1, 0, 1, 2):
        yy = by1 - (tick-zmin)/(zmax-zmin)*(by1-by0)
        dash = "5 4" if tick == 0 else "none"
        parts.append(f'<line x1="{bx0}" y1="{yy:.1f}" x2="{bx1}" y2="{yy:.1f}" stroke="#e2e8f0" stroke-dasharray="{dash}"/>')
        parts.append(text(bx0-10, yy+5, f"{tick:+d}", 12, "#64748b", anchor="end"))
    # Approximate ±1.96 reference band; highlighted only as a guide, not an iid claim.
    for band in (-1.96, 1.96):
        yy = by1 - (band-zmin)/(zmax-zmin)*(by1-by0)
        parts.append(f'<line x1="{bx0}" y1="{yy:.1f}" x2="{bx1}" y2="{yy:.1f}" stroke="#fca5a5" stroke-dasharray="4 5"/>')
    parts.append(f'<line x1="{bx0}" y1="{by0}" x2="{bx0}" y2="{by1}" stroke="#94a3b8"/><line x1="{bx0}" y1="{by1}" x2="{bx1}" y2="{by1}" stroke="#94a3b8"/>')
    for mode_idx, mode in enumerate(MODES):
        center = 925 + mode_idx * 255
        for offset, arm in zip((-38, 0, 38), ARMS):
            z = summaries[(arm, mode)]["selection_z_logged"]
            yy = by1 - (max(zmin, min(zmax, z))-zmin)/(zmax-zmin)*(by1-by0)
            x = center + offset
            parts.append(f'<circle cx="{x}" cy="{yy:.1f}" r="7" fill="{colors[arm]}" stroke="white" stroke-width="2"/>')
            parts.append(text(x, by1+28, "T1" if mode == "temporal1" else "T4", 14, "#334155", "600", "middle"))
    # Legend and scope callout.
    for i, arm in enumerate(ARMS):
        x = 88 + i*190
        parts.append(f'<rect x="{x}" y="565" width="15" height="15" rx="3" fill="{colors[arm]}"/>')
        parts.append(text(x+23, 578, labels[arm], 13, "#334155"))
    parts.extend(['<rect x="58" y="610" width="1284" height="300" rx="15" fill="#f1f5f9" stroke="#cbd5e1"/>',
                  text(82, 647, "C  ·  Estimator outcome comparison (mean ± cohort SE; then cohort RMSE)", 18, "#0f172a", "700")])
    for mode, label, y_mean, y_rmse in (("temporal1", "Temporal 1", 682, 710),
                                        ("temporal4", "Temporal 4", 748, 776)):
        summaries_line = "  |  ".join(
            f"{labels[arm]} {summaries[(arm, mode)]['mean']:.6f}±{summaries[(arm, mode)]['mean_se']:.6f}"
            for arm in ARMS)
        rmse_line = "  |  ".join(
            f"{labels[arm]} {summaries[(arm, mode)]['rmse']:.5f}"
            for arm in ARMS)
        parts.append(text(82, y_mean, f"{label} mean ± SE:  {summaries_line}", 13, "#334155"))
        parts.append(text(82, y_rmse, f"{label} RMSE:  {rmse_line}", 13, "#334155"))
    parts.extend([text(82, 832, "Controls: 262,144 path-seed + 262,144 selection-seed checks matched host reconstruction per arm; all 512 pixels accepted history.", 13, "#334155"),
                  text(82, 864, "z uses an iid Bernoulli approximation despite spatial correlation. Synthetic holdout diagnostics—not a production-frame image or bias proof.", 13, "#64748b"),
                  '</svg>'])
    Path(output_path).write_text("\n".join(parts), encoding="utf-8")

if __name__ == "__main__":
    if len(sys.argv) not in (4, 5):
        raise SystemExit(f"Usage: {Path(sys.argv[0]).name} PROVENANCE_LOG SELECTION_LOG PATH_LOG [CHART.svg]")
    try:
        summaries = run(*sys.argv[1:4])
        if len(sys.argv) == 5:
            write_chart(sys.argv[4], summaries)
            print(f"VISUAL {sys.argv[4]}")
    except (KeyError, ValueError, ZeroDivisionError) as error:
        print(f"FAIL ReplayTemporalProvenanceSummary: {error}", file=sys.stderr)
        raise SystemExit(2)
