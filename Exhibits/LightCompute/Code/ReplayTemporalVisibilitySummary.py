#!/usr/bin/env python3
"""Validate per-pixel temporal path visibility readbacks and compare against prior baselines."""
from pathlib import Path
import math
import re
import sys

MODES = ("temporal1", "temporal4")
COHORTS = 128
GRID = 32
PIXELS = GRID * GRID // 2


def fields(line):
    return dict(re.findall(r"([A-Za-z_]+)=([^\s]+)", line))


def f(row, key):
    value = float(row[key])
    if not math.isfinite(value):
        raise ValueError(f"non-finite {key}={row[key]}")
    return value


def i(row, key):
    value = int(row[key])
    if value < 0:
        raise ValueError(f"negative {key}={value}")
    return value


def mean(xs):
    return sum(xs) / len(xs) if xs else 0.0


def moments(xs):
    m = mean(xs)
    if len(xs) < 2:
        return m, 0.0
    return m, math.sqrt(sum((x - m) ** 2 for x in xs) / (len(xs) - 1) / len(xs))


def load_run(path):
    pixels, cohorts, results, exits = {}, {}, {}, []
    for line in Path(path).read_text(errors="replace").splitlines():
        if line.startswith("REPLAY_TEMPORAL_VISIBILITY_PIXEL "):
            row = fields(line)
            key = (row["mode"], int(row["cohort"]), int(row["pixel"]))
            if key in pixels:
                raise ValueError(f"duplicate per-pixel row {key}")
            pixels[key] = row
        elif line.startswith("REPLAY_TEMPORAL_VISIBILITY_COHORT "):
            row = fields(line)
            key = (row["mode"], int(row["cohort"]))
            if key in cohorts:
                raise ValueError(f"duplicate cohort summary {key}")
            cohorts[key] = row
        elif line.startswith("REPLAY_TEMPORAL_VISIBILITY_RESULT "):
            row = fields(line)
            if row["mode"] in results:
                raise ValueError(f"duplicate mode result {row['mode']}")
            results[row["mode"]] = row
        elif line.startswith("REPLAY_TEMPORAL_VISIBILITY_EXIT "):
            exits.append(int(fields(line)["status"]))
    expected_pixels = [p for p in range(GRID * GRID) if (p % GRID + p // GRID) % 2 == 1]
    expected = {(mode, cohort, pixel) for mode in MODES for cohort in range(COHORTS) for pixel in expected_pixels}
    expected_cohorts = {(mode, cohort) for mode in MODES for cohort in range(COHORTS)}
    if set(pixels) != expected:
        raise ValueError(f"per-pixel coverage {len(pixels)}/{len(expected)}")
    if set(cohorts) != expected_cohorts or set(results) != set(MODES):
        raise ValueError("cohort or mode-summary coverage incomplete")
    if exits != [0]:
        raise ValueError(f"run did not have one successful exit marker: {exits}")
    return pixels, cohorts, results


def summarize(pixels, cohorts, results):
    summaries = {}
    for mode in MODES:
        pixel_ids = [p for p in range(GRID * GRID) if (p % GRID + p // GRID) % 2 == 1]
        rows = [pixels[(mode, c, p)] for c in range(COHORTS) for p in pixel_ids]
        direct_rays = sum(i(r, "direct_rays") for r in rows)
        direct_visible = sum(i(r, "direct_visible") for r in rows)
        bsdf_miss = sum(i(r, "bsdf_miss") for r in rows)
        opaque_hits = sum(i(r, "first_opaque_hit") for r in rows)
        emitter_hits = sum(i(r, "emitter_hits") for r in rows)
        if direct_visible > direct_rays:
            raise ValueError(f"{mode}: more visible direct segments than traced segments")
        traced_path = [r for r in rows if i(r, "bsdf_miss") + i(r, "first_opaque_hit") + i(r, "emitter_hits") > 0]
        visible = [r for r in traced_path if i(r, "bsdf_miss") + i(r, "emitter_hits") > 0]
        occluded = [r for r in traced_path if i(r, "first_opaque_hit") > 0]
        unclassified = len(rows) - len(traced_path)
        for row in rows:
            history_weight = f(row, "history_weight")
            target = f(row, "target")
            source_weight = f(row, "source_weight")
            source_count = f(row, "source_count")
            retained = f(row, "retained")
            if not math.isfinite(source_count) or source_count < retained or retained < 1:
                raise ValueError(f"{mode}: invalid retained candidate count")
            expected_weight = target * source_weight * retained
            if not math.isclose(history_weight, expected_weight, rel_tol=5e-6, abs_tol=5e-7):
                raise ValueError(f"{mode}: temporal weight not target × source weight × retained count")
            probability = f(row, "selection_probability")
            fresh = f(row, "fresh_weight")
            if not math.isclose(probability, history_weight / (fresh + history_weight), rel_tol=5e-6, abs_tol=5e-7):
                raise ValueError(f"{mode}: roulette probability disagrees with fresh/history weight totals")
        cohort_means = [f(cohorts[(mode, c)], "mean") for c in range(COHORTS)]
        cohort_mse = [f(cohorts[(mode, c)], "mse") for c in range(COHORTS)]
        cohort_mean, cohort_se = moments(cohort_means)
        cohort_rmse = math.sqrt(mean(cohort_mse))
        transitions = {
            "prior_lit_current_blocked": sum(f(r, "previous_path_luma") > 1e-8 and f(r, "path_luma") <= 1e-8 for r in rows),
            "prior_blocked_current_lit": sum(f(r, "previous_path_luma") <= 1e-8 and f(r, "path_luma") > 1e-8 for r in rows),
            "stable_lit": sum(f(r, "previous_path_luma") > 1e-8 and f(r, "path_luma") > 1e-8 for r in rows),
            "stable_blocked": sum(f(r, "previous_path_luma") <= 1e-8 and f(r, "path_luma") <= 1e-8 for r in rows),
        }
        result = results[mode]
        for field, value in (("mean", cohort_mean), ("mean_se", cohort_se), ("rmse", cohort_rmse)):
            if not math.isclose(f(result, field), value, rel_tol=5e-7, abs_tol=5e-8):
                raise ValueError(f"{mode}: mode result {field} does not recompute from cohorts")
        summaries[mode] = {
            "pixels": len(rows), "direct_rays": direct_rays, "direct_visible": direct_visible,
            "direct_visibility_rate": direct_visible / direct_rays if direct_rays else 0.0,
            "path_visible_rate": (bsdf_miss + emitter_hits) / (bsdf_miss + opaque_hits + emitter_hits)
                if bsdf_miss + opaque_hits + emitter_hits else 0.0,
            "bsdf_miss": bsdf_miss, "opaque_hits": opaque_hits, "emitter_hits": emitter_hits,
            "unclassified": unclassified, "visible_samples": len(visible),
            "occluded_samples": len(occluded),
            "visible_target": mean([f(r, "target") for r in visible]),
            "occluded_target": mean([f(r, "target") for r in occluded]),
            "visible_weight": mean([f(r, "history_weight") for r in visible]),
            "occluded_weight": mean([f(r, "history_weight") for r in occluded]),
            "visible_nee": mean([f(r, "nee_luma") for r in visible]),
            "occluded_nee": mean([f(r, "nee_luma") for r in occluded]),
            "visible_emitter": mean([f(r, "emitter_luma") for r in visible]),
            "occluded_emitter": mean([f(r, "emitter_luma") for r in occluded]),
            "mean_emitter_hits": mean([f(r, "emitter_hits") for r in rows]),
            "selected": sum(i(r, "selected") for r in rows),
            "mean": cohort_mean, "mean_se": cohort_se, "rmse": cohort_rmse,
            "mean_path_luma": mean([f(r, "path_luma") for r in rows]),
            "mean_history_weight_all": mean([f(r, "history_weight") for r in rows]),
            **transitions,
        }
        if int(f(result, "direct_rays")) != direct_rays or int(f(result, "direct_visible")) != direct_visible:
            raise ValueError(f"{mode}: aggregate ray counts disagree with per-pixel rows")
        if int(f(result, "bsdf_miss")) != bsdf_miss or int(f(result, "first_opaque_hit")) != opaque_hits or \
           int(f(result, "emitter_hits")) != emitter_hits:
            raise ValueError(f"{mode}: primary-ray outcomes disagree with per-pixel rows")
        if int(f(result, "selected")) != summaries[mode]["selected"]:
            raise ValueError(f"{mode}: selected-history count disagrees with per-pixel rows")
        for field, value in transitions.items():
            if int(f(result, field)) != value:
                raise ValueError(f"{mode}: {field} disagrees with per-pixel prior/current path outcomes")
        if int(f(result, "visible_samples")) != len(visible) or int(f(result, "occluded_samples")) != len(occluded):
            raise ValueError(f"{mode}: visible/occluded sample counts disagree")
        print(f"VISIBILITY mode={mode} pixels={len(rows)} bsdf_rays={bsdf_miss+opaque_hits+emitter_hits} "
              f"environment_miss={bsdf_miss} opaque_hit={opaque_hits} emitter_hit={emitter_hits} "
              f"path_visible_rate={summaries[mode]['path_visible_rate']:.6f} direct_rays={direct_rays} "
              f"direct_visible={direct_visible} direct_visibility_rate={summaries[mode]['direct_visibility_rate']:.6f} "
              f"unclassified={unclassified} visible_samples={len(visible)} occluded_samples={len(occluded)} "
              f"visible_mean_target={summaries[mode]['visible_target']:.9g} "
              f"occluded_mean_target={summaries[mode]['occluded_target']:.9g} "
              f"visible_mean_history_weight={summaries[mode]['visible_weight']:.9g} "
              f"occluded_mean_history_weight={summaries[mode]['occluded_weight']:.9g} "
              f"visible_mean_NEE={summaries[mode]['visible_nee']:.9g} "
              f"occluded_mean_NEE={summaries[mode]['occluded_nee']:.9g} "
              f"visible_mean_emitter_luma={summaries[mode]['visible_emitter']:.9g} "
              f"occluded_mean_emitter_luma={summaries[mode]['occluded_emitter']:.9g} "
              f"mean_emitter_hits={summaries[mode]['mean_emitter_hits']:.6g} "
              f"radiance_state_transitions=nonzero→zero:{transitions['prior_lit_current_blocked']},"
              f"zero→nonzero:{transitions['prior_blocked_current_lit']},stable_nonzero:{transitions['stable_lit']},"
              f"stable_zero:{transitions['stable_blocked']} "
              f"mean={cohort_mean:.9f}±{cohort_se:.9f} RMSE={cohort_rmse:.6g}")
    return summaries


def prior_batches(path, prefix):
    rows = {}
    for line in Path(path).read_text(errors="replace").splitlines():
        if line.startswith(prefix + "_BATCH "):
            row = fields(line)
            if row.get("arm") == "baseline" and row.get("floor") == "lower" and row.get("mode") in MODES:
                key = (row["mode"], int(row["batch"]))
                if key in rows:
                    raise ValueError(f"duplicate prior baseline {prefix} {key}")
                rows[key] = row
    expected = {(mode, c) for mode in MODES for c in range(COHORTS)}
    if set(rows) != expected:
        raise ValueError(f"prior {prefix} coverage {len(rows)}/{len(expected)}")
    return rows


def compare_prior(cohorts, path_log, selection_log):
    path = prior_batches(path_log, "REPLAY_PATH_STREAM")
    selection = prior_batches(selection_log, "REPLAY_SELECTION_STREAM")
    for mode in MODES:
        hash_match = {"path": 0, "selection": 0}
        max_mean, max_mse = 0.0, 0.0
        for cohort in range(COHORTS):
            new = cohorts[(mode, cohort)]
            old_path, old_selection = path[(mode, cohort)], selection[(mode, cohort)]
            for label, old in (("path", old_path), ("selection", old_selection)):
                hash_match[label] += new["hash"] == old["hash"]
                max_mean = max(max_mean, abs(f(new, "mean") - f(old, "mean")))
                max_mse = max(max_mse, abs(f(new, "mse") - f(old, "mse")))
                if not math.isclose(f(new, "mean"), f(old, "mean"), rel_tol=1e-8, abs_tol=1e-9) or \
                   not math.isclose(f(new, "mse"), f(old, "mse"), rel_tol=1e-8, abs_tol=1e-9):
                    raise ValueError(f"{mode} cohort {cohort}: instrumented baseline changed prior mean/MSE")
        print(f"PRIOR_BASELINE mode={mode} image_hash_matches_path={hash_match['path']}/{COHORTS} "
              f"image_hash_matches_selection={hash_match['selection']}/{COHORTS} "
              f"max_abs_mean_delta={max_mean:.9g} max_abs_mse_delta={max_mse:.9g} "
              "hash mismatch cause remains unestablished")


def chart(path, s):
    W, H = 1400, 760
    colors = {"visible": "#15803d", "occluded": "#dc2626", "baseline": "#334155"}
    def txt(x, y, value, size=14, color="#334155", weight="400"):
        return f'<text x="{x}" y="{y}" font-family="Inter,Segoe UI,Arial,sans-serif" font-size="{size}" fill="{color}" font-weight="{weight}">{value}</text>'
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">',
             '<rect width="100%" height="100%" fill="#f8fafc"/>',
             '<rect x="22" y="20" width="1356" height="720" rx="20" fill="white" stroke="#dbe3ee"/>',
             txt(58, 72, "Temporal-history visibility → contribution attribution", 27, "#0f172a", "700"),
             txt(58, 105, "128 cohorts · 512 far-group history samples per cohort · moving visibility · SwiftShader Vulkan", 15, "#475569"),
             txt(66, 155, "A · First BSDF-ray outcome: environment miss vs opaque hit", 17, "#0f172a", "700"),
             txt(720, 155, "B · Target and temporal weight by first-ray outcome", 17, "#0f172a", "700")]
    for idx, mode in enumerate(MODES):
        y = 220 + idx * 100
        path_rays = s[mode]["bsdf_miss"] + s[mode]["opaque_hits"] + s[mode]["emitter_hits"]
        visible_fraction = (s[mode]["bsdf_miss"] + s[mode]["emitter_hits"]) / path_rays if path_rays else 0
        label = "Temporal 1" if mode == "temporal1" else "Temporal 4"
        parts.append(txt(70, y + 5, label, 14, "#334155", "600"))
        parts.append(f'<rect x="205" y="{y-18}" width="400" height="30" rx="6" fill="{colors["occluded"]}"/>')
        parts.append(f'<rect x="205" y="{y-18}" width="{400*visible_fraction:.2f}" height="30" rx="6" fill="{colors["visible"]}"/>')
        parts.append(txt(620, y + 4, f"{visible_fraction:.1%} miss/emitter · {path_rays} traced rays", 14))
        parts.append(txt(742, y + 4, f"Miss target {s[mode]['visible_target']:.5g}", 13, colors["visible"], "600"))
        parts.append(txt(742, y + 31, f"Opaque target {s[mode]['occluded_target']:.5g}", 13, colors["occluded"], "600"))
        parts.append(txt(995, y + 4, f"Miss history wt. {s[mode]['visible_weight']:.5g}", 13, colors["visible"], "600"))
        parts.append(txt(995, y + 31, f"Opaque history wt. {s[mode]['occluded_weight']:.5g}", 13, colors["occluded"], "600"))
    parts.extend(['<rect x="58" y="445" width="1284" height="245" rx="15" fill="#f1f5f9" stroke="#cbd5e1"/>',
                  txt(82, 485, "C · Temporal estimator and weight accounting", 18, "#0f172a", "700")])
    for idx, mode in enumerate(MODES):
        y = 525 + idx * 48
        label = "Temporal 1" if mode == "temporal1" else "Temporal 4"
        parts.append(txt(82, y, f"{label}: mean {s[mode]['mean']:.6f} ± {s[mode]['mean_se']:.6f}  |  RMSE {s[mode]['rmse']:.5f}  |  "
                              f"visible/occluded sample n={s[mode]['visible_samples']}/{s[mode]['occluded_samples']}  |  "
                              f"mean history weight visible/occluded={s[mode]['visible_weight']:.5g}/{s[mode]['occluded_weight']:.5g}  |  "
                              f"same-seed radiance states nonzero→zero={s[mode]['prior_lit_current_blocked']}, "
                              f"zero→nonzero={s[mode]['prior_blocked_current_lit']}", 12))
    parts.extend([txt(82, 642, "All 131,072 prior/current seed pairs matched; history weight recomputed from target × stored source weight × retained count.", 13),
                  txt(82, 668, "No emissive facets: direct-NEE rays = 0. First BSDF ray is measured; zero-radiance state is only a transition proxy for 7 temporal-1 untraced samples.", 12, "#64748b"),
                  '</svg>'])
    Path(path).write_text("\n".join(parts), encoding="utf-8")


def run(log, path_log, selection_log, svg=None):
    pixels, cohorts, results = load_run(log)
    summaries = summarize(pixels, cohorts, results)
    compare_prior(cohorts, path_log, selection_log)
    if svg:
        chart(svg, summaries)
        print(f"VISUAL {svg}")
    print("NOTICE This fixture has no emissive facets: direct-NEE segment traces and contributions are zero. The measured ray is the replayed temporal path's first BSDF continuation (environment miss, opaque hit, or emitter hit).")
    print("NOTICE Prior/current same-seed transitions compare path-radiance zero/nonzero; seven temporal-1 current samples had no recorded first-ray event, so zero is not always an opaque hit. This is descriptive attribution, not a correction or bias proof.")


if __name__ == "__main__":
    if len(sys.argv) not in (4, 5):
        raise SystemExit(f"Usage: {Path(sys.argv[0]).name} VISIBILITY_LOG PATH_STUDY_LOG SELECTION_STUDY_LOG [CHART.svg]")
    try:
        run(*sys.argv[1:])
    except (KeyError, ValueError, ZeroDivisionError) as error:
        print(f"FAIL ReplayTemporalVisibilitySummary: {error}", file=sys.stderr)
        raise SystemExit(2)
