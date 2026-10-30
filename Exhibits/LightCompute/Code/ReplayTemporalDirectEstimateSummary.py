#!/usr/bin/env python3
"""Validate fresh/temporal direct-emitter image means against the CPU area integral."""
from pathlib import Path
import math
import re
import sys

MODES = ("fresh1", "fresh4", "temporal1", "temporal4")
GRID = 32
PIXEL_COUNT = sum(1 for p in range(GRID * GRID) if (p % GRID + p // GRID) % 2 == 1)


def fields(line):
    return dict(re.findall(r"([A-Za-z_][A-Za-z_0-9]*)=([^\s]+)", line))


def number(row, key):
    value = float(row[key])
    if not math.isfinite(value):
        raise ValueError(f"non-finite {key}={row[key]}")
    return value


def integer(row, key):
    value = int(row[key])
    if value < 0:
        raise ValueError(f"negative {key}={value}")
    return value


def load(path, scenario=None):
    headers, fixture_headers, cohorts, results, passes = {}, {}, {}, {}, {}
    for line in Path(path).read_text(errors="replace").splitlines():
        if line.startswith("REPLAY_TEMPORAL_DIRECT_ESTIMATE fixture="):
            row = fields(line)
            visibility = row.get("visibility", "blocked")
            if visibility in fixture_headers:
                raise ValueError(f"duplicate direct estimate fixture metadata for {visibility}")
            fixture_headers[visibility] = row
        elif line.startswith("REPLAY_TEMPORAL_DIRECT_ESTIMATE pipelines_ready="):
            row = fields(line)
            visibility = row.get("visibility", "blocked")
            if visibility in headers:
                raise ValueError(f"duplicate direct estimate header for {visibility}")
            headers[visibility] = row
        elif line.startswith("REPLAY_TEMPORAL_DIRECT_ESTIMATE_COHORT "):
            row = fields(line)
            visibility = row.get("visibility", "blocked")
            key = (visibility, row["mode"], integer(row, "cohort"))
            if key in cohorts:
                raise ValueError(f"duplicate direct estimate cohort {key}")
            cohorts[key] = row
        elif line.startswith("REPLAY_TEMPORAL_DIRECT_ESTIMATE_RESULT "):
            row = fields(line)
            visibility = row.get("visibility", "blocked")
            key = (visibility, row["mode"])
            if key in results:
                raise ValueError(f"duplicate direct estimate result {key}")
            results[key] = row
        elif line.startswith("PASS ReplayTemporalDirectEmitterEstimateValidation "):
            row = fields(line)
            visibility = row.get("visibility", "blocked")
            if visibility in passes:
                raise ValueError(f"duplicate direct estimate pass marker for {visibility}")
            passes[visibility] = row

    if not headers or set(headers) != set(passes) or set(headers) != {key[0] for key in results}:
        raise ValueError("direct estimate scenario headers, mode results, or PASS coverage incomplete")
    if set(headers) != {key[0] for key in cohorts}:
        raise ValueError("direct estimate scenario cohort coverage incomplete")
    if fixture_headers and set(fixture_headers) != set(headers):
        raise ValueError("fixture and result header disagree about direct visibility")
    if scenario is None:
        if len(headers) != 1:
            raise ValueError("select a direct-visibility scenario when the log contains multiple scenarios")
        scenario = next(iter(headers))
    if scenario not in headers:
        raise ValueError(f"direct estimate scenario not found: {scenario}")
    if scenario not in ("blocked", "clear", "base_only", "added_only"):
        raise ValueError(f"unknown direct-visibility scenario: {scenario}")

    header = headers[scenario]
    fixture = fixture_headers.get(scenario)
    batches = integer(header, "cohorts")
    if (batches < 2 or integer(header, "pixels_per_cohort") != PIXEL_COUNT or
            header.get("scope") != "nee_only" or header.get("bsdf_continuation_excluded") != "1" or
            header.get("total_radiance_claim") != "0"):
        raise ValueError("direct estimate header has invalid dimensions or an ambiguous radiance scope")
    if fixture:
        fixture_motion = fixture.get("motion")
        header_motion = header.get("motion")
        if fixture_motion and header_motion and fixture_motion != header_motion:
            raise ValueError("fixture and result header disagree about receiver motion")
        if fixture.get("visibility", scenario) != scenario:
            raise ValueError("fixture and result header disagree about direct visibility")
    motion = ((fixture.get("motion") if fixture else None) or header.get("motion") or "moving")
    if motion not in ("moving", "stationary"):
        raise ValueError(f"unknown reference motion scenario: {motion}")
    expected = {(scenario, mode, cohort) for mode in MODES for cohort in range(batches)}
    actual = {key for key in cohorts if key[0] == scenario}
    if actual != expected:
        raise ValueError(f"direct estimate cohort coverage {len(actual)}/{len(expected)} for {scenario}")
    pass_row = passes[scenario]
    if (integer(pass_row, "cohorts") != batches or integer(pass_row, "modes") != len(MODES) or
            pass_row.get("independent_area_reference") != "1" or pass_row.get("scope") != "nee_only" or
            pass_row.get("estimator_math_changed") != "0"):
        raise ValueError("direct estimate pass marker is invalid")

    reference = number(header, "reference_luma")
    coarse = number(header, "reference_coarse_luma")
    tolerance = number(header, "gate_tolerance")
    if reference <= 0 or coarse <= 0 or tolerance <= 0:
        raise ValueError("direct estimate reference or tolerance is invalid")
    summaries = {}
    for mode in MODES:
        rows = [cohorts[(scenario, mode, cohort)] for cohort in range(batches)]
        means, mses = [], []
        for row in rows:
            if integer(row, "pixels") != PIXEL_COUNT:
                raise ValueError(f"{mode}: cohort pixel count is invalid")
            mean = number(row, "mean_luma")
            mse = number(row, "mse")
            if mean < 0 or mse < 0:
                raise ValueError(f"{mode}: negative image estimate or MSE")
            means.append(mean)
            mses.append(mse)
        mean = sum(means) / batches
        variance = sum((x - mean) ** 2 for x in means) / (batches - 1)
        standard_error = math.sqrt(max(0.0, variance) / batches)
        rmse = math.sqrt(sum(mses) / batches)
        mean_gate = abs(mean - reference) <= 6.0 * standard_error + tolerance
        result = results[(scenario, mode)]
        checks = {
            "cohorts": batches,
            "pixels_per_cohort": PIXEL_COUNT,
            "reference_luma": reference,
            "reference_coarse_luma": coarse,
            "mean_luma": mean,
            "batch_se": standard_error,
            "rmse": rmse,
            "gate_tolerance": tolerance,
            "mean_gate": int(mean_gate),
        }
        for key, expected_value in checks.items():
            if key in ("mean_gate", "cohorts", "pixels_per_cohort"):
                actual_value = integer(result, key)
                if actual_value != expected_value:
                    raise ValueError(f"{mode}: aggregate {key} disagrees with cohort records")
            elif not math.isclose(number(result, key), expected_value, rel_tol=5e-7, abs_tol=5e-9):
                raise ValueError(f"{mode}: aggregate {key} disagrees with cohort records")
        summaries[mode] = {"mean": mean, "se": standard_error, "rmse": rmse,
                           "gate": mean_gate, "reference": reference, "tolerance": tolerance,
                           "motion": motion, "visibility": scenario}
        print(f"DIRECT_ESTIMATE motion={motion} visibility={scenario} mode={mode} "
              f"mean_luma={mean:.12g} batch_se={standard_error:.9g} rmse={rmse:.9g} "
              f"reference_luma={reference:.12g} mean_gate={int(mean_gate)}")
    all_gates = int(all(summaries[mode]["gate"] for mode in MODES))
    if integer(pass_row, "all_mean_gates") != all_gates:
        raise ValueError("direct estimate PASS gate summary disagrees with mode results")
    if abs(reference - coarse) > tolerance:
        raise ValueError("direct emitter reference failed the recorded coarse/fine convergence tolerance")
    return summaries

def chart(path, summaries):
    width, height = 1180, 560
    left, right = 245, 840
    maximum = max(max(row["mean"] + 6 * row["se"], row["reference"]) for row in summaries.values())
    maximum = max(maximum * 1.08, 1e-12)
    scale = (right - left) / maximum
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
             '<rect width="100%" height="100%" fill="#f8fafc"/>',
             '<rect x="18" y="18" width="1144" height="524" rx="16" fill="white" stroke="#dbe3ee"/>',
             '<text x="48" y="62" font-family="Inter,Segoe UI,Arial,sans-serif" font-size="25" font-weight="700" fill="#0f172a">Fresh vs temporal direct-emitter estimates</text>',
             '<text x="48" y="91" font-family="Arial,sans-serif" font-size="14" fill="#475569">Reservoir-weighted direct NEE luminance · independent CPU area integral · error bars ±6 batch SE</text>']
    reference = next(iter(summaries.values()))["reference"]
    motion = next(iter(summaries.values()))["motion"]
    visibility = next(iter(summaries.values()))["visibility"]
    reference_x = left + reference * scale
    parts[3] = parts[3].replace("Fresh vs temporal direct-emitter estimates",
                                f"Fresh vs temporal NEE · {motion} / {visibility}")
    parts.append(f'<line x1="{reference_x:.2f}" y1="130" x2="{reference_x:.2f}" y2="445" stroke="#0f172a" stroke-width="2" stroke-dasharray="7 5"/>')
    parts.append(f'<text x="{reference_x:.2f}" y="122" text-anchor="middle" font-family="Arial" font-size="12" fill="#0f172a">CPU reference {reference:.6g}</text>')
    colors = {"fresh1": "#2563eb", "fresh4": "#0ea5e9", "temporal1": "#d97706", "temporal4": "#dc2626"}
    for index, mode in enumerate(MODES):
        row = summaries[mode]
        y = 165 + index * 78
        x = left + row["mean"] * scale
        low = left + max(0.0, row["mean"] - 6 * row["se"]) * scale
        high = left + (row["mean"] + 6 * row["se"]) * scale
        parts.append(f'<text x="48" y="{y+5}" font-family="Arial" font-size="16" font-weight="700" fill="#0f172a">{mode}</text>')
        parts.append(f'<line x1="{low:.2f}" y1="{y}" x2="{high:.2f}" y2="{y}" stroke="{colors[mode]}" stroke-width="3"/>')
        parts.append(f'<line x1="{low:.2f}" y1="{y-7}" x2="{low:.2f}" y2="{y+7}" stroke="{colors[mode]}" stroke-width="2"/>')
        parts.append(f'<line x1="{high:.2f}" y1="{y-7}" x2="{high:.2f}" y2="{y+7}" stroke="{colors[mode]}" stroke-width="2"/>')
        parts.append(f'<circle cx="{x:.2f}" cy="{y}" r="7" fill="{colors[mode]}"/>')
        status = "within mean gate" if row["gate"] else "outside mean gate"
        parts.append(f'<text x="880" y="{y-4}" font-family="Arial" font-size="13" fill="#334155">mean {row["mean"]:.6g} · RMSE {row["rmse"]:.5g}</text>')
        parts.append(f'<text x="880" y="{y+16}" font-family="Arial" font-size="12" fill="{"#15803d" if row["gate"] else "#b91c1c"}">{status}</text>')
    parts.extend(['<line x1="245" y1="445" x2="1085" y2="445" stroke="#94a3b8"/>',
                  '<text x="48" y="485" font-family="Arial" font-size="12" fill="#475569">NEE contribution only: BSDF continuation, emitter-hit and environment terms are excluded.</text>',
                  f'<text x="48" y="507" font-family="Arial" font-size="12" fill="#64748b">Fixture-level {motion}-receiver/{visibility} visibility; temporal modes replay four frames.</text>',
                  '</svg>'])
    Path(path).write_text("\n".join(parts) + "\n", encoding="utf-8")


def run(log, svg, scenario=None):
    summaries = load(log, scenario)
    chart(svg, summaries)
    print(f"VISUAL {svg}")


if __name__ == "__main__":
    if len(sys.argv) not in (3, 4):
        raise SystemExit(f"Usage: {Path(sys.argv[0]).name} RUN_LOG CHART.svg [SCENARIO]")
    try:
        run(*sys.argv[1:])
    except (KeyError, ValueError, ZeroDivisionError) as error:
        print(f"FAIL ReplayTemporalDirectEstimateSummary: {error}", file=sys.stderr)
        raise SystemExit(2)
