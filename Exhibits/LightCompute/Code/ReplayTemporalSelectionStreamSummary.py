#!/usr/bin/env python3
"""Audit the paired deterministic selection-stream holdout log and compute paired contrasts."""
from collections import defaultdict
from pathlib import Path
import math
import re
import sys

MODES = ("fresh1", "fresh2", "fresh4", "temporal1", "temporal4")
FLOORS = ("default", "lower")
ARMS = ("baseline", "decorrelated")
COHORTS = 128
PIXELS = 512
NUMBER = re.compile(r"^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$")

def parse(line):
    return dict(re.findall(r"([A-Za-z_]+)=([^\s]+)", line))

def num(row, key):
    value = row[key]
    if not NUMBER.fullmatch(value):
        raise ValueError(f"non-numeric {key}={value}")
    result = float(value)
    if not math.isfinite(result):
        raise ValueError(f"non-finite {key}={value}")
    return result

def stats(values):
    mean = sum(values) / len(values)
    if len(values) < 2:
        return mean, 0.0
    variance = sum((value - mean) ** 2 for value in values) / (len(values) - 1)
    return mean, math.sqrt(variance / len(values))

def main(path):
    lines = Path(path).read_text(errors="replace").splitlines()
    batches, results, controls, exits = {}, {}, {}, {}
    for line in lines:
        if not line.startswith("REPLAY_SELECTION_STREAM_"):
            continue
        row = parse(line)
        tag = line.split(" ", 1)[0]
        if tag == "REPLAY_SELECTION_STREAM_BATCH":
            key = (row["arm"], row["mode"], row["floor"], int(row["batch"]))
            if key in batches:
                raise ValueError(f"duplicate batch {key}")
            batches[key] = row
        elif tag == "REPLAY_SELECTION_STREAM_RESULT":
            key = (row["arm"], row["mode"], row["floor"])
            if key in results:
                raise ValueError(f"duplicate result {key}")
            results[key] = row
        elif tag == "REPLAY_SELECTION_STREAM_CONTROL":
            if row["arm"] in controls:
                raise ValueError(f"duplicate control {row['arm']}")
            controls[row["arm"]] = row
        elif tag == "REPLAY_SELECTION_STREAM_ARM_EXIT":
            exits[row["arm"]] = int(row["status"])

    expected_keys = {(arm, mode, floor, cohort) for arm in ARMS for mode in MODES
                     for floor in FLOORS for cohort in range(COHORTS)}
    if set(batches) != expected_keys:
        raise ValueError(f"batch coverage mismatch: got {len(batches)}, expected {len(expected_keys)}")
    result_keys = {(arm, mode, floor) for arm in ARMS for mode in MODES for floor in FLOORS}
    if set(results) != result_keys:
        raise ValueError(f"summary coverage mismatch: got {len(results)}, expected {len(result_keys)}")
    if set(controls) != set(ARMS) or set(exits) != set(ARMS):
        raise ValueError("missing arm control or process-exit marker")

    for arm in ARMS:
        control = controls[arm]
        expected = COHORTS * len(MODES) * len(FLOORS) * 1024
        if int(control["path_seed_checks"]) != expected or int(control["selection_seed_checks"]) != expected:
            raise ValueError(f"{arm}: incomplete seed-stream validation")
        if int(control["continued_frames"]) != COHORTS * 2 * len(FLOORS) * 3:
            raise ValueError(f"{arm}: temporal history continuation count mismatch")
        if int(control["minimum_accepted"]) != 1024 or int(control["cap"]) != 1:
            raise ValueError(f"{arm}: temporal cap/acceptance mismatch")

    paired_selection_different = paired_fresh1_equal = 0
    for mode in MODES:
        for floor in FLOORS:
            for cohort in range(COHORTS):
                baseline = batches[("baseline", mode, floor, cohort)]
                candidate = batches[("decorrelated", mode, floor, cohort)]
                if baseline["selection_hash"] != candidate["selection_hash"]:
                    paired_selection_different += 1
                if mode == "fresh1" and baseline["hash"] == candidate["hash"]:
                    paired_fresh1_equal += 1
    total_pairs = len(MODES) * len(FLOORS) * COHORTS
    if paired_selection_different < total_pairs - 2:
        raise ValueError(f"selection-stream hashes differed in only {paired_selection_different}/{total_pairs} paired batches")
    if paired_fresh1_equal != COHORTS * len(FLOORS):
        raise ValueError(f"fresh1 control images matched in only {paired_fresh1_equal}/{COHORTS * len(FLOORS)} batches")

    failed = {arm: [] for arm in ARMS}
    for arm in ARMS:
        for mode in MODES:
            for floor in FLOORS:
                key = (arm, mode, floor)
                batch_rows = [batches[(arm, mode, floor, cohort)] for cohort in range(COHORTS)]
                summary = results[key]
                reference = num(summary, "reference")
                means = [num(row, "mean") for row in batch_rows]
                mses = [num(row, "mse") for row in batch_rows]
                mean, mean_se = stats(means)
                mse, mse_se = stats(mses)
                rmse = math.sqrt(mse)
                max_pixel = max(num(row, "max_pixel") for row in batch_rows)
                final_rays = num(summary, "final_rays")
                warm_rays = num(summary, "warm_rays")
                triangles = num(summary, "triangles")
                gate = abs(mean - reference) <= 5 * mean_se + .001
                for field, actual in (("mean", mean), ("mean_se", mean_se), ("mse", mse),
                                      ("mse_se", mse_se), ("rmse", rmse), ("max_pixel", max_pixel),
                                      ("final_rays", final_rays), ("warm_rays", warm_rays),
                                      ("triangles", triangles)):
                    if not math.isclose(num(summary, field), actual, rel_tol=2e-9, abs_tol=2e-10):
                        raise ValueError(f"{key}: logged {field} disagrees with its 128 cohorts")
                if int(summary["mean_gate"]) != int(gate) or int(summary["cohorts"]) != COHORTS:
                    raise ValueError(f"{key}: gate/count mismatch")
                if not gate:
                    failed[arm].append(f"{mode}/{floor}")
                print(f"RESULT arm={arm:12s} mode={mode:9s} floor={floor:7s} ref={reference:.9f} "
                      f"mean={mean:.9f}±{mean_se:.9f} RMSE={rmse:.6f} MSE_SE={mse_se:.7f} "
                      f"rays(final/warm)={final_rays:.1f}/{warm_rays:.1f} gate={'PASS' if gate else 'FAIL'}")

    for mode in MODES:
        for floor in FLOORS:
            key = ("baseline", mode, floor)
            other = ("decorrelated", mode, floor)
            mean_differences = [num(batches[("decorrelated", mode, floor, cohort)], "mean") -
                                num(batches[("baseline", mode, floor, cohort)], "mean")
                                for cohort in range(COHORTS)]
            mse_differences = [num(batches[("decorrelated", mode, floor, cohort)], "mse") -
                               num(batches[("baseline", mode, floor, cohort)], "mse")
                               for cohort in range(COHORTS)]
            mean_delta, mean_delta_se = stats(mean_differences)
            mse_delta, mse_delta_se = stats(mse_differences)
            print(f"PAIRED mode={mode:9s} floor={floor:7s} delta_mean={mean_delta:+.8f}±{mean_delta_se:.8f} "
                  f"delta_MSE={mse_delta:+.8f}±{mse_delta_se:.8f} "
                  f"delta_RMSE={math.sqrt(num(results[other], 'mse')) - math.sqrt(num(results[key], 'mse')):+.6f}")

    print(f"CONTROL paired_selection_hashes_different={paired_selection_different}/{total_pairs} "
          f"fresh1_image_hashes_equal={paired_fresh1_equal}/{COHORTS * len(FLOORS)}")
    for arm in ARMS:
        expected_status = int(bool(failed[arm]))
        if exits[arm] != expected_status:
            raise ValueError(f"{arm}: process status {exits[arm]} does not match mean gates {failed[arm]}")
        print(f"ARM_GATE arm={arm} failed={','.join(failed[arm]) or 'none'} exit={exits[arm]} verified=1")

    # Operational comparison: both candidates have the same last-frame path-seed stream.
    for floor in FLOORS:
        a = results[("baseline", "temporal1", floor)]
        b = results[("baseline", "fresh2", floor)]
        c = results[("decorrelated", "temporal1", floor)]
        print(f"WORK floor={floor} baseline_temporal1_vs_fresh2 final_rays={num(a,'final_rays'):.1f}/"
              f"{num(b,'final_rays'):.1f} warm_rays={num(a,'warm_rays'):.1f}/0 "
              f"decorrelated_temporal1_final_warm={num(c,'final_rays'):.1f}/{num(c,'warm_rays'):.1f}")
    if any(failed.values()):
        return 1
    print("PASS ReplayTemporalSelectionStreamSummary: paired cohort audit and all mean gates passed")
    return 0

if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(f"Usage: {Path(sys.argv[0]).name} RESULT_LOG")
    try:
        raise SystemExit(main(sys.argv[1]))
    except (KeyError, ValueError, IndexError) as error:
        print(f"FAIL ReplayTemporalSelectionStreamSummary: {error}", file=sys.stderr)
        raise SystemExit(2)
